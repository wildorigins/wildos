#include "windowsusb.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QDBusPendingCallWatcher>
#include <QDBusPendingReply>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcess>
#include <QStandardPaths>

namespace
{
const QString service = QStringLiteral("org.wildos.WindowsMode1");
const QString path = QStringLiteral("/org/wildos/WindowsMode1");
const QString host = QStringLiteral("org.wildos.WindowsMode1.Host");
const QString allowHelper = QStringLiteral("/usr/libexec/wildos/usb-devices");
}

WindowsUsb::WindowsUsb(QObject *parent)
    : QObject(parent)
{
    m_installed = !QStandardPaths::findExecutable(QStringLiteral("wildos-winmode")).isEmpty();
    if (m_installed) {
        QDBusConnection::systemBus().connect(service, path, host, QStringLiteral("UsbChanged"), this, SLOT(reload()));
    }
}

bool WindowsUsb::canAllow() const
{
    return QFile::exists(allowHelper);
}

void WindowsUsb::reload()
{
    if (!m_installed) {
        return;
    }
    const QDBusMessage message = QDBusMessage::createMethodCall(service, path, host, QStringLiteral("UsbDevices"));
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 30000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        if (reply.isError()) {
            return;
        }
        const QJsonObject list = QJsonDocument::fromJson(reply.value().toUtf8()).object();
        m_devices = list.value(QStringLiteral("devices")).toArray().toVariantList();
        m_running = list.value(QStringLiteral("running")).toBool();
        m_loaded = true;
        Q_EMIT changed();
    });
}

void WindowsUsb::setUse(const QString &port, bool use)
{
    if (!m_installed || !m_busy.isEmpty()) {
        return;
    }
    setBusy(port);
    // Giving a device to Windows can wait on the administrator password.
    const QDBusMessage message = QDBusMessage::createMethodCall(service, path, host, QStringLiteral("SetUsbDevice")) << port << use;
    auto *watcher = new QDBusPendingCallWatcher(QDBusConnection::systemBus().asyncCall(message, 300000), this);
    connect(watcher, &QDBusPendingCallWatcher::finished, this, [this, watcher] {
        watcher->deleteLater();
        const QDBusPendingReply<QString> reply = *watcher;
        setBusy({});
        const QString answer = reply.isError() ? tr("Windows mode didn't answer. Restart the PC, then try again.") : reply.value();
        if (!answer.isEmpty()) {
            Q_EMIT failed(answer);
        }
        // Puts a switch the answer turned down back where it was.
        reload();
    });
}

void WindowsUsb::allow(const QString &port)
{
    if (!canAllow() || !m_busy.isEmpty()) {
        return;
    }
    setBusy(port);
    auto *process = new QProcess(this);
    connect(process, &QProcess::finished, this, [this, process] {
        process->deleteLater();
        setBusy({});
        const QString result = QString::fromUtf8(process->readAllStandardOutput()).trimmed();
        if (result == QLatin1String("denied")) {
            Q_EMIT failed(tr("Only an administrator can allow USB devices."));
        } else if (result != QLatin1String("allowed") && result != QLatin1String("cancelled")) {
            Q_EMIT failed(tr("This device couldn't be allowed. Unplug it and try again."));
        }
        reload();
    });
    process->start(allowHelper, {QStringLiteral("allow"), port});
}

void WindowsUsb::setBusy(const QString &port)
{
    if (m_busy != port) {
        m_busy = port;
        Q_EMIT busyChanged();
    }
}
