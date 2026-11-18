#include "lighting.h"
#include "plan.h"

#include <KConfigGroup>
#include <KSharedConfig>

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>
#include <QDir>
#include <QStandardPaths>
#include <QHostAddress>
#include <QTcpSocket>

namespace
{
const QString openrgb = QStringLiteral("/usr/bin/openrgb");
const QString serverUnit = QStringLiteral("wildos-openrgb.service");
const QString login1 = QStringLiteral("org.freedesktop.login1");
const QString upower = QStringLiteral("org.freedesktop.UPower");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");

void getProperty(const QDBusConnection &bus, const QString &service, const QString &path, const QString &interface, const QString &name,
                 QObject *context, std::function<void(const QVariant &)> done)
{
    QDBusMessage call = QDBusMessage::createMethodCall(service, path, properties, QStringLiteral("Get"));
    call << interface << name;
    auto watcher = new QDBusPendingCallWatcher(bus.asyncCall(call), context);
    QObject::connect(watcher, &QDBusPendingCallWatcher::finished, context, [watcher, done] {
        watcher->deleteLater();
        const QDBusPendingReply<QDBusVariant> reply = *watcher;
        if (!reply.isError()) {
            done(reply.value().variant());
        }
    });
}
}

Lighting::Lighting(QObject *parent)
    : QObject(parent)
    , m_wildos(KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("wildosrc"))))
    , m_globals(KConfigWatcher::create(KSharedConfig::openConfig(QStringLiteral("kdeglobals"))))
{
    // Settings writes several keys at once; act on the last of them.
    m_sync.setSingleShot(true);
    m_sync.setInterval(300);
    connect(&m_sync, &QTimer::timeout, this, &Lighting::sync);

    m_retry.setInterval(500);
    connect(&m_retry, &QTimer::timeout, this, &Lighting::waitForServer);

    connect(m_wildos.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
        if (group.name() == QLatin1String("Underglow") || group.name() == QLatin1String("Lighting")) {
            readSettings();
        }
    });
    connect(m_globals.data(), &KConfigWatcher::configChanged, this, [this](const KConfigGroup &group) {
        if (group.name() == QLatin1String("General") || group.name() == QLatin1String("KDE")) {
            readSettings();
        }
    });

    QDBusConnection system = QDBusConnection::systemBus();
    system.connect(upower, QStringLiteral("/org/freedesktop/UPower"), properties, QStringLiteral("PropertiesChanged"), this,
                   SLOT(upowerChanged(QString, QVariantMap, QStringList)));
    // Devices come back from suspend in their own default lighting.
    system.connect(login1, QStringLiteral("/org/freedesktop/login1"), QStringLiteral("org.freedesktop.login1.Manager"),
                   QStringLiteral("PrepareForSleep"), this, SLOT(prepareForSleep(bool)));

    connect(&m_lights, &Lights::legacyWanted, this, &Lighting::apply);
    // Cooler's "Turn on" is the same switch as the one in Settings.
    connect(&m_lights, &Lights::onRequested, this, [this](bool on) {
        KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
        wildos->reparseConfiguration();
        wildos->group(QStringLiteral("Lighting")).writeEntry("Rgb", on, KConfig::Notify);
        wildos->sync();
        readSettings();
    });

    readSettings();
    readBattery();
    findSession();
}

void Lighting::readSettings()
{
    KSharedConfigPtr wildos = KSharedConfig::openConfig(QStringLiteral("wildosrc"));
    wildos->reparseConfiguration();
    m_rgb = wildos->group(QStringLiteral("Lighting")).readEntry("Rgb", false);
    const KConfigGroup glow = wildos->group(QStringLiteral("Underglow"));
    m_colour = glow.readEntry("Colour", QString());
    m_custom = glow.readEntry("Custom", QString());
    m_effect = glow.readEntry("Effect", QString());

    KSharedConfigPtr globals = KSharedConfig::openConfig(QStringLiteral("kdeglobals"));
    globals->reparseConfiguration();
    m_accent = globals->group(QStringLiteral("General")).readEntry("AccentColor", QString());
    m_reducedMotion = globals->group(QStringLiteral("KDE")).readEntry("AnimationDurationFactor", 1.0) <= 0.0;

    m_sync.start();
}

void Lighting::readBattery()
{
    getProperty(QDBusConnection::systemBus(), upower, QStringLiteral("/org/freedesktop/UPower"), upower, QStringLiteral("OnBattery"), this,
                [this](const QVariant &value) {
                    if (value.toBool() != m_onBattery) {
                        m_onBattery = value.toBool();
                        m_sync.start();
                    }
                });
}

void Lighting::upowerChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != upower) {
        return;
    }
    if (changed.contains(QStringLiteral("OnBattery"))) {
        const bool onBattery = changed.value(QStringLiteral("OnBattery")).toBool();
        if (onBattery != m_onBattery) {
            m_onBattery = onBattery;
            m_sync.start();
        }
    } else if (invalidated.contains(QLatin1String("OnBattery"))) {
        readBattery();
    }
}

void Lighting::findSession()
{
    // This runs as a user service, outside the session itself, so the
    // session is found through the user: Display is the graphical session
    // this account is signed in with.
    getProperty(QDBusConnection::systemBus(), login1, QStringLiteral("/org/freedesktop/login1/user/self"), QStringLiteral("org.freedesktop.login1.User"),
                QStringLiteral("Display"), this, [this](const QVariant &value) {
                    const QDBusArgument arg = value.value<QDBusArgument>();
                    QString id;
                    QDBusObjectPath path;
                    arg.beginStructure();
                    arg >> id >> path;
                    arg.endStructure();
                    if (path.path().isEmpty() || path.path() == QLatin1String("/")) {
                        return;
                    }
                    m_session = path;
                    QDBusConnection::systemBus().connect(login1, path.path(), properties, QStringLiteral("PropertiesChanged"), this,
                                                         SLOT(sessionChanged(QString, QVariantMap, QStringList)));
                    readSessionActive();
                });
}

void Lighting::readSessionActive()
{
    getProperty(QDBusConnection::systemBus(), login1, m_session.path(), QStringLiteral("org.freedesktop.login1.Session"), QStringLiteral("Active"), this,
                [this](const QVariant &value) {
                    if (value.toBool() != m_sessionActive) {
                        m_sessionActive = value.toBool();
                        m_sync.start();
                    }
                });
}

void Lighting::sessionChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated)
{
    if (interface != QLatin1String("org.freedesktop.login1.Session")) {
        return;
    }
    if (changed.contains(QStringLiteral("Active"))) {
        const bool active = changed.value(QStringLiteral("Active")).toBool();
        if (active != m_sessionActive) {
            m_sessionActive = active;
            m_sync.start();
        }
    } else if (invalidated.contains(QLatin1String("Active"))) {
        readSessionActive();
    }
}

void Lighting::prepareForSleep(bool sleeping)
{
    if (!sleeping && m_serverReady) {
        QTimer::singleShot(2000, this, [this] {
            m_lights.sendAgain();
            apply();
        });
    }
}

void Lighting::sync()
{
    const bool wanted = m_rgb && m_sessionActive;
    setServer(wanted);
    tellLights();
    if (wanted && m_serverReady) {
        apply();
    }
}

void Lighting::tellLights()
{
    m_lights.setUnderglow(Plan::look(m_colour, m_effect, m_onBattery, m_reducedMotion), Plan::colourFor(m_colour, m_custom, Plan::parseAccent(m_accent)),
                          Plan::parseAccent(m_accent), m_onBattery || m_reducedMotion);
    m_lights.setServer(!m_rgb ? Lights::Server::Off
                           : !m_sessionActive ? Lights::Server::Away
                           : m_serverReady ? Lights::Server::Ready
                           : m_retry.isActive() || !m_serverKnown ? Lights::Server::Starting
                           : Lights::Server::Failed);
}

void Lighting::setServer(bool on)
{
    // The first time round the server is stopped even if it seems off: one
    // left behind by a crashed copy of this program must not keep running.
    if (m_serverKnown && on == m_serverOn) {
        return;
    }
    m_serverKnown = true;
    m_serverOn = on;
    m_serverReady = false;
    m_retry.stop();

    QDBusMessage call = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
                                                       QStringLiteral("org.freedesktop.systemd1.Manager"),
                                                       on ? QStringLiteral("StartUnit") : QStringLiteral("StopUnit"));
    call << serverUnit << QStringLiteral("replace");
    QDBusConnection::sessionBus().asyncCall(call);
    qInfo("%s the OpenRGB server", on ? "Starting" : "Stopping");

    if (on) {
        m_waits = 0;
        m_retry.start();
    }
}

void Lighting::waitForServer()
{
    // A refused connection on the loopback answers at once, so this never
    // holds the event loop for long.
    QTcpSocket probe;
    probe.connectToHost(QHostAddress::LocalHost, Lights::port());
    if (probe.waitForConnected(250)) {
        probe.abort();
        m_retry.stop();
        m_serverReady = true;
        tellLights();
        // Detection can still be running when the server starts listening;
        // a second pass catches devices it found after the first.
        QTimer::singleShot(2000, this, &Lighting::apply);
        QTimer::singleShot(15000, this, &Lighting::apply);
        return;
    }
    if (++m_waits >= 120) {
        m_retry.stop();
        tellLights();
        qWarning("The OpenRGB server did not start listening within a minute");
    }
}

void Lighting::run(const QStringList &arguments, std::function<void(const QString &)> done)
{
    // --nodetect: if the server has gone, the command does nothing rather
    // than open the devices itself. Its settings stay apart from the
    // server's and out of ~/.config.
    const QString config = QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/wildos/openrgb-client");
    QDir().mkpath(config);

    m_running = new QProcess(this);
    m_running->setProgram(openrgb);
    m_running->setArguments(QStringList{QStringLiteral("--nodetect"), QStringLiteral("--config"), config} + arguments);
    m_running->setProcessChannelMode(QProcess::SeparateChannels);
    QProcess *process = m_running;
    auto timeout = new QTimer(process);
    timeout->setSingleShot(true);
    connect(timeout, &QTimer::timeout, process, &QProcess::kill);
    connect(process, &QProcess::finished, this, [this, process, done] {
        const QString out = QString::fromUtf8(process->readAllStandardOutput());
        process->deleteLater();
        m_running = nullptr;
        done(out);
    });
    connect(process, &QProcess::errorOccurred, this, [this, process](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            process->deleteLater();
            m_running = nullptr;
        }
    });
    timeout->start(30000);
    process->start();
}

void Lighting::apply()
{
    if (!m_serverReady) {
        return;
    }
    // From the first choice made in Cooler the lights are set by Lights,
    // and the openrgb command is left out of it.
    if (m_lights.inCharge()) {
        return;
    }
    if (m_running) {
        m_applyAgain = true;
        return;
    }
    m_applyAgain = false;

    const Plan::Look look = Plan::look(m_colour, m_effect, m_onBattery, m_reducedMotion);
    const QColor colour = Plan::colourFor(m_colour, m_custom, Plan::parseAccent(m_accent));
    run({QStringLiteral("--list-detailed")}, [this, look, colour](const QString &listing) {
        const QStringList args = Plan::arguments(Plan::parseDevices(listing), look, colour);
        const auto finish = [this](const QString &) {
            if (m_applyAgain) {
                apply();
            }
        };
        if (args.isEmpty()) {
            finish(QString());
            return;
        }
        run(args, finish);
    });
}
