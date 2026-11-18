#pragma once

#include <QObject>
#include <QTimer>
#include <QVariantMap>

// Whether a game is running, for "Full speed in games". Two signs count:
//
//  - Steam starts every game under a small program whose command line
//    reads "SteamLaunch AppId=...". /proc is looked through for one every
//    ten seconds while watching; it is a few hundred small reads.
//  - GameMode (Lutris, Heroic and games that ask for it) says on the
//    session bus how many games have asked for it.
//
// A game started any other way is not seen.
class Games : public QObject
{
    Q_OBJECT

public:
    explicit Games(QObject *parent = nullptr);

    void watch(bool watch);
    bool running() const { return m_steam || m_gameMode > 0; }

Q_SIGNALS:
    void runningChanged();

private Q_SLOTS:
    void gameModeChanged(const QString &interface, const QVariantMap &changed, const QStringList &invalidated);

private:
    void look();
    void set(bool steam, int gameMode);

    QTimer m_timer;
    bool m_connected = false;
    bool m_steam = false;
    int m_gameMode = 0;
};
