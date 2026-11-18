#pragma once

#include <QByteArray>
#include <QJsonObject>
#include <QObject>
#include <QProcess>
#include <QStringList>

// One restic command in the background. The password goes to restic on a
// pipe it reads as /dev/fd/3 (--password-file), so it is never on a command
// line, in an environment or in a file. Lines of --json output come out as
// `message`; anything else on standard output as `text`.
class Restic : public QObject
{
    Q_OBJECT

public:
    // restic's exit codes (0.17 and later).
    enum Exit {
        Ok = 0,
        Fatal = 1,
        Incomplete = 3, // the snapshot was made, but some files could not be read
        NoRepository = 10,
        Locked = 11,
        WrongPassword = 12,
        Interrupted = 130,
        // Not restic's: it never ran, so there is no exit code of its own.
        NotStarted = -1,
        NoPipe = -2,
    };

    Restic(const QString &repository, const QByteArray &password, QObject *parent = nullptr);
    ~Restic() override;

    // Also keeps standard output whole, for `output()` once finished.
    void setCollect(bool collect)
    {
        m_collect = collect;
    }
    void start(const QStringList &arguments);
    // As Ctrl+C would: restic removes its lock and stops.
    void interrupt();
    void kill();
    bool running() const;

    QByteArray output() const
    {
        return m_output;
    }
    // restic's last few lines of complaint, for the person in plain words
    // (see explain()).
    QString errorText() const
    {
        return m_error;
    }
    // A sentence for an exit code and restic's error output.
    static QString explain(int code, const QString &error);
    // restic's own last line, to show behind Details, when explain() has
    // only its general sentence for what went wrong; empty otherwise.
    static QString detail(int code, const QString &error);

    static QString program();

Q_SIGNALS:
    void message(const QJsonObject &message);
    void text(const QString &line);
    void finished(int code);

private:
    void readOutput();

    QString m_repository;
    QByteArray m_password;
    QProcess m_process;
    QByteArray m_buffer;
    QByteArray m_output;
    QString m_error;
    bool m_collect = false;
    int m_passwordFd = -1;
};
