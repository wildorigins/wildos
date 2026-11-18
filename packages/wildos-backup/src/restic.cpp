#include "restic.h"

#include <QCoreApplication>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>

#include <cerrno>
#include <csignal>
#include <fcntl.h>
#include <unistd.h>

Restic::Restic(const QString &repository, const QByteArray &password, QObject *parent)
    : QObject(parent)
    , m_repository(repository)
    , m_password(password)
{
    m_process.setProcessChannelMode(QProcess::SeparateChannels);
    connect(&m_process, &QProcess::readyReadStandardOutput, this, &Restic::readOutput);
    connect(&m_process, &QProcess::readyReadStandardError, this, [this] {
        m_error += QString::fromUtf8(m_process.readAllStandardError());
        // Only the end matters, and a long run can say a lot.
        if (m_error.size() > 8000) {
            m_error = m_error.right(4000);
        }
    });
    connect(&m_process, &QProcess::finished, this, [this](int code, QProcess::ExitStatus status) {
        readOutput();
        if (!m_buffer.trimmed().isEmpty()) {
            m_buffer.append('\n');
            readOutput();
        }
        Q_EMIT finished(status == QProcess::CrashExit ? Interrupted : code);
    });
    connect(&m_process, &QProcess::errorOccurred, this, [this](QProcess::ProcessError error) {
        if (error == QProcess::FailedToStart) {
            m_error.clear();
            Q_EMIT finished(NotStarted);
        }
    });
}

Restic::~Restic()
{
    // The process would report finishing from its own destructor, into
    // whatever listens to this half-destroyed object.
    m_process.disconnect();
    disconnect();
    if (m_process.state() != QProcess::NotRunning) {
        ::kill(static_cast<pid_t>(m_process.processId()), SIGINT);
        if (!m_process.waitForFinished(3000)) {
            m_process.kill();
            m_process.waitForFinished(1000);
        }
    }
    if (m_passwordFd >= 0) {
        ::close(m_passwordFd);
    }
    m_password.fill('\0');
}

QString Restic::program()
{
    const QString found = QStandardPaths::findExecutable(QStringLiteral("restic"));
    return found.isEmpty() ? QStringLiteral("/usr/bin/restic") : found;
}

void Restic::start(const QStringList &arguments)
{
    m_buffer.clear();
    m_output.clear();
    m_error.clear();

    int fds[2];
    if (::pipe2(fds, O_CLOEXEC) != 0) {
        QMetaObject::invokeMethod(this, [this] { Q_EMIT finished(NoPipe); }, Qt::QueuedConnection);
        return;
    }
    // A password is a few dozen bytes, far below a pipe's buffer, so this
    // never waits for restic.
    QByteArray line = m_password;
    line.append('\n');
    qsizetype written = 0;
    while (written < line.size()) {
        const ssize_t n = ::write(fds[1], line.constData() + written, line.size() - written);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        written += n;
    }
    line.fill('\0');
    ::close(fds[1]);
    m_passwordFd = fds[0];

    const int fd = m_passwordFd;
    m_process.setChildProcessModifier([fd]() {
        if (fd == 3) {
            ::fcntl(3, F_SETFD, 0);
        } else {
            ::dup2(fd, 3);
        }
    });

    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    for (const QString &key : env.keys()) {
        if (key.startsWith(QLatin1String("RESTIC_"))) {
            env.remove(key);
        }
    }
    // Progress twice a second is plenty for the window and the status file.
    env.insert(QStringLiteral("RESTIC_PROGRESS_FPS"), QStringLiteral("2"));
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    m_process.setProcessEnvironment(env);

    QStringList args{QStringLiteral("--repo"), m_repository, QStringLiteral("--password-file"), QStringLiteral("/dev/fd/3")};
    args << arguments;
    m_process.start(program(), args);
    // QProcess forks within start(), so the child already has its copy.
    ::close(m_passwordFd);
    m_passwordFd = -1;
}

void Restic::interrupt()
{
    if (m_process.state() != QProcess::NotRunning) {
        ::kill(static_cast<pid_t>(m_process.processId()), SIGINT);
    }
}

void Restic::kill()
{
    m_process.kill();
}

bool Restic::running() const
{
    return m_process.state() != QProcess::NotRunning;
}

void Restic::readOutput()
{
    const QByteArray chunk = m_process.readAllStandardOutput();
    if (m_collect) {
        m_output.append(chunk);
    }
    m_buffer.append(chunk);
    qsizetype newline;
    while ((newline = m_buffer.indexOf('\n')) >= 0) {
        const QByteArray line = m_buffer.left(newline).trimmed();
        m_buffer.remove(0, newline + 1);
        if (line.isEmpty()) {
            continue;
        }
        if (line.startsWith('{')) {
            QJsonParseError error;
            const QJsonDocument doc = QJsonDocument::fromJson(line, &error);
            if (error.error == QJsonParseError::NoError && doc.isObject()) {
                Q_EMIT message(doc.object());
                continue;
            }
        }
        Q_EMIT text(QString::fromUtf8(line));
    }
}

namespace
{
// The problems Backup has a sentence of its own for; empty for the rest.
QString knownProblem(int code, const QString &error)
{
    const QString e = error.toLower();
    if (code == Restic::WrongPassword || e.contains(QLatin1String("wrong password"))) {
        return QCoreApplication::translate("Restic", "The password doesn't open the backups on this drive.");
    }
    if (code == Restic::NoRepository || e.contains(QLatin1String("repository does not exist")) || e.contains(QLatin1String("unable to open config file"))) {
        return QCoreApplication::translate("Restic", "The backups on this drive couldn't be found. Was the drive unplugged?");
    }
    if (code == Restic::Locked || e.contains(QLatin1String("repository is already locked"))) {
        return QCoreApplication::translate("Restic", "Another backup was using the drive. It will try again later.");
    }
    if (e.contains(QLatin1String("no space left"))) {
        return QCoreApplication::translate("Restic", "The backup drive is full.");
    }
    if (e.contains(QLatin1String("read-only file system"))) {
        return QCoreApplication::translate("Restic", "The backup drive can be read but not written to.");
    }
    if (e.contains(QLatin1String("input/output error"))) {
        return QCoreApplication::translate("Restic", "The backup drive couldn't be read or written. It may have been unplugged, or it may be failing.");
    }
    if (code == Restic::Interrupted) {
        return QCoreApplication::translate("Restic", "The backup was stopped.");
    }
    if (code == Restic::NotStarted) {
        return QCoreApplication::translate("Restic", "The backup tool that comes with Wild OS couldn't be started. Installing the latest updates may put it right.");
    }
    if (code == Restic::NoPipe) {
        return QCoreApplication::translate("Restic", "Backup couldn't pass the password to its backup tool. Close a few apps and try again.");
    }
    return {};
}

// restic's own words for what went wrong, from the last line it wrote:
// with --json, errors come as {"message_type":"exit_error","message":...}.
QString lastLine(const QString &error)
{
    QString last;
    const QStringList lines = error.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    for (auto it = lines.crbegin(); it != lines.crend() && last.isEmpty(); ++it) {
        const QString l = it->trimmed();
        if (l.startsWith(QLatin1Char('{'))) {
            const QJsonObject o = QJsonDocument::fromJson(l.toUtf8()).object();
            last = o.value(QStringLiteral("message")).toString();
            if (last.isEmpty()) {
                last = o.value(QStringLiteral("error")).toObject().value(QStringLiteral("message")).toString();
            }
        } else {
            last = l;
        }
    }
    last = last.trimmed();
    if (last.startsWith(QLatin1String("Fatal: "))) {
        last = last.mid(7);
    }
    if (last.isEmpty()) {
        return {};
    }
    last[0] = last.at(0).toUpper();
    if (!last.endsWith(QLatin1Char('.'))) {
        last += QLatin1Char('.');
    }
    return last;
}
}

QString Restic::explain(int code, const QString &error)
{
    const QString known = knownProblem(code, error);
    if (!known.isEmpty()) {
        return known;
    }
    return QCoreApplication::translate("Restic", "Something went wrong with the backups on the drive. Try again. If it keeps happening, report a problem.");
}

QString Restic::detail(int code, const QString &error)
{
    if (!knownProblem(code, error).isEmpty()) {
        return {};
    }
    const QString last = lastLine(error);
    return last.isEmpty() ? QCoreApplication::translate("Restic", "restic stopped with error %1 and didn't say why.").arg(code) : last;
}
