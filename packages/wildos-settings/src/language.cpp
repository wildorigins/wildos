#include "language.h"

#include <KConfigGroup>
#include <KFormat>
#include <KSharedConfig>

#include <QCollator>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCall>
#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QLocale>
#include <QProcess>
#include <QSet>

namespace
{
const QString helper = QStringLiteral("/usr/libexec/wildos/settings-locale");
const QString fallbackFormat = QStringLiteral("en_GB.UTF-8");
const QString fallbackLanguage = QStringLiteral("en_GB");

// Every format variable the installer writes to /etc/default/locale and
// startplasma reads from plasma-localerc; choosing a regional format here
// sets them all, as Windows does.
const QStringList categories = {QStringLiteral("LC_NUMERIC"), QStringLiteral("LC_TIME"), QStringLiteral("LC_MONETARY"),
                                QStringLiteral("LC_MEASUREMENT"), QStringLiteral("LC_COLLATE"), QStringLiteral("LC_CTYPE"),
                                QStringLiteral("LC_PAPER"), QStringLiteral("LC_ADDRESS"), QStringLiteral("LC_NAME"),
                                QStringLiteral("LC_TELEPHONE"), QStringLiteral("LC_IDENTIFICATION")};

// The parts More format options offers on their own, under the keys
// kcm_regionandlang writes (regionandlangsettingsbase.kcfg). Addresses,
// names and telephone numbers are left to the regional format, as Windows
// leaves them.
const QStringList shownCategories = {QStringLiteral("LC_NUMERIC"), QStringLiteral("LC_MONETARY"), QStringLiteral("LC_TIME"),
                                     QStringLiteral("LC_MEASUREMENT"), QStringLiteral("LC_PAPER")};

// "en_GB.utf8", "en_GB.UTF-8" and "en_GB" are the same locale.
QString canonical(QString locale)
{
    locale = locale.trimmed();
    const int at = locale.indexOf(QLatin1Char('@'));
    const QString modifier = at >= 0 ? locale.mid(at) : QString();
    if (at >= 0) {
        locale.truncate(at);
    }
    const QString base = locale.section(QLatin1Char('.'), 0, 0);
    if (base.isEmpty() || base == QLatin1String("C") || base == QLatin1String("POSIX")) {
        return {};
    }
    return base + QStringLiteral(".UTF-8") + modifier;
}

QString firstLanguage(const QString &list)
{
    for (const QString &entry : list.split(QLatin1Char(':'), Qt::SkipEmptyParts)) {
        const QString code = entry.section(QLatin1Char('.'), 0, 0);
        if (!code.isEmpty() && code != QLatin1String("C")) {
            return code;
        }
    }
    return {};
}

QString capitalised(QString text)
{
    if (!text.isEmpty()) {
        text[0] = text[0].toUpper();
    }
    return text;
}

QStringList supported()
{
    QStringList list;
    QFile file(QStringLiteral("/usr/share/i18n/SUPPORTED"));
    if (file.open(QIODevice::ReadOnly)) {
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            const QString name = line.section(QLatin1Char(' '), 0, 0);
            if (line.endsWith(QLatin1String(" UTF-8")) && name.endsWith(QLatin1String(".UTF-8")) && !name.contains(QLatin1Char('@'))) {
                list << name;
            }
        }
    }
    return list;
}

QSet<QString> generated()
{
    QProcess process;
    process.start(QStringLiteral("locale"), {QStringLiteral("-a")});
    process.waitForFinished(5000);
    QSet<QString> set;
    const auto lines = QString::fromUtf8(process.readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        if (line.contains(QLatin1String("utf8"), Qt::CaseInsensitive) || line.contains(QLatin1String("UTF-8"), Qt::CaseInsensitive)) {
            set.insert(canonical(line));
        }
    }
    return set;
}
}

Language::Language(QObject *parent)
    : QObject(parent)
{
    // What this session started with, which is what apps use until the
    // next sign-in; wildos.sh gives English sessions UK English text.
    m_sessionLanguage = firstLanguage(qEnvironmentVariable("LANGUAGE"));
    m_sessionFormat = canonical(qEnvironmentVariable("LANG"));
    if (m_sessionFormat.isEmpty()) {
        m_sessionFormat = fallbackFormat;
    }
    if (m_sessionLanguage.isEmpty() || m_sessionLanguage == QLatin1String("en")) {
        m_sessionLanguage = m_sessionFormat.startsWith(QLatin1String("en_")) ? fallbackLanguage : m_sessionFormat.section(QLatin1Char('_'), 0, 0);
    }
    for (const QString &key : shownCategories) {
        m_sessionCategories.insert(key, canonical(qEnvironmentVariable(key.toLatin1().constData())));
    }
    read();
}

void Language::read()
{
    auto config = KSharedConfig::openConfig(QStringLiteral("plasma-localerc"), KConfig::NoGlobals);
    config->reparseConfiguration();
    KConfigGroup formats(config, QStringLiteral("Formats"));
    const KConfigGroup translations(config, QStringLiteral("Translations"));
    m_language = firstLanguage(translations.readEntry("LANGUAGE", QString()));
    if (m_language.isEmpty()) {
        m_language = m_sessionLanguage;
    }
    m_format = canonical(formats.readEntry("LANG", QString()));
    if (m_format.isEmpty()) {
        m_format = m_sessionFormat;
    }

    // The installer writes every LC_ variable into /etc/default/locale, and
    // the sign-in passes them to the session, where each one outranks LANG.
    // startplasma only replaces the ones written here, so a part missing
    // here is not "the regional format" but whatever the installer chose.
    // Earlier versions of this page removed the parts that followed the
    // regional format; a page that wrote LC_TIME is one of those, and its
    // missing parts are written out now as the format they were meant to
    // follow, so the next sign-in finally applies it.
    if (formats.hasKey("LANG") && formats.hasKey("LC_TIME")) {
        bool wrote = false;
        for (const QString &key : categories) {
            if (!formats.hasKey(key)) {
                formats.writeEntry(key, m_format, KConfig::Notify);
                wrote = true;
            }
        }
        if (wrote) {
            config->sync();
        }
    }

    // What each part really is: the value written here, or else the one
    // this session was given at sign-in.
    const auto resolved = [this, &formats](const QString &key) {
        const QString value = canonical(formats.readEntry(key, QString()));
        if (!value.isEmpty()) {
            return value;
        }
        const QString session = m_sessionCategories.value(key).toString();
        return session.isEmpty() ? m_format : session;
    };
    m_complete = true;
    for (const QString &key : categories) {
        if (canonical(formats.readEntry(key, QString())) != m_format) {
            m_complete = false;
        }
    }
    m_detailed = false;
    m_overrides.clear();
    for (const QString &key : shownCategories) {
        const QString value = resolved(key);
        m_overrides.insert(key, value == m_format ? QString() : value);
        if (value != m_format) {
            m_detailed = true;
        }
    }
    for (const QString &key : categories) {
        const QString value = canonical(formats.readEntry(key, QString()));
        if (!value.isEmpty() && value != m_format) {
            m_detailed = true;
        }
    }
    auto globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    globals->reparseConfiguration();
    const KConfigGroup locale(globals, QStringLiteral("Locale"));
    m_fileSizeUnits = locale.readEntry("BinaryUnitDialect", int(KFormat::IECBinaryDialect));
    Q_EMIT changed();
}

bool Language::needsSignOut() const
{
    if (m_language != m_sessionLanguage || m_format != m_sessionFormat) {
        return true;
    }
    // A part chosen on its own reaches apps as its LC_ variable at sign-in;
    // one this session did not get as a variable came from LANG.
    for (const QString &key : shownCategories) {
        const QString chosen = m_overrides.value(key).toString();
        const QString session = m_sessionCategories.value(key).toString();
        if (!chosen.isEmpty() && chosen != (session.isEmpty() ? m_sessionFormat : session)) {
            return true;
        }
        if (chosen.isEmpty() && !session.isEmpty() && session != m_format) {
            return true;
        }
    }
    return false;
}

QVariantList Language::languages() const
{
    // A language counts when the desktop shell and Wild OS's own apps are
    // both translated into it. KDE has about 80 languages to our 43, and the
    // others gave a desktop half in English. US English has no translation
    // because it is the language KDE is written in, so it is added by hand.
    // Its list still falls back to UK English (see setLanguage), which keeps
    // Wild OS's own wording, while apps with a US English translation, such
    // as the browser, use it. A language chosen before this rule stays
    // listed, so the box never shows a bare code.
    const QString ours = QStringLiteral("/usr/share/wildos/translations/wildos-settings_%1.qm");
    QSet<QString> codes = {fallbackLanguage, QStringLiteral("en_US")};
    if (!m_language.isEmpty()) {
        codes.insert(m_language);
    }
    const auto dirs = QDir(QStringLiteral("/usr/share/locale")).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &code : dirs) {
        if (!code.contains(QLatin1Char('@')) && code != QLatin1String("en")
            && QFile::exists(QStringLiteral("/usr/share/locale/") + code + QStringLiteral("/LC_MESSAGES/plasmashell.mo"))
            && QFile::exists(ours.arg(code))) {
            codes.insert(code);
        }
    }
    QVariantList list;
    for (const QString &code : std::as_const(codes)) {
        const QLocale locale(code);
        if (locale.language() == QLocale::C || code.size() > 5) {
            continue;
        }
        // Qt names UK English "British English", which would then read
        // "British English (United Kingdom)" with the territory.
        QString text = locale.language() == QLocale::English ? QStringLiteral("English") : capitalised(locale.nativeLanguageName());
        if (code.contains(QLatin1Char('_'))) {
            text += QStringLiteral(" (") + locale.nativeTerritoryName() + QLatin1Char(')');
        }
        QString english = QLocale::languageToString(locale.language());
        if (code.contains(QLatin1Char('_'))) {
            english += QStringLiteral(" (") + QLocale::territoryToString(locale.territory()) + QLatin1Char(')');
        }
        list << QVariantMap{{QStringLiteral("value"), code}, {QStringLiteral("text"), text}, {QStringLiteral("english"), english}};
    }
    QCollator collator(QLocale(QStringLiteral("en_GB")));
    std::sort(list.begin(), list.end(), [&collator](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap();
        const QVariantMap y = b.toMap();
        if ((x.value(QStringLiteral("value")) == fallbackLanguage) != (y.value(QStringLiteral("value")) == fallbackLanguage)) {
            return x.value(QStringLiteral("value")) == fallbackLanguage;
        }
        return collator.compare(x.value(QStringLiteral("english")).toString(), y.value(QStringLiteral("english")).toString()) < 0;
    });
    return list;
}

QVariantList Language::formats() const
{
    QStringList names = supported();
    if (names.isEmpty()) {
        names = generated().values();
    }
    QVariantList list;
    QSet<QString> seen;
    for (const QString &name : std::as_const(names)) {
        const QLocale locale(name.section(QLatin1Char('.'), 0, 0));
        if (locale.language() == QLocale::C) {
            continue;
        }
        const QString text = QLocale::languageToString(locale.language()) + QStringLiteral(" (") + QLocale::territoryToString(locale.territory()) + QLatin1Char(')');
        if (seen.contains(text)) {
            continue;
        }
        seen.insert(text);
        list << QVariantMap{{QStringLiteral("value"), name}, {QStringLiteral("text"), text}};
    }
    QCollator collator(QLocale(QStringLiteral("en_GB")));
    std::sort(list.begin(), list.end(), [&collator](const QVariant &a, const QVariant &b) {
        const QVariantMap x = a.toMap();
        const QVariantMap y = b.toMap();
        if ((x.value(QStringLiteral("value")) == fallbackFormat) != (y.value(QStringLiteral("value")) == fallbackFormat)) {
            return x.value(QStringLiteral("value")) == fallbackFormat;
        }
        return collator.compare(x.value(QStringLiteral("text")).toString(), y.value(QStringLiteral("text")).toString()) < 0;
    });
    return list;
}

QVariantMap Language::examples(const QString &format) const
{
    const QLocale locale(format.section(QLatin1Char('.'), 0, 0));
    const QDateTime now = QDateTime::currentDateTime();
    QString measurement;
    switch (locale.measurementSystem()) {
    case QLocale::ImperialUSSystem:
        measurement = tr("Imperial: inches, feet and pounds");
        break;
    case QLocale::ImperialUKSystem:
        measurement = tr("Metric, with miles and pints");
        break;
    default:
        measurement = tr("Metric: centimetres, metres and kilograms");
        break;
    }
    return {
        {QStringLiteral("shortDate"), locale.toString(now.date(), QLocale::ShortFormat)},
        {QStringLiteral("longDate"), locale.toString(now.date(), QLocale::LongFormat)},
        {QStringLiteral("time"), locale.toString(now.time(), QLocale::ShortFormat)},
        {QStringLiteral("number"), locale.toString(1234567.89, 'f', 2)},
        {QStringLiteral("currency"), locale.toCurrencyString(1234.5)},
        {QStringLiteral("firstDay"), QLocale(QStringLiteral("en_GB")).dayName(locale.firstDayOfWeek())},
        {QStringLiteral("measurement"), measurement},
    };
}

void Language::ensure(const QStringList &locales, const std::function<void()> &then)
{
    const QSet<QString> have = generated();
    QStringList missing;
    for (const QString &l : locales) {
        if (!have.contains(canonical(l))) {
            missing << l;
        }
    }
    if (missing.isEmpty()) {
        then();
        return;
    }
    m_busy = true;
    Q_EMIT busyChanged();
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, then](int code, QProcess::ExitStatus status) {
        const QString error = QString::fromUtf8(process->readAllStandardError()).trimmed().section(QLatin1Char('\n'), -1);
        process->deleteLater();
        m_busy = false;
        Q_EMIT busyChanged();
        if (status == QProcess::NormalExit && code == 0) {
            then();
        } else {
            Q_EMIT failed(code == 126 || code == 127 ? tr("An administrator's password is needed to add a language to this computer.")
                                                     : error.isEmpty() ? tr("The language could not be set up.") : error);
            Q_EMIT changed();
        }
    });
    process->start(QStringLiteral("pkexec"), QStringList{helper} + missing);
}

void Language::setLanguage(const QString &code)
{
    if (code.isEmpty() || code == m_language) {
        return;
    }
    // Translations are read through LANGUAGE alone; glibc only needs a
    // UTF-8 locale for them, which the regional format already is.
    auto config = KSharedConfig::openConfig(QStringLiteral("plasma-localerc"), KConfig::NoGlobals);
    KConfigGroup translations(config, QStringLiteral("Translations"));
    // UK English stands behind every other language, so anything a
    // translation lacks reads in Wild OS's own words.
    const QString list = code == fallbackLanguage ? QStringLiteral("en_GB:en") : code + QStringLiteral(":en_GB:en");
    translations.writeEntry("LANGUAGE", list, KConfig::Notify);
    config->sync();
    read();
}

void Language::setFormat(const QString &locale)
{
    const QString chosen = canonical(locale);
    // Choosing the same format again is how the page puts every part back
    // to it, so it only does nothing when they all follow it already.
    if (chosen.isEmpty() || (chosen == m_format && m_complete) || m_busy) {
        return;
    }
    ensure({chosen}, [this, chosen] {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasma-localerc"), KConfig::NoGlobals);
        KConfigGroup formats(config, QStringLiteral("Formats"));
        formats.writeEntry("LANG", chosen, KConfig::Notify);
        // Every part is written out, not left to follow LANG: the LC_
        // variables the installer set (and wildos.sh's LC_TIME for US
        // English) outrank LANG, so only a value written here replaces them.
        for (const QString &key : categories) {
            formats.writeEntry(key, chosen, KConfig::Notify);
        }
        config->sync();
        read();
    });
}

QString Language::example(const QString &category, const QString &format) const
{
    const QString name = format.section(QLatin1Char('.'), 0, 0);
    const QLocale locale(name);
    if (category == QLatin1String("LC_NUMERIC")) {
        return locale.toString(1234.56, 'f', 2);
    }
    if (category == QLatin1String("LC_MONETARY")) {
        return locale.toCurrencyString(1234.56);
    }
    if (category == QLatin1String("LC_TIME")) {
        const QDateTime now = QDateTime::currentDateTime();
        return locale.toString(now.date(), QLocale::ShortFormat) + QStringLiteral(", ") + locale.toString(now.time(), QLocale::ShortFormat);
    }
    if (category == QLatin1String("LC_MEASUREMENT")) {
        switch (locale.measurementSystem()) {
        case QLocale::ImperialUSSystem:
            return tr("Inches, feet and pounds");
        case QLocale::ImperialUKSystem:
            return tr("Metric, with miles and pints");
        default:
            return tr("Centimetres, metres and kilograms");
        }
    }
    if (category == QLatin1String("LC_PAPER")) {
        // The same list KDE's region module uses (exampleutility.cpp):
        // these regions measure in metric and still print on Letter.
        static const QSet<QString> letter = {
            QStringLiteral("en_CA"), QStringLiteral("en_PH"), QStringLiteral("es_BO"), QStringLiteral("es_CL"), QStringLiteral("es_CO"),
            QStringLiteral("es_CR"), QStringLiteral("es_GT"), QStringLiteral("es_MX"), QStringLiteral("es_NI"), QStringLiteral("es_PA"),
            QStringLiteral("es_PR"), QStringLiteral("es_SV"), QStringLiteral("es_VE"), QStringLiteral("fil_PH"), QStringLiteral("fr_CA"),
            QStringLiteral("ik_CA"), QStringLiteral("iu_CA"), QStringLiteral("miq_NI"), QStringLiteral("nhn_MX"), QStringLiteral("shs_CA"),
            QStringLiteral("tl_PH")};
        const bool isLetter = letter.contains(name) || locale.measurementSystem() == QLocale::ImperialUSSystem;
        return isLetter ? tr("Letter, 8.5 by 11 inches") : tr("A4, 210 by 297 mm");
    }
    return {};
}

void Language::setCategory(const QString &category, const QString &locale)
{
    if (!shownCategories.contains(category) || m_busy) {
        return;
    }
    QString chosen = canonical(locale);
    if (chosen == m_format) {
        chosen.clear();
    }
    if (chosen == m_overrides.value(category).toString()) {
        return;
    }
    const auto write = [this, category, chosen] {
        auto config = KSharedConfig::openConfig(QStringLiteral("plasma-localerc"), KConfig::NoGlobals);
        KConfigGroup formats(config, QStringLiteral("Formats"));
        // Following the regional format is written out as that format, for
        // the reason setFormat writes every part.
        formats.writeEntry(category, chosen.isEmpty() ? m_format : chosen, KConfig::Notify);
        config->sync();
        read();
    };
    if (chosen.isEmpty()) {
        write();
    } else {
        ensure({chosen}, write);
    }
}

QString Language::fileSizeExample(int units) const
{
    return KFormat().formatByteSize(1536000, 1, KFormat::BinaryUnitDialect(units));
}

void Language::setFileSizeUnits(int units)
{
    if (units < KFormat::IECBinaryDialect || units > KFormat::MetricBinaryDialect || units == m_fileSizeUnits) {
        return;
    }
    // Saved as kcm_regionandlang saves it: the IEC default is not stored.
    // KFormat reads this when an app starts, so open apps keep the old
    // units until they are restarted.
    auto config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    KConfigGroup locale(config, QStringLiteral("Locale"));
    if (units == KFormat::IECBinaryDialect) {
        locale.deleteEntry("BinaryUnitDialect", KConfig::Notify);
    } else {
        locale.writeEntry("BinaryUnitDialect", units, KConfig::Notify);
    }
    config->sync();
    read();
}

void Language::signOut()
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"), QStringLiteral("org.kde.Shutdown"),
                                                  QStringLiteral("logout"));
    QDBusConnection::sessionBus().asyncCall(message);
}
