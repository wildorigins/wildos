#include "firstrun.h"
#include "background.h"
#include "privacy.h"
#include "setup.h"

#include <KUiServerV2JobTracker>

#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDBusConnection>
#include <QDBusInterface>
#include <QDBusMessage>
#include <QDBusReply>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonDocument>
#include <QJsonObject>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QProcess>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStandardPaths>
#include <QTimer>

#include <functional>

namespace
{
QString cacheDir()
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericCacheLocation) + QStringLiteral("/wildos-welcome");
    QDir().mkpath(dir);
    return dir;
}

QString writeJob(const QString &kind, const QJsonDocument &job)
{
    const QString path = cacheDir() + QStringLiteral("/%1-%2.json").arg(kind).arg(QDateTime::currentMSecsSinceEpoch());
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return {};
    }
    file.write(job.toJson());
    return path;
}

QString moved(QString path, const QString &from, const QString &to)
{
    if (!from.isEmpty() && path.startsWith(from)) {
        path.replace(0, from.size(), to);
    }
    return path;
}

QString mountPointOf(const QString &kname)
{
    QFile mounts(QStringLiteral("/proc/self/mounts"));
    if (!mounts.open(QIODevice::ReadOnly)) {
        return {};
    }
    const QString device = QStringLiteral("/dev/") + kname;
    for (const QByteArray &line : mounts.readAll().split('\n')) {
        const QList<QByteArray> f = line.split(' ');
        if (f.size() > 1 && QString::fromUtf8(f[0]) == device) {
            // Spaces in mount points are written as \040.
            return QString::fromUtf8(f[1]).replace(QStringLiteral("\\040"), QStringLiteral(" "));
        }
    }
    return {};
}

// The setup session had the Windows drive open as its own account and
// closed it at the end; this account opens it again, read-only, and the
// planned copy is moved onto wherever it is now.
void startCopy(QJsonObject job)
{
    const QString kname = job.value(QStringLiteral("kname")).toString();
    const QString oldRoot = job.value(QStringLiteral("root")).toString();
    const QString oldHome = job.value(QStringLiteral("home")).toString();
    QString root = kname.isEmpty() ? QString() : mountPointOf(kname);
    bool weMounted = false;
    if (root.isEmpty() && !kname.isEmpty()) {
        auto message = QDBusMessage::createMethodCall(QStringLiteral("org.freedesktop.UDisks2"), QStringLiteral("/org/freedesktop/UDisks2/block_devices/") + kname,
                                                      QStringLiteral("org.freedesktop.UDisks2.Filesystem"), QStringLiteral("Mount"));
        message << QVariantMap{{QStringLiteral("options"), QStringLiteral("ro")}};
        const QDBusReply<QString> reply = QDBusConnection::systemBus().call(message, QDBus::Block, 300000);
        root = reply.isValid() ? reply.value() : QString();
        weMounted = reply.isValid();
    }
    if (root.isEmpty()) {
        Background::notify(QCoreApplication::translate("FirstRun", "Your files weren't copied from Windows"),
                           QCoreApplication::translate("FirstRun", "Wild OS couldn't open the Windows drive again. Copy them with Files instead; nothing was changed on Windows."));
        return;
    }

    QJsonArray folders;
    for (const QJsonValue &value : job.value(QStringLiteral("folders")).toArray()) {
        QJsonObject folder = value.toObject();
        QJsonArray from;
        for (const QJsonValue &path : folder.value(QStringLiteral("from")).toArray()) {
            from.append(moved(path.toString(), oldRoot, root));
        }
        folder.insert(QStringLiteral("from"), from);
        folder.insert(QStringLiteral("to"), moved(folder.value(QStringLiteral("to")).toString(), oldHome, QDir::homePath()));
        folders.append(folder);
    }
    QJsonArray bookmarks;
    for (const QJsonValue &path : job.value(QStringLiteral("bookmarks")).toArray()) {
        bookmarks.append(moved(path.toString(), oldRoot, root));
    }
    job.insert(QStringLiteral("folders"), folders);
    job.insert(QStringLiteral("bookmarks"), bookmarks);
    for (const char *key : {"root", "home", "kname", "unmount"}) {
        job.remove(QLatin1String(key));
    }
    if (weMounted) {
        job.insert(QStringLiteral("unmount"), kname);
    }
    const QString path = writeJob(QStringLiteral("copy"), QJsonDocument(job));
    if (!path.isEmpty()) {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--copy"), path});
    }
}

}

namespace FirstRun
{
void startPrograms(const QVariantList &programs)
{
    QJsonArray store;
    QJsonArray downloads;
    for (const QVariant &value : programs) {
        const QVariantMap program = value.toMap();
        const QString action = program.value(QStringLiteral("action")).toString();
        if (action == QLatin1String("store")) {
            store.append(QJsonObject{
                {QStringLiteral("name"), program.value(QStringLiteral("name")).toString()},
                {QStringLiteral("package"), QString()},
                {QStringLiteral("flatpak"), program.value(QStringLiteral("storeId")).toString()},
            });
        } else if (action == QLatin1String("download")) {
            QJsonObject entry = QJsonObject::fromVariantMap(program.value(QStringLiteral("installer")).toMap());
            entry.insert(QStringLiteral("name"), program.value(QStringLiteral("name")).toString());
            downloads.append(entry);
        }
    }
    if (!store.isEmpty()) {
        const QString path = writeJob(QStringLiteral("install"), QJsonDocument(store));
        if (!path.isEmpty()) {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--install"), path});
        }
    }
    if (!downloads.isEmpty()) {
        const QString path = writeJob(QStringLiteral("programs"), QJsonDocument(downloads));
        if (!path.isEmpty()) {
            QProcess::startDetached(QCoreApplication::applicationFilePath(), {QStringLiteral("--programs"), path});
        }
    }
}

// What first run found is kept for Windows mode (package wildos-winmode),
// and its setup opens to check this PC and carry on: the person chose
// "Set up Windows mode" here, so it isn't asked twice.
void saveWindowsMode(const QVariantMap &plan)
{
    if (plan.isEmpty()) {
        return;
    }
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::GenericDataLocation) + QStringLiteral("/wildos");
    QDir().mkpath(dir);
    QVariantMap saved = plan;
    saved.insert(QStringLiteral("planned"), QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QSaveFile file(dir + QStringLiteral("/windows-mode-plan.json"));
    if (file.open(QIODevice::WriteOnly)) {
        file.write(QJsonDocument(QJsonObject::fromVariantMap(saved)).toJson());
        file.commit();
    }
    if (!QStandardPaths::findExecutable(QStringLiteral("wildos-winmode")).isEmpty()) {
        QProcess::startDetached(QStringLiteral("wildos-winmode"), {});
    }
}

// Step 7's choice (boards 3.9 and 3.10), made in the setup session before
// this account existed: the request goes to Windows mode's agent as this
// account, which owns Windows mode from then on, and setup runs in the
// background (9.11). Every thread was chosen there too, but turning that on
// needs the administrator, so it asks here.
void startWindowsMode(const QString &request)
{
    if (QJsonDocument::fromJson(request.toUtf8()).object().value(QStringLiteral("options")).toObject().value(QStringLiteral("hyperThreading")).toBool()) {
        QProcess::startDetached(QStringLiteral("pkexec"), {QStringLiteral("/usr/libexec/wildos/settings-cpu-threads"), QStringLiteral("on")});
    }
    QDBusInterface agent(QStringLiteral("org.wildos.WindowsMode1"), QStringLiteral("/org/wildos/WindowsMode1"), QStringLiteral("org.wildos.WindowsMode1"),
                         QDBusConnection::sessionBus());
    agent.setTimeout(120000);
    const QDBusReply<QString> reply = agent.call(QStringLiteral("StartSetup"), request);
    if (!reply.isValid() || !reply.value().isEmpty()) {
        const QString why = reply.isValid() ? reply.value() : QCoreApplication::translate("FirstRun", "Windows mode's background service didn't answer.");
        qWarning("Windows mode didn't start setting up: %s", qPrintable(reply.isValid() ? reply.value() : reply.error().message()));
        // Said where it shows, since nothing else on screen would.
        QProcess::startDetached(QStringLiteral("notify-send"), {QStringLiteral("--app-name=Windows mode"), QStringLiteral("--icon=wildos-windows-app"),
                                                               QCoreApplication::translate("FirstRun", "Windows mode didn't start setting up"),
                                                               QCoreApplication::translate("FirstRun", "%1 Set it up any time in Settings, under Windows apps.").arg(why)});
    }
}

QString planFile()
{
    return QStandardPaths::writableLocation(QStandardPaths::GenericStateLocation) + QStringLiteral("/wildos-welcome/first-run.json");
}

bool hasPlan()
{
    return QFile::exists(planFile());
}

int apply()
{
    QFile file(planFile());
    if (!file.open(QIODevice::ReadOnly)) {
        return 1;
    }
    const QVariantMap plan = QJsonDocument::fromJson(file.readAll()).object().toVariantMap();
    file.close();
    // Gone before anything starts, so a crash halfway never copies twice.
    QFile::remove(planFile());

    Privacy privacy;
    privacy.apply(plan.value(QStringLiteral("privacy")).toMap());
    const QVariantMap copy = plan.value(QStringLiteral("copy")).toMap();
    if (!copy.isEmpty()) {
        startCopy(QJsonObject::fromVariantMap(copy));
    }
    const QVariantMap programs = plan.value(QStringLiteral("programs")).toMap();
    startPrograms(programs.value(QStringLiteral("programs")).toList());
    saveWindowsMode(programs.value(QStringLiteral("windowsMode")).toMap());
    const QString windowsMode = plan.value(QStringLiteral("windowsMode")).toString();

    auto *setup = new Setup(qApp);
    setup->apply(plan.value(QStringLiteral("home")).toMap(), [setup, windowsMode] {
        // Once the taskbar is up, so its Windows mode item and the
        // notification have somewhere to show.
        if (!windowsMode.isEmpty()) {
            startWindowsMode(windowsMode);
        }
        setup->finish();
        // A scheme switch finishes in its own process; give it a moment.
        QTimer::singleShot(3000, qApp, &QCoreApplication::quit);
    });
    return QCoreApplication::exec();
}

int runPrograms(const QString &jobFile)
{
    QFile file(jobFile);
    if (!file.open(QIODevice::ReadOnly)) {
        return 1;
    }
    const QJsonArray programs = QJsonDocument::fromJson(file.readAll()).array();
    file.close();
    QFile::remove(jobFile);

    auto tracker = new KUiServerV2JobTracker(qApp);
    auto job = new DownloadJob(programs, qApp);
    tracker->registerJob(job);
    QObject::connect(job, &KJob::result, qApp, [job] {
        const QStringList failed = job->failed();
        if (!failed.isEmpty()) {
            Background::notify(QCoreApplication::translate("FirstRun", "Some installers weren't downloaded"), failed.join(QLatin1Char(' ')));
        }
        // One at a time: each opens the App Bridge's own install dialog.
        auto *queue = new QStringList(job->ready());
        auto *runNext = new std::function<void()>;
        *runNext = [queue, runNext] {
            if (queue->isEmpty()) {
                delete queue;
                delete runNext;
                QCoreApplication::quit();
                return;
            }
            auto *process = new QProcess(qApp);
            QObject::connect(process, &QProcess::finished, qApp, [process, runNext] {
                process->deleteLater();
                (*runNext)();
            });
            process->start(QStringLiteral("wildos-appbridge"), {queue->takeFirst()});
        };
        (*runNext)();
    });
    job->start();
    return QCoreApplication::exec();
}
}

DownloadJob::DownloadJob(const QJsonArray &programs, QObject *parent)
    : KJob(parent)
    , m_programs(programs)
    , m_network(new QNetworkAccessManager(this))
{
    setCapabilities(KJob::Killable);
}

void DownloadJob::start()
{
    QStringList names;
    for (const QJsonValue &value : std::as_const(m_programs)) {
        names << value.toObject().value(QStringLiteral("name")).toString();
    }
    Q_EMIT description(this, QCoreApplication::translate("FirstRun", "Downloading Windows installers"), {QCoreApplication::translate("FirstRun", "For"), names.join(QStringLiteral(", "))});
    QTimer::singleShot(0, this, &DownloadJob::next);
}

bool DownloadJob::doKill()
{
    if (m_reply) {
        m_reply->abort();
    }
    return true;
}

void DownloadJob::fail(const QString &name, const QString &why)
{
    m_failed << QCoreApplication::translate("FirstRun", "%1: %2").arg(name, why);
    next();
}

// The latest release of the program's own repository, over HTTPS; the
// asset is the installer the list names, and GitHub publishes its SHA-256.
void DownloadJob::next()
{
    if (++m_index >= m_programs.size()) {
        emitResult();
        return;
    }
    const QJsonObject program = m_programs.at(m_index).toObject();
    const QString name = program.value(QStringLiteral("name")).toString();
    const QString repo = program.value(QStringLiteral("github")).toString();
    static const QRegularExpression repoShape(QStringLiteral("^[A-Za-z0-9._-]+/[A-Za-z0-9._-]+$"));
    if (!repoShape.match(repo).hasMatch()) {
        fail(name, QCoreApplication::translate("FirstRun", "Wild OS doesn't know where to get it."));
        return;
    }
    QNetworkRequest request(QUrl(QStringLiteral("https://api.github.com/repos/%1/releases/latest").arg(repo)));
    request.setRawHeader("Accept", "application/vnd.github+json");
    request.setTransferTimeout(30000);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::finished, this, [this, name, program] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        if (reply->error() != QNetworkReply::NoError) {
            fail(name, QCoreApplication::translate("FirstRun", "its download page didn't answer."));
            return;
        }
        const QRegularExpression wanted(program.value(QStringLiteral("asset")).toString());
        const QJsonArray assets = QJsonDocument::fromJson(reply->readAll()).object().value(QStringLiteral("assets")).toArray();
        for (const QJsonValue &value : assets) {
            const QJsonObject asset = value.toObject();
            if (!wanted.match(asset.value(QStringLiteral("name")).toString()).hasMatch()) {
                continue;
            }
            const QString digest = asset.value(QStringLiteral("digest")).toString();
            if (!digest.startsWith(QLatin1String("sha256:"))) {
                fail(name, QCoreApplication::translate("FirstRun", "its installer has no checksum to check it against."));
                return;
            }
            const QUrl url(asset.value(QStringLiteral("browser_download_url")).toString());
            if (url.scheme() != QLatin1String("https")) {
                fail(name, QCoreApplication::translate("FirstRun", "its installer isn't offered securely."));
                return;
            }
            fetch(name, url.toString(), asset.value(QStringLiteral("size")).toInteger(), QByteArray::fromHex(digest.mid(7).toLatin1()));
            return;
        }
        fail(name, QCoreApplication::translate("FirstRun", "its latest release has no Windows installer."));
    });
}

void DownloadJob::fetch(const QString &name, const QString &url, qint64 size, const QByteArray &sha256)
{
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::DownloadLocation) + QStringLiteral("/Windows installers");
    QDir().mkpath(dir);
    const QString target = dir + QLatin1Char('/') + QUrl(url).fileName();
    auto *out = new QSaveFile(target, this);
    if (!out->open(QIODevice::WriteOnly)) {
        out->deleteLater();
        fail(name, QCoreApplication::translate("FirstRun", "there's no room for it in Downloads."));
        return;
    }
    auto *hash = new QCryptographicHash(QCryptographicHash::Sha256);
    Q_EMIT description(this, QCoreApplication::translate("FirstRun", "Downloading Windows installers"), {QCoreApplication::translate("FirstRun", "Program"), name});
    setTotalAmount(KJob::Bytes, size);
    setProcessedAmount(KJob::Bytes, 0);
    QNetworkRequest request{QUrl(url)};
    request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
    request.setTransferTimeout(60000);
    m_reply = m_network->get(request);
    connect(m_reply, &QNetworkReply::readyRead, this, [this, out, hash] {
        const QByteArray chunk = m_reply->readAll();
        hash->addData(chunk);
        out->write(chunk);
    });
    connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64) {
        setProcessedAmount(KJob::Bytes, received);
    });
    connect(m_reply, &QNetworkReply::finished, this, [this, name, out, hash, sha256, target] {
        QNetworkReply *reply = m_reply;
        m_reply = nullptr;
        reply->deleteLater();
        const QByteArray rest = reply->readAll();
        hash->addData(rest);
        out->write(rest);
        const bool good = reply->error() == QNetworkReply::NoError && hash->result() == sha256;
        delete hash;
        if (!good) {
            out->cancelWriting();
            out->deleteLater();
            fail(name, reply->error() == QNetworkReply::NoError ? QCoreApplication::translate("FirstRun", "the download didn't match its checksum, so it was thrown away.")
                                                                : QCoreApplication::translate("FirstRun", "the download stopped."));
            return;
        }
        out->commit();
        out->deleteLater();
        m_ready << target;
        next();
    });
}
