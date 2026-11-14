#pragma once

#include <QObject>
#include <QQmlEngine>
#include <QStringList>

namespace KScreen
{
class Dpms;
}

// What the session is doing that should hold the desktop still: the screen
// locked, a screen switched off, or battery saver on. Nobody sees the
// desktop then, or the user has asked for power to be saved, so the Warp
// wallpaper's breathing and its W rest until it changes back.
class Session : public QObject
{
    Q_OBJECT
    QML_ELEMENT
    QML_SINGLETON

    Q_PROPERTY(bool locked READ locked NOTIFY lockedChanged)
    // power-profiles-daemon's power-saver profile, which is what Battery
    // saver turns on in quick settings, Settings and the low battery prompt.
    Q_PROPERTY(bool powerSaver READ powerSaver NOTIFY powerSaverChanged)
    // The names (QScreen::name, Screen.name in QML) of the screens KWin has
    // switched off.
    Q_PROPERTY(QStringList screensOff READ screensOff NOTIFY screensOffChanged)
    // The sign-in splash is still covering the screen.
    Q_PROPERTY(bool splashShowing READ splashShowing NOTIFY splashShowingChanged)

public:
    explicit Session(QObject *parent = nullptr);

    bool locked() const { return m_locked; }
    bool powerSaver() const { return m_powerSaver; }
    QStringList screensOff() const { return m_screensOff; }
    bool splashShowing() const { return m_splashShowing; }

    // The taskbar has loaded and drawn what it draws ahead of time, so the
    // sign-in splash can go (wildos-wait-desktop, before KSplash's "ready").
    Q_INVOKABLE void desktopReady();

Q_SIGNALS:
    void lockedChanged();
    void powerSaverChanged();
    void screensOffChanged();
    void splashShowingChanged();

private Q_SLOTS:
    void setLocked(bool locked);
    void profilesPropertiesChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void readLocked();
    void readProfile();
    void setPowerSaver(bool powerSaver);
    void setScreenOff(const QString &name, bool off);

    KScreen::Dpms *m_dpms = nullptr;
    bool m_locked = false;
    bool m_powerSaver = false;
    QStringList m_screensOff;
    bool m_splashShowing = false;
};
