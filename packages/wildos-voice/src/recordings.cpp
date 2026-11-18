#include "recordings.h"

#include "audio.h"
#include "paths.h"
#include "sidecar.h"

#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QFutureWatcher>
#include <QPointer>
#include <QProcess>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QUrl>
#include <QtConcurrent>

namespace
{
Recordings *s_instance = nullptr;

QVariantMap describe(const QString &path)
{
    const QFileInfo file(path);
    const Sidecar sidecar = Sidecar::read(path);
    QVariantMap item;
    item.insert(QStringLiteral("path"), file.absoluteFilePath());
    item.insert(QStringLiteral("title"), file.completeBaseName());
    QDateTime created = sidecar.created;
    if (!created.isValid()) {
        created = file.birthTime().isValid() ? file.birthTime() : file.lastModified();
    }
    item.insert(QStringLiteral("created"), created.toLocalTime());
    qint64 duration = sidecar.durationMs;
    if (duration < 0) {
        duration = Audio::opusDuration(path);
    }
    item.insert(QStringLiteral("duration"), double(std::max<qint64>(0, duration)));
    QVariantList markers;
    for (qint64 marker : sidecar.markers) {
        markers.append(double(marker));
    }
    item.insert(QStringLiteral("markers"), markers);
    QFile transcript(VoicePaths::transcriptOf(path));
    const bool hasTranscript = transcript.exists() && transcript.size() > 0;
    item.insert(QStringLiteral("hasTranscript"), hasTranscript);
    // For the search; the transcript card reads the file itself.
    if (hasTranscript && transcript.size() < 2 * 1024 * 1024 && transcript.open(QIODevice::ReadOnly)) {
        item.insert(QStringLiteral("transcript"), QString::fromUtf8(transcript.readAll()));
    }
    return item;
}

QVariantList scan(const QString &folder)
{
    QList<QVariantMap> found;
    const QFileInfoList files = QDir(folder).entryInfoList({QStringLiteral("*.opus")}, QDir::Files | QDir::Readable);
    for (const QFileInfo &file : files) {
        found.append(describe(file.absoluteFilePath()));
    }
    std::sort(found.begin(), found.end(), [](const QVariantMap &a, const QVariantMap &b) {
        return a.value(QStringLiteral("created")).toDateTime() > b.value(QStringLiteral("created")).toDateTime();
    });
    QVariantList items;
    for (const QVariantMap &item : found) {
        items.append(item);
    }
    return items;
}

// Characters a file name can't have, or that would hide or misplace it.
bool usableTitle(const QString &title)
{
    return !title.isEmpty() && title.size() <= 200 && !title.startsWith(QLatin1Char('.')) && !title.contains(QLatin1Char('/'))
        && !title.contains(QChar(0));
}
}

Recordings::Recordings(QObject *parent)
    : QObject(parent)
{
    s_instance = this;
    m_refreshTimer.setSingleShot(true);
    m_refreshTimer.setInterval(250);
    connect(&m_refreshTimer, &QTimer::timeout, this, &Recordings::refresh);
    connect(&m_watcher, &QFileSystemWatcher::directoryChanged, this, &Recordings::scheduleRefresh);
    refresh();
}

Recordings *Recordings::instance()
{
    if (!s_instance) {
        new Recordings(qApp);
    }
    return s_instance;
}

Recordings *Recordings::create(QQmlEngine *, QJSEngine *)
{
    QQmlEngine::setObjectOwnership(instance(), QQmlEngine::CppOwnership);
    return instance();
}

QString Recordings::folder() const
{
    return VoicePaths::recordingsFolder();
}

void Recordings::scheduleRefresh()
{
    m_refreshTimer.start();
}

void Recordings::refresh()
{
    if (m_scanning) {
        m_rescan = true;
        return;
    }
    const QString dir = folder();
    // Until the Recordings folder exists, the home folder is watched for it.
    if (!m_watcher.directories().contains(dir)) {
        if (QFileInfo(dir).isDir()) {
            if (!m_watcher.directories().isEmpty()) {
                m_watcher.removePaths(m_watcher.directories());
            }
            m_watcher.addPath(dir);
        } else if (!m_watcher.directories().contains(QDir::homePath())) {
            m_watcher.addPath(QDir::homePath());
        }
    }
    m_scanning = true;
    auto *watcher = new QFutureWatcher<QVariantList>(this);
    connect(watcher, &QFutureWatcher<QVariantList>::finished, this, [this, watcher]() {
        m_items = watcher->result();
        watcher->deleteLater();
        m_loaded = true;
        m_scanning = false;
        Q_EMIT itemsChanged();
        if (m_rescan) {
            m_rescan = false;
            scheduleRefresh();
        }
    });
    watcher->setFuture(QtConcurrent::run(scan, dir));
}

QVariantMap Recordings::info(const QString &path) const
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    for (const QVariant &item : m_items) {
        const QVariantMap map = item.toMap();
        if (map.value(QStringLiteral("path")).toString() == absolute) {
            return map;
        }
    }
    if (!QFileInfo::exists(absolute)) {
        return {};
    }
    return describe(absolute);
}

QString Recordings::nextTitle() const
{
    static const QRegularExpression numbered(QStringLiteral("^Recording (\\d+)\\.(opus|json|txt)$"));
    const QString base = QCoreApplication::translate("Recordings", "Recording");
    int highest = 0;
    for (const QString &name : QDir(folder()).entryList(QDir::Files)) {
        const QRegularExpressionMatch match = numbered.match(name);
        if (match.hasMatch()) {
            highest = std::max(highest, match.captured(1).toInt());
        }
    }
    QString title;
    for (int n = highest + 1;; ++n) {
        title = base + QLatin1Char(' ') + QString::number(n);
        if (!QFileInfo::exists(QDir(folder()).filePath(title + QStringLiteral(".opus")))) {
            return title;
        }
    }
}

QString Recordings::newRecordingPath(QString *title)
{
    if (!QDir().mkpath(folder())) {
        return QString();
    }
    *title = nextTitle();
    return QDir(folder()).filePath(*title + QStringLiteral(".opus"));
}

bool Recordings::rename(const QString &path, const QString &title)
{
    const QString name = title.trimmed();
    if (!usableTitle(name)) {
        setError(tr("A name can't be empty, start with a full stop or have a / in it."));
        return false;
    }
    const QFileInfo file(path);
    if (name == file.completeBaseName()) {
        return true;
    }
    const QDir dir = file.absoluteDir();
    const QString target = dir.filePath(name + QStringLiteral(".opus"));
    if (QFileInfo::exists(target) || QFileInfo::exists(VoicePaths::sidecarOf(target)) || QFileInfo::exists(VoicePaths::transcriptOf(target))) {
        setError(tr("There's already a recording called %1.").arg(name));
        return false;
    }
    if (!QFile::rename(path, target)) {
        setError(tr("%1 couldn't be renamed.").arg(file.completeBaseName()));
        return false;
    }
    QFile::rename(VoicePaths::transcriptOf(path), VoicePaths::transcriptOf(target));
    Sidecar sidecar = Sidecar::read(path);
    if (QFile::rename(VoicePaths::sidecarOf(path), VoicePaths::sidecarOf(target))) {
        sidecar.title = name;
        sidecar.write(target);
    }
    if (m_waveforms.contains(path)) {
        m_waveforms.insert(target, m_waveforms.take(path));
    }
    Q_EMIT renamed(path, target);
    refresh();
    return true;
}

bool Recordings::remove(const QString &path)
{
    const QString title = QFileInfo(path).completeBaseName();
    QString binned;
    if (!QFile::moveToTrash(path, &binned)) {
        setError(tr("%1 couldn't be moved to the bin.").arg(title));
        return false;
    }
    m_binned.clear();
    m_binned.append(std::make_pair(path, binned));
    for (const QString &extra : {VoicePaths::sidecarOf(path), VoicePaths::transcriptOf(path)}) {
        if (QFileInfo::exists(extra) && QFile::moveToTrash(extra, &binned)) {
            m_binned.append(std::make_pair(extra, binned));
        }
    }
    m_waveforms.remove(path);
    Q_EMIT removed(path);
    refresh();
    return true;
}

bool Recordings::restore(const QString &path)
{
    const auto files = m_binned;
    m_binned.clear();
    // The bin keeps each file in "files" and a note about it in "info".
    const auto takeBack = [](const QString &original, const QString &held) {
        if (held.isEmpty() || !QFileInfo::exists(held) || QFileInfo::exists(original) || !QFile::rename(held, original)) {
            return false;
        }
        const QFileInfo file(held);
        QFile::remove(file.absoluteDir().absoluteFilePath(QStringLiteral("../info/") + file.fileName() + QStringLiteral(".trashinfo")));
        return true;
    };
    if (files.isEmpty() || files.first().first != path || !takeBack(files.first().first, files.first().second)) {
        setError(tr("%1 couldn't be brought back from the bin.").arg(QFileInfo(path).completeBaseName()));
        return false;
    }
    for (qsizetype i = 1; i < files.size(); ++i) {
        takeBack(files.at(i).first, files.at(i).second);
    }
    refresh();
    return true;
}

void Recordings::showInFiles(const QString &path) const
{
    // Wild OS Files selects the recording in its folder. Not through
    // org.freedesktop.FileManager1: Kicksecure's fm-shim-backend holds that
    // name and shows nothing.
    const QString files = QStandardPaths::findExecutable(QStringLiteral("wildos-files"));
    if (!files.isEmpty() && QProcess::startDetached(files, {QStringLiteral("--select"), path})) {
        return;
    }
    QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(path).absolutePath()));
}

void Recordings::loadWaveform(const QString &path)
{
    if (m_waveforms.contains(path)) {
        const QVariantList bars = m_waveforms.value(path);
        Q_EMIT waveformReady(path, bars, double(bars.size()) * Audio::barMs);
        return;
    }
    struct Result
    {
        QVariantList bars;
        qint64 duration = 0;
    };
    auto *watcher = new QFutureWatcher<Result>(this);
    connect(watcher, &QFutureWatcher<Result>::finished, this, [this, watcher, path]() {
        const Result result = watcher->result();
        watcher->deleteLater();
        if (m_waveforms.size() > 12) {
            m_waveforms.clear();
        }
        m_waveforms.insert(path, result.bars);
        Q_EMIT waveformReady(path, result.bars, double(result.duration));
    });
    watcher->setFuture(QtConcurrent::run([path]() {
        Result result;
        QList<float> bars;
        Audio::decodeOpus(path, nullptr, &bars, &result.duration, nullptr);
        result.bars.reserve(bars.size());
        for (float bar : bars) {
            result.bars.append(bar);
        }
        return result;
    }));
}

void Recordings::forgetWaveform(const QString &path)
{
    m_waveforms.remove(path);
}

void Recordings::setError(const QString &message)
{
    m_lastError = message;
    Q_EMIT lastErrorChanged();
}
