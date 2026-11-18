#pragma once

#include "coolerclient.h"
#include "games.h"
#include "sensors.h"
#include "session.h"

#include <KConfigWatcher>
#include <KSharedConfig>

#include <QElapsedTimer>
#include <QImage>
#include <QList>
#include <QObject>
#include <QTimer>

#include <memory>

class FaceRenderer;
class QSocketNotifier;

// Draws the chosen face and feeds it to the cooler's screen for as long as
// there is a cooler that can show it, the person's session is in front and
// unlocked, and the settings ask for it. While any of those does not hold
// it draws nothing, reads nothing and wakes for nothing but the signal that
// says otherwise; the socket is closed, which has the service put the
// cooler back on its own liquid screen.
class Streamer : public QObject
{
    Q_OBJECT

public:
    explicit Streamer(QObject *parent = nullptr);
    ~Streamer() override;

    // Leaves the cooler on its own screen and at its own brightness.
    void stop();

private:
    void readSettings();
    void update();
    void begin();
    void end();
    void opened(int fd);
    void closeSocket();
    void frame();
    void flush();
    void sample();
    void applyFace();
    void applyDim();
    void applyFullSpeed();
    QImage turned(const QImage &face) const;
    int restingBrightness() const;

    Cooler::Client m_client;
    Cooler::Sensors m_sensors;
    Session m_session;
    Games m_games;
    KSharedConfig::Ptr m_config;
    KConfigWatcher::Ptr m_watcher;

    // From wildoscoolerrc.
    QString m_face;
    bool m_showFaces = true;
    bool m_showMark = true;
    bool m_themeColours = true;
    QString m_temperature;
    bool m_dimWhenIdle = true;
    int m_warnLiquidAbove = 0;
    int m_warnCpuLimitSeconds = 0;
    bool m_fullSpeedInGames = true;
    int m_chosenBrightness = -1;

    bool m_stopped = false;
    bool m_showing = false;
    bool m_opening = false;
    std::unique_ptr<FaceRenderer> m_renderer;
    QObject *m_theme = nullptr;
    QTimer m_frameTimer;
    QTimer m_retry;
    QElapsedTimer m_clock;
    int m_screen = 0;
    int m_rotation = 0;

    int m_socket = -1;
    QSocketNotifier *m_writable = nullptr;
    QByteArray m_sending;
    qsizetype m_sent = 0;
    QImage m_lastFrame;

    bool m_liquidHot = false;
    bool m_cpuHot = false;
    int m_secondsAtLimit = 0;
    int m_secondsCooler = 0;
    bool m_warning = false;

    bool m_dimmed = false;
    int m_brightnessBeforeDim = -1;
    bool m_fullSpeed = false;
    bool m_launcherChecked = false;
    bool m_wasPresent = false;

    // For the log line that says what this costs.
    int m_drawn = 0;
    int m_written = 0;
    qint64 m_drawingNs = 0;
    qint64 m_turningNs = 0;
    qint64 m_statsAt = 0;
    qint64 m_cpuAt = 0;
};

// Cooler's entry in Start names a file that exists only while a cooler is
// connected (TryExec). Start reads that when its list of apps is rebuilt,
// not when the file comes or goes, so the list is rebuilt here whenever the
// two disagree. Returns whether a cooler is there.
bool checkLauncher();
