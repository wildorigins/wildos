#pragma once

#include <QElapsedTimer>
#include <QObject>
#include <QQmlEngine>
#include <QTimer>
#include <QVariantList>

struct mpv_handle;
struct mpv_event;

// The one video player of the process, a libmpv core. The picture is drawn
// by VideoOutput through mpv's render API; everything else (the file, the
// place in it, chapters, tracks, speed, volume) is read and set here.
// Positions are remembered as it plays, so a video picks up where it was
// left; the volume, the speed and the picture shape are kept between runs.
class Player : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString path READ path NOTIFY fileChanged)
    Q_PROPERTY(QString title READ title NOTIFY fileChanged)
    Q_PROPERTY(QString fileName READ fileName NOTIFY fileChanged)
    Q_PROPERTY(QString folder READ folder NOTIFY fileChanged)
    // "Videos › Holidays": where the file is, as the overlay names it.
    Q_PROPERTY(QString place READ place NOTIFY fileChanged)
    Q_PROPERTY(bool loaded READ loaded NOTIFY loadedChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(bool ended READ ended NOTIFY playingChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(int videoWidth READ videoWidth NOTIFY videoSizeChanged)
    Q_PROPERTY(int videoHeight READ videoHeight NOTIFY videoSizeChanged)
    // "1080p", "4K": the short side of the picture.
    Q_PROPERTY(QString quality READ quality NOTIFY videoSizeChanged)
    Q_PROPERTY(bool hasVideo READ hasVideo NOTIFY tracksChanged)
    // [{ title, time }], time in seconds.
    Q_PROPERTY(QVariantList chapters READ chapters NOTIFY chaptersChanged)
    Q_PROPERTY(int chapter READ chapter NOTIFY chapterChanged)
    // [{ id, label, selected }]; subtitle id 0 is Off.
    Q_PROPERTY(QVariantList audioTracks READ audioTracks NOTIFY tracksChanged)
    Q_PROPERTY(QVariantList subtitleTracks READ subtitleTracks NOTIFY tracksChanged)
    Q_PROPERTY(int audioTrack READ audioTrack NOTIFY tracksChanged)
    Q_PROPERTY(int subtitleTrack READ subtitleTrack NOTIFY tracksChanged)
    Q_PROPERTY(double speed READ speed WRITE setSpeed NOTIFY speedChanged)
    // 0 to 100.
    Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)
    // "" for the file's own shape, or "16:9", "4:3", "21:9".
    Q_PROPERTY(QString aspect READ aspect WRITE setAspect NOTIFY aspectChanged)
    // The videos beside the one playing: its folder's, or the ones opened
    // together (`queued`), which play one after the other.
    Q_PROPERTY(QStringList folderVideos READ folderVideos NOTIFY folderVideosChanged)
    Q_PROPERTY(bool queued READ queued NOTIFY folderVideosChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY folderVideosChanged)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY folderVideosChanged)

public:
    // No default constructor: QML would make a second one instead of
    // calling create().
    explicit Player(QObject *parent);
    ~Player() override;
    static Player *instance();
    static Player *create(QQmlEngine *, QJSEngine *);

    mpv_handle *handle() const { return m_mpv; }

    QString path() const { return m_path; }
    QString title() const;
    QString fileName() const;
    QString folder() const;
    QString place() const;
    bool loaded() const { return m_loaded; }
    bool playing() const { return m_loaded && !m_paused && !m_ended; }
    bool ended() const { return m_ended; }
    double position() const { return m_position; }
    double duration() const { return m_duration; }
    int videoWidth() const { return m_videoWidth; }
    int videoHeight() const { return m_videoHeight; }
    QString quality() const;
    bool hasVideo() const { return m_hasVideo; }
    QVariantList chapters() const { return m_chapters; }
    int chapter() const { return m_chapter; }
    QVariantList audioTracks() const { return m_audioTracks; }
    QVariantList subtitleTracks() const { return m_subtitleTracks; }
    int audioTrack() const { return m_audioTrack; }
    int subtitleTrack() const { return m_subtitleTrack; }
    double speed() const { return m_speed; }
    void setSpeed(double speed);
    int volume() const { return m_volume; }
    void setVolume(int volume);
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    QString aspect() const { return m_aspect; }
    void setAspect(const QString &aspect);
    QStringList folderVideos() const { return m_folderVideos; }
    bool queued() const { return m_queued; }
    bool canNext() const;
    bool canPrevious() const;

    Q_INVOKABLE void open(const QString &pathOrUrl);
    // Several files or folders at once: the first plays and the rest wait
    // in the list.
    Q_INVOKABLE void openFiles(const QStringList &pathsOrUrls);
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void stop();
    // Leaves the video for the library: its place is noted first, then
    // there is no file at all.
    Q_INVOKABLE void close();
    Q_INVOKABLE void seek(double seconds);
    Q_INVOKABLE void skip(double seconds);
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void setAudioTrack(int id);
    Q_INVOKABLE void setSubtitleTrack(int id);
    // C: the next subtitle track, then Off.
    Q_INVOKABLE void cycleSubtitles();
    Q_INVOKABLE void stepSpeed(int steps);
    Q_INVOKABLE void stepVolume(int steps);
    // The chapter at a time, for the seek bar's tooltip; "" if none.
    Q_INVOKABLE QString chapterAt(double seconds) const;
    Q_INVOKABLE int chapterIndexAt(double seconds) const;
    // Writes where this video is now, as on a pause.
    Q_INVOKABLE void rememberPosition();

    // From VideoOutput, once mpv has somewhere to draw.
    Q_INVOKABLE void setRenderReady();

    // From mpv's threads: a new frame waits to be drawn, or events wait.
    static void wakeup(void *context);
    static void frameWaiting(void *context);

Q_SIGNALS:
    void fileChanged();
    void loadedChanged();
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void videoSizeChanged();
    void chaptersChanged();
    void chapterChanged();
    void tracksChanged();
    void speedChanged();
    void volumeChanged();
    void mutedChanged();
    void aspectChanged();
    void folderVideosChanged();
    // A jump, for MPRIS's Seeked; in seconds.
    void seeked(double position);
    // Picked up from a remembered place, in seconds.
    void resumed(double position);
    void failed(const QString &message);
    // A new frame is ready for VideoOutput to draw.
    void frameReady();

private Q_SLOTS:
    void processEvents();

private:
    void handleEvent(const mpv_event *event);
    void load();
    void setPositionValue(double position, bool force);
    void readTracks(const QVariant &list);
    void readChapters(const QVariant &list);
    void readFolder();
    void setMpv(const char *name, const QVariant &value);
    void command(const QVariantList &arguments);
    void saveSettings();

    mpv_handle *m_mpv = nullptr;
    QString m_path;
    bool m_loaded = false;
    bool m_paused = true;
    bool m_ended = false;
    double m_position = 0;
    double m_duration = 0;
    int m_videoWidth = 0;
    int m_videoHeight = 0;
    bool m_hasVideo = false;
    QVariantList m_chapters;
    int m_chapter = -1;
    QVariantList m_audioTracks;
    QVariantList m_subtitleTracks;
    int m_audioTrack = -1;
    int m_subtitleTrack = 0;
    double m_speed = 1.0;
    int m_volume = 100;
    bool m_muted = false;
    QString m_aspect;
    QStringList m_folderVideos;
    QStringList m_queue;
    bool m_queued = false;
    double m_resumeAt = 0;
    bool m_renderReady = false;
    QElapsedTimer m_positionClock;
    QTimer m_remember;
    QTimer m_settings;
};
