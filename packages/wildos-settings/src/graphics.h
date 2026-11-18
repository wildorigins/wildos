#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QString>

class QProcess;

// Settings > System > Display > Graphics driver: an NVIDIA card can use the
// open driver Wild OS ships (nouveau) or NVIDIA's own from Debian.
// settings-graphics-driver does the switching as root and reports its steps;
// with Secure Boot on it also queues this PC's module key and prints the code
// the firmware asks for at the next restart.
class Graphics : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool hasNvidia READ hasNvidia CONSTANT)
    // The card's name, as lspci knows it.
    Q_PROPERTY(QString nvidiaName READ nvidiaName CONSTANT)
    // The kernel driver bound to the card now: nouveau, nvidia, or empty.
    Q_PROPERTY(QString driverInUse READ driverInUse NOTIFY changed)
    Q_PROPERTY(bool nvidiaInstalled READ nvidiaInstalled NOTIFY changed)
    Q_PROPERTY(bool secureBoot READ secureBoot CONSTANT)
    // What is installed and what is running differ until the next start-up.
    Q_PROPERTY(bool restartNeeded READ restartNeeded NOTIFY changed)
    Q_PROPERTY(bool busy READ busy NOTIFY changed)
    // The step the helper is on while busy.
    Q_PROPERTY(QString step READ step NOTIFY changed)
    // The code to type into the firmware's key enrolment screen, once queued.
    Q_PROPERTY(QString enrolCode READ enrolCode NOTIFY changed)
    // Why the last switch failed, in plain words, or empty.
    Q_PROPERTY(QString error READ error NOTIFY changed)
    // The desktop never appeared on the first start-up with NVIDIA's driver,
    // so graphics-driver-trial put the open one back.
    Q_PROPERTY(bool trialFailed READ trialFailed NOTIFY changed)

public:
    explicit Graphics(QObject *parent = nullptr);

    bool hasNvidia() const { return !m_device.isEmpty(); }
    QString nvidiaName() const { return m_name; }
    QString driverInUse() const;
    bool nvidiaInstalled() const;
    bool secureBoot() const;
    bool restartNeeded() const;
    bool busy() const { return m_process != nullptr; }
    QString step() const { return m_step; }
    QString enrolCode() const { return m_code; }
    QString error() const { return m_error; }
    bool trialFailed() const;

    // "nvidia" or "open".
    Q_INVOKABLE void useDriver(const QString &driver);
    // Restart to finish a switch.
    Q_INVOKABLE void restart();

Q_SIGNALS:
    void changed();

private:
    QString m_device; // /sys/bus/pci/devices/<address>
    QString m_name;
    QProcess *m_process = nullptr;
    QString m_step;
    QString m_code;
    QString m_error;
};
