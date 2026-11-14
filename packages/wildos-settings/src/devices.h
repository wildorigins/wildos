#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QVariantList>

// Settings > System > Devices, Wild OS's Device Manager. Every device a
// person would recognise (graphics, Wi-Fi, sound, camera, drives, keyboard
// and so on), each with a plain name, what it is really called, the driver
// running it and where it is connected, read from sysfs, udev's hardware
// database and the services that own each kind (NetworkManager, BlueZ).
//
// A device needs attention when it has no driver, runs on a stand-in
// driver where a proper one exists, can't start for lack of firmware, or
// its drive health check fails. The kernel's log and the drives' health
// logs are root's, so settings-devices reads those through
// wildos-devices.socket. Printers and fingerprints come from the Printers
// and Fingerprint objects in QML, which already watch them.
class Devices : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // One map per device, in the order the page lists them:
    //   key, kind, name, icon, line (one line of status), state ("ok",
    //   "attention" or "off"), badge, problem, action ("graphicsdriver",
    //   "update", "report", "backup", "restart", "printers", "allow" or
    //   empty), actionLabel, port (a blocked USB device's),
    //   model, maker, driver, connection, hardwareId, extra ([{label, value}])
    Q_PROPERTY(QVariantList devices READ devices NOTIFY devicesChanged)
    Q_PROPERTY(bool scanning READ scanning NOTIFY scanningChanged)
    // False until the first look has finished.
    Q_PROPERTY(bool ready READ ready NOTIFY devicesChanged)
    // Drive health could not be read (settings-devices didn't answer).
    Q_PROPERTY(bool healthUnknown READ healthUnknown NOTIFY devicesChanged)

public:
    explicit Devices(QObject *parent = nullptr);

    QVariantList devices() const { return m_devices; }
    bool scanning() const { return m_scanning; }
    bool ready() const { return m_ready; }
    bool healthUnknown() const { return m_healthUnknown; }

    Q_INVOKABLE void scan();
    // Lets a device USB protection blocked work until it is unplugged, as
    // the shield's "Allow once" does.
    Q_INVOKABLE void allow(const QString &port);
    Q_INVOKABLE bool openReport() const;
    Q_INVOKABLE bool openBackup() const;

Q_SIGNALS:
    void devicesChanged();
    void scanningChanged();
    void failed(const QString &message);

private:
    QVariantList m_devices;
    bool m_scanning = false;
    bool m_ready = false;
    bool m_healthUnknown = false;
    bool m_again = false;
};
