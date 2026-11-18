#include "walletkey.h"

#include <QDir>
#include <QFile>
#include <QPasswordDigestor>

#include <cerrno>
#include <cstring>
#include <fcntl.h>
#include <grp.h>
#include <pwd.h>
#include <sys/random.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>

namespace
{
const char storeDir[] = "/var/lib/wildos/wallet";
const char pinDir[] = "/var/lib/wildos/pin";
const char pinSignInDir[] = "/run/wildos-pin";

QByteArray keyPath(uid_t uid)
{
    return QByteArray(storeDir) + '/' + QByteArray::number(uint(uid)) + ".key";
}

void syncDir(const char *path)
{
    const int dir = ::open(path, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (dir >= 0) {
        ::fsync(dir);
        ::close(dir);
    }
}

// Made by root, for root: anything else there (a link, someone else's
// folder) means the store cannot be trusted.
bool ensureStoreDir()
{
    ::mkdir("/var/lib/wildos", 0755);
    if (::mkdir(storeDir, 0700) != 0 && errno != EEXIST) {
        return false;
    }
    struct stat st;
    return ::lstat(storeDir, &st) == 0 && S_ISDIR(st.st_mode) && st.st_uid == 0 && ::chmod(storeDir, 0700) == 0;
}

ssize_t writeAll(int fd, const char *data, size_t size)
{
    size_t done = 0;
    while (done < size) {
        const ssize_t n = ::write(fd, data + done, size - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            return -1;
        }
        done += size_t(n);
    }
    return ssize_t(done);
}

ssize_t readAll(int fd, char *data, size_t size)
{
    size_t done = 0;
    while (done < size) {
        const ssize_t n = ::read(fd, data + done, size - done);
        if (n < 0 && errno == EINTR) {
            continue;
        }
        if (n <= 0) {
            break;
        }
        done += size_t(n);
    }
    return ssize_t(done);
}

void mkpath(const char *path)
{
    char buffer[4096];
    const size_t size = ::strlen(path);
    if (size >= sizeof buffer) {
        return;
    }
    ::memcpy(buffer, path, size + 1);
    for (char *p = buffer + 1; *p; ++p) {
        if (*p == '/') {
            *p = '\0';
            ::mkdir(buffer, 0755);
            *p = '/';
        }
    }
    ::mkdir(buffer, 0700);
}

// The child's half of accountSalt: only plain system calls between fork
// and _exit, since the parent has other threads.
[[noreturn]] void saltAsAccount(const struct passwd *pw, const char *dir, const char *path, int out)
{
    if (::setgroups(0, nullptr) != 0 || ::setgid(pw->pw_gid) != 0 || ::setuid(pw->pw_uid) != 0 || ::getuid() != pw->pw_uid) {
        ::_exit(1);
    }
    struct stat st;
    if (::stat(path, &st) != 0 || st.st_size == 0 || !S_ISREG(st.st_mode)) {
        ::unlink(path);
        mkpath(dir);
        char fresh[WalletKey::saltSize];
        if (::getrandom(fresh, sizeof fresh, 0) != sizeof fresh) {
            ::_exit(1);
        }
        const int fd = ::open(path, O_CREAT | O_WRONLY | O_TRUNC | O_CLOEXEC, 0600);
        if (fd < 0 || writeAll(fd, fresh, sizeof fresh) != sizeof fresh || ::fsync(fd) != 0) {
            ::unlink(path);
            ::_exit(1);
        }
        ::close(fd);
    }
    char salt[WalletKey::saltSize];
    const int fd = ::open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0 || readAll(fd, salt, sizeof salt) != sizeof salt || writeAll(out, salt, sizeof salt) != sizeof salt) {
        ::_exit(1);
    }
    ::_exit(0);
}
}

namespace WalletKey
{
QByteArray derive(const QByteArray &password, const QByteArray &salt)
{
    return QPasswordDigestor::deriveKeyPbkdf2(QCryptographicHash::Sha512, password, salt, iterations, keySize);
}

QByteArray accountSalt(const struct passwd *pw)
{
    const QByteArray dir = QByteArray(pw->pw_dir) + "/.local/share/kwalletd";
    const QByteArray path = dir + "/kdewallet.salt";
    int pipeFds[2];
    if (::pipe2(pipeFds, O_CLOEXEC) != 0) {
        return {};
    }
    const pid_t pid = ::fork();
    if (pid < 0) {
        ::close(pipeFds[0]);
        ::close(pipeFds[1]);
        return {};
    }
    if (pid == 0) {
        ::close(pipeFds[0]);
        saltAsAccount(pw, dir.constData(), path.constData(), pipeFds[1]);
    }
    ::close(pipeFds[1]);
    QByteArray salt(saltSize, '\0');
    const ssize_t got = readAll(pipeFds[0], salt.data(), size_t(salt.size()));
    ::close(pipeFds[0]);
    int status = 0;
    while (::waitpid(pid, &status, 0) < 0 && errno == EINTR) {
    }
    if (got != saltSize || !WIFEXITED(status) || WEXITSTATUS(status) != 0) {
        return {};
    }
    return salt;
}

QString autoLoginUser()
{
    QStringList files;
    for (const QString &dir : {QStringLiteral("/usr/lib/sddm/sddm.conf.d"), QStringLiteral("/etc/sddm.conf.d")}) {
        const auto names = QDir(dir).entryList({QStringLiteral("*.conf")}, QDir::Files, QDir::Name);
        for (const QString &name : names) {
            files << dir + QLatin1Char('/') + name;
        }
    }
    files << QStringLiteral("/etc/sddm.conf");
    QString user;
    for (const QString &path : std::as_const(files)) {
        QFile file(path);
        if (!file.open(QIODevice::ReadOnly)) {
            continue;
        }
        QString section;
        while (!file.atEnd()) {
            const QString line = QString::fromUtf8(file.readLine()).trimmed();
            if (line.startsWith(QLatin1Char('['))) {
                section = line;
            } else if (section == QLatin1String("[Autologin]") && line.startsWith(QLatin1String("User="))) {
                user = line.mid(5).trimmed();
            }
        }
    }
    return user;
}

bool store(uid_t uid, const QByteArray &key)
{
    if (key.size() != keySize || !ensureStoreDir()) {
        return false;
    }
    const QByteArray target = keyPath(uid);
    const QByteArray temp = target + ".new";
    ::unlink(temp.constData());
    const int fd = ::open(temp.constData(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC | O_NOFOLLOW, 0600);
    if (fd < 0) {
        return false;
    }
    const bool ok = writeAll(fd, key.constData(), size_t(key.size())) == key.size() && ::fchmod(fd, 0600) == 0 && ::fsync(fd) == 0;
    ::close(fd);
    if (!ok || ::rename(temp.constData(), target.constData()) != 0) {
        ::unlink(temp.constData());
        return false;
    }
    syncDir(storeDir);
    return true;
}

QByteArray load(uid_t uid)
{
    const int fd = ::open(keyPath(uid).constData(), O_RDONLY | O_CLOEXEC | O_NOFOLLOW);
    if (fd < 0) {
        return {};
    }
    struct stat st;
    QByteArray key(keySize, '\0');
    const bool ok = ::fstat(fd, &st) == 0 && S_ISREG(st.st_mode) && st.st_uid == 0 && st.st_size == keySize
        && readAll(fd, key.data(), size_t(key.size())) == keySize;
    ::close(fd);
    return ok ? key : QByteArray();
}

void forget(uid_t uid)
{
    if (::unlink(keyPath(uid).constData()) == 0) {
        syncDir(storeDir);
    }
}

bool hasPin(const QByteArray &name)
{
    if (name.isEmpty() || name == "." || name == ".." || name.contains('/')) {
        return false;
    }
    struct stat st;
    return ::lstat((QByteArray(pinDir) + '/' + name).constData(), &st) == 0 && S_ISREG(st.st_mode);
}

bool takePinSignIn(uid_t uid)
{
    return ::unlink((QByteArray(pinSignInDir) + '/' + QByteArray::number(uint(uid))).constData()) == 0;
}

void forgetOthers()
{
    const QByteArray user = autoLoginUser().toUtf8();
    const struct passwd *pw = user.isEmpty() ? nullptr : ::getpwnam(user.constData());
    const QString keep = pw ? QString::number(uint(pw->pw_uid)) + QStringLiteral(".key") : QString();
    const auto names = QDir(QString::fromLatin1(storeDir)).entryList(QDir::Files | QDir::System | QDir::Hidden);
    bool removed = false;
    for (const QString &name : names) {
        bool numeric = false;
        const uint uid = name.endsWith(QLatin1String(".key")) ? name.chopped(4).toUInt(&numeric) : 0;
        const struct passwd *owner = numeric ? ::getpwuid(uid) : nullptr;
        if (name != keep && !(owner && hasPin(owner->pw_name))) {
            removed |= ::unlink((QByteArray(storeDir) + '/' + QFile::encodeName(name)).constData()) == 0;
        }
    }
    if (removed) {
        syncDir(storeDir);
    }
}
}
