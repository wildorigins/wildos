#include "thunderbolt.h"

#include <QDBusArgument>
#include <QDBusConnection>
#include <QDBusError>
#include <QDBusMessage>
#include <QDBusObjectPath>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QDBusVariant>

namespace
{
const QString service = QStringLiteral("org.freedesktop.bolt");
const QString managerPath = QStringLiteral("/org/freedesktop/bolt");
const QString managerInterface = QStringLiteral("org.freedesktop.bolt1.Manager");
const QString deviceInterface = QStringLiteral("org.freedesktop.bolt1.Device");
const QString properties = QStringLiteral("org.freedesktop.DBus.Properties");

QVariantMap allProperties(const QString &path, const QString &interface)
{
    auto message = QDBusMessage::createMethodCall(service, path, properties, QStringLiteral("GetAll"));
    message << interface;
    const QDBusMessage reply = QDBusConnection::systemBus().call(message, QDBus::Block, 3000);
    if (reply.type() != QDBusMessage::ReplyMessage || reply.arguments().isEmpty()) {
        return {};
    }
    QVariantMap map;
    const QDBusArgument arg = reply.arguments().constFirst().value<QDBusArgument>();
    arg >> map;
    for (auto it = map.begin(); it != map.end(); ++it) {
        if (it.value().canConvert<QDBusVariant>()) {
            it.value() = it.value().value<QDBusVariant>().variant();
        }
    }
    return map;
}
}

Thunderbolt::Thunderbolt(QObject *parent)
    : QObject(parent)
{
    QDBusConnection bus = QDBusConnection::systemBus();
    bus.connect(service, managerPath, managerInterface, QStringLiteral("DeviceAdded"), this, SLOT(onChanged()));
    bus.connect(service, managerPath, managerInterface, QStringLiteral("DeviceRemoved"), this, SLOT(onChanged()));
    // A device's status or stored flag changing, on whichever device path.
    bus.connect(service, QString(), properties, QStringLiteral("PropertiesChanged"), this, SLOT(onChanged()));
    reload();
}

void Thunderbolt::onChanged()
{
    reload();
}

void Thunderbolt::reload()
{
    const QVariantMap manager = allProperties(managerPath, managerInterface);
    m_available = !manager.isEmpty();
    const QString security = manager.value(QStringLiteral("SecurityLevel")).toString();
    m_firmwareOff = security == QLatin1String("dponly") || security == QLatin1String("usbonly");
    m_hasPorts = false;
    m_devices.clear();

    if (m_available) {
        auto message = QDBusMessage::createMethodCall(service, managerPath, managerInterface, QStringLiteral("ListDevices"));
        const QDBusMessage reply = QDBusConnection::systemBus().call(message, QDBus::Block, 3000);
        QList<QDBusObjectPath> paths;
        if (reply.type() == QDBusMessage::ReplyMessage && !reply.arguments().isEmpty()) {
            reply.arguments().constFirst().value<QDBusArgument>() >> paths;
        }
        for (const QDBusObjectPath &path : std::as_const(paths)) {
            const QVariantMap device = allProperties(path.path(), deviceInterface);
            if (device.isEmpty()) {
                continue;
            }
            // Each Thunderbolt or USB4 controller in the PC is listed as a
            // "host" device, so a PC with none has no such ports.
            if (device.value(QStringLiteral("Type")).toString() == QLatin1String("host")) {
                m_hasPorts = true;
                continue;
            }
            const QString label = device.value(QStringLiteral("Label")).toString();
            m_devices.append(QVariantMap{
                {QStringLiteral("uid"), device.value(QStringLiteral("Uid"))},
                {QStringLiteral("name"), label.isEmpty() ? device.value(QStringLiteral("Name")) : QVariant(label)},
                {QStringLiteral("vendor"), device.value(QStringLiteral("Vendor"))},
                {QStringLiteral("status"), device.value(QStringLiteral("Status"))},
                {QStringLiteral("stored"), device.value(QStringLiteral("Stored"))},
            });
        }
        // A remembered device is listed even when the controller is not.
        m_hasPorts = m_hasPorts || !m_devices.isEmpty();
    }
    Q_EMIT changed();
}

void Thunderbolt::setBusy(const QString &uid)
{
    if (m_busy != uid) {
        m_busy = uid;
        Q_EMIT busyChanged();
    }
}

void Thunderbolt::call(const QString &uid, const QString &method, const QVariantList &arguments, const QString &failure)
{
    if (!m_busy.isEmpty()) {
        return;
    }
    setBusy(uid);
    auto message = QDBusMessage::createMethodCall(service, managerPath, managerInterface, method);
    message.setArguments(arguments);
    // boltd checks with polkit, which may need to ask for a password.
    message.setInteractiveAuthorizationAllowed(true);
    auto watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 120000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher, failure] {
        watcher->deleteLater();
        const QDBusPendingReply<> reply = *watcher;
        setBusy(QString());
        if (reply.isError()) {
            const QDBusError error = reply.error();
            Q_EMIT failed(failure, error.type() == QDBusError::AccessDenied ? tr("Only an administrator can do this.") : error.message());
        }
        reload();
    });
}

void Thunderbolt::allow(const QString &uid)
{
    // The policy and flags KDE's module enrols with: let the device in
    // again at start-up too, without a key challenge, which most devices
    // cannot answer.
    call(uid, QStringLiteral("EnrollDevice"), {uid, QStringLiteral("default"), QStringLiteral("boot | nokey")}, tr("The device was not allowed"));
}

void Thunderbolt::forget(const QString &uid)
{
    call(uid, QStringLiteral("ForgetDevice"), {uid}, tr("The device was not forgotten"));
}
