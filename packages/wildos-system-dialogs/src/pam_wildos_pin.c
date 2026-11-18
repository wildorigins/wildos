// pam_wildos_pin: the lock screen and the sign-in screen take the PIN from
// Settings > Accounts > Sign-in options in the same box as the password.
//
// /etc/pam.d/kde (the lock screen) and common-auth (the wildos-pin PAM
// profile, for the sign-in screen) run this before pam_unix. Whatever was
// typed is fetched once here and left as PAM_AUTHTOK, so pam_unix checks
// the same text as a password when it is not the PIN. Only text that could
// be a PIN (4 to 12 digits) is ever sent to wildos-pin, the root service
// that holds the hash, so a password is never handed to it and never
// counts as a wrong PIN unless it is all digits itself.
//
// PAM_SUCCESS only for the right PIN. Everything else is PAM_IGNORE or an
// error the PAM file ignores, and the stack carries on to the password as
// if this module were not there: any service but those two (sudo,
// administrator prompts and the console never take a PIN), no PIN set, the
// service not answering, or too many wrong PINs.
//
// On the sign-in screen two more things send the PIN to the password
// instead: a locked password, which locks the account, and Advanced
// protection's "Fingerprint needs your password sometimes", which wants
// the password itself after a restart.
//
// After the right PIN at sign-in the typed text is emptied, so pam_kwallet
// does not make a wallet key from the PIN. wildos-pin notes the sign-in and
// the session gets the kept key instead (wildos-setup-wallet).

#define _GNU_SOURCE
#include <errno.h>
#include <pwd.h>
#include <shadow.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/un.h>
#include <unistd.h>

#include <security/pam_ext.h>
#include <security/pam_modules.h>

#define SOCKET_PATH "/run/wildos/pin.socket"
#define STORE "/var/lib/wildos/pin"
#define ADVANCED "/etc/wildos/advanced-protection"
#define MIN_DIGITS 4
#define MAX_DIGITS 12

static int name_ok(const char *user)
{
    if (!user || !*user || !strcmp(user, ".") || !strcmp(user, "..") || strlen(user) > 64)
        return 0;
    return strchr(user, '/') == NULL;
}

static int write_all(int fd, const char *buf, size_t len)
{
    while (len > 0) {
        ssize_t put = send(fd, buf, len, MSG_NOSIGNAL);
        if (put < 0 && errno == EINTR)
            continue;
        if (put <= 0)
            return -1;
        buf += put;
        len -= (size_t)put;
    }
    return 0;
}

// Advanced protection's fingerprint_hours, 0 when off or unset.
static long password_hours(void)
{
    char line[128];
    long hours = 0;
    FILE *f = fopen(ADVANCED, "re");

    if (!f)
        return 0;
    while (fgets(line, sizeof line, f)) {
        if (!strncmp(line, "fingerprint_hours=", 18))
            hours = strtol(line + 18, NULL, 10);
    }
    fclose(f);
    return hours > 0 ? hours : 0;
}

// The service's answer, at most 31 characters. The lock screen asks as the
// person ("check"); the sign-in screen asks as root and names the account
// ("signin").
static int ask(const char *account, const char *pin, char *reply, size_t size)
{
    struct sockaddr_un addr = {.sun_family = AF_UNIX};
    // yescrypt takes well under a second and a wrong PIN waits one more.
    struct timeval limit = {.tv_sec = 10};
    size_t len = 0;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    int sent;

    if (fd < 0)
        return -1;
    strncpy(addr.sun_path, SOCKET_PATH, sizeof addr.sun_path - 1);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &limit, sizeof limit);
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &limit, sizeof limit);
    if (connect(fd, (struct sockaddr *)&addr, sizeof addr) < 0) {
        close(fd);
        return -1;
    }
    if (account)
        sent = write_all(fd, "signin\n", 7) == 0 && write_all(fd, account, strlen(account)) == 0 && write_all(fd, "\n", 1) == 0;
    else
        sent = write_all(fd, "check\n", 6) == 0;
    if (!sent || write_all(fd, pin, strlen(pin)) < 0) {
        close(fd);
        return -1;
    }
    shutdown(fd, SHUT_WR);
    while (len < size - 1) {
        ssize_t got = recv(fd, reply + len, size - 1 - len, 0);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        len += (size_t)got;
    }
    close(fd);
    reply[len] = '\0';
    return len > 0 ? 0 : -1;
}

int pam_sm_authenticate(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    const char *service = NULL;
    const char *user = NULL;
    const char *typed = NULL;
    const struct passwd *pw;
    char path[256];
    char reply[32];
    struct stat st;
    size_t len;
    int signin;

    (void)flags;
    (void)argc;
    (void)argv;

    if (pam_get_item(pamh, PAM_SERVICE, (const void **)&service) != PAM_SUCCESS || !service)
        return PAM_IGNORE;
    signin = !strcmp(service, "sddm");
    if (!signin && strcmp(service, "kde"))
        return PAM_IGNORE;
    if (pam_get_user(pamh, &user, NULL) != PAM_SUCCESS || !name_ok(user))
        return PAM_IGNORE;
    if (signin) {
        const struct spwd *sp;

        // The sign-in screen's helper checks as root, for the account
        // picked there.
        if (getuid() != 0 || password_hours() > 0)
            return PAM_IGNORE;
        pw = getpwnam(user);
        if (!pw || pw->pw_uid < 1000)
            return PAM_IGNORE;
        sp = getspnam(user);
        if (!sp || !sp->sp_pwdp || sp->sp_pwdp[0] == '!' || sp->sp_pwdp[0] == '*')
            return PAM_IGNORE;
    } else {
        // The lock screen checks the person it runs as; the service only
        // answers for that account anyway.
        pw = getpwuid(getuid());
        if (!pw || strcmp(pw->pw_name, user))
            return PAM_IGNORE;
    }
    snprintf(path, sizeof path, "%s/%s", STORE, user);
    if (stat(path, &st) < 0)
        return PAM_IGNORE;

    if (pam_get_authtok(pamh, PAM_AUTHTOK, &typed, NULL) != PAM_SUCCESS || !typed)
        return PAM_IGNORE;
    len = strlen(typed);
    if (len < MIN_DIGITS || len > MAX_DIGITS || strspn(typed, "0123456789") != len)
        return PAM_IGNORE;

    if (ask(signin ? user : NULL, typed, reply, sizeof reply) < 0)
        return PAM_IGNORE;
    if (!strcmp(reply, "ok")) {
        if (signin)
            pam_set_item(pamh, PAM_AUTHTOK, "");
        return PAM_SUCCESS;
    }
    if (!strncmp(reply, "wait ", 5)) {
        long seconds = 0;
        sscanf(reply + 5, "%ld", &seconds);
        if (seconds > 90)
            pam_error(pamh, "Too many wrong PINs. Use your password, or try your PIN again in %ld minutes.", (seconds + 59) / 60);
        else
            pam_error(pamh, "Too many wrong PINs. Use your password, or try your PIN again in a minute.");
    }
    return PAM_AUTH_ERR;
}

int pam_sm_setcred(pam_handle_t *pamh, int flags, int argc, const char **argv)
{
    (void)pamh;
    (void)flags;
    (void)argc;
    (void)argv;
    return PAM_IGNORE;
}
