#include "config.h"
#include "schemes.h"

#include <KConfigGroup>
#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFont>
#include <QFontDatabase>
#include <QRandomGenerator>

namespace
{
const QString general = QStringLiteral("General");
const QString profilePrefix = QStringLiteral("Profile ");

// Cascadia Mono is the Windows Terminal font, so people arriving from
// Windows see the text they know. The others are fallbacks in that order.
QString defaultFont()
{
    const QStringList families = QFontDatabase::families();
    for (const QString &f : {QStringLiteral("Cascadia Mono"), QStringLiteral("JetBrains Mono"), QStringLiteral("DejaVu Sans Mono")}) {
        if (families.contains(f)) {
            return f;
        }
    }
    return QFontDatabase::systemFont(QFontDatabase::FixedFont).family();
}

QString shellName(const QString &path)
{
    QString name = QFileInfo(path).fileName();
    if (!name.isEmpty()) {
        name[0] = name[0].toUpper();
    }
    return name.isEmpty() ? QCoreApplication::translate("Config", "Shell") : name;
}

QVariantMap makeProfile(const QString &id, const QString &name)
{
    return QVariantMap{
        {QStringLiteral("id"), id},
        {QStringLiteral("name"), name},
        {QStringLiteral("command"), QString()},
        {QStringLiteral("directory"), QString()},
        {QStringLiteral("icon"), QStringLiteral("terminal")},
    };
}
}

Config *Config::instance()
{
    static Config *config = new Config;
    return config;
}

Config *Config::create(QQmlEngine *, QJSEngine *)
{
    Config *config = instance();
    QJSEngine::setObjectOwnership(config, QJSEngine::CppOwnership);
    return config;
}

Config::Config(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QStringLiteral("wildosterminalrc")))
{
    read();
    readProfiles();
    readAccent();
    m_watcher = KConfigWatcher::create(m_config);
    connect(m_watcher.data(), &KConfigWatcher::configChanged, this, [this] {
        read();
        readProfiles();
    });
    m_globals = KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("kdeglobals")));
    connect(m_globals.data(), &KConfigWatcher::configChanged, this, &Config::readAccent);
}

void Config::read()
{
    m_config->reparseConfiguration();
    const KConfigGroup g(m_config, general);
    m_scheme = g.readEntry("ColourScheme", QStringLiteral("Homebrew"));
    m_fontFamily = g.readEntry("FontFamily", defaultFont());
    m_fontSize = std::clamp(g.readEntry("FontSize", 11), 6, 40);
    m_cursorShape = g.readEntry("CursorShape", QStringLiteral("bar"));
    m_cursorBlink = g.readEntry("CursorBlink", true);
    m_opacity = std::clamp(g.readEntry("Opacity", 100), 30, 100);
    m_blur = g.readEntry("Blur", true);
    m_boldBright = g.readEntry("BoldBright", true);
    m_defaultProfile = g.readEntry("DefaultProfile", QString());
    m_launchColumns = std::clamp(g.readEntry("LaunchColumns", 120), 20, 400);
    m_launchRows = std::clamp(g.readEntry("LaunchRows", 30), 5, 200);
    m_launchMaximised = g.readEntry("LaunchMaximised", false);
    // Selecting copies unless switched off (Dan, 14 Nov 2026): the quickest
    // way to get a long link or an error out of the terminal.
    m_copyOnSelect = g.readEntry("CopyOnSelect", true);
    m_pasteOnRightClick = g.readEntry("PasteOnRightClick", false);
    m_ctrlCopyPaste = g.readEntry("CtrlCopyPaste", true);
    m_warnMultilinePaste = g.readEntry("WarnMultilinePaste", true);
    m_confirmCloseTabs = g.readEntry("ConfirmCloseTabs", true);
    m_scrollback = g.readEntry("Scrollback", 10000);
    Q_EMIT changed();
}

void Config::write(const char *key, const QVariant &value)
{
    KConfigGroup g(m_config, general);
    g.writeEntry(key, value, KConfig::Notify);
    m_config->sync();
    Q_EMIT changed();
}

#define WILDOS_SETTER(type, member, setter, key) \
    void Config::setter(type v)                  \
    {                                            \
        if (member == v) {                       \
            return;                              \
        }                                        \
        member = v;                              \
        write(key, QVariant::fromValue(member)); \
    }

WILDOS_SETTER(const QString &, m_scheme, setScheme, "ColourScheme")
WILDOS_SETTER(const QString &, m_fontFamily, setFontFamily, "FontFamily")
WILDOS_SETTER(const QString &, m_cursorShape, setCursorShape, "CursorShape")
WILDOS_SETTER(bool, m_cursorBlink, setCursorBlink, "CursorBlink")
WILDOS_SETTER(bool, m_blur, setBlur, "Blur")
WILDOS_SETTER(bool, m_boldBright, setBoldBright, "BoldBright")
WILDOS_SETTER(const QString &, m_defaultProfile, setDefaultProfile, "DefaultProfile")
WILDOS_SETTER(bool, m_launchMaximised, setLaunchMaximised, "LaunchMaximised")
WILDOS_SETTER(bool, m_copyOnSelect, setCopyOnSelect, "CopyOnSelect")
WILDOS_SETTER(bool, m_pasteOnRightClick, setPasteOnRightClick, "PasteOnRightClick")
WILDOS_SETTER(bool, m_ctrlCopyPaste, setCtrlCopyPaste, "CtrlCopyPaste")
WILDOS_SETTER(bool, m_warnMultilinePaste, setWarnMultilinePaste, "WarnMultilinePaste")
WILDOS_SETTER(bool, m_confirmCloseTabs, setConfirmCloseTabs, "ConfirmCloseTabs")
WILDOS_SETTER(int, m_scrollback, setScrollback, "Scrollback")
#undef WILDOS_SETTER

void Config::setFontSize(int v)
{
    v = std::clamp(v, 6, 40);
    if (v != m_fontSize) {
        m_fontSize = v;
        write("FontSize", v);
    }
}

void Config::setOpacity(int v)
{
    v = std::clamp(v, 30, 100);
    if (v != m_opacity) {
        m_opacity = v;
        write("Opacity", v);
    }
}

void Config::setLaunchColumns(int v)
{
    v = std::clamp(v, 20, 400);
    if (v != m_launchColumns) {
        m_launchColumns = v;
        write("LaunchColumns", v);
    }
}

void Config::setLaunchRows(int v)
{
    v = std::clamp(v, 5, 200);
    if (v != m_launchRows) {
        m_launchRows = v;
        write("LaunchRows", v);
    }
}

QString Config::defaultProfile() const
{
    for (const QVariant &p : m_profiles) {
        if (p.toMap().value(QStringLiteral("id")).toString() == m_defaultProfile) {
            return m_defaultProfile;
        }
    }
    return m_profiles.isEmpty() ? QString() : m_profiles.first().toMap().value(QStringLiteral("id")).toString();
}

QVariantList Config::schemes() const
{
    return Schemes::toVariant();
}

QStringList Config::monospaceFonts() const
{
    QStringList fonts;
    for (const QString &family : QFontDatabase::families()) {
        // Symbol and private families have no Latin letters; they do not
        // belong in a terminal.
        if (family.startsWith(QLatin1Char('.')) || !QFontDatabase::isFixedPitch(family)) {
            continue;
        }
        if (!QFontDatabase::writingSystems(family).contains(QFontDatabase::Latin)) {
            continue;
        }
        fonts.append(family);
    }
    if (!fonts.contains(m_fontFamily)) {
        fonts.append(m_fontFamily);
    }
    fonts.sort(Qt::CaseInsensitive);
    return fonts;
}

QString Config::homePath() const
{
    return QDir::homePath();
}

QString Config::defaultShell() const
{
    const QString shell = qEnvironmentVariable("SHELL");
    return shell.isEmpty() ? QStringLiteral("/bin/bash") : shell;
}

QFont Config::terminalFont() const
{
    QFont font(m_fontFamily);
    font.setPointSize(m_fontSize);
    font.setStyleHint(QFont::TypeWriter);
    font.setFixedPitch(true);
    font.setKerning(false);
    return font;
}

QString Config::schemePath() const
{
    return Schemes::find(m_scheme).path;
}

QColor Config::schemeBackground() const
{
    return Schemes::find(m_scheme).background;
}

void Config::readProfiles()
{
    QVariantList list;
    const KConfigGroup g(m_config, general);
    const QStringList ids = g.readEntry("Profiles", QStringList());
    for (const QString &id : ids) {
        const KConfigGroup p(m_config, profilePrefix + id);
        QVariantMap profile = makeProfile(id, p.readEntry("Name", shellName(defaultShell())));
        profile.insert(QStringLiteral("command"), p.readEntry("Command", QString()));
        profile.insert(QStringLiteral("directory"), p.readEntry("Directory", QString()));
        profile.insert(QStringLiteral("icon"), p.readEntry("Icon", QStringLiteral("terminal")));
        list.append(profile);
    }
    // The first run has one profile for the login shell. It is only saved
    // with the first change, so until then it follows the login shell.
    if (list.isEmpty()) {
        list.append(makeProfile(QStringLiteral("shell"), shellName(defaultShell())));
    }
    if (list != m_profiles) {
        m_profiles = list;
        Q_EMIT profilesChanged();
        Q_EMIT changed();
    }
}

void Config::writeProfiles()
{
    QStringList ids;
    for (const QString &group : m_config->groupList()) {
        if (group.startsWith(profilePrefix)) {
            m_config->deleteGroup(group, KConfig::Notify);
        }
    }
    for (const QVariant &v : std::as_const(m_profiles)) {
        const QVariantMap p = v.toMap();
        const QString id = p.value(QStringLiteral("id")).toString();
        ids.append(id);
        KConfigGroup g(m_config, profilePrefix + id);
        g.writeEntry("Name", p.value(QStringLiteral("name")).toString(), KConfig::Notify);
        g.writeEntry("Command", p.value(QStringLiteral("command")).toString(), KConfig::Notify);
        g.writeEntry("Directory", p.value(QStringLiteral("directory")).toString(), KConfig::Notify);
        g.writeEntry("Icon", p.value(QStringLiteral("icon")).toString(), KConfig::Notify);
    }
    KConfigGroup(m_config, general).writeEntry("Profiles", ids, KConfig::Notify);
    m_config->sync();
    Q_EMIT profilesChanged();
    Q_EMIT changed();
}

QVariantMap Config::profile(const QString &id) const
{
    for (const QVariant &v : m_profiles) {
        if (v.toMap().value(QStringLiteral("id")).toString() == id) {
            return v.toMap();
        }
    }
    const QString fallback = defaultProfile();
    for (const QVariant &v : m_profiles) {
        if (v.toMap().value(QStringLiteral("id")).toString() == fallback) {
            return v.toMap();
        }
    }
    return {};
}

QString Config::newId() const
{
    for (;;) {
        const QString id = QString::number(QRandomGenerator::global()->bounded(0x10000000u, 0xffffffffu), 16);
        if (profile(id).value(QStringLiteral("id")).toString() != id) {
            return id;
        }
    }
}

QString Config::addProfile()
{
    const QString id = newId();
    m_profiles.append(makeProfile(id, tr("New profile")));
    writeProfiles();
    return id;
}

QString Config::duplicateProfile(const QString &id)
{
    QVariantMap p = profile(id);
    if (p.isEmpty()) {
        return {};
    }
    const QString copyId = newId();
    p.insert(QStringLiteral("id"), copyId);
    p.insert(QStringLiteral("name"), p.value(QStringLiteral("name")).toString() + QStringLiteral(" copy"));
    m_profiles.append(p);
    writeProfiles();
    return copyId;
}

void Config::removeProfile(const QString &id)
{
    if (m_profiles.size() <= 1) {
        return;
    }
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].toMap().value(QStringLiteral("id")).toString() == id) {
            m_profiles.removeAt(i);
            writeProfiles();
            return;
        }
    }
}

void Config::setProfileValue(const QString &id, const QString &key, const QString &value)
{
    for (QVariant &v : m_profiles) {
        QVariantMap p = v.toMap();
        if (p.value(QStringLiteral("id")).toString() == id) {
            if (p.value(key).toString() == value) {
                return;
            }
            p.insert(key, value);
            v = p;
            writeProfiles();
            return;
        }
    }
}

void Config::moveProfile(const QString &id, int step)
{
    for (int i = 0; i < m_profiles.size(); ++i) {
        if (m_profiles[i].toMap().value(QStringLiteral("id")).toString() == id) {
            const int to = std::clamp(i + step, 0, int(m_profiles.size()) - 1);
            if (to != i) {
                m_profiles.move(i, to);
                writeProfiles();
            }
            return;
        }
    }
}

void Config::readAccent()
{
    KSharedConfigPtr globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    globals->reparseConfiguration();
    const QString value = globals->group(QStringLiteral("General")).readEntry("AccentColor");
    QColor colour;
    const QStringList parts = value.split(QLatin1Char(','));
    if (parts.size() >= 3) {
        colour = QColor(parts[0].trimmed().toInt(), parts[1].trimmed().toInt(), parts[2].trimmed().toInt());
    } else if (!value.isEmpty()) {
        colour = QColor(value);
    }
    if (!colour.isValid()) {
        colour = QColor(QStringLiteral("#8fb4ff"));
    }
    if (colour != m_accent) {
        m_accent = colour;
        Q_EMIT accentChanged();
    }
}
