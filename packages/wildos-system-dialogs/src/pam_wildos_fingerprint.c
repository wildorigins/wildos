// pam_wildos_fingerprint: sudo in a terminal takes a password or a finger,
// whichever comes first, from one prompt.
//
// pam_fprintd can only wait for the finger or let the password through, one
// after the other, so Wild OS's common-auth asks for an empty password to mean
// "use my finger" (fingerprint-turn). Here the reader listens while the
// password is typed: fprintd's own fprintd-verify runs in a child process,
// and the terminal and the child's output are watched together with poll().
// Nothing is interrupted with signals or cancelled threads, so the terminal
// always gets its echo back.
//
// A typed password is handed on as PAM_AUTHTOK and checked by pam_unix as
// before, with faillock and Kicksecure's checks untouched. A finger returns
// PAM_SUCCESS, which the wildos-fingerprint profile turns into a jump to
// faillock's authsucc, as pam_fprintd's success already did.
//
// Whenever this cannot do the job properly it returns PAM_IGNORE and changes
// nothing, so the stack carries on exactly as it did before this module:
// another service than sudo, sudo -n/-S/-A or a custom -p prompt, no
// terminal, the "administrator prompts" fingerprint switch off, no enrolled
// finger, or a locked password.

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <ftw.h>
#include <poll.h>
#include <shadow.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <security/pam_ext.h>
#include <security/pam_modules.h>

#define FPRINTD_VERIFY "/usr/bin/fprintd-verify"
#define SWITCHES "/etc/wildos/fingerprint.conf"
#define PRINTS "/var/lib/fprint"
#define ADVANCED "/etc/wildos/advanced-protection"
#define STAMPS "/run/wildos/fingerprint"
#define FINGER_TRIES 3
// sudo's own default password timeout.
#define WAIT_SECONDS (5 * 60)

static volatile sig_atomic_t interrupted;

static void on_interrupt(int sig)
{
    interrupted = sig;
}

static int name_ok(const char *user)
{
    if (!user || !*user || !strcmp(user, ".") || !strcmp(user, ".."))
        return 0;
    return strchr(user, '/') == NULL;
}

// Settings > Accounts > Sign-in options writes admin=off to stop fingerprints
// at administrator prompts; anything else, or no file, leaves them on.
static int switch_on(void)
{
    FILE *f = fopen(SWITCHES, "re");
    char line[256];
    int on = 1;

    if (!f)
        return 1;
    while (fgets(line, sizeof line, f)) {
        char *p = line;
        while (*p == ' ' || *p == '\t')
            p++;
        if (strncmp(p, "admin", 5))
            continue;
        p += 5;
        while (*p == ' ' || *p == '\t')
            p++;
        if (*p++ != '=')
            continue;
        while (*p == ' ' || *p == '\t')
            p++;
        on = strncmp(p, "off", 3) != 0;
    }
    fclose(f);
    return on;
}

// Settings > Privacy and security > Advanced protection's "Fingerprint
// needs your password sometimes" (fingerprint_hours, off unless set;
// Strict and Maximum set it): no finger after a restart until the password has been typed once,
// nor once that many hours have passed since. fingerprint-stamp notes each
// password sign-in in /run, in seconds since start-up; fingerprint-turn
// reads the same files.
static long finger_hours(void)
{
    FILE *f = fopen(ADVANCED, "re");
    char line[256];
    long hours = 0;

    if (!f)
        return hours;
    while (fgets(line, sizeof line, f)) {
        char *end;
        long value;
        if (strncmp(line, "fingerprint_hours=", 18))
            continue;
        value = strtol(line + 18, &end, 10);
        if (end != line + 18 && (*end == '\n' || *end == '\0') && value >= 0)
            hours = value;
    }
    fclose(f);
    return hours;
}

static int password_recent(const char *user)
{
    long hours = finger_hours();
    char path[512];
    FILE *f;
    long typed = -1;
    double now = -1;

    if (hours == 0)
        return 1;
    if (snprintf(path, sizeof path, "%s/%s", STAMPS, user) >= (int)sizeof path)
        return 0;
    f = fopen(path, "re");
    if (!f)
        return 0;
    if (fscanf(f, "%ld", &typed) != 1)
        typed = -1;
    fclose(f);
    f = fopen("/proc/uptime", "re");
    if (!f)
        return 0;
    if (fscanf(f, "%lf", &now) != 1)
        now = -1;
    fclose(f);
    return typed >= 0 && now >= typed && (long)now - typed < hours * 3600;
}

static int found_print;

static int look(const char *path, const struct stat *st, int type, struct FTW *ftw)
{
    (void)path;
    (void)st;
    (void)ftw;
    if (type == FTW_F) {
        found_print = 1;
        return 1;
    }
    return 0;
}

static int enrolled(const char *user)
{
    char dir[512];

    if (snprintf(dir, sizeof dir, "%s/%s", PRINTS, user) >= (int)sizeof dir)
        return 0;
    found_print = 0;
    nftw(dir, look, 8, FTW_PHYS);
    return found_print;
}

// A locked password (passwd -l) locks the account; a finger must not get
// round it.
static int locked(const char *user)
{
    struct spwd *sp = getspnam(user);

    if (!sp || !sp->sp_pwdp)
        return 1;
    return sp->sp_pwdp[0] == '!' || sp->sp_pwdp[0] == '*';
}

// Only an ordinary interactive sudo: not -n (nothing may be asked), -S (the
// password comes on stdin), -A (an askpass helper asks) or -p (the caller
// chose its own prompt). Reading sudo's options from its command line is
// crude, so anything unexpected counts as "not ordinary" and falls back.
static int ordinary_sudo(void)
{
    static const char *const long_with_arg[] = {
        "user", "group", "close-from", "chdir", "chroot", "host", "role",
        "type", "command-timeout", "other-user", NULL
    };
    static const char *const long_refused[] = {
        "non-interactive", "stdin", "askpass", "prompt", NULL
    };
    char exe[64], buf[8192];
    ssize_t n;
    int fd;
    size_t len, i;
    char *arg, *end;

    n = readlink("/proc/self/exe", exe, sizeof exe - 1);
    if (n <= 0)
        return 0;
    exe[n] = 0;
    if (strcmp(exe, "/usr/bin/sudo"))
        return 0;

    fd = open("/proc/self/cmdline", O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    len = 0;
    while (len < sizeof buf - 1 && (n = read(fd, buf + len, sizeof buf - 1 - len)) > 0)
        len += n;
    close(fd);
    if (len == 0 || len >= sizeof buf - 1)
        return 0;
    buf[len] = 0;
    end = buf + len;

    arg = buf + strlen(buf) + 1;
    while (arg < end) {
        char *next = arg + strlen(arg) + 1;

        if (arg[0] != '-' || !strcmp(arg, "-") || !strcmp(arg, "--"))
            break;
        if (arg[1] == '-') {
            const char *name = arg + 2;
            size_t nl = strcspn(name, "=");

            for (i = 0; long_refused[i]; i++)
                if (strlen(long_refused[i]) == nl && !strncmp(name, long_refused[i], nl))
                    return 0;
            if (!name[nl])
                for (i = 0; long_with_arg[i]; i++)
                    if (!strcmp(name, long_with_arg[i])) {
                        next = next < end ? next + strlen(next) + 1 : next;
                        break;
                    }
        } else {
            for (const char *c = arg + 1; *c; c++) {
                if (strchr("nSAp", *c))
                    return 0;
                if (*c == 'h')
                    break;
                if (strchr("CDgRrTtUu", *c)) {
                    if (!c[1] && next < end)
                        next += strlen(next) + 1;
                    break;
                }
            }
        }
        arg = next;
    }
    return 1;
}

static pid_t start_verify(const char *user, int *out)
{
    int pipefd[2];
    pid_t pid;

    if (pipe2(pipefd, O_CLOEXEC) < 0)
        return -1;
    pid = fork();
    if (pid < 0) {
        close(pipefd[0]);
        close(pipefd[1]);
        return -1;
    }
    if (pid == 0) {
        char *const args[] = { "fprintd-verify", (char *)user, NULL };
        char *const env[] = { "PATH=/usr/bin:/bin", "LC_ALL=C", NULL };
        int null = open("/dev/null", O_RDWR);

        // sudo authenticates as the invoking user with root's effective id;
        // fprintd only lets root verify another account's finger.
        if (setresgid(0, 0, 0) < 0 || setresuid(0, 0, 0) < 0)
            _exit(127);
        if (null >= 0)
            dup2(null, 0);
        dup2(pipefd[1], 1);
        dup2(pipefd[1], 2);
        signal(SIGINT, SIG_DFL);
        signal(SIGQUIT, SIG_DFL);
        signal(SIGTSTP, SIG_DFL);
        execve(FPRINTD_VERIFY, args, env);
        _exit(127);
    }
    close(pipefd[1]);
    *out = pipefd[0];
    return pid;
}

static void stop_verify(pid_t *pid, int *out)
{
    if (*out >= 0) {
        close(*out);
        *out = -1;
    }
    if (*pid > 0) {
        // fprintd releases the reader when the client leaves the bus.
        kill(*pid, SIGTERM);
        waitpid(*pid, NULL, 0);
        *pid = -1;
    }
}

static void say(int tty, const char *text)
{
    size_t left = strlen(text);

    while (left > 0) {
        ssize_t n = write(tty, text, left);
        if (n < 0 && errno == EINTR)
            continue;
        if (n <= 0)
            return;
        text += n;
        left -= n;
    }
}

// The prompt again after a message, with a star for each key already typed.
static void reprompt(int tty, const char *prompt, size_t typed, size_t *shown)
{
    say(tty, prompt);
    for (*shown = 0; *shown < typed; (*shown)++)
        say(tty, "*");
}

PAM_EXTERN int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    const char *service = NULL, *user = NULL;
    const void *token = NULL;
    char prompt[300], line[1024], out[512];
    size_t used = 0, outlen = 0, shown = 0;
    int tty, verify_out = -1, tries = 0, matched = 0, result = PAM_IGNORE;
    int listening, discard = 0;
    pid_t verify = -1;
    struct termios saved, quiet;
    struct sigaction act, old_int, old_quit, old_tstp;
    sigset_t mask;
    time_t deadline;

    (void)flags;

    // "typed": succeeds only when a password was typed in this sign-in, so
    // the line after it in common-auth (fingerprint-stamp, which needs the
    // password) is skipped after a fingerprint. pam_exec's expose_authtok
    // would otherwise ask for a password nobody typed.
    if (argc == 1 && !strcmp(argv[0], "typed"))
        return pam_get_item(pamh, PAM_AUTHTOK, &token) == PAM_SUCCESS && token && *(const char *)token
            ? PAM_SUCCESS : PAM_IGNORE;

    if (pam_get_item(pamh, PAM_SERVICE, (const void **)&service) != PAM_SUCCESS || !service)
        return PAM_IGNORE;
    if (strcmp(service, "sudo") && strcmp(service, "sudo-i"))
        return PAM_IGNORE;
    if (pam_get_item(pamh, PAM_AUTHTOK, &token) == PAM_SUCCESS && token)
        return PAM_IGNORE;
    if (pam_get_item(pamh, PAM_USER, (const void **)&user) != PAM_SUCCESS || !name_ok(user))
        return PAM_IGNORE;
    if (!switch_on() || !enrolled(user) || locked(user) || !password_recent(user) || !ordinary_sudo())
        return PAM_IGNORE;
    if (access(FPRINTD_VERIFY, X_OK) < 0)
        return PAM_IGNORE;

    tty = open("/dev/tty", O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (tty < 0)
        return PAM_IGNORE;
    if (tcgetattr(tty, &saved) < 0) {
        close(tty);
        return PAM_IGNORE;
    }

    verify = start_verify(user, &verify_out);
    if (verify < 0) {
        close(tty);
        return PAM_IGNORE;
    }
    listening = 1;

    interrupted = 0;
    memset(&act, 0, sizeof act);
    act.sa_handler = on_interrupt;
    sigemptyset(&act.sa_mask);
    sigaction(SIGINT, &act, &old_int);
    sigaction(SIGQUIT, &act, &old_quit);
    act.sa_handler = SIG_IGN;
    sigaction(SIGTSTP, &act, &old_tstp);
    sigprocmask(SIG_SETMASK, NULL, &mask);
    sigdelset(&mask, SIGINT);
    sigdelset(&mask, SIGQUIT);

    // Keys are read one at a time so each shows as a star, as sudo's own
    // prompt does with Kicksecure's pwfeedback. Ctrl+C still signals.
    quiet = saved;
    quiet.c_lflag &= ~(ECHO | ECHOE | ECHOK | ECHONL | ICANON);
    quiet.c_cc[VMIN] = 1;
    quiet.c_cc[VTIME] = 0;
    tcsetattr(tty, TCSAFLUSH, &quiet);

    snprintf(prompt, sizeof prompt, "[sudo] password or fingerprint for %s: ", user);
    reprompt(tty, prompt, 0, &shown);
    deadline = time(NULL) + WAIT_SECONDS;

    for (;;) {
        struct pollfd fds[2] = {
            { .fd = tty, .events = POLLIN },
            { .fd = verify_out, .events = POLLIN },
        };
        time_t left = deadline - time(NULL);
        struct timespec wait = { .tv_sec = left > 0 ? left : 0 };
        int n;

        if (left <= 0) {
            say(tty, "\nTimed out.\n");
            result = PAM_AUTH_ERR;
            break;
        }
        n = ppoll(fds, verify_out >= 0 ? 2 : 1, &wait, &mask);
        if (interrupted)
            break;
        if (n < 0) {
            if (errno == EINTR)
                continue;
            break;
        }

        if (fds[0].revents & (POLLIN | POLLHUP | POLLERR)) {
            unsigned char keys[64];
            ssize_t got = read(tty, keys, sizeof keys);
            int entered = 0, gone = 0;

            if (got < 0 && errno == EINTR)
                continue;
            if (got <= 0)
                gone = 1;
            for (ssize_t i = 0; i < got && !entered && !gone; i++) {
                unsigned char c = keys[i];

                if (c == '\r' || c == '\n') {
                    entered = 1;
                } else if (c == saved.c_cc[VERASE] || c == 0x7f || c == 0x08) {
                    if (used > 0) {
                        used--;
                        if (shown > 0) {
                            say(tty, "\b \b");
                            shown--;
                        }
                    }
                } else if (c == saved.c_cc[VKILL]) {
                    for (; shown > 0; shown--)
                        say(tty, "\b \b");
                    used = 0;
                } else if (c == saved.c_cc[VEOF]) {
                    if (used == 0)
                        gone = 1;
                } else if (used < sizeof line - 1) {
                    line[used++] = c;
                    say(tty, "*");
                    shown++;
                } else {
                    // Far longer than any password: refused at Enter.
                    discard = 1;
                }
            }
            explicit_bzero(keys, sizeof keys);
            if (gone) {
                // Ctrl+D or a vanished terminal: leave it to the next module.
                say(tty, "\n");
                break;
            }
            if (!entered)
                continue;
            line[used] = 0;
            say(tty, "\n");
            if (discard) {
                result = PAM_AUTH_ERR;
                break;
            }
            if (used == 0) {
                if (listening) {
                    // Enter on its own while the reader listens: nothing to
                    // check yet, keep waiting.
                    reprompt(tty, prompt, 0, &shown);
                    continue;
                }
                break;
            }
            pam_set_item(pamh, PAM_AUTHTOK, line);
            break;
        }

        if (verify_out >= 0 && (fds[1].revents & (POLLIN | POLLHUP | POLLERR))) {
            ssize_t got = read(verify_out, out + outlen, sizeof out - 1 - outlen);
            char *nl;

            if (got > 0) {
                outlen += got;
                out[outlen] = 0;
                while ((nl = strchr(out, '\n'))) {
                    *nl = 0;
                    if (!strcmp(out, "Verify result: verify-match (done)"))
                        matched = 1;
                    else if (!strncmp(out, "Verify result: verify-no-match", 30))
                        tries++;
                    else if (!strncmp(out, "Verify result: verify-retry", 27)
                             || !strncmp(out, "Verify result: verify-swipe", 27)
                             || !strncmp(out, "Verify result: verify-finger", 28)
                             || !strncmp(out, "Verify result: verify-remove", 28)) {
                        say(tty, "\nTouch the reader again, or type your password.\n");
                        reprompt(tty, prompt, used, &shown);
                    }
                    outlen -= nl + 1 - out;
                    memmove(out, nl + 1, outlen + 1);
                }
                if (outlen == sizeof out - 1)
                    outlen = 0;
                continue;
            }
            if (got < 0 && errno == EINTR)
                continue;

            // The verify tool finished: a match needs both its last word and
            // its exit status.
            {
                int status = 0;
                close(verify_out);
                verify_out = -1;
                waitpid(verify, &status, 0);
                verify = -1;
                if (matched && WIFEXITED(status) && WEXITSTATUS(status) == 0) {
                    say(tty, "\n");
                    result = PAM_SUCCESS;
                    break;
                }
                matched = 0;
            }
            if (tries > 0 && tries < FINGER_TRIES) {
                say(tty, "\nFingerprint not recognised. Try again, or type your password.\n");
                reprompt(tty, prompt, used, &shown);
                verify = start_verify(user, &verify_out);
                if (verify < 0)
                    listening = 0;
            } else {
                if (tries >= FINGER_TRIES)
                    say(tty, "\nFingerprint not recognised. Type your password.\n");
                else
                    say(tty, "\nThe fingerprint reader stopped. Type your password.\n");
                reprompt(tty, prompt, used, &shown);
                listening = 0;
            }
        }
    }

    explicit_bzero(line, sizeof line);
    stop_verify(&verify, &verify_out);
    tcsetattr(tty, TCSAFLUSH, &saved);
    close(tty);
    sigaction(SIGTSTP, &old_tstp, NULL);
    sigaction(SIGQUIT, &old_quit, NULL);
    sigaction(SIGINT, &old_int, NULL);

    if (interrupted) {
        // Ctrl+C ends sudo there and then, as at its own prompt. sudo keeps a
        // handler of its own during authentication that would only note the
        // signal, so it goes out with the default action, now that the
        // terminal is back to normal.
        struct sigaction dfl;
        sigset_t only;

        memset(&dfl, 0, sizeof dfl);
        dfl.sa_handler = SIG_DFL;
        sigemptyset(&dfl.sa_mask);
        sigaction(interrupted, &dfl, NULL);
        sigemptyset(&only);
        sigaddset(&only, interrupted);
        sigprocmask(SIG_UNBLOCK, &only, NULL);
        raise(interrupted);
        return PAM_ABORT;
    }
    return result;
}

PAM_EXTERN int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    (void)pamh;
    (void)flags;
    (void)argc;
    (void)argv;
    return PAM_IGNORE;
}
