#pragma once

#include <QDBusConnection>
#include <QList>
#include <QMap>
#include <QObject>
#include <QString>
#include <QVariantMap>

#include <functional>

class QDBusPendingCall;

// wildos-coolerd from the other side of the bus, for the Cooler window and
// for the program that draws the faces. It keeps the service's last answers
// and asks again whenever the service says something changed. Every call is
// asynchronous: nothing here waits on the bus or on the cooler.
//
// The service is on the system bus. WILDOS_COOLER_BUS=session looks on the
// session bus instead, where tests run it with a pretend cooler.
namespace Cooler
{
struct SpeedPoint {
    int temperature = 0;
    int percent = 0;

    bool operator==(const SpeedPoint &) const = default;
};
using SpeedCurve = QList<SpeedPoint>;

// What Describe() says.
struct Device {
    bool present = false;
    QString model;
    QString firmware;
    int screen = 0;
    int brightness = 0;
    int rotation = 0;
    bool canShowFaces = false;
    bool fansOnCooler = false;
    bool fullSpeed = false;
    // By channel, "pump" and "fan": the curves the service keeps and puts
    // back at start-up. Empty until one has been set.
    QMap<QString, SpeedCurve> curves;

    bool operator==(const Device &) const = default;
};

// What Readings() says; valid is false until the cooler has been read.
struct CoolerReadings {
    bool valid = false;
    double liquid = 0;
    int pumpRpm = 0;
    int pumpPercent = 0;
    int fanRpm = 0;
    int fanPercent = 0;

    bool operator==(const CoolerReadings &) const = default;
};

class Client : public QObject
{
    Q_OBJECT

public:
    explicit Client(QObject *parent = nullptr);

    // The service has answered at least once, or failed to.
    bool ready() const { return m_ready; }
    const Device &device() const { return m_device; }
    const CoolerReadings &readings() const { return m_readings; }
    // The answers as the service gave them, for a problem report.
    QVariantMap rawDevice() const { return m_rawDevice; }
    QVariantMap rawReadings() const { return m_rawReadings; }

    // The service says "changed" once a second while it has a cooler. A
    // program with nothing to show can stop listening, and so stop waking;
    // listening again asks for what was missed.
    void follow(bool follow);

    void lookAgain();
    void setBrightness(int percent);
    void setRotation(int degrees);
    // channel is "pump" or "fan".
    void setCurve(const QString &channel, const SpeedCurve &curve);
    void setFullSpeed(bool full);
    // Hands over the socket frames are written into, or -1 when the service
    // would not give one. The caller owns it and closes it.
    void openFrames(QObject *context, std::function<void(int)> done);

Q_SIGNALS:
    void deviceChanged();
    void readingsChanged();

private:
    void refresh();
    void call(const QString &method, const QVariantList &arguments = {});
    void gone();

    QDBusConnection m_bus;
    bool m_ready = false;
    bool m_following = false;
    Device m_device;
    CoolerReadings m_readings;
    QVariantMap m_rawDevice;
    QVariantMap m_rawReadings;

private Q_SLOTS:
    void changed();
};
}

Q_DECLARE_METATYPE(Cooler::SpeedPoint)
