#include "streamer.h"

#include "facerenderer.h"
#include "settings.h"

#include <KConfigGroup>

#include <QCoreApplication>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QPainter>
#include <QProcess>
#include <QQmlComponent>
#include <QQmlEngine>
#include <QQuickItem>
#include <QSocketNotifier>
#include <QStandardPaths>

#include <cerrno>
#include <iterator>
#include <fcntl.h>
#include <sys/resource.h>
#include <sys/socket.h>
#include <unistd.h>

Q_LOGGING_CATEGORY(faces, "wildos.cooler.faces", QtInfoMsg)
// What drawing costs, every half minute. Off unless asked for:
// QT_LOGGING_RULES=wildos.cooler.faces.stats.info=true
Q_LOGGING_CATEGORY(stats, "wildos.cooler.faces.stats", QtWarningMsg)

namespace Settings = Cooler::Settings;

namespace
{
// The faces are drawn at the board's size and scaled to the screen.
constexpr int drawnSize = 640;
// The cooler takes about thirteen frames a second; drawing more only makes
// frames for the service to drop.
constexpr int frameEveryMs = 75;
constexpr int retryAfterMs = 3000;

// Against burn-in the whole face moves one pixel a minute round a ring of
// eight places, never more than one pixel from the middle either way. It is
// drawn this many pixels smaller all round, so its edge stays on the round
// screen wherever it stands.
constexpr int shiftMargin = 2;
constexpr QPoint shifts[] = {{1, 0}, {1, 1}, {0, 1}, {-1, 1}, {-1, 0}, {-1, -1}, {0, -1}, {1, -1}};

// The dimmed screen. Low enough to spare the panel, still readable.
constexpr int dimmedBrightness = 10;

// The warning starts above the chosen liquid temperature and ends this far
// below it, so a liquid hovering at the line does not flick the face.
constexpr double liquidRelief = 2;
// The processor counts as at its limit within this of the temperature at
// which it slows itself down, and as cooled once it has stayed this far
// below that for a while.
constexpr double limitWithin = 3;
constexpr double cooledBelow = 8;
constexpr int cooledForSeconds = 10;

const QString marker = QStringLiteral("/run/wildos-cooler/present");

int shiftEveryMs()
{
    bool ok = false;
    const int seconds = qEnvironmentVariableIntValue("WILDOS_COOLER_SHIFT_SECONDS", &ok);
    return ok && seconds > 0 ? seconds * 1000 : 60 * 1000;
}

qint64 cpuMs()
{
    struct rusage used{};
    getrusage(RUSAGE_SELF, &used);
    return (used.ru_utime.tv_sec + used.ru_stime.tv_sec) * 1000LL + (used.ru_utime.tv_usec + used.ru_stime.tv_usec) / 1000;
}

QVariantList asList(const QList<double> &values)
{
    QVariantList list;
    list.reserve(values.size());
    for (const double value : values) {
        list << value;
    }
    return list;
}
}

bool checkLauncher()
{
    const bool there = QFileInfo::exists(marker);
    const QString flag = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wildos-cooler/offered");
    if (there == QFileInfo::exists(flag)) {
        return there;
    }
    if (there) {
        QDir().mkpath(QFileInfo(flag).path());
        QFile made(flag);
        made.open(QIODevice::WriteOnly);
    } else {
        QFile::remove(flag);
    }
    qCInfo(faces, "a cooler %s: rebuilding the list of apps", there ? "is connected" : "has gone");
    QProcess::startDetached(QStringLiteral("kbuildsycoca6"), {});
    return there;
}

Streamer::Streamer(QObject *parent)
    : QObject(parent)
    , m_config(KSharedConfig::openConfig(QLatin1String(Settings::file), KConfig::SimpleConfig))
    , m_watcher(KConfigWatcher::create(m_config))
{
    readSettings();

    m_frameTimer.setInterval(frameEveryMs);
    connect(&m_frameTimer, &QTimer::timeout, this, &Streamer::frame);
    m_retry.setSingleShot(true);
    m_retry.setInterval(retryAfterMs);
    connect(&m_retry, &QTimer::timeout, this, [this]() {
        if (m_showing && m_socket < 0 && !m_opening) {
            m_opening = true;
            m_client.openFrames(this, [this](int fd) {
                opened(fd);
            });
        }
    });

    connect(m_watcher.data(), &KConfigWatcher::configChanged, this, [this]() {
        readSettings();
        applyFace();
        update();
    });
    connect(&m_client, &Cooler::Client::deviceChanged, this, &Streamer::update);
    connect(&m_session, &Session::changed, this, &Streamer::update);
    connect(&m_session, &Session::idleChanged, this, &Streamer::applyDim);
    connect(&m_games, &Games::runningChanged, this, &Streamer::applyFullSpeed);
    connect(&m_sensors, &Cooler::Sensors::changed, this, &Streamer::sample);
}

Streamer::~Streamer()
{
    stop();
}

void Streamer::stop()
{
    m_stopped = true;
    update();
}

void Streamer::readSettings()
{
    m_config->reparseConfiguration();
    const KConfigGroup screen = m_config->group(QLatin1String(Settings::screenGroup));
    m_face = screen.readEntry(Settings::face, QString::fromLatin1(Settings::defaultFace));
    m_showFaces = screen.readEntry(Settings::showFaces, true);
    m_showMark = screen.readEntry(Settings::showMark, Settings::defaultShowMark);
    m_themeColours = screen.readEntry(Settings::themeColours, Settings::defaultThemeColours);
    m_temperature = screen.readEntry(Settings::temperature, QString::fromLatin1(Settings::defaultTemperature));
    m_dimWhenIdle = screen.readEntry(Settings::dimWhenIdle, Settings::defaultDimWhenIdle);
    m_warnLiquidAbove = screen.readEntry(Settings::warnLiquidAbove, Settings::defaultWarnLiquidAbove);
    m_warnCpuLimitSeconds = screen.readEntry(Settings::warnCpuLimitSeconds, Settings::defaultWarnCpuLimitSeconds);
    m_chosenBrightness = screen.readEntry(Settings::brightness, -1);
    m_fullSpeedInGames = m_config->group(QLatin1String(Settings::coolingGroup)).readEntry(Settings::fullSpeedInGames, Settings::defaultFullSpeedInGames);
}

void Streamer::update()
{
    // Behind the lock screen or another account there is nothing to draw
    // and nothing to hear from the service until that ends.
    m_client.follow(!m_stopped && !m_session.locked() && m_session.inFront());
    const Cooler::Device &device = m_client.device();
    if (m_client.ready() && !m_stopped && (!m_launcherChecked || device.present != m_wasPresent)) {
        m_launcherChecked = true;
        m_wasPresent = device.present;
        checkLauncher();
    }

    const bool show = !m_stopped && device.present && device.canShowFaces && device.screen > 0 && m_showFaces && !m_session.locked()
        && m_session.inFront();
    if (m_showing && (!show || device.screen != m_screen)) {
        end();
    }
    m_rotation = device.rotation;
    if (show && !m_showing) {
        begin();
    }
    m_session.watchIdle(m_showing && m_dimWhenIdle);
    applyDim();
    m_games.watch(!m_stopped && device.present && m_fullSpeedInGames && m_session.inFront());
    applyFullSpeed();
}

void Streamer::begin()
{
    m_renderer = std::make_unique<FaceRenderer>(drawnSize);
    if (!m_renderer->isValid()) {
        // Nothing to retry: the session has no OpenGL to draw with. The
        // cooler keeps its own liquid screen.
        qCCritical(faces, "the faces cannot be drawn in this session (%s); leaving the cooler on its own screen",
                   qPrintable(m_renderer->errorString()));
        m_renderer.reset();
        m_stopped = true;
        QCoreApplication::exit(0);
        return;
    }
    // The screen is round itself, so the corners need no cutting.
    m_renderer->setCircular(false);

    QQmlComponent theme(m_renderer->engine(), QUrl(QStringLiteral("qrc:/wildos-cooler-faces/Theme.qml")));
    m_theme = theme.createWithInitialProperties({{QStringLiteral("view"), QVariant::fromValue<QObject *>(m_renderer->view())}});
    if (!m_theme) {
        qCWarning(faces, "the theme's colours cannot be read, so the faces keep their own: %s", qPrintable(theme.errorString()));
    }

    m_screen = m_client.device().screen;
    m_showing = true;
    m_warning = m_liquidHot = m_cpuHot = false;
    m_secondsAtLimit = m_secondsCooler = 0;
    m_clock.start();
    m_statsAt = 0;
    m_cpuAt = cpuMs();
    m_drawn = m_written = 0;
    m_drawingNs = m_turningNs = 0;
    applyFace();
    m_sensors.start();
    m_opening = true;
    m_client.openFrames(this, [this](int fd) {
        opened(fd);
    });
    m_frameTimer.start();
    qCInfo(faces, "showing %s on a %d pixel screen", qPrintable(m_face), m_screen);
}

void Streamer::end()
{
    m_showing = false;
    m_frameTimer.stop();
    m_retry.stop();
    m_sensors.stop();
    closeSocket();
    delete m_theme;
    m_theme = nullptr;
    m_renderer.reset();
    qCInfo(faces, "stopped; the cooler shows its own screen");
}

void Streamer::opened(int fd)
{
    m_opening = false;
    if (!m_showing) {
        if (fd >= 0) {
            ::close(fd);
        }
        return;
    }
    if (fd < 0) {
        m_retry.start();
        return;
    }
    ::fcntl(fd, F_SETFL, ::fcntl(fd, F_GETFL) | O_NONBLOCK);
    m_socket = fd;
    m_lastFrame = {};
    m_writable = new QSocketNotifier(fd, QSocketNotifier::Write, this);
    m_writable->setEnabled(false);
    connect(m_writable, &QSocketNotifier::activated, this, &Streamer::flush);
}

void Streamer::closeSocket()
{
    delete m_writable;
    m_writable = nullptr;
    if (m_socket >= 0) {
        ::close(m_socket);
        m_socket = -1;
    }
    m_sending.clear();
    m_sent = 0;
}

void Streamer::applyFace()
{
    if (!m_renderer) {
        return;
    }
    const QString face = m_warning ? QStringLiteral("limit") : m_face;
    if (!m_renderer->setFace(face)) {
        qCWarning(faces, "there is no face called %s; showing %s", qPrintable(face), Settings::defaultFace);
        m_renderer->setFace(QLatin1String(Settings::defaultFace));
    }
    m_renderer->setShowMark(m_showMark);
    if (m_theme) {
        m_theme->setProperty("use", m_themeColours);
    }
}

// One frame, unless the last has not all gone into the socket yet: the
// cooler is behind, and a frame drawn now would only be dropped.
void Streamer::frame()
{
    if (m_socket < 0 || !m_sending.isEmpty()) {
        return;
    }
    QElapsedTimer spent;
    spent.start();
    const QImage face = m_renderer->render(m_clock.elapsed() / 1000.0);
    m_drawingNs += spent.nsecsElapsed();
    if (face.isNull()) {
        qCCritical(faces, "a face could not be drawn; leaving the cooler on its own screen");
        m_stopped = true;
        end();
        QCoreApplication::exit(0);
        return;
    }
    ++m_drawn;
    spent.restart();
    const QImage turnedFace = turned(face);
    m_turningNs += spent.nsecsElapsed();
    // A face that has not changed is not sent again: the cooler keeps
    // showing the last frame it was given.
    if (turnedFace == m_lastFrame) {
        return;
    }
    m_lastFrame = turnedFace;
    m_sending = QByteArray(reinterpret_cast<const char *>(turnedFace.constBits()), turnedFace.sizeInBytes());
    m_sent = 0;
    flush();
}

void Streamer::flush()
{
    while (m_sent < m_sending.size()) {
        const ssize_t wrote = ::send(m_socket, m_sending.constData() + m_sent, size_t(m_sending.size() - m_sent), MSG_NOSIGNAL | MSG_DONTWAIT);
        if (wrote > 0) {
            m_sent += wrote;
        } else if (wrote < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            m_writable->setEnabled(true);
            return;
        } else if (wrote < 0 && errno == EINTR) {
            continue;
        } else {
            // The service closed its end: it restarted, or lost the cooler.
            qCInfo(faces, "the cooler service stopped taking frames");
            closeSocket();
            if (m_showing) {
                m_retry.start();
            }
            return;
        }
    }
    m_sending.clear();
    m_sent = 0;
    m_writable->setEnabled(false);
    ++m_written;
}

// The face as the service wants it: the screen's size, turned to the
// screen's rotation, four bytes a pixel (red, green, blue, one unused), and
// standing wherever the burn-in ring has it this minute.
QImage Streamer::turned(const QImage &face) const
{
    const int side = m_screen - 2 * shiftMargin;
    const QImage scaled = face.scaled(side, side, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    const QPoint shift = shifts[(m_clock.elapsed() / shiftEveryMs()) % std::size(shifts)];

    QImage out(m_screen, m_screen, QImage::Format_RGBX8888);
    out.fill(Qt::black);
    QPainter painter(&out);
    painter.translate(m_screen / 2.0 + shift.x(), m_screen / 2.0 + shift.y());
    painter.rotate(m_rotation);
    painter.drawImage(QPointF(-side / 2.0, -side / 2.0), scaled);
    return out;
}

// Once a second while showing: the readings the faces draw, the minute of
// history behind the graphs, and whether the red warning should take over.
void Streamer::sample()
{
    if (!m_showing || !m_renderer) {
        return;
    }
    const Cooler::CoolerReadings &cooler = m_client.readings();
    const double cpu = m_sensors.cpuTemperature();
    const double gpu = m_sensors.gpuTemperature();

    QVariantMap readings{
        {QStringLiteral("cpuLoad"), m_sensors.cpuLoad()},
        {QStringLiteral("coreLoads"), asList(m_sensors.coreLoads())},
        {QStringLiteral("now"), QDateTime::currentDateTime()},
        {QStringLiteral("show"), m_temperature},
    };
    if (cooler.valid) {
        readings.insert(QStringLiteral("liquidTemp"), cooler.liquid);
        readings.insert(QStringLiteral("pumpRpm"), cooler.pumpRpm);
        readings.insert(QStringLiteral("pumpPercent"), cooler.pumpPercent);
        readings.insert(QStringLiteral("fanRpm"), cooler.fanRpm);
        readings.insert(QStringLiteral("fanPercent"), cooler.fanPercent);
    }
    // A reading that is not known is left out, as the Cooler window's
    // previews leave it out, and the face then leaves it off.
    if (cpu >= 0) {
        readings.insert(QStringLiteral("cpuTemp"), cpu);
        readings.insert(QStringLiteral("cpuHighest"), m_sensors.cpuHighestToday());
        readings.insert(QStringLiteral("cpuHistory"), asList(m_sensors.cpuHistory()));
    }
    if (gpu >= 0) {
        readings.insert(QStringLiteral("gpuTemp"), gpu);
        readings.insert(QStringLiteral("gpuHistory"), asList(m_sensors.gpuHistory()));
    }
    m_renderer->setReadings(readings);

    if (cooler.valid) {
        if (cooler.liquid > m_warnLiquidAbove) {
            m_liquidHot = true;
        } else if (cooler.liquid <= m_warnLiquidAbove - liquidRelief) {
            m_liquidHot = false;
        }
    }
    if (cpu >= 0) {
        const double limit = m_sensors.cpuLimit();
        m_secondsAtLimit = cpu >= limit - limitWithin ? m_secondsAtLimit + 1 : 0;
        m_secondsCooler = cpu < limit - cooledBelow ? m_secondsCooler + 1 : 0;
        if (m_secondsAtLimit >= m_warnCpuLimitSeconds) {
            m_cpuHot = true;
        } else if (m_secondsCooler >= cooledForSeconds) {
            m_cpuHot = false;
        }
    }
    if ((m_liquidHot || m_cpuHot) != m_warning) {
        m_warning = m_liquidHot || m_cpuHot;
        qCInfo(faces, "%s", m_warning ? "too warm: the warning face takes over" : "cooled: back to the chosen face");
        applyFace();
    }

    if (stats().isInfoEnabled() && m_clock.elapsed() - m_statsAt >= 30000) {
        const qint64 now = m_clock.elapsed();
        const qint64 cpuNow = cpuMs();
        const double frames = std::max(1, m_drawn);
        qCInfo(stats, "%d frames drawn (%.1f ms each to draw, %.1f ms to scale and turn), %d sent, %.1f%% of one processor core over %lld s",
               m_drawn, m_drawingNs / frames / 1e6, m_turningNs / frames / 1e6, m_written,
               100.0 * double(cpuNow - m_cpuAt) / double(now - m_statsAt), (now - m_statsAt) / 1000);
        m_statsAt = now;
        m_cpuAt = cpuNow;
        m_drawn = m_written = 0;
        m_drawingNs = m_turningNs = 0;
    }
}

int Streamer::restingBrightness() const
{
    return m_chosenBrightness >= 0 ? m_chosenBrightness : m_brightnessBeforeDim;
}

void Streamer::applyDim()
{
    const bool dim = m_showing && m_dimWhenIdle && m_session.idle();
    if (dim == m_dimmed) {
        return;
    }
    if (dim) {
        m_brightnessBeforeDim = m_client.device().brightness;
        if (restingBrightness() <= dimmedBrightness) {
            return;
        }
        m_client.setBrightness(dimmedBrightness);
    } else if (m_client.device().present) {
        m_client.setBrightness(restingBrightness());
    }
    m_dimmed = dim;
}

void Streamer::applyFullSpeed()
{
    const bool present = m_client.device().present;
    const bool full = present && m_fullSpeedInGames && m_games.running() && !m_stopped;
    if (full == m_fullSpeed) {
        return;
    }
    m_fullSpeed = full;
    if (present) {
        qCInfo(faces, "%s", full ? "a game is running: pump and fans to full speed" : "no game is running: back to the curves");
        m_client.setFullSpeed(full);
    }
}
