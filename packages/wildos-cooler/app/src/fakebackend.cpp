#include "fakebackend.h"

#include "curves.h"

namespace Cooler
{
FakeBackend::FakeBackend(const QString &kind, QObject *parent)
    : Backend(parent)
{
    if (kind == QLatin1String("kraken")) {
        m_present = true;
        m_model = QStringLiteral("NZXT Kraken");
        m_screenSize = 640;
        m_firmware = QStringLiteral("2.1.0");
        m_fanCount = 3;
        m_lighting = true;
    } else if (kind == QLatin1String("krakenz")) {
        m_present = true;
        m_model = QStringLiteral("NZXT Kraken Z");
        m_screenSize = 320;
        m_firmware = QStringLiteral("6.0.2");
    }
    if (m_present) {
        m_readings.cpuTemperature = 54;
        m_readings.cpuHighestToday = 78;
        m_readings.liquidTemperature = 34;
        m_readings.roomTemperature = 21;
        m_readings.pumpRpm = 2100;
        m_readings.pumpPercent = 70;
        m_readings.fanRpm = m_fanCount > 0 ? 840 : 0;
        m_readings.fanPercent = m_fanCount > 0 ? 38 : 0;
        m_readings.cpuLoad = 23;
        m_readings.gpuTemperature = 61;
        // The boards' own bars and lines (10.8).
        m_readings.coreLoads = {62, 18, 41, 9, 27, 73, 12, 33, 8, 22, 5, 14, 11, 6, 19, 8};
        m_readings.cpuHistory = {49, 50, 50, 52, 51, 53, 56, 60, 63, 62, 58, 55, 54, 52, 51, 52, 55, 57, 56, 54, 53, 54, 56, 58, 57, 55, 54, 54};
        m_readings.gpuHistory = {58, 58, 59, 59, 60, 61, 61, 62, 64, 66, 67, 66, 65, 63, 62, 62, 61, 61, 62, 63, 63, 62, 61, 61, 62, 62, 61, 61};
    }
    defaults();
}

void FakeBackend::defaults()
{
    m_face = QStringLiteral("liquid");
    m_brightness = 70;
    m_rotation = 0;
    m_showMark = true;
    m_themeColours = true;
    m_dimWhenIdle = true;
    m_warnLiquidAbove = 50;
    m_warnCpuLimitSeconds = 30;
    m_mode = QStringLiteral("balanced");
    m_customFans = builtInCurve(m_mode, Channel::Fans);
    m_customPump = builtInCurve(m_mode, Channel::Pump);
    m_stopFansWhenCool = false;
    m_pumpMinimum = 60;
    m_fullSpeedInGames = true;
}

Curve FakeBackend::builtInCurve(const QString &mode, Channel channel)
{
    return Cooler::builtInCurve(mode, channel);
}

Curve FakeBackend::curve(const QString &mode, Channel channel) const
{
    if (mode == QLatin1String("custom")) {
        return channel == Channel::Fans ? m_customFans : m_customPump;
    }
    return builtInCurve(mode, channel);
}

void FakeBackend::setCustomCurve(Channel channel, const Curve &curve)
{
    (channel == Channel::Fans ? m_customFans : m_customPump) = curve;
}

void FakeBackend::putEverythingBack()
{
    defaults();
    Q_EMIT settingsChanged();
}

QString FakeBackend::report() const
{
    return m_present ? QStringLiteral("Example cooler (WILDOS_COOLER_FAKE): %1, firmware %2\n").arg(m_model, m_firmware)
                     : QStringLiteral("No cooler (WILDOS_COOLER_FAKE=none)\n");
}
}
