#include "system.h"

#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusReply>
#include <QDBusVariant>
#include <QFile>
#include <QProcess>
#include <QStandardPaths>

#include <unistd.h>

namespace
{
const QString windowMenuService = QStringLiteral("org.wildos.WindowMenu");
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
