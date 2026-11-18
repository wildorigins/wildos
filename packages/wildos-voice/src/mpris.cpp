#include "mpris.h"
#include "player.h"
#include "recordings.h"

#include <QDBusConnection>
#include <QDBusMessage>
#include <QFileInfo>
#include <QMetaProperty>
#include <QUrl>

namespace
{
const QString objectPath = QStringLiteral("/org/mpris/MediaPlayer2");
const QString serviceName = QStringLiteral("org.mpris.MediaPlayer2.wildosvoice");
const QString playerInterface = QStringLiteral("org.mpris.MediaPlayer2.Player");

// MPRIS counts in microseconds, Player in milliseconds.
qlonglong micro(double ms)
{
    return qlonglong(ms) * 1000;
}
}

Mpris::Mpris(QObject *parent)
    : QObject(parent)
{
    new MprisRoot(this);
    new MprisPlayer(this);
    QDBusConnection::sessionBus().registerObject(objectPath, this, QDBusConnection::ExportAdaptors);
    connect(Player::instance(), &Player::pathChanged, this, &Mpris::announce);
    announce();
}

Mpris::~Mpris()
{
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (m_announced) {
        bus.unregisterService(serviceName);
    }
    bus.unregisterObject(objectPath);
}

void Mpris::announce()
{
    const bool wanted = !Player::instance()->path().isEmpty();
    if (wanted == m_announced) {
        return;
    }
    m_announced = wanted;
    QDBusConnection bus = QDBusConnection::sessionBus();
    if (wanted) {
        bus.registerService(serviceName);
    } else {
        bus.unregisterService(serviceName);
    }
}

void Mpris::notify(const QString &interface, const QVariantMap &changed)
{
    QDBusMessage signal = QDBusMessage::createSignal(objectPath, QStringLiteral("org.freedesktop.DBus.Properties"), QStringLiteral("PropertiesChanged"));
    signal << interface << changed << QStringList();
    QDBusConnection::sessionBus().send(signal);
}

MprisRoot::MprisRoot(Mpris *parent)
    : QDBusAbstractAdaptor(parent)
    , m_mpris(parent)
{
}

QString MprisRoot::identity() const
{
    return QStringLiteral("Voice");
}

QString MprisRoot::desktopEntry() const
{
    return QStringLiteral("org.wildos.voice");
}

void MprisRoot::Raise()
{
    Q_EMIT m_mpris->raiseRequested();
}

MprisPlayer::MprisPlayer(Mpris *parent)
    : QDBusAbstractAdaptor(parent)
    , m_mpris(parent)
{
    Player *player = Player::instance();
    connect(player, &Player::playingChanged, this, [this] {
        changed({QStringLiteral("PlaybackStatus")});
    });
    connect(player, &Player::pathChanged, this, [this] {
        changed({QStringLiteral("PlaybackStatus"), QStringLiteral("Metadata"), QStringLiteral("CanPlay"), QStringLiteral("CanPause"), QStringLiteral("CanSeek")});
    });
    connect(player, &Player::durationChanged, this, [this] {
        changed({QStringLiteral("Metadata"), QStringLiteral("CanSeek")});
    });
    // Position is never sent as a change (MPRIS says listeners work it
    // out from the rate); a jump is announced with Seeked instead.
    connect(player, &Player::seeked, this, [this](double position) {
        Q_EMIT Seeked(micro(position));
    });
}

void MprisPlayer::changed(const QStringList &properties)
{
    QVariantMap values;
    for (const QString &name : properties) {
        const int index = metaObject()->indexOfProperty(name.toLatin1().constData());
        if (index >= 0) {
            values.insert(name, metaObject()->property(index).read(this));
        }
    }
    m_mpris->notify(playerInterface, values);
}

QString MprisPlayer::playbackStatus() const
{
    Player *player = Player::instance();
    if (player->playing()) {
        return QStringLiteral("Playing");
    }
    return player->path().isEmpty() ? QStringLiteral("Stopped") : QStringLiteral("Paused");
}

QDBusObjectPath MprisPlayer::trackId() const
{
    return QDBusObjectPath(Player::instance()->path().isEmpty() ? QStringLiteral("/org/mpris/MediaPlayer2/TrackList/NoTrack")
                                                               : QStringLiteral("/org/wildos/voice/recording"));
}

QVariantMap MprisPlayer::metadata() const
{
    Player *player = Player::instance();
    const QString path = player->path();
    if (path.isEmpty()) {
        return {};
    }
    QString title = Recordings::instance()->info(path).value(QStringLiteral("title")).toString();
    if (title.isEmpty()) {
        title = QFileInfo(path).completeBaseName();
    }
    return {
        {QStringLiteral("mpris:trackid"), QVariant::fromValue(trackId())},
        {QStringLiteral("mpris:length"), micro(player->duration())},
        {QStringLiteral("xesam:title"), title},
        {QStringLiteral("xesam:url"), QUrl::fromLocalFile(path).toString()},
    };
}

qlonglong MprisPlayer::position() const
{
    return micro(Player::instance()->position());
}

bool MprisPlayer::canPlay() const
{
    return !Player::instance()->path().isEmpty();
}

bool MprisPlayer::canSeek() const
{
    Player *player = Player::instance();
    return !player->path().isEmpty() && player->duration() > 0;
}

void MprisPlayer::Pause()
{
    Player::instance()->pause();
}

void MprisPlayer::PlayPause()
{
    Player::instance()->playPause();
}

void MprisPlayer::Stop()
{
    Player *player = Player::instance();
    player->pause();
    player->seek(0);
}

void MprisPlayer::Play()
{
    Player::instance()->play();
}

void MprisPlayer::Seek(qlonglong offset)
{
    Player::instance()->skip(offset / 1000.0);
}

void MprisPlayer::SetPosition(const QDBusObjectPath &track, qlonglong position)
{
    Player *player = Player::instance();
    if (track != trackId() || position < 0 || position / 1000.0 > player->duration()) {
        return;
    }
    player->seek(position / 1000.0);
}
