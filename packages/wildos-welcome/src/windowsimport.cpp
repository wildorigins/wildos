#include "windowsimport.h"
#include "bookmarks.h"
#include "setup.h"

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QtConcurrent>

#include <cmath>

namespace
{
// Partitions Windows makes for itself that never hold anyone's files.
const QStringList skippedPartTypes = {
    QStringLiteral("de94bba4-06d1-4d40-a16a-bfd50179d6ac"), // recovery
    QStringLiteral("e3c9e316-0b5c-4db8-817d-f92df00215ae"), // reserved
    QStringLiteral("0x27"),
};

// Folders under Users that belong to Windows rather than to a person.
const QStringList systemProfiles = {
    QStringLiteral("all users"), QStringLiteral("default"), QStringLiteral("default user"),
    QStringLiteral("public"), QStringLiteral("defaultapppool"), QStringLiteral("wdagutilityaccount"),
};

struct FolderSpec {
    const char *key;
    const char *name;
    QStandardPaths::StandardLocation location;
};

const FolderSpec folderSpecs[] = {
    {"documents", QT_TRANSLATE_NOOP("WindowsImport", "Documents"), QStandardPaths::DocumentsLocation},
    {"desktop", QT_TRANSLATE_NOOP("WindowsImport", "Desktop"), QStandardPaths::DesktopLocation},
    {"pictures", QT_TRANSLATE_NOOP("WindowsImport", "Pictures"), QStandardPaths::PicturesLocation},
    {"music", QT_TRANSLATE_NOOP("WindowsImport", "Music"), QStandardPaths::MusicLocation},
    {"videos", QT_TRANSLATE_NOOP("WindowsImport", "Videos"), QStandardPaths::MoviesLocation},
};

// About what a USB 3 or SATA drive manages with lots of small files mixed
// in; good enough for "about 15 minutes".
constexpr double bytesPerSecond = 80e6;

// NTFS keeps the case it was given but Windows never cares; neither do we.
QString childIgnoringCase(const QString &dir, const QString &name)
{
    const QString exact = dir + QLatin1Char('/') + name;
    if (QFileInfo::exists(exact)) {
        return exact;
    }
    const auto entries = QDir(dir).entryList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot | QDir::Hidden | QDir::System);
    for (const QString &entry : entries) {
        if (entry.compare(name, Qt::CaseInsensitive) == 0) {
            return dir + QLatin1Char('/') + entry;
        }
    }
    return {};
}

QString pathIgnoringCase(const QString &root, const QStringList &parts)
{
    QString path = root;
    for (const QString &part : parts) {
        path = childIgnoringCase(path, part);
        if (path.isEmpty()) {
            return {};
        }
    }
    return path;
}

qint64 folderSize(const QStringList &paths)
{
    // The files Windows leaves in every folder, which the copy skips too, so
    // a folder holding only those counts as empty.
    static const QStringList clutter = {QStringLiteral("desktop.ini"), QStringLiteral("thumbs.db"), QStringLiteral("ehthumbs.db")};
    qint64 total = 0;
    for (const QString &path : paths) {
        // Junctions such as "My Music" inside Documents show up as links;
        // following them would count folders twice or loop for ever.
        QDirIterator it(path, QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            if (!clutter.contains(it.fileName().toLower())) {
                total += it.fileInfo().size();
            }
        }
    }
    return total;
}

QDBusMessage filesystemCall(const QString &kname, const QString &method)
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.UDisks2"),
                                                  QStringLiteral("/org/freedesktop/UDisks2/block_devices/") + kname,
                                                  QStringLiteral("org.freedesktop.UDisks2.Filesystem"), method);
    QVariantMap options;
    if (method == QLatin1String("Mount")) {
        options.insert(QStringLiteral("options"), QStringLiteral("ro"));
    }
    message << options;
    return message;
}
}

WindowsImport::WindowsImport(QObject *parent)
    : QObject(parent)
{
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &WindowsImport::release);
    scan();
}

WindowsImport::~WindowsImport()
{
    release();
}

void WindowsImport::setState(const QString &state, const QString &message)
{
    m_state = state;
    m_message = message;
    Q_EMIT stateChanged();
}

QVariantList WindowsImport::sources() const
{
    QVariantList list;
    for (const Source &source : m_sources) {
        const Drive &drive = m_drives.at(source.drive);
        list.append(QVariantMap{
            {QStringLiteral("label"), drive.label},
            {QStringLiteral("user"), source.user},
            {QStringLiteral("detail"), QCoreApplication::translate("WindowsImport", "Found on this computer, user folder \"%1\"").arg(source.user)},
        });
    }
    return list;
}

void WindowsImport::scan()
{
    if (m_started) {
        return;
    }
    release();
    m_drives.clear();
    m_sources.clear();
    m_toMount.clear();
    m_current = -1;
    m_folders.clear();
    m_sawBitLocker = false;
    m_mountError.clear();
    ++m_generation;
    Q_EMIT sourcesChanged();
    Q_EMIT foldersChanged();
    setState(QStringLiteral("scanning"));

    // Lets the flow be tried on a computer without Windows, against a copy
    // of a Windows drive's folders.
    const QString testRoot = qEnvironmentVariable("WILDOS_WELCOME_WINDOWS_ROOT");
    if (!testRoot.isEmpty()) {
        Drive drive;
        drive.mountPoint = testRoot;
        m_drives.append(drive);
        inspect(0);
        finishScan();
        return;
    }
    readBlockDevices();
}

void WindowsImport::readBlockDevices()
{
    auto process = new QProcess(this);
    const int generation = m_generation;
    connect(process, &QProcess::finished, this, [this, process, generation] {
        process->deleteLater();
        if (generation != m_generation) {
            return;
        }
        const QJsonArray devices = QJsonDocument::fromJson(process->readAllStandardOutput()).object().value(QStringLiteral("blockdevices")).toArray();
        for (const QJsonValue &value : devices) {
            const QJsonObject device = value.toObject();
            const QString fstype = device.value(QStringLiteral("fstype")).toString();
            const QString partType = device.value(QStringLiteral("parttype")).toString().toLower();
            if (fstype.compare(QLatin1String("BitLocker"), Qt::CaseInsensitive) == 0) {
                m_sawBitLocker = true;
                continue;
            }
            if (fstype != QLatin1String("ntfs") || skippedPartTypes.contains(partType)) {
                continue;
            }
            Drive drive;
            drive.device = device.value(QStringLiteral("path")).toString();
            drive.kname = device.value(QStringLiteral("kname")).toString();
            drive.label = device.value(QStringLiteral("label")).toString();
            drive.size = device.value(QStringLiteral("size")).toVariant().toLongLong();
            drive.mountPoint = device.value(QStringLiteral("mountpoint")).toString();
            // A few hundred megabytes is a Windows tools partition, not C:.
            if (drive.size > 0 && drive.size < 2LL * 1000 * 1000 * 1000) {
                continue;
            }
            m_drives.append(drive);
            if (drive.mountPoint.isEmpty()) {
                m_toMount.append(m_drives.size() - 1);
            } else {
                inspect(m_drives.size() - 1);
            }
        }
        mountNext();
    });
    process->start(QStringLiteral("lsblk"), {QStringLiteral("--json"), QStringLiteral("--bytes"), QStringLiteral("--list"),
                                             QStringLiteral("--output"), QStringLiteral("PATH,KNAME,FSTYPE,LABEL,SIZE,MOUNTPOINT,PARTTYPE")});
}

void WindowsImport::mountNext()
{
    if (m_toMount.isEmpty()) {
        finishScan();
        return;
    }
    const int index = m_toMount.takeFirst();
    const int generation = m_generation;
    // Mounting an internal drive can ask for the administrator password, so
    // the call is given as long as a person needs to type it.
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(filesystemCall(m_drives.at(index).kname, QStringLiteral("Mount")), 300000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, index, generation] {
        watcher->deleteLater();
        if (generation != m_generation) {
            return;
        }
        QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            const QString text = reply.error().message();
            if (text.contains(QLatin1String("hibernat"), Qt::CaseInsensitive) || text.contains(QLatin1String("unclean"), Qt::CaseInsensitive)
                || text.contains(QLatin1String("dirty"), Qt::CaseInsensitive)) {
                m_mountError = QCoreApplication::translate("WindowsImport", "Windows was not shut down fully, so its drive can't be read. Start Windows, hold Shift while you choose Shut down, then come back to this step.");
            } else if (reply.error().name().contains(QLatin1String("NotAuthorized"))) {
                m_mountError = QCoreApplication::translate("WindowsImport", "Reading the Windows drive needs the administrator password, and it was not given.");
            }
        } else {
            m_drives[index].mountPoint = reply.value();
            m_drives[index].weMounted = true;
            inspect(index);
        }
        mountNext();
    });
}

void WindowsImport::inspect(int driveIndex)
{
    Drive &drive = m_drives[driveIndex];
    drive.hasWindows = !pathIgnoringCase(drive.mountPoint, {QStringLiteral("Windows"), QStringLiteral("System32")}).isEmpty();
    if (drive.hasWindows) {
        // Linux has no drive letters; the drive Windows runs from is C: on
        // every normal install.
        drive.label = QCoreApplication::translate("WindowsImport", "Windows (C:)");
    } else if (drive.label.isEmpty()) {
        drive.label = QCoreApplication::translate("WindowsImport", "Local disk");
    }

    const QString users = childIgnoringCase(drive.mountPoint, QStringLiteral("Users"));
    if (users.isEmpty()) {
        return;
    }
    QList<std::pair<QDateTime, Source>> found;
    const auto entries = QDir(users).entryInfoList(QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks);
    for (const QFileInfo &entry : entries) {
        if (systemProfiles.contains(entry.fileName().toLower())) {
            continue;
        }
        const QString profile = entry.absoluteFilePath();
        const QString ntuser = childIgnoringCase(profile, QStringLiteral("NTUSER.DAT"));
        if (ntuser.isEmpty() && childIgnoringCase(profile, QStringLiteral("Documents")).isEmpty()) {
            continue;
        }
        Source source;
        source.drive = driveIndex;
        source.user = entry.fileName();
        source.home = profile;
        // The person who used the computer most recently is most likely the
        // one sitting here now.
        found.append({QFileInfo(ntuser.isEmpty() ? profile : ntuser).lastModified(), source});
    }
    std::sort(found.begin(), found.end(), [](const auto &a, const auto &b) {
        return a.first > b.first;
    });
    for (const auto &pair : found) {
        m_sources.append(pair.second);
    }
}

void WindowsImport::finishScan()
{
    // Windows drives first, so C: is what the step opens on.
    std::stable_sort(m_sources.begin(), m_sources.end(), [this](const Source &a, const Source &b) {
        return m_drives.at(a.drive).hasWindows && !m_drives.at(b.drive).hasWindows;
    });
    Q_EMIT sourcesChanged();
    if (!m_sources.isEmpty()) {
        setState(QStringLiteral("found"));
        choose(0);
        return;
    }
    if (!m_mountError.isEmpty()) {
        setState(QStringLiteral("error"), m_mountError);
    } else if (m_sawBitLocker) {
        setState(QStringLiteral("locked"), QCoreApplication::translate("WindowsImport", "Windows on this computer is encrypted with BitLocker, so Wild OS can't read your files from it. Copy them to a USB drive from Windows instead."));
    } else {
        setState(QStringLiteral("none"), QCoreApplication::translate("WindowsImport", "There's no Windows on this computer, so there's nothing to copy. If your files are on a USB drive, open Files after setup."));
    }
}

void WindowsImport::choose(int index)
{
    if (index < 0 || index >= m_sources.size() || m_started) {
        return;
    }
    m_current = index;
    Q_EMIT currentChanged();
    buildFolders();
}

void WindowsImport::buildFolders()
{
    const Source &source = m_sources.at(m_current);
    const int generation = ++m_generation;
    m_folders.clear();
    m_pendingSizes = 0;

    // Every other account on the same drive comes along too, each into a
    // folder of its own ("Documents/From Sam"), so two people's files never
    // mix and nobody's are left behind. The chosen account fills the
    // folders themselves.
    QList<Source> accounts{source};
    for (int i = 0; i < m_sources.size(); ++i) {
        if (i != m_current && m_sources.at(i).drive == source.drive) {
            accounts.append(m_sources.at(i));
        }
    }

    for (const FolderSpec &spec : folderSpecs) {
        const QString name = QString::fromLatin1(spec.name);
        const QString home = QStandardPaths::writableLocation(spec.location);
        QStringList from;
        QVariantList parts;
        for (int a = 0; a < accounts.size(); ++a) {
            QStringList partFrom;
            const QString local = childIgnoringCase(accounts.at(a).home, name);
            if (!local.isEmpty()) {
                partFrom << local;
            }
            // OneDrive's folder backup moves Desktop, Documents and Pictures
            // into the OneDrive folder; whatever was synced down is there.
            const QString onedrive = pathIgnoringCase(accounts.at(a).home, {QStringLiteral("OneDrive"), name});
            if (!onedrive.isEmpty()) {
                partFrom << onedrive;
            }
            from << partFrom;
            parts.append(QVariantMap{
                {QStringLiteral("user"), accounts.at(a).user},
                {QStringLiteral("from"), partFrom},
                {QStringLiteral("to"), a == 0 ? home : home + QStringLiteral("/From ") + accounts.at(a).user},
                {QStringLiteral("size"), qint64(0)},
            });
        }
        m_folders.append(QVariantMap{
            {QStringLiteral("key"), QString::fromLatin1(spec.key)},
            {QStringLiteral("name"), QCoreApplication::translate("WindowsImport", spec.name)},
            {QStringLiteral("from"), from},
            {QStringLiteral("parts"), parts},
            {QStringLiteral("to"), home},
            {QStringLiteral("size"), qint64(0)},
            {QStringLiteral("sized"), from.isEmpty()},
            {QStringLiteral("enabled"), !from.isEmpty()},
            {QStringLiteral("checked"), !from.isEmpty()},
            {QStringLiteral("detail"), from.isEmpty() ? QCoreApplication::translate("WindowsImport", "Not found") : QString()},
        });
        if (from.isEmpty()) {
            continue;
        }
        ++m_pendingSizes;
        const int row = m_folders.size() - 1;
        auto watcher = new QFutureWatcher<QList<qint64>>(this);
        connect(watcher, &QFutureWatcher<QList<qint64>>::finished, this, [this, watcher, row, generation] {
            watcher->deleteLater();
            if (generation != m_generation) {
                return;
            }
            QVariantMap folder = m_folders.at(row).toMap();
            const QList<qint64> sizes = watcher->result();
            QVariantList parts = folder.value(QStringLiteral("parts")).toList();
            qint64 size = 0;
            QStringList others;
            for (int i = 0; i < parts.size() && i < sizes.size(); ++i) {
                QVariantMap part = parts.at(i).toMap();
                part.insert(QStringLiteral("size"), sizes.at(i));
                parts[i] = part;
                size += sizes.at(i);
                if (i > 0 && sizes.at(i) > 0) {
                    others << part.value(QStringLiteral("user")).toString();
                }
            }
            folder.insert(QStringLiteral("parts"), parts);
            folder.insert(QStringLiteral("size"), size);
            folder.insert(QStringLiteral("sized"), true);
            // Shown after the size: "2.1 GB, also from Sam".
            folder.insert(QStringLiteral("also"), others.join(QStringLiteral(" and ")));
            if (size == 0) {
                folder.insert(QStringLiteral("enabled"), false);
                folder.insert(QStringLiteral("checked"), false);
                folder.insert(QStringLiteral("detail"), QStringLiteral("Empty"));
            }
            m_folders[row] = folder;
            --m_pendingSizes;
            if (m_pendingSizes == 0) {
                // Leave room on this drive: if everything will not fit, the
                // biggest folders start unticked and the person decides.
                const qint64 room = qint64(double(freeBytes()) * 0.95);
                while (selectedBytes() > room) {
                    int biggest = -1;
                    qint64 biggestSize = -1;
                    for (int i = 0; i < m_folders.size(); ++i) {
                        const QVariantMap f = m_folders.at(i).toMap();
                        if (f.value(QStringLiteral("checked")).toBool() && f.value(QStringLiteral("key")) != QLatin1String("bookmarks")
                            && f.value(QStringLiteral("size")).toLongLong() > biggestSize) {
                            biggest = i;
                            biggestSize = f.value(QStringLiteral("size")).toLongLong();
                        }
                    }
                    if (biggest < 0) {
                        break;
                    }
                    QVariantMap f = m_folders.at(biggest).toMap();
                    f.insert(QStringLiteral("checked"), false);
                    m_folders[biggest] = f;
                }
            }
            Q_EMIT foldersChanged();
        });
        QList<QStringList> partPaths;
        for (const QVariant &part : std::as_const(parts)) {
            partPaths << part.toMap().value(QStringLiteral("from")).toStringList();
        }
        watcher->setFuture(QtConcurrent::run([partPaths] {
            QList<qint64> sizes;
            for (const QStringList &paths : partPaths) {
                sizes << folderSize(paths);
            }
            return sizes;
        }));
    }

    const QList<BrowserProfile> browsers = Bookmarks::findWindowsProfiles(source.home);
    QStringList browserNames;
    QVariantList files;
    for (const BrowserProfile &profile : browsers) {
        if (!browserNames.contains(profile.browser)) {
            browserNames << profile.browser;
        }
        files.append(QVariantMap{{QStringLiteral("browser"), profile.browser}, {QStringLiteral("path"), profile.path}});
    }
    const bool brave = Bookmarks::braveInstalled();
    QString detail;
    if (!brave) {
        detail = QCoreApplication::translate("WindowsImport", "Needs Brave, which isn't installed");
    } else if (browserNames.isEmpty()) {
        detail = QCoreApplication::translate("WindowsImport", "None found in Edge or Chrome");
    } else {
        detail = QCoreApplication::translate("WindowsImport", "From %1").arg(browserNames.join(QCoreApplication::translate("WindowsImport", " and ")));
    }
    const bool usable = brave && !browserNames.isEmpty();
    m_folders.append(QVariantMap{
        {QStringLiteral("key"), QStringLiteral("bookmarks")},
        {QStringLiteral("name"), QCoreApplication::translate("WindowsImport", "Browser bookmarks and history")},
        {QStringLiteral("files"), files},
        {QStringLiteral("size"), qint64(0)},
        {QStringLiteral("sized"), true},
        {QStringLiteral("enabled"), usable},
        {QStringLiteral("checked"), usable},
        {QStringLiteral("detail"), detail},
    });
    Q_EMIT foldersChanged();
}

qint64 WindowsImport::selectedBytes() const
{
    qint64 total = 0;
    for (const QVariant &value : m_folders) {
        const QVariantMap folder = value.toMap();
        if (folder.value(QStringLiteral("checked")).toBool()) {
            total += folder.value(QStringLiteral("size")).toLongLong();
        }
    }
    return total;
}

qint64 WindowsImport::freeBytes() const
{
    // The setup account's home is in memory; the files go to /home.
    return QStorageInfo(planning() ? QStringLiteral("/home") : QDir::homePath()).bytesAvailable();
}

bool WindowsImport::planning() const
{
    return Setup::planning();
}

QString WindowsImport::windowsRoot() const
{
    if (m_current < 0) {
        return {};
    }
    const Drive &drive = m_drives.at(m_sources.at(m_current).drive);
    return drive.hasWindows ? drive.mountPoint : QString();
}

QString WindowsImport::userHome() const
{
    return m_current < 0 ? QString() : m_sources.at(m_current).home;
}

void WindowsImport::setChecked(const QString &key, bool checked)
{
    for (int i = 0; i < m_folders.size(); ++i) {
        QVariantMap folder = m_folders.at(i).toMap();
        if (folder.value(QStringLiteral("key")) == key && folder.value(QStringLiteral("enabled")).toBool()) {
            folder.insert(QStringLiteral("checked"), checked);
            m_folders[i] = folder;
            Q_EMIT foldersChanged();
            return;
        }
    }
}

QJsonObject WindowsImport::job() const
{
    const Source &source = m_sources.at(m_current);
    const Drive &drive = m_drives.at(source.drive);

    QJsonArray folders;
    QJsonArray bookmarks;
    for (const QVariant &value : std::as_const(m_folders)) {
        const QVariantMap folder = value.toMap();
        if (!folder.value(QStringLiteral("checked")).toBool()) {
            continue;
        }
        if (folder.value(QStringLiteral("key")) == QLatin1String("bookmarks")) {
            bookmarks = QJsonArray::fromVariantList(folder.value(QStringLiteral("files")).toList());
            continue;
        }
        // One entry per account with something to copy, so an account
        // whose folder is empty doesn't leave an empty "From" folder.
        for (const QVariant &value : folder.value(QStringLiteral("parts")).toList()) {
            const QVariantMap part = value.toMap();
            if (part.value(QStringLiteral("size")).toLongLong() <= 0) {
                continue;
            }
            folders.append(QJsonObject{
                {QStringLiteral("name"), folder.value(QStringLiteral("name")).toString()},
                {QStringLiteral("from"), QJsonArray::fromStringList(part.value(QStringLiteral("from")).toStringList())},
                {QStringLiteral("to"), part.value(QStringLiteral("to")).toString()},
                {QStringLiteral("bytes"), part.value(QStringLiteral("size")).toLongLong()},
            });
        }
    }
    if (folders.isEmpty() && bookmarks.isEmpty()) {
        return {};
    }

    QJsonObject job{
        {QStringLiteral("drive"), drive.label},
        {QStringLiteral("folders"), folders},
        {QStringLiteral("bookmarks"), bookmarks},
    };
    // The copier unmounts the drive when it is done, but only a drive this
    // step mounted; one that was already open stays as it was.
    if (drive.weMounted) {
        job.insert(QStringLiteral("unmount"), drive.kname);
    }
    return job;
}

bool WindowsImport::start()
{
    if (m_started || m_current < 0) {
        return false;
    }
    QJsonObject job = this->job();
    if (job.isEmpty()) {
        return false;
    }
    const Source &source = m_sources.at(m_current);

    if (planning()) {
        // The new account mounts the drive again itself after signing in
        // (Background::applyCopy), so the plan names the drive and keeps
        // paths that can be moved onto wherever it is mounted then.
        const Drive &drive = m_drives.at(source.drive);
        job.insert(QStringLiteral("root"), drive.mountPoint);
        job.insert(QStringLiteral("home"), QDir::homePath());
        job.insert(QStringLiteral("kname"), drive.kname);
        m_plan = job.toVariantMap();
        m_started = true;
        Q_EMIT startedChanged();
        return true;
    }

    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wildos-welcome");
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/copy-%1.json").arg(QDateTime::currentMSecsSinceEpoch());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    file.write(QJsonDocument(job).toJson());
    file.close();

    // A process of its own, so the copy carries on after this window closes
    // and reports through the desktop's notifications.
    if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--copy"), path})) {
        return false;
    }
    m_started = true;
    // The copier owns the mount from here.
    m_drives[source.drive].weMounted = false;
    Q_EMIT startedChanged();
    return true;
}

void WindowsImport::unmount(const Drive &drive)
{
    if (!drive.weMounted || drive.kname.isEmpty()) {
        return;
    }
    QDBusConnection::systemBus().call(filesystemCall(drive.kname, QStringLiteral("Unmount")), QDBus::Block, 10000);
}

void WindowsImport::release()
{
    for (Drive &drive : m_drives) {
        if (drive.weMounted) {
            unmount(drive);
            // Nothing is there any more; step 7 opens it again if it needs to.
            drive.mountPoint.clear();
        }
        drive.weMounted = false;
    }
    Q_EMIT currentChanged();
}

QString WindowsImport::formatBytes(qint64 bytes, bool whole) const
{
    // Decimal units, the way drives are sold and Windows shows them.
    const QLocale locale = QLocale::system();
    const double value = double(bytes);
    if (value >= 1e12) {
        return QCoreApplication::translate("WindowsImport", "%1 TB").arg(locale.toString(value / 1e12, 'f', 1));
    }
    if (value >= 1e9) {
        return QCoreApplication::translate("WindowsImport", "%1 GB").arg(locale.toString(value / 1e9, 'f', whole && value >= 1e10 ? 0 : 1));
    }
    if (value >= 1e6) {
        return QCoreApplication::translate("WindowsImport", "%1 MB").arg(locale.toString(value / 1e6, 'f', 0));
    }
    if (value >= 1e3) {
        return QCoreApplication::translate("WindowsImport", "%1 KB").arg(locale.toString(value / 1e3, 'f', 0));
    }
    return bytes == 1 ? QCoreApplication::translate("WindowsImport", "1 byte") : QCoreApplication::translate("WindowsImport", "%1 bytes").arg(bytes);
}

QString WindowsImport::estimate() const
{
    const qint64 bytes = selectedBytes();
    const double minutes = double(bytes) / bytesPerSecond / 60.0;
    QString time;
    if (minutes < 1.5) {
        time = QCoreApplication::translate("WindowsImport", "about a minute");
    } else if (minutes < 10) {
        time = QCoreApplication::translate("WindowsImport", "about %1 minutes").arg(qRound(minutes));
    } else if (minutes < 90) {
        time = QCoreApplication::translate("WindowsImport", "about %1 minutes").arg(qRound(minutes / 5) * 5);
    } else {
        const double hours = minutes / 60;
        time = hours < 1.75 ? QCoreApplication::translate("WindowsImport", "about an hour") : QCoreApplication::translate("WindowsImport", "about %1 hours").arg(qRound(hours));
    }
    if (bytes == 0) {
        return QCoreApplication::translate("WindowsImport", "Your bookmarks and history will be ready in Brave in a moment.");
    }
    return QCoreApplication::translate("WindowsImport", "Copying %1, %2. You can start using Wild OS while it runs.").arg(formatBytes(bytes, true), time);
}
