#include "system.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QMimeDatabase>
#include <QProcess>
#include <QStandardPaths>
#include <QUrl>

#include <KConfigGroup>
#include <KDesktopFile>
#include <KSharedConfig>

#include <cstring>
#include <pwd.h>
#include <unistd.h>

namespace
{
const QString windowMenuService = QStringLiteral("org.wildos.WindowMenu");
const QString graphicsService = QStringLiteral("net.hadess.SwitcherooControl");
const QString graphicsPath = QStringLiteral("/net/hadess/SwitcherooControl");
const QString desktopSuffix = QStringLiteral(".desktop");

QString localPath(const QString &target)
{
    QString path = target;
    if (path.startsWith(QLatin1String("applications:"))) {
        path = path.mid(13);
    }
    if (path.startsWith(QLatin1String("file:"))) {
        path = QUrl(path).toLocalFile();
    }
    return path;
}

// The .desktop file the target names, or nothing when it is not an app.
QString desktopFileOf(const QString &target)
{
    const QString path = localPath(target);
    if (!path.endsWith(desktopSuffix)) {
        return {};
    }
    if (path.startsWith(QLatin1Char('/'))) {
        return QFile::exists(path) ? path : QString();
    }
    return QStandardPaths::locate(QStandardPaths::ApplicationsLocation, path);
}

// The Linux program the target names, or nothing when it is not one.
QString programOf(const QString &target)
{
    const QString path = localPath(target);
    if (!path.startsWith(QLatin1Char('/')) || path.endsWith(desktopSuffix) || !QFileInfo(path).isFile()) {
        return {};
    }
    const QMimeType type = QMimeDatabase().mimeTypeForFile(path);
    const bool program = type.name() == QLatin1String("application/x-executable") || type.name() == QLatin1String("application/x-pie-executable")
        || type.inherits(QStringLiteral("application/vnd.appimage")) || type.inherits(QStringLiteral("application/x-iso9660-appimage"));
    return program ? path : QString();
}

// What the list holds for the target: an installed app goes by its file
// name, as KIO knows it, and anything else by its path.
QString graphicsEntry(const QString &target)
{
    const QString path = localPath(target);
    if (path.startsWith(QLatin1Char('/')) && path.endsWith(desktopSuffix)
        && QStandardPaths::standardLocations(QStandardPaths::ApplicationsLocation).contains(QFileInfo(path).absolutePath())) {
        return QFileInfo(path).fileName();
    }
    return path;
}

KConfigGroup graphicsChoices()
{
    const KSharedConfig::Ptr config = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    // Another app's menu may have changed the list since this one read it.
    config->reparseConfiguration();
    return config->group(QStringLiteral("Graphics"));
}
}

System::System(QObject *parent)
    : QObject(parent)
{
}

bool System::canRun(const QString &program) const
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

bool System::run(const QString &program, const QStringList &arguments) const
{
    const QString path = QStandardPaths::findExecutable(program);
    return !path.isEmpty() && QProcess::startDetached(path, arguments);
}

bool System::hasWindowMenu() const
{
    const QDBusConnection bus = QDBusConnection::sessionBus();
    return bus.interface() && bus.interface()->isServiceRegistered(windowMenuService);
}

void System::showWindowMenu() const
{
    // KWin knows where the pointer is and which window is under it; a
    // Wayland client knows neither.
    QDBusConnection::sessionBus().asyncCall(QDBusMessage::createMethodCall(
        windowMenuService, QStringLiteral("/WindowMenu"), windowMenuService, QStringLiteral("ShowAtPointer")));
}

void System::maximizeEdge(int edges) const
{
    QDBusMessage call = QDBusMessage::createMethodCall(windowMenuService, QStringLiteral("/WindowMenu"), windowMenuService,
                                                       QStringLiteral("MaximizeEdgeAtPointer"));
    call << edges;
    QDBusConnection::sessionBus().asyncCall(call);
}

QString System::passwordHint(const QString &userName) const
{
    // Asked only after a failed attempt, so a short wait is fine; the
    // greeter has no other way to reach AccountsService from QML. An empty
    // name means whoever runs this, as the lock screen does.
    const QString service = QStringLiteral("org.freedesktop.Accounts");
    QDBusMessage find = QDBusMessage::createMethodCall(service, QStringLiteral("/org/freedesktop/Accounts"), service,
                                                       userName.isEmpty() ? QStringLiteral("FindUserById") : QStringLiteral("FindUserByName"));
    if (userName.isEmpty()) {
        find << qint64(::getuid());
    } else {
        find << userName;
    }
    const QDBusReply<QDBusObjectPath> user = QDBusConnection::systemBus().call(find, QDBus::Block, 1000);
    if (!user.isValid()) {
        return {};
    }
    QDBusMessage get = QDBusMessage::createMethodCall(service, user.value().path(), QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    get << QStringLiteral("org.freedesktop.Accounts.User") << QStringLiteral("PasswordHint");
    const QDBusReply<QDBusVariant> hint = QDBusConnection::systemBus().call(get, QDBus::Block, 1000);
    return hint.isValid() ? hint.value().variant().toString().trimmed() : QString();
}

bool System::fingerprintReader() const
{
    // The signin= line wildos-desktop's fingerprint-turn reads; off means
    // the sign-in screen turns an empty password down.
    QFile conf(QStringLiteral("/etc/wildos/fingerprint.conf"));
    if (conf.open(QIODevice::ReadOnly | QIODevice::Text)) {
        bool on = true;
        while (!conf.atEnd()) {
            const QByteArray line = conf.readLine().trimmed();
            const qsizetype equals = line.indexOf('=');
            if (equals > 0 && line.left(equals).trimmed() == "signin") {
                on = line.mid(equals + 1).trimmed() != "off";
            }
        }
        if (!on) {
            return false;
        }
    }
    // fprintd starts on demand and looks for readers as it starts.
    const QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("net.reactivated.Fprint"), QStringLiteral("/net/reactivated/Fprint/Manager"),
                                                             QStringLiteral("net.reactivated.Fprint.Manager"), QStringLiteral("GetDefaultDevice"));
    const QDBusReply<QDBusObjectPath> device = QDBusConnection::systemBus().call(call, QDBus::Block, 3000);
    return device.isValid();
}

bool System::pinSet() const
{
    const struct passwd *account = ::getpwuid(::getuid());
    if (!account || !account->pw_name || ::strchr(account->pw_name, '/')) {
        return false;
    }
    return QFile::exists(QStringLiteral("/var/lib/wildos/pin/") + QString::fromLocal8Bit(account->pw_name));
}

bool System::pinSignIn(const QString &userName) const
{
    if (userName.isEmpty() || userName == QLatin1String(".") || userName == QLatin1String("..") || userName.contains(QLatin1Char('/'))) {
        return false;
    }
    if (!QFile::exists(QStringLiteral("/var/lib/wildos/pin/") + userName)) {
        return false;
    }
    QFile store(QStringLiteral("/etc/wildos/advanced-protection"));
    int hours = 0;
    if (store.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!store.atEnd()) {
            const QByteArray line = store.readLine().trimmed();
            if (line.startsWith("fingerprint_hours=")) {
                hours = line.mid(18).toInt();
            }
        }
    }
    return hours <= 0;
}

bool System::fingerprintAllowed(const QString &userName) const
{
    int hours = 0;
    QFile store(QStringLiteral("/etc/wildos/advanced-protection"));
    if (store.open(QIODevice::ReadOnly | QIODevice::Text)) {
        while (!store.atEnd()) {
            const QByteArray line = store.readLine().trimmed();
            if (line.startsWith("fingerprint_hours=")) {
                bool ok = false;
                const int value = line.mid(18).toInt(&ok);
                if (ok) {
                    hours = value;
                }
            }
        }
    }
    if (hours <= 0) {
        return true;
    }
    if (userName.isEmpty() || userName.contains(QLatin1Char('/')) || userName.startsWith(QLatin1Char('.'))) {
        return false;
    }
    // Both in seconds since start-up, so changing the clock moves neither.
    QFile stamp(QStringLiteral("/run/wildos/fingerprint/") + userName);
    QFile uptime(QStringLiteral("/proc/uptime"));
    if (!stamp.open(QIODevice::ReadOnly) || !uptime.open(QIODevice::ReadOnly)) {
        return false;
    }
    bool okStamp = false;
    bool okNow = false;
    const qint64 typed = stamp.readAll().trimmed().toLongLong(&okStamp);
    const qint64 now = static_cast<qint64>(uptime.readAll().split(' ').value(0).toDouble(&okNow));
    return okStamp && okNow && now >= typed && now - typed < qint64(hours) * 3600;
}

bool System::twoGraphicsCards() const
{
    // Asked once per app: the cards a PC has do not change while it runs.
    // An answer that never came is asked for again.
    static int cards = -1;
    if (cards < 0) {
        QDBusMessage call = QDBusMessage::createMethodCall(graphicsService, graphicsPath, QStringLiteral("org.freedesktop.DBus.Properties"),
                                                           QStringLiteral("Get"));
        call << graphicsService << QStringLiteral("NumGPUs");
        const QDBusReply<QDBusVariant> reply = QDBusConnection::systemBus().call(call, QDBus::Block, 1000);
        if (!reply.isValid()) {
            return false;
        }
        cards = reply.value().variant().toInt();
    }
    return cards > 1;
}

bool System::canChooseGraphics(const QString &target) const
{
    const QString desktopFile = desktopFileOf(target);
    if (desktopFile.isEmpty()) {
        return !programOf(target).isEmpty();
    }
    const KConfigGroup entry = KDesktopFile(desktopFile).desktopGroup();
    return !entry.readEntry("PrefersNonDefaultGPU", false) && !entry.readEntry("X-KDE-RunOnDiscreteGpu", false);
}

bool System::dedicatedGraphics(const QString &target) const
{
    return graphicsChoices().readEntry("Dedicated", QStringList()).contains(graphicsEntry(target));
}

void System::setDedicatedGraphics(const QString &target, bool on) const
{
    if (!canChooseGraphics(target)) {
        return;
    }
    KConfigGroup group = graphicsChoices();
    QStringList chosen = group.readEntry("Dedicated", QStringList());
    const QString entry = graphicsEntry(target);
    chosen.removeAll(entry);
    if (on) {
        chosen.append(entry);
    }
    group.writeEntry("Dedicated", chosen);
    group.sync();
}

bool System::addToDesktop(const QString &target) const
{
    const QString source = desktopFileOf(target);
    const QString desktop = QStandardPaths::writableLocation(QStandardPaths::DesktopLocation);
    if (source.isEmpty() || desktop.isEmpty() || QDir::cleanPath(desktop) == QDir::homePath() || !QDir().mkpath(desktop)) {
        return false;
    }
    const QString base = QFileInfo(source).completeBaseName();
    QString path = desktop + QLatin1Char('/') + base + desktopSuffix;
    // A link left by an app that was removed counts as taken too.
    const auto taken = [](const QString &file) {
        const QFileInfo info(file);
        return info.exists() || info.isSymLink();
    };
    for (int n = 2; taken(path); ++n) {
        path = desktop + QLatin1Char('/') + base + QStringLiteral(" (%1)").arg(n) + desktopSuffix;
    }
    // A link to the app's own entry, like a Windows shortcut: it follows the
    // app's updates (a new name, icon or command) and stops working if the
    // app is removed. The desktop opens it without asking because the file
    // it leads to sits in an applications folder, which KDE trusts.
    return QFile::link(source, path);
}
