#include "settings.h"
#include "paths.h"

#include <KConfigGroup>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFontDatabase>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QSaveFile>
#include <QSettings>
#include <QStandardPaths>

#include <algorithm>

namespace
{
const QString terminalProgram = QStringLiteral("wildos-terminal");

QString ownFile()
{
    return Paths::config() + QStringLiteral("/settings.json");
}

QString ownScheme()
{
    const QString installed = QStringLiteral(WILDOS_REMOTE_DATADIR "/Remote.colorscheme");
    return QFileInfo::exists(installed) ? installed : QStringLiteral(WILDOS_REMOTE_SOURCEDIR "/Remote.colorscheme");
}

// One of the schemes Terminal ships, where Terminal installs them beside
// Remote's own data; empty when Terminal is not installed.
QString terminalScheme(const QString &id)
{
    if (id.isEmpty() || id.contains(QLatin1Char('/'))) {
        return {};
    }
    const QString path = QDir::cleanPath(QStringLiteral(WILDOS_REMOTE_DATADIR "/../wildos-terminal/schemes/") + id + QStringLiteral(".colorscheme"));
    return QFileInfo::exists(path) ? path : QString();
}

QColor schemeColour(const QString &path, const QString &group, const QColor &fallback)
{
    QSettings file(path, QSettings::IniFormat);
    const QStringList parts = file.value(group + QStringLiteral("/Color")).toStringList();
    if (parts.size() < 3) {
        return fallback;
    }
    return QColor(parts[0].toInt(), parts[1].toInt(), parts[2].toInt());
}

// Terminal's own default font (wildos-terminal config.cpp defaultFont), so
// Remote's terminal looks like Terminal until either is changed.
QFont ownFont()
{
    const QStringList families = QFontDatabase::families();
    QFont font = QFontDatabase::systemFont(QFontDatabase::FixedFont);
    for (const QString &f : {QStringLiteral("Cascadia Mono"), QStringLiteral("JetBrains Mono"), QStringLiteral("DejaVu Sans Mono")}) {
        if (families.contains(f)) {
            font = QFont(f);
            break;
        }
    }
    font.setPointSize(11);
    font.setStyleHint(QFont::Monospace);
    return font;
}
}

RemoteSettings *RemoteSettings::instance()
{
    static RemoteSettings *settings = new RemoteSettings;
    return settings;
}

RemoteSettings *RemoteSettings::create(QQmlEngine *, QJSEngine *)
{
    RemoteSettings *settings = instance();
    QJSEngine::setObjectOwnership(settings, QJSEngine::CppOwnership);
    return settings;
}

RemoteSettings::RemoteSettings(QObject *parent)
    : QObject(parent)
    , m_terminal(KSharedConfig::openConfig(QStringLiteral("wildosterminalrc")))
{
    readOwn();
    checkTerminal();
    readTerminal();
    // Terminal announces each change it saves, so open tabs here follow at
    // once, as Terminal's own do.
    m_watcher = KConfigWatcher::create(m_terminal);
    connect(m_watcher.data(), &KConfigWatcher::configChanged, this, &RemoteSettings::readTerminal);
}

void RemoteSettings::readOwn()
{
    QFile file(ownFile());
    if (!file.open(QIODevice::ReadOnly)) {
        return;
    }
    const QJsonObject saved = QJsonDocument::fromJson(file.readAll()).object();
    const QString display = saved.value(QStringLiteral("desktopDisplay")).toString();
    if (display == QLatin1String("window") || display == QLatin1String("full") || display == QLatin1String("all")) {
        m_desktopDisplay = display;
    }
    m_desktopClipboard = saved.value(QStringLiteral("desktopClipboard")).toBool(true);
    m_keepAlive = saved.value(QStringLiteral("keepAlive")).toBool(true);
}

void RemoteSettings::writeOwn()
{
    QDir().mkpath(Paths::config());
    const QJsonObject saved{
        {QStringLiteral("desktopDisplay"), m_desktopDisplay},
        {QStringLiteral("desktopClipboard"), m_desktopClipboard},
        {QStringLiteral("keepAlive"), m_keepAlive},
    };
    QSaveFile file(ownFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(saved).toJson());
        file.commit();
    }
    Q_EMIT changed();
}

void RemoteSettings::setDesktopDisplay(const QString &display)
{
    if (display != m_desktopDisplay && (display == QLatin1String("window") || display == QLatin1String("full") || display == QLatin1String("all"))) {
        m_desktopDisplay = display;
        writeOwn();
    }
}

void RemoteSettings::setDesktopClipboard(bool on)
{
    if (on != m_desktopClipboard) {
        m_desktopClipboard = on;
        writeOwn();
    }
}

void RemoteSettings::setKeepAlive(bool on)
{
    if (on != m_keepAlive) {
        m_keepAlive = on;
        writeOwn();
    }
}

void RemoteSettings::checkTerminal()
{
    const bool installed = !QStandardPaths::findExecutable(terminalProgram).isEmpty();
    if (installed != m_terminalInstalled) {
        m_terminalInstalled = installed;
        Q_EMIT terminalInstalledChanged();
    }
}

void RemoteSettings::openTerminalSettings()
{
    QProcess::startDetached(terminalProgram, {QStringLiteral("--settings")});
}

void RemoteSettings::readTerminal()
{
    m_terminal->reparseConfiguration();
    const KConfigGroup g(m_terminal, QStringLiteral("General"));

    // Terminal's settings, with Terminal's defaults for anything not set
    // there, so a session looks and behaves like Terminal.
    m_schemePath = terminalScheme(g.readEntry("ColourScheme", QStringLiteral("Homebrew")));
    if (m_schemePath.isEmpty()) {
        m_schemePath = ownScheme();
    }
    m_background = schemeColour(m_schemePath, QStringLiteral("Background"), QColor(18, 19, 23));
    m_foreground = schemeColour(m_schemePath, QStringLiteral("Foreground"), QColor(215, 222, 239));

    m_font = ownFont();
    const QString family = g.readEntry("FontFamily", QString());
    if (!family.isEmpty()) {
        m_font.setFamily(family);
    }
    if (g.hasKey("FontSize")) {
        m_font.setPointSize(std::clamp(g.readEntry("FontSize", 11), 6, 40));
    }

    m_cursorShape = g.readEntry("CursorShape", QStringLiteral("bar"));
    m_cursorBlink = g.readEntry("CursorBlink", true);
    m_boldBright = g.readEntry("BoldBright", true);
    m_scrollback = g.readEntry("Scrollback", 10000);
    m_copyOnSelect = g.readEntry("CopyOnSelect", true);
    m_pasteOnRightClick = g.readEntry("PasteOnRightClick", false);
    // Terminal's "Ctrl+C copies and Ctrl+V pastes": Ctrl+C copies a
    // selection not yet copied, otherwise it stops the running command.
    m_ctrlPastes = g.readEntry("CtrlCopyPaste", true);
    m_ctrlCopies = m_ctrlPastes;
    m_warnMultilinePaste = g.readEntry("WarnMultilinePaste", true);

    Q_EMIT terminalChanged();
}
