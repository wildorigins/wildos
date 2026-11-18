#pragma once

#include <QAudioOutput>
#include <QMediaPlayer>
#include <QObject>
#include <QQmlEngine>

// Plays back the recording picked in the list.
class Player : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString path READ path NOTIFY pathChanged)
    Q_PROPERTY(bool playing READ playing NOTIFY playingChanged)
    Q_PROPERTY(double position READ position NOTIFY positionChanged)
    Q_PROPERTY(double duration READ duration NOTIFY durationChanged)

public:
    explicit Player(QObject *parent);
    static Player *instance();
    static Player *create(QQmlEngine *, QJSEngine *);

    QString path() const { return m_path; }
    bool playing() const;
    double position() const;
    double duration() const;

    // Loads a recording, paused at its start; an empty path unloads.
    Q_INVOKABLE void load(const QString &path);
    Q_INVOKABLE void play();
    Q_INVOKABLE void playPause();
    Q_INVOKABLE void pause();
    Q_INVOKABLE void seek(double ms);
    Q_INVOKABLE void skip(double ms);

Q_SIGNALS:
    void pathChanged();
    void playingChanged();
    void positionChanged();
    void durationChanged();
    // A jump in position, in milliseconds (MPRIS Seeked).
    void seeked(double position);
    void failed(const QString &message);

private:
    QMediaPlayer m_player;
    QAudioOutput m_output;
    QString m_path;
};
