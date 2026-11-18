#include "player.h"
#include "library.h"
#include "positions.h"

#include <mpv/client.h>

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QLocale>
#include <QSettings>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <list>

namespace
{
Player *s_instance = nullptr;

enum Observed : quint64 {
    PauseProperty = 1,
    TimeProperty,
    DurationProperty,
    SpeedProperty,
    VolumeProperty,
    MuteProperty,
    EofProperty,
    TracksProperty,
    ChaptersProperty,
    ChapterProperty,
    VideoParamsProperty,
};

constexpr double minimumSpeed = 0.5;
constexpr double maximumSpeed = 2.0;
constexpr double speedStep = 0.25;

QVariant toVariant(const mpv_node *node)
{
    switch (node->format) {
    case MPV_FORMAT_STRING:
        return QString::fromUtf8(node->u.string);
    case MPV_FORMAT_FLAG:
        return bool(node->u.flag);
    case MPV_FORMAT_INT64:
        return qlonglong(node->u.int64);
    case MPV_FORMAT_DOUBLE:
        return node->u.double_;
    case MPV_FORMAT_NODE_ARRAY: {
        QVariantList list;
        for (int i = 0; i < node->u.list->num; ++i) {
            list.append(toVariant(&node->u.list->values[i]));
        }
        return list;
    }
    case MPV_FORMAT_NODE_MAP: {
        QVariantMap map;
        for (int i = 0; i < node->u.list->num; ++i) {
            map.insert(QString::fromUtf8(node->u.list->keys[i]), toVariant(&node->u.list->values[i]));
        }
        return map;
    }
    default:
        return {};
    }
}

// Fills `node` from a plain value; strings are kept alive in `store` for
// as long as mpv needs them, which is until the call returns.
void toNode(const QVariant &value, mpv_node *node, std::list<QByteArray> &store)
{
    switch (value.typeId()) {
    case QMetaType::Bool:
        node->format = MPV_FORMAT_FLAG;
        node->u.flag = value.toBool() ? 1 : 0;
        break;
    case QMetaType::Int:
    case QMetaType::LongLong:
    case QMetaType::UInt:
    case QMetaType::ULongLong:
        node->format = MPV_FORMAT_INT64;
        node->u.int64 = value.toLongLong();
        break;
    case QMetaType::Double:
    case QMetaType::Float:
        node->format = MPV_FORMAT_DOUBLE;
        node->u.double_ = value.toDouble();
        break;
    default:
        store.push_back(value.toString().toUtf8());
        node->format = MPV_FORMAT_STRING;
        node->u.string = store.back().data();
        break;
    }
}

// The picture shapes the menu offers; anything else saved is ignored.
QStringList shapes()
{
    return {QStringLiteral("16:9"), QStringLiteral("4:3"), QStringLiteral("21:9")};
}

QString languageName(const QString &code)
{
    if (code.isEmpty() || code == QLatin1String("und")) {
        return {};
    }
    const QLocale::Language language = QLocale::codeToLanguage(code, QLocale::AnyLanguageCode);
    if (language == QLocale::AnyLanguage || language == QLocale::C) {
        return code;
    }
    return QLocale::languageToString(language);
}
}

Player::Player(QObject *parent)
    : QObject(parent)
{
    s_instance = this;

    QSettings settings;
    m_volume = std::clamp(settings.value(QStringLiteral("Player/volume"), 100).toInt(), 0, 100);
    m_muted = settings.value(QStringLiteral("Player/muted"), false).toBool();
    m_speed = std::clamp(std::round(settings.value(QStringLiteral("Player/speed"), 1.0).toDouble() / speedStep) * speedStep, minimumSpeed, maximumSpeed);
    m_aspect = settings.value(QStringLiteral("Player/aspect")).toString();
    if (!shapes().contains(m_aspect)) {
        m_aspect.clear();
    }

    m_mpv = mpv_create();
    if (!m_mpv) {
        qFatal("Videos could not start its player (libmpv).");
    }
    // Only the file itself is read: no user config or scripts, no online
    // lookups (ytdl), and none of mpv's own keys or on-screen controls,
    // which Videos draws itself.
    const char *const options[][2] = {
        {"config", "no"},
        {"load-scripts", "no"},
        {"ytdl", "no"},
        {"osc", "no"},
        {"input-default-bindings", "no"},
        {"input-vo-keyboard", "no"},
        {"terminal", "no"},
        {"msg-level", "all=error"},
        {"vo", "libmpv"},
        {"hwdec", "auto-safe"},
        {"idle", "yes"},
        // Stops on the last frame instead of closing the file, so the end
        // of a video stays on screen and Play starts it again.
        {"keep-open", "yes"},
        {"sub-auto", "fuzzy"},
        {"sub-font", "Albert Sans"},
        {"audio-client-name", "wildos-videos"},
        // No sound device (or one that will not open) still shows the
        // picture.
        {"audio-fallback-to-null", "yes"},
        {"audio-display", "no"},
    };
    for (const auto &option : options) {
        mpv_set_option_string(m_mpv, option[0], option[1]);
    }
    // WILDOS_VIDEOS_MPV_LOG=1 prints mpv's own log, to see why a file
    // will not play.
    if (qEnvironmentVariableIntValue("WILDOS_VIDEOS_MPV_LOG") > 0) {
        mpv_set_option_string(m_mpv, "terminal", "yes");
        mpv_set_option_string(m_mpv, "msg-level", "all=v");
    }
    mpv_set_option_string(m_mpv, "volume", QByteArray::number(m_volume).constData());
    mpv_set_option_string(m_mpv, "mute", m_muted ? "yes" : "no");
    mpv_set_option_string(m_mpv, "speed", QByteArray::number(m_speed, 'f', 2).constData());
    if (!m_aspect.isEmpty()) {
        mpv_set_option_string(m_mpv, "video-aspect-override", m_aspect.toLatin1().constData());
    }

    if (mpv_initialize(m_mpv) < 0) {
        qFatal("Videos could not start its player (libmpv).");
    }
    mpv_observe_property(m_mpv, PauseProperty, "pause", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, TimeProperty, "time-pos", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, DurationProperty, "duration", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, SpeedProperty, "speed", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, VolumeProperty, "volume", MPV_FORMAT_DOUBLE);
    mpv_observe_property(m_mpv, MuteProperty, "mute", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, EofProperty, "eof-reached", MPV_FORMAT_FLAG);
    mpv_observe_property(m_mpv, TracksProperty, "track-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, ChaptersProperty, "chapter-list", MPV_FORMAT_NODE);
    mpv_observe_property(m_mpv, ChapterProperty, "chapter", MPV_FORMAT_INT64);
    mpv_observe_property(m_mpv, VideoParamsProperty, "video-params", MPV_FORMAT_NODE);
    mpv_set_wakeup_callback(m_mpv, &Player::wakeup, this);

    m_positionClock.start();
    // The place in a video is noted now and then as it plays, so a crash
    // or a power cut loses little.
    m_remember.setInterval(10000);
    connect(&m_remember, &QTimer::timeout, this, &Player::rememberPosition);
    connect(this, &Player::playingChanged, this, [this] {
        if (playing()) {
            m_remember.start();
        } else {
            m_remember.stop();
            rememberPosition();
        }
    });
    m_settings.setSingleShot(true);
    m_settings.setInterval(1000);
    connect(&m_settings, &QTimer::timeout, this, &Player::saveSettings);
    connect(QCoreApplication::instance(), &QCoreApplication::aboutToQuit, this, [this] {
        rememberPosition();
        Positions::instance()->save();
        saveSettings();
    });
    connect(Library::instance(), &Library::folderChanged, this, [this](const QString &changed) {
        if (changed == folder()) {
            readFolder();
        }
    });
}

Player::~Player()
{
    // VideoOutput's render context has gone with the windows by now; the
    // core goes last, as mpv requires.
    s_instance = nullptr;
    if (m_mpv) {
        mpv_set_wakeup_callback(m_mpv, nullptr, nullptr);
        mpv_terminate_destroy(m_mpv);
        m_mpv = nullptr;
    }
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

void Player::wakeup(void *context)
{
    QMetaObject::invokeMethod(static_cast<Player *>(context), "processEvents", Qt::QueuedConnection);
}

void Player::frameWaiting(void *context)
{
    // Called from mpv's own thread; the signal is queued to the GUI thread.
    QMetaObject::invokeMethod(static_cast<Player *>(context), &Player::frameReady, Qt::QueuedConnection);
}

void Player::processEvents()
{
    while (m_mpv) {
        mpv_event *event = mpv_wait_event(m_mpv, 0);
        if (event->event_id == MPV_EVENT_NONE) {
            break;
        }
        handleEvent(event);
    }
}

void Player::handleEvent(const mpv_event *event)
{
    switch (event->event_id) {
    case MPV_EVENT_PROPERTY_CHANGE: {
        const auto *property = static_cast<const mpv_event_property *>(event->data);
        const bool has = property->format != MPV_FORMAT_NONE && property->data;
        switch (event->reply_userdata) {
        case PauseProperty: {
            const bool paused = has ? *static_cast<int *>(property->data) != 0 : true;
            if (paused != m_paused) {
                m_paused = paused;
                setPositionValue(m_position, true);
                Q_EMIT playingChanged();
            }
            break;
        }
        case TimeProperty:
            setPositionValue(has ? *static_cast<double *>(property->data) : 0, false);
            break;
        case DurationProperty: {
            const double duration = has ? *static_cast<double *>(property->data) : 0;
            if (duration != m_duration) {
                m_duration = duration;
                Q_EMIT durationChanged();
            }
            break;
        }
        case SpeedProperty:
            if (has && *static_cast<double *>(property->data) != m_speed) {
                m_speed = *static_cast<double *>(property->data);
                Q_EMIT speedChanged();
            }
            break;
        case VolumeProperty:
            if (has && int(std::lround(*static_cast<double *>(property->data))) != m_volume) {
                m_volume = int(std::lround(*static_cast<double *>(property->data)));
                Q_EMIT volumeChanged();
                m_settings.start();
            }
            break;
        case MuteProperty:
            if (has && (*static_cast<int *>(property->data) != 0) != m_muted) {
                m_muted = *static_cast<int *>(property->data) != 0;
                Q_EMIT mutedChanged();
                m_settings.start();
            }
            break;
        case EofProperty: {
            const bool ended = has && *static_cast<int *>(property->data) != 0;
            if (ended != m_ended) {
                m_ended = ended;
                if (ended && m_duration > 0) {
                    Positions::instance()->markWatched(m_path, m_duration);
                }
                Q_EMIT playingChanged();
                if (ended && m_queued && canNext()) {
                    next();
                }
            }
            break;
        }
        case TracksProperty:
            readTracks(has ? toVariant(static_cast<mpv_node *>(property->data)) : QVariant());
            break;
        case ChaptersProperty:
            readChapters(has ? toVariant(static_cast<mpv_node *>(property->data)) : QVariant());
            break;
        case ChapterProperty: {
            const int chapter = has ? int(*static_cast<qint64 *>(property->data)) : -1;
            if (chapter != m_chapter) {
                m_chapter = chapter;
                Q_EMIT chapterChanged();
            }
            break;
        }
        case VideoParamsProperty: {
            const QVariantMap params = has ? toVariant(static_cast<mpv_node *>(property->data)).toMap() : QVariantMap();
            // The size it is shown at: rotation and the pixel shape applied.
            const int width = params.value(QStringLiteral("dw")).toInt();
            const int height = params.value(QStringLiteral("dh")).toInt();
            if (width != m_videoWidth || height != m_videoHeight) {
                m_videoWidth = width;
                m_videoHeight = height;
                Q_EMIT videoSizeChanged();
            }
            break;
        }
        }
        break;
    }
    case MPV_EVENT_FILE_LOADED:
        // The remembered place was for this file only.
        mpv_set_property_string(m_mpv, "start", "none");
        if (!m_loaded) {
            m_loaded = true;
            Q_EMIT loadedChanged();
            Q_EMIT playingChanged();
        }
        if (m_resumeAt > 0) {
            Q_EMIT resumed(m_resumeAt);
            m_resumeAt = 0;
        }
        break;
    case MPV_EVENT_PLAYBACK_RESTART:
        setPositionValue(m_position, true);
        Q_EMIT seeked(m_position);
        break;
    case MPV_EVENT_END_FILE: {
        const auto *end = static_cast<const mpv_event_end_file *>(event->data);
        if (end->reason == MPV_END_FILE_REASON_ERROR) {
            m_loaded = false;
            Q_EMIT loadedChanged();
            Q_EMIT playingChanged();
            Q_EMIT failed(tr("%1 couldn't be played. The file may be damaged, or in a format Videos can't read.").arg(fileName()));
        }
        break;
    }
    default:
        break;
    }
}

void Player::setPositionValue(double position, bool force)
{
    m_position = position;
    // A position comes with every frame; the seek bar and the clock need
    // a tenth of that.
    if (force || m_positionClock.elapsed() >= 100) {
        m_positionClock.restart();
        Q_EMIT positionChanged();
    }
}

void Player::readTracks(const QVariant &list)
{
    QVariantList audio;
    QVariantList subtitles;
    int audioId = -1;
    int subtitleId = 0;
    bool video = false;
    int audioNumber = 0;
    int subtitleNumber = 0;
    for (const QVariant &item : list.toList()) {
        const QVariantMap track = item.toMap();
        const QString type = track.value(QStringLiteral("type")).toString();
        const int id = track.value(QStringLiteral("id")).toInt();
        const bool selected = track.value(QStringLiteral("selected")).toBool();
        const QString title = track.value(QStringLiteral("title")).toString();
        const QString language = languageName(track.value(QStringLiteral("lang")).toString());
        if (type == QLatin1String("video")) {
            video = video || !track.value(QStringLiteral("albumart")).toBool();
            continue;
        }
        QString label;
        if (!language.isEmpty() && !title.isEmpty() && title != language) {
            label = language + QStringLiteral(" · ") + title;
        } else if (!language.isEmpty()) {
            label = language;
        } else if (!title.isEmpty()) {
            label = title;
        }
        if (type == QLatin1String("audio")) {
            ++audioNumber;
            if (label.isEmpty()) {
                label = tr("Track %1").arg(audioNumber);
            }
            audio.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("label"), label}, {QStringLiteral("selected"), selected}});
            if (selected) {
                audioId = id;
            }
        } else if (type == QLatin1String("sub")) {
            ++subtitleNumber;
            if (track.value(QStringLiteral("external")).toBool()) {
                // mpv titles a file beside the video by its format ("srt");
                // its name says more, with the language if the name has one.
                const QString file = QFileInfo(track.value(QStringLiteral("external-filename")).toString()).fileName();
                label = language.isEmpty() ? file : tr("%1, from %2").arg(language, file);
            } else if (label.isEmpty()) {
                label = tr("Subtitles %1").arg(subtitleNumber);
            }
            subtitles.append(QVariantMap{{QStringLiteral("id"), id}, {QStringLiteral("label"), label}, {QStringLiteral("selected"), selected}});
            if (selected) {
                subtitleId = id;
            }
        }
    }
    m_audioTracks = audio;
    m_subtitleTracks = subtitles;
    m_audioTrack = audioId;
    m_subtitleTrack = subtitleId;
    m_hasVideo = video;
    Q_EMIT tracksChanged();
}

void Player::readChapters(const QVariant &list)
{
    QVariantList chapters;
    int number = 0;
    for (const QVariant &item : list.toList()) {
        const QVariantMap chapter = item.toMap();
        ++number;
        QString title = chapter.value(QStringLiteral("title")).toString();
        if (title.isEmpty()) {
            title = tr("Chapter %1").arg(number);
        }
        chapters.append(QVariantMap{
            {QStringLiteral("title"), title},
            {QStringLiteral("time"), chapter.value(QStringLiteral("time")).toDouble()},
            {QStringLiteral("number"), number},
        });
    }
    m_chapters = chapters;
    Q_EMIT chaptersChanged();
}

void Player::readFolder()
{
    // Opening a video that is not in the queue leaves the queue behind.
    const bool wasQueued = m_queued;
    if (!m_queue.contains(m_path)) {
        m_queue.clear();
    }
    m_queued = !m_queue.isEmpty();
    const QStringList videos = m_queued ? m_queue : Library::instance()->videosIn(folder());
    if (videos != m_folderVideos || wasQueued != m_queued) {
        m_folderVideos = videos;
        Q_EMIT folderVideosChanged();
    }
}

QString Player::title() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).completeBaseName();
}

QString Player::fileName() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).fileName();
}

QString Player::folder() const
{
    return m_path.isEmpty() ? QString() : QFileInfo(m_path).absolutePath();
}

QString Player::place() const
{
    return Library::instance()->placeOf(folder());
}

QString Player::quality() const
{
    const int side = std::min(m_videoWidth, m_videoHeight);
    if (side <= 0) {
        return {};
    }
    if (side >= 4320) {
        return QStringLiteral("8K");
    }
    if (side >= 2160) {
        return QStringLiteral("4K");
    }
    return QStringLiteral("%1p").arg(side);
}

bool Player::canNext() const
{
    const qsizetype index = m_folderVideos.indexOf(m_path);
    return index >= 0 && index + 1 < m_folderVideos.size();
}

bool Player::canPrevious() const
{
    return m_folderVideos.indexOf(m_path) > 0;
}

void Player::open(const QString &pathOrUrl)
{
    const QUrl url(pathOrUrl);
    QString path = url.isLocalFile() ? url.toLocalFile() : pathOrUrl;
    if (path.contains(QLatin1String("://"))) {
        Q_EMIT failed(tr("Videos plays files on this PC only."));
        return;
    }
    path = QFileInfo(path).absoluteFilePath();
    if (!QFileInfo(path).isFile()) {
        Q_EMIT failed(tr("%1 isn't there any more. It may have been moved or deleted.").arg(QFileInfo(path).fileName()));
        return;
    }
    if (path == m_path && m_loaded) {
        play();
        return;
    }
    rememberPosition();

    m_resumeAt = Positions::instance()->resumeAt(path);
    mpv_set_property_string(m_mpv, "start", m_resumeAt > 0 ? QByteArray("+" + QByteArray::number(m_resumeAt, 'f', 3)).constData() : "none");
    m_path = path;
    m_loaded = false;
    m_ended = false;
    m_position = 0;
    m_duration = 0;
    m_chapters.clear();
    m_chapter = -1;
    Q_EMIT fileChanged();
    Q_EMIT loadedChanged();
    Q_EMIT positionChanged();
    Q_EMIT durationChanged();
    Q_EMIT chaptersChanged();
    Q_EMIT chapterChanged();
    readFolder();
    Library::instance()->watch(folder());

    if (m_renderReady) {
        load();
    }
}

void Player::openFiles(const QStringList &pathsOrUrls)
{
    if (pathsOrUrls.isEmpty()) {
        return;
    }
    QStringList files;
    for (const QString &item : pathsOrUrls) {
        const QUrl url(item);
        const QFileInfo info(url.isLocalFile() ? url.toLocalFile() : item);
        if (info.isDir()) {
            files.append(Library::instance()->videosIn(info.absoluteFilePath()));
        } else if (info.isFile()) {
            files.append(info.absoluteFilePath());
        }
    }
    files.removeDuplicates();
    if (files.size() < 2) {
        // One file, or nothing that can be played: open() says why.
        open(files.isEmpty() ? pathsOrUrls.first() : files.first());
        return;
    }
    m_queue = files;
    open(files.first());
    // open() does nothing more for the video already playing.
    readFolder();
}

void Player::load()
{
    command({QStringLiteral("loadfile"), m_path, QStringLiteral("replace")});
    setMpv("pause", false);
}

void Player::setRenderReady()
{
    // mpv picks its video output as a file starts; a file started before
    // VideoOutput had a render context would play without a picture.
    if (m_renderReady) {
        return;
    }
    m_renderReady = true;
    if (!m_path.isEmpty()) {
        load();
    }
}

void Player::play()
{
    if (m_path.isEmpty()) {
        return;
    }
    if (m_ended) {
        seek(0);
    }
    setMpv("pause", false);
}

void Player::pause()
{
    setMpv("pause", true);
}

void Player::playPause()
{
    if (playing()) {
        pause();
    } else {
        play();
    }
}

void Player::stop()
{
    pause();
    seek(0);
}

void Player::close()
{
    if (m_path.isEmpty()) {
        return;
    }
    rememberPosition();
    mpv_set_property_string(m_mpv, "start", "none");
    command({QStringLiteral("stop")});
    m_path.clear();
    m_loaded = false;
    m_ended = false;
    m_position = 0;
    m_duration = 0;
    m_resumeAt = 0;
    m_chapters.clear();
    m_chapter = -1;
    m_folderVideos.clear();
    m_queue.clear();
    m_queued = false;
    Q_EMIT fileChanged();
    Q_EMIT loadedChanged();
    Q_EMIT playingChanged();
    Q_EMIT positionChanged();
    Q_EMIT durationChanged();
    Q_EMIT chaptersChanged();
    Q_EMIT chapterChanged();
    Q_EMIT folderVideosChanged();
}

void Player::seek(double seconds)
{
    if (!m_loaded) {
        return;
    }
    const double to = m_duration > 0 ? std::clamp(seconds, 0.0, m_duration) : std::max(0.0, seconds);
    setPositionValue(to, true);
    command({QStringLiteral("seek"), to, QStringLiteral("absolute+exact")});
}

void Player::skip(double seconds)
{
    seek(m_position + seconds);
}

void Player::next()
{
    const qsizetype index = m_folderVideos.indexOf(m_path);
    if (index >= 0 && index + 1 < m_folderVideos.size()) {
        open(m_folderVideos.at(index + 1));
    }
}

void Player::previous()
{
    // As every player does: back to the start first, then the one before.
    if (m_position > 5) {
        seek(0);
        return;
    }
    const qsizetype index = m_folderVideos.indexOf(m_path);
    if (index > 0) {
        open(m_folderVideos.at(index - 1));
    }
}

void Player::setAudioTrack(int id)
{
    setMpv("aid", id > 0 ? QVariant(qlonglong(id)) : QVariant(QStringLiteral("no")));
}

void Player::setSubtitleTrack(int id)
{
    setMpv("sid", id > 0 ? QVariant(qlonglong(id)) : QVariant(QStringLiteral("no")));
}

void Player::cycleSubtitles()
{
    if (m_subtitleTracks.isEmpty()) {
        return;
    }
    int index = -1;
    for (qsizetype i = 0; i < m_subtitleTracks.size(); ++i) {
        if (m_subtitleTracks.at(i).toMap().value(QStringLiteral("id")).toInt() == m_subtitleTrack) {
            index = int(i);
        }
    }
    const int next = index + 1;
    setSubtitleTrack(next < m_subtitleTracks.size() ? m_subtitleTracks.at(next).toMap().value(QStringLiteral("id")).toInt() : 0);
}

void Player::setSpeed(double speed)
{
    speed = std::clamp(std::round(speed / speedStep) * speedStep, minimumSpeed, maximumSpeed);
    if (speed != m_speed) {
        m_speed = speed;
        Q_EMIT speedChanged();
        m_settings.start();
    }
    setMpv("speed", speed);
}

void Player::stepSpeed(int steps)
{
    setSpeed(m_speed + steps * speedStep);
}

void Player::setVolume(int volume)
{
    volume = std::clamp(volume, 0, 100);
    if (volume != m_volume) {
        m_volume = volume;
        Q_EMIT volumeChanged();
        m_settings.start();
    }
    setMpv("volume", double(volume));
}

void Player::stepVolume(int steps)
{
    if (m_muted && steps > 0) {
        setMuted(false);
    }
    setVolume(m_volume + steps * 5);
}

void Player::setMuted(bool muted)
{
    if (muted != m_muted) {
        m_muted = muted;
        Q_EMIT mutedChanged();
        m_settings.start();
    }
    setMpv("mute", muted);
}

void Player::setAspect(const QString &aspect)
{
    if (aspect == m_aspect) {
        return;
    }
    m_aspect = aspect;
    Q_EMIT aspectChanged();
    m_settings.start();
    setMpv("video-aspect-override", aspect.isEmpty() ? QStringLiteral("no") : aspect);
}

QString Player::chapterAt(double seconds) const
{
    const int index = chapterIndexAt(seconds);
    return index >= 0 ? m_chapters.at(index).toMap().value(QStringLiteral("title")).toString() : QString();
}

int Player::chapterIndexAt(double seconds) const
{
    int found = -1;
    for (qsizetype i = 0; i < m_chapters.size(); ++i) {
        if (m_chapters.at(i).toMap().value(QStringLiteral("time")).toDouble() <= seconds + 0.001) {
            found = int(i);
        }
    }
    return found;
}

void Player::rememberPosition()
{
    if (!m_path.isEmpty() && m_loaded && m_duration > 0) {
        Positions::instance()->remember(m_path, m_ended ? m_duration : m_position, m_duration);
    }
}

void Player::saveSettings()
{
    m_settings.stop();
    QSettings settings;
    settings.setValue(QStringLiteral("Player/volume"), m_volume);
    settings.setValue(QStringLiteral("Player/muted"), m_muted);
    settings.setValue(QStringLiteral("Player/speed"), m_speed);
    settings.setValue(QStringLiteral("Player/aspect"), m_aspect);
}

void Player::setMpv(const char *name, const QVariant &value)
{
    std::list<QByteArray> store;
    mpv_node node;
    toNode(value, &node, store);
    mpv_set_property_async(m_mpv, 0, name, MPV_FORMAT_NODE, &node);
}

void Player::command(const QVariantList &arguments)
{
    std::list<QByteArray> store;
    QList<mpv_node> values(arguments.size());
    for (qsizetype i = 0; i < arguments.size(); ++i) {
        toNode(arguments.at(i), &values[i], store);
    }
    mpv_node_list list;
    list.num = int(values.size());
    list.values = values.data();
    list.keys = nullptr;
    mpv_node node;
    node.format = MPV_FORMAT_NODE_ARRAY;
    node.u.list = &list;
    mpv_command_node_async(m_mpv, 0, &node);
}
