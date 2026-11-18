#include "coolerclient.h"

#include <QDBusArgument>
#include <QDBusMessage>
#include <QDBusMetaType>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusServiceWatcher>
#include <QDBusUnixFileDescriptor>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLoggingCategory>

#include <unistd.h>

Q_LOGGING_CATEGORY(coolerClient, "wildos.cooler.client", QtInfoMsg)

namespace Cooler
{
namespace
{
const QString service = QStringLiteral("io.wildos.Cooler");
const QString path = QStringLiteral("/io/wildos/Cooler");
const QString interface = QStringLiteral("io.wildos.Cooler1");

QDBusConnection chosenBus()
{
    return qEnvironmentVariable("WILDOS_COOLER_BUS") == QLatin1String("session") ? QDBusConnection::sessionBus()
                                                                                    : QDBusConnection::systemBus();
}

QMap<QString, SpeedCurve> curvesFromJson(const QString &text)
{
    QMap<QString, SpeedCurve> curves;
    const QJsonObject all = QJsonDocument::fromJson(text.toUtf8()).object();
    for (auto it = all.begin(); it != all.end(); ++it) {
        SpeedCurve curve;
        const QJsonArray points = it.value().toArray();
        for (const QJsonValue &value : points) {
            const QJsonArray point = value.toArray();
            curve << SpeedPoint{point.at(0).toInt(), point.at(1).toInt()};
        }
        curves.insert(it.key(), curve);
    }
    return curves;
}
}

QDBusArgument &operator<<(QDBusArgument &argument, const SpeedPoint &point)
{
    argument.beginStructure();
    argument << point.temperature << point.percent;
    argument.endStructure();
    return argument;
}

const QDBusArgument &operator>>(const QDBusArgument &argument, SpeedPoint &point)
{
    argument.beginStructure();
    argument >> point.temperature >> point.percent;
    argument.endStructure();
    return argument;
}

Client::Client(QObject *parent)
    : QObject(parent)
    , m_bus(chosenBus())
{
    qDBusRegisterMetaType<SpeedPoint>();
    qDBusRegisterMetaType<SpeedCurve>();

    auto *watcher = new QDBusServiceWatcher(service, m_bus, QDBusServiceWatcher::WatchForOwnerChange, this);
    connect(watcher, &QDBusServiceWatcher::serviceUnregistered, this, &Client::gone);
    connect(watcher, &QDBusServiceWatcher::serviceRegistered, this, &Client::refresh);
    follow(true);
}

void Client::follow(bool follow)
{
    if (follow == m_following) {
        return;
    }
    m_following = follow;
    if (follow) {
        m_bus.connect(service, path, interface, QStringLiteral("Changed"), this, SLOT(changed()));
        refresh();
    } else {
        m_bus.disconnect(service, path, interface, QStringLiteral("Changed"), this, SLOT(changed()));
    }
}

void Client::changed()
{
    refresh();
}

void Client::gone()
{
    m_ready = true;
    m_rawDevice.clear();
    m_rawReadings.clear();
    if (m_device != Device{}) {
        m_device = {};
        Q_EMIT deviceChanged();
    }
    if (m_readings != CoolerReadings{}) {
        m_readings = {};
        Q_EMIT readingsChanged();
    }
}

void Client::refresh()
{
    auto *describe = new QDBusPendingCallWatcher(m_bus.asyncCall(QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("Describe"))), this);
    connect(describe, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        if (reply.isError()) {
            qCInfo(coolerClient, "the cooler service did not answer: %s", qPrintable(reply.error().message()));
            gone();
            return;
        }
        m_rawDevice = reply.value();
        Device device;
        device.present = m_rawDevice.value(QStringLiteral("present")).toBool();
        device.model = m_rawDevice.value(QStringLiteral("model")).toString();
        device.firmware = m_rawDevice.value(QStringLiteral("firmware")).toString();
        device.screen = m_rawDevice.value(QStringLiteral("screen")).toInt();
        device.brightness = m_rawDevice.value(QStringLiteral("brightness")).toInt();
        device.rotation = m_rawDevice.value(QStringLiteral("rotation")).toInt();
        device.canShowFaces = m_rawDevice.value(QStringLiteral("canShowFaces")).toBool();
        device.fansOnCooler = m_rawDevice.value(QStringLiteral("fansOnCooler")).toBool();
        device.fullSpeed = m_rawDevice.value(QStringLiteral("fullSpeed")).toBool();
        device.curves = curvesFromJson(m_rawDevice.value(QStringLiteral("curves")).toString());
        const bool first = !m_ready;
        m_ready = true;
        if (first || device != m_device) {
            m_device = device;
            Q_EMIT deviceChanged();
        }
    });

    auto *read = new QDBusPendingCallWatcher(m_bus.asyncCall(QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("Readings"))), this);
    connect(read, &QDBusPendingCallWatcher::finished, this, [this](QDBusPendingCallWatcher *watcher) {
        watcher->deleteLater();
        const QDBusPendingReply<QVariantMap> reply = *watcher;
        if (reply.isError()) {
            return;
        }
        m_rawReadings = reply.value();
        CoolerReadings readings;
        readings.valid = m_rawReadings.contains(QStringLiteral("liquid"));
        readings.liquid = m_rawReadings.value(QStringLiteral("liquid")).toDouble();
        readings.pumpRpm = m_rawReadings.value(QStringLiteral("pumpRpm")).toInt();
        readings.pumpPercent = m_rawReadings.value(QStringLiteral("pumpDuty")).toInt();
        readings.fanRpm = m_rawReadings.value(QStringLiteral("fanRpm")).toInt();
        readings.fanPercent = m_rawReadings.value(QStringLiteral("fanDuty")).toInt();
        if (readings != m_readings) {
            m_readings = readings;
            Q_EMIT readingsChanged();
        }
    });
}

// A change the service refuses (no cooler, not the person at the screen) is
// logged and otherwise dropped: the next Changed brings what really holds.
void Client::call(const QString &method, const QVariantList &arguments)
{
    QDBusMessage message = QDBusMessage::createMethodCall(service, path, interface, method);
    message.setArguments(arguments);
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(message), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [method](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        if (finished->isError()) {
            qCWarning(coolerClient, "%s: %s", qPrintable(method), qPrintable(finished->error().message()));
        }
    });
}

void Client::lookAgain()
{
    call(QStringLiteral("LookAgain"));
}

void Client::setBrightness(int percent)
{
    call(QStringLiteral("SetBrightness"), {percent});
}

void Client::setRotation(int degrees)
{
    call(QStringLiteral("SetRotation"), {degrees});
}

void Client::setCurve(const QString &channel, const SpeedCurve &curve)
{
    call(QStringLiteral("SetCurve"), {channel, QVariant::fromValue(curve)});
}

void Client::setFullSpeed(bool full)
{
    call(QStringLiteral("SetFullSpeed"), {full});
}

void Client::openFrames(QObject *context, std::function<void(int)> done)
{
    auto *watcher = new QDBusPendingCallWatcher(m_bus.asyncCall(QDBusMessage::createMethodCall(service, path, interface, QStringLiteral("OpenFrames"))), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, context, [done](QDBusPendingCallWatcher *finished) {
        finished->deleteLater();
        const QDBusPendingReply<QDBusUnixFileDescriptor> reply = *finished;
        if (reply.isError() || !reply.value().isValid()) {
            qCWarning(coolerClient, "OpenFrames: %s", qPrintable(reply.error().message()));
            done(-1);
            return;
        }
        done(::dup(reply.value().fileDescriptor()));
    });
}
}
