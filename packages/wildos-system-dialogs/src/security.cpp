#include "security.h"
#include "session.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusObjectPath>
#include <QDBusVariant>
#include <QFileInfo>
#include <QProcess>
#include <QStandardPaths>
#include <QTimer>

namespace
{
constexpr int quick = 400;

QVariant dbusProperty(const QDBusConnection &bus, const QString &service, const QString &path, const QString &interface, const QString &name)
{
    auto message = QDBusMessage::createMethodCall(service, path, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("Get"));
    message << interface << name;
    message.setAutoStartService(false);
    const QDBusMessage reply = bus.call(message, QDBus::Block, quick);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return reply.arguments().constFirst().value<QDBusVariant>().variant();
}

QString call(const QDBusConnection &bus, const QString &service, const QString &path, const QString &interface, const QString &method)
{
    auto message = QDBusMessage::createMethodCall(service, path, interface, method);
    const QDBusMessage reply = bus.call(message, QDBus::Block, quick);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    return reply.arguments().constFirst().toString();
}

bool preview()
{
    return qEnvironmentVariableIsSet("WILDOS_DIALOGS_PREVIEW");
}

bool registered(const QDBusConnection &bus, const QString &service)
{
    return bus.isConnected() && bus.interface()->isServiceRegistered(service);
}

const QString login1 = QStringLiteral("org.freedesktop.login1");
const QString login1Path = QStringLiteral("/org/freedesktop/login1");
const QString login1Manager = QStringLiteral("org.freedesktop.login1.Manager");
const QString displayManager = QStringLiteral("org.freedesktop.DisplayManager");

QString seatPath()
{
    const QString env = qEnvironmentVariable("XDG_SEAT_PATH");
    return env.isEmpty() ? QStringLiteral("/org/freedesktop/DisplayManager/Seat0") : env;
}

KConfigGroup kaccess(const QString &group)
{
    return KSharedConfig::openConfig(QStringLiteral("kaccessrc"))->group(group);
}

bool hasApp(const QString &desktopFile)
{
    return !QStandardPaths::locate(QStandardPaths::ApplicationsLocation, desktopFile).isEmpty();
}

void startApp(const QString &desktopId)
{
    // kstart starts it as an app (launch feedback, its own scope), as the
    // taskbar does.
    if (!QStandardPaths::findExecutable(QStringLiteral("kstart")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("kstart"), {QStringLiteral("--application"), desktopId});
    } else if (!QStandardPaths::findExecutable(QStringLiteral("gtk-launch")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("gtk-launch"), {desktopId});
    }
}
}

Security::Security(QObject *parent)
    : QObject(parent)
{
    refresh();
}

void Security::show()
{
    if (!m_open) {
        m_open = true;
        Q_EMIT openChanged();
    }
    QTimer::singleShot(0, this, &Security::refresh);
}

void Security::close()
{
    if (m_open) {
        m_open = false;
        Q_EMIT openChanged();
    }
    if (m_quitOnClose) {
        QTimer::singleShot(200, qApp, &QCoreApplication::quit);
    }
}

void Security::refresh()
{
    const QDBusConnection system = QDBusConnection::systemBus();
    const QDBusConnection session = QDBusConnection::sessionBus();

    m_canSwitchUser = registered(system, displayManager) && dbusProperty(system, displayManager, seatPath(), QStringLiteral("org.freedesktop.DisplayManager.Seat"), QStringLiteral("CanSwitch")).toBool();
    m_canSignOut = registered(session, QStringLiteral("org.kde.Shutdown")) || !SessionInfo::id().isEmpty();
    const auto can = [&system](const QString &method) {
        const QString answer = call(system, login1, login1Path, login1Manager, method);
        return answer == QLatin1String("yes") || answer == QLatin1String("challenge");
    };
    m_canSleep = can(QStringLiteral("CanSuspend"));
    m_canShutDown = can(QStringLiteral("CanPowerOff")) || registered(session, QStringLiteral("org.kde.Shutdown"));

    const QString nm = QStringLiteral("org.freedesktop.NetworkManager");
    m_networkAvailable = registered(system, nm);
    m_networkConnected = false;
    m_networkText.clear();
    if (m_networkAvailable) {
        const uint state = dbusProperty(system, nm, QStringLiteral("/org/freedesktop/NetworkManager"), nm, QStringLiteral("State")).toUInt();
        const auto primary = dbusProperty(system, nm, QStringLiteral("/org/freedesktop/NetworkManager"), nm, QStringLiteral("PrimaryConnection")).value<QDBusObjectPath>();
        QString name;
        if (!primary.path().isEmpty() && primary.path() != QLatin1String("/")) {
            name = dbusProperty(system, nm, primary.path(), QStringLiteral("org.freedesktop.NetworkManager.Connection.Active"), QStringLiteral("Id")).toString();
        }
        // NetworkManager's states: 70 connected to the internet, 50 and 60
        // connected to a network only.
        if (state >= 70) {
            m_networkConnected = true;
            m_networkText = name.isEmpty() ? tr("Connected to the internet") : tr("Connected to %1").arg(name);
        } else if (state >= 50) {
            m_networkText = name.isEmpty() ? tr("Connected, no internet") : tr("%1, no internet").arg(name);
        } else {
            m_networkText = tr("Not connected");
        }
    }
    // Design reviews show every option, as the board does.
    if (preview()) {
        m_canSwitchUser = m_canSignOut = m_canSleep = m_canShutDown = m_networkAvailable = m_networkConnected = true;
        m_networkText = QStringLiteral("Connected to Home");
    }
    Q_EMIT refreshed();
}

bool Security::canChangePassword() const
{
    return preview() || !QStandardPaths::findExecutable(QStringLiteral("wildos-settings")).isEmpty();
}

bool Security::canTaskManager() const
{
    return preview() || hasApp(QStringLiteral("org.wildos.taskmanager.desktop")) || hasApp(QStringLiteral("org.kde.plasma-systemmonitor.desktop"));
}

void Security::lockSession()
{
    const QDBusObjectPath path = SessionInfo::path();
    if (!path.path().isEmpty()) {
        auto message = QDBusMessage::createMethodCall(login1, path.path(), QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("Lock"));
        QDBusConnection::systemBus().asyncCall(message);
        return;
    }
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("/ScreenSaver"),
                                                  QStringLiteral("org.freedesktop.ScreenSaver"), QStringLiteral("Lock"));
    QDBusConnection::sessionBus().asyncCall(message);
}

void Security::lock()
{
    close();
    lockSession();
}

// As Windows does: this session locks and stays signed in, and the sign-in
// screen comes up for the other person.
void Security::switchUser()
{
    close();
    lockSession();
    QTimer::singleShot(1200, this, [] {
        auto message = QDBusMessage::createMethodCall(displayManager, seatPath(), QStringLiteral("org.freedesktop.DisplayManager.Seat"), QStringLiteral("SwitchToGreeter"));
        QDBusConnection::systemBus().asyncCall(message);
    });
}

void Security::signOut()
{
    close();
    shutdownCall(QStringLiteral("logout"), QString());
}

// The session manager's paths give open apps the chance to save; logind is
// the fallback only when the session manager answers with an error.
// org.kde.Shutdown is started by D-Bus on demand, so asking whether it is
// running says no until the first call: checking first sent every sign out
// to logind, which ended the session underneath SDDM's helper and left a
// black screen instead of the sign-in screen. The reply only comes once
// every app has closed, so it is never waited on.
void Security::shutdownCall(const QString &kdeMethod, const QString &logindMethod)
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.Shutdown"), QStringLiteral("/Shutdown"), QStringLiteral("org.kde.Shutdown"), kdeMethod);
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 600000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [watcher, logindMethod] {
        watcher->deleteLater();
        const QDBusError::ErrorType error = watcher->error().type();
        if (!watcher->isError() || (error != QDBusError::ServiceUnknown && error != QDBusError::UnknownObject
                                    && error != QDBusError::UnknownMethod && error != QDBusError::UnknownInterface)) {
            return;
        }
        if (logindMethod.isEmpty()) {
            const QDBusObjectPath path = SessionInfo::path();
            if (!path.path().isEmpty()) {
                QDBusConnection::systemBus().asyncCall(
                    QDBusMessage::createMethodCall(login1, path.path(), QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("Terminate")));
            }
            return;
        }
        auto fallback = QDBusMessage::createMethodCall(login1, login1Path, login1Manager, logindMethod);
        fallback << true;
        QDBusConnection::systemBus().asyncCall(fallback);
    });
}

void Security::changePassword()
{
    close();
    QProcess::startDetached(QStringLiteral("wildos-settings"), {QStringLiteral("--page"), QStringLiteral("signin")});
}

void Security::taskManager()
{
    close();
    startApp(hasApp(QStringLiteral("org.wildos.taskmanager.desktop")) ? QStringLiteral("org.wildos.taskmanager") : QStringLiteral("org.kde.plasma-systemmonitor"));
}

void Security::sleep()
{
    close();
    auto message = QDBusMessage::createMethodCall(login1, login1Path, login1Manager, QStringLiteral("Suspend"));
    message << true;
    QDBusConnection::systemBus().asyncCall(message);
}

void Security::shutDown()
{
    close();
    shutdownCall(QStringLiteral("logoutAndShutdown"), QStringLiteral("PowerOff"));
}

void Security::restart()
{
    close();
    shutdownCall(QStringLiteral("logoutAndReboot"), QStringLiteral("Reboot"));
}

void Security::networkSettings()
{
    close();
    QProcess::startDetached(QStringLiteral("wildos-settings"), {QStringLiteral("--page"), QStringLiteral("network")});
}

bool Security::stickyKeys() const
{
    return kaccess(QStringLiteral("Keyboard")).readEntry("StickyKeys", false);
}

// KWin's sticky keys follow kaccessrc as soon as it changes.
void Security::setStickyKeys(bool on)
{
    KConfigGroup group = kaccess(QStringLiteral("Keyboard"));
    group.writeEntry("StickyKeys", on, KConfig::Notify);
    group.sync();
    Q_EMIT refreshed();
}

// KWin's zoom effect, through the same shortcuts as Meta+= and Meta+0.
void Security::setMagnifier(bool on)
{
    auto message = QDBusMessage::createMethodCall(QStringLiteral("org.kde.kglobalaccel"), QStringLiteral("/component/kwin"),
                                                  QStringLiteral("org.kde.kglobalaccel.Component"), QStringLiteral("invokeShortcut"));
    message << (on ? QStringLiteral("view_zoom_in") : QStringLiteral("view_actual_size"));
    QDBusConnection::sessionBus().asyncCall(message);
    m_magnifier = on;
    Q_EMIT refreshed();
}

bool Security::screenReaderAvailable() const
{
    return !QStandardPaths::findExecutable(QStringLiteral("orca")).isEmpty();
}

bool Security::screenReader() const
{
    return kaccess(QStringLiteral("ScreenReader")).readEntry("Enabled", false);
}

void Security::setScreenReader(bool on)
{
    KConfigGroup group = kaccess(QStringLiteral("ScreenReader"));
    group.writeEntry("Enabled", on, KConfig::Notify);
    group.sync();
    if (on) {
        QProcess::startDetached(QStringLiteral("orca"), {QStringLiteral("--replace")});
    } else {
        QProcess::startDetached(QStringLiteral("orca"), {QStringLiteral("--quit")});
    }
    Q_EMIT refreshed();
}
