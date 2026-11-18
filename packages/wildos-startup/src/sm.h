/* The Wild OS start-up manager: shared declarations. */
#ifndef WILDOS_SM_H
#define WILDOS_SM_H

#include <cairo.h>
#include <stdbool.h>
#include <stdint.h>

#define SM_MAX_TARGETS 12
#define SM_MAX_POINTS 24

enum sm_kind { K_WILDOS, K_WINDOWS, K_LINUX, K_USB, K_RESTORE };

/* Something this PC can start. */
struct sm_target {
    enum sm_kind kind;
    char name[64];
    char where[96];
    /* "wildos", "restore", or the file system and loader: "efi:UUID:/path". */
    char id[200];
    char fs_uuid[48];
    char part_uuid[48];
    char path[128];
    char loader[8]; /* efi or ntldr */
    char disk[32];
    int disk_rank; /* 0 is the disk Wild OS starts from */
    uint64_t disk_bytes;
    int boot_num; /* the firmware's own entry for it, or -1 */
    bool bitlocker;
    bool removable;
};

struct sm_point {
    char id[32];
    char title[96];
};

/* What the installed system left for the manager on /boot. */
struct sm_config {
    char mode[8]; /* hold, always or off */
    int wait;
    bool restore;
    bool password;
    char lang[16];
};

/* Baked into the start-up files when they are built (module-setup.sh). */
struct sm_host {
    char boot_uuid[48];
    char boot_rel[128];
    char esp_uuid[48];
    char luks_uuid[48];
    char root_uuid[48];
    char lang[16];
};

/* scan.c */
int sm_scan(const struct sm_host *host, struct sm_target *out, int max);
void sm_scan_json(const struct sm_target *t, int n);
bool sm_is_efi(void);

/* efi.c */
int sm_efi_find_entry(const char *part_uuid, const char *path);
bool sm_efi_set_boot_next(int num);
bool sm_efi_firmware_setup_supported(void);
bool sm_efi_request_firmware_setup(void);

/* env.c */
struct sm_env;
struct sm_env *sm_env_load(const char *path);
const char *sm_env_get(const struct sm_env *e, const char *key);
void sm_env_set(struct sm_env *e, const char *key, const char *value);
bool sm_env_save(const struct sm_env *e);
void sm_env_free(struct sm_env *e);
bool sm_read_kv(const char *path, void (*each)(const char *k, const char *v, void *u), void *u);

/* text.c */
void sm_tr_load(const char *dir, const char *lang);
const char *tr(const char *english);

/* ui.c */
enum sm_view { V_TILES, V_PASSWORD, V_POINTS, V_MESSAGE };
enum sm_focus { F_TILES, F_FIRMWARE, F_SHUTDOWN };

struct sm_ui {
    int w, h;
    struct sm_target targets[SM_MAX_TARGETS];
    int count;
    int selected;
    int default_index;
    enum sm_view view;
    enum sm_focus focus;
    bool firmware;
    bool hold_hint;
    /* Seconds left, or a negative number when nothing counts down. */
    double countdown;
    double countdown_total;
    struct sm_point points[SM_MAX_POINTS];
    int point_count;
    int point_selected;
    char prompt[160];
    int typed;
    bool wrong;
    bool checking;
    char message[200];
};

void sm_ui_draw(cairo_t *cr, const struct sm_ui *ui);
void sm_ui_assets(const char *dir);

/* screen.c */
struct sm_screen;
struct sm_screen *sm_screen_open(void);
cairo_surface_t *sm_screen_surface(struct sm_screen *s);
void sm_screen_present(struct sm_screen *s);
void sm_screen_close(struct sm_screen *s, bool text);

/* keys.c */
enum sm_key {
    SK_NONE, SK_LEFT, SK_RIGHT, SK_UP, SK_DOWN, SK_ENTER, SK_CTRL_ENTER,
    SK_ESCAPE, SK_BACKSPACE, SK_CHAR, SK_OTHER
};
struct sm_keys;
struct sm_keys *sm_keys_open(void);
enum sm_key sm_keys_read(struct sm_keys *k, int timeout_ms, char *ch, int ch_size);
void sm_keys_close(struct sm_keys *k);
bool sm_key_held(int code);
bool sm_space_held_at_start(void);

#endif
