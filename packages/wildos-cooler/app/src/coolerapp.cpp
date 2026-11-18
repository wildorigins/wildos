#include "coolerapp.h"

#include "settings.h"

#include <KConfig>
#include <KConfigGroup>
#include <KWindowSystem>

#include <QCoreApplication>
#include <QDateTime>
#include <QFile>
#include <QQmlComponent>
#include <QStandardPaths>
#include <QTextStream>

#include <algorithm>

namespace Settings = Cooler::Settings;
using Cooler::Channel;
using Cooler::Curve;
using Cooler::CurvePoint;

namespace
{
CoolerApp *s_instance = nullptr;

const QString configName = QLatin1String(Settings::file);
using Settings::coolingGroup;
using Settings::screenGroup;
constexpr int coldest = 20;
constexpr int hottest = 60;

const QStringList faces{
    QStringLiteral("liquid"), QStringLiteral("clock"), QStringLiteral("last-minute"), QStringLiteral("both"),
    QStringLiteral("cores"), QStringLiteral("warp"), QStringLiteral("tunnel"), QStringLiteral("reactor"),
    QStringLiteral("hive"), QStringLiteral("matrix"), QStringLiteral("neon"), QStringLiteral("rgb-ring"),
};
const QStringList temperatures{QStringLiteral("cpu"), QStringLiteral("gpu"), QStringLiteral("both")};

const QStringList modes{
    QStringLiteral("quiet"), QStringLiteral("balanced"), QStringLiteral("performance"), QStringLiteral("custom"),
};

// "20:25,30:30": short enough to read in the file.
QString curveText(const Curve &curve)
{
    QStringList parts;
    for (const CurvePoint &point : curve) {
        parts << QStringLiteral("%1:%2").arg(point.temperature).arg(point.percent);
    }
    return parts.join(QLatin1Char(','));
}

// The ends pinned to the chart's ends, temperatures rising, speeds never
// falling: what a hand-edited file or a careless caller might not give.
Curve tidied(Curve curve)
{
    if (curve.size() < 2 || curve.size() > hottest - coldest) {
        return {};
    }
    curve.first().temperature = coldest;
    curve.last().temperature = hottest;
    for (qsizetype i = 0; i < curve.size(); ++i) {
        CurvePoint &point = curve[i];
        point.percent = std::clamp(point.percent, 0, 100);
        if (i > 0) {
            const qsizetype left = curve.size() - 1 - i;
            point.temperature = std::clamp(point.temperature, curve[i - 1].temperature + (i < curve.size() - 1 ? 1 : 0), hottest - int(left));
            point.percent = std::max(point.percent, curve[i - 1].percent);
        }
    }
    return curve;
}

Curve curveFromText(const QString &text)
{
    Curve curve;
    for (const QString &part : text.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        const QStringList pair = part.split(QLatin1Char(':'));
        bool okTemperature = false;
        bool okPercent = false;
        const CurvePoint point{pair.value(0).toInt(&okTemperature), pair.value(1).toInt(&okPercent)};
        if (pair.size() != 2 || !okTemperature || !okPercent) {
            return {};
        }
        curve << point;
    }
    return tidied(curve);
}

QVariantList curveList(const Curve &curve)
{
    QVariantList list;
    for (const CurvePoint &point : curve) {
        list << QVariantMap{{QStringLiteral("temperature"), point.temperature}, {QStringLiteral("percent"), point.percent}};
    }
    return list;
}
}

CoolerApp::CoolerApp(Cooler::Backend *backend, QObject *parent)
    : QObject(parent)
    , m_backend(backend)
{
    s_instance = this;
    m_readings = backend->readings();
    load();

    connect(backend, &Cooler::Backend::presentChanged, this, [this]() {
        m_readings = m_backend->readings();
        load();
        Q_EMIT presentChanged();
        Q_EMIT deviceChanged();
        Q_EMIT readingsChanged();
    });
    connect(backend, &Cooler::Backend::deviceChanged, this, &CoolerApp::deviceChanged);
    connect(backend, &Cooler::Backend::readingsChanged, this, [this]() {
        const Cooler::Readings readings = m_backend->readings();
        if (readings != m_readings) {
            m_readings = readings;
            Q_EMIT readingsChanged();
        }
    });
    connect(backend, &Cooler::Backend::settingsChanged, this, &CoolerApp::load);
    handOver();
}

// A backend starts out knowing none of the choices made in earlier runs, so
// it is told them once, as if each had just been made. Brightness and
// rotation only where the person chose them: otherwise the cooler keeps its
// own.
void CoolerApp::handOver()
{
    const KConfig config(configName);
    const KConfigGroup screen = config.group(QLatin1String(screenGroup));
    if (screen.hasKey(Settings::brightness)) {
        m_backend->setBrightness(m_brightness);
    }
    if (screen.hasKey(Settings::rotation)) {
        m_backend->setRotation(m_rotation);
    }
    m_backend->setCustomCurve(Channel::Fans, m_customFans);
    m_backend->setCustomCurve(Channel::Pump, m_customPump);
    m_backend->setStopFansWhenCool(m_stopFansWhenCool);
    m_backend->setPumpMinimum(m_pumpMinimum);
    m_backend->setFullSpeedInGames(m_fullSpeedInGames);
    m_backend->setMode(m_mode);
}

CoolerApp *CoolerApp::instance()
{
    return s_instance;
}

CoolerApp *CoolerApp::create(QQmlEngine *, QJSEngine *)
{
    QQmlEngine::setObjectOwnership(s_instance, QQmlEngine::CppOwnership);
    return s_instance;
}

void CoolerApp::setEngine(QQmlEngine *engine)
{
    m_engine = engine;
    QQmlComponent faceView(engine);
    faceView.loadFromModule("WildOS.Cooler.Faces", "FaceView");
    m_facesAvailable = faceView.isReady();
}

void CoolerApp::setMainWindow(QQuickWindow *window)
{
    m_window = window;
}

void CoolerApp::activate()
{
    if (!m_window) {
        return;
    }
    if (m_window->visibility() == QWindow::Minimized) {
        m_window->showNormal();
    }
    m_window->show();
    m_window->raise();
    // A start from the launcher carries an activation token, which lets the
    // window take the focus from the app in front.
    KWindowSystem::updateStartupId(m_window);
    KWindowSystem::activateWindow(m_window);
    m_window->requestActivate();
}

void CoolerApp::load()
{
    const KConfig config(configName);
    const KConfigGroup screen = config.group(QLatin1String(screenGroup));
    const KConfigGroup cooling = config.group(QLatin1String(coolingGroup));

    m_face = screen.readEntry(Settings::face, m_backend->face());
    if (!faces.contains(m_face)) {
        m_face = faces.constFirst();
    }
    m_brightness = std::clamp(screen.readEntry(Settings::brightness, m_backend->brightness()), 0, 100);
    m_rotation = screen.readEntry(Settings::rotation, m_backend->rotation());
    if (m_rotation % 90 != 0 || m_rotation < 0 || m_rotation > 270) {
        m_rotation = 0;
    }
    m_showMark = screen.readEntry(Settings::showMark, m_backend->showMark());
    m_themeColours = screen.readEntry(Settings::themeColours, m_backend->themeColours());
    m_temperature = screen.readEntry(Settings::temperature, QString::fromLatin1(Settings::defaultTemperature));
    if (!temperatures.contains(m_temperature)) {
        m_temperature = QString::fromLatin1(Settings::defaultTemperature);
    }
    m_dimWhenIdle = screen.readEntry(Settings::dimWhenIdle, m_backend->dimWhenIdle());
    m_warnLiquidAbove = screen.readEntry(Settings::warnLiquidAbove, m_backend->warnLiquidAbove());
    m_warnCpuLimitSeconds = screen.readEntry(Settings::warnCpuLimitSeconds, m_backend->warnCpuLimitSeconds());

    m_mode = cooling.readEntry(Settings::mode, m_backend->mode());
    if (!modes.contains(m_mode)) {
        m_mode = QStringLiteral("balanced");
    }
    m_customFans = curveFromText(cooling.readEntry(Settings::fanCurve, QString()));
    if (m_customFans.isEmpty()) {
        m_customFans = m_backend->curve(QStringLiteral("custom"), Channel::Fans);
    }
    m_customPump = curveFromText(cooling.readEntry(Settings::pumpCurve, QString()));
    if (m_customPump.isEmpty()) {
        m_customPump = m_backend->curve(QStringLiteral("custom"), Channel::Pump);
    }
    m_stopFansWhenCool = cooling.readEntry(Settings::stopFansWhenCool, m_backend->stopFansWhenCool());
    m_pumpMinimum = std::clamp(cooling.readEntry(Settings::pumpMinimum, m_backend->pumpMinimum()), 0, 100);
    m_fullSpeedInGames = cooling.readEntry(Settings::fullSpeedInGames, m_backend->fullSpeedInGames());

    Q_EMIT faceChanged();
    Q_EMIT brightnessChanged();
    Q_EMIT rotationChanged();
    Q_EMIT showMarkChanged();
    Q_EMIT themeColoursChanged();
    Q_EMIT temperatureChanged();
    Q_EMIT dimWhenIdleChanged();
    Q_EMIT warningChanged();
    Q_EMIT modeChanged();
    Q_EMIT curvesChanged();
    Q_EMIT optionsChanged();
}

void CoolerApp::store(const char *group, const char *key, const QVariant &value)
{
    KConfig config(configName);
    // With notice, so the program that draws the cooler's screen follows a
    // click here at once (it watches this file with KConfigWatcher).
    config.group(QLatin1String(group)).writeEntry(key, value, KConfig::Notify);
    config.sync();
}

// The names the faces read (faces/FaceBase.qml). "now" is the time the
// clock faces show. A reading that is not known is left out, and a face
// then leaves it off: it never stands in a number of its own.
QVariantMap CoolerApp::readings() const
{
    const auto list = [](const QList<double> &values) {
        QVariantList out;
        out.reserve(values.size());
        for (const double value : values) {
            out << value;
        }
        return out;
    };
    QVariantMap readings{
        {QStringLiteral("cpuLoad"), m_readings.cpuLoad},
        {QStringLiteral("coreLoads"), list(m_readings.coreLoads)},
        {QStringLiteral("now"), QDateTime::currentDateTime()},
        {QStringLiteral("show"), m_temperature},
    };
    if (m_readings.cpuTemperature >= 0) {
        readings.insert(QStringLiteral("cpuTemp"), m_readings.cpuTemperature);
        readings.insert(QStringLiteral("cpuHighest"), m_readings.cpuHighestToday);
        readings.insert(QStringLiteral("cpuHistory"), list(m_readings.cpuHistory));
    }
    if (m_readings.gpuTemperature >= 0) {
        readings.insert(QStringLiteral("gpuTemp"), m_readings.gpuTemperature);
        readings.insert(QStringLiteral("gpuHistory"), list(m_readings.gpuHistory));
    }
    if (m_readings.liquidTemperature >= 0) {
        readings.insert(QStringLiteral("liquidTemp"), m_readings.liquidTemperature);
        readings.insert(QStringLiteral("pumpRpm"), m_readings.pumpRpm);
        readings.insert(QStringLiteral("pumpPercent"), m_readings.pumpPercent);
        readings.insert(QStringLiteral("fanRpm"), m_readings.fanRpm);
        readings.insert(QStringLiteral("fanPercent"), m_readings.fanPercent);
    }
    return readings;
}

void CoolerApp::setFace(const QString &face)
{
    if (face == m_face || !faces.contains(face)) {
        return;
    }
    m_face = face;
    store(screenGroup, Settings::face, face);
    m_backend->setFace(face);
    Q_EMIT faceChanged();
}

void CoolerApp::setBrightness(int percent)
{
    percent = std::clamp(percent, 0, 100);
    if (percent == m_brightness) {
        return;
    }
    m_brightness = percent;
    store(screenGroup, Settings::brightness, percent);
    m_backend->setBrightness(percent);
    Q_EMIT brightnessChanged();
}

void CoolerApp::setRotation(int degrees)
{
    if (degrees == m_rotation || degrees % 90 != 0 || degrees < 0 || degrees > 270) {
        return;
    }
    m_rotation = degrees;
    store(screenGroup, Settings::rotation, degrees);
    m_backend->setRotation(degrees);
    Q_EMIT rotationChanged();
}

void CoolerApp::setShowMark(bool show)
{
    if (show == m_showMark) {
        return;
    }
    m_showMark = show;
    store(screenGroup, Settings::showMark, show);
    m_backend->setShowMark(show);
    Q_EMIT showMarkChanged();
}

void CoolerApp::setThemeColours(bool use)
{
    if (use == m_themeColours) {
        return;
    }
    m_themeColours = use;
    store(screenGroup, Settings::themeColours, use);
    m_backend->setThemeColours(use);
    Q_EMIT themeColoursChanged();
}

void CoolerApp::setTemperature(const QString &temperature)
{
    if (temperature == m_temperature || !temperatures.contains(temperature)) {
        return;
    }
    m_temperature = temperature;
    store(screenGroup, Settings::temperature, temperature);
    Q_EMIT temperatureChanged();
    // The previews draw from the readings, which carry the choice.
    Q_EMIT readingsChanged();
}

void CoolerApp::setDimWhenIdle(bool dim)
{
    if (dim == m_dimWhenIdle) {
        return;
    }
    m_dimWhenIdle = dim;
    store(screenGroup, Settings::dimWhenIdle, dim);
    m_backend->setDimWhenIdle(dim);
    Q_EMIT dimWhenIdleChanged();
}

void CoolerApp::setWarnLiquidAbove(int temperature)
{
    if (temperature == m_warnLiquidAbove) {
        return;
    }
    m_warnLiquidAbove = temperature;
    store(screenGroup, Settings::warnLiquidAbove, temperature);
    m_backend->setWarnLiquidAbove(temperature);
    Q_EMIT warningChanged();
}

void CoolerApp::setWarnCpuLimitSeconds(int seconds)
{
    if (seconds == m_warnCpuLimitSeconds) {
        return;
    }
    m_warnCpuLimitSeconds = seconds;
    store(screenGroup, Settings::warnCpuLimitSeconds, seconds);
    m_backend->setWarnCpuLimitSeconds(seconds);
    Q_EMIT warningChanged();
}

void CoolerApp::setMode(const QString &mode)
{
    if (mode == m_mode || !modes.contains(mode)) {
        return;
    }
    m_mode = mode;
    store(coolingGroup, Settings::mode, mode);
    m_backend->setMode(mode);
    Q_EMIT modeChanged();
    Q_EMIT curvesChanged();
}

Curve CoolerApp::shownCurve(Channel channel) const
{
    if (m_mode == QLatin1String("custom")) {
        return channel == Channel::Fans ? m_customFans : m_customPump;
    }
    return m_backend->curve(m_mode, channel);
}

QVariantList CoolerApp::fanCurve() const
{
    return curveList(shownCurve(Channel::Fans));
}

QVariantList CoolerApp::pumpCurve() const
{
    return curveList(shownCurve(Channel::Pump));
}

void CoolerApp::setCurve(const QString &channel, const QVariantList &points)
{
    Curve curve;
    for (const QVariant &value : points) {
        const QVariantMap point = value.toMap();
        curve << CurvePoint{qRound(point.value(QStringLiteral("temperature")).toDouble()),
                            qRound(point.value(QStringLiteral("percent")).toDouble())};
    }
    curve = tidied(curve);
    if (curve.isEmpty()) {
        return;
    }
    const bool fans = channel == QLatin1String("fans");
    // Moving a point of a built-in mode starts "Your own" from that mode,
    // so the other curve stays where it was.
    if (m_mode != QLatin1String("custom")) {
        m_customFans = shownCurve(Channel::Fans);
        m_customPump = shownCurve(Channel::Pump);
    }
    (fans ? m_customFans : m_customPump) = curve;

    KConfig config(configName);
    KConfigGroup cooling = config.group(QLatin1String(coolingGroup));
    cooling.writeEntry(Settings::fanCurve, curveText(m_customFans));
    cooling.writeEntry(Settings::pumpCurve, curveText(m_customPump));
    cooling.writeEntry(Settings::mode, QStringLiteral("custom"));
    config.sync();

    m_backend->setCustomCurve(Channel::Fans, m_customFans);
    m_backend->setCustomCurve(Channel::Pump, m_customPump);
    if (m_mode != QLatin1String("custom")) {
        m_mode = QStringLiteral("custom");
        m_backend->setMode(m_mode);
        Q_EMIT modeChanged();
    }
    Q_EMIT curvesChanged();
}

void CoolerApp::setStopFansWhenCool(bool stop)
{
    if (stop == m_stopFansWhenCool) {
        return;
    }
    m_stopFansWhenCool = stop;
    store(coolingGroup, Settings::stopFansWhenCool, stop);
    m_backend->setStopFansWhenCool(stop);
    Q_EMIT optionsChanged();
}

void CoolerApp::setPumpMinimum(int percent)
{
    percent = std::clamp(percent, 0, 100);
    if (percent == m_pumpMinimum) {
        return;
    }
    m_pumpMinimum = percent;
    store(coolingGroup, Settings::pumpMinimum, percent);
    m_backend->setPumpMinimum(percent);
    Q_EMIT optionsChanged();
}

void CoolerApp::setFullSpeedInGames(bool full)
{
    if (full == m_fullSpeedInGames) {
        return;
    }
    m_fullSpeedInGames = full;
    store(coolingGroup, Settings::fullSpeedInGames, full);
    m_backend->setFullSpeedInGames(full);
    Q_EMIT optionsChanged();
}

void CoolerApp::lookAgain()
{
    m_backend->lookAgain();
}

void CoolerApp::putEverythingBack()
{
    KConfig config(configName);
    config.deleteGroup(QLatin1String(screenGroup), KConfig::Notify);
    config.deleteGroup(QLatin1String(coolingGroup), KConfig::Notify);
    config.sync();
    m_backend->putEverythingBack();
    load();
}

QUrl CoolerApp::reportFolder() const
{
    return QUrl::fromLocalFile(QStandardPaths::writableLocation(QStandardPaths::DocumentsLocation));
}

// In English whatever the display language: it is read by whoever answers
// the problem report.
QString CoolerApp::reportText() const
{
    QString text;
    QTextStream out(&text);
    out << "Wild OS Cooler report\n";
    out << "Saved: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    out << "Cooler version: " << QCoreApplication::applicationVersion() << "\n\n";
    if (!present()) {
        out << "No supported cooler found.\n";
    } else {
        out << "Model: " << model() << "\n";
        out << "Screen: " << screenSize() << " by " << screenSize() << "\n";
        out << "Firmware: " << (firmware().isEmpty() ? QStringLiteral("not known") : firmware()) << "\n";
        out << "Fans on the cooler: " << fanCount() << "\n";
        out << "Lighting: " << (hasLighting() ? "yes" : "no") << "\n\n";
        out << "CPU: " << m_readings.cpuTemperature << " C, highest today " << m_readings.cpuHighestToday << " C, load "
            << m_readings.cpuLoad << " per cent\n";
        out << "Liquid: " << m_readings.liquidTemperature << " C\n";
        out << "Pump: " << m_readings.pumpRpm << " rpm, " << m_readings.pumpPercent << " per cent\n";
        out << "Fans: " << m_readings.fanRpm << " rpm, " << m_readings.fanPercent << " per cent\n";
        if (m_readings.gpuTemperature >= 0) {
            out << "GPU: " << m_readings.gpuTemperature << " C\n";
        }
    }
    out << "\nFace: " << m_face << "\n";
    out << "Brightness: " << m_brightness << "\n";
    out << "Rotation: " << m_rotation << "\n";
    out << "Wild OS mark: " << (m_showMark ? "on" : "off") << "\n";
    out << "Theme colours: " << (m_themeColours ? "on" : "off") << "\n";
    out << "Dim when idle: " << (m_dimWhenIdle ? "on" : "off") << "\n";
    out << "Warning: liquid above " << m_warnLiquidAbove << " C, CPU at its limit for " << m_warnCpuLimitSeconds << " s\n";
    out << "Mode: " << m_mode << "\n";
    out << "Fan curve: " << curveText(shownCurve(Channel::Fans)) << "\n";
    out << "Pump curve: " << curveText(shownCurve(Channel::Pump)) << "\n";
    out << "Stop the fans when cool: " << (m_stopFansWhenCool ? "on" : "off") << "\n";
    out << "Pump never below: " << m_pumpMinimum << " per cent\n";
    out << "Full speed in games: " << (m_fullSpeedInGames ? "on" : "off") << "\n";
    const QString device = m_backend->report();
    if (!device.isEmpty()) {
        out << "\n" << device;
    }
    return text;
}

bool CoolerApp::saveReport(const QUrl &file)
{
    QFile out(file.toLocalFile());
    if (!out.open(QIODevice::WriteOnly | QIODevice::Text)) {
        return false;
    }
    const QByteArray bytes = reportText().toUtf8();
    return out.write(bytes) == bytes.size();
}
