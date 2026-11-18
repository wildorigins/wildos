// The parts of FileOps behind the file menu and Properties that
// DialogFileMenu.dc.html added: Compress to ZIP file, Send to, Pin to Start,
// network sharing, previous versions and the Properties dialog's OK.

#include "fileops.h"

#include "drives.h"
#include "format.h"
#include "settings.h"
#include "zipjob.h"

#include <KApplicationTrader>
#include <KIO/CopyJob>
#include <KIO/JobTracker>
#include <KJobTrackerInterface>
#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>

namespace
{
QString folderPath(const QUrl &folder, const QString &name)
{
    return folder.path() + (folder.path().endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + name;
}

bool bluetoothPresent()
{
    return !QDir(QStringLiteral("/sys/class/bluetooth")).entryList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::System).isEmpty()
        && !QStandardPaths::findExecutable(QStringLiteral("bluedevil-sendfile")).isEmpty();
}

bool mailPresent()
{
    return !QStandardPaths::findExecutable(QStringLiteral("xdg-email")).isEmpty()
        && KApplicationTrader::preferredService(QStringLiteral("x-scheme-handler/mailto"));
}

// Samba user shares: what Windows calls sharing a folder on the network.
// They need Samba installed and the usershare directory it creates.
bool sambaPresent()
{
    return !QStandardPaths::findExecutable(QStringLiteral("net")).isEmpty() && QDir(QStringLiteral("/var/lib/samba/usershares")).exists();
}

QString runNet(const QStringList &args, QString *error = nullptr)
{
    QProcess net;
    net.start(QStringLiteral("net"), QStringList{QStringLiteral("usershare")} + args);
    if (!net.waitForFinished(8000) || net.exitCode() != 0) {
        if (error) {
            *error = QString::fromLocal8Bit(net.readAllStandardError()).trimmed();
        }
        return QString();
    }
    return QString::fromLocal8Bit(net.readAllStandardOutput());
}

// The share's name and path for each of this account's user shares.
QList<QPair<QString, QString>> userShares()
{
    QList<QPair<QString, QString>> shares;
    QString name;
    const QStringList lines = runNet({QStringLiteral("info")}).split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const QString text = line.trimmed();
        if (text.startsWith(QLatin1Char('[')) && text.endsWith(QLatin1Char(']'))) {
            name = text.mid(1, text.size() - 2);
        } else if (text.startsWith(QLatin1String("path=")) && !name.isEmpty()) {
            shares.append({name, QDir::cleanPath(text.mid(5))});
        }
    }
    return shares;
}
}

bool FileOps::canWriteIn(const QUrl &folder) const
{
    return folder.isLocalFile() && QFileInfo(folder.toLocalFile()).isWritable();
}

void FileOps::compress(const QList<QUrl> &urls)
{
    if (urls.isEmpty() || !urls.constFirst().isLocalFile()) {
        return;
    }
    // Named after the item, as File Explorer does: "Budget 2026.zip".
    const QFileInfo first(urls.constFirst().toLocalFile());
    const QString base = first.isDir() ? first.fileName() : first.completeBaseName().isEmpty() ? first.fileName() : first.completeBaseName();
    const QUrl folder = QUrl::fromLocalFile(first.absolutePath());
    if (!QFileInfo(first.absolutePath()).isWritable()) {
        Q_EMIT failed(QCoreApplication::translate("FileOps", "You cannot save in this folder, so the ZIP file has nowhere to go. Copy the items somewhere of yours first."));
        return;
    }
    QUrl target = folder;
    target.setPath(folderPath(folder, uniqueName(folder, base, QStringLiteral(".zip"))));

    auto job = new ZipJob(urls, target, this);
    if (auto tracker = KIO::getJobTracker()) {
        tracker->registerJob(job);
    }
    connect(job, &KJob::result, this, [this, job, folder, target] {
        if (job->error() == 0) {
            // Windows leaves the new archive selected with its name ready
            // to change.
            Q_EMIT created(folder, {target}, true);
        } else if (job->error() != KJob::KilledJobError) {
            Q_EMIT failed(job->errorText());
        }
    });
    job->start();
}

bool FileOps::canUseArchive() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("wildos-archive")).isEmpty();
}

bool FileOps::areArchives(const QList<QUrl> &urls) const
{
    // The types Archive opens, by their contents first and then by name, so
    // a .tgz or a .zip with an odd ending is still found.
    static const QStringList types = {
        QStringLiteral("application/zip"), QStringLiteral("application/x-7z-compressed"), QStringLiteral("application/vnd.rar"),
        QStringLiteral("application/x-tar"), QStringLiteral("application/x-compressed-tar"), QStringLiteral("application/x-bzip2-compressed-tar"),
        QStringLiteral("application/x-bzip-compressed-tar"), QStringLiteral("application/x-xz-compressed-tar"),
        QStringLiteral("application/x-zstd-compressed-tar"), QStringLiteral("application/x-lzma-compressed-tar"), QStringLiteral("application/gzip"),
        QStringLiteral("application/x-bzip2"), QStringLiteral("application/x-bzip"), QStringLiteral("application/x-xz"),
        QStringLiteral("application/zstd"), QStringLiteral("application/x-lzma"), QStringLiteral("application/x-cpio"),
        QStringLiteral("application/vnd.ms-cab-compressed"), QStringLiteral("application/x-lha"),
    };
    if (urls.isEmpty() || !canUseArchive()) {
        return false;
    }
    static QMimeDatabase db;
    for (const QUrl &url : urls) {
        if (!url.isLocalFile()) {
            return false;
        }
        const QFileInfo info(url.toLocalFile());
        if (!info.isFile()) {
            return false;
        }
        const QMimeType mime = db.mimeTypeForFile(info);
        const bool known = std::any_of(types.cbegin(), types.cend(), [&](const QString &type) {
            return mime.inherits(type);
        });
        if (!known) {
            return false;
        }
    }
    return true;
}

void FileOps::compressWith(const QList<QUrl> &urls)
{
    QStringList args{QStringLiteral("--compress")};
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            args.append(url.toLocalFile());
        }
    }
    if (!QProcess::startDetached(QStringLiteral("wildos-archive"), args)) {
        Q_EMIT failed(QCoreApplication::translate("FileOps", "Archive could not be opened."));
    }
}

void FileOps::extractHere(const QList<QUrl> &urls)
{
    QStringList args{QStringLiteral("--extract-here")};
    for (const QUrl &url : urls) {
        if (url.isLocalFile()) {
            args.append(url.toLocalFile());
        }
    }
    if (!QFileInfo(QFileInfo(urls.value(0).toLocalFile()).absolutePath()).isWritable()) {
        Q_EMIT failed(QCoreApplication::translate("FileOps", "You cannot save in this folder, so there is nowhere to extract to. Copy the archive somewhere of yours first."));
        return;
    }
    if (!QProcess::startDetached(QStringLiteral("wildos-archive"), args)) {
        Q_EMIT failed(QCoreApplication::translate("FileOps", "Archive could not be opened."));
    }
}

QVariantList FileOps::sendTargets(const QList<QUrl> &urls) const
{
    QVariantList list;
    if (urls.isEmpty()) {
        return list;
    }
    bool local = true;
    bool onlyFiles = true;
    for (const QUrl &url : urls) {
        local = local && url.isLocalFile();
        onlyFiles = onlyFiles && url.isLocalFile() && QFileInfo(url.toLocalFile()).isFile();
    }
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (local && !desktop.isEmpty() && QDir::cleanPath(desktop) != QDir::homePath() && QFileInfo(desktop).isDir()) {
        list.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("desktop")}, {QStringLiteral("text"), QCoreApplication::translate("FileOps", "Desktop (create shortcut)")},
                                {QStringLiteral("icon"), QStringLiteral("desktop")}});
    }
    if (local) {
        list.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("zip")}, {QStringLiteral("text"), QCoreApplication::translate("FileOps", "Compressed (zipped) folder")},
                                {QStringLiteral("icon"), QStringLiteral("zip")}});
    }
    // Mail and Bluetooth carry files, not folders; each appears only when
    // the computer can actually do it.
    if (onlyFiles && mailPresent()) {
        list.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("mail")}, {QStringLiteral("text"), QCoreApplication::translate("FileOps", "Mail recipient")},
                                {QStringLiteral("icon"), QStringLiteral("mail")}});
    }
    if (onlyFiles && bluetoothPresent()) {
        list.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("bluetooth")}, {QStringLiteral("text"), QCoreApplication::translate("FileOps", "Bluetooth device")},
                                {QStringLiteral("icon"), QStringLiteral("bluetooth")}});
    }
    const auto drives = Drives::instance()->list();
    for (const Drive &drive : drives) {
        if (drive.removable && drive.mounted && !drive.mountPath.isEmpty()) {
            list.append(QVariantMap{{QStringLiteral("kind"), QStringLiteral("drive")}, {QStringLiteral("text"), drive.name},
                                    {QStringLiteral("themeIcon"), drive.themeIcon + QStringLiteral(",drive-removable-media")},
                                    {QStringLiteral("target"), QUrl::fromLocalFile(drive.mountPath)}});
        }
    }
    return list;
}

void FileOps::sendTo(const QList<QUrl> &urls, const QString &kind, const QUrl &target)
{
    if (urls.isEmpty()) {
        return;
    }
    if (kind == QLatin1String("desktop")) {
        const QUrl desktop = QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DesktopLocation));
        for (const QUrl &url : urls) {
            const QFileInfo info(url.toLocalFile());
            newShortcut(desktop, info.absoluteFilePath(), Format::visibleName(info.fileName(), info.isDir(), Settings::instance()->showExtensions()));
        }
        Q_EMIT notice(urls.size() == 1 ? QCoreApplication::translate("FileOps", "Shortcut added to the desktop") : QCoreApplication::translate("FileOps", "Shortcuts added to the desktop"));
    } else if (kind == QLatin1String("zip")) {
        compress(urls);
    } else if (kind == QLatin1String("mail")) {
        QStringList args;
        for (const QUrl &url : urls) {
            args << QStringLiteral("--attach") << url.toLocalFile();
        }
        if (!QProcess::startDetached(QStringLiteral("xdg-email"), args)) {
            Q_EMIT failed(QCoreApplication::translate("FileOps", "Your mail app could not be opened."));
        }
    } else if (kind == QLatin1String("bluetooth")) {
        QStringList args;
        for (const QUrl &url : urls) {
            args << QStringLiteral("-f") << url.toLocalFile();
        }
        if (!QProcess::startDetached(QStringLiteral("bluedevil-sendfile"), args)) {
            Q_EMIT failed(QCoreApplication::translate("FileOps", "Bluetooth could not be started."));
        }
    } else if (kind == QLatin1String("drive") && target.isValid()) {
        drop(urls, target, Qt::CopyAction);
    }
}

bool FileOps::canPinToStart() const
{
    auto bus = QDBusConnection::sessionBus().interface();
    if (!bus) {
        return false;
    }
    const QString service = QStringLiteral("org.kde.ActivityManager");
    return bus->isServiceRegistered(service) || bus->activatableServiceNames().value().contains(service);
}

void FileOps::pinToStart(const QList<QUrl> &urls)
{
    // Start's pinned apps are the activity manager's favourites, the list
    // Start's own "Pin to Start" adds to (org.wildos.start main.qml uses
    // Kicker's KAStats favourites model). Files and folders are linked under
    // the documents agent, which that model lists alongside apps.
    for (const QUrl &url : urls) {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.ActivityManager"), QStringLiteral("/ActivityManager/Resources/Linking"),
                                                      QStringLiteral("org.kde.ActivityManager.ResourcesLinking"), QStringLiteral("LinkResourceToActivity"));
        message << QStringLiteral("org.kde.plasma.favorites.documents") << url.toString() << QStringLiteral(":global");
        const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 3000);
        if (reply.type() == QDBusMessage::ErrorMessage) {
            Q_EMIT failed(QCoreApplication::translate("FileOps", "Start could not be reached, so nothing was pinned. Try again in a moment."));
            return;
        }
    }
    Q_EMIT notice(QCoreApplication::translate("FileOps", "Pinned to Start"));
}

QVariantMap FileOps::networkShare(const QUrl &folder) const
{
    QVariantMap state{{QStringLiteral("available"), sambaPresent()}, {QStringLiteral("shared"), false},
                      {QStringLiteral("computer"), QSysInfo::machineHostName()}};
    if (!state.value(QStringLiteral("available")).toBool() || !folder.isLocalFile()) {
        return state;
    }
    const QString path = QDir::cleanPath(folder.toLocalFile());
    const auto shares = userShares();
    for (const auto &share : shares) {
        if (share.second == path) {
            state[QStringLiteral("shared")] = true;
            state[QStringLiteral("name")] = share.first;
        }
    }
    return state;
}

void FileOps::setNetworkShare(const QUrl &folder, bool shared)
{
    const QString path = QDir::cleanPath(folder.toLocalFile());
    QString error;
    if (shared) {
        // Share names cannot hold these characters; the folder's own name
        // without them is what other computers see.
        QString name = QFileInfo(path).fileName();
        static const QString banned = QStringLiteral("%<>*?|/\\+=;:\",");
        for (const QChar c : banned) {
            name.remove(c);
        }
        if (name.isEmpty()) {
            name = QStringLiteral("Shared");
        }
        const auto shares = userShares();
        QString unique = name;
        for (int n = 2; std::any_of(shares.cbegin(), shares.cend(), [&unique](const auto &s) { return s.first.compare(unique, Qt::CaseInsensitive) == 0; }); ++n) {
            unique = QStringLiteral("%1 %2").arg(name).arg(n);
        }
        runNet({QStringLiteral("add"), unique, path, QString(), QStringLiteral("Everyone:R"), QStringLiteral("guest_ok=n")}, &error);
    } else {
        const auto shares = userShares();
        for (const auto &share : shares) {
            if (share.second == path) {
                runNet({QStringLiteral("delete"), share.first}, &error);
            }
        }
    }
    if (!error.isEmpty()) {
        Q_EMIT failed(QCoreApplication::translate("FileOps", "Network sharing did not change. Your account may need to be allowed to share folders (the sambashare group)."));
    }
}

QVariantList FileOps::previousVersions(const QUrl &url) const
{
    // Earlier copies exist only where the drive keeps snapshots: Btrfs with
    // Snapper (/.snapshots/N/snapshot) or ZFS (/.zfs/snapshot/NAME). Each
    // snapshot holding this path is a previous version.
    QVariantList list;
    if (!url.isLocalFile()) {
        return list;
    }
    const QString path = QDir::cleanPath(url.toLocalFile());
    const QString root = QDir::cleanPath(QStorageInfo(path).rootPath());
    const QString relative = QDir(root).relativeFilePath(path);
    const QFileInfo current(path);

    struct Found {
        QDateTime when;
        QString path;
    };
    QList<Found> found;
    auto scan = [&](const QString &dir, const QString &inner) {
        const QStringList names = QDir(dir).entryList(QDir::Dirs | QDir::NoDotAndDotDot);
        for (const QString &name : names) {
            const QString base = dir + QLatin1Char('/') + name + inner;
            const QFileInfo copy(base + QLatin1Char('/') + relative);
            if (copy.exists()) {
                found.append({QFileInfo(base).lastModified(), copy.absoluteFilePath()});
            }
        }
    };
    scan(root + QStringLiteral("/.snapshots"), QStringLiteral("/snapshot"));
    scan(root + QStringLiteral("/.zfs/snapshot"), QString());

    std::sort(found.begin(), found.end(), [](const Found &a, const Found &b) {
        return a.when > b.when;
    });
    for (const Found &f : std::as_const(found)) {
        const QFileInfo copy(f.path);
        // A snapshot with the file unchanged since is not an earlier version.
        if (!current.isDir() && copy.lastModified() == current.lastModified() && copy.size() == current.size()) {
            continue;
        }
        list.append(QVariantMap{{QStringLiteral("label"), Format::longDate(f.when)},
                                {QStringLiteral("detail"), current.isDir() ? QString() : QCoreApplication::translate("FileOps", "%1, changed %2").arg(Format::size(copy.size()), Format::longDate(copy.lastModified()))},
                                {QStringLiteral("url"), QUrl::fromLocalFile(f.path)}});
    }
    return list;
}

void FileOps::restoreVersion(const QUrl &version, const QUrl &current)
{
    QVariantMap prompt;
    prompt[QStringLiteral("kind")] = QStringLiteral("confirm");
    prompt[QStringLiteral("title")] = QCoreApplication::translate("FileOps", "Restore this version?");
    prompt[QStringLiteral("text")] = QCoreApplication::translate("FileOps", "%1 will be replaced with the earlier copy. You cannot undo this.").arg(current.fileName());
    prompt[QStringLiteral("primary")] = QCoreApplication::translate("FileOps", "Restore");
    ask(prompt, [this, version, current](const QString &choice, bool) {
        if (choice != QLatin1String("primary")) {
            return;
        }
        auto job = KIO::copyAs(version, current, KIO::Overwrite);
        job->setUiDelegate(delegate());
        watchJob(job, QCoreApplication::translate("FileOps", "restore this"));
    });
}

void FileOps::duplicate(const QUrl &url)
{
    const QFileInfo info(url.toLocalFile());
    const QUrl folder = QUrl::fromLocalFile(info.absolutePath());
    const QString suffix = info.isDir() || info.suffix().isEmpty() ? QString() : QLatin1Char('.') + info.suffix();
    const QString base = info.isDir() || suffix.isEmpty() ? info.fileName() : info.completeBaseName();
    QUrl target = folder;
    target.setPath(folderPath(folder, uniqueName(folder, base + QStringLiteral(" (copy)"), suffix)));
    auto job = KIO::copyAs(url, target);
    job->setUiDelegate(delegate());
    connect(job, &KJob::result, this, [this, job, folder, target] {
        if (!job->error()) {
            Q_EMIT notice(QCoreApplication::translate("FileOps", "Copy made: %1").arg(target.fileName()));
        }
    });
    watchJob(job, QCoreApplication::translate("FileOps", "copy this"));
}

void FileOps::applyProperties(const QUrl &url, const QString &fileName, bool isDir, const QString &typed, bool hidden)
{
    // The name as typed keeps the extension hidden from view, and Hidden
    // is a leading dot, as on every Linux system.
    const QString visible = Format::visibleName(fileName, isDir, Settings::instance()->showExtensions());
    QString name = typed.trimmed().isEmpty() ? visible : typed.trimmed();
    if (name.contains(QLatin1Char('/'))) {
        Q_EMIT failed(QCoreApplication::translate("FileOps", "A name cannot contain /. Try a different name."));
        return;
    }
    name += fileName.mid(visible.size());
    while (name.startsWith(QLatin1Char('.'))) {
        name.remove(0, 1);
    }
    if (name.isEmpty()) {
        Q_EMIT failed(QCoreApplication::translate("FileOps", "A name cannot be just dots. Try a different name."));
        return;
    }
    if (hidden) {
        name.prepend(QLatin1Char('.'));
    }
    if (name == fileName) {
        return;
    }
    const QUrl folder = KIO::upUrl(url);
    QUrl target = folder;
    target.setPath(folderPath(folder, name));
    if (target.isLocalFile() && QFileInfo::exists(target.toLocalFile())) {
        Q_EMIT failed(QCoreApplication::translate("FileOps", "There is already an item called \"%1\" here. Pick a different name.").arg(name));
        return;
    }
    auto job = KIO::moveAs(url, target, KIO::HideProgressInfo);
    job->setUiDelegate(delegate());
    connect(job, &KJob::result, this, [this, job, folder, target] {
        if (!job->error()) {
            Q_EMIT created(folder, {target}, false);
        }
    });
    watchJob(job, QCoreApplication::translate("FileOps", "rename this"));
}
