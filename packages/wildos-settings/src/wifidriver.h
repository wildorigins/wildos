#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

class QProcess;

// Settings > Network and internet: Broadcom's own Wi-Fi driver for the
// Broadcom chips the open drivers don't cover, which otherwise leave a PC
// with no Wi-Fi. settings-wifi-driver installs it as root and reports its
// steps; with Secure Boot on it also queues this PC's module key and prints
// the code the firmware asks for at the next restart.
class WifiDriver : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    // A Broadcom Wi-Fi chip with no driver, on a PC with no working Wi-Fi.
    Q_PROPERTY(bool needed READ needed NOTIFY changed)
    // The chip's name, as lspci knows it.
    Q_PROPERTY(QString chipName READ chipName CONSTANT)
    Q_PROPERTY(bool installed READ installed NOTIFY changed)
    // Installed but not running yet: Secure Boot's key waits for a restart.
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    Q_PROPERTY(QString step READ step NOTIFY changed)
    Q_PROPERTY(QString enrolCode READ enrolCode NOTIFY changed)
    Q_PROPERTY(QString error READ error NOTIFY changed)

public:
    explicit WifiDriver(QObject *parent = nullptr);
    ~WifiDriver() override;

    bool needed() const;
    QString chipName() const { return m_name; }
    bool installed() const;
    bool restartNeeded() const;
    bool busy() const { return m_process != nullptr; }
    QString step() const { return m_step; }
    QString enrolCode() const { return m_code; }
    QString error() const { return m_error; }

    Q_INVOKABLE void install();
    Q_INVOKABLE void restart();

Q_SIGNALS:
    void changed();

private:
    QString driverInUse() const;

    QString m_device; // /sys/bus/pci/devices/<address>
    QString m_name;
    QProcess *m_process = nullptr;
    QString m_step;
    QString m_code;
    QString m_error;
};
