#include "lighting.h"

#include <QColor>
#include <QDBusConnection>
#include <QDBusConnectionInterface>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

namespace
{
const QString service = QStringLiteral("io.wildos.Lighting");
const QString path = QStringLiteral("/io/wildos/Lighting");
const QString interface = QStringLiteral("io.wildos.Lighting1");
}

Lighting::Lighting(QObject *parent)
    : QObject(parent)
{
    // Ten times a second is as smooth as a row of small lights needs.
    m_poll.setInterval(100);
    connect(&m_poll, &QTimer::timeout, this, &Lighting::fetchColours);
    m_send.setSingleShot(true);
    m_send.setInterval(80);
    connect(&m_send, &QTimer::timeout, this, [this] {
        const auto pending = m_pending;
        m_pending.clear();
        for (const auto &[method, arguments] : pending) {
            call(method, arguments);
        }
    });

    QDBusConnection bus = QDBusConnection::sessionBus();
    auto *watcher = new QDBusServiceWatcher(service, bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &Lighting::refresh);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, &Lighting::setMissing);
    bus.connect(service, path, interface, QStringLiteral("Changed"), this, SLOT(refresh()));
    if (bus.interface() && bus.interface()->isServiceRegistered(service)) {
        refresh();
    } else {
        // Straight after an update the copy of wildos-lighting still
        // running may be one from before it could be asked anything.
        // Starting its unit again brings up the new one; where OpenRGB is
        // not installed the unit declines to start and nothing changes.
        QDBusMessage restart = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.systemd1"), QStringLiteral("/org/freedesktop/systemd1"),
                                                              QStringLiteral("org.freedesktop.systemd1.Manager"), QStringLiteral("RestartUnit"));
        restart << QStringLiteral("wildos-lighting.service") << QStringLiteral("replace");
        bus.asyncCall(restart, 3000);
    }
}

void Lighting::setWatching(bool watching)
{
    if (watching == m_watching) {
        return;
    }
    m_watching = watching;
    Q_EMIT watchingChanged();
    updatePolling();
    if (watching) {
        refresh();
    }
}

void Lighting::updatePolling()
{
    const bool poll = m_watching && m_moving && m_state == QLatin1String("ready");
    if (poll != m_poll.isActive()) {
        poll ? m_poll.start() : m_poll.stop();
    }
}

void Lighting::setMissing()
{
    m_state = QStringLiteral("missing");
    m_parts.clear();
    m_colours.clear();
    m_moving = false;
    updatePolling();
    Q_EMIT changed();
    Q_EMIT coloursChanged();
}

void Lighting::refresh()
{
    const QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("Describe"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            setMissing();
            return;
        }
        const QJsonObject all = QJsonDocument::fromJson(reply.value().toUtf8()).object();
        m_state = all.value(QLatin1String("state")).toString(QStringLiteral("missing"));
        m_everything = all.value(QLatin1String("everything")).toObject().toVariantMap();
        m_moving = all.value(QLatin1String("moving")).toBool();
        QVariantList parts;
        for (const QJsonValue &value : all.value(QLatin1String("parts")).toArray()) {
            QVariantMap part = value.toObject().toVariantMap();
            int lights = 0;
            for (const QVariant &zone : part.value(QStringLiteral("zones")).toList()) {
                lights += zone.toMap().value(QStringLiteral("lights")).toInt();
            }
            part.insert(QStringLiteral("lights"), lights);
            parts.append(part);
        }
        m_parts = parts;
        updatePolling();
        Q_EMIT changed();
        fetchColours();
    });
}

void Lighting::fetchColours()
{
    if (m_asking) {
        return;
    }
    m_asking = true;
    const QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("Colours"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::sessionBus().asyncCall(message, 3000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        m_asking = false;
        const QDBusPendingReply<QByteArray> reply = *watcher;
        if (reply.isError()) {
            return;
        }
        const QByteArray raw = reply.value();
        qsizetype expected = 0;
        for (const QVariant &part : std::as_const(m_parts)) {
            expected += 3 * part.toMap().value(QStringLiteral("lights")).toInt();
        }
        // The parts changed between the two answers: the next one fits.
        if (raw.size() != expected) {
            return;
        }
        QVariantList colours;
        qsizetype at = 0;
        for (const QVariant &part : std::as_const(m_parts)) {
            QVariantList zones;
            for (const QVariant &zone : part.toMap().value(QStringLiteral("zones")).toList()) {
                QVariantList lights;
                const int count = zone.toMap().value(QStringLiteral("lights")).toInt();
                for (int i = 0; i < count; ++i, at += 3) {
                    lights.append(QColor(uchar(raw[at]), uchar(raw[at + 1]), uchar(raw[at + 2])));
                }
                zones.append(QVariant(lights));
            }
            colours.append(QVariant(zones));
        }
        m_colours = colours;
        Q_EMIT coloursChanged();
    });
}

void Lighting::call(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    QDBusConnection::sessionBus().asyncCall(message, 3000);
}

void Lighting::later(const QString &method, const QVariantList &arguments)
{
    const bool forPart = method == QLatin1String("SetPart");
    for (auto &pending : m_pending) {
        // The same slider, moved again: only where it is now matters.
        if (pending.first == method && (!forPart || pending.second.value(0) == arguments.value(0))) {
            pending.second = arguments;
            return;
        }
    }
    m_pending.append({method, arguments});
    if (!m_send.isActive()) {
        m_send.start();
    }
}

void Lighting::turnOn()
{
    call(QStringLiteral("SetOn"), {true});
}

void Lighting::setEverything(const QString &effect, int brightness, int speed)
{
    later(QStringLiteral("SetEverything"), {effect, brightness, speed});
}

void Lighting::setOffWhenLocked(bool off)
{
    call(QStringLiteral("SetOffWhenLocked"), {off});
}

void Lighting::setPartOwn(const QString &part, bool own)
{
    call(QStringLiteral("SetPartOwn"), {part, own});
}

void Lighting::setPart(const QString &part, const QString &effect, int brightness, int speed)
{
    later(QStringLiteral("SetPart"), {part, effect, brightness, speed});
}

void Lighting::setZoneName(const QString &part, int zone, const QString &name)
{
    call(QStringLiteral("SetZoneName"), {part, zone, name});
}

void Lighting::setZoneShape(const QString &part, int zone, const QString &shape)
{
    call(QStringLiteral("SetZoneShape"), {part, zone, shape});
}

void Lighting::setZoneCount(const QString &part, int zone, int count)
{
    call(QStringLiteral("SetZoneCount"), {part, zone, count});
}

void Lighting::setZoneSplit(const QString &part, int zone, int sections)
{
    call(QStringLiteral("SetZoneSplit"), {part, zone, sections});
}

void Lighting::flash(const QString &part, int zone)
{
    call(QStringLiteral("Flash"), {part, zone});
}
