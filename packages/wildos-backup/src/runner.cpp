#include "runner.h"
#include "drives.h"
#include "history.h"
#include "keyring.h"
#include "paths.h"
#include "plan.h"
#include "restic.h"
#include "settings.h"
#include "windowsmode.h"

#include <KNotification>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QProcess>
#include <QSaveFile>
#include <QSocketNotifier>
#include <QTemporaryFile>
#include <QTimer>

#include <csignal>
#include <sys/socket.h>
#include <unistd.h>

namespace
{
int s_signalFds[2] = {-1, -1};

void onSignal(int)
{
    const char c = 1;
    [[maybe_unused]] const ssize_t n = ::write(s_signalFds[0], &c, 1);
}

// Tidy every week, or sooner when the drive is nearly full: pruning reads
// and rewrites part of the repository, too slow for every run.
constexpr qint64 pruneEvery = 7 * 24 * 3600;
constexpr qint64 gib = qint64(1) << 30;

bool nearlyFull(const QString &mountPoint)
{
    const qint64 free = Drives::freeBytes(mountPoint);
    const qint64 total = Drives::totalBytes(mountPoint);
    if (free < 0 || total <= 0) {
        return false;
    }
    return free < std::min(total / 20, 20 * gib);
}

const QString filesTag = QStringLiteral("wildos-files");
const QString windowsTag = QStringLiteral("wildos-windows");
}

Runner::Runner(const QString &kind, QObject *parent)
    : QObject(parent)
    , m_kind(kind)
    , m_lock(Paths::lockFile())
{
}

Runner::~Runner()
{
    m_password.fill('\0');
}

int Runner::exec()
{
    QDir().mkpath(Paths::runtimeDir());
    QFile::setPermissions(Paths::runtimeDir(), QFileDevice::ReadOwner | QFileDevice::WriteOwner | QFileDevice::ExeOwner);
    // A lock left by a run that crashed is cleared by QLockFile itself,
    // which checks the process named in it.
    m_lock.setStaleLockTime(0);
    if (!m_lock.tryLock(0)) {
        return 0;
    }

    if (::socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, s_signalFds) == 0) {
        m_signals = new QSocketNotifier(s_signalFds[1], QSocketNotifier::Read, this);
        connect(m_signals, &QSocketNotifier::activated, this, [this] {
            char c;
            [[maybe_unused]] const ssize_t n = ::read(s_signalFds[1], &c, 1);
            stop();
        });
        struct sigaction action = {};
        action.sa_handler = onSignal;
        sigemptyset(&action.sa_mask);
        action.sa_flags = SA_RESTART;
        sigaction(SIGTERM, &action, nullptr);
        sigaction(SIGINT, &action, nullptr);
        sigaction(SIGHUP, &action, nullptr);
    }

    QTimer::singleShot(0, this, &Runner::begin);
    return QCoreApplication::exec();
}

void Runner::begin()
{
    Settings *settings = Settings::instance();
    m_started = QDateTime::currentDateTime();
    if (!settings->configured()) {
        QCoreApplication::exit(0);
        return;
    }

    const QDateTime pending = History::pendingSince();
    const QDateTime previous = settings->previousRun(m_started);
    if (m_kind == QLatin1String("catch-up")) {
        QDateTime lastDone;
        for (const QJsonValue &v : History::load()) {
            const QJsonObject e = v.toObject();
            if (e.value(QStringLiteral("result")).toString() == QLatin1String("done")) {
                lastDone = QDateTime::fromString(e.value(QStringLiteral("at")).toString(), Qt::ISODate);
                break;
            }
        }
        // A drive set up today with no backup yet hasn't missed anything:
        // its first backup is on its way from the setup.
        const bool overdue = previous.isValid() && lastDone.isValid() && lastDone < previous;
        if (!pending.isValid() && !overdue) {
            QCoreApplication::exit(0);
            return;
        }
        m_missedAt = pending.isValid() ? pending : previous;
        m_missedWhy = pending.isValid() ? QStringLiteral("drive") : QStringLiteral("late");
    } else if (m_kind == QLatin1String("scheduled")) {
        if (pending.isValid()) {
            m_missedAt = pending;
            m_missedWhy = QStringLiteral("drive");
        } else if (previous.isValid() && previous.secsTo(m_started) > 15 * 60 && !History::load().isEmpty()) {
            // Started by Persistent= after the PC was off or asleep.
            m_missedAt = previous;
            m_missedWhy = QStringLiteral("late");
        }
    }

    // The default drive first, then any other of theirs that is plugged in.
    const QList<DriveInfo> present = Drives::scan();
    QList<QString> order{settings->defaultDrive()};
    for (const QJsonValue &v : settings->driveList()) {
        const QString uuid = v.toObject().value(QStringLiteral("uuid")).toString();
        if (!order.contains(uuid)) {
            order.append(uuid);
        }
    }
    DriveInfo drive;
    for (const QString &uuid : order) {
        for (const DriveInfo &d : present) {
            if (d.uuid == uuid) {
                drive = d;
                break;
            }
        }
        if (!drive.uuid.isEmpty()) {
            break;
        }
    }
    if (drive.uuid.isEmpty()) {
        if (m_kind != QLatin1String("manual")) {
            History::setPending(m_kind == QLatin1String("scheduled") && previous.isValid() ? previous : m_started);
            QCoreApplication::exit(0);
            return;
        }
        // Asked for by name, so say why nothing happened.
        fail(QCoreApplication::translate("Runner", "The backup drive isn't plugged in."));
        return;
    }
    m_driveUuid = drive.uuid;
    m_driveName = drive.name;

    m_phase = QStringLiteral("preparing");
    writeStatus(true);

    QString why;
    m_mountPoint = Drives::mount(drive, false, &why);
    if (m_mountPoint.isEmpty()) {
        fail(QCoreApplication::translate("Runner", "The backup drive couldn't be opened. %1").arg(why));
        return;
    }
    m_repo = m_mountPoint + QLatin1Char('/') + Paths::repoFolder();
    if (!QFileInfo::exists(m_repo + QStringLiteral("/config"))) {
        fail(QCoreApplication::translate("Runner", "There are no backups on %1 any more. Set the drive up again on the Drives page.").arg(m_driveName));
        return;
    }
    if (!Keyring::readBlocking(&m_password, &why)) {
        fail(why == QLatin1String("missing")
                 ? QCoreApplication::translate("Runner", "The backup password isn't in your keyring. Type it on the Drives page to carry on.")
                 : QCoreApplication::translate("Runner", "Your keyring couldn't be opened for the backup password."));
        return;
    }

    if (nearlyFull(m_mountPoint)) {
        m_phase = QStringLiteral("tidying");
        writeStatus(true);
        // Room first, then the backup.
        trim(0, true);
        return;
    }
    backUpFiles();
}

void Runner::backUpFiles()
{
    if (m_stopping) {
        finish();
        return;
    }
    const Plan plan = Plan::from(Settings::instance());
    if (plan.paths.isEmpty()) {
        backUpWindows();
        return;
    }
    m_phase = QStringLiteral("files");
    writeStatus(true);

    auto *excludes = new QTemporaryFile(Paths::runtimeDir() + QStringLiteral("/excludes-XXXXXX"), this);
    if (!excludes->open()) {
        fail(QCoreApplication::translate("Runner", "The list of folders to leave out couldn't be written."));
        return;
    }
    excludes->write(plan.excludes.join(QLatin1Char('\n')).toUtf8());
    excludes->write("\n");
    excludes->flush();

    newRestic();
    connect(m_restic, &Restic::message, this, &Runner::onBackupMessage);
    connect(m_restic, &Restic::finished, this, [this, excludes](int code) {
        excludes->deleteLater();
        if (code == Restic::Ok || code == Restic::Incomplete) {
            if (code == Restic::Incomplete) {
                m_warning = m_errors == 1 ? QCoreApplication::translate("Runner", "1 file couldn't be read.")
                                          : QCoreApplication::translate("Runner", "%1 files couldn't be read.").arg(m_errors);
            }
            backUpWindows();
        } else if (code == Restic::Interrupted || m_stopping) {
            m_stopping = true;
            finish();
        } else {
            fail(Restic::explain(code, m_restic->errorText()), Restic::detail(code, m_restic->errorText()));
        }
    });
    QStringList args{
        QStringLiteral("backup"), QStringLiteral("--json"), QStringLiteral("--tag"), filesTag,
        QStringLiteral("--exclude-caches"), QStringLiteral("--exclude-file"), excludes->fileName(),
    };
    args << QStringLiteral("--") << plan.paths;
    m_restic->start(args);
}

void Runner::onBackupMessage(const QJsonObject &message)
{
    const QString type = message.value(QStringLiteral("message_type")).toString();
    if (type == QLatin1String("status")) {
        m_status = message;
        writeStatus();
    } else if (type == QLatin1String("summary")) {
        m_summary = message;
    } else if (type == QLatin1String("error")) {
        ++m_errors;
    }
}

void Runner::backUpWindows()
{
    Settings *settings = Settings::instance();
    if (m_stopping || !settings->includeWindows()) {
        tidy();
        return;
    }
    const WindowsMode windows = WindowsMode::query();
    if (!windows.setUp()) {
        tidy();
        return;
    }
    if (!windows.shutDown()) {
        m_windows = QStringLiteral("running");
        tidy();
        return;
    }
    m_phase = QStringLiteral("windows");
    m_status = {};
    m_status.insert(QStringLiteral("total_bytes"), double(windows.diskBytes));
    writeStatus(true);

    const QJsonObject filesSummary = m_summary;
    newRestic();
    connect(m_restic, &Restic::message, this, [this](const QJsonObject &message) {
        if (message.value(QStringLiteral("message_type")).toString() == QLatin1String("status")) {
            const double total = m_status.value(QStringLiteral("total_bytes")).toDouble();
            m_status = message;
            m_status.insert(QStringLiteral("total_bytes"), total);
            writeStatus();
        }
    });
    connect(m_restic, &Restic::finished, this, [this, filesSummary](int code) {
        m_summary = filesSummary;
        const QString error = m_restic->errorText();
        if (code == Restic::Ok) {
            m_windows = QStringLiteral("done");
        } else if (code == Restic::Interrupted || m_stopping) {
            m_stopping = true;
            finish();
            return;
        } else if (error.contains(QLatin1String("not shut down")) || error.contains(QLatin1String("started while"))) {
            m_windows = QStringLiteral("running");
        } else {
            m_windows = QStringLiteral("failed");
        }
        tidy();
    });
    m_restic->start({
        QStringLiteral("backup"), QStringLiteral("--json"), QStringLiteral("--tag"), windowsTag,
        QStringLiteral("--stdin-from-command"), QStringLiteral("--stdin-filename"), QStringLiteral("windows.qcow2"),
        QStringLiteral("--"), QStringLiteral("pkexec"), Paths::windowsHelper(),
    });
}

void Runner::tidy()
{
    if (m_stopping) {
        finish();
        return;
    }
    m_phase = QStringLiteral("tidying");
    m_status = {};
    writeStatus(true);

    const QDateTime lastPrune = QDateTime::fromString(Settings::instance()->drive(m_driveUuid).value(QStringLiteral("lastPrune")).toString(), Qt::ISODate);
    if (!lastPrune.isValid()) {
        // A new drive has nothing to prune yet; start its week now.
        Settings::instance()->setDriveValue(m_driveUuid, QStringLiteral("lastPrune"), QDateTime::currentDateTime().toString(Qt::ISODate));
    }
    const bool prune = (lastPrune.isValid() && lastPrune.secsTo(QDateTime::currentDateTime()) > pruneEvery) || nearlyFull(m_mountPoint);

    QStringList args{QStringLiteral("forget"), QStringLiteral("--json"), QStringLiteral("--group-by"), QStringLiteral("host,tags")};
    args << Settings::instance()->keepArguments();
    if (prune) {
        args << QStringLiteral("--prune");
    }
    newRestic();
    connect(m_restic, &Restic::finished, this, [this, prune](int code) {
        if (code == Restic::Interrupted || m_stopping) {
            m_stopping = true;
            finish();
            return;
        }
        if (code != Restic::Ok) {
            // The backup itself is safe; old ones just stay a while longer.
            qWarning("wildos-backup: tidying old backups failed: %s", qPrintable(m_restic->errorText()));
        } else if (prune) {
            Settings::instance()->setDriveValue(m_driveUuid, QStringLiteral("lastPrune"), QDateTime::currentDateTime().toString(Qt::ISODate));
        }
        if (nearlyFull(m_mountPoint)) {
            trim(0, false);
        } else {
            finish();
        }
    });
    m_restic->start(args);
}

// Oldest backups go first when the drive is nearly full: one at a time,
// never the newest of each kind, until there is room again.
void Runner::trim(int round, bool before)
{
    if (m_stopping || round >= 8 || !nearlyFull(m_mountPoint)) {
        if (before) {
            backUpFiles();
        } else {
            finish();
        }
        return;
    }
    newRestic();
    m_restic->setCollect(true);
    connect(m_restic, &Restic::finished, this, [this, round, before](int code) {
        if (code != Restic::Ok) {
            if (before) {
                backUpFiles();
            } else {
                finish();
            }
            return;
        }
        const QJsonArray snapshots = QJsonDocument::fromJson(m_restic->output()).array();
        QMap<QString, int> perTag;
        for (const QJsonValue &v : snapshots) {
            perTag[v.toObject().value(QStringLiteral("tags")).toArray().first().toString()]++;
        }
        // restic lists oldest first.
        QString oldest;
        for (const QJsonValue &v : snapshots) {
            const QString tag = v.toObject().value(QStringLiteral("tags")).toArray().first().toString();
            if (perTag.value(tag) > 1) {
                oldest = v.toObject().value(QStringLiteral("id")).toString();
                break;
            }
        }
        if (oldest.isEmpty()) {
            if (before) {
                backUpFiles();
            } else {
                finish();
            }
            return;
        }
        newRestic();
        connect(m_restic, &Restic::finished, this, [this, round, before](int) {
            trim(round + 1, before);
        });
        m_restic->start({QStringLiteral("forget"), QStringLiteral("--prune"), QStringLiteral("--max-unused"), QStringLiteral("0"), oldest});
    });
    m_restic->start({QStringLiteral("snapshots"), QStringLiteral("--json")});
}

Restic *Runner::newRestic()
{
    // The old one may be the very one whose finished() got us here.
    if (m_restic) {
        m_restic->disconnect(this);
        m_restic->deleteLater();
    }
    m_restic = new Restic(m_repo, m_password, this);
    return m_restic;
}

void Runner::fail(const QString &reason, const QString &detail)
{
    m_failed = true;
    m_reason = reason;
    m_detail = detail;
    m_exit = 1;
    finish();
}

void Runner::stop()
{
    if (m_stopping) {
        return;
    }
    m_stopping = true;
    if (m_restic && m_restic->running()) {
        m_restic->interrupt();
    } else if (m_phase.isEmpty() || m_phase == QLatin1String("preparing")) {
        finish();
    }
}

void Runner::finish()
{
    if (m_finished) {
        return;
    }
    m_finished = true;

    QJsonObject entry{
        {QStringLiteral("at"), m_started.toString(Qt::ISODate)},
        {QStringLiteral("finished"), QDateTime::currentDateTime().toString(Qt::ISODate)},
        {QStringLiteral("kind"), m_kind},
        {QStringLiteral("drive"), m_driveUuid},
        {QStringLiteral("driveName"), m_driveName},
    };
    if (m_failed) {
        entry.insert(QStringLiteral("result"), QStringLiteral("failed"));
        entry.insert(QStringLiteral("reason"), m_reason);
        if (!m_detail.isEmpty()) {
            entry.insert(QStringLiteral("detail"), m_detail);
        }
    } else if (m_stopping) {
        entry.insert(QStringLiteral("result"), QStringLiteral("stopped"));
    } else {
        entry.insert(QStringLiteral("result"), QStringLiteral("done"));
    }
    if (!m_warning.isEmpty()) {
        entry.insert(QStringLiteral("warning"), m_warning);
    }
    if (m_missedAt.isValid()) {
        entry.insert(QStringLiteral("missedAt"), m_missedAt.toString(Qt::ISODate));
        entry.insert(QStringLiteral("missedWhy"), m_missedWhy);
    }
    if (!m_windows.isEmpty()) {
        entry.insert(QStringLiteral("windows"), m_windows);
    }
    if (!m_summary.isEmpty()) {
        entry.insert(QStringLiteral("dataAdded"), m_summary.value(QStringLiteral("data_added")));
        entry.insert(QStringLiteral("filesNew"), m_summary.value(QStringLiteral("files_new")));
        entry.insert(QStringLiteral("filesChanged"), m_summary.value(QStringLiteral("files_changed")));
        entry.insert(QStringLiteral("filesTotal"), m_summary.value(QStringLiteral("total_files_processed")));
        entry.insert(QStringLiteral("bytesTotal"), m_summary.value(QStringLiteral("total_bytes_processed")));
        entry.insert(QStringLiteral("snapshot"), m_summary.value(QStringLiteral("snapshot_id")));
    }
    entry.insert(QStringLiteral("seconds"), m_started.secsTo(QDateTime::currentDateTime()));
    History::append(entry);
    if (!m_failed && !m_stopping) {
        History::clearPending();
        Settings::instance()->setDriveValue(m_driveUuid, QStringLiteral("lastBackup"), m_started.toString(Qt::ISODate));
    }
    QFile::remove(Paths::statusFile());
    if (m_restic) {
        m_restic->disconnect(this);
        m_restic->deleteLater();
    }
    m_password.fill('\0');
    m_lock.unlock();

    notify();
}

void Runner::notify()
{
    const bool failed = m_failed;
    if (m_stopping || (!failed && !Settings::instance()->notifyFinished())) {
        QCoreApplication::exit(m_exit);
        return;
    }
    auto *n = new KNotification(failed ? QStringLiteral("failed") : QStringLiteral("finished"));
    n->setComponentName(QStringLiteral("wildos-backup"));
    n->setIconName(QStringLiteral("wildos-backup"));
    n->setHint(QStringLiteral("desktop-entry"), QStringLiteral("org.wildos.backup"));
    if (failed) {
        n->setTitle(QCoreApplication::translate("Runner", "Backup didn't finish"));
        n->setText(m_reason);
        n->setHint(QStringLiteral("resident"), true);
    } else {
        n->setTitle(QCoreApplication::translate("Runner", "Your files are backed up"));
        QString text = m_driveName.isEmpty() ? QString() : QCoreApplication::translate("Runner", "Saved to %1.").arg(m_driveName);
        if (m_windows == QLatin1String("running")) {
            text += QLatin1Char(' ') + QCoreApplication::translate("Runner", "Windows mode waits until Windows is shut down.");
        } else if (m_windows == QLatin1String("failed")) {
            text += QLatin1Char(' ') + QCoreApplication::translate("Runner", "Windows mode couldn't be backed up this time.");
        }
        if (!m_warning.isEmpty()) {
            text += QLatin1Char(' ') + m_warning;
        }
        n->setText(text.trimmed());
    }
    auto *open = n->addDefaultAction(QCoreApplication::translate("Runner", "Open Backup"));
    connect(open, &KNotificationAction::activated, this, [n] {
        QProcess::startDetached(QCoreApplication::applicationFilePath(), {});
        n->close();
    });
    connect(n, &KNotification::closed, this, [this] { QCoreApplication::exit(m_exit); });
    n->sendEvent();
    // Long enough to answer a click on the notification while it shows.
    QTimer::singleShot(failed ? 120000 : 15000, this, [this] { QCoreApplication::exit(m_exit); });
}

void Runner::writeStatus(bool force)
{
    if (!force && m_lastStatus.isValid() && m_lastStatus.elapsed() < 400) {
        return;
    }
    m_lastStatus.restart();
    QJsonObject o{
        {QStringLiteral("pid"), double(QCoreApplication::applicationPid())},
        {QStringLiteral("kind"), m_kind},
        {QStringLiteral("phase"), m_phase},
        {QStringLiteral("started"), m_started.toString(Qt::ISODate)},
        {QStringLiteral("drive"), m_driveName},
    };
    const auto copy = [&](const char *from, const char *to) {
        const QJsonValue v = m_status.value(QLatin1String(from));
        if (!v.isUndefined()) {
            o.insert(QLatin1String(to), v);
        }
    };
    copy("percent_done", "percent");
    copy("files_done", "filesDone");
    copy("total_files", "totalFiles");
    copy("bytes_done", "bytesDone");
    copy("total_bytes", "totalBytes");
    copy("seconds_remaining", "secondsLeft");
    QSaveFile f(Paths::statusFile());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Compact));
        f.commit();
    }
}
