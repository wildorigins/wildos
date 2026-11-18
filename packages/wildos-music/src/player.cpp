#include "player.h"
#include "library.h"
#include "paths.h"
#include "playlists.h"
#include "tags.h"

#include <QAudio>
#include <QCoreApplication>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QRandomGenerator>
#include <QSaveFile>
#include <QUrl>

#include <algorithm>

namespace
{
Player *s_instance = nullptr;
constexpr int recentLimit = 30;

void shuffleList(QStringList &list, int from)
{
    QRandomGenerator *random = QRandomGenerator::global();
    for (int i = int(list.size()) - 1; i > from; --i) {
        const int j = from + int(random->bounded(i - from + 1));
        list.swapItemsAt(i, j);
    }
}
}

Player::Player(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    m_player.setAudioOutput(&m_output);

    connect(&m_player, &QMediaPlayer::playbackStateChanged, this, &Player::playingChanged);
    connect(&m_player, &QMediaPlayer::positionChanged, this, &Player::positionChanged);
    connect(&m_player, &QMediaPlayer::durationChanged, this, &Player::durationChanged);
    connect(&m_player, &QMediaPlayer::seekableChanged, this, &Player::seekableChanged);
    connect(&m_player, &QMediaPlayer::mediaStatusChanged, this, [this](QMediaPlayer::MediaStatus status) {
        if (status == QMediaPlayer::LoadedMedia && m_resumeAt > 0) {
            m_player.setPosition(m_resumeAt);
            m_resumeAt = -1;
        } else if (status == QMediaPlayer::BufferedMedia) {
            m_failures = 0;
        } else if (status == QMediaPlayer::EndOfMedia) {
            finished();
        }
    });
    connect(&m_player, &QMediaPlayer::errorOccurred, this, [this](QMediaPlayer::Error error, const QString &) {
        if (error == QMediaPlayer::NoError) {
            return;
        }
        const QString title = m_current.value(QStringLiteral("title")).toString();
        Q_EMIT failed(tr("Couldn't play %1").arg(title.isEmpty() ? QFileInfo(currentPath()).fileName() : title));
        ++m_failures;
        if (m_failures < m_queue.size() && canNext()) {
            QTimer::singleShot(0, this, [this] {
                load(m_index + 1 < m_queue.size() ? m_index + 1 : 0, true);
            });
        }
    });
    connect(Library::instance(), &Library::changed, this, &Player::refreshCurrent);

    m_save.setSingleShot(true);
    m_save.setInterval(1000);
    connect(&m_save, &QTimer::timeout, this, &Player::saveState);
    // The place in a song is kept now and then while it plays, so a crash
    // or a power cut loses little.
    m_savePosition.setInterval(15000);
    connect(&m_savePosition, &QTimer::timeout, this, &Player::saveState);
    connect(this, &Player::playingChanged, this, [this] {
        if (playing()) {
            m_savePosition.start();
        } else {
            m_savePosition.stop();
            scheduleSave();
        }
    });
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, &Player::saveState);

    restoreState();
}

Player::~Player()
{
    // QMediaPlayer reports its state as it goes; nothing here should hear it.
    m_player.disconnect(this);
}

Player *Player::instance()
{
    if (!s_instance) {
        new Player(QCoreApplication::instance());
    }
    return s_instance;
}

Player *Player::create(QQmlEngine *, QJSEngine *)
{
    Player *player = instance();
    QQmlEngine::setObjectOwnership(player, QQmlEngine::CppOwnership);
    return player;
}

QString Player::currentPath() const
{
    return m_index >= 0 && m_index < m_queue.size() ? m_queue.at(m_index) : QString();
}

bool Player::playing() const
{
    return m_player.playbackState() == QMediaPlayer::PlayingState;
}

qint64 Player::position() const
{
    return m_resumeAt > 0 ? m_resumeAt : m_player.position();
}

qint64 Player::duration() const
{
    const qint64 d = m_player.duration();
    return d > 0 ? d : m_current.value(QStringLiteral("duration")).toLongLong();
}

bool Player::seekable() const
{
    return m_player.isSeekable() || m_resumeAt > 0;
}

void Player::setVolume(double volume)
{
    volume = std::clamp(volume, 0.0, 1.0);
    if (qFuzzyCompare(volume + 1, m_volume + 1)) {
        return;
    }
    m_volume = volume;
    applyVolume();
    Q_EMIT volumeChanged();
    scheduleSave();
}

void Player::setMuted(bool muted)
{
    if (muted == m_muted) {
        return;
    }
    m_muted = muted;
    applyVolume();
    Q_EMIT volumeChanged();
    scheduleSave();
}

void Player::applyVolume()
{
    m_output.setMuted(m_muted);
    m_output.setVolume(float(QtAudio::convertVolume(m_volume, QtAudio::LogarithmicVolumeScale, QtAudio::LinearVolumeScale)));
}

void Player::setShuffle(bool on)
{
    if (on == m_shuffle) {
        return;
    }
    m_shuffle = on;
    const QString now = currentPath();
    if (on) {
        m_unshuffled = m_queue;
        if (m_index >= 0) {
            m_queue.move(m_index, 0);
            m_index = 0;
            shuffleList(m_queue, 1);
        } else {
            shuffleList(m_queue, 0);
        }
    } else if (!m_unshuffled.isEmpty()) {
        m_queue = m_unshuffled;
        m_unshuffled.clear();
        m_index = now.isEmpty() ? -1 : int(m_queue.indexOf(now));
    }
    Q_EMIT shuffleChanged();
    Q_EMIT queueChanged();
    scheduleSave();
}

void Player::setRepeat(const QString &mode)
{
    const QString clean = mode == QLatin1String("all") || mode == QLatin1String("one") ? mode : QStringLiteral("off");
    if (clean == m_repeat) {
        return;
    }
    m_repeat = clean;
    Q_EMIT repeatChanged();
    Q_EMIT queueChanged();
    scheduleSave();
}

void Player::cycleRepeat()
{
    setRepeat(m_repeat == QLatin1String("off") ? QStringLiteral("all") : m_repeat == QLatin1String("all") ? QStringLiteral("one") : QStringLiteral("off"));
}

bool Player::canNext() const
{
    return m_index + 1 < m_queue.size() || (m_repeat == QLatin1String("all") && !m_queue.isEmpty());
}

bool Player::canPrevious() const
{
    return !m_queue.isEmpty() && m_index >= 0;
}

void Player::playSongs(const QStringList &paths, int start, bool shuffled)
{
    if (paths.isEmpty()) {
        return;
    }
    m_queue = paths;
    m_unshuffled.clear();
    int first = std::clamp(start, 0, int(paths.size()) - 1);
    if (shuffled) {
        if (!m_shuffle) {
            m_shuffle = true;
            Q_EMIT shuffleChanged();
        }
    }
    if (m_shuffle) {
        m_unshuffled = m_queue;
        if (start < 0) {
            shuffleList(m_queue, 0);
        } else {
            m_queue.move(first, 0);
            shuffleList(m_queue, 1);
        }
        first = 0;
    }
    m_failures = 0;
    load(first, true);
    scheduleSave();
}

void Player::openFiles(const QStringList &paths)
{
    QStringList songs;
    for (const QString &arg : paths) {
        const QString path = arg.startsWith(QLatin1String("file:")) ? QUrl(arg).toLocalFile() : QFileInfo(arg).absoluteFilePath();
        const QString suffix = QFileInfo(path).suffix().toLower();
        if (QFileInfo(path).isDir()) {
            QStringList inside;
            QDirIterator walk(path, QDir::Files | QDir::Readable, QDirIterator::Subdirectories);
            while (walk.hasNext()) {
                const QString file = walk.next();
                if (Tags::isAudio(file)) {
                    inside.append(file);
                }
            }
            // By path, so each folder's songs stay together in name order.
            inside.sort();
            songs.append(inside);
        } else if (suffix == QLatin1String("m3u8") || suffix == QLatin1String("m3u")) {
            songs.append(Playlists::read(path));
        } else if (Tags::isAudio(path)) {
            songs.append(path);
        }
    }
    if (!songs.isEmpty()) {
        playSongs(songs, 0, false);
    } else if (!paths.isEmpty()) {
        Q_EMIT failed(tr("Music found no songs it can play there."));
    }
}

void Player::load(int index, bool start)
{
    if (index < 0 || index >= m_queue.size()) {
        return;
    }
    const bool same = index == m_index && m_player.source() == QUrl::fromLocalFile(m_queue.at(index));
    m_index = index;
    m_resumeAt = -1;
    const QUrl url = QUrl::fromLocalFile(m_queue.at(index));
    if (m_player.source() != url) {
        m_player.setSource(url);
    } else if (same || start) {
        m_player.setPosition(0);
    }
    refreshCurrent();
    Q_EMIT queueChanged();
    Q_EMIT durationChanged();
    if (start) {
        m_player.play();
        remember(m_queue.at(index));
    }
    scheduleSave();
}

void Player::refreshCurrent()
{
    const QString path = currentPath();
    const QVariantMap now = path.isEmpty() ? QVariantMap() : Library::instance()->track(path);
    if (now == m_current) {
        return;
    }
    m_current = now;
    Q_EMIT currentChanged();
}

void Player::remember(const QString &path)
{
    const QString key = Library::instance()->albumKeyOf(path);
    const QVariantMap entry = key.isEmpty() ? QVariantMap{{QStringLiteral("kind"), QStringLiteral("song")}, {QStringLiteral("path"), path}}
                                            : QVariantMap{{QStringLiteral("kind"), QStringLiteral("album")}, {QStringLiteral("key"), key}};
    m_recent.removeAll(entry);
    m_recent.prepend(entry);
    while (m_recent.size() > recentLimit) {
        m_recent.removeLast();
    }
    Q_EMIT recentChanged();
}

void Player::playPause()
{
    if (playing()) {
        pause();
    } else {
        play();
    }
}

void Player::play()
{
    if (m_queue.isEmpty()) {
        return;
    }
    if (m_index < 0) {
        load(0, true);
        return;
    }
    if (m_player.source().isEmpty()) {
        load(m_index, true);
        return;
    }
    m_player.play();
}

void Player::pause()
{
    m_player.pause();
}

void Player::stop()
{
    m_player.stop();
}

void Player::next()
{
    if (m_index + 1 < m_queue.size()) {
        load(m_index + 1, true);
    } else if (m_repeat == QLatin1String("all") && !m_queue.isEmpty()) {
        load(0, true);
    }
}

void Player::previous()
{
    // As on every player: a song more than a few seconds in goes back to
    // its start first.
    if (position() > 3000 || m_index <= 0) {
        if (m_index <= 0 && m_repeat == QLatin1String("all") && m_queue.size() > 1 && position() <= 3000) {
            load(int(m_queue.size()) - 1, true);
            return;
        }
        seek(0);
        if (!playing()) {
            m_player.play();
        }
        return;
    }
    load(m_index - 1, true);
}

void Player::seek(qint64 position)
{
    if (m_resumeAt > 0) {
        m_resumeAt = std::max<qint64>(1, position);
        Q_EMIT positionChanged();
        Q_EMIT seeked(position);
        return;
    }
    m_player.setPosition(std::max<qint64>(0, position));
    Q_EMIT seeked(position);
}

void Player::skip(qint64 by)
{
    if (currentPath().isEmpty() || !seekable()) {
        return;
    }
    const qint64 to = std::max<qint64>(0, position() + by);
    seek(duration() > 0 ? std::min(to, duration()) : to);
}

void Player::stepVolume(int steps)
{
    if (m_muted && steps > 0) {
        setMuted(false);
    }
    setVolume(m_volume + steps * 0.05);
}

void Player::jump(int index)
{
    m_failures = 0;
    load(index, true);
}

void Player::playNext(const QStringList &paths)
{
    if (m_queue.isEmpty()) {
        playSongs(paths, 0, false);
        return;
    }
    for (int i = 0; i < paths.size(); ++i) {
        m_queue.insert(m_index + 1 + i, paths.at(i));
    }
    if (m_shuffle) {
        const qsizetype at = m_unshuffled.indexOf(currentPath());
        for (int i = 0; i < paths.size(); ++i) {
            m_unshuffled.insert(at + 1 + i, paths.at(i));
        }
    }
    Q_EMIT queueChanged();
    scheduleSave();
}

void Player::addToQueue(const QStringList &paths)
{
    if (m_queue.isEmpty()) {
        playSongs(paths, 0, false);
        return;
    }
    m_queue.append(paths);
    if (m_shuffle) {
        m_unshuffled.append(paths);
    }
    Q_EMIT queueChanged();
    scheduleSave();
}

void Player::removeFromQueue(int index)
{
    if (index < 0 || index >= m_queue.size() || index == m_index) {
        return;
    }
    const QString path = m_queue.takeAt(index);
    if (m_shuffle) {
        m_unshuffled.removeOne(path);
    }
    if (index < m_index) {
        --m_index;
    }
    Q_EMIT queueChanged();
    scheduleSave();
}

void Player::moveInQueue(int from, int to)
{
    if (from < 0 || from >= m_queue.size() || to < 0 || to >= m_queue.size() || from == to) {
        return;
    }
    m_queue.move(from, to);
    if (m_index == from) {
        m_index = to;
    } else if (from < m_index && to >= m_index) {
        --m_index;
    } else if (from > m_index && to <= m_index) {
        ++m_index;
    }
    Q_EMIT queueChanged();
    scheduleSave();
}

void Player::clearUpNext()
{
    if (m_index < 0) {
        return;
    }
    m_queue = m_queue.mid(0, m_index + 1);
    if (m_shuffle) {
        m_unshuffled = m_queue;
    }
    Q_EMIT queueChanged();
    scheduleSave();
}

void Player::finished()
{
    if (m_repeat == QLatin1String("one")) {
        m_player.setPosition(0);
        m_player.play();
        return;
    }
    if (m_index + 1 < m_queue.size()) {
        load(m_index + 1, true);
    } else if (m_repeat == QLatin1String("all") && !m_queue.isEmpty()) {
        load(0, true);
    } else {
        // The end of the queue: the last song stays, ready at its start.
        m_player.stop();
        scheduleSave();
    }
}

void Player::scheduleSave()
{
    m_save.start();
}

void Player::saveState()
{
    m_save.stop();
    QJsonObject state{
        {QStringLiteral("queue"), QJsonArray::fromStringList(m_queue)},
        {QStringLiteral("index"), m_index},
        {QStringLiteral("position"), position()},
        {QStringLiteral("volume"), m_volume},
        {QStringLiteral("muted"), m_muted},
        {QStringLiteral("shuffle"), m_shuffle},
        {QStringLiteral("repeat"), m_repeat},
        {QStringLiteral("recent"), QJsonArray::fromVariantList(m_recent)},
    };
    if (m_shuffle) {
        state.insert(QStringLiteral("unshuffled"), QJsonArray::fromStringList(m_unshuffled));
    }
    QDir().mkpath(MusicPaths::dataFolder());
    QSaveFile file(MusicPaths::stateFile());
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(state).toJson(QJsonDocument::Compact));
        file.commit();
    }
}

void Player::restoreState()
{
    QFile file(MusicPaths::stateFile());
    if (!file.open(QIODevice::ReadOnly)) {
        applyVolume();
        return;
    }
    const QJsonObject state = QJsonDocument::fromJson(file.readAll()).object();
    auto strings = [](const QJsonValue &v) {
        QStringList list;
        for (const QJsonValue &item : v.toArray()) {
            list.append(item.toString());
        }
        return list;
    };
    m_queue = strings(state.value(QLatin1String("queue")));
    m_unshuffled = strings(state.value(QLatin1String("unshuffled")));
    m_volume = std::clamp(state.value(QLatin1String("volume")).toDouble(0.7), 0.0, 1.0);
    m_muted = state.value(QLatin1String("muted")).toBool();
    m_shuffle = state.value(QLatin1String("shuffle")).toBool();
    m_repeat = state.value(QLatin1String("repeat")).toString(QStringLiteral("off"));
    m_recent = state.value(QLatin1String("recent")).toArray().toVariantList();
    if (m_shuffle && m_unshuffled.isEmpty()) {
        m_unshuffled = m_queue;
    }
    applyVolume();
    const int index = state.value(QLatin1String("index")).toInt(-1);
    if (index >= 0 && index < m_queue.size()) {
        load(index, false);
        const qint64 at = state.value(QLatin1String("position")).toInteger();
        m_resumeAt = at > 0 ? at : -1;
    }
}
