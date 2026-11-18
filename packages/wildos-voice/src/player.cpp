#include "player.h"

#include <QCoreApplication>
#include <QUrl>

#include <algorithm>

namespace
{
Player *s_instance = nullptr;
}

Player::Player(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    m_player.setAudioOutput(&m_output);
    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, &Player::playingChanged);
    connect(&m_player, &QMediaPlayer::positionChanged, this, &Player::positionChanged);
    connect(&m_player, &QMediaPlayer::durationChanged, this, &Player::durationChanged);
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::EndOfMedia) {
            // Back to the start, ready to play again.
            m_player.stop();
            m_player.setPosition(0);
        }
    });
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error) {
        if (error != QMediaPlayer::NoError) {
            Q_EMIT failed(tr("This recording can't be played."));
        }
    });
}

Player *Player::instance()
{
    if (!s_instance) {
        new Player(qApp);
    }
    return s_instance;
}

Player *Player::create(QQmlEngine *, QJSEngine *)
{
    QQmlEngine::setObjectOwnership(instance(), QQmlEngine::CppOwnership);
    return instance();
}

bool Player::playing() const
{
    return m_player.playbackState() == QMediaPlayer::PlayingState;
}

double Player::position() const
{
    return double(m_player.position());
}

double Player::duration() const
{
    return double(m_player.duration());
}

void Player::load(const QString &path)
{
    if (path == m_path) {
        return;
    }
    m_player.stop();
    m_path = path;
    m_player.setSource(path.isEmpty() ? QUrl() : QUrl::fromLocalFile(path));
    Q_EMIT pathChanged();
}

void Player::play()
{
    if (!m_path.isEmpty()) {
        m_player.play();
    }
}

void Player::playPause()
{
    if (m_path.isEmpty()) {
        return;
    }
    if (playing()) {
        m_player.pause();
    } else {
        m_player.play();
    }
}

void Player::pause()
{
    m_player.pause();
}

void Player::seek(double ms)
{
    const qint64 length = m_player.duration();
    qint64 target = qint64(ms);
    if (length > 0) {
        target = std::clamp<qint64>(target, 0, length);
    }
    target = std::max<qint64>(0, target);
    m_player.setPosition(target);
    Q_EMIT seeked(double(target));
}

void Player::skip(double ms)
{
    seek(position() + ms);
}
