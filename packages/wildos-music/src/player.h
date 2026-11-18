#pragma once

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>
#include <QQmlEngine>
#include <QStringList>
#include <QTimer>
#include <QVariantList>

// Playback and the queue. The queue is the songs in the order they will
// play: shuffling reorders it and keeps the unshuffled order to go back
// to. The queue, the place in it, the volume and the shuffle and repeat
// settings are kept in MusicPaths::stateFile between runs.
class Player : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QVariantMap current READ current NOTIFY currentChanged)
    Q_PROPERTY(QString currentPath READ currentPath NOTIFY currentChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(qint64 position READ position NOTIFY positionChanged)
    Q_PROPERTY(qint64 duration READ duration NOTIFY durationChanged)
    Q_PROPERTY(bool seekable READ seekable NOTIFY seekableChanged)
    // 0 to 1, as the slider shows it; the output gets it on a loudness
    // curve so the middle of the slider sounds like the middle.
    Q_PROPERTY(double volume READ volume WRITE setVolume NOTIFY volumeChanged)
    Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY volumeChanged)
    Q_PROPERTY(bool shuffle READ shuffle WRITE setShuffle NOTIFY shuffleChanged)
    // "off", "all" or "one".
    Q_PROPERTY(QString repeat READ repeat WRITE setRepeat NOTIFY repeatChanged)
    Q_PROPERTY(QStringList queue READ queue NOTIFY queueChanged)
    Q_PROPERTY(int index READ index NOTIFY queueChanged)
    Q_PROPERTY(bool canNext READ canNext NOTIFY queueChanged)
    Q_PROPERTY(bool canPrevious READ canPrevious NOTIFY queueChanged)
    // What was played lately, newest first: [{ kind: "album", key } or
    // { kind: "song", path }].
    Q_PROPERTY(QVariantList recent READ recent NOTIFY recentChanged)

public:
    explicit Player(QObject *parent);
    ~Player() override;
    static Player *instance();
    static Player *create(QQmlEngine *, QJSEngine *);

    QVariantMap current() const { return m_current; }
    QString currentPath() const;
    bool playing() const;
    qint64 position() const;
    qint64 duration() const;
    bool seekable() const;
    double volume() const { return m_volume; }
    void setVolume(double volume);
    bool muted() const { return m_muted; }
    void setMuted(bool muted);
    bool shuffle() const { return m_shuffle; }
    void setShuffle(bool on);
    QString repeat() const { return m_repeat; }
    void setRepeat(const QString &mode);
    QStringList queue() const { return m_queue; }
    int index() const { return m_index; }
    bool canNext() const;
    bool canPrevious() const;
    QVariantList recent() const { return m_recent; }

    // Plays the songs from `start`; with shuffle on, in a random order
    // starting with that song (or any, when start is -1).
    Q_INVOKABLE void playSongs(const QStringList &paths, int start, bool shuffled);
    // Songs, playlist files and folders of songs handed over from Files,
    // the command line or a drop on the window.
    Q_INVOKABLE void openFiles(const QStringList &paths);
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void play();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void stop();
    Q_INVOKABLE void next();
    Q_INVOKABLE void previous();
    Q_INVOKABLE void seek(qint64 position);
    // Forward or back from where the song is, in milliseconds.
    Q_INVOKABLE void skip(qint64 by);
    // Up or down in steps of a twentieth; turning it up unmutes.
    Q_INVOKABLE void stepVolume(int steps);
    Q_INVOKABLE void jump(int index);
    Q_INVOKABLE void playNext(const QStringList &paths);
    Q_INVOKABLE void addToQueue(const QStringList &paths);
    Q_INVOKABLE void removeFromQueue(int index);
    // Takes the song at `from` to place `to` in the queue; the song that
    // is playing stays the one playing.
    Q_INVOKABLE void moveInQueue(int from, int to);
    Q_INVOKABLE void clearUpNext();
    Q_INVOKABLE void cycleRepeat();

    void saveState();

Q_SIGNALS:
    void currentChanged();
    void playingChanged();
    void positionChanged();
    void durationChanged();
    void seekableChanged();
    void volumeChanged();
    void shuffleChanged();
    void repeatChanged();
    void queueChanged();
    void recentChanged();
    // A jump in position the listener did not get by playing on (MPRIS
    // Seeked).
    void seeked(qint64 position);
    void failed(const QString &message);

private:
    void load(int index, bool start);
    void refreshCurrent();
    void remember(const QString &path);
    void restoreState();
    void scheduleSave();
    void applyVolume();
    void finished();

    QMediaPlayer m_player;
    QAudioOutput m_output;
    QStringList m_queue;
    // The queue's order before shuffling, while shuffle is on.
    QStringList m_unshuffled;
    int m_index = -1;
    QVariantMap m_current;
    double m_volume = 0.7;
    bool m_muted = false;
    bool m_shuffle = false;
    QString m_repeat = QStringLiteral("off");
    QVariantList m_recent;
    qint64 m_resumeAt = -1;
    int m_failures = 0;
    QTimer m_save;
    QTimer m_savePosition;
};
