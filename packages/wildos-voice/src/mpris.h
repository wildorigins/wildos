#pragma once

#include <QDBusAbstractAdaptor>
#include <QDBusObjectPath>
#include <QObject>
#include <QStringList>
#include <QVariantMap>

// The recording being played back in the media controls (the taskbar's
// media flyout, the lock screen, the keyboard's media keys), through MPRIS
// 2 on /org/mpris/MediaPlayer2. The bus name
// org.mpris.MediaPlayer2.wildosvoice is held only while a recording is
// open for playback, so the media keys stay with Music or Videos the rest
// of the time, and while Voice records.
class Mpris : public QObject
{
    Q_OBJECT

public:
    explicit Mpris(QObject *parent);
    ~Mpris() override;

    void notify(const QString &interface, const QVariantMap &changed);

Q_SIGNALS:
    void raiseRequested();

private:
    void announce();

    bool m_announced = false;
};

class MprisRoot : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2")

    Q_PROPERTY(bool CanQuit READ canQuit)
    Q_PROPERTY(bool CanRaise READ canRaise)
    Q_PROPERTY(bool CanSetFullscreen READ canSetFullscreen)
    Q_PROPERTY(bool Fullscreen READ fullscreen)
    Q_PROPERTY(bool HasTrackList READ hasTrackList)
    Q_PROPERTY(QString Identity READ identity)
    Q_PROPERTY(QString DesktopEntry READ desktopEntry)
    Q_PROPERTY(QStringList SupportedUriSchemes READ supportedUriSchemes)
    Q_PROPERTY(QStringList SupportedMimeTypes READ supportedMimeTypes)

public:
    explicit MprisRoot(Mpris *parent);

    // Not from the media controls: Voice may be recording.
    bool canQuit() const { return false; }
    bool canRaise() const { return true; }
    bool canSetFullscreen() const { return false; }
    bool fullscreen() const { return false; }
    bool hasTrackList() const { return false; }
    QString identity() const;
    QString desktopEntry() const;
    QStringList supportedUriSchemes() const { return {}; }
    QStringList supportedMimeTypes() const { return {}; }

public Q_SLOTS:
    void Raise();
    void Quit() {}

private:
    Mpris *m_mpris;
};

class MprisPlayer : public QDBusAbstractAdaptor
{
    Q_OBJECT
    Q_CLASSINFO("D-Bus Interface", "org.mpris.MediaPlayer2.Player")

    Q_PROPERTY(QString PlaybackStatus READ playbackStatus)
    Q_PROPERTY(double Rate READ rate WRITE setRate)
    Q_PROPERTY(QVariantMap Metadata READ metadata)
    Q_PROPERTY(double Volume READ volume WRITE setVolume)
    Q_PROPERTY(qlonglong Position READ position)
    Q_PROPERTY(double MinimumRate READ rate)
    Q_PROPERTY(double MaximumRate READ rate)
    Q_PROPERTY(bool CanGoNext READ canGoNext)
    Q_PROPERTY(bool CanGoPrevious READ canGoNext)
    Q_PROPERTY(bool CanPlay READ canPlay)
    Q_PROPERTY(bool CanPause READ canPlay)
    Q_PROPERTY(bool CanSeek READ canSeek)
    Q_PROPERTY(bool CanControl READ canControl)

public:
    explicit MprisPlayer(Mpris *parent);

    QString playbackStatus() const;
    // Voice plays at normal speed and at the system's volume; MPRIS still
    // wants Rate and Volume writable.
    double rate() const { return 1.0; }
    void setRate(double) {}
    double volume() const { return 1.0; }
    void setVolume(double) {}
    QVariantMap metadata() const;
    qlonglong position() const;
    bool canGoNext() const { return false; }
    bool canPlay() const;
    bool canSeek() const;
    bool canControl() const { return true; }

public Q_SLOTS:
    void Next() {}
    void Previous() {}
    void Pause();
    void PlayPause();
    void Stop();
    void Play();
    void Seek(qlonglong offset);
    void SetPosition(const QDBusObjectPath &trackId, qlonglong position);
    void OpenUri(const QString &) {}

Q_SIGNALS:
    void Seeked(qlonglong position);

private:
    QDBusObjectPath trackId() const;
    void changed(const QStringList &properties);

    Mpris *m_mpris;
};
