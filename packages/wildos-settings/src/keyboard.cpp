#include "keyboard.h"

#include "typingmethods.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QCollator>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QXmlStreamReader>

#include <algorithm>

namespace
{
// What people search for besides a layout's own name: the countries and
// languages xkeyboard-config lists for it (Belgium, Dutch, French for
// "Belgian"), its code, and the letter order printed on the keys.
QString searchWords(const QString &layout, const QString &description, const QStringList &countries, const QStringList &languages)
{
    QStringList words{layout};
    for (const QString &code : countries) {
        const QLocale::Territory territory = QLocale::codeToTerritory(code);
        if (territory != QLocale::AnyTerritory) {
            words << QLocale::territoryToString(territory);
        }
    }
    for (const QString &code : languages) {
        const QLocale::Language language = QLocale::codeToLanguage(code, QLocale::ISO639Part2);
        if (language != QLocale::AnyLanguage) {
            words << QLocale::languageToString(language);
        }
    }
    static const QStringList azerty{QStringLiteral("be"), QStringLiteral("fr")};
    static const QStringList qwertz{QStringLiteral("de"), QStringLiteral("at"), QStringLiteral("ch"), QStringLiteral("cz"), QStringLiteral("sk"),
                                    QStringLiteral("hu"), QStringLiteral("si"), QStringLiteral("hr")};
    static const QRegularExpression otherOrder(QStringLiteral("qwerty|bepo|bépo|dvorak|colemak|workman"), QRegularExpression::CaseInsensitiveOption);
    if (!description.contains(otherOrder)) {
        if (azerty.contains(layout)) {
            words << QStringLiteral("azerty");
        } else if (qwertz.contains(layout)) {
            words << QStringLiteral("qwertz");
        }
    }
    return words.join(QLatin1Char(' ')).toLower();
}
}

namespace
{
const QString service = QStringLiteral("org.kde.keyboard");
const QString path = QStringLiteral("/Layouts");
const QString interface = QStringLiteral("org.kde.KeyboardLayouts");

// The switch-key and Caps Lock choices Settings offers; any other XKB
// option someone set in the full keyboard module is kept as it is.
const QStringList switchPrefixes = {QStringLiteral("grp:")};
const QStringList capsPrefixes = {QStringLiteral("caps:"), QStringLiteral("ctrl:nocaps"), QStringLiteral("ctrl:swapcaps")};
const QStringList composePrefixes = {QStringLiteral("compose:")};
const QStringList altGrPrefixes = {QStringLiteral("lv3:")};

// What KWin builds the keymap with when kxkbrc names no model.
const QString defaultModel = QStringLiteral("pc104");

// The system layout when the account has none of its own: what the
// installer wrote for the console and the sign-in screen.
QHash<QString, QString> systemKeyboard()
{
    QHash<QString, QString> values;
    QFile file(QStringLiteral("/etc/default/keyboard"));
    if (!file.open(QIODevice::ReadOnly)) {
        return values;
    }
    while (!file.atEnd()) {
        const QString line = QString::fromUtf8(file.readLine()).trimmed();
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq <= 0 || line.startsWith(QLatin1Char('#'))) {
            continue;
        }
        QString value = line.mid(eq + 1).trimmed();
        if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\'')))) {
            value = value.mid(1, value.size() - 2);
        }
        values.insert(line.left(eq).trimmed(), value);
    }
    return values;
}

QStringList splitList(const QString &value)
{
    return value.split(QLatin1Char(','), Qt::KeepEmptyParts);
}
}

Keyboard::Keyboard(QObject *parent)
    : QObject(parent)
{
    QDBusConnection::sessionBus().connect(service, path, interface, QStringLiteral("layoutChanged"), this, SLOT(onLayoutChanged(uint)));
    QDBusConnection::sessionBus().connect(service, path, interface, QStringLiteral("layoutListChanged"), this, SLOT(readCurrent()));
    reload();
}

void Keyboard::reload()
{
    const KConfigGroup layout(KSharedConfig::openConfig(QStringLiteral("kxkbrc"), KConfig::NoGlobals), QStringLiteral("Layout"));
    const bool own = layout.readEntry("Use", false);
    QStringList layouts = splitList(layout.readEntry("LayoutList", QString()));
    QStringList variants = splitList(layout.readEntry("VariantList", QString()));
    QStringList methods = splitList(layout.readEntry(TypingMethods::configKey(), QString()));
    m_options = layout.readEntry("Options", QString()).split(QLatin1Char(','), Qt::SkipEmptyParts);
    m_model = layout.readEntry("Model", QString());
    if (!own || layouts.join(QString()).isEmpty()) {
        const auto system = systemKeyboard();
        if (!own && m_model.isEmpty()) {
            m_model = system.value(QStringLiteral("XKBMODEL"));
        }
        layouts = splitList(system.value(QStringLiteral("XKBLAYOUT"), QStringLiteral("gb")));
        variants = splitList(system.value(QStringLiteral("XKBVARIANT")));
        methods.clear();
        if (!own) {
            m_options = system.value(QStringLiteral("XKBOPTIONS")).split(QLatin1Char(','), Qt::SkipEmptyParts);
        }
    }
    m_layouts.clear();
    for (int i = 0; i < layouts.size(); ++i) {
        const QString code = layouts.at(i).trimmed();
        if (!code.isEmpty()) {
            const QString method = methods.value(i).trimmed();
            m_layouts.append({code, variants.value(i).trimmed(), TypingMethods::engine(method).isEmpty() ? QString() : method});
        }
    }
    if (m_layouts.isEmpty()) {
        m_layouts.append({QStringLiteral("gb"), QString()});
    }
    if (m_model.isEmpty()) {
        m_model = defaultModel;
    }

    const KConfigGroup keys(KSharedConfig::openConfig(QStringLiteral("kcminputrc"), KConfig::NoGlobals), QStringLiteral("Keyboard"));
    m_numLock = keys.readEntry("NumLock", 2);
    m_repeat = keys.readEntry("KeyRepeat", QStringLiteral("repeat")) != QLatin1String("nothing");
    m_repeatDelay = keys.readEntry("RepeatDelay", 600);
    m_repeatRate = qRound(keys.readEntry("RepeatRate", 25.0));
    Q_EMIT changed();
    Q_EMIT methodChanged();
    readCurrent();
}

void Keyboard::loadNames()
{
    if (m_namesLoaded) {
        return;
    }
    m_namesLoaded = true;
    QFile file(QStringLiteral("/usr/share/X11/xkb/rules/evdev.xml"));
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    // <modelList><model><configItem><name>pc105</name><description>
    // Generic 105-key PC</description>... comes first, then <layoutList>
    // <layout><configItem><name>gb</name><description>English (UK)
    // </description></configItem><variantList><variant><configItem>...
    // per variant. The option groups after the layouts use the same
    // elements, so reading stops there.
    QXmlStreamReader xml(&file);
    QString section;
    QString layout;
    QString name;
    QString description;
    QStringList countries;
    QStringList languages;
    QStringList layoutCountries;
    QStringList layoutLanguages;
    bool inVariant = false;
    while (!xml.atEnd()) {
        xml.readNext();
        if (xml.isEndElement() && xml.name() == QLatin1String("layoutList")) {
            break;
        }
        if (xml.isStartElement()) {
            const auto tag = xml.name();
            if (tag == QLatin1String("modelList") || tag == QLatin1String("layoutList")) {
                section = tag.toString();
            } else if (tag == QLatin1String("layout")) {
                layout.clear();
                inVariant = false;
            } else if (tag == QLatin1String("variant")) {
                inVariant = true;
            } else if (tag == QLatin1String("configItem")) {
                name.clear();
                description.clear();
                countries.clear();
                languages.clear();
            } else if (tag == QLatin1String("iso3166Id")) {
                countries << xml.readElementText();
            } else if (tag == QLatin1String("iso639Id")) {
                languages << xml.readElementText();
            } else if (tag == QLatin1String("name")) {
                name = xml.readElementText();
            } else if (tag == QLatin1String("description")) {
                description = xml.readElementText();
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("configItem")) {
            if (name.isEmpty() || description.isEmpty()) {
                continue;
            }
            if (section == QLatin1String("modelList")) {
                m_modelNames.insert(name, description);
                m_models.append(QVariantMap{{QStringLiteral("model"), name}, {QStringLiteral("name"), description}});
            } else if (section != QLatin1String("layoutList")) {
                continue;
            } else if (!inVariant) {
                layout = name;
                layoutCountries = countries;
                layoutLanguages = languages;
                m_names.insert(layout + QLatin1Char('|'), description);
                m_available.append(QVariantMap{{QStringLiteral("layout"), layout},
                                               {QStringLiteral("variant"), QString()},
                                               {QStringLiteral("name"), description},
                                               {QStringLiteral("keywords"), searchWords(layout, description, countries, languages)}});
            } else if (!layout.isEmpty()) {
                // A variant without its own lists is for the layout's.
                m_names.insert(layout + QLatin1Char('|') + name, description);
                m_available.append(QVariantMap{{QStringLiteral("layout"), layout},
                                               {QStringLiteral("variant"), name},
                                               {QStringLiteral("name"), description},
                                               {QStringLiteral("keywords"),
                                                searchWords(layout, description, countries.isEmpty() ? layoutCountries : countries,
                                                            languages.isEmpty() ? layoutLanguages : languages)}});
            }
        } else if (xml.isEndElement() && xml.name() == QLatin1String("variant")) {
            inVariant = false;
        }
    }
    // Windows' two Vietnamese keyboards. Both type on the US English keys:
    // Telex with letters (aa for â, s after a vowel for its acute accent),
    // VNI with the number keys.
    const QString vietnamese = QStringLiteral("vietnam vietnamese tieng viet tiếng việt vi");
    m_available.append(QVariantMap{{QStringLiteral("layout"), QStringLiteral("us")},
                                   {QStringLiteral("variant"), QString()},
                                   {QStringLiteral("method"), QStringLiteral("telex")},
                                   {QStringLiteral("name"), nameOf({QStringLiteral("us"), QString(), QStringLiteral("telex")})},
                                   {QStringLiteral("keywords"), vietnamese + QStringLiteral(" telex")}});
    m_available.append(QVariantMap{{QStringLiteral("layout"), QStringLiteral("us")},
                                   {QStringLiteral("variant"), QString()},
                                   {QStringLiteral("method"), QStringLiteral("vni")},
                                   {QStringLiteral("name"), nameOf({QStringLiteral("us"), QString(), QStringLiteral("vni")})},
                                   {QStringLiteral("keywords"), vietnamese + QStringLiteral(" vni number")}});

    QCollator collator;
    collator.setNumericMode(true);
    std::sort(m_available.begin(), m_available.end(), [&collator](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value(QStringLiteral("name")).toString(), b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
    std::sort(m_models.begin(), m_models.end(), [&collator](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value(QStringLiteral("name")).toString(), b.toMap().value(QStringLiteral("name")).toString()) < 0;
    });
}

QString Keyboard::nameOf(const Layout &l) const
{
    if (l.method == QLatin1String("telex")) {
        return tr("Vietnamese (Telex)");
    }
    if (l.method == QLatin1String("vni")) {
        return tr("Vietnamese (VNI)");
    }
    const QString name = m_names.value(l.layout + QLatin1Char('|') + l.variant);
    if (!name.isEmpty()) {
        return name;
    }
    return l.variant.isEmpty() ? l.layout.toUpper() : l.layout.toUpper() + QStringLiteral(" (") + l.variant + QLatin1Char(')');
}

QVariantList Keyboard::layouts() const
{
    const_cast<Keyboard *>(this)->loadNames();
    QVariantList list;
    for (const Layout &l : m_layouts) {
        list.append(QVariantMap{{QStringLiteral("layout"), l.layout},
                                {QStringLiteral("variant"), l.variant},
                                {QStringLiteral("method"), l.method},
                                {QStringLiteral("name"), nameOf(l)}});
    }
    return list;
}

QVariantList Keyboard::available()
{
    loadNames();
    return m_available;
}

QVariantList Keyboard::models()
{
    loadNames();
    return m_models;
}

QString Keyboard::modelName() const
{
    const_cast<Keyboard *>(this)->loadNames();
    return m_modelNames.value(m_model, m_model);
}

QString Keyboard::optionWithPrefix(const QStringList &prefixes) const
{
    for (const QString &option : m_options) {
        for (const QString &prefix : prefixes) {
            if (option.startsWith(prefix)) {
                return option;
            }
        }
    }
    return {};
}

void Keyboard::replaceOptions(const QStringList &prefixes, const QString &option)
{
    m_options.erase(std::remove_if(m_options.begin(), m_options.end(), [&prefixes](const QString &o) {
                        return std::any_of(prefixes.begin(), prefixes.end(), [&o](const QString &p) {
                            return o.startsWith(p);
                        });
                    }),
                    m_options.end());
    if (!option.isEmpty()) {
        m_options.append(option);
    }
    saveLayouts();
}

QString Keyboard::switchKeys() const
{
    return optionWithPrefix(switchPrefixes);
}

QString Keyboard::capsLock() const
{
    return optionWithPrefix(capsPrefixes);
}

QString Keyboard::compose() const
{
    return optionWithPrefix(composePrefixes);
}

QString Keyboard::altGr() const
{
    return optionWithPrefix(altGrPrefixes);
}

void Keyboard::saveLayouts()
{
    QStringList layouts;
    QStringList variants;
    QStringList methods;
    QStringList displayNames;
    for (const Layout &l : std::as_const(m_layouts)) {
        layouts << l.layout;
        variants << l.variant;
        methods << l.method;
        // The indicator's short name: VI for a Vietnamese method, so it
        // isn't a second US beside English (US).
        displayNames << (l.method.isEmpty() ? QString() : QStringLiteral("vi"));
    }
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kxkbrc"), KConfig::NoGlobals), QStringLiteral("Layout"));
    group.writeEntry("Use", true);
    group.writeEntry("LayoutList", layouts.join(QLatin1Char(',')));
    group.writeEntry("VariantList", variants.join(QLatin1Char(',')));
    // One display name per layout, as the keyboard module writes it; empty
    // shows the layout's own short name.
    group.writeEntry("DisplayNames", displayNames.join(QLatin1Char(',')));
    if (std::all_of(methods.cbegin(), methods.cend(), [](const QString &m) {
            return m.isEmpty();
        })) {
        group.deleteEntry(TypingMethods::configKey());
    } else {
        group.writeEntry(TypingMethods::configKey(), methods.join(QLatin1Char(',')));
    }
    group.writeEntry("Model", m_model);
    group.writeEntry("Options", m_options.join(QLatin1Char(',')));
    // Without this KWin adds the options to the ones it started with, so a
    // switch key once chosen could never be taken away again.
    group.writeEntry("ResetOldOptions", true);
    group.sync();

    QDBusConnection::sessionBus().send(QDBusMessage::createSignal(path, QStringLiteral("org.kde.keyboard"), QStringLiteral("reloadConfig")));
    QDBusConnection::sessionBus().send(
        QDBusMessage::createSignal(TypingMethods::signalPath(), TypingMethods::signalInterface(), QStringLiteral("changed")));
    Q_EMIT changed();
    Q_EMIT methodChanged();
}

void Keyboard::add(const QString &layout, const QString &variant, const QString &method)
{
    if (!method.isEmpty() && TypingMethods::engine(method).isEmpty()) {
        return;
    }
    for (const Layout &l : std::as_const(m_layouts)) {
        if (l.layout == layout && l.variant == variant && l.method == method) {
            return;
        }
    }
    m_layouts.append({layout, variant, method});
    // A second layout is no use without a way to reach it, so it comes with
    // the key Windows uses unless one is already set.
    if (m_layouts.size() == 2 && switchKeys().isEmpty()) {
        m_options.append(QStringLiteral("grp:win_space_toggle"));
    }
    saveLayouts();
    if (methodMissing()) {
        download();
    }
}

bool Keyboard::methodMissing() const
{
    return std::any_of(m_layouts.cbegin(), m_layouts.cend(), [](const Layout &l) {
        return !l.method.isEmpty() && !TypingMethods::installed(TypingMethods::engine(l.method));
    });
}

bool Keyboard::methodInUse() const
{
    return std::any_of(m_layouts.cbegin(), m_layouts.cend(), [](const Layout &l) {
        return !l.method.isEmpty();
    });
}

void Keyboard::download()
{
    if (m_process) {
        return;
    }
    QString engine;
    for (const Layout &l : std::as_const(m_layouts)) {
        const QString e = TypingMethods::engine(l.method);
        if (!e.isEmpty() && !TypingMethods::installed(e)) {
            engine = e;
            break;
        }
    }
    if (engine.isEmpty()) {
        return;
    }
    m_error.clear();
    m_step = tr("Waiting for your password");
    m_process = new QProcess(this);
    connect(m_process, &QProcess::readyReadStandardOutput, this, [this] {
        while (m_process->canReadLine()) {
            const QString line = QString::fromUtf8(m_process->readLine()).trimmed();
            if (line == QLatin1String("step Checking for updates")) {
                m_step = tr("Checking for updates");
            } else if (line == QLatin1String("step Downloading")) {
                m_step = tr("Downloading");
            } else if (line == QLatin1String("step Installing")) {
                m_step = tr("Installing");
            }
            Q_EMIT methodChanged();
        }
    });
    connect(m_process, &QProcess::finished, this, [this](int code) {
        if (code == 126 || code == 127) {
            m_error = tr("Nothing was downloaded because the password was not given.");
        } else if (code != 0) {
            m_error = tr("The typing method could not be downloaded. Check the internet connection and try again.");
        }
        m_process->deleteLater();
        m_process = nullptr;
        m_step.clear();
        // wildos-access hands the keyboard to Fcitx now it is there.
        QDBusConnection::sessionBus().send(
            QDBusMessage::createSignal(TypingMethods::signalPath(), TypingMethods::signalInterface(), QStringLiteral("changed")));
        Q_EMIT methodChanged();
    });
    m_process->start(QStringLiteral("pkexec"), {QStringLiteral("/usr/libexec/wildos/settings-input-method"), QStringLiteral("install"), engine});
    Q_EMIT methodChanged();
}

// A download still running when Settings closes carries on by itself (the
// helper ignores the closed pipe), so dpkg is never stopped halfway.
Keyboard::~Keyboard()
{
    if (m_process) {
        m_process->disconnect(this);
        m_process->setParent(nullptr);
        m_process = nullptr;
    }
}

void Keyboard::remove(int index)
{
    if (index < 0 || index >= m_layouts.size() || m_layouts.size() <= 1) {
        return;
    }
    m_layouts.removeAt(index);
    saveLayouts();
}

void Keyboard::move(int from, int to)
{
    if (from < 0 || from >= m_layouts.size() || to < 0 || to >= m_layouts.size() || from == to) {
        return;
    }
    m_layouts.move(from, to);
    saveLayouts();
}

void Keyboard::use(int index)
{
    auto message = QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("setLayout"));
    message << uint(index);
    QDBusConnection::sessionBus().asyncCall(message);
}

void Keyboard::readCurrent()
{
    auto message = QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("getLayout"));
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<uint> reply = *watcher;
        if (reply.isValid()) {
            onLayoutChanged(reply.value());
        }
    });
}

void Keyboard::onLayoutChanged(uint index)
{
    if (int(index) != m_current) {
        m_current = int(index);
        Q_EMIT currentChanged();
    }
}

void Keyboard::setSwitchKeys(const QString &option)
{
    replaceOptions(switchPrefixes, option);
}

void Keyboard::setCapsLock(const QString &option)
{
    replaceOptions(capsPrefixes, option);
}

void Keyboard::setCompose(const QString &option)
{
    replaceOptions(composePrefixes, option);
}

void Keyboard::setAltGr(const QString &option)
{
    replaceOptions(altGrPrefixes, option);
}

void Keyboard::setModel(const QString &model)
{
    if (model.isEmpty() || model == m_model) {
        return;
    }
    m_model = model;
    saveLayouts();
}

void Keyboard::saveRepeat()
{
    // Notify: KWin watches kcminputrc and applies the change at once.
    KConfigGroup group(KSharedConfig::openConfig(QStringLiteral("kcminputrc"), KConfig::NoGlobals), QStringLiteral("Keyboard"));
    group.writeEntry("NumLock", m_numLock, KConfig::Notify);
    group.writeEntry("KeyRepeat", m_repeat ? QStringLiteral("repeat") : QStringLiteral("nothing"), KConfig::Notify);
    group.writeEntry("RepeatDelay", m_repeatDelay, KConfig::Notify);
    group.writeEntry("RepeatRate", double(m_repeatRate), KConfig::Notify);
    group.sync();
    Q_EMIT changed();
}

void Keyboard::setNumLock(int state)
{
    m_numLock = qBound(0, state, 2);
    saveRepeat();
}

void Keyboard::setRepeat(bool on)
{
    m_repeat = on;
    saveRepeat();
}

void Keyboard::setRepeatDelay(int ms)
{
    m_repeatDelay = qBound(100, ms, 2000);
    saveRepeat();
}

void Keyboard::setRepeatRate(int perSecond)
{
    m_repeatRate = qBound(1, perSecond, 100);
    saveRepeat();
}
