// wildos-pin: keeps and checks the PIN that unlocks the lock screen and
// signs in on the sign-in screen.
//
// A PIN is 4 to 12 digits, set in Settings > Accounts > Sign-in options.
// sudo, administrator prompts and the encrypted disk keep asking for the
// password.
//
// The lock screen runs as the person, who must not be able to read the
// PIN's hash (four digits fall to guessing in no time once the hash is
// out), so this runs as root behind wildos-pin.socket, one connection each
// (wildos-pin@.service). For a person it only ever acts on the account at
// the other end of the socket, found from the socket itself, never from
// what is sent. Root, which could read the hashes anyway, names the
// account: that is the sign-in screen's helper (pam_wildos_pin).
//
// A request is a few lines, then the sender closes its side:
//
//   status                  "set" or "none"
//   check, PIN              "ok", "no", "none", or "wait SECONDS"
//   set, PASSWORD, PIN      "ok", "password" (wrong), "pin" (not 4 to 12
//                           digits), or "error"
//   remove                  "ok"
//   signin, USER, PIN       root only; answers as check does
//
// The right PIN at sign-in is noted in /run/wildos-pin/UID. The session
// that follows typed no password, so its saved passwords open with the key
// wildos-wallet-keys keeps, and that note is what lets the key out, once.
//
// Five wrong PINs in a row make the next one wait a minute, and each wrong
// one after that doubles the wait, up to an hour. The password unlocks all
// the while, and the right PIN after a wait starts the count again.
//
// Hashes are yescrypt, as the account passwords are, in
// /var/lib/wildos/pin/USER, readable by root alone. The wrong-PIN count is
// beside it as USER.tries.

#define _GNU_SOURCE
#include <crypt.h>
#include <errno.h>
#include <fcntl.h>
#include <pwd.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define STORE "/var/lib/wildos/pin"
#define SIGNED_IN "/run/wildos-pin"
#define CHKPWD "/usr/sbin/unix_chkpwd"
#define MAX_REQUEST 1024
#define MIN_DIGITS 4
#define MAX_DIGITS 12
#define FREE_TRIES 5
#define LONGEST_WAIT 3600

static int name_ok(const char *user)
{
    if (!user || !*user || !strcmp(user, ".") || !strcmp(user, "..") || strlen(user) > 64)
        return 0;
    return strchr(user, '/') == NULL;
}

static int pin_ok(const char *pin)
{
    size_t len = strlen(pin);

    if (len < MIN_DIGITS || len > MAX_DIGITS)
        return 0;
    return strspn(pin, "0123456789") == len;
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

static void answer(const char *text)
{
    write_all(STDOUT_FILENO, text, strlen(text));
}

// unix_chkpwd checks any account's password when run by root, reading it
// from stdin up to a NUL.
static int password_right(const char *user, const char *password)
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
    write_all(in[1], password, strlen(password) + 1);
    close(in[1]);
    while (waitpid(pid, &status, 0) < 0 && errno == EINTR)
        ;
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

static int read_file(const char *path, char *buf, size_t size)
{
    int fd = open(path, O_RDONLY | O_NOFOLLOW | O_CLOEXEC);
    ssize_t got;

    if (fd < 0)
        return -1;
    got = read(fd, buf, size - 1);
    close(fd);
    if (got < 0)
        return -1;
    buf[got] = '\0';
    buf[strcspn(buf, "\n")] = '\0';
    return 0;
}

static int write_file(const char *path, const char *text)
{
    char tmp[400];
    int fd;

    snprintf(tmp, sizeof tmp, "%s.new", path);
    fd = open(tmp, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd < 0)
        return -1;
    if (write_all(fd, text, strlen(text)) < 0 || write_all(fd, "\n", 1) < 0 || fchmod(fd, 0600) < 0 || fsync(fd) < 0) {
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

static int same(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b), i;
    unsigned char diff = la != lb;

    for (i = 0; i < la && i < lb; i++)
        diff |= (unsigned char)(a[i] ^ b[i]);
    return !diff;
}

static int set_pin(const char *hash_path, const char *tries_path, const char *pin)
{
    struct crypt_data data;
    char salt[CRYPT_GENSALT_OUTPUT_SIZE];
    const char *hash;
    int ok;

    memset(&data, 0, sizeof data);
    if (!crypt_gensalt_rn("$y$", 0, NULL, 0, salt, sizeof salt))
        return -1;
    hash = crypt_r(pin, salt, &data);
    if (!hash || hash[0] == '*')
        return -1;
    ok = write_file(hash_path, hash);
    explicit_bzero(&data, sizeof data);
    if (ok == 0)
        unlink(tries_path);
    return ok;
}

// "ok", "no" or "wait N": counts wrong PINs and makes guessing wait.
// Returns 1 for the right PIN.
static int check_pin(const char *hash_path, const char *tries_path, const char *pin)
{
    char stored[CRYPT_OUTPUT_SIZE];
    char tries[64] = "";
    struct crypt_data data;
    const char *hash;
    long count = 0;
    long long last = 0;
    time_t now = time(NULL);
    int right;

    if (read_file(hash_path, stored, sizeof stored) < 0) {
        answer("none");
        return 0;
    }
    if (read_file(tries_path, tries, sizeof tries) == 0)
        sscanf(tries, "%ld %lld", &count, &last);
    if (count >= FREE_TRIES) {
        long shift = count - FREE_TRIES;
        long wait = shift >= 6 ? LONGEST_WAIT : 60L << shift;
        long long since = (long long)now - last;
        // A clock moved back does not shorten the wait.
        if (since < 0)
            since = 0;
        if (wait > LONGEST_WAIT)
            wait = LONGEST_WAIT;
        if (since < wait) {
            char text[32];
            snprintf(text, sizeof text, "wait %lld", (long long)wait - since);
            answer(text);
            return 0;
        }
    }
    memset(&data, 0, sizeof data);
    hash = pin_ok(pin) ? crypt_r(pin, stored, &data) : NULL;
    right = hash && hash[0] != '*' && same(hash, stored);
    explicit_bzero(&data, sizeof data);
    if (right) {
        unlink(tries_path);
        return 1;
    }
    snprintf(tries, sizeof tries, "%ld %lld", count + 1, (long long)now);
    write_file(tries_path, tries);
    sleep(1);
    answer("no");
    return 0;
}

static void note_sign_in(uid_t uid)
{
    char path[64];
    int fd;

    snprintf(path, sizeof path, "%s/%u", SIGNED_IN, (unsigned)uid);
    fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW | O_CLOEXEC, 0600);
    if (fd >= 0)
        close(fd);
}

static int serve(void)
{
    struct ucred cred;
    socklen_t size = sizeof cred;
    struct passwd *pw;
    char request[MAX_REQUEST + 1];
    char hash_path[256], tries_path[256];
    char *lines[3] = {NULL, NULL, NULL};
    char *p;
    size_t len = 0;
    int count = 0;
    int by_root;
    int lock;

    if (getsockopt(STDIN_FILENO, SOL_SOCKET, SO_PEERCRED, &cred, &size) < 0)
        return 1;
    by_root = cred.uid == 0;
    while (len < MAX_REQUEST) {
        ssize_t got = read(STDIN_FILENO, request + len, MAX_REQUEST - len);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        len += (size_t)got;
    }
    request[len] = '\0';
    // A NUL in the middle would hide what follows it.
    if (strlen(request) != len) {
        explicit_bzero(request, sizeof request);
        return 1;
    }
    for (p = request; p && count < 3; count++) {
        lines[count] = p;
        p = strchr(p, '\n');
        if (p)
            *p++ = '\0';
    }
    if (by_root) {
        pw = count == 3 && !strcmp(lines[0], "signin") && name_ok(lines[1]) ? getpwnam(lines[1]) : NULL;
        if (pw && pw->pw_uid < 1000)
            pw = NULL;
    } else {
        pw = getpwuid(cred.uid);
    }
    if (!pw || !name_ok(pw->pw_name)) {
        explicit_bzero(request, sizeof request);
        return 1;
    }
    snprintf(hash_path, sizeof hash_path, "%s/%s", STORE, pw->pw_name);
    snprintf(tries_path, sizeof tries_path, "%s/%s.tries", STORE, pw->pw_name);

    // One request at a time, so guesses sent together are still counted
    // one after the other.
    lock = open(STORE, O_RDONLY | O_DIRECTORY | O_CLOEXEC);
    if (lock < 0 || flock(lock, LOCK_EX) < 0) {
        explicit_bzero(request, sizeof request);
        answer("error");
        return 0;
    }

    if (by_root) {
        if (check_pin(hash_path, tries_path, lines[2])) {
            note_sign_in(pw->pw_uid);
            answer("ok");
        }
    } else if (!strcmp(lines[0], "status") && count == 1) {
        answer(access(hash_path, F_OK) == 0 ? "set" : "none");
    } else if (!strcmp(lines[0], "check") && count == 2) {
        if (check_pin(hash_path, tries_path, lines[1]))
            answer("ok");
    } else if (!strcmp(lines[0], "set") && count == 3) {
        if (!pin_ok(lines[2])) {
            answer("pin");
        } else if (!password_right(pw->pw_name, lines[1])) {
            sleep(2);
            answer("password");
        } else {
            answer(set_pin(hash_path, tries_path, lines[2]) == 0 ? "ok" : "error");
        }
    } else if (!strcmp(lines[0], "remove") && count == 1) {
        unlink(hash_path);
        unlink(tries_path);
        answer("ok");
    } else {
        answer("error");
    }
    explicit_bzero(request, sizeof request);
    return 0;
}

int main(int argc, char **argv)
{
    signal(SIGPIPE, SIG_IGN);
    if (argc == 2 && !strcmp(argv[1], "--serve"))
        return serve();
    fprintf(stderr, "wildos-pin runs from wildos-pin@.service\n");
    return 2;
}
