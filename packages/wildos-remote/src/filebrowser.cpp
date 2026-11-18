#include "filebrowser.h"
#include "clockformat.h"
#include "paths.h"
#include "secrets.h"

#include <QCoreApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFileSystemWatcher>
#include <QLocale>
#include <QPointer>
#include <QUrl>

#include <algorithm>

namespace
{
QString sizeText(qint64 bytes)
{
    if (bytes < 1024 * 1024) {
        return QCoreApplication::translate("FileBrowser", "%1 KB").arg(std::max<qint64>(1, (bytes + 1023) / 1024));
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return QCoreApplication::translate("FileBrowser", "%1 MB").arg(QLocale::system().toString(bytes / (1024.0 * 1024.0), 'f', bytes < 10LL * 1024 * 1024 ? 1 : 0));
    }
    return QCoreApplication::translate("FileBrowser", "%1 GB").arg(QLocale::system().toString(bytes / (1024.0 * 1024.0 * 1024.0), 'f', 1));
}

QString whenText(qint64 secs)
{
    if (secs <= 0) {
        return {};
    }
    const QDateTime t = QDateTime::fromSecsSinceEpoch(secs);
    const QDate today = QDate::currentDate();
    if (t.date() == today) {
        return QCoreApplication::translate("FileBrowser", "Today, %1").arg(ClockFormat::time(t.time()));
    }
    if (t.date() == today.addDays(-1)) {
        return QCoreApplication::translate("FileBrowser", "Yesterday");
    }
    return QLocale::system().toString(t.date(), QLocale::ShortFormat);
}

QString secondsText(qint64 s)
{
    if (s < 60) {
        return s <= 1 ? QCoreApplication::translate("FileBrowser", "about a second left") : QCoreApplication::translate("FileBrowser", "about %1 seconds left").arg(s);
    }
    const qint64 m = (s + 30) / 60;
    return m == 1 ? QCoreApplication::translate("FileBrowser", "about a minute left") : QCoreApplication::translate("FileBrowser", "about %1 minutes left").arg(m);
}
}

FileBrowser::FileBrowser(const QString &hostName, QObject *parent)
    : QObject(parent)
    , m_hostName(hostName)
    , m_sftp(new Sftp(this))
{
    m_localPath = QDir::homePath();
    listLocal();
    connect(m_sftp, &Sftp::readyChanged, this, [this] {
        if (m_sftp->ready()) {
            m_remoteError.clear();
            if (m_remotePath.isEmpty()) {
                m_sftp->realPath(QStringLiteral("."), [this](const QString &path, const QString &) {
                    m_remotePath = path.isEmpty() ? QStringLiteral("/") : path;
                    listRemote();
                });
            } else {
                listRemote();
            }
        }
        Q_EMIT remoteChanged();
    });
    connect(m_sftp, &Sftp::failed, this, [this](const QString &error) {
        m_loading = false;
        // ssh's own words, but plainer for the one everyone meets.
        m_remoteError = error.contains(QLatin1String("Control socket")) || error.contains(QLatin1String("Permission denied"))
                            ? tr("Files use the terminal's connection. Sign in to %1 in the terminal first, then try again.").arg(m_hostName)
                            : error;
        Q_EMIT remoteChanged();
    });
    m_progressTimer.setInterval(250);
    connect(&m_progressTimer, &QTimer::timeout, this, &FileBrowser::transfersChanged);
    m_saveTimer.setSingleShot(true);
    m_saveTimer.setInterval(600);
    connect(&m_saveTimer, &QTimer::timeout, this, [this] {
        const QStringList changed = std::exchange(m_changed, {});
        for (const QString &local : changed) {
            const QString remote = m_editing.value(local);
            if (remote.isEmpty() || !QFileInfo::exists(local)) {
                continue;
            }
            // Editors that save by replacing the file drop the watch.
            if (!m_watcher->files().contains(local)) {
                m_watcher->addPath(local);
            }
            queueUpload(local, remote, QFileInfo(local).fileName());
        }
    });
}

FileBrowser::~FileBrowser() = default;

void FileBrowser::connectTo(const QStringList &sshArgs, const QString &passwordKey)
{
    m_sshArgs = sshArgs;
    m_passwordKey = passwordKey;
    m_loading = true;
    m_remoteError.clear();
    Q_EMIT remoteChanged();
    if (passwordKey.isEmpty()) {
        m_sftp->start(sshArgs);
        return;
    }
    Secrets::instance()->read(passwordKey, [this, guard = QPointer(this), sshArgs](const QString &secret, bool found) {
        if (!guard || sshArgs != m_sshArgs) {
            return;
        }
        m_sftp->start(sshArgs, found ? std::optional<QString>(secret) : std::nullopt);
    });
}

void FileBrowser::disconnectNow()
{
    m_sftp->stop();
}

void FileBrowser::retry()
{
    if (!m_sshArgs.isEmpty()) {
        connectTo(m_sshArgs, m_passwordKey);
    }
}

QString FileBrowser::localShown() const
{
    const QString home = QDir::homePath();
    if (m_localPath == home) {
        return QStringLiteral("~");
    }
    if (m_localPath.startsWith(home + QLatin1Char('/'))) {
        return QStringLiteral("~") + m_localPath.mid(home.size());
    }
    return m_localPath;
}

QVariantMap FileBrowser::entryMap(const QString &name, bool dir, qint64 size, qint64 mtime)
{
    return {{QStringLiteral("name"), name},
            {QStringLiteral("dir"), dir},
            {QStringLiteral("size"), size},
            {QStringLiteral("sizeText"), dir ? QString() : sizeText(size)},
            {QStringLiteral("modified"), whenText(mtime)},
            {QStringLiteral("hidden"), name.startsWith(QLatin1Char('.'))}};
}

void FileBrowser::listLocal()
{
    QVariantList list;
    const QDir dir(m_localPath);
    for (const QFileInfo &fi : dir.entryInfoList(QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDir::DirsFirst | QDir::Name | QDir::IgnoreCase)) {
        list.append(entryMap(fi.fileName(), fi.isDir(), fi.size(), fi.lastModified().toSecsSinceEpoch()));
    }
    m_localEntries = list;
    Q_EMIT localChanged();
}

void FileBrowser::listRemote()
{
    if (!m_sftp->ready()) {
        return;
    }
    m_loading = true;
    Q_EMIT remoteChanged();
    const QString path = m_remotePath;
    m_sftp->list(path, [this, path](const QList<Sftp::Entry> &entries, const QString &error) {
        if (path != m_remotePath) {
            return;
        }
        m_loading = false;
        if (!error.isEmpty()) {
            Q_EMIT notice(error);
        }
        QList<Sftp::Entry> sorted = entries;
        std::sort(sorted.begin(), sorted.end(), [](const Sftp::Entry &a, const Sftp::Entry &b) {
            if (a.dir != b.dir) {
                return a.dir;
            }
            return a.name.compare(b.name, Qt::CaseInsensitive) < 0;
        });
        QVariantList list;
        for (const Sftp::Entry &e : std::as_const(sorted)) {
            list.append(entryMap(e.name, e.dir, e.size, e.mtime));
        }
        m_remoteEntries = list;
        Q_EMIT remoteChanged();
    });
}

QString FileBrowser::joinRemote(const QString &dir, const QString &name) const
{
    return dir.endsWith(QLatin1Char('/')) ? dir + name : dir + QLatin1Char('/') + name;
}

void FileBrowser::openLocal(const QString &path)
{
    QString target = path;
    if (!QDir::isAbsolutePath(target)) {
        target = QDir(m_localPath).absoluteFilePath(target);
    }
    target = QDir::cleanPath(target);
    if (!QFileInfo(target).isDir()) {
        return;
    }
    m_localHistory.append(m_localPath);
    m_localPath = target;
    listLocal();
}

void FileBrowser::openRemote(const QString &path)
{
    QString target = path.startsWith(QLatin1Char('/')) ? path : joinRemote(m_remotePath, path);
    target = QDir::cleanPath(target);
    m_remoteHistory.append(m_remotePath);
    m_remotePath = target;
    listRemote();
}

void FileBrowser::localBack()
{
    if (!m_localHistory.isEmpty()) {
        m_localPath = m_localHistory.takeLast();
    } else {
        m_localPath = QFileInfo(m_localPath).absolutePath();
    }
    listLocal();
}

void FileBrowser::remoteBack()
{
    if (!m_remoteHistory.isEmpty()) {
        m_remotePath = m_remoteHistory.takeLast();
    } else if (m_remotePath != QLatin1String("/")) {
        m_remotePath = QDir::cleanPath(m_remotePath + QStringLiteral("/.."));
    }
    listRemote();
}

void FileBrowser::refreshLocal()
{
    listLocal();
}

void FileBrowser::refreshRemote()
{
    listRemote();
}

void FileBrowser::newLocalFolder(const QString &name)
{
    if (!name.trimmed().isEmpty() && !QDir(m_localPath).mkdir(name.trimmed())) {
        Q_EMIT notice(tr("The folder couldn't be made here."));
    }
    listLocal();
}

void FileBrowser::newRemoteFolder(const QString &name)
{
    if (name.trimmed().isEmpty()) {
        return;
    }
    m_sftp->mkdir(joinRemote(m_remotePath, name.trimmed()), [this](const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT notice(error);
        }
        listRemote();
    });
}

void FileBrowser::removeRemote(const QStringList &names)
{
    for (const QString &name : names) {
        bool dir = false;
        for (const QVariant &v : std::as_const(m_remoteEntries)) {
            if (v.toMap().value(QStringLiteral("name")).toString() == name) {
                dir = v.toMap().value(QStringLiteral("dir")).toBool();
            }
        }
        const QString path = joinRemote(m_remotePath, name);
        auto done = [this](const QString &error) {
            if (!error.isEmpty()) {
                Q_EMIT notice(error.contains(QLatin1String("Failure")) ? tr("Only empty folders can be deleted from here.") : error);
            }
            listRemote();
        };
        if (dir) {
            m_sftp->rmdir(path, done);
        } else {
            m_sftp->remove(path, done);
        }
    }
}

void FileBrowser::renameRemote(const QString &from, const QString &to)
{
    if (to.trimmed().isEmpty() || to.contains(QLatin1Char('/'))) {
        return;
    }
    m_sftp->rename(joinRemote(m_remotePath, from), joinRemote(m_remotePath, to.trimmed()), [this](const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT notice(error);
        }
        listRemote();
    });
}

void FileBrowser::queueUpload(const QString &local, const QString &remote, const QString &shown)
{
    Job j;
    j.upload = true;
    j.name = shown;
    j.local = local;
    j.remote = remote;
    j.size = QFileInfo(local).size();
    m_jobs.append(j);
    startNext();
}

void FileBrowser::upload(const QStringList &names)
{
    m_cancelled = false;
    for (const QString &name : names) {
        const QString local = QDir(m_localPath).absoluteFilePath(name);
        const QFileInfo fi(local);
        const QString remote = joinRemote(m_remotePath, name);
        if (!fi.isDir()) {
            queueUpload(local, remote, name);
            continue;
        }
        // A folder: its folders are made first, in order, then its files.
        QStringList dirs{QString()};
        QList<QPair<QString, QString>> files;
        QDirIterator it(local, QDir::AllEntries | QDir::NoDotAndDotDot | QDir::Hidden, QDirIterator::Subdirectories);
        while (it.hasNext()) {
            it.next();
            const QString rel = QDir(local).relativeFilePath(it.filePath());
            if (it.fileInfo().isDir()) {
                dirs.append(rel);
            } else if (it.fileInfo().isFile()) {
                files.append({it.filePath(), rel});
            }
        }
        std::sort(dirs.begin(), dirs.end(), [](const QString &a, const QString &b) { return a.count(QLatin1Char('/')) < b.count(QLatin1Char('/')) || (a.count(QLatin1Char('/')) == b.count(QLatin1Char('/')) && a < b); });
        auto remaining = QSharedPointer<int>::create(int(dirs.size()));
        for (const QString &d : std::as_const(dirs)) {
            const QString target = d.isEmpty() ? remote : joinRemote(remote, d);
            // An existing folder is fine: its files are copied into it.
            m_sftp->mkdir(target, [this, remaining, files, remote, name](const QString &) {
                if (--*remaining == 0) {
                    for (const auto &f : files) {
                        queueUpload(f.first, joinRemote(remote, f.second), name + QLatin1Char('/') + f.second);
                    }
                    if (files.isEmpty()) {
                        listRemote();
                    }
                }
            });
        }
    }
}

void FileBrowser::queueDownload(const QString &remote, const QString &local, const QString &shown, bool isDir, qint64 size)
{
    if (!isDir) {
        Job j;
        j.upload = false;
        j.name = shown;
        j.local = local;
        j.remote = remote;
        j.size = size;
        m_jobs.append(j);
        startNext();
        return;
    }
    QDir().mkpath(local);
    ++m_pendingLists;
    m_sftp->list(remote, [this, remote, local, shown](const QList<Sftp::Entry> &entries, const QString &error) {
        --m_pendingLists;
        if (!error.isEmpty()) {
            Q_EMIT notice(error);
        }
        for (const Sftp::Entry &e : entries) {
            if (e.link) {
                continue;
            }
            queueDownload(joinRemote(remote, e.name), local + QLatin1Char('/') + e.name, shown + QLatin1Char('/') + e.name, e.dir, e.size);
        }
        if (m_pendingLists == 0 && m_active < 0) {
            listLocal();
        }
    });
}

void FileBrowser::download(const QStringList &names)
{
    m_cancelled = false;
    for (const QString &name : names) {
        for (const QVariant &v : std::as_const(m_remoteEntries)) {
            const QVariantMap e = v.toMap();
            if (e.value(QStringLiteral("name")).toString() == name) {
                queueDownload(joinRemote(m_remotePath, name), QDir(m_localPath).absoluteFilePath(name), name, e.value(QStringLiteral("dir")).toBool(),
                              e.value(QStringLiteral("size")).toLongLong());
            }
        }
    }
}

void FileBrowser::startNext()
{
    if (m_active >= 0 || m_cancelled) {
        return;
    }
    for (int i = 0; i < m_jobs.size(); ++i) {
        if (m_jobs[i].state != QLatin1String("waiting")) {
            continue;
        }
        if (!m_batchClock.isValid()) {
            m_batchClock.start();
            m_batchStartBytes = 0;
            m_progressTimer.start();
        }
        m_active = i;
        m_jobs[i].state = QStringLiteral("active");
        Q_EMIT transfersChanged();
        auto progress = [this, i](qint64 done) {
            if (i < m_jobs.size()) {
                m_jobs[i].done = done;
            }
        };
        auto done = [this, i](const QString &error) {
            if (i < m_jobs.size()) {
                m_jobs[i].state = error.isEmpty() ? QStringLiteral("done") : QStringLiteral("failed");
                m_jobs[i].error = error;
                if (error.isEmpty()) {
                    m_jobs[i].done = m_jobs[i].size;
                }
            }
            m_active = -1;
            Q_EMIT transfersChanged();
            startNext();
            if (m_active < 0) {
                finishBatch();
            }
        };
        if (m_jobs[i].upload) {
            m_sftp->upload(m_jobs[i].local, m_jobs[i].remote, progress, done);
        } else {
            m_sftp->download(m_jobs[i].remote, m_jobs[i].local, progress, done);
        }
        return;
    }
}

void FileBrowser::finishBatch()
{
    m_progressTimer.stop();
    m_batchClock.invalidate();
    bool anyUpload = false;
    bool anyDownload = false;
    for (const Job &j : std::as_const(m_jobs)) {
        anyUpload |= j.upload;
        anyDownload |= !j.upload;
    }
    if (anyUpload) {
        listRemote();
    }
    if (anyDownload) {
        listLocal();
    }
    // Copies made for editing say so when they are back on the server.
    for (const Job &j : std::as_const(m_jobs)) {
        if (j.upload && m_editing.contains(j.local) && j.state == QLatin1String("done")) {
            Q_EMIT notice(tr("Saved %1 to %2").arg(j.name, m_hostName));
        }
    }
    Q_EMIT transfersChanged();
}

QVariantList FileBrowser::transfers() const
{
    QVariantList out;
    for (const Job &j : m_jobs) {
        out.append(QVariantMap{
            {QStringLiteral("name"), j.name},
            {QStringLiteral("upload"), j.upload},
            {QStringLiteral("state"), j.state},
            {QStringLiteral("progress"), j.size > 0 ? double(j.done) / double(j.size) : (j.state == QLatin1String("done") ? 1.0 : 0.0)},
            {QStringLiteral("detail"), j.state == QLatin1String("done") ? tr("Done")
                                       : j.state == QLatin1String("failed") ? j.error
                                       : j.state == QLatin1String("waiting") ? tr("Waiting")
                                       : tr("%1 of %2").arg(sizeText(j.done).section(QLatin1Char(' '), 0, 0), sizeText(j.size))},
        });
    }
    return out;
}

bool FileBrowser::transferring() const
{
    return m_active >= 0;
}

QString FileBrowser::transferTitle() const
{
    if (m_jobs.isEmpty()) {
        return {};
    }
    int up = 0;
    int down = 0;
    for (const Job &j : m_jobs) {
        (j.upload ? up : down)++;
    }
    const bool upload = up >= down;
    const int n = upload ? up : down;
    const QString files = n == 1 ? tr("1 file") : tr("%1 files").arg(n);
    if (m_active < 0) {
        return upload ? tr("Uploaded %1 to %2").arg(files, m_hostName) : tr("Downloaded %1 from %2").arg(files, m_hostName);
    }
    return upload ? tr("Uploading %1 to %2").arg(files, m_hostName) : tr("Downloading %1 from %2").arg(files, m_hostName);
}

QString FileBrowser::transferSummary() const
{
    int done = 0;
    int failed = 0;
    qint64 total = 0;
    qint64 moved = 0;
    for (const Job &j : m_jobs) {
        done += j.state == QLatin1String("done");
        failed += j.state == QLatin1String("failed");
        total += j.size;
        moved += j.done;
    }
    QString text = tr("%1 of %2 done").arg(done).arg(m_jobs.size());
    if (failed > 0) {
        text += failed == 1 ? tr(", 1 didn't copy") : tr(", %1 didn't copy").arg(failed);
    }
    // Only once there's enough to go on.
    if (m_active >= 0 && m_batchClock.isValid() && m_batchClock.elapsed() > 1000 && moved > 0) {
        const double rate = double(moved) / (m_batchClock.elapsed() / 1000.0);
        const qint64 left = qint64((total - moved) / std::max(1.0, rate));
        text += QStringLiteral(", ") + secondsText(left);
    }
    return text;
}

void FileBrowser::clearFinished()
{
    if (m_active >= 0) {
        return;
    }
    m_jobs.clear();
    Q_EMIT transfersChanged();
}

void FileBrowser::cancelTransfers()
{
    m_cancelled = true;
    for (Job &j : m_jobs) {
        if (j.state == QLatin1String("waiting")) {
            j.state = QStringLiteral("failed");
            j.error = tr("Cancelled");
        }
    }
    Q_EMIT transfersChanged();
}

void FileBrowser::openLocalFile(const QString &name)
{
    QDesktopServices::openUrl(QUrl::fromLocalFile(QDir(m_localPath).absoluteFilePath(name)));
}

void FileBrowser::editRemote(const QString &name)
{
    const QString remote = joinRemote(m_remotePath, name);
    // A folder of its own per host and path, so two files called index.html
    // never meet.
    const QString dir = Paths::cache() + QStringLiteral("/editing/") + m_hostName + QString(m_remotePath).replace(QLatin1Char('/'), QLatin1Char('_'));
    QDir().mkpath(dir);
    QFile::setPermissions(Paths::cache() + QStringLiteral("/editing"), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    const QString local = dir + QLatin1Char('/') + name;
    m_sftp->download(remote, local, {}, [this, local, remote](const QString &error) {
        if (!error.isEmpty()) {
            Q_EMIT notice(error);
            return;
        }
        if (!m_watcher) {
            m_watcher = new QFileSystemWatcher(this);
            connect(m_watcher, &QFileSystemWatcher::fileChanged, this, [this](const QString &path) {
                if (!m_changed.contains(path)) {
                    m_changed.append(path);
                }
                m_saveTimer.start();
            });
        }
        m_editing.insert(local, remote);
        m_watcher->addPath(local);
        QDesktopServices::openUrl(QUrl::fromLocalFile(local));
    });
}
