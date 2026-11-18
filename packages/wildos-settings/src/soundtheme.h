#pragma once

#include <QObject>
#include <QQmlEngine>

// The sound scheme (Wild OS's own, KDE's Ocean, or no sounds) and whether a
// sound plays when the desktop starts. Written where KDE's own sound module
// writes them, kdeglobals [Sounds] Theme and Enable, and the startkde event
// in plasma_workspace.notifyrc, so every KDE app and Plasma follow. The
// shutdown sound is Wild OS's own, in wildosrc [Sound] ShutdownSound.
class SoundTheme : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(QString scheme READ scheme NOTIFY changed)
    Q_PROPERTY(bool startupSound READ startupSound NOTIFY changed)
    Q_PROPERTY(bool shutdownSound READ shutdownSound NOTIFY changed)

public:
    explicit SoundTheme(QObject *parent = nullptr);

    QString scheme() const;
    bool startupSound() const;
    bool shutdownSound() const;

    // "wildos", "ocean" or "none".
    Q_INVOKABLE void setScheme(const QString &scheme);
    Q_INVOKABLE void setStartupSound(bool on);
    Q_INVOKABLE void setShutdownSound(bool on);
    // Plays a sound from the current scheme, so a choice can be heard.
    Q_INVOKABLE void preview(const QString &name);

Q_SIGNALS:
    void changed();
};
