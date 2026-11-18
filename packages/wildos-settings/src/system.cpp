#include "system.h"

#include <KConfigGroup>
#include <KPluginMetaData>
#include <KConfigWatcher>
#include <KSharedConfig>
#include <KUser>

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLocale>
#include <QProcess>
#include <QRegularExpression>
#include <QSet>
#include <QStandardPaths>
#include <QStorageInfo>
#include <QSysInfo>
#include <QTimeZone>
#include <QTimer>
#include <QUrl>

namespace
{
const QString statusTool = QStringLiteral("/usr/libexec/wildos/security-status");
const QString defaultWallpaper = QStringLiteral("/usr/share/wallpapers/WildOS");

QString readFile(const QString &path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    return QString::fromUtf8(file.readAll()).trimmed();
}

// Firmware fills unset DMI fields with placeholders that mean nothing to
// anyone reading the About page.
bool meaningfulDmi(const QString &value)
{
    static const QStringList placeholders = {
        QStringLiteral("to be filled by o.e.m."), QStringLiteral("system product name"),
        QStringLiteral("system manufacturer"), QStringLiteral("default string"),
        QStringLiteral("not specified"), QStringLiteral("none"), QStringLiteral("o.e.m."),
    };
    return !value.isEmpty() && !placeholders.contains(value.toLower());
}

QString localPath(const QString &urlOrPath)
{
    QString path = urlOrPath.startsWith(QLatin1String("file:")) ? QUrl(urlOrPath).toLocalFile() : urlOrPath;
    while (path.length() > 1 && path.endsWith(QLatin1Char('/'))) {
        path.chop(1);
    }
    return path;
}

// A wallpaper package holds the same photo at many sizes; the largest is the
// one to hand to anything that wants a single file.
QString largestImage(const QString &package)
{
    QDir images(package + QStringLiteral("/contents/images"));
    QString best;
    qint64 bestPixels = 0;
    const auto entries = images.entryList(QDir::Files);
    for (const QString &name : entries) {
        const auto match = QRegularExpression(QStringLiteral("^(\\d+)x(\\d+)")).match(name);
        const qint64 pixels = match.hasMatch() ? match.captured(1).toLongLong() * match.captured(2).toLongLong() : 1;
        if (pixels > bestPixels) {
            bestPixels = pixels;
            best = images.filePath(name);
        }
    }
    return best;
}

// The wallpaper plugin to draw `path` with, when it is not Plasma's plain
// image one. A theme can name a plugin for its own wallpaper with
// "wallpaperPlugin", as Overclock does for Warp's live W; the same picture
// chosen under another theme stays a still image.
QString wallpaperPluginFor(const QString &path)
{
    KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    wildos->reparseConfiguration();
    const QString id = wildos->group(QStringLiteral("Theme")).readEntry("Id");
    if (id.isEmpty()) {
        return {};
    }
    QFile file(QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("wildos/themes/%1.json").arg(id)));
    if (!file.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QJsonObject theme = QJsonDocument::fromJson(file.readAll()).object();
    const QString plugin = theme.value(QStringLiteral("wallpaperPlugin")).toString();
    if (plugin.isEmpty() || localPath(theme.value(QStringLiteral("wallpaper")).toString()) != path) {
        return {};
    }
    // A missing plugin would leave the desktop black; the plain image is
    // the safe way to show the picture.
    if (QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("plasma/wallpapers/%1/metadata.json").arg(plugin)).isEmpty()) {
        return {};
    }
    return plugin;
}

// For a single-quoted string in a Plasma script.
QString scriptString(QString text)
{
    return text.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('\''), QStringLiteral("\\'"));
}

QString thumbnailFor(const QString &path)
{
    if (QFileInfo(path).isFile()) {
        return QUrl::fromLocalFile(path).toString();
    }
    for (const QString &name : {QStringLiteral("screenshot.jpg"), QStringLiteral("screenshot.png")}) {
        const QString shot = path + QStringLiteral("/contents/") + name;
        if (QFile::exists(shot)) {
            return QUrl::fromLocalFile(shot).toString();
        }
    }
    QDir images(path + QStringLiteral("/contents/images"));
    QString smallest;
    qint64 smallestPixels = std::numeric_limits<qint64>::max();
    const auto entries = images.entryList(QDir::Files);
    for (const QString &name : entries) {
        const auto match = QRegularExpression(QStringLiteral("^(\\d+)x(\\d+)")).match(name);
        const qint64 pixels = match.hasMatch() ? match.captured(1).toLongLong() * match.captured(2).toLongLong() : 0;
        // Anything narrower than a thumbnail would look soft when scaled up.
        if (pixels >= 1280 * 720 && pixels < smallestPixels) {
            smallestPixels = pixels;
            smallest = images.filePath(name);
        }
    }
    if (smallest.isEmpty()) {
        smallest = largestImage(path);
    }
    return smallest.isEmpty() ? QString() : QUrl::fromLocalFile(smallest).toString();
}

QString packageName(const QString &path)
{
    QFile metadata(path + QStringLiteral("/metadata.json"));
    if (metadata.open(QIODevice::ReadOnly)) {
        // KPlugin.Name, not the first "Name" in the file, which is the
        // photographer's in the Authors list.
        const QString name = QJsonDocument::fromJson(metadata.readAll()).object().value(QStringLiteral("KPlugin")).toObject().value(QStringLiteral("Name")).toString();
        if (!name.isEmpty()) {
            return name;
        }
    }
    return QFileInfo(path).completeBaseName();
}
}

System::System(QObject *parent)
    : QObject(parent)
{
    readOsRelease();
    readHardware();
    readAccent();
    readColourScheme();
    readWallpaper();
    readLockWallpaper();
    readTaskbar();
    readTaskbarLayout();
    readTextScale();
    refreshUpdatePolicy();
    refreshAppUpdates();
    refreshSecurity();
    readProtectionLevel();
    {
        KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
        m_taskbarFloating = wildos->group(QStringLiteral("Taskbar")).readEntry("Style") == QLatin1String("floating");
    }

    // The users page and other tools change the picture and name through
    // AccountsService, which announces each change on the user's object.
    QDBusConnection::systemBus().connect(QStringLiteral("org.freedesktop.Accounts"),
                                         QStringLiteral("/org/freedesktop/Accounts/User%1").arg(KUserId::currentUserId().nativeId()),
                                         QStringLiteral("org.freedesktop.Accounts.User"),
                                         QStringLiteral("Changed"),
                                         this,
                                         SIGNAL(userChanged()));

    // The accent can also change from the panel or another Settings window;
    // follow it so this window never shows a stale colour.
    m_globals = KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("kdeglobals")));
    if (m_globals) {
        connect(m_globals.data(), &KConfigWatcher::configChanged, this, [this] {
            readAccent();
            readColourScheme();
            readTextScale();
        });
    }
}

void System::readOsRelease()
{
    // wildos-base installs its own copy here, whatever /etc/os-release and
    // the base-files diversion say.
    QString text = readFile(QStringLiteral("/usr/lib/wildos/os-release"));
    if (text.isEmpty()) {
        text = readFile(QStringLiteral("/etc/os-release"));
    }
    if (text.isEmpty()) {
        text = readFile(QStringLiteral("/usr/lib/os-release"));
    }
    QHash<QString, QString> values;
    const auto lines = text.split(QLatin1Char('\n'));
    for (const QString &line : lines) {
        const int eq = line.indexOf(QLatin1Char('='));
        if (eq > 0) {
            QString value = line.mid(eq + 1).trimmed();
            if (value.size() >= 2 && (value.startsWith(QLatin1Char('"')) || value.startsWith(QLatin1Char('\'')))) {
                value = value.mid(1, value.size() - 2);
            }
            values.insert(line.left(eq).trimmed(), value);
        }
    }
    // Where the release file is not Wild OS's own (the diversion missing,
    // or Settings running elsewhere), the product is still Wild OS and the
    // other system's name and version are not shown.
    if (values.value(QStringLiteral("ID")) != QLatin1String("wildos")) {
        m_osName = QStringLiteral("Wild OS");
        m_osVersion.clear();
        return;
    }
    m_osName = values.value(QStringLiteral("NAME"), QStringLiteral("Wild OS"));
    m_osVersion = values.value(QStringLiteral("VERSION"), values.value(QStringLiteral("VERSION_ID")));
}

void System::readHardware()
{
    const QString vendor = readFile(QStringLiteral("/sys/class/dmi/id/sys_vendor"));
    const QString product = readFile(QStringLiteral("/sys/class/dmi/id/product_name"));
    QStringList model;
    if (meaningfulDmi(vendor)) {
        model << vendor;
    }
    if (meaningfulDmi(product) && !product.startsWith(vendor)) {
        model << product;
    } else if (meaningfulDmi(product)) {
        model = {product};
    }
    m_deviceModel = model.join(QLatin1Char(' '));

    const QString cpuinfo = readFile(QStringLiteral("/proc/cpuinfo"));
    const auto cpuName = QRegularExpression(QStringLiteral("^model name\\s*:\\s*(.+)$"), QRegularExpression::MultilineOption).match(cpuinfo);
    const int threads = cpuinfo.count(QRegularExpression(QStringLiteral("^processor\\s*:"), QRegularExpression::MultilineOption));
    if (cpuName.hasMatch()) {
        m_processor = cpuName.captured(1).simplified();
    }
    if (threads > 0) {
        const QString count = threads == 1 ? tr("1 thread") : tr("%1 threads").arg(threads);
        m_processor = m_processor.isEmpty() ? count : QStringLiteral("%1, %2").arg(m_processor, count);
    }

    const auto memTotal = QRegularExpression(QStringLiteral("^MemTotal:\\s*(\\d+)"), QRegularExpression::MultilineOption)
                              .match(readFile(QStringLiteral("/proc/meminfo")));
    if (memTotal.hasMatch()) {
        const double gib = memTotal.captured(1).toDouble() / (1024.0 * 1024.0);
        m_memory = tr("%1 GB usable").arg(QLocale::system().toString(gib, 'f', 1));
    }
}

QString System::kernel() const
{
    // "6.12.48+deb13-amd64" names the distribution it was built for; the
    // version number alone is what someone helping you needs.
    static const QRegularExpression number(QStringLiteral("^\\d+(\\.\\d+)+"));
    const QString full = QSysInfo::kernelVersion();
    const auto match = number.match(full);
    return match.hasMatch() ? match.captured(0) : full;
}

QString System::architecture() const
{
    const QString arch = QSysInfo::currentCpuArchitecture();
    if (arch == QLatin1String("x86_64")) {
        return tr("64-bit, x64 processor");
    }
    if (arch == QLatin1String("arm64")) {
        return tr("64-bit, Arm processor");
    }
    return arch;
}

QString System::secureBoot() const
{
    if (!QFileInfo::exists(QStringLiteral("/sys/firmware/efi"))) {
        return QStringLiteral("na");
    }
    QFile var(QStringLiteral("/sys/firmware/efi/efivars/SecureBoot-8be4df61-93ca-11d2-aa0d-00e098032b8c"));
    if (!var.open(QIODevice::ReadOnly)) {
        return QStringLiteral("off");
    }
    // Four bytes of attributes come before the one-byte value.
    const QByteArray data = var.readAll();
    return data.size() >= 5 && data.at(4) == 1 ? QStringLiteral("on") : QStringLiteral("off");
}

QString System::hostName() const
{
    return m_hostName.isEmpty() ? QSysInfo::machineHostName() : m_hostName;
}

void System::renameHost(const QString &name)
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.hostname1"),
                                                  QStringLiteral("/org/freedesktop/hostname1"),
                                                  QStringLiteral("org.freedesktop.hostname1"),
                                                  QStringLiteral("SetStaticHostname"));
    // "true" lets polkit ask for the administrator password.
    message << name << true;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, name] {
        watcher->deleteLater();
        QDBusPendingReply<> reply = *watcher;
        if (reply.isError()) {
            Q_EMIT renameFailed(reply.error().name() == QLatin1String("org.freedesktop.DBus.Error.AccessDenied")
                                        || reply.error().name().contains(QLatin1String("NotAuthorized"))
                                    ? tr("Your PC was not renamed because the password was not given.")
                                    : tr("Your PC could not be renamed. %1").arg(reply.error().message()));
            return;
        }
        m_hostName = name;
        Q_EMIT hostNameChanged();
    });
}

QString System::userName() const
{
    return KUser().loginName();
}

QString System::userFullName() const
{
    const KUser user;
    const QString full = user.property(KUser::FullName).toString().section(QLatin1Char(','), 0, 0).trimmed();
    return full.isEmpty() ? user.loginName() : full;
}

QString System::userAvatar() const
{
    const KUser user;
    QString path = user.faceIconPath();
    if (path.isEmpty()) {
        const QString accounts = QStringLiteral("/var/lib/AccountsService/icons/") + user.loginName();
        if (QFileInfo(accounts).isReadable()) {
            path = accounts;
        }
    }
    if (path.isEmpty()) {
        return {};
    }
    // The picture keeps its path when it is replaced; the timestamp makes
    // the URL change with it so the image cache does not show the old one.
    QUrl url = QUrl::fromLocalFile(path);
    url.setQuery(QStringLiteral("v=") + QString::number(QFileInfo(path).lastModified().toSecsSinceEpoch()));
    return url.toString();
}

bool System::userIsAdmin() const
{
    const QStringList groups = KUser().groupNames();
    return groups.contains(QStringLiteral("sudo")) || groups.contains(QStringLiteral("wheel"));
}

void System::refreshSecurity()
{
    if (m_securityChecking) {
        return;
    }
    if (!QFileInfo(statusTool).isExecutable()) {
        m_security.clear();
        m_securityChecked = QDateTime::currentDateTime();
        Q_EMIT securityChanged();
        return;
    }
    m_securityChecking = true;
    Q_EMIT securityCheckingChanged();

    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        QVariantMap next;
        const auto lines = QString::fromUtf8(process->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        for (const QString &line : lines) {
            const int eq = line.indexOf(QLatin1Char('='));
            if (eq > 0) {
                next.insert(line.left(eq).trimmed(), line.mid(eq + 1).trimmed());
            }
        }
        // Older versions of the status tool used narrower names for the
        // same protections; map them so either version fills the page.
        const QList<std::pair<QString, QString>> aliases = {
            {QStringLiteral("disk"), QStringLiteral("encryption")},
            {QStringLiteral("usb"), QStringLiteral("devices")},
            {QStringLiteral("apparmor"), QStringLiteral("apps")},
        };
        for (const auto &[from, to] : aliases) {
            if (next.contains(from) && !next.contains(to)) {
                next.insert(to, next.value(from));
            }
        }
        m_security = next;
        m_securityChecked = QDateTime::currentDateTime();
        m_securityChecking = false;
        Q_EMIT securityChanged();
        Q_EMIT securityCheckingChanged();
    });
    process->start(statusTool, {});
}

bool System::locationServices() const
{
    return QFileInfo::exists(QStringLiteral("/usr/libexec/geoclue"));
}

QVariantList System::volumes() const
{
    static const QSet<QByteArray> skipTypes = {"tmpfs", "devtmpfs", "squashfs", "efivarfs", "ramfs", "proc", "sysfs", "fuse.portal"};
    QVariantList list;
    QSet<QByteArray> seenDevices;
    const QStorageInfo root = QStorageInfo::root();
    const auto all = QStorageInfo::mountedVolumes();
    for (const QStorageInfo &volume : all) {
        const bool isRoot = volume.rootPath() == QLatin1String("/");
        // A single file mounted over another (containers do this) is not
        // a drive anyone would recognise.
        if (!volume.isValid() || !volume.isReady() || !QFileInfo(volume.rootPath()).isDir()) {
            continue;
        }
        if (!isRoot) {
            if (skipTypes.contains(volume.fileSystemType()) || !volume.device().startsWith("/dev/")
                || volume.rootPath().startsWith(QLatin1String("/boot")) || volume.rootPath().startsWith(QLatin1String("/run/live"))
                || volume.rootPath().startsWith(QLatin1String("/snap")) || seenDevices.contains(volume.device())
                || volume.device() == root.device()) {
                continue;
            }
        }
        seenDevices.insert(volume.device());
        QString name;
        if (isRoot) {
            name = tr("System disk");
        } else if (volume.rootPath() == QLatin1String("/home")) {
            name = tr("Home");
        } else if (!volume.displayName().isEmpty() && volume.displayName() != volume.rootPath()) {
            name = volume.displayName();
        } else {
            name = QFileInfo(volume.rootPath()).fileName();
        }
        const QVariantMap entry = {
            {QStringLiteral("name"), name},
            {QStringLiteral("mount"), volume.rootPath()},
            {QStringLiteral("total"), volume.bytesTotal()},
            {QStringLiteral("free"), volume.bytesAvailable()},
            {QStringLiteral("removable"), volume.rootPath().startsWith(QLatin1String("/media"))},
        };
        if (isRoot) {
            list.prepend(entry);
        } else {
            list.append(entry);
        }
    }
    return list;
}

qint64 System::binSize() const
{
    qint64 total = 0;
    QDirIterator it(QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/Trash/files"),
                    QDir::Files | QDir::Hidden | QDir::System | QDir::NoSymLinks, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        it.next();
        total += it.fileInfo().size();
    }
    return total;
}

void System::emptyBin()
{
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        Q_EMIT binEmptied();
    });
    process->start(QStringLiteral("ktrash6"), {QStringLiteral("--empty")});
}

QVariantList System::wallpapers() const
{
    QVariantList list;
    QSet<QString> seen;
    QStringList roots = QStandardPaths::locateAll(QStandardPaths::GenericDataLocation, QStringLiteral("wallpapers"), QStandardPaths::LocateDirectory);
    if (!roots.contains(QStringLiteral("/usr/share/wallpapers"))) {
        roots << QStringLiteral("/usr/share/wallpapers");
    }
    for (const QString &root : std::as_const(roots)) {
        QDir dir(root);
        const auto entries = dir.entryInfoList(QDir::Dirs | QDir::Files | QDir::NoDotAndDotDot, QDir::Name);
        for (const QFileInfo &info : entries) {
            const QString path = info.absoluteFilePath();
            const bool isPackage = info.isDir() && QFileInfo::exists(path + QStringLiteral("/contents/images"));
            const bool isImage = info.isFile()
                && QStringList{QStringLiteral("jpg"), QStringLiteral("jpeg"), QStringLiteral("png"), QStringLiteral("webp")}.contains(info.suffix().toLower());
            if ((!isPackage && !isImage) || seen.contains(info.fileName())) {
                continue;
            }
            seen.insert(info.fileName());
            const QVariantMap entry = {
                {QStringLiteral("path"), path},
                {QStringLiteral("name"), isPackage ? packageName(path) : info.completeBaseName()},
                {QStringLiteral("thumbnail"), thumbnailFor(path)},
            };
            // The Wild OS photo leads, as it is the default.
            if (path == defaultWallpaper) {
                list.prepend(entry);
            } else {
                list.append(entry);
            }
        }
    }
    return list;
}

void System::readWallpaper()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("plasma-org.kde.plasma.desktop-appletsrc"));
    config->reparseConfiguration();
    KConfigGroup containments(config, QStringLiteral("Containments"));
    QString found;
    bool slideshow = false;
    QString folder;
    const auto ids = containments.groupList();
    for (const QString &id : ids) {
        KConfigGroup c = containments.group(id);
        // Panels are containments too; only desktops carry a wallpaper.
        if (c.readEntry("plugin") == QLatin1String("org.kde.panel")) {
            continue;
        }
        KConfigGroup wallpapers = c.group(QStringLiteral("Wallpaper"));
        if (c.readEntry("wallpaperplugin") == QLatin1String("org.kde.slideshow")) {
            slideshow = true;
            const QStringList paths = wallpapers.group(QStringLiteral("org.kde.slideshow")).group(QStringLiteral("General")).readEntry("SlidePaths", QStringList());
            if (!paths.isEmpty()) {
                folder = localPath(paths.constFirst());
            }
        }
        const QString image = wallpapers.group(QStringLiteral("org.kde.image")).group(QStringLiteral("General")).readEntry("Image");
        if (!image.isEmpty() && found.isEmpty()) {
            found = localPath(image);
        }
    }
    m_wallpaper = found.isEmpty() ? defaultWallpaper : found;
    m_slideshow = slideshow;
    m_slideshowFolder = folder;
    Q_EMIT wallpaperChanged();
}

QString System::wallpaperName() const
{
    if (m_slideshow && !m_slideshowFolder.isEmpty()) {
        return tr("Photos from %1").arg(QFileInfo(m_slideshowFolder).fileName());
    }
    QString path = m_wallpaper;
    const int contents = path.indexOf(QStringLiteral("/contents/images/"));
    if (contents > 0) {
        path = path.left(contents);
    }
    if (QFileInfo(path).isDir()) {
        return packageName(path);
    }
    return QFileInfo(path).completeBaseName();
}

void System::setWallpaper(const QString &path)
{
    const QString local = localPath(path);
    const QString plugin = wallpaperPluginFor(local);
    const QString url = scriptString(QUrl::fromLocalFile(local).toString());
    if (plugin.isEmpty()) {
        // Written here rather than through plasma-apply-wallpaperimage,
        // which takes only a plain path, so a photo picked in the file
        // dialog (a file:// address) or named with an apostrophe never
        // reached the desktop.
        evaluatePlasmaScript(QStringLiteral("desktops().forEach(function (d) {"
                                            "d.wallpaperPlugin = 'org.kde.image';"
                                            "d.currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General'];"
                                            "d.writeConfig('Image', '%1');"
                                            "d.reloadConfig(); });")
                                 .arg(url));
    } else {
        // The image plugin's Image is written too: Settings, Welcome and the
        // plain image plugin's fallback all read the wallpaper from there.
        evaluatePlasmaScript(QStringLiteral("desktops().forEach(function (d) {"
                                            "d.currentConfigGroup = ['Wallpaper', 'org.kde.image', 'General'];"
                                            "d.writeConfig('Image', '%1');"
                                            "d.wallpaperPlugin = '%2';"
                                            "d.currentConfigGroup = ['Wallpaper', '%2', 'General'];"
                                            "d.writeConfig('Image', '%1');"
                                            "d.reloadConfig(); });")
                                 .arg(url, scriptString(plugin)));
    }
    // A picture of its own ends a slideshow, as both scripts above do.
    m_wallpaper = local;
    m_slideshow = false;
    m_slideshowFolder.clear();
    Q_EMIT wallpaperChanged();
}

void System::readLockWallpaper()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kscreenlockerrc"));
    const QString image = config->group(QStringLiteral("Greeter"))
                              .group(QStringLiteral("Wallpaper"))
                              .group(QStringLiteral("org.kde.image"))
                              .group(QStringLiteral("General"))
                              .readEntry("Image");
    QString path = image.isEmpty() ? defaultWallpaper : localPath(image);
    // Show the package rather than one of its sizes, so the picker can mark
    // the right thumbnail.
    const int contents = path.indexOf(QStringLiteral("/contents/images/"));
    if (contents > 0) {
        path = path.left(contents);
    }
    m_lockWallpaper = path;
    Q_EMIT lockWallpaperChanged();
}

void System::setLockWallpaper(const QString &path)
{
    QString file = localPath(path);
    if (QFileInfo(file).isDir()) {
        file = largestImage(file);
    }
    if (file.isEmpty()) {
        return;
    }
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kscreenlockerrc"));
    KConfigGroup greeter = config->group(QStringLiteral("Greeter"));
    greeter.writeEntry("WallpaperPlugin", QStringLiteral("org.kde.image"));
    greeter.group(QStringLiteral("Wallpaper")).group(QStringLiteral("org.kde.image")).group(QStringLiteral("General"))
        .writeEntry("Image", QUrl::fromLocalFile(file).toString(), KConfig::Notify);
    config->sync();
    readLockWallpaper();
}

void System::readColourScheme()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    config->reparseConfiguration();
    const QString scheme = config->group(QStringLiteral("General")).readEntry("ColorScheme", QStringLiteral("WildOS"));
    KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    wildos->reparseConfiguration();
    const bool automatic = wildos->group(QStringLiteral("Theme")).readEntry("Mode") == QLatin1String("automatic");
    if (scheme != m_colourScheme || automatic != m_automaticTheme) {
        m_colourScheme = scheme;
        m_automaticTheme = automatic;
        Q_EMIT colourSchemeChanged();
    }
}

void System::setThemeMode(const QString &mode)
{
    // wildos-auto-theme watches this key and switches straight away when it
    // becomes "automatic".
    KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    wildos->group(QStringLiteral("Theme")).writeEntry("Mode", mode, KConfig::Notify);
    wildos->sync();
    m_automaticTheme = mode == QLatin1String("automatic");
    Q_EMIT colourSchemeChanged();
}

void System::setAutomaticTheme()
{
    if (hasColourScheme(QStringLiteral("WildOSLight"))) {
        setThemeMode(QStringLiteral("automatic"));
    }
}

bool System::hasColourScheme(const QString &name) const
{
    return !name.isEmpty()
        && !QStandardPaths::locate(QStandardPaths::GenericDataLocation, QStringLiteral("color-schemes/%1.colors").arg(name)).isEmpty();
}

void System::setColourScheme(const QString &name)
{
    setColours(name, QColor());
}

void System::setColours(const QString &name, const QColor &newAccent)
{
    if (!hasColourScheme(name)) {
        if (newAccent.isValid()) {
            setAccent(newAccent);
        }
        return;
    }
    // Choosing a theme by hand ends automatic switching.
    setThemeMode(name == QLatin1String("WildOSLight") ? QStringLiteral("light") : QStringLiteral("dark"));
    if (name == m_colourScheme) {
        if (newAccent.isValid() && newAccent != m_accent) {
            setAccent(newAccent);
        }
        return;
    }
    QColor accent = newAccent.isValid() ? newAccent : m_accent;
    // High contrast has no accent of its own; leaving it for a theme brings
    // back the accent from before it (Accessibility keeps it).
    if (!newAccent.isValid() && m_colourScheme == QLatin1String("WildOSHighContrast")) {
        KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
        wildos->reparseConfiguration();
        const QStringList rgb = wildos->group(QStringLiteral("Accessibility")).readEntry("AccentBeforeContrast", QString()).split(QLatin1Char(','));
        if (rgb.size() >= 3) {
            accent = QColor(rgb[0].trimmed().toInt(), rgb[1].trimmed().toInt(), rgb[2].trimmed().toInt());
        }
    }
    // Applying a scheme resets the accent key, so the accent goes back on
    // once the tool is done, the way setAccent writes it.
    const auto finish = [this, accent] {
        readColourScheme();
        setAccent(accent);
    };
    if (hasProgram(QStringLiteral("plasma-apply-colorscheme"))) {
        auto process = new QProcess(this);
        connect(process, &QProcess::finished, this, [process, finish] {
            process->deleteLater();
            finish();
        });
        process->start(QStringLiteral("plasma-apply-colorscheme"), {name});
    } else {
        KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
        config->group(QStringLiteral("General")).writeEntry("ColorScheme", name, KConfig::Notify);
        config->sync();
        finish();
    }
    m_colourScheme = name;
    Q_EMIT colourSchemeChanged();
}

void System::readAccent()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    config->reparseConfiguration();
    const QString value = config->group(QStringLiteral("General")).readEntry("AccentColor");
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

void System::setAccent(const QColor &colour)
{
    // The key is what the colours module writes and what Wild OS widgets
    // read; the tool alone recolours the scheme but leaves the key, and
    // /etc/xdg/kdeglobals would then keep reporting Glacier.
    const auto writeKey = [colour] {
        KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
        config->reparseConfiguration();
        KConfigGroup general = config->group(QStringLiteral("General"));
        const QString rgb = QStringLiteral("%1,%2,%3").arg(colour.red()).arg(colour.green()).arg(colour.blue());
        general.writeEntry("AccentColor", rgb, KConfig::Notify);
        general.writeEntry("LastUsedCustomAccentColor", rgb, KConfig::Notify);
        config->sync();
    };

    if (hasProgram(QStringLiteral("plasma-apply-colorscheme"))) {
        // The tool recolours selections, focus and the title bar edge and
        // tells every running app to reload. The key is written after it,
        // so its own write of kdeglobals cannot undo ours.
        auto process = new QProcess(this);
        connect(process, &QProcess::finished, this, [process, writeKey] {
            process->deleteLater();
            writeKey();
        });
        process->start(QStringLiteral("plasma-apply-colorscheme"), {QStringLiteral("--accent-color"), colour.name(QColor::HexRgb)});
    } else {
        writeKey();
    }
    m_accent = colour;
    Q_EMIT accentChanged();
}

QString System::evaluatePlasmaScript(const QString &script) const
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
                                                  QStringLiteral("org.kde.PlasmaShell"), QStringLiteral("evaluateScript"));
    message << script;
    const QDBusMessage reply = QDBusConnection::sessionBus().call(message, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return reply.arguments().constFirst().toString().trimmed();
}

void System::readTaskbar()
{
    const QString hiding = evaluatePlasmaScript(QStringLiteral("print(panels().length ? panels()[0].hiding : '')"));
    m_taskbarHiding = hiding == QLatin1String("autohide") ? 1 : hiding == QLatin1String("dodgewindows") ? 2 : 0;
    Q_EMIT taskbarHidingChanged();
}

void System::setTaskbarHiding(int mode)
{
    const QString value = mode == 1 ? QStringLiteral("autohide") : mode == 2 ? QStringLiteral("dodgewindows") : QStringLiteral("none");
    evaluatePlasmaScript(QStringLiteral("panels().forEach(function (p) { p.hiding = '%1'; })").arg(value));
    m_taskbarHiding = mode;
    Q_EMIT taskbarHidingChanged();
}

bool System::snapEdges() const
{
    KConfigGroup windows = KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("Windows"));
    return windows.readEntry("ElectricBorderTiling", true) || windows.readEntry("ElectricBorderMaximize", true);
}

void System::setSnapEdges(bool on)
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup windows = config->group(QStringLiteral("Windows"));
    windows.writeEntry("ElectricBorderTiling", on);
    windows.writeEntry("ElectricBorderMaximize", on);
    config->sync();
    reconfigureKWin();
}

// Snap layouts and snap assist are one KWin effect, org.wildos.snap, which
// reads its two switches from its own kwinrc group. A switch only counts as
// on while the effect is enabled, so turning both off unloads it, and
// systems that turned snap layouts off before assist existed read as both
// off.
bool System::snapLayouts() const
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    return config->group(QStringLiteral("Plugins")).readEntry("org.wildos.snapEnabled", true)
        && config->group(QStringLiteral("Effect-org.wildos.snap")).readEntry("ShowLayouts", true);
}

void System::setSnapLayouts(bool on)
{
    applySnapEffect(on, snapAssist());
}

bool System::snapAssist() const
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    return config->group(QStringLiteral("Plugins")).readEntry("org.wildos.snapEnabled", true)
        && config->group(QStringLiteral("Effect-org.wildos.snap")).readEntry("SnapAssist", true);
}

void System::setSnapAssist(bool on)
{
    applySnapEffect(snapLayouts(), on);
}

void System::applySnapEffect(bool layouts, bool assist)
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup plugins = config->group(QStringLiteral("Plugins"));
    const bool wasLoaded = plugins.readEntry("org.wildos.snapEnabled", true);
    const bool load = layouts || assist;
    KConfigGroup effect = config->group(QStringLiteral("Effect-org.wildos.snap"));
    effect.writeEntry("ShowLayouts", layouts);
    effect.writeEntry("SnapAssist", assist);
    plugins.writeEntry("org.wildos.snapEnabled", load);
    config->sync();
    // Effects are loaded and unloaded by name; a plain reconfigure neither
    // starts nor stops one, nor makes a running one read its settings again.
    QString method = QStringLiteral("reconfigureEffect");
    if (load != wasLoaded) {
        method = load ? QStringLiteral("loadEffect") : QStringLiteral("unloadEffect");
    } else if (!load) {
        method.clear();
    }
    if (!method.isEmpty()) {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"), method);
        message << QStringLiteral("org.wildos.snap");
        QDBusConnection::sessionBus().asyncCall(message);
    }
    reconfigureKWin();
}

bool System::altTabAllDesktops() const
{
    return KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("TabBox")).readEntry("DesktopMode", 1) == 0;
}

void System::setAltTabAllDesktops(bool on)
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    config->group(QStringLiteral("TabBox")).writeEntry("DesktopMode", on ? 0 : 1);
    config->sync();
    reconfigureKWin();
}

namespace
{
// The defaults for new accounts, which /etc/skel/.config/wildosrc also
// holds: every corner does nothing until chosen, as on Windows, since a
// corner that fires on its own is easy to set off by accident.
const QVariantMap hotCornerDefaults = {
    {QStringLiteral("TopLeft"), QStringLiteral("none")},
    {QStringLiteral("TopRight"), QStringLiteral("none")},
    {QStringLiteral("BottomLeft"), QStringLiteral("none")},
    {QStringLiteral("BottomRight"), QStringLiteral("none")},
    {QStringLiteral("TopLeftKey"), QStringLiteral("none")},
    {QStringLiteral("TopRightKey"), QStringLiteral("none")},
    {QStringLiteral("BottomLeftKey"), QStringLiteral("none")},
    {QStringLiteral("BottomRightKey"), QStringLiteral("none")},
    {QStringLiteral("Delay"), 150},
    {QStringLiteral("SkipFullScreen"), true},
};
}

QVariantMap System::hotCorners() const
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    config->reparseConfiguration();
    const KConfigGroup group = config->group(QStringLiteral("HotCorners"));
    QVariantMap result;
    for (auto it = hotCornerDefaults.cbegin(); it != hotCornerDefaults.cend(); ++it) {
        result.insert(it.key(), group.readEntry(it.key(), it.value()));
    }
    // KConfig hands numbers and switches back as text.
    result[QStringLiteral("Delay")] = group.readEntry("Delay", 150);
    result[QStringLiteral("SkipFullScreen")] = group.readEntry("SkipFullScreen", true);
    return result;
}

// KWin watches the group and applies a change at once.
void System::setHotCorner(const QString &key, const QVariant &value)
{
    if (!hotCornerDefaults.contains(key)) {
        return;
    }
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    config->group(QStringLiteral("HotCorners")).writeEntry(key, value, KConfig::Notify);
    config->sync();
    Q_EMIT hotCornersChanged();
}

void System::resetHotCorners()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    KConfigGroup group = config->group(QStringLiteral("HotCorners"));
    for (auto it = hotCornerDefaults.cbegin(); it != hotCornerDefaults.cend(); ++it) {
        group.writeEntry(it.key(), it.value(), KConfig::Notify);
    }
    config->sync();
    Q_EMIT hotCornersChanged();
}

bool System::taskbarShowDesktop() const
{
    return KSharedConfig::openConfig(QStringLiteral("wildosrc"))->group(QStringLiteral("Taskbar")).readEntry("ShowDesktop", true);
}

void System::setTaskbarShowDesktop(bool on)
{
    KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    wildos->group(QStringLiteral("Taskbar")).writeEntry("ShowDesktop", on, KConfig::Notify);
    wildos->sync();
    Q_EMIT taskbarShowDesktopChanged();
}

bool System::taskbarPeek() const
{
    return KSharedConfig::openConfig(QStringLiteral("wildosrc"))->group(QStringLiteral("Taskbar")).readEntry("PeekDesktop", false);
}

void System::setTaskbarPeek(bool on)
{
    KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    wildos->group(QStringLiteral("Taskbar")).writeEntry("PeekDesktop", on, KConfig::Notify);
    wildos->sync();
    Q_EMIT taskbarShowDesktopChanged();
}

void System::reconfigureKWin()
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/KWin"), QStringLiteral("org.kde.KWin"),
                                                  QStringLiteral("reconfigure"));
    QDBusConnection::sessionBus().asyncCall(message);
    Q_EMIT kwinChanged();
}

QString System::timeZone() const
{
    const QTimeZone zone = QTimeZone::systemTimeZone();
    return QString::fromUtf8(zone.id()).replace(QLatin1Char('_'), QLatin1Char(' '));
}

QString System::region() const
{
    const QLocale locale = QLocale::system();
    return QStringLiteral("%1 (%2)").arg(QLocale::languageToString(locale.language()), QLocale::territoryToString(locale.territory()));
}

bool System::hasProgram(const QString &program) const
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

void System::launch(const QString &program, const QStringList &arguments)
{
    QProcess::startDetached(program, arguments);
}

QString System::formatBytes(qint64 bytes) const
{
    // Units of 1024 labelled GB, as Windows shows sizes everywhere and as
    // Files shows them, so a drive reads the same in both places.
    const QLocale locale = QLocale::system();
    const double value = double(bytes);
    const double kb = 1024.0, mb = kb * 1024, gb = mb * 1024, tb = gb * 1024;
    if (value >= tb) {
        return tr("%1 TB").arg(locale.toString(value / tb, 'f', 2));
    }
    if (value >= gb) {
        return tr("%1 GB").arg(locale.toString(value / gb, 'f', value >= 100 * gb ? 0 : 1));
    }
    if (value >= mb) {
        return tr("%1 MB").arg(locale.toString(value / mb, 'f', 0));
    }
    if (value >= kb) {
        return tr("%1 KB").arg(locale.toString(value / kb, 'f', 0));
    }
    return tr("%1 bytes").arg(bytes);
}

QString System::thumbnail(const QString &path) const
{
    return thumbnailFor(localPath(path));
}

// The taskbar is centred by an expanding spacer before Start (see the Wild OS
// look-and-feel layout). Left-aligned, as on Windows 10, is that spacer
// shrunk to nothing; it stays, so centring again is one change.
namespace
{
const QString leftSpacerScript = QStringLiteral(
    "var p = panels()[0]; var s = null;"
    "if (p) { var all = p.widgets('org.kde.plasma.panelspacer');"
    "  all.sort(function (a, b) { return a.geometry.x - b.geometry.x; });"
    "  s = all.length ? all[0] : null; }");
}

void System::readTaskbarLayout()
{
    const QString expanding = evaluatePlasmaScript(leftSpacerScript
        + QStringLiteral("if (s) { s.currentConfigGroup = ['General']; print(String(s.readConfig('expanding', true))); }"));
    m_taskbarCentred = expanding != QLatin1String("false");
    Q_EMIT taskbarCentredChanged();
}

void System::setTaskbarCentred(bool centred)
{
    evaluatePlasmaScript(leftSpacerScript
        + QStringLiteral("if (s) { s.currentConfigGroup = ['General']; s.writeConfig('expanding', %1);"
                         " s.writeConfig('length', 1); s.reloadConfig(); }")
              .arg(centred ? QStringLiteral("true") : QStringLiteral("false")));
    m_taskbarCentred = centred;
    Q_EMIT taskbarCentredChanged();
}

// The taskbar's showOnlyCurrentDesktop, the stock task manager's key, which
// the Wild OS taskbar (org.wildos.tasks) binds its desktop filter to.
namespace
{
const QString tasksScript = QStringLiteral(
    "var tasks = []; panels().forEach(function (p) { tasks = tasks.concat(p.widgets('org.wildos.tasks')); });");
}

void System::readTaskbarDesktops()
{
    const QString only = evaluatePlasmaScript(tasksScript
        + QStringLiteral("if (tasks.length) { tasks[0].currentConfigGroup = ['General']; print(String(tasks[0].readConfig('showOnlyCurrentDesktop', true))); }"));
    const bool all = only == QLatin1String("false");
    if (all != m_taskbarAllDesktops) {
        m_taskbarAllDesktops = all;
        Q_EMIT taskbarAllDesktopsChanged();
    }
}

void System::setTaskbarAllDesktops(bool all)
{
    evaluatePlasmaScript(tasksScript
        + QStringLiteral("tasks.forEach(function (t) { t.currentConfigGroup = ['General']; t.writeConfig('showOnlyCurrentDesktop', %1); t.reloadConfig(); });")
              .arg(all ? QStringLiteral("false") : QStringLiteral("true")));
    if (all != m_taskbarAllDesktops) {
        m_taskbarAllDesktops = all;
        Q_EMIT taskbarAllDesktopsChanged();
    }
}

// The item's own "shown" setting (its contents/config/main.xml), which it
// reads live, so turning it off keeps it on the taskbar, and in its place,
// for turning on again.
namespace
{
const QString winModeScript = QStringLiteral(
    "var items = []; panels().forEach(function (p) { items = items.concat(p.widgets('org.wildos.winmode')); });");
}

// Plasma widgets on the Wild OS desktop (org.wildos.desktop's allowWidgets):
// off by default, never blocked; every desktop, one per screen, follows it.
namespace
{
const QString desktopsScript = QStringLiteral("var d = desktops().filter(function (x) { return x.type === 'org.wildos.desktop'; });");
}

void System::readDesktopWidgets()
{
    const QString allowed = evaluatePlasmaScript(desktopsScript
        + QStringLiteral("if (d.length) { d[0].currentConfigGroup = ['General']; print(String(d[0].readConfig('allowWidgets', false))); }"));
    const bool on = allowed == QLatin1String("true");
    if (on != m_desktopWidgets) {
        m_desktopWidgets = on;
        Q_EMIT desktopWidgetsChanged();
    }
}

void System::setDesktopWidgets(bool allowed)
{
    evaluatePlasmaScript(desktopsScript
        + QStringLiteral("d.forEach(function (x) { x.currentConfigGroup = ['General']; x.writeConfig('allowWidgets', %1); x.reloadConfig(); });")
              .arg(allowed ? QStringLiteral("true") : QStringLiteral("false")));
    m_desktopWidgets = allowed;
    Q_EMIT desktopWidgetsChanged();
}

// The taskbar is locked so it can't be taken apart by accident (the
// look-and-feel layout locks the panel and each of its widgets). Widgets on
// the taskbar lift that lock, and locking again puts it back the same way.
void System::readTaskbarWidgets()
{
    const QString locked = evaluatePlasmaScript(QStringLiteral("print(panels().length ? String(panels()[0].locked) : 'true')"));
    const bool on = locked == QLatin1String("false");
    if (on != m_taskbarWidgets) {
        m_taskbarWidgets = on;
        Q_EMIT taskbarWidgetsChanged();
    }
}

void System::setTaskbarWidgets(bool allowed)
{
    evaluatePlasmaScript(QStringLiteral(
        "panels().forEach(function (p) {"
        "  p.locked = %1;"
        "  p.widgets().forEach(function (w) { w.currentConfigGroup = []; w.writeConfig('immutability', %2); w.locked = %1; });"
        "});")
            .arg(allowed ? QStringLiteral("false") : QStringLiteral("true"), allowed ? QStringLiteral("1") : QStringLiteral("2")));
    m_taskbarWidgets = allowed;
    Q_EMIT taskbarWidgetsChanged();
}

// The widgets someone added to the taskbar: everything on it that isn't
// part of the Wild OS taskbar itself. Plasma's own way to remove a panel
// widget is hidden with the rest of the taskbar's right-click menu, so
// Settings lists them with a Remove button instead.
void System::readAddedTaskbarWidgets()
{
    const QString answer = evaluatePlasmaScript(QStringLiteral(
        "var out = []; panels().forEach(function (p) { p.widgets().forEach(function (w) {"
        "  if (w.type.indexOf('org.wildos.') !== 0 && w.type !== 'org.kde.plasma.panelspacer') out.push(w.id + ' ' + w.type); }); });"
        "print(out.join(';'));"));
    QVariantList list;
    const auto lines = answer.split(QLatin1Char(';'), Qt::SkipEmptyParts);
    for (const QString &line : lines) {
        const int space = line.indexOf(QLatin1Char(' '));
        if (space <= 0) {
            continue;
        }
        const QString plugin = line.mid(space + 1).trimmed();
        const QString file = QStandardPaths::locate(QStandardPaths::GenericDataLocation,
                                                    QStringLiteral("plasma/plasmoids/%1/metadata.json").arg(plugin));
        const KPluginMetaData meta = file.isEmpty() ? KPluginMetaData() : KPluginMetaData::fromJsonFile(file);
        list.append(QVariantMap{{QStringLiteral("id"), line.left(space).toInt()},
                                {QStringLiteral("name"), meta.isValid() && !meta.name().isEmpty() ? meta.name() : plugin},
                                {QStringLiteral("icon"), meta.iconName()}});
    }
    if (list != m_addedTaskbarWidgets) {
        m_addedTaskbarWidgets = list;
        Q_EMIT addedTaskbarWidgetsChanged();
    }
}

void System::removeTaskbarWidget(int id)
{
    evaluatePlasmaScript(QStringLiteral(
        "panels().forEach(function (p) { p.widgets().forEach(function (w) { if (w.id === %1) w.remove(); }); });").arg(id));
    readAddedTaskbarWidgets();
}

// Plasma's widget picker; widgets are dragged from it onto the taskbar.
void System::openWidgetExplorer()
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.plasmashell"), QStringLiteral("/PlasmaShell"),
                                                  QStringLiteral("org.kde.PlasmaShell"), QStringLiteral("toggleWidgetExplorer"));
    QDBusConnection::sessionBus().asyncCall(message);
}

void System::readTaskbarWinModeStatus()
{
    const QString shown = evaluatePlasmaScript(winModeScript
        + QStringLiteral("if (items.length) { items[0].currentConfigGroup = ['General']; print(String(items[0].readConfig('shown', true))); }"));
    const bool on = shown != QLatin1String("false");
    if (on != m_taskbarWinModeStatus) {
        m_taskbarWinModeStatus = on;
        Q_EMIT taskbarWinModeStatusChanged();
    }
}

void System::setTaskbarWinModeStatus(bool shown)
{
    evaluatePlasmaScript(winModeScript
        + QStringLiteral("items.forEach(function (w) { w.currentConfigGroup = ['General']; w.writeConfig('shown', %1); w.reloadConfig(); });")
              .arg(shown ? QStringLiteral("true") : QStringLiteral("false")));
    if (shown != m_taskbarWinModeStatus) {
        m_taskbarWinModeStatus = shown;
        Q_EMIT taskbarWinModeStatusChanged();
    }
}

// Plasma's own slideshow wallpaper, pointed at one folder. Turning it off
// goes back to the single picture, which the image plugin still remembers.
void System::setSlideshow(bool on, const QString &folder)
{
    QString path = localPath(folder);
    if (on && path.isEmpty()) {
        path = m_slideshowFolder.isEmpty() ? QStandardPaths::writableLocation(QStandardPaths::PicturesLocation) : m_slideshowFolder;
    }
    QString script = QStringLiteral("desktops().forEach(function (d) {");
    if (on) {
        QString quoted = QUrl::fromLocalFile(path).toString();
        quoted.replace(QLatin1Char('\\'), QStringLiteral("\\\\")).replace(QLatin1Char('\''), QStringLiteral("\\'"));
        script += QStringLiteral(
                      "d.wallpaperPlugin = 'org.kde.slideshow';"
                      "d.currentConfigGroup = ['Wallpaper', 'org.kde.slideshow', 'General'];"
                      "d.writeConfig('SlidePaths', ['%1']);"
                      "d.writeConfig('SlideInterval', 1800);")
                      .arg(quoted);
    } else {
        const QString plugin = wallpaperPluginFor(m_wallpaper);
        script += QStringLiteral("d.wallpaperPlugin = '%1';").arg(plugin.isEmpty() ? QStringLiteral("org.kde.image") : scriptString(plugin));
    }
    script += QStringLiteral("d.reloadConfig(); });");
    evaluatePlasmaScript(script);
    m_slideshow = on;
    m_slideshowFolder = on ? path : QString();
    Q_EMIT wallpaperChanged();
}

bool System::transparency() const
{
    return KSharedConfig::openConfig(QStringLiteral("kwinrc"))->group(QStringLiteral("Plugins")).readEntry("blurEnabled", true);
}

// The frosted glass is KWin's blur, with its contrast effect keeping text on
// glass readable. With both off, Plasma draws menus and the taskbar from the
// theme's more solid variant.
void System::setTransparency(bool on)
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kwinrc"));
    KConfigGroup plugins = config->group(QStringLiteral("Plugins"));
    plugins.writeEntry("blurEnabled", on);
    plugins.writeEntry("contrastEnabled", on);
    config->sync();
    for (const QString &effect : {QStringLiteral("blur"), QStringLiteral("contrast")}) {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.KWin"), QStringLiteral("/Effects"), QStringLiteral("org.kde.kwin.Effects"),
                                                      on ? QStringLiteral("loadEffect") : QStringLiteral("unloadEffect"));
        message << effect;
        QDBusConnection::sessionBus().asyncCall(message);
    }
    reconfigureKWin();
    Q_EMIT transparencyChanged();
}

namespace
{
// The Wild OS defaults from /etc/xdg/kdeglobals, at 100%.
struct FontKey {
    const char *group;
    const char *key;
    const char *fallback;
    double base;
};
const FontKey fontKeys[] = {
    {"General", "font", "Albert Sans,10,-1,5,50,0,0,0,0,0", 10},
    {"General", "menuFont", "Albert Sans,10,-1,5,50,0,0,0,0,0", 10},
    {"General", "toolBarFont", "Albert Sans,10,-1,5,50,0,0,0,0,0", 10},
    {"General", "smallestReadableFont", "Albert Sans,9,-1,5,50,0,0,0,0,0", 9},
    {"General", "fixed", "Hack,10,-1,5,50,0,0,0,0,0", 10},
    {"WM", "activeFont", "Albert Sans,10,-1,5,550,0,0,0,0,0,0,0,0,0,0,1", 10},
};
}

void System::readTextScale()
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    const QString font = config->group(QStringLiteral("General")).readEntry("font", QString::fromLatin1(fontKeys[0].fallback));
    const double size = font.section(QLatin1Char(','), 1, 1).toDouble();
    m_textScale = size > 0 ? int(std::lround(size * 10.0)) : 100;
    Q_EMIT textScaleChanged();
}

// Every font size the desktop uses, scaled together. The sizes are written
// in the form the defaults use: ten fields, which Qt 5 apps can still read,
// and the Qt 6 form for the window title font, which keeps its 550 weight.
void System::setTextScale(int percent)
{
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    const QLocale c = QLocale::c();
    for (const FontKey &f : fontKeys) {
        KConfigGroup group = config->group(QString::fromLatin1(f.group));
        QStringList fields = group.readEntry(f.key, QString::fromLatin1(f.fallback)).split(QLatin1Char(','));
        if (fields.size() < 2) {
            fields = QString::fromLatin1(f.fallback).split(QLatin1Char(','));
        }
        const double size = std::round(f.base * percent / 100.0 * 2.0) / 2.0;
        fields[1] = c.toString(size, 'g', 4);
        group.writeEntry(f.key, fields.join(QLatin1Char(',')), KConfig::Notify);
    }
    config->sync();
    m_textScale = percent;
    Q_EMIT textScaleChanged();

    // The same two announcements the fonts module makes: the platform theme
    // in every running Qt app rereads the font on the first, older toolkits
    // listen for the second (1 is FontChanged).
    QDBusConnection::sessionBus().send(QDBusMessage::createSignal(QStringLiteral("/KDEPlatformTheme"),
                                                                  QStringLiteral("org.kde.KDEPlatformTheme"),
                                                                  QStringLiteral("refreshFonts")));
    auto notify = QDBusMessage::createSignal(QStringLiteral("/KGlobalSettings"), QStringLiteral("org.kde.KGlobalSettings"), QStringLiteral("notifyChange"));
    notify << 1 << 0;
    QDBusConnection::sessionBus().send(notify);
}

// USBGuard is a system service; systemd asks for the administrator password
// through polkit before switching it.
void System::setUsbProtection(bool on)
{
    const QString service = QStringLiteral("org.freedesktop.systemd1");
    const QString path = QStringLiteral("/org/freedesktop/systemd1");
    const QString manager = QStringLiteral("org.freedesktop.systemd1.Manager");
    // usbguard-dbus requires usbguard and is enabled on its own, so left
    // enabled it started USBGuard again at the next start-up.
    const QStringList units = {QStringLiteral("usbguard.service"), QStringLiteral("usbguard-dbus.service")};

    auto unitFiles = QDBusMessage::createMethodCall(service, path, manager, on ? QStringLiteral("EnableUnitFiles") : QStringLiteral("DisableUnitFiles"));
    if (on) {
        unitFiles << units << false << true;
    } else {
        unitFiles << units << false;
    }
    unitFiles.setInteractiveAuthorizationAllowed(true);
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(unitFiles, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, on, service, path, manager] {
        watcher->deleteLater();
        if (watcher->isError()) {
            const QString name = watcher->error().name();
            Q_EMIT usbProtectionFailed(name.contains(QLatin1String("AccessDenied")) || name.contains(QLatin1String("Interactive"))
                                           ? tr("USB protection was not changed because the password was not given.")
                                           : tr("USB protection could not be changed. %1").arg(watcher->error().message()));
            refreshSecurity();
            return;
        }
        auto unit = QDBusMessage::createMethodCall(service, path, manager, on ? QStringLiteral("StartUnit") : QStringLiteral("StopUnit"));
        unit << QStringLiteral("usbguard.service") << QStringLiteral("replace");
        unit.setInteractiveAuthorizationAllowed(true);
        auto next = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(unit, 120000), this);
        connect(next, &QDBusPendingCallWatcher::finished, this, [this, next] {
            next->deleteLater();
            // The service takes a moment to settle either way.
            QTimer::singleShot(1500, this, &System::refreshSecurity);
        });
    });
}

namespace
{
QDBusMessage logindCall(const QString &method)
{
    return QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.login1"),
                                          QStringLiteral("/org/freedesktop/login1"),
                                          QStringLiteral("org.freedesktop.login1.Manager"),
                                          method);
}
}

// Secure Boot can only be switched in the firmware's own settings. Where the
// firmware supports it, logind can ask for them on the next start, so the
// page can offer to go straight there instead of naming keys to press.
bool System::canRestartToFirmware() const
{
    const QDBusMessage reply = QDBusConnection::systemBus().call(logindCall(QStringLiteral("CanRebootToFirmwareSetup")), QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return false;
    }
    const QString answer = reply.arguments().constFirst().toString();
    return answer == QLatin1String("yes") || answer == QLatin1String("challenge");
}

void System::restartToFirmware()
{
    auto message = logindCall(QStringLiteral("SetRebootToFirmwareSetup"));
    message << true;
    message.setInteractiveAuthorizationAllowed(true);
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        if (watcher->isError()) {
            Q_EMIT firmwareRestartFailed(tr("The computer could not be set to open its firmware settings. %1").arg(watcher->error().message()));
            return;
        }
        // Plasma's own restart path, so open apps get the chance to save.
        QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(QStringLiteral("org.kde.Shutdown"),
                                                                               QStringLiteral("/Shutdown"),
                                                                               QStringLiteral("org.kde.Shutdown"),
                                                                               QStringLiteral("logoutAndReboot")));
    });
}

namespace
{
const QString policyHelper = QStringLiteral("/usr/libexec/wildos/settings-update-policy");
const QString protectionHelper = QStringLiteral("/usr/libexec/wildos/settings-protection");
}

void System::readProtectionLevel()
{
    QFile file(QStringLiteral("/etc/wildos/protection-level"));
    QString level = file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()).trimmed() : QString();
    if (level != QLatin1String("strict") && level != QLatin1String("maximum")) {
        level = QStringLiteral("standard");
    }
    if (level != m_protectionLevel) {
        m_protectionLevel = level;
        Q_EMIT protectionLevelChanged();
    }
}

// The screen lock is the person's own setting, so the levels only tighten
// it: Strict locks after at most 5 minutes, Maximum after at most 2, both
// at once with no grace period and on waking. Standard leaves it as it is.
void System::applyLockTimes(const QString &level)
{
    const int most = level == QLatin1String("maximum") ? 2 : level == QLatin1String("strict") ? 5 : 0;
    if (most == 0) {
        return;
    }
    KSharedConfigPtr config = KSharedConfig::openConfig(QStringLiteral("kscreenlockerrc"));
    KConfigGroup daemon = config->group(QStringLiteral("Daemon"));
    daemon.writeEntry("Autolock", true, KConfig::Notify);
    daemon.writeEntry("LockOnResume", true, KConfig::Notify);
    daemon.writeEntry("LockGrace", 0, KConfig::Notify);
    daemon.writeEntry("Timeout", qMin(daemon.readEntry("Timeout", 5), most), KConfig::Notify);
    config->sync();
}

// The firewall, USB, Bluetooth and camera parts need root; the helper takes
// only the three level names, and polkit asks for the password.
void System::setProtectionLevel(const QString &level)
{
    if (m_protectionBusy || level == m_protectionLevel
        || (level != QLatin1String("standard") && level != QLatin1String("strict") && level != QLatin1String("maximum"))) {
        return;
    }
    m_protectionBusy = true;
    Q_EMIT protectionLevelChanged();
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process, level](int code) {
        process->deleteLater();
        m_protectionBusy = false;
        if (code == 0) {
            applyLockTimes(level);
        } else if (code == 126 || code == 127) {
            Q_EMIT protectionLevelFailed(tr("The level was not changed because the password was not given."));
        } else {
            Q_EMIT protectionLevelFailed(tr("The level could not be changed."));
        }
        readProtectionLevel();
        Q_EMIT protectionLevelChanged();
        refreshSecurity();
    });
    process->start(QStringLiteral("pkexec"), {protectionHelper, level});
}

void System::refreshUpdatePolicy()
{
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        const QString dump = QString::fromUtf8(process->readAllStandardOutput());
        const auto value = [&dump](const QString &key) {
            const auto match = QRegularExpression(QStringLiteral("^%1 \"([^\"]*)\";$").arg(QRegularExpression::escape(key)), QRegularExpression::MultilineOption)
                                   .match(dump);
            return match.hasMatch() ? match.captured(1) : QString();
        };
        // The channel is the Suites line of the Wild OS source.
        QString channel = QStringLiteral("stable");
        QFile sources(QStringLiteral("/etc/apt/sources.list.d/wildos.sources"));
        if (sources.open(QIODevice::ReadOnly)) {
            const auto match = QRegularExpression(QStringLiteral("^Suites:\\s*(\\S+)"), QRegularExpression::MultilineOption)
                                   .match(QString::fromUtf8(sources.readAll()));
            if (match.hasMatch()) {
                channel = match.captured(1);
            }
        }
        m_updatePolicy = {
            {QStringLiteral("channel"), channel},
            {QStringLiteral("channelAvailable"), sources.exists() && QFileInfo(policyHelper).isExecutable()},
            {QStringLiteral("available"), hasProgram(QStringLiteral("unattended-upgrade")) && QFileInfo(policyHelper).isExecutable()},
            {QStringLiteral("auto"), value(QStringLiteral("APT::Periodic::Unattended-Upgrade")) == QLatin1String("1")},
            {QStringLiteral("restart"),
             value(QStringLiteral("Unattended-Upgrade::Automatic-Reboot")) == QLatin1String("true") ? QStringLiteral("idle") : QStringLiteral("ask")},
        };
        Q_EMIT updatePolicyChanged();
    });
    process->start(QStringLiteral("apt-config"), {QStringLiteral("dump")});
}

// Writing apt's configuration needs root. The helper only ever writes the
// Wild OS files, and polkit asks for the password.
void System::setUpdatePolicy(const QString &key, const QString &value)
{
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code) {
        process->deleteLater();
        if (code == 126 || code == 127) {
            Q_EMIT updatePolicyFailed(tr("The setting was not changed because the password was not given."));
        } else if (code != 0) {
            Q_EMIT updatePolicyFailed(tr("The setting could not be changed."));
        }
        refreshUpdatePolicy();
        refreshSecurity();
    });
    process->start(QStringLiteral("pkexec"), {policyHelper, key, value});
    // Show the new state straight away; a refusal puts it back.
    if (key == QLatin1String("auto")) {
        m_updatePolicy.insert(QStringLiteral("auto"), value == QLatin1String("on"));
    } else if (key == QLatin1String("channel")) {
        m_updatePolicy.insert(QStringLiteral("channel"), value);
    } else {
        m_updatePolicy.insert(QStringLiteral("restart"), value);
    }
    Q_EMIT updatePolicyChanged();
}

// Apps from the Store are Flatpaks, which update there rather than with the
// system updates. The count comes from the summaries Flatpak already has, so
// checking costs no download.
void System::refreshAppUpdates()
{
    if (!hasProgram(QStringLiteral("flatpak"))) {
        return;
    }
    auto process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process](int code, QProcess::ExitStatus status) {
        process->deleteLater();
        if (status != QProcess::NormalExit || code != 0) {
            return;
        }
        const auto lines = QString::fromUtf8(process->readAllStandardOutput()).split(QLatin1Char('\n'), Qt::SkipEmptyParts);
        QSet<QString> apps;
        for (const QString &line : lines) {
            const QString id = line.trimmed();
            if (!id.isEmpty()) {
                apps.insert(id);
            }
        }
        m_appUpdates = apps.size();
        Q_EMIT appUpdatesChanged();
    });
    process->start(QStringLiteral("flatpak"),
                   {QStringLiteral("remote-ls"), QStringLiteral("--updates"), QStringLiteral("--cached"), QStringLiteral("--app"), QStringLiteral("--columns=application")});
}

QString System::storeApp() const
{
    if (hasProgram(QStringLiteral("wildos-store"))) {
        return QStringLiteral("wildos-store");
    }
    if (hasProgram(QStringLiteral("plasma-discover"))) {
        return QStringLiteral("plasma-discover");
    }
    return {};
}

void System::openStore(const QString &mode)
{
    const QString app = storeApp();
    const bool updates = mode == QLatin1String("updates");
    if (app == QLatin1String("wildos-store")) {
        QProcess::startDetached(app, updates ? QStringList{QStringLiteral("--page"), QStringLiteral("updates")} : QStringList{});
    } else if (!app.isEmpty()) {
        QProcess::startDetached(app, updates ? QStringList{QStringLiteral("--mode"), QStringLiteral("update")} : QStringList{});
    }
}

namespace
{
// "Wallpaper/org.kde.image/General" names nested groups.
KConfigGroup groupAt(const KSharedConfigPtr &config, const QString &path)
{
    const QStringList names = path.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    KConfigGroup group = config->group(names.value(0));
    for (int i = 1; i < names.size(); ++i) {
        group = group.group(names.at(i));
    }
    return group;
}
}

QVariant System::config(const QString &file, const QString &group, const QString &key, const QVariant &fallback) const
{
    KSharedConfigPtr config = KSharedConfig::openConfig(file);
    config->reparseConfiguration();
    const KConfigGroup g = groupAt(config, group);
    if (!g.hasKey(key)) {
        return fallback;
    }
    switch (fallback.typeId()) {
    case QMetaType::Bool:
        return g.readEntry(key, fallback.toBool());
    case QMetaType::Int:
        return g.readEntry(key, fallback.toInt());
    case QMetaType::Double:
        return g.readEntry(key, fallback.toDouble());
    default:
        return g.readEntry(key, fallback.toString());
    }
}

void System::setConfig(const QString &file, const QString &group, const QString &key, const QVariant &value)
{
    KSharedConfigPtr config = KSharedConfig::openConfig(file);
    KConfigGroup g = groupAt(config, group);
    g.writeEntry(key, value, KConfig::Notify);
    config->sync();
}

void System::reloadKWin()
{
    reconfigureKWin();
}

// Powerdevil rereads its settings when told to; this is what its own
// settings module calls after saving.
void System::reloadPowerManagement()
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Solid.PowerManagement"), QStringLiteral("/org/kde/Solid/PowerManagement"),
                                                  QStringLiteral("org.kde.Solid.PowerManagement"), QStringLiteral("refreshStatus"));
    QDBusConnection::sessionBus().asyncCall(message);
}

namespace
{
const QString profileService = QStringLiteral("org.kde.Solid.PowerManagement");
const QString profilePath = QStringLiteral("/org/kde/Solid/PowerManagement/Actions/PowerProfile");
const QString profileInterface = QStringLiteral("org.kde.Solid.PowerManagement.Actions.PowerProfile");
}

void System::refreshPower()
{
    auto choices = QDBusMessage::createMethodCall(profileService, profilePath, profileInterface, QStringLiteral("profileChoices"));
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(choices), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        QDBusPendingReply<QStringList> reply = *watcher;
        m_powerProfiles = reply.isError() ? QStringList() : reply.value();
        auto current = QDBusMessage::createMethodCall(profileService, profilePath, profileInterface, QStringLiteral("currentProfile"));
        auto next = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(current), this);
        connect(next, &QDBusPendingCallWatcher::finished, this, [this, next] {
            next->deleteLater();
            QDBusPendingReply<QString> reply = *next;
            m_powerProfile = reply.isError() ? QString() : reply.value();
            Q_EMIT powerChanged();
        });
    });
}

void System::setPowerProfile(const QString &profile)
{
    auto message = QDBusMessage::createMethodCall(profileService, profilePath, profileInterface, QStringLiteral("setProfile"));
    message << profile;
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        refreshPower();
    });
    m_powerProfile = profile;
    Q_EMIT powerChanged();
}

void System::reloadScreenLocker()
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
                                                  QStringLiteral("org.kde.screensaver"), QStringLiteral("configure"));
    QDBusConnection::sessionBus().asyncCall(message);
}

namespace
{
// Windows mode's status (boards 4.13 and 4.14) is at the left end of the
// bar in both styles. A bar from when floating put it just before Start gets
// it moved back: Plasma's scripting can't reorder a panel, so the item is
// taken off and dropped on the bar's first pixel, which a panel takes to mean
// "before everything". It holds no settings of its own to lose; it is locked
// again afterwards, as the layout locks it.
const QString placeWinModeScript = QStringLiteral(
    "panels().forEach(function (p) {"
    "  var all = p.widgets();"
    "  all.sort(function (a, b) { return a.geometry.x - b.geometry.x; });"
    "  var w = -1;"
    "  for (var i = 0; i < all.length; i++) {"
    "    if (all[i].type === 'org.wildos.winmode') w = i;"
    "  }"
    "  if (w <= 0) return;"
    "  var locked = p.locked;"
    "  p.locked = false;"
    "  all[w].remove();"
    "  var item = p.addWidget('org.wildos.winmode', 0, 0, -1, -1);"
    "  if (item) { item.currentConfigGroup = []; item.writeConfig('immutability', 2); item.locked = true; }"
    "  p.locked = locked;"
    "});");
}

// The panel (wildos-shell) watches this key and redraws straight away.
void System::setTaskbarFloating(bool floating)
{
    KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    wildos->group(QStringLiteral("Taskbar")).writeEntry("Style", floating ? QStringLiteral("floating") : QStringLiteral("docked"), KConfig::Notify);
    wildos->sync();
    // The panel's height keeps maximised windows clear of the bar: 52 px
    // docked, 76 floating (62 px pills 14 px above the screen edge).
    evaluatePlasmaScript(QStringLiteral("panels().forEach(function (p) { if (p.location === 'bottom') p.height = %1; });").arg(floating ? 76 : 52));
    evaluatePlasmaScript(placeWinModeScript);
    if (floating != m_taskbarFloating) {
        m_taskbarFloating = floating;
        Q_EMIT taskbarFloatingChanged();
    }
}
