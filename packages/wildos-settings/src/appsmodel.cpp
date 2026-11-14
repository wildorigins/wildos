#include "appsmodel.h"

#include <KApplicationTrader>
#include <KService>
#include <KShell>

#include <PackageKit/Daemon>

#include <QCollator>
#include <algorithm>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QDirIterator>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QJsonDocument>
#include <QJsonObject>

using PackageKit::Daemon;
using PackageKit::Transaction;

namespace
{
QString homePath()
{
    static const QString home = QDir(QDir::homePath()).canonicalPath();
    return home;
}

bool insideHome(const QString &path)
{
    return !path.isEmpty() && path.startsWith(homePath() + QLatin1Char('/'));
}

// Folders that hold many apps' files. One of them is never removed whole,
// only the app's own folder or file inside it.
QStringList sharedRoots()
{
    const QString h = homePath();
    return {h,
            h + QStringLiteral("/.local"),
            h + QStringLiteral("/.local/bin"),
            h + QStringLiteral("/.local/share"),
            h + QStringLiteral("/.local/state"),
            h + QStringLiteral("/.local/lib"),
            h + QStringLiteral("/.local/opt"),
            h + QStringLiteral("/.config"),
            h + QStringLiteral("/.cache"),
            h + QStringLiteral("/opt"),
            h + QStringLiteral("/Applications"),
            h + QStringLiteral("/bin")};
}

// Names under ~/.local/share and ~/.config that belong to the desktop or to
// many apps, never to the one being removed.
const QStringList sharedNames = {QStringLiteral("applications"), QStringLiteral("icons"), QStringLiteral("pixmaps"), QStringLiteral("flatpak"),
                                 QStringLiteral("Trash"), QStringLiteral("fonts"), QStringLiteral("mime"), QStringLiteral("themes"),
                                 QStringLiteral("kservices6"), QStringLiteral("plasma"), QStringLiteral("dbus-1"), QStringLiteral("systemd"),
                                 QStringLiteral("autostart"), QStringLiteral("wildos"), QStringLiteral("kde"), QStringLiteral("gtk-3.0"),
                                 QStringLiteral("gtk-4.0"), QStringLiteral("recently-used.xbel"), QStringLiteral("user-places.xbel")};

// The program a launcher starts, past "env" and its VAR=value settings.
QString launcherProgram(const QString &exec)
{
    const QStringList words = KShell::splitArgs(exec);
    int i = 0;
    if (i < words.size() && QFileInfo(words.at(i)).fileName() == QLatin1String("env")) {
        ++i;
        while (i < words.size() && (words.at(i).contains(QLatin1Char('=')) || words.at(i).startsWith(QLatin1Char('-')))) {
            ++i;
        }
    }
    if (i >= words.size()) {
        return {};
    }
    const QString program = words.at(i);
    return program.contains(QLatin1Char('/')) ? program : QStandardPaths::findExecutable(program);
}

// The app's own folder or file directly inside one of the shared folders,
// for a path somewhere within it.
QString ownTopLevel(const QString &path)
{
    const QStringList roots = {homePath() + QStringLiteral("/.local/share"), homePath() + QStringLiteral("/.local/opt"),
                               homePath() + QStringLiteral("/.local/lib"), homePath() + QStringLiteral("/opt"),
                               homePath() + QStringLiteral("/Applications"), homePath() + QStringLiteral("/.local/bin"),
                               homePath() + QStringLiteral("/bin")};
    for (const QString &root : roots) {
        if (path.startsWith(root + QLatin1Char('/'))) {
            const QString first = path.mid(root.size() + 1).section(QLatin1Char('/'), 0, 0);
            if (first.isEmpty() || sharedNames.contains(first)) {
                return {};
            }
            return root + QLatin1Char('/') + first;
        }
    }
    return {};
}

void addPath(QStringList &list, const QString &path)
{
    if (!insideHome(path) || sharedRoots().contains(path) || list.contains(path)) {
        return;
    }
    const QFileInfo info(path);
    if (info.exists() || info.isSymLink()) {
        list << path;
    }
}

QString displayPath(const QString &path)
{
    return QStringLiteral("~") + path.mid(homePath().size());
}

// Packages that make up the desktop itself. Removing any of them would take
// Wild OS apart, whatever the dependency tree allows.
const QStringList coreMetapackages = {QStringLiteral("wildos-desktop"), QStringLiteral("wildos-base"), QStringLiteral("wildos-branding")};

// The Store and Settings are how people add, remove and fix everything else,
// so they stay even though Wild OS only recommends the Store.
const QStringList managementPackages = {QStringLiteral("plasma-discover"), QStringLiteral("systemsettings"), QStringLiteral("wildos-settings"),
                                        QStringLiteral("kinfocenter")};

QStringList dependsOf(const QString &status, const QString &package)
{
    const QRegularExpression block(QStringLiteral("(?:^|\\n)Package: %1\\n(.*?)(?:\\n\\n|$)").arg(QRegularExpression::escape(package)),
                                   QRegularExpression::DotMatchesEverythingOption);
    const auto match = block.match(status);
    if (!match.hasMatch()) {
        return {};
    }
    QStringList names;
    const auto lines = match.captured(1).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        if (line.startsWith(QLatin1String("Depends:")) || line.startsWith(QLatin1String("Pre-Depends:"))) {
            const auto items = line.section(QLatin1Char(':'), 1).split(QRegularExpression(QStringLiteral("[,|]")));
            for (const QString &item : items) {
                const QString name = item.trimmed().section(QLatin1Char(' '), 0, 0).section(QLatin1Char(':'), 0, 0);
                if (!name.isEmpty()) {
                    names << name;
                }
            }
        }
    }
    return names;
}
}

AppsModel::AppsModel(QObject *parent)
    : QAbstractListModel(parent)
{
    // Lets polkit ask for the password. Without this hint PackageKit
    // marks every request as non-interactive and polkit refuses at once,
    // so installs failed with "needs the administrator password" and no
    // prompt ever appeared.
    PackageKit::Daemon::global()->setHints(QStringList{QStringLiteral("interactive=true")});
    m_reloadSoon.setSingleShot(true);
    m_reloadSoon.setInterval(1500);
    connect(&m_reloadSoon, &QTimer::timeout, this, &AppsModel::reload);
    const QString launchers = homePath() + QStringLiteral("/.local/share/applications");
    if (QFileInfo::exists(launchers)) {
        m_launchers.addPath(launchers);
    }
    connect(&m_launchers, &QFileSystemWatcher::directoryChanged, &m_reloadSoon, qOverload<>(&QTimer::start));
    reload();
}

QHash<QString, QString> AppsModel::desktopOwners() const
{
    QHash<QString, QString> owners;
    // A Wild OS package that ships a launcher whole under a stock app's id
    // (Write is libreoffice-writer.desktop) owns that app, not the stock
    // package whose launcher it stands in for.
    QHash<QString, QString> shipped;
    QDir info(QStringLiteral("/var/lib/dpkg/info"));
    const auto lists = info.entryList({QStringLiteral("*.list")}, QDir::Files);
    for (const QString &list : lists) {
        QFile file(info.filePath(list));
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        const QString package = list.section(QLatin1Char('.'), 0, -2).section(QLatin1Char(':'), 0, 0);
        while (!file.atEnd()) {
            const QByteArray line = file.readLine().trimmed();
            if (line.startsWith("/usr/share/applications/") && line.endsWith(".desktop")) {
                owners.insert(QFileInfo(QString::fromUtf8(line)).fileName(), package);
            } else if (line.startsWith("/usr/share/wildos/launchers/") && line.endsWith(".desktop")) {
                shipped.insert(QFileInfo(QString::fromUtf8(line)).fileName(), package);
            }
        }
    }
    owners.insert(shipped);
    return owners;
}

QSet<QString> AppsModel::protectedPackages() const
{
    QFile file(QStringLiteral("/var/lib/dpkg/status"));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString status = QString::fromUtf8(file.readAll());
    QSet<QString> names(coreMetapackages.begin(), coreMetapackages.end());
    names.unite(QSet<QString>(managementPackages.begin(), managementPackages.end()));
    for (const QString &meta : coreMetapackages) {
        const auto deps = dependsOf(status, meta);
        names.unite(QSet<QString>(deps.begin(), deps.end()));
    }
    return names;
}

void AppsModel::reload()
{
    const QHash<QString, QString> owners = desktopOwners();
    const QSet<QString> core = protectedPackages();

    QList<App> apps;
    QSet<QString> seen;
    const KService::List services = KApplicationTrader::query([](const KService::Ptr &service) {
        return !service->noDisplay() && service->showOnCurrentPlatform();
    });
    for (const KService::Ptr &service : services) {
        const QString id = service->desktopEntryName();
        // Just uninstalled: the launcher is gone before the menu cache knows.
        if (seen.contains(id) || service->name().isEmpty() || !QFileInfo::exists(service->entryPath())) {
            continue;
        }
        seen.insert(id);
        App app;
        app.name = service->name();
        app.icon = service->icon();
        app.id = id;
        app.flatpak = service->property<QString>(QStringLiteral("X-Flatpak"));
        app.userFlatpak = service->entryPath().contains(QLatin1String("/.local/share/flatpak/"));
        app.package = owners.value(QFileInfo(service->entryPath()).fileName());
        // Wild OS's own apps are only recommended by the desktop, so they
        // can go; what the desktop depends on cannot.
        app.protectedApp = app.flatpak.isEmpty() && (app.package.isEmpty() || core.contains(app.package));
        // A launcher in the home folder for a program in the home folder,
        // put there by the app's own installer. Windows mode writes its
        // apps' launchers there too.
        const QString entry = QFileInfo(service->entryPath()).canonicalFilePath();
        if (id.startsWith(QLatin1String("wildos-winmode-")) && service->property<QString>(QStringLiteral("X-WildOS-Engine")) == QLatin1String("windows-mode")) {
            app.windowsApp = true;
            app.protectedApp = false;
        } else if (app.flatpak.isEmpty() && app.package.isEmpty() && !id.startsWith(QLatin1String("wildos-"))
            && entry.startsWith(homePath() + QStringLiteral("/.local/share/applications/"))) {
            findHomeFiles(app, entry, service->exec(), service->icon());
            app.homeApp = !app.files.isEmpty();
            app.protectedApp = !app.homeApp;
        }
        apps << app;
    }

    QCollator collator;
    collator.setCaseSensitivity(Qt::CaseInsensitive);
    std::sort(apps.begin(), apps.end(), [&collator](const App &a, const App &b) {
        return collator.compare(a.name, b.name) < 0;
    });

    // Two launchers can share a folder or a maker's prefix (an app suite).
    // A file more than one of them would take stays, so removing one never
    // breaks the other; only each launcher's own files go with it.
    QHash<QString, int> claims;
    for (const App &app : std::as_const(apps)) {
        for (const QString &path : app.files) {
            claims[path] += 1;
        }
    }
    for (App &app : apps) {
        app.files.erase(std::remove_if(app.files.begin(), app.files.end(),
                                       [&claims](const QString &path) {
                                           return claims.value(path) > 1;
                                       }),
                        app.files.end());
    }

    m_all = apps;
    applyFilter();
}

void AppsModel::findHomeFiles(App &app, const QString &entryPath, const QString &exec, const QString &icon)
{
    const QString program = launcherProgram(exec);
    const QString target = QFileInfo(program).canonicalFilePath();
    if (!insideHome(target)) {
        return;
    }
    // A program outside the folders apps install into (a build in someone's
    // projects folder) stays; only its launcher goes.
    // The app's own folder holds the program (or is it, for an AppImage or a
    // single file in ~/.local/bin), so the program is not listed twice.
    QStringList files;
    files << entryPath;
    const QString folder = ownTopLevel(target);
    addPath(files, folder);
    // The program as the launcher names it, often a link in ~/.local/bin.
    addPath(files, ownTopLevel(QFileInfo(program).absoluteFilePath()));

    // Names the app's files go by: the launcher's, the program's and its
    // folder's, and the maker's prefix ("herdr" for herdr-app) for the other
    // programs an installer drops beside it.
    QSet<QString> names = {QFileInfo(entryPath).completeBaseName(), QFileInfo(program).fileName(), QFileInfo(target).fileName()};
    if (!folder.isEmpty()) {
        names.insert(QFileInfo(folder).fileName());
    }
    const QString prefix = QFileInfo(entryPath).completeBaseName().section(QRegularExpression(QStringLiteral("[-_.]")), 0, 0);
    for (const QString &bin : {homePath() + QStringLiteral("/.local/bin"), homePath() + QStringLiteral("/bin")}) {
        const auto entries = QDir(bin).entryInfoList(QDir::Files | QDir::System | QDir::NoDotAndDotDot);
        for (const QFileInfo &info : entries) {
            const QString name = info.fileName();
            const bool samePrefix = prefix.size() >= 4 && (name == prefix || name.startsWith(prefix + QLatin1Char('-')));
            const QString points = info.isSymLink() ? info.canonicalFilePath() : QString();
            const bool intoFolder = !folder.isEmpty() && points.startsWith(folder + QLatin1Char('/'));
            if (samePrefix || intoFolder) {
                addPath(files, info.absoluteFilePath());
                names.insert(name);
            }
        }
    }

    if (QFileInfo(icon).isAbsolute()) {
        addPath(files, QFileInfo(icon).canonicalFilePath());
    } else if (!icon.isEmpty()) {
        for (const QString &dir : {homePath() + QStringLiteral("/.local/share/icons"), homePath() + QStringLiteral("/.local/share/pixmaps")}) {
            QDirIterator it(dir, {icon + QStringLiteral(".svg"), icon + QStringLiteral(".svgz"), icon + QStringLiteral(".png"), icon + QStringLiteral(".xpm")},
                            QDir::Files, QDirIterator::Subdirectories);
            while (it.hasNext()) {
                addPath(files, it.next());
            }
        }
    }

    // Settings and data in the usual places under the same names, offered
    // separately: someone reinstalling may want to keep them.
    QStringList data;
    for (const QString &name : std::as_const(names)) {
        if (name.isEmpty() || sharedNames.contains(name)) {
            continue;
        }
        for (const QString &base : {QStringLiteral("/.config/"), QStringLiteral("/.local/share/"), QStringLiteral("/.local/state/"), QStringLiteral("/.cache/")}) {
            const QString path = homePath() + base + name;
            if (!files.contains(path)) {
                addPath(data, path);
            }
        }
    }
    data.sort();
    app.files = files;
    app.data = data;
}

QString AppsModel::nameOf(const QString &id) const
{
    for (const App &app : m_all) {
        if (app.id == id || app.id + QStringLiteral(".desktop") == id) {
            return app.name;
        }
    }
    return {};
}

bool AppsModel::canUninstall(const QString &id) const
{
    for (const App &app : m_all) {
        if (app.id == id || app.id + QStringLiteral(".desktop") == id) {
            return !app.protectedApp;
        }
    }
    return false;
}

bool AppsModel::isWindowsApp(const QString &id) const
{
    for (const App &app : m_all) {
        if (app.id == id || app.id + QStringLiteral(".desktop") == id) {
            return app.windowsApp;
        }
    }
    return false;
}

QStringList AppsModel::homeFiles(const QString &id) const
{
    QStringList shown;
    for (const App &app : m_all) {
        if (app.id == id && app.homeApp) {
            for (const QString &path : app.files) {
                shown << displayPath(path);
            }
        }
    }
    return shown;
}

QStringList AppsModel::homeData(const QString &id) const
{
    QStringList shown;
    for (const App &app : m_all) {
        if (app.id == id && app.homeApp) {
            for (const QString &path : app.data) {
                shown << displayPath(path);
            }
        }
    }
    return shown;
}

void AppsModel::setFilter(const QString &filter)
{
    if (filter == m_filter) {
        return;
    }
    m_filter = filter;
    Q_EMIT filterChanged();
    applyFilter();
}

void AppsModel::applyFilter()
{
    beginResetModel();
    m_shown.clear();
    const QString needle = m_filter.trimmed();
    for (const App &app : std::as_const(m_all)) {
        if (needle.isEmpty() || app.name.contains(needle, Qt::CaseInsensitive)) {
            m_shown << app;
        }
    }
    endResetModel();
    Q_EMIT countChanged();
}

int AppsModel::rowCount(const QModelIndex &parent) const
{
    return parent.isValid() ? 0 : int(m_shown.size());
}

QVariant AppsModel::data(const QModelIndex &index, int role) const
{
    if (!index.isValid() || index.row() >= m_shown.size()) {
        return {};
    }
    const App &app = m_shown.at(index.row());
    switch (role) {
    case NameRole:
        return app.name;
    case IconRole:
        return app.icon;
    case IdRole:
        return app.id;
    case SourceRole:
        return app.windowsApp ? QStringLiteral("windows") : app.homeApp ? QStringLiteral("home") : app.flatpak.isEmpty() ? QStringLiteral("system") : QStringLiteral("store");
    case PackageRole:
        return app.flatpak.isEmpty() ? app.package : app.flatpak;
    case CanUninstallRole:
        return !app.protectedApp;
    }
    return {};
}

QHash<int, QByteArray> AppsModel::roleNames() const
{
    return {
        {NameRole, "name"},
        {IconRole, "appIcon"},
        {IdRole, "appId"},
        {SourceRole, "source"},
        {PackageRole, "packageName"},
        {CanUninstallRole, "canUninstall"},
    };
}

void AppsModel::uninstall(const QString &id, bool withData)
{
    if (!m_busyId.isEmpty()) {
        return;
    }
    auto it = std::find_if(m_all.cbegin(), m_all.cend(), [&id](const App &app) {
        return app.id == id;
    });
    if (it == m_all.cend() || it->protectedApp) {
        return;
    }
    const App app = *it;
    m_busyId = id;
    Q_EMIT busyChanged();

    if (app.windowsApp) {
        uninstallWindowsApp(app);
        return;
    }

    // Everything goes to the Trash, so a file that was not the app's after
    // all can be put back. Each path is checked again: it must still be
    // inside the home folder and not one of the shared folders.
    if (app.homeApp) {
        QStringList paths = app.files;
        if (withData) {
            paths << app.data;
        }
        QStringList stuck;
        for (const QString &path : std::as_const(paths)) {
            const QFileInfo info(path);
            if (!insideHome(path) || sharedRoots().contains(path) || (!info.exists() && !info.isSymLink())) {
                continue;
            }
            // A link holds nothing to put back, and once its target is in the
            // Trash it points nowhere and cannot be moved there itself.
            if (info.isSymLink() ? !QFile::remove(path) : !QFile::moveToTrash(path)) {
                stuck << displayPath(path);
            }
        }
        finishUninstall(app, stuck.isEmpty(), stuck.isEmpty() ? QString() : QStringLiteral("These could not be moved to the Recycle Bin: ") + stuck.join(QStringLiteral(", ")));
        return;
    }

    if (!app.flatpak.isEmpty()) {
        auto process = new QProcess(this);
        connect(process, &QProcess::finished, this, [this, process, app](int code, QProcess::ExitStatus) {
            process->deleteLater();
            finishUninstall(app, code == 0, QString::fromUtf8(process->readAllStandardError()).trimmed());
        });
        QStringList args = {QStringLiteral("uninstall"), QStringLiteral("--noninteractive"), QStringLiteral("-y")};
        args << (app.userFlatpak ? QStringLiteral("--user") : QStringLiteral("--system")) << app.flatpak;
        process->start(QStringLiteral("flatpak"), args);
        return;
    }

    Transaction *resolve = Daemon::resolve(app.package, Transaction::FilterInstalled);
    auto packageId = std::make_shared<QString>();
    connect(resolve, &Transaction::package, this, [packageId](Transaction::Info, const QString &pid, const QString &) {
        *packageId = pid;
    });
    connect(resolve, &Transaction::finished, this, [this, packageId, app](Transaction::Exit status, uint) {
        if (status != Transaction::ExitSuccess || packageId->isEmpty()) {
            finishUninstall(app, false, QString());
            return;
        }
        // allowDeps stays off: if anything else needs this app, PackageKit
        // refuses rather than quietly taking other apps with it.
        Transaction *remove = Daemon::removePackages({*packageId}, false, true);
        auto reason = std::make_shared<QString>();
        connect(remove, &Transaction::errorCode, this, [reason](Transaction::Error error, const QString &) {
            *reason = error == Transaction::ErrorDepResolutionFailed || error == Transaction::ErrorCannotRemoveSystemPackage
                ? QStringLiteral("Other apps need it, so it was left in place.")
                : error == Transaction::ErrorNotAuthorized ? QStringLiteral("It was not removed because the password was not given.") : QString();
        });
        connect(remove, &Transaction::finished, this, [this, app, reason](Transaction::Exit status, uint) {
            finishUninstall(app, status == Transaction::ExitSuccess, *reason);
        });
    });
}

// Windows mode may have to start Windows first, so the call is given as
// long as a cold start and Windows finding the uninstaller take.
void AppsModel::uninstallWindowsApp(const App &app)
{
    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.wildos.WindowsMode1"), QStringLiteral("/org/wildos/WindowsMode1"),
                                                       QStringLiteral("org.wildos.WindowsMode1"), QStringLiteral("UninstallApp"));
    call << app.id.mid(QStringLiteral("wildos-winmode-").size());
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call, 600000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, app] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            finishUninstall(app, false, QStringLiteral("Windows mode didn't answer. Restart the PC, then try again."));
            return;
        }
        const QJsonObject answer = QJsonDocument::fromJson(reply.value().toUtf8()).object();
        const QString state = answer.value(QStringLiteral("state")).toString();
        if (state == QLatin1String("opened")) {
            m_busyId.clear();
            Q_EMIT busyChanged();
            Q_EMIT uninstallOpened(app.name);
            return;
        }
        finishUninstall(app, state == QLatin1String("removed"), answer.value(QStringLiteral("message")).toString());
    });
}

void AppsModel::finishUninstall(const App &app, bool ok, const QString &reason)
{
    m_busyId.clear();
    Q_EMIT busyChanged();
    if (ok) {
        Q_EMIT uninstalled(app.name);
        reload();
    } else {
        Q_EMIT uninstallFailed(app.name, reason);
    }
}
