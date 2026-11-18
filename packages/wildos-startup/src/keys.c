/* The keyboard, two ways.
 *
 * Typed keys come from the console, which already knows the keyboard
 * layout the PC was set up with, so a password is typed the same as at the
 * disk unlock screen. The console cannot tell Ctrl+Enter from Enter or say
 * whether a key is being held, so those two are asked of the keyboards
 * themselves.
 */
#include "sm.h"

#include <dirent.h>
#include <fcntl.h>
#include <linux/hidraw.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

struct sm_keys {
    int fd;
    struct termios saved;
    bool have_saved;
};

/* What the kernel has seen. A laptop's own keyboard repeats a held key, so
 * the kernel knows it is down however early it was pressed. Sets *keyboards
 * to how many devices with a Space key there are. */
static bool held_seen(int code, int *keyboards)
{
    DIR *d = opendir("/dev/input");
    if (!d)
        return false;
    bool held = false;
    for (struct dirent *e; !held && (e = readdir(d));) {
        if (strncmp(e->d_name, "event", 5))
            continue;
        char path[300];
        unsigned char bits[KEY_MAX / 8 + 1] = { 0 }, has[KEY_MAX / 8 + 1] = { 0 };
        snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
        int fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof has), has) >= 0 && (has[KEY_SPACE / 8] & (1 << (KEY_SPACE % 8))))
            (*keyboards)++;
        if (ioctl(fd, EVIOCGKEY(sizeof bits), bits) >= 0)
            held = bits[code / 8] & (1 << (code % 8));
        close(fd);
    }
    closedir(d);
    return held;
}

/* What a USB keyboard says when asked. It only reports changes, so a key
 * that went down before the kernel was listening is never mentioned; asked
 * outright for its state, it lists the keys held now. `usage` is the key's
 * number in the USB keyboard table. */
static bool held_asked(int usage)
{
    DIR *d = opendir("/sys/class/hidraw");
    if (!d)
        return false;
    bool held = false;
    for (struct dirent *e; !held && (e = readdir(d));) {
        if (strncmp(e->d_name, "hidraw", 6))
            continue;
        /* Only keyboards: a mouse's report can hold the same byte. */
        char path[400], word[64] = "";
        snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/input", e->d_name);
        DIR *inputs = opendir(path);
        bool keyboard = false;
        for (struct dirent *i; inputs && (i = readdir(inputs));) {
            if (strncmp(i->d_name, "input", 5))
                continue;
            snprintf(path, sizeof path, "/sys/class/hidraw/%s/device/input/%s/capabilities/key",
                     e->d_name, i->d_name);
            FILE *f = fopen(path, "r");
            char caps[1024] = "";
            if (f && fgets(caps, sizeof caps, f)) {
                /* The last word holds keys 0 to 63; Space is key 57. */
                char *last = strrchr(caps, ' ');
                snprintf(word, sizeof word, "%s", last ? last + 1 : caps);
                keyboard = keyboard || ((strtoull(word, NULL, 16) >> KEY_SPACE) & 1);
            }
            if (f)
                fclose(f);
        }
        if (inputs)
            closedir(inputs);
        if (!keyboard)
            continue;
        snprintf(path, sizeof path, "/dev/%s", e->d_name);
        int fd = open(path, O_RDWR | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0)
            continue;
        /* Report 0 is a plain keyboard's only one; some number theirs. */
        for (int id = 0; id < 3 && !held; id++) {
            unsigned char report[64] = { (unsigned char)id };
            int got = ioctl(fd, HIDIOCGINPUT(sizeof report), report);
            for (int i = 2; i < got && i < 10; i++)
                held = held || report[i] == usage;
        }
        close(fd);
    }
    closedir(d);
    return held;
}

bool sm_key_held(int code)
{
    int keyboards = 0;
    /* 0x2c and 0xe0/0xe4: Space and the two Ctrl keys in the USB table. */
    int usage = code == KEY_SPACE ? 0x2c : code == KEY_LEFTCTRL ? 0xe0 : code == KEY_RIGHTCTRL ? 0xe4 : 0;
    return held_seen(code, &keyboards) || (code == KEY_SPACE && held_asked(usage));
}

/* For the look as Wild OS starts. A USB keyboard can take a few tenths of a
 * second longer to appear than this runs, so when there is no keyboard at
 * all yet, wait that long for one. A PC whose keyboard is already there
 * (every laptop) is not held up. */
bool sm_space_held_at_start(void)
{
    for (int tries = 0; tries < 8; tries++) {
        int keyboards = 0;
        if (held_seen(KEY_SPACE, &keyboards) || held_asked(0x2c))
            return true;
        if (keyboards > 0)
            return false;
        usleep(50000);
    }
    return false;
}

struct sm_keys *sm_keys_open(void)
{
    struct sm_keys *k = calloc(1, sizeof *k);
    k->fd = open("/dev/tty0", O_RDWR | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    if (k->fd < 0)
        k->fd = open("/dev/console", O_RDWR | O_NONBLOCK | O_NOCTTY | O_CLOEXEC);
    struct termios raw;
    if (k->fd >= 0 && !tcgetattr(k->fd, &k->saved)) {
        k->have_saved = true;
        raw = k->saved;
        cfmakeraw(&raw);
        tcsetattr(k->fd, TCSANOW, &raw);
        tcflush(k->fd, TCIFLUSH);
    }
    return k;
}

static int byte(struct sm_keys *k, int timeout_ms)
{
    struct pollfd p = { .fd = k->fd, .events = POLLIN };
    unsigned char c;
    if (k->fd < 0) {
        if (timeout_ms > 0)
            usleep((useconds_t)timeout_ms * 1000);
        return -1;
    }
    if (poll(&p, 1, timeout_ms) <= 0 || read(k->fd, &c, 1) != 1)
        return -1;
    return c;
}

enum sm_key sm_keys_read(struct sm_keys *k, int timeout_ms, char *ch, int ch_size)
{
    int c = byte(k, timeout_ms);
    if (c < 0)
        return SK_NONE;
    if (c == 0x1b) {
        /* An arrow key arrives as Esc [ letter in one burst; Esc by itself
         * is followed by nothing. */
        int next = byte(k, 40);
        if (next < 0)
            return SK_ESCAPE;
        if (next != '[' && next != 'O')
            return SK_OTHER;
        int last = byte(k, 40);
        while (last >= '0' && last <= ';')
            last = byte(k, 40);
        switch (last) {
        case 'A': return SK_UP;
        case 'B': return SK_DOWN;
        case 'C': return SK_RIGHT;
        case 'D': return SK_LEFT;
        default: return SK_OTHER;
        }
    }
    if (c == '\r' || c == '\n')
        return sm_key_held(KEY_LEFTCTRL) || sm_key_held(KEY_RIGHTCTRL) ? SK_CTRL_ENTER : SK_ENTER;
    if (c == 0x7f || c == 0x08)
        return SK_BACKSPACE;
    if (c < 0x20)
        return SK_OTHER;
    /* One character, which in UTF-8 can be up to four bytes. */
    int n = 0, more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : c >= 0xc0 ? 1 : 0;
    ch[n++] = (char)c;
    while (more-- > 0 && n < ch_size - 1) {
        int b = byte(k, 40);
        if (b < 0)
            break;
        ch[n++] = (char)b;
    }
    ch[n] = 0;
    return SK_CHAR;
}

void sm_keys_close(struct sm_keys *k)
{
    if (k->fd >= 0) {
        if (k->have_saved)
            tcsetattr(k->fd, TCSANOW, &k->saved);
        close(k->fd);
    }
    free(k);
}
