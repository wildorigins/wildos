#include "recorder.h"

#include "qwayland-zkde-screencast-unstable-v1.h"

#include <KPipeWire/PipeWireRecord>

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QGuiApplication>
#include <QScreen>
#include <QStandardPaths>
#include <QWaylandClientExtensionTemplate>

class ScreencastStream : public QObject, public QtWayland::zkde_screencast_stream_unstable_v1
{
    Q_OBJECT

public:
    explicit ScreencastStream(::zkde_screencast_stream_unstable_v1 *stream, QObject *parent)
        : QObject(parent)
        , QtWayland::zkde_screencast_stream_unstable_v1(stream)
    {
    }

    ~ScreencastStream() override
    {
        close();
    }

Q_SIGNALS:
    void created(uint node);
    void failed(const QString &error);
    void closed();

protected:
    void zkde_screencast_stream_unstable_v1_created(uint32_t node) override
    {
        Q_EMIT created(node);
    }
    void zkde_screencast_stream_unstable_v1_failed(const QString &error) override
    {
        Q_EMIT failed(error);
    }
    void zkde_screencast_stream_unstable_v1_closed() override
    {
        Q_EMIT closed();
    }
};

class Screencasting : public QWaylandClientExtensionTemplate<Screencasting>, public QtWayland::zkde_screencast_unstable_v1
{
public:
    Screencasting()
        : QWaylandClientExtensionTemplate<Screencasting>(3)
    {
        initialize();
    }

    ~Screencasting() override
    {
        if (isActive()) {
            destroy();
        }
    }
};

Recorder::Recorder(QObject *parent)
    : QObject(parent)
{
    if (QGuiApplication::platformName().startsWith(QLatin1String("wayland"))) {
        m_casting = std::make_unique<Screencasting>();
    }
    m_tick.setInterval(500);
    connect(&m_tick, &QTimer::timeout, this, &Recorder::secondsChanged);
}

Recorder::~Recorder()
{
    if (m_record) {
        m_record->stop();
    }
}

bool Recorder::available() const
{
    return m_casting && m_casting->isActive();
}

QString Recorder::nextPath(const QString &extension) const
{
    // Snipping Tool keeps recordings in Videos\Screen Recordings.
    QString videos = QStandardPaths::writableLocation(QStandardPaths::MoviesLocation);
    if (videos.isEmpty()) {
        videos = QDir::homePath() + QStringLiteral("/Videos");
    }
    const QString folder = videos + QStringLiteral("/Screen recordings");
    QDir().mkpath(folder);
    const QString stem = folder + QStringLiteral("/Screen recording ") + QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HHmmss"));
    QString path = stem + QLatin1Char('.') + extension;
    for (int n = 2; QFileInfo::exists(path); ++n) {
        path = stem + QStringLiteral(" (%1).").arg(n) + extension;
    }
    return path;
}

void Recorder::recordArea(const QRect &area, qreal scale)
{
    if (!available() || m_recording) {
        Q_EMIT failed(tr("Screen recording is not available right now."));
        return;
    }
    attach(new ScreencastStream(m_casting->stream_region(area.x(), area.y(), area.width(), area.height(), wl_fixed_from_double(scale),
                                                         QtWayland::zkde_screencast_unstable_v1::pointer_embedded),
                                this));
}

void Recorder::recordWindow(const QString &id)
{
    if (!available() || m_recording) {
        Q_EMIT failed(tr("Screen recording is not available right now."));
        return;
    }
    attach(new ScreencastStream(m_casting->stream_window(id, QtWayland::zkde_screencast_unstable_v1::pointer_embedded), this));
}

void Recorder::recordScreen(QScreen *screen)
{
    if (!screen) {
        return;
    }
    recordArea(screen->geometry(), screen->devicePixelRatio());
}

void Recorder::attach(ScreencastStream *stream)
{
    m_stream = stream;
    m_recording = true;
    Q_EMIT recordingChanged();

    connect(stream, &ScreencastStream::failed, this, [this](const QString &error) {
        qWarning("Screen recording stream failed: %s", qPrintable(error));
        m_recording = false;
        dropStream();
        Q_EMIT recordingChanged();
        Q_EMIT failed(tr("The recording could not start."));
    });
    connect(stream, &ScreencastStream::closed, this, &Recorder::stop);
    connect(stream, &ScreencastStream::created, this, [this](uint node) {
        m_record = new PipeWireRecord(this);
        // MP4 plays everywhere, Windows included; WebM when this machine's
        // FFmpeg has no H.264 encoder.
        const auto encoders = m_record->suggestedEncoders();
        for (auto wanted : {PipeWireBaseEncodedStream::H264Main, PipeWireBaseEncodedStream::H264Baseline, PipeWireBaseEncodedStream::VP9,
                            PipeWireBaseEncodedStream::VP8}) {
            if (encoders.contains(wanted)) {
                m_record->setEncoder(wanted);
                break;
            }
        }
        m_path = nextPath(m_record->extension());
        m_record->setOutput(m_path);
        m_record->setNodeId(node);
        connect(m_record, &PipeWireBaseEncodedStream::errorFound, this, [](const QString &error) {
            qWarning("Screen recording: %s", qPrintable(error));
        });
        connect(m_record, &PipeWireBaseEncodedStream::stateChanged, this, [this] {
            if (m_record->state() != PipeWireBaseEncodedStream::Idle || !m_finishing) {
                return;
            }
            m_finishing = false;
            m_recording = false;
            // The encoder has drained, so KWin can stop sending frames.
            dropStream();
            // Its destructor reports the state once more, by which time
            // m_record is already gone.
            disconnect(m_record, nullptr, this, nullptr);
            m_record->deleteLater();
            m_record = nullptr;
            Q_EMIT recordingChanged();
            if (QFileInfo(m_path).size() > 0) {
                Q_EMIT finished(m_path);
            } else {
                Q_EMIT failed(tr("Nothing was recorded."));
            }
        });
        m_record->start();
        m_elapsed.start();
        m_tick.start();
        Q_EMIT secondsChanged();
    });
}

void Recorder::dropStream()
{
    if (m_stream) {
        disconnect(m_stream, nullptr, this, nullptr);
        m_stream->deleteLater();
        m_stream = nullptr;
    }
}

void Recorder::stop()
{
    if (!m_recording || m_finishing) {
        return;
    }
    m_tick.stop();
    m_elapsed.invalidate();
    if (!m_record) {
        dropStream();
        m_recording = false;
        Q_EMIT recordingChanged();
        return;
    }
    m_finishing = true;
    Q_EMIT recordingChanged();
    m_record->stop();
}

#include "recorder.moc"
