#include "servicebackend.h"

#include "curves.h"
#include "settings.h"

#include <QTextStream>

namespace Cooler
{
namespace
{
SpeedCurve forService(const Curve &curve)
{
    SpeedCurve points;
    points.reserve(curve.size());
    for (const CurvePoint &point : curve) {
        points << SpeedPoint{point.temperature, point.percent};
    }
    return points;
}
}

ServiceBackend::ServiceBackend(QObject *parent)
    : Backend(parent)
{
    defaults();
    m_sendSoon.setSingleShot(true);
    m_sendSoon.setInterval(150);
    connect(&m_sendSoon, &QTimer::timeout, this, &ServiceBackend::send);

    connect(&m_client, &Client::deviceChanged, this, &ServiceBackend::coolerChanged);
    connect(&m_client, &Client::readingsChanged, this, &Backend::readingsChanged);
    connect(&m_sensors, &Sensors::changed, this, &Backend::readingsChanged);
    m_sensors.start();
}

void ServiceBackend::defaults()
{
    m_mode = QStringLiteral("balanced");
    m_customFans = builtInCurve(m_mode, Channel::Fans);
    m_customPump = builtInCurve(m_mode, Channel::Pump);
    m_stopFansWhenCool = false;
    m_pumpMinimum = 60;
}

void ServiceBackend::coolerChanged()
{
    const bool present = m_client.device().present;
    if (present != m_wasPresent) {
        m_wasPresent = present;
        Q_EMIT presentChanged();
        if (present) {
            // A cooler that has just been found, or has had its power back,
            // gets what the window shows.
            send();
        }
        return;
    }
    Q_EMIT deviceChanged();
}

QString ServiceBackend::model() const
{
    return present() ? QStringLiteral("NZXT ") + m_client.device().model : QString();
}

Readings ServiceBackend::readings() const
{
    Readings readings;
    readings.cpuTemperature = m_sensors.cpuTemperature();
    readings.coreLoads = m_sensors.coreLoads();
    readings.cpuHistory = m_sensors.cpuHistory();
    readings.gpuHistory = m_sensors.gpuHistory();
    readings.cpuHighestToday = m_sensors.cpuHighestToday();
    readings.cpuLoad = m_sensors.cpuLoad();
    readings.gpuTemperature = m_sensors.gpuTemperature();
    const CoolerReadings &cooler = m_client.readings();
    readings.liquidTemperature = cooler.valid ? cooler.liquid : -1;
    readings.pumpRpm = cooler.pumpRpm;
    readings.pumpPercent = cooler.pumpPercent;
    if (m_client.device().fansOnCooler) {
        readings.fanRpm = cooler.fanRpm;
        readings.fanPercent = cooler.fanPercent;
    }
    return readings;
}

QString ServiceBackend::face() const
{
    return QLatin1String(Settings::defaultFace);
}

int ServiceBackend::brightness() const
{
    return m_brightness >= 0 ? m_brightness : present() ? m_client.device().brightness : Settings::defaultBrightness;
}

void ServiceBackend::setBrightness(int percent)
{
    m_brightness = percent;
    m_sendSoon.start();
}

int ServiceBackend::rotation() const
{
    return m_rotation >= 0 ? m_rotation : m_client.device().rotation;
}

void ServiceBackend::setRotation(int degrees)
{
    m_rotation = degrees;
    m_sendSoon.start();
}

bool ServiceBackend::showMark() const
{
    return Settings::defaultShowMark;
}

bool ServiceBackend::themeColours() const
{
    return Settings::defaultThemeColours;
}

bool ServiceBackend::dimWhenIdle() const
{
    return Settings::defaultDimWhenIdle;
}

int ServiceBackend::warnLiquidAbove() const
{
    return Settings::defaultWarnLiquidAbove;
}

int ServiceBackend::warnCpuLimitSeconds() const
{
    return Settings::defaultWarnCpuLimitSeconds;
}

bool ServiceBackend::fullSpeedInGames() const
{
    return Settings::defaultFullSpeedInGames;
}

void ServiceBackend::setMode(const QString &mode)
{
    m_mode = mode;
    m_cooling = true;
    m_sendSoon.start();
}

Curve ServiceBackend::curve(const QString &mode, Channel channel) const
{
    if (mode == QLatin1String("custom")) {
        return channel == Channel::Fans ? m_customFans : m_customPump;
    }
    return builtInCurve(mode, channel);
}

void ServiceBackend::setCustomCurve(Channel channel, const Curve &curve)
{
    (channel == Channel::Fans ? m_customFans : m_customPump) = curve;
    m_sendSoon.start();
}

void ServiceBackend::setStopFansWhenCool(bool stop)
{
    m_stopFansWhenCool = stop;
    m_sendSoon.start();
}

void ServiceBackend::setPumpMinimum(int percent)
{
    m_pumpMinimum = percent;
    m_sendSoon.start();
}

// Only what differs from what the cooler has goes over the bus, so opening
// the window on a cooler that is already set as shown sends nothing.
void ServiceBackend::send()
{
    m_sendSoon.stop();
    const Device &device = m_client.device();
    if (!device.present) {
        return;
    }
    if (m_brightness >= 0 && m_brightness != device.brightness) {
        m_client.setBrightness(m_brightness);
    }
    if (m_rotation >= 0 && m_rotation != device.rotation) {
        m_client.setRotation(m_rotation);
    }
    if (!m_cooling) {
        return;
    }
    Curve fans = curve(m_mode, Channel::Fans);
    if (m_stopFansWhenCool) {
        fans = stoppedWhenCool(fans);
    }
    const Curve pump = withFloor(curve(m_mode, Channel::Pump), m_pumpMinimum);
    if (device.curves.value(QStringLiteral("fan")) != forService(fans)) {
        m_client.setCurve(QStringLiteral("fan"), forService(fans));
    }
    if (device.curves.value(QStringLiteral("pump")) != forService(pump)) {
        m_client.setCurve(QStringLiteral("pump"), forService(pump));
    }
}

void ServiceBackend::putEverythingBack()
{
    defaults();
    m_brightness = Settings::defaultBrightness;
    m_rotation = 0;
    m_cooling = true;
    send();
    Q_EMIT settingsChanged();
}

QString ServiceBackend::report() const
{
    QString text;
    QTextStream out(&text);
    const auto list = [&out](const QVariantMap &map) {
        for (auto it = map.begin(); it != map.end(); ++it) {
            out << "  " << it.key() << ": " << it.value().toString() << "\n";
        }
    };
    out << "Cooler service (io.wildos.Cooler)\n";
    if (!m_client.ready()) {
        out << "  has not answered yet\n";
    } else if (m_client.rawDevice().isEmpty()) {
        out << "  is not answering\n";
    } else {
        out << "Describe:\n";
        list(m_client.rawDevice());
        out << "Readings:\n";
        list(m_client.rawReadings());
    }
    out << "This PC's sensors:\n";
    out << "  CPU temperature: " << m_sensors.cpuTemperature() << " (slows down at " << m_sensors.cpuLimit() << ")\n";
    out << "  GPU temperature: " << m_sensors.gpuTemperature() << "\n";
    out << "  A negative temperature means no sensor was found.\n";
    return text;
}
}
