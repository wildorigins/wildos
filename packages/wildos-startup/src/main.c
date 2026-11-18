/* The Wild OS start-up manager.
 *
 *   startup-manager --boot     from the start-up files, before the boot
 *                              screen and the disk unlock prompt
 *   startup-manager --scan     what this PC can start, as JSON, for the
 *                              Settings page (wildos-startup-ctl)
 *   startup-manager --boot-next N
 *                              ask the firmware to start its entry N next
 *   startup-manager --render FILE [--size WxH] [--demo NAME]
 *                              one frame as a picture, to check the drawing
 *                              against the artboards
 *
 * GRUB stays the first thing the PC runs, unseen. It starts the manager
 * (this same start of Wild OS, with wildos.manager= on the kernel line)
 * when Space is held, or at every start if that is what Settings says.
 * Choosing Wild OS simply lets start-up carry on. Choosing anything else
 * leaves a note for the next start and restarts the PC: a running Linux
 * cannot hand the PC over to Windows any other way.
 */
#include "sm.h"

#include <blkid/blkid.h>
#include <crypt.h>
#include <errno.h>
#include <fcntl.h>
#include <linux/input-event-codes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mount.h>
#include <sys/reboot.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define RUN "/run/wildos-startup"
#define BOOT RUN "/boot"
#define ROOT RUN "/root"

static struct sm_host host;
static char root_opts[64];
static struct sm_config config = { .mode = "hold", .wait = 5, .restore = true };
static bool have_recovery;
static char boot_dir[400];
static struct sm_env *env;
static struct sm_screen *screen;
static struct sm_keys *keys;
static struct sm_ui ui;

static void host_kv(const char *k, const char *v, void *u)
{
    (void)u;
#define TAKE(field) if (!strcmp(k, #field)) snprintf(host.field, sizeof host.field, "%s", v)
    TAKE(boot_uuid);
    TAKE(boot_rel);
    TAKE(esp_uuid);
    TAKE(luks_uuid);
    TAKE(root_uuid);
    TAKE(lang);
#undef TAKE
    if (!strcmp(k, "root_opts"))
        snprintf(root_opts, sizeof root_opts, "%s", v);
}

static void config_kv(const char *k, const char *v, void *u)
{
    (void)u;
    if (!strcmp(k, "mode"))
        snprintf(config.mode, sizeof config.mode, "%s", v);
    else if (!strcmp(k, "wait"))
        config.wait = atoi(v);
    else if (!strcmp(k, "restore"))
        config.restore = atoi(v) != 0;
    else if (!strcmp(k, "password"))
        config.password = atoi(v) != 0;
    else if (!strcmp(k, "lang"))
        snprintf(config.lang, sizeof config.lang, "%s", v);
    else if (!strcmp(k, "recovery"))
        have_recovery = atoi(v) != 0;
}

/* The value of wildos.manager= on the kernel line, or NULL without it. */
static const char *asked(void)
{
    static char line[4096], value[32];
    FILE *f = fopen("/proc/cmdline", "r");
    if (!f)
        return NULL;
    bool ok = fgets(line, sizeof line, f) != NULL;
    fclose(f);
    if (!ok)
        return NULL;
    for (char *word = strtok(line, " \n"); word; word = strtok(NULL, " \n"))
        if (!strncmp(word, "wildos.manager=", 15)) {
            snprintf(value, sizeof value, "%s", word + 15);
            return value;
        }
    return NULL;
}

static bool mount_uuid(const char *uuid, const char *where, unsigned long flags, const char *opts)
{
    char *dev = NULL;
    /* Disks appear as their drivers load; this runs early enough to be
     * ahead of a slow one. */
    for (int tries = 0; tries < 100 && !dev; tries++) {
        dev = blkid_evaluate_tag("UUID", uuid, NULL);
        if (!dev)
            usleep(100000);
    }
    if (!dev)
        return false;
    char *type = blkid_get_tag_value(NULL, "TYPE", dev);
    char all[128] = "";
    if (type && !strcmp(type, "btrfs"))
        snprintf(all, sizeof all, "%s", opts && *opts ? opts : "subvolid=5");
    else if (type && !strncmp(type, "ext", 3) && (flags & MS_RDONLY))
        snprintf(all, sizeof all, "noload");
    mkdir(RUN, 0700);
    mkdir(where, 0700);
    bool ok = type && !mount(dev, where, type, flags | MS_NOSUID | MS_NODEV | MS_NOEXEC, all);
    free(type);
    free(dev);
    return ok;
}

static void draw(void)
{
    cairo_t *cr = cairo_create(sm_screen_surface(screen));
    sm_ui_draw(cr, &ui);
    cairo_destroy(cr);
    sm_screen_present(screen);
}

static void message(const char *fmt, const char *name)
{
    ui.view = V_MESSAGE;
    snprintf(ui.message, sizeof ui.message, fmt, name);
    draw();
}

/* This start did not fail, it was sent elsewhere: the counts GRUB keeps for
 * start-up recovery and for trying a new kernel go back to what they were
 * before it (16_wildos_startup notes them as wsm_t0 and wsm_k0). */
static void uncount_start(void)
{
    sm_env_set(env, "wildos_tries", sm_env_get(env, "wsm_t0"));
    sm_env_set(env, "wildos_kernel_tried", sm_env_get(env, "wsm_k0"));
}

static void leave(int how)
{
    sm_env_save(env);
    sync();
    umount(BOOT);
    sync();
    reboot(how);
    /* Only reached if the kernel refused. Carry on into Wild OS. */
}

static void set_default(const struct sm_target *t)
{
    bool other = t->kind != K_WILDOS;
    sm_env_set(env, "wsm_def_uuid", other ? t->fs_uuid : "");
    sm_env_set(env, "wsm_def_path", other ? t->path : "");
    sm_env_set(env, "wsm_def_kind", other ? t->loader : "");
    /* A BitLocker Windows has to be started by the firmware itself, which
     * only a running Wild OS can ask for. */
    sm_env_set(env, "wsm_def_linux", other && t->bitlocker && t->boot_num >= 0 ? "1" : "");
}

static void start_other(const struct sm_target *t, bool permanent)
{
    if (permanent)
        set_default(t);
    if (t->boot_num < 0 || !sm_efi_set_boot_next(t->boot_num)) {
        sm_env_set(env, "wsm_once_uuid", t->fs_uuid);
        sm_env_set(env, "wsm_once_path", t->path);
        sm_env_set(env, "wsm_once_kind", t->loader);
        sm_env_set(env, "next_entry", "wildos-start-once");
    }
    uncount_start();
    if (screen)
        message(tr("Starting %s"), t->name);
    leave(RB_AUTOBOOT);
}

static bool check_disk_password(const char *typed)
{
    int pipefd[2];
    char *dev = blkid_evaluate_tag("UUID", host.luks_uuid, NULL);
    if (!dev || pipe(pipefd)) {
        free(dev);
        return false;
    }
    pid_t pid = fork();
    if (pid == 0) {
        dup2(pipefd[0], 0);
        close(pipefd[0]);
        close(pipefd[1]);
        int null = open("/dev/null", O_WRONLY);
        dup2(null, 1);
        dup2(null, 2);
        execlp("cryptsetup", "cryptsetup", "open", "--test-passphrase", "--batch-mode", "--tries",
               "1", dev, (char *)NULL);
        _exit(127);
    }
    close(pipefd[0]);
    if (write(pipefd[1], typed, strlen(typed)) < 0 || write(pipefd[1], "\n", 1) < 0)
        typed = "";
    close(pipefd[1]);
    free(dev);
    int status = 1;
    waitpid(pid, &status, 0);
    return WIFEXITED(status) && WEXITSTATUS(status) == 0;
}

/* Without disk encryption, the password of any person who signs in here. */
static bool check_account_password(const char *typed)
{
    if (!mount_uuid(host.root_uuid, ROOT, MS_RDONLY, root_opts))
        return false;
    bool ok = false;
    char row[1024];
    FILE *f = fopen(ROOT "/etc/shadow", "r");
    while (f && !ok && fgets(row, sizeof row, f)) {
        char *name = strtok(row, ":"), *hash = strtok(NULL, ":");
        if (!name || !hash || hash[0] != '$' || !strcmp(name, "root"))
            continue;
        struct crypt_data data = { 0 };
        const char *made = crypt_r(typed, hash, &data);
        ok = made && !strcmp(made, hash);
    }
    if (f)
        fclose(f);
    umount(ROOT);
    return ok;
}

/* "Ask for my password before starting another system". `disk` and `account`
 * are the question for an encrypted and an unencrypted PC. True to go on. */
static bool allowed(const char *disk, const char *account, const char *name)
{
    if (!config.password)
        return true;
    char typed[256] = "", ch[8];
    int len = 0;
    ui.view = V_PASSWORD;
    ui.typed = 0;
    ui.wrong = false;
    snprintf(ui.prompt, sizeof ui.prompt, *host.luks_uuid ? disk : account, name);
    for (;;) {
        draw();
        switch (sm_keys_read(keys, 60000, ch, sizeof ch)) {
        case SK_ESCAPE:
            ui.view = V_TILES;
            return false;
        case SK_BACKSPACE:
            while (len > 0 && (typed[--len] & 0xc0) == 0x80)
                ;
            typed[len] = 0;
            if (ui.typed > 0)
                ui.typed--;
            break;
        case SK_CHAR:
            if (len + strlen(ch) < sizeof typed) {
                len += snprintf(typed + len, sizeof typed - (size_t)len, "%s", ch);
                ui.typed++;
            }
            break;
        case SK_ENTER:
        case SK_CTRL_ENTER: {
            /* The disk's own check is slow by design, a second or more. */
            ui.checking = true;
            draw();
            bool ok = *host.luks_uuid ? check_disk_password(typed) : check_account_password(typed);
            ui.checking = false;
            memset(typed, 0, sizeof typed);
            if (ok) {
                ui.view = V_TILES;
                return true;
            }
            len = 0;
            ui.typed = 0;
            ui.wrong = true;
            break;
        }
        default:
            break;
        }
    }
}

static void load_points(void)
{
    char path[500], row[200];
    snprintf(path, sizeof path, "%s/wildos-startup/points", boot_dir);
    FILE *f = fopen(path, "r");
    while (f && ui.point_count < SM_MAX_POINTS && fgets(row, sizeof row, f)) {
        row[strcspn(row, "\n")] = 0;
        char *tab = strchr(row, '\t');
        if (!tab)
            continue;
        *tab = 0;
        struct sm_point *p = &ui.points[ui.point_count++];
        snprintf(p->id, sizeof p->id, "%s", row);
        snprintf(p->title, sizeof p->title, "%s", tab + 1);
    }
    if (f)
        fclose(f);
}

static void restore_points(void)
{
    char ch[8];
    if (ui.point_count == 0) {
        /* No points that can be started (an ext4 disk): start-up recovery
         * has the list, and goes back for good. */
        sm_env_set(env, "wildos_why", "menu");
        uncount_start();
        message("%s", tr("Opening restore points"));
        leave(RB_AUTOBOOT);
        return;
    }
    ui.view = V_POINTS;
    ui.point_selected = 0;
    for (;;) {
        draw();
        switch (sm_keys_read(keys, 60000, ch, sizeof ch)) {
        case SK_UP:
            if (ui.point_selected > 0)
                ui.point_selected--;
            break;
        case SK_DOWN:
            if (ui.point_selected < ui.point_count - 1)
                ui.point_selected++;
            break;
        case SK_ESCAPE:
            ui.view = V_TILES;
            return;
        case SK_ENTER:
        case SK_CTRL_ENTER: {
            char entry[100];
            snprintf(entry, sizeof entry, "wildos-restore-points>wildos-point-%s",
                     ui.points[ui.point_selected].id);
            sm_env_set(env, "next_entry", entry);
            uncount_start();
            message(tr("Starting %s"), ui.points[ui.point_selected].title);
            leave(RB_AUTOBOOT);
            return;
        }
        default:
            break;
        }
    }
}

static int default_index(void)
{
    const char *uuid = sm_env_get(env, "wsm_def_uuid"), *path = sm_env_get(env, "wsm_def_path");
    for (int i = 1; *uuid && i < ui.count; i++)
        if (!strcmp(ui.targets[i].fs_uuid, uuid) && !strcmp(ui.targets[i].path, path))
            return i;
    return 0;
}

static void find_targets(void)
{
    char keep[sizeof ui.targets[0].id];
    snprintf(keep, sizeof keep, "%s", ui.count ? ui.targets[ui.selected].id : "");
    ui.count = sm_scan(&host, ui.targets, SM_MAX_TARGETS - 1);
    if (config.restore && (ui.point_count > 0 || have_recovery)) {
        struct sm_target *t = &ui.targets[ui.count++];
        memset(t, 0, sizeof *t);
        t->kind = K_RESTORE;
        t->boot_num = -1;
        snprintf(t->id, sizeof t->id, "restore");
        snprintf(t->name, sizeof t->name, "%s", tr("Restore points"));
        snprintf(t->where, sizeof t->where, "%s", tr("Go back to an earlier day"));
    }
    ui.default_index = default_index();
    ui.selected = ui.default_index;
    for (int i = 0; *keep && i < ui.count; i++)
        if (!strcmp(ui.targets[i].id, keep))
            ui.selected = i;
}

/* How many disks and partitions the kernel knows: a cheap way to notice a
 * USB stick going in or out. */
static int block_count(void)
{
    int n = 0;
    FILE *f = fopen("/proc/partitions", "r");
    for (int c; f && (c = fgetc(f)) != EOF;)
        n += c == '\n';
    if (f)
        fclose(f);
    return n;
}

static double now(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + (double)t.tv_nsec / 1e9;
}

/* Returns when Wild OS is to start. Anything else restarts from inside. */
static void run(bool counting)
{
    char ch[8];
    ui.view = V_TILES;
    ui.focus = F_TILES;
    ui.countdown = -1;
    double ends = 0;
    if (counting && config.wait > 0) {
        ui.countdown_total = config.wait;
        ends = now() + config.wait;
        ui.countdown = config.wait;
    }
    int blocks = block_count();
    for (;;) {
        if (ui.countdown >= 0) {
            ui.countdown = ends - now();
            if (ui.countdown <= 0) {
                const struct sm_target *t = &ui.targets[ui.default_index];
                if (t->kind == K_WILDOS)
                    return;
                start_other(t, false);
                return;
            }
        }
        draw();
        enum sm_key key = sm_keys_read(keys, ui.countdown >= 0 ? 100 : 1500, ch, sizeof ch);
        if (key == SK_NONE) {
            int seen = block_count();
            if (ui.countdown < 0 && seen != blocks) {
                blocks = seen;
                find_targets();
            }
            continue;
        }
        ui.countdown = -1;
        const struct sm_target *t = &ui.targets[ui.selected];
        switch (key) {
        case SK_LEFT:
            if (ui.focus == F_TILES && ui.selected > 0)
                ui.selected--;
            else if (ui.focus == F_SHUTDOWN && ui.firmware)
                ui.focus = F_FIRMWARE;
            break;
        case SK_RIGHT:
            if (ui.focus == F_TILES && ui.selected < ui.count - 1)
                ui.selected++;
            else if (ui.focus == F_FIRMWARE)
                ui.focus = F_SHUTDOWN;
            break;
        case SK_DOWN:
            if (ui.focus == F_TILES)
                ui.focus = ui.firmware && ui.selected * 2 < ui.count ? F_FIRMWARE : F_SHUTDOWN;
            break;
        case SK_UP:
        case SK_ESCAPE:
            ui.focus = F_TILES;
            break;
        case SK_ENTER:
        case SK_CTRL_ENTER:
            if (ui.focus == F_SHUTDOWN) {
                uncount_start();
                message("%s", tr("Shutting down"));
                leave(RB_POWER_OFF);
            } else if (ui.focus == F_FIRMWARE) {
                if (allowed(tr("Type the disk password to open the PC firmware settings"),
                            tr("Type your password to open the PC firmware settings"), "")
                    && sm_efi_request_firmware_setup()) {
                    uncount_start();
                    message("%s", tr("Opening the PC firmware settings"));
                    leave(RB_AUTOBOOT);
                }
            } else if (t->kind == K_RESTORE) {
                restore_points();
            } else if (t->kind == K_WILDOS) {
                if (key == SK_CTRL_ENTER) {
                    set_default(t);
                    sm_env_save(env);
                }
                return;
            } else if (allowed(tr("Type the disk password to start %s"),
                               tr("Type your password to start %s"), t->name)) {
                start_other(t, key == SK_CTRL_ENTER && t->kind != K_USB);
            }
            break;
        default:
            break;
        }
    }
}

static int boot(void)
{
    const char *how = asked();
    /* The usual start: GRUB did not ask for the manager. Space may still be
     * held where the firmware never told GRUB about it. */
    if (!how && !sm_space_held_at_start())
        return 0;

    sm_read_kv("/etc/wildos-startup.conf", host_kv, NULL);
    if (!*host.boot_uuid || !mount_uuid(host.boot_uuid, BOOT, 0, NULL))
        return 0;
    snprintf(boot_dir, sizeof boot_dir, BOOT "%s", host.boot_rel);
    char path[500];
    snprintf(path, sizeof path, "%s/wildos-startup/config", boot_dir);
    sm_read_kv(path, config_kv, NULL);
    snprintf(path, sizeof path, "%s/grub/grubenv", boot_dir);
    env = sm_env_load(path);

    if (!strcmp(config.mode, "off") && (!how || strcmp(how, "default"))) {
        umount(BOOT);
        return 0;
    }
    sm_tr_load("/usr/share/wildos-startup", *config.lang ? config.lang : host.lang);
    load_points();
    find_targets();

    /* Sent here only to start the default, a BitLocker Windows, the way
     * the firmware would. If it has gone, show what there is instead. */
    if (how && !strcmp(how, "default") && !sm_key_held(KEY_SPACE)) {
        if (ui.default_index > 0)
            start_other(&ui.targets[ui.default_index], false);
        sm_env_set(env, "wsm_def_uuid", "");
        sm_env_save(env);
    }

    screen = sm_screen_open();
    if (!screen) {
        umount(BOOT);
        return 0;
    }
    cairo_surface_t *s = sm_screen_surface(screen);
    ui.w = cairo_image_surface_get_width(s);
    ui.h = cairo_image_surface_get_height(s);
    ui.firmware = sm_efi_firmware_setup_supported();
    ui.hold_hint = strcmp(config.mode, "off") != 0;
    keys = sm_keys_open();

    run(how && !strcmp(how, "always"));

    /* Wild OS it is. The screen goes dark for the boot screen to draw on. */
    cairo_t *cr = cairo_create(s);
    cairo_set_source_rgb(cr, 0, 0, 0);
    cairo_paint(cr);
    cairo_destroy(cr);
    sm_screen_present(screen);
    sm_keys_close(keys);
    sm_screen_close(screen, false);
    sm_env_free(env);
    umount(BOOT);
    return 0;
}

static int render(int argc, char **argv)
{
    const char *file = NULL, *demo = "choose";
    int w = 1440, h = 900;
    for (int i = 2; i < argc; i++) {
        if (!strcmp(argv[i], "--size") && i + 1 < argc)
            sscanf(argv[++i], "%dx%d", &w, &h);
        else if (!strcmp(argv[i], "--demo") && i + 1 < argc)
            demo = argv[++i];
        else if (!strcmp(argv[i], "--assets") && i + 1 < argc)
            sm_ui_assets(argv[++i]);
        else if (!strcmp(argv[i], "--lang") && i + 2 < argc) {
            sm_tr_load(argv[i + 1], argv[i + 2]);
            i += 2;
        }
        else
            file = argv[i];
    }
    if (!file)
        return 2;
    static const struct { enum sm_kind kind; const char *name, *where; } demo_targets[] = {
        { K_WILDOS, "Wild OS", "On the main disk" },
        { K_WINDOWS, "Windows 11", "On the second disk" },
        { K_USB, "USB stick", "Wild OS installer" },
        { K_RESTORE, "Restore points", "Go back to an earlier day" },
    };
    bool countdown = !strcmp(demo, "countdown");
    for (unsigned i = 0; i < 4; i++) {
        if (countdown && demo_targets[i].kind == K_USB)
            continue;
        struct sm_target *t = &ui.targets[ui.count++];
        t->kind = demo_targets[i].kind;
        snprintf(t->name, sizeof t->name, "%s", tr(demo_targets[i].name));
        snprintf(t->where, sizeof t->where, "%s", tr(demo_targets[i].where));
    }
    ui.w = w;
    ui.h = h;
    ui.firmware = true;
    ui.hold_hint = true;
    ui.countdown = -1;
    ui.selected = 1;
    if (countdown) {
        ui.selected = 0;
        ui.countdown = 3.8;
        ui.countdown_total = 10;
    } else if (!strcmp(demo, "points")) {
        ui.view = V_POINTS;
        static const char *const titles[] = { "Before the update (18 Nov 09:12)",
                                              "Before the graphics driver (16 Nov 20:40)",
                                              "Before the update (14 Nov 08:03)" };
        for (int i = 0; i < 3; i++)
            snprintf(ui.points[ui.point_count++].title, sizeof ui.points[0].title, "%s", titles[i]);
    } else if (!strcmp(demo, "password")) {
        ui.view = V_PASSWORD;
        snprintf(ui.prompt, sizeof ui.prompt, tr("Type the disk password to start %s"), "Windows 11");
        ui.typed = 9;
    } else if (!strcmp(demo, "corner")) {
        ui.focus = F_FIRMWARE;
    }
    cairo_surface_t *s = cairo_image_surface_create(CAIRO_FORMAT_RGB24, w, h);
    cairo_t *cr = cairo_create(s);
    sm_ui_draw(cr, &ui);
    cairo_destroy(cr);
    cairo_status_t st = cairo_surface_write_to_png(s, file);
    cairo_surface_destroy(s);
    return st == CAIRO_STATUS_SUCCESS ? 0 : 1;
}

int main(int argc, char **argv)
{
    if (argc >= 2 && !strcmp(argv[1], "--boot"))
        return boot();
    if (argc >= 2 && !strcmp(argv[1], "--render"))
        return render(argc, argv);
    if (argc >= 2 && !strcmp(argv[1], "--scan")) {
        for (int i = 2; i + 1 < argc; i += 2) {
            if (!strcmp(argv[i], "--boot-uuid"))
                snprintf(host.boot_uuid, sizeof host.boot_uuid, "%s", argv[i + 1]);
            else if (!strcmp(argv[i], "--esp-uuid"))
                snprintf(host.esp_uuid, sizeof host.esp_uuid, "%s", argv[i + 1]);
        }
        static struct sm_target found[SM_MAX_TARGETS];
        sm_scan_json(found, sm_scan(&host, found, SM_MAX_TARGETS));
        return 0;
    }
    /* For the Settings helper: have the firmware start its entry N next. */
    if (argc >= 3 && !strcmp(argv[1], "--boot-next"))
        return sm_efi_set_boot_next(atoi(argv[2])) ? 0 : 1;
    fprintf(stderr, "usage: startup-manager --boot | --scan | --boot-next N | --render FILE\n");
    return 2;
}
