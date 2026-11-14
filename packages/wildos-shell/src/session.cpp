#include "session.h"

#include <KScreenDpms/Dpms>

#include <QCoreApplication>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusVariant>
#include <QGuiApplication>
#include <QSaveFile>
#include <QScreen>
#include <QStandardPaths>

namespace
{
const QString screenSaverService = QStringLiteral("org.freedesktop.ScreenSaver");
const QString screenSaverPath = QStringLiteral("/ScreenSaver");
const QString profilesService = QStringLiteral("org.freedesktop.UPower.PowerProfiles");
const QString profilesPath = QStringLiteral("/org/freedesktop/UPower/PowerProfiles");
const QString propertiesInterface = QStringLiteral("org.freedesktop.DBus.Properties");
const QString splashService = QStringLiteral("org.kde.KSplash");
}

Session::Session(QObject *parent)
    : QObject(parent)
    , m_dpms(new KScreen::Dpms(this))
{
    QDBusConnection::sessionBus().connect(screenSaverService, screenSaverPath, screenSaverService, QStringLiteral("ActiveChanged"), this,
                                          SLOT(setLocked(bool)));
    QDBusConnection::systemBus().connect(profilesService, profilesPath, propertiesInterface, QStringLiteral("PropertiesChanged"), this,
                                         SLOT(profilesPropertiesChanged(QString, QVariantMap, QStringList)));

    // KWin reports each screen's power state as it changes, and once for
    // every screen when the protocol is bound.
    connect(m_dpms, &KScreen::Dpms::modeChanged, this, [this](KScreen::Dpms::Mode mode, QScreen *screen) {
        if (screen) {
            setScreenOff(screen->name(), mode != KScreen::Dpms::On);
        }
    });
    // A screen unplugged while off would otherwise stay listed, and a new
    // one can take its name.
    connect(qGuiApp, &QGuiApplication::screenRemoved, this, [this](QScreen *screen) {
        setScreenOff(screen->name(), false);
    });

    // KSplash leaves the bus as the splash closes; it never comes back in
    // the same session.
    auto splash = new QDBusServiceWatcher(splashService, QDBusConnection::sessionBus(), QDBusServiceWatcher::WatchForUnregistration, this);
    connect(splash, &QDBusServiceWatcher::serviceUnregistered, this, [this] {
        if (m_splashShowing) {
            m_splashShowing = false;
            Q_EMIT splashShowingChanged();
        }
    });
    m_splashShowing = QDBusConnection::sessionBus().interface()->isServiceRegistered(splashService);

    readLocked();
    readProfile();
}

void Session::desktopReady()
{
    // Names this plasmashell, so a mark left by an earlier one never counts.
    QSaveFile file(QStandardPaths::writableLocation(QStandardPaths::RuntimeLocation) + QStringLiteral("/wildos-desktop-ready"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QByteArray::number(QCoreApplication::applicationPid()));
        file.commit();
    }
    // The start-up animation watches for this name and opens onto the real
    // desktop once it appears. It goes with plasmashell, so it never counts
    // for a later one.
    QDBusConnection::sessionBus().registerService(QStringLiteral("org.wildos.shell.DesktopReady"));
}

void Session::readLocked()
{
    const QDBusMessage call = QDBusMessage::createMethodCall(screenSaverService, screenSaverPath, screenSaverService, QStringLiteral("GetActive"));
    // Asynchronous, so a slow screen locker never holds up plasmashell's
    // start; until it answers the screen counts as unlocked.
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<bool> reply = *watcher;
        if (!reply.isError()) {
            setLocked(reply.value());
        }
    });
}

void Session::readProfile()
{
    QDBusMessage call = QDBusMessage::createMethodCall(profilesService, profilesPath, propertiesInterface, QStringLiteral("Get"));
    call << profilesService << QStringLiteral("ActiveProfile");
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(call), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (!reply.isError()) {
            setPowerSaver(reply.value().variant().toString() == QLatin1String("power-saver"));
        }
    });
}

void Session::profilesPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != profilesService) {
        return;
    }
    const auto it = changed.constFind(QStringLiteral("ActiveProfile"));
    if (it != changed.constEnd()) {
        setPowerSaver(it->toString() == QLatin1String("power-saver"));
    } else if (invalidated.contains(QLatin1String("ActiveProfile"))) {
        readProfile();
    }
}

void Session::setLocked(bool locked)
{
    if (locked != m_locked) {
        m_locked = locked;
        Q_EMIT lockedChanged();
    }
}

void Session::setPowerSaver(bool powerSaver)
{
    if (powerSaver != m_powerSaver) {
        m_powerSaver = powerSaver;
        Q_EMIT powerSaverChanged();
    }
}

void Session::setScreenOff(const QString &name, bool off)
{
    if (off == m_screensOff.contains(name)) {
        return;
    }
    if (off) {
        m_screensOff.append(name);
    } else {
        m_screensOff.removeAll(name);
    }
    Q_EMIT screensOffChanged();
}
