#include "tools.h"

#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QProcessEnvironment>
#include <QStandardPaths>
#include <QTimeZone>

#include <cerrno>
#include <csignal>
#include <cstring>
#include <fcntl.h>
#include <ftw.h>
#include <mutex>
#include <poll.h>
#include <pty.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <unistd.h>
#include <vector>
#include <QCoreApplication>

namespace
{
// What unar's getpass() shows. The tools print plain English that is
// never translated, so these are stable.
constexpr char unarPrompt[] = "(will not be shown): ";
constexpr char sevenZipPrompt[] = "Enter password";

// So a tool that quits before reading its password cannot kill Archive
// with SIGPIPE while the password is written to it.
void ignoreSigpipe()
{
    static std::once_flag once;
    std::call_once(once, [] {
        ::signal(SIGPIPE, SIG_IGN);
    });
}

bool writeAll(int fd, const char *data, size_t size)
{
    while (size > 0) {
        const ssize_t n = ::write(fd, data, size);
        if (n < 0) {
            if (errno == EINTR) {
                continue;
            }
            return false;
        }
        data += n;
        size -= size_t(n);
    }
    return true;
}

void closeFd(int &fd)
{
    if (fd >= 0) {
        ::close(fd);
        fd = -1;
    }
}

// The tools print paths and their own messages in UTF-8 under this
// locale, and 7z decodes a typed password as UTF-8 under it.
std::vector<QByteArray> environment()
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.insert(QStringLiteral("LC_ALL"), QStringLiteral("C.UTF-8"));
    env.insert(QStringLiteral("LANG"), QStringLiteral("C.UTF-8"));
    env.remove(QStringLiteral("LANGUAGE"));
    std::vector<QByteArray> out;
    for (const QString &pair : env.toStringList()) {
        out.push_back(pair.toLocal8Bit());
    }
    return out;
}

QDateTime localTime(const QString &text)
{
    // 7z prints local time, to a tenth of a microsecond or less.
    const QDateTime t = QDateTime::fromString(text.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    return t.isValid() ? t : QDateTime();
}

QDateTime lsarTime(const QString &text)
{
    // "2026-11-07 17:31:05 +0000"
    const QDateTime t = QDateTime::fromString(text.left(19), QStringLiteral("yyyy-MM-dd HH:mm:ss"));
    if (!t.isValid()) {
        return QDateTime();
    }
    int offset = 0;
    const QString zone = text.mid(20).trimmed();
    if (zone.size() == 5 && (zone.at(0) == QLatin1Char('+') || zone.at(0) == QLatin1Char('-'))) {
        offset = (zone.mid(1, 2).toInt() * 60 + zone.mid(3, 2).toInt()) * 60;
        if (zone.at(0) == QLatin1Char('-')) {
            offset = -offset;
        }
    }
    return QDateTime(t.date(), t.time(), QTimeZone::fromSecondsAheadOfUtc(offset)).toLocalTime();
}
}

namespace Tools
{

bool available(const QString &program)
{
    return !QStandardPaths::findExecutable(program).isEmpty();
}

void wipe(QByteArray &bytes)
{
    if (!bytes.isEmpty()) {
        // Through constData so the shared buffer is cleared rather than a
        // detached copy.
        volatile char *p = const_cast<char *>(bytes.constData());
        for (qsizetype i = 0; i < bytes.size(); ++i) {
            p[i] = 0;
        }
    }
    bytes.clear();
}

void removeTree(const QString &path)
{
    if (path.isEmpty()) {
        return;
    }
    ::nftw(
        QFile::encodeName(path).constData(),
        [](const char *name, const struct stat *, int type, struct FTW *) -> int {
            if (type == FTW_DP) {
                ::rmdir(name);
            } else {
                ::unlink(name);
            }
            return 0;
        },
        32, FTW_DEPTH | FTW_PHYS);
}

QString makeStaging(const QString &parent)
{
    QByteArray name = QFile::encodeName(parent + QStringLiteral("/.wildos-archive-XXXXXX"));
    if (!::mkdtemp(name.data())) {
        return QString();
    }
    return QFile::decodeName(name);
}

Outcome run(const Job &job, Progress *progress)
{
    Outcome outcome;
    const QString exe = QStandardPaths::findExecutable(job.program);
    if (exe.isEmpty()) {
        outcome.errors = job.program.toUtf8() + " isn't installed";
        return outcome;
    }
    ignoreSigpipe();

    // Everything the child needs is made before fork: in a process with
    // threads, only async-signal-safe calls are allowed between fork and
    // exec.
    std::vector<QByteArray> args;
    args.push_back(QFile::encodeName(exe));
    for (const QString &a : job.arguments) {
        args.push_back(QFile::encodeName(a));
    }
    std::vector<char *> argv;
    for (QByteArray &a : args) {
        argv.push_back(a.data());
    }
    argv.push_back(nullptr);
    std::vector<QByteArray> env = environment();
    std::vector<char *> envp;
    for (QByteArray &e : env) {
        envp.push_back(e.data());
    }
    envp.push_back(nullptr);

    int in[2] = {-1, -1};
    int out[2] = {-1, -1};
    int err[2] = {-1, -1};
    int master = -1;
    int slave = -1;
    if (job.terminal) {
        struct winsize size = {};
        size.ws_row = 24;
        size.ws_col = 4000;
        if (::openpty(&master, &slave, nullptr, nullptr, &size) != 0) {
            outcome.errors = "Couldn't make a terminal for " + job.program.toUtf8();
            return outcome;
        }
        ::fcntl(master, F_SETFD, FD_CLOEXEC);
        ::fcntl(slave, F_SETFD, FD_CLOEXEC);
        // No echo at all, so nothing typed is ever shown back, even
        // before the tool turns echo off for its prompt.
        struct termios t;
        if (::tcgetattr(slave, &t) == 0) {
            t.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL);
            ::tcsetattr(slave, TCSANOW, &t);
        }
    } else if (::pipe2(in, O_CLOEXEC) != 0 || ::pipe2(out, O_CLOEXEC) != 0 || ::pipe2(err, O_CLOEXEC) != 0) {
        for (int *fd : {&in[0], &in[1], &out[0], &out[1], &err[0], &err[1]}) {
            closeFd(*fd);
        }
        outcome.errors = "Couldn't start " + job.program.toUtf8();
        return outcome;
    }

    const pid_t pid = ::fork();
    if (pid == 0) {
        ::signal(SIGPIPE, SIG_DFL);
        if (job.terminal) {
            // Its own session with the pseudo terminal as its controlling
            // terminal: getpass() reads the password from /dev/tty.
            ::setsid();
            ::ioctl(slave, TIOCSCTTY, 0);
            ::dup2(slave, 0);
            ::dup2(slave, 1);
            ::dup2(slave, 2);
        } else {
            ::setpgid(0, 0);
            ::dup2(in[0], 0);
            ::dup2(out[1], 1);
            ::dup2(err[1], 2);
        }
        ::close_range(3, ~0U, 0);
        ::execve(argv[0], argv.data(), envp.data());
        ::_exit(127);
    }
    if (pid < 0) {
        for (int *fd : {&in[0], &in[1], &out[0], &out[1], &err[0], &err[1], &master, &slave}) {
            closeFd(*fd);
        }
        outcome.errors = "Couldn't start " + job.program.toUtf8();
        return outcome;
    }
    outcome.started = true;
    closeFd(slave);
    closeFd(in[0]);
    closeFd(out[1]);
    closeFd(err[1]);

    if (!job.terminal) {
        // 7z reads one line when it needs the password and nothing when
        // it doesn't. With no password, stdin just ends, and 7z says so.
        if (!job.password.isEmpty()) {
            QByteArray line = job.password + '\n';
            writeAll(in[1], line.constData(), size_t(line.size()));
            wipe(line);
        }
        closeFd(in[1]);
    }

    int killed = 0;
    qint64 killedAt = 0;
    struct timespec started;
    ::clock_gettime(CLOCK_MONOTONIC, &started);
    const auto now = [&] {
        struct timespec t;
        ::clock_gettime(CLOCK_MONOTONIC, &t);
        return qint64(t.tv_sec - started.tv_sec) * 1000 + (t.tv_nsec - started.tv_nsec) / 1000000;
    };
    const auto stop = [&] {
        if (killed == 0) {
            ::kill(-pid, SIGTERM);
            ::kill(pid, SIGTERM);
            killed = 1;
            killedAt = now();
        }
    };

    QByteArray pending;
    const auto lines = [&](const char *data, ssize_t n) {
        if (!job.line) {
            return;
        }
        pending.append(data, n);
        qsizetype start = 0;
        for (qsizetype i = 0; i < pending.size(); ++i) {
            // 7z redraws its progress with backspaces, unar ends lines
            // with \r\n on the terminal.
            const char c = pending.at(i);
            if (c == '\n' || c == '\r' || c == '\b') {
                if (i > start) {
                    job.line(pending.mid(start, i - start));
                }
                start = i + 1;
            }
        }
        pending.remove(0, start);
    };

    bool answered = false;
    qsizetype scanned = 0;
    int status = 0;
    bool reaped = false;
    char buffer[65536];
    for (;;) {
        if (progress && progress->cancel && !outcome.cancelled) {
            outcome.cancelled = true;
            stop();
        }
        if (killed == 1 && now() - killedAt > 1500) {
            ::kill(-pid, SIGKILL);
            ::kill(pid, SIGKILL);
            killed = 2;
        }
        struct pollfd fds[2];
        int count = 0;
        if (job.terminal) {
            if (master >= 0) {
                fds[count++] = {master, POLLIN, 0};
            }
        } else {
            if (out[0] >= 0) {
                fds[count++] = {out[0], POLLIN, 0};
            }
            if (err[0] >= 0) {
                fds[count++] = {err[0], POLLIN, 0};
            }
        }
        if (count == 0) {
            if (!reaped) {
                // Every stream is closed; the tool is finishing or has
                // handed its terminal to nobody. Wait for it, still
                // honouring a cancel.
                const pid_t r = ::waitpid(pid, &status, WNOHANG);
                if (r == pid) {
                    reaped = true;
                } else {
                    ::usleep(20000);
                    continue;
                }
            }
            break;
        }
        const int ready = ::poll(fds, nfds_t(count), 100);
        if (ready < 0 && errno != EINTR) {
            break;
        }
        for (int i = 0; i < count && ready > 0; ++i) {
            if (!(fds[i].revents & (POLLIN | POLLHUP | POLLERR))) {
                continue;
            }
            const ssize_t n = ::read(fds[i].fd, buffer, sizeof buffer);
            if (n <= 0) {
                if (n < 0 && errno == EINTR) {
                    continue;
                }
                // A pseudo terminal reads EIO once the tool has gone.
                if (fds[i].fd == master) {
                    closeFd(master);
                } else if (fds[i].fd == out[0]) {
                    closeFd(out[0]);
                } else {
                    closeFd(err[0]);
                }
                continue;
            }
            if (fds[i].fd == err[0]) {
                if (outcome.errors.size() < 1024 * 1024) {
                    outcome.errors.append(buffer, n);
                }
                lines(buffer, n);
                continue;
            }
            if (job.data && fds[i].fd == out[0]) {
                if (!outcome.stopped && !job.data(buffer, n)) {
                    outcome.stopped = true;
                    stop();
                }
                continue;
            }
            if (outcome.output.size() < 256 * 1024 * 1024) {
                outcome.output.append(buffer, n);
            }
            lines(buffer, n);
            if (job.terminal) {
                const qsizetype at = outcome.output.indexOf(unarPrompt, qMax<qsizetype>(0, scanned - qsizetype(sizeof unarPrompt)));
                scanned = outcome.output.size();
                if (at >= 0) {
                    outcome.output.replace(at, qsizetype(std::strlen(unarPrompt)), "(asked)\n");
                    scanned = outcome.output.size();
                    outcome.askedForPassword = true;
                    if (answered || job.password.isEmpty()) {
                        // Asked again: unar took the first answer as
                        // wrong. Nothing more to try.
                        outcome.askedAgain = answered;
                        stop();
                    } else {
                        QByteArray line = job.password + '\n';
                        writeAll(master, line.constData(), size_t(line.size()));
                        wipe(line);
                        answered = true;
                    }
                }
            }
        }
    }
    closeFd(master);
    closeFd(out[0]);
    closeFd(err[0]);
    closeFd(in[1]);
    if (!reaped) {
        while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
        }
    }
    outcome.exitCode = WIFEXITED(status) ? WEXITSTATUS(status) : -1;
    const QByteArray all = outcome.output + outcome.errors;
    if (all.contains(sevenZipPrompt)) {
        outcome.askedForPassword = true;
    }
    return outcome;
}

Reader::PasswordState passwordOutcome(const Outcome &outcome, const QByteArray &password)
{
    const Reader::PasswordState missing = password.isEmpty() ? Reader::PasswordState::Needed : Reader::PasswordState::Wrong;
    if (outcome.askedAgain) {
        return Reader::PasswordState::Wrong;
    }
    const QByteArray all = (outcome.output + outcome.errors).toLower();
    // 7z: "Wrong password?" and "Cannot open encrypted archive"; unar:
    // "Missing or wrong password".
    if (all.contains("wrong password") || all.contains("cannot open encrypted archive")) {
        return missing;
    }
    if (outcome.askedForPassword && password.isEmpty()) {
        return Reader::PasswordState::Needed;
    }
    return Reader::PasswordState::Fine;
}

Contents list7z(const QString &archive, const QByteArray &password, Progress *progress)
{
    Contents contents;
    Job job;
    job.program = QStringLiteral("7z");
    // -spd: names are names, never wildcards.
    job.arguments = {QStringLiteral("l"), QStringLiteral("-slt"), QStringLiteral("-bsp0"), QStringLiteral("-sccUTF-8"), QStringLiteral("-spd"), archive};
    job.password = password;
    const Outcome outcome = run(job, progress);
    if (outcome.cancelled) {
        contents.cancelled = true;
        return contents;
    }
    if (!outcome.started) {
        contents.error = QString::fromUtf8(outcome.errors);
        return contents;
    }
    contents.password = passwordOutcome(outcome, password);
    if (contents.password != Reader::PasswordState::Fine) {
        contents.needsPassword = true;
        return contents;
    }
    if (outcome.exitCode != 0) {
        const QString errors = QString::fromUtf8(outcome.errors).trimmed();
        contents.error = errors.isEmpty() ? QCoreApplication::translate("Tools", "7-Zip couldn't read this archive.") : errors;
        return contents;
    }

    const QList<QByteArray> lines = outcome.output.split('\n');
    bool inItems = false;
    Item item;
    bool haveItem = false;
    const auto finish = [&] {
        if (haveItem) {
            contents.items.append(item);
        }
        item = Item();
        haveItem = false;
    };
    for (const QByteArray &raw : lines) {
        const QString line = QString::fromUtf8(raw).remove(QLatin1Char('\r'));
        if (!inItems) {
            if (line.startsWith(QLatin1String("Type = ")) && contents.type.isEmpty()) {
                contents.type = line.mid(7);
            }
            if (line == QLatin1String("----------")) {
                inItems = true;
            }
            continue;
        }
        if (line.isEmpty()) {
            finish();
            continue;
        }
        const qsizetype eq = line.indexOf(QLatin1String(" = "));
        const QString key = eq < 0 ? line.chopped(line.endsWith(QLatin1String(" =")) ? 2 : 0) : line.left(eq);
        const QString value = eq < 0 ? QString() : line.mid(eq + 3);
        if (key == QLatin1String("Path")) {
            finish();
            item.path = value;
            haveItem = true;
        } else if (key == QLatin1String("Folder")) {
            item.dir = item.dir || value == QLatin1String("+");
        } else if (key == QLatin1String("Size")) {
            item.size = value.toLongLong();
        } else if (key == QLatin1String("Packed Size")) {
            item.packed = value.isEmpty() ? -1 : value.toLongLong();
        } else if (key == QLatin1String("Modified")) {
            item.modified = localTime(value);
        } else if (key == QLatin1String("Attributes")) {
            // "D drwxr-xr-x", "A -rw-r--r--", " lrwxrwxrwx": Windows
            // attributes, then the Unix mode when the archive has one.
            const QStringList parts = value.split(QLatin1Char(' '));
            item.dir = item.dir || parts.value(0).contains(QLatin1Char('D'));
            const QString mode = parts.value(1);
            if (mode.size() == 10) {
                item.dir = item.dir || mode.at(0) == QLatin1Char('d');
                item.symlink = mode.at(0) == QLatin1Char('l');
                item.executable = mode.at(3) == QLatin1Char('x');
            }
        } else if (key == QLatin1String("Encrypted")) {
            item.encrypted = value == QLatin1String("+");
        } else if (key == QLatin1String("Symbolic Link")) {
            if (!value.isEmpty()) {
                item.symlink = true;
                item.linkTarget = value;
            }
        }
    }
    finish();
    contents.ok = true;
    return contents;
}

Contents listLsar(const QString &archive, Progress *progress)
{
    Contents contents;
    Job job;
    job.program = QStringLiteral("lsar");
    // -ja: plain ASCII JSON, every other character escaped. -nr: what is
    // in the archive, not inside archives in it.
    job.arguments = {QStringLiteral("-ja"), QStringLiteral("-nr"), archive};
    const Outcome outcome = run(job, progress);
    if (outcome.cancelled) {
        contents.cancelled = true;
        return contents;
    }
    if (!outcome.started) {
        contents.error = QString::fromUtf8(outcome.errors);
        return contents;
    }
    if (outcome.errors.contains("requires a password")) {
        contents.needsPassword = true;
        contents.password = Reader::PasswordState::Needed;
        return contents;
    }
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(outcome.output, &parseError);
    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        const QString errors = QString::fromUtf8(outcome.errors).trimmed();
        contents.error = errors.isEmpty() ? QCoreApplication::translate("Tools", "The Unarchiver couldn't read this archive.") : errors;
        return contents;
    }
    const QJsonObject root = document.object();
    contents.type = root.value(QLatin1String("lsarFormatName")).toString();
    const QJsonArray list = root.value(QLatin1String("lsarContents")).toArray();
    for (qsizetype i = 0; i < list.size(); ++i) {
        const QJsonObject o = list.at(i).toObject();
        // Archive picks entries for unar by this index, so the listing
        // must have them in that order.
        if (o.value(QLatin1String("XADIndex")).toInt(-1) != int(i)) {
            contents.error = QCoreApplication::translate("Tools", "The Unarchiver listed this archive out of order.");
            return contents;
        }
        Item item;
        // Mac resource forks sit beside their files; unar is told to
        // skip them, so they are left out here too.
        if (!o.value(QLatin1String("XADIsResourceFork")).toInt()) {
            item.path = o.value(QLatin1String("XADFileName")).toString();
        }
        item.dir = o.value(QLatin1String("XADIsDirectory")).toInt() != 0;
        item.symlink = o.value(QLatin1String("XADIsLink")).toInt() != 0;
        item.linkTarget = o.value(QLatin1String("XADLinkDestination")).toString();
        item.size = qint64(o.value(QLatin1String("XADFileSize")).toDouble());
        item.packed = o.contains(QLatin1String("XADCompressedSize")) ? qint64(o.value(QLatin1String("XADCompressedSize")).toDouble()) : -1;
        item.modified = lsarTime(o.value(QLatin1String("XADLastModificationDate")).toString());
        item.encrypted = o.value(QLatin1String("XADIsEncrypted")).toInt() != 0;
        item.executable = (o.value(QLatin1String("XADPosixPermissions")).toInt() & 0111) != 0;
        contents.items.append(item);
    }
    contents.ok = true;
    return contents;
}

}
