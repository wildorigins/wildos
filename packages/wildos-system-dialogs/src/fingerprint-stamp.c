// fingerprint-stamp: notes that a password was just typed, for Advanced
// protection's "Fingerprint needs your password sometimes".
//
// fingerprint-turn and pam_wildos_fingerprint turn the finger down after a
// restart until the password has been typed once, and again once the
// chosen hours have passed. They read /run/wildos/fingerprint/USER, which
// holds the seconds since start-up at the last password sign-in: /run is
// empty after a restart, and seconds since start-up don't move when the
// clock is changed.
//
// The file is root's, so nothing running as the person can make the
// finger last longer. PAM runs this after the password with pam_exec's
// expose_authtok, and it checks the password again itself with
// unix_chkpwd before writing anything:
//
//   fingerprint-stamp          From PAM. As root (sign-in screen, sudo,
//                              administrator prompts) it checks and writes
//                              for PAM_USER; as the person (the lock
//                              screen) it hands the password to the
//                              service below, which can write the file.
//   fingerprint-stamp --serve  fingerprint-stamp@.service, one connection
//                              on fingerprint-stamp.socket as its stdin.
//                              Only ever notes the password of the account
//                              on the other end of the socket.
//
// A wrong password writes nothing; the service then waits two seconds, so
// it is no quicker to guess with than the lock screen.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define STAMPS "/run/wildos/fingerprint"
#define SOCKET_PATH "/run/wildos/fingerprint-stamp.socket"
#define CHKPWD "/usr/sbin/unix_chkpwd"
// PAM_MAX_RESP_SIZE, the most pam_exec hands over.
#define MAX_PASSWORD 512

static int name_ok(const char *user)
{
    if (!user || !*user || !strcmp(user, ".") || !strcmp(user, "..") || strlen(user) > 64)
        return 0;
    return strchr(user, '/') == NULL;
}

// Up to the NUL pam_exec ends it with, or the end of the input.
static ssize_t read_password(int fd, char *buf, size_t size)
{
    size_t n = 0;

    while (n < size) {
        ssize_t got = read(fd, buf + n, size - n);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        void *nul = memchr(buf + n, '\0', (size_t)got);
        n += (size_t)got;
        if (nul) {
            n = (size_t)((char *)nul - buf);
            break;
        }
    }
    return (ssize_t)n;
}

static int write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t put = write(fd, buf, len);
        if (put < 0 && errno == EINTR)
            continue;
        if (put <= 0)
            return -1;
        buf += put;
        len -= (size_t)put;
    }
    return 0;
}

// unix_chkpwd checks any account's password when run by root, reading it
// from stdin up to a NUL.
static int password_right(const char *user, const char *password, size_t len)
{
    int in[2];
    pid_t pid;
    int status = 0;

    if (pipe2(in, O_CLOEXEC) < 0)
        return 0;
    pid = fork();
    if (pid < 0) {
        close(in[0]);
        close(in[1]);
        return 0;
    }
    if (pid == 0) {
        int null = open("/dev/null", O_RDWR);
        dup2(in[0], STDIN_FILENO);
        if (null >= 0) {
            dup2(null, STDOUT_FILENO);
            dup2(null, STDERR_FILENO);
        }
        execl(CHKPWD, CHKPWD, user, "nonull", (char *)NULL);
        _exit(127);
    }
    close(in[0]);
    signal(SIGPIPE, SIG_IGN);
    write_all(in[1], password, len);
    write_all(in[1], "", 1);
    close(in[1]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int write_stamp(const char *user)
{
    char path[256];
    char tmp[300];
    char text[32];
    struct timespec now;
    int fd;
    int len;

    if (clock_gettime(CLOCK_BOOTTIME, &now) < 0)
        return -1;
    mkdir("/run/wildos", 0755);
    if (mkdir(STAMPS, 0755) < 0 && errno != EEXIST)
        return -1;
    snprintf(path, sizeof path, "%s/%s", STAMPS, user);
    snprintf(tmp, sizeof tmp, "%s/.%s.new", STAMPS, user);
    len = snprintf(text, sizeof text, "%lld\n", (long long)now.tv_sec);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0644);
    if (fd < 0)
        return -1;
    if (write_all(fd, text, (size_t)len) < 0 || fchmod(fd, 0644) < 0) {
        close(fd);
        unlink(tmp);
        return -1;
    }
    close(fd);
    if (rename(tmp, path) < 0) {
        unlink(tmp);
        return -1;
    }
    return 0;
}

static int serve(void)
{
    struct ucred cred;
    socklen_t size = sizeof cred;
    struct passwd *pw;
    char password[MAX_PASSWORD];
    ssize_t len;
    int ok = 0;

    if (getsockopt(STDIN_FILENO, SOL_SOCKET, SO_PEERCRED, &cred, &size) < 0)
        return 1;
    pw = getpwuid(cred.uid);
    if (!pw || cred.uid == 0 || !name_ok(pw->pw_name))
        return 1;
    len = read_password(STDIN_FILENO, password, sizeof password);
    if (len > 0 && password_right(pw->pw_name, password, (size_t)len))
        ok = write_stamp(pw->pw_name) == 0;
    explicit_bzero(password, sizeof password);
    if (!ok)
        sleep(2);
    write_all(STDOUT_FILENO, ok ? "1" : "0", 1);
    // The answer goes back on the socket; a wrong password is no failure of
    // the service's own.
    return 0;
}

static int hand_over(const char *password, size_t len)
{
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    char answer = '0';
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if (fd < 0)
        return 1;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof addr.sun_path - 1);
    signal(SIGPIPE, SIG_IGN);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0 || write_all(fd, password, len) < 0) {
        close(fd);
        return 1;
    }
    shutdown(fd, SHUT_WR);
    while (read(fd, &answer, 1) < 0 && errno == EINTR)
        ;
    close(fd);
    return answer == '1' ? 0 : 1;
}

int main(int argc, char **argv)
{
    const char *type = getenv("PAM_TYPE");
    const char *user = getenv("PAM_USER");
    char password[MAX_PASSWORD];
    ssize_t len;
    int status = 1;

    if (argc == 2 && !strcmp(argv[1], "--serve"))
        return serve();
    if (argc != 1)
        return 2;
    // pam_exec also runs for the other PAM steps; only a sign-in counts.
    if (!type || strcmp(type, "auth") || !name_ok(user))
        return 0;
    len = read_password(STDIN_FILENO, password, sizeof password);
    if (len <= 0)
        return 1;
    if (geteuid() == 0) {
        if (password_right(user, password, (size_t)len))
            status = write_stamp(user) == 0 ? 0 : 1;
    } else {
        const struct passwd *pw = getpwuid(getuid());
        if (pw && !strcmp(pw->pw_name, user))
            status = hand_over(password, (size_t)len);
    }
    explicit_bzero(password, sizeof password);
    return status;
}
