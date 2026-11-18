#pragma once

#include <QDateTime>
#include <QElapsedTimer>
#include <QJsonObject>
#include <QLockFile>
#include <QObject>
#include <QPointer>

class Restic;
class QSocketNotifier;

// `wildos-backup --run`: one backup with no window, started by the timer,
// the drive watcher or Back up now. It backs up to the first of the
// person's drives that is plugged in (the default first), then Windows
// mode's disk if that is on and Windows is shut down, then tidies old
// backups away by the Keep choices. It writes its progress where the app
// reads it, adds the run to the history and says how it went in a
// notification. With no drive plugged in it does nothing but note that a
// backup is waiting.
//
// Only one runs at a time: a second start finds the lock taken and leaves.
class Runner : public QObject
{
    Q_OBJECT

public:
    // "scheduled", "manual" or "catch-up".
    explicit Runner(const QString &kind, QObject *parent = nullptr);
    ~Runner() override;

    // Returns the process's exit code once done.
    int exec();

private:
    void begin();
    void backUpFiles();
    void backUpWindows();
    void tidy();
    // before: making room ahead of the backup, which follows.
    void trim(int round, bool before);
    Restic *newRestic();
    void finish();
    void fail(const QString &reason, const QString &detail = QString());
    void stop();
    void writeStatus(bool force = false);
    void notify();
    void onBackupMessage(const QJsonObject &message);

    QString m_kind;
    QLockFile m_lock;
    QString m_repo;
    QString m_mountPoint;
    QString m_driveUuid;
    QString m_driveName;
    QByteArray m_password;
    QPointer<Restic> m_restic;
    QSocketNotifier *m_signals = nullptr;

    QDateTime m_started;
    QDateTime m_missedAt;
    // "drive" (it wasn't plugged in) or "late" (the PC was off or asleep).
    QString m_missedWhy;
    QElapsedTimer m_lastStatus;
    QJsonObject m_status;
    QJsonObject m_summary;
    QString m_phase;
    QString m_reason;
    QString m_detail;
    QString m_warning;
    QString m_windows;
    int m_errors = 0;
    bool m_stopping = false;
    bool m_failed = false;
    bool m_finished = false;
    int m_exit = 0;
};
