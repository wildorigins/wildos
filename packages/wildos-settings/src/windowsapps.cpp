#include "windowsapps.h"

#include <KConfigGroup>
#include <KDesktopFile>
#include <KSharedConfig>

#include <QCollator>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QStandardPaths>

namespace
{
const QString configName = QStringLiteral("wildos-appbridgerc");

KConfigGroup settings()
{
    return KSharedConfig::openConfig(configName)->group(QStringLiteral("General"));
}

QString launchersDir()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/applications");
}

// Launchers that start a program inside this app's prefix. Wine files its
// own under applications/wine/Programs, so the search goes all the way down.
QStringList launchersFor(const QString &prefix)
{
    QStringList found;
    QDirIterator it(launchersDir(), {QStringLiteral("*.desktop")}, QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        KDesktopFile file(path);
        const KConfigGroup entry = file.desktopGroup();
        const QString exec = entry.readEntry("Exec");
        const QString workDir = entry.readEntry("Path");
        if (exec.contains(prefix) || workDir.startsWith(prefix)) {
            found << path;
        }
    }
    return found;
}
}

WindowsApps::WindowsApps(QObject *parent)
    : QObject(parent)
{
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &WindowsApps::reload);
    reload();
}

QString WindowsApps::root()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wildos/windows-apps");
}

bool WindowsApps::installed() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("wildos-appbridge")).isEmpty()
        || !QStandardPaths::locate(QStandardPaths::ApplicationsLocation, QStringLiteral("org.wildos.appbridge.desktop")).isEmpty();
}

bool WindowsApps::enabled() const
{
    return settings().readEntry("Enabled", true);
}

QString WindowsApps::startingPermissions() const
{
    return settings().readEntry("StartingPermissions", QStringLiteral("documents-offline"));
}

void WindowsApps::setEnabled(bool on)
{
    KConfigGroup group = settings();
    group.writeEntry("Enabled", on, KConfig::Notify);
    group.sync();
    Q_EMIT settingsChanged();
}

void WindowsApps::setStartingPermissions(const QString &value)
{
    KConfigGroup group = settings();
    group.writeEntry("StartingPermissions", value, KConfig::Notify);
    group.sync();
    Q_EMIT settingsChanged();
}

void WindowsApps::watch()
{
    if (!m_watcher.directories().isEmpty()) {
        m_watcher.removePaths(m_watcher.directories());
    }
    // Watch the nearest folder that exists, so the first app the bridge
    // installs still shows up while the page is open.
    QString dir = root();
    while (!QFileInfo::exists(dir) && dir.contains(QLatin1Char('/'))) {
        dir = dir.section(QLatin1Char('/'), 0, -2);
    }
    if (!dir.isEmpty()) {
        m_watcher.addPath(dir);
    }
}

void WindowsApps::reload()
{
    watch();
    QVariantList list;
    QDir dir(root());
    const auto names = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &name : names) {
        const QString prefix = dir.filePath(name);
        QString title = name;
        QString icon;
        const QStringList launchers = launchersFor(prefix);
        if (!launchers.isEmpty()) {
            KDesktopFile file(launchers.constFirst());
            if (!file.readName().isEmpty()) {
                title = file.readName();
            }
            icon = file.readIcon();
        }

        QVariantList permissions;
        const QString appConf = prefix + QStringLiteral("/app.conf");
        if (QFileInfo::exists(appConf)) {
            KSharedConfig::Ptr conf = KSharedConfig::openConfig(appConf, KConfig::SimpleConfig);
            const KConfigGroup group = conf->group(QStringLiteral("App"));
            title = group.readEntry("Name", title);
            icon = group.readEntry("Icon", icon);
            const KConfigGroup p = conf->group(QStringLiteral("Permissions"));
            const bool documents = p.readEntry("Documents", false);
            const bool printer = p.readEntry("Printer", false);
            const bool internet = p.readEntry("Internet", false);
            permissions << QVariantMap{{QStringLiteral("text"), documents ? tr("Documents") : tr("Its own folder")},
                                       {QStringLiteral("granted"), true}};
            if (printer) {
                permissions << QVariantMap{{QStringLiteral("text"), tr("Printer")}, {QStringLiteral("granted"), true}};
            }
            permissions << QVariantMap{{QStringLiteral("text"), internet ? tr("Internet") : tr("No internet")},
                                       {QStringLiteral("granted"), internet}};
        }

        list << QVariantMap{
            {QStringLiteral("name"), name},
            {QStringLiteral("title"), title},
            {QStringLiteral("icon"), icon},
            {QStringLiteral("permissions"), permissions},
            {QStringLiteral("launchers"), launchers},
        };
    }
    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(list.begin(), list.end(), [&collator](const QVariant &a, const QVariant &b) {
        return collator.compare(a.toMap().value(QStringLiteral("title")).toString(), b.toMap().value(QStringLiteral("title")).toString()) < 0;
    });
    m_apps = list;
    Q_EMIT appsChanged();
}

// Uninstalling a Windows app removes its prefix and launchers. Both go to
// the Recycle Bin rather than being deleted, so a mistake can be undone.
bool WindowsApps::uninstall(const QString &name)
{
    if (name.isEmpty() || name.contains(QLatin1Char('/')) || name.startsWith(QLatin1Char('.'))) {
        return false;
    }
    const QString prefix = root() + QLatin1Char('/') + name;
    if (!QFileInfo(prefix).isDir()) {
        return false;
    }
    const QStringList launchers = launchersFor(prefix);
    for (const QString &launcher : launchers) {
        QFile::moveToTrash(launcher);
    }
    const bool ok = QFile::moveToTrash(prefix);
    reload();
    return ok;
}
