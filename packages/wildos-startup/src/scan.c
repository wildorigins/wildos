/* Finds what this PC can start besides this copy of Wild OS.
 *
 * Every FAT partition on every disk is opened and looked through for a boot
 * loader, each time the manager or the Settings page asks. Nothing is kept
 * from the day Wild OS was installed, so Windows on a second disk, a system
 * added later or a USB stick plugged in a minute ago are all found the same
 * way, whether or not the PC's firmware still lists them.
 */
#include "sm.h"

#include <blkid/blkid.h>
#include <ctype.h>
#include <dirent.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/mount.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <unistd.h>

#define MNT "/run/wildos-startup/scan"
#define MAX_PARTS 64

struct part {
    char name[32], disk[32], type[24], uuid[48], part_uuid[48], label[64];
    bool removable;
    uint64_t disk_bytes;
};

bool sm_is_efi(void)
{
    return access("/sys/firmware/efi", F_OK) == 0;
}

static bool read_line(const char *path, char *out, size_t size)
{
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    bool ok = fgets(out, (int)size, f) != NULL;
    fclose(f);
    out[strcspn(out, "\n")] = 0;
    return ok;
}

static void probe(const char *dev, struct part *p)
{
    blkid_probe pr = blkid_new_probe_from_filename(dev);
    if (!pr)
        return;
    blkid_probe_enable_partitions(pr, 1);
    blkid_probe_set_partitions_flags(pr, BLKID_PARTS_ENTRY_DETAILS);
    blkid_probe_enable_superblocks(pr, 1);
    blkid_probe_set_superblocks_flags(pr, BLKID_SUBLKS_UUID | BLKID_SUBLKS_LABEL | BLKID_SUBLKS_TYPE);
    if (blkid_do_safeprobe(pr) == 0) {
        const char *v;
        if (!blkid_probe_lookup_value(pr, "TYPE", &v, NULL))
            snprintf(p->type, sizeof p->type, "%s", v);
        if (!blkid_probe_lookup_value(pr, "UUID", &v, NULL))
            snprintf(p->uuid, sizeof p->uuid, "%s", v);
        if (!blkid_probe_lookup_value(pr, "LABEL", &v, NULL))
            snprintf(p->label, sizeof p->label, "%s", v);
        if (!blkid_probe_lookup_value(pr, "PART_ENTRY_UUID", &v, NULL))
            snprintf(p->part_uuid, sizeof p->part_uuid, "%s", v);
    }
    blkid_free_probe(pr);
}

static int list_parts(struct part *parts)
{
    int n = 0;
    DIR *d = opendir("/sys/class/block");
    if (!d)
        return 0;
    for (struct dirent *e; (e = readdir(d)) && n < MAX_PARTS;) {
        char path[PATH_MAX], real[PATH_MAX], value[64];
        snprintf(path, sizeof path, "/sys/class/block/%s/partition", e->d_name);
        if (access(path, F_OK))
            continue;
        snprintf(path, sizeof path, "/sys/class/block/%s", e->d_name);
        if (!realpath(path, real))
            continue;
        struct part *p = &parts[n];
        memset(p, 0, sizeof *p);
        snprintf(p->name, sizeof p->name, "%s", e->d_name);
        char *slash = strrchr(real, '/');
        *slash = 0;
        snprintf(p->disk, sizeof p->disk, "%s", strrchr(real, '/') + 1);
        snprintf(path, sizeof path, "/sys/class/block/%s/removable", p->disk);
        p->removable = (read_line(path, value, sizeof value) && value[0] == '1')
                       || strstr(real, "/usb") != NULL;
        snprintf(path, sizeof path, "/sys/class/block/%s/size", p->disk);
        if (read_line(path, value, sizeof value))
            p->disk_bytes = strtoull(value, NULL, 10) * 512;
        snprintf(path, sizeof path, "/dev/%s", p->name);
        probe(path, p);
        n++;
    }
    closedir(d);
    return n;
}

/* Where the partition is mounted already, if it is. Mounting it a second
 * time read-only is refused while it is in use read-write. */
static bool mounted_at(const char *name, char *out, size_t size)
{
    char dev[64], row[1024];
    struct stat st;
    snprintf(dev, sizeof dev, "/dev/%s", name);
    if (stat(dev, &st))
        return false;
    FILE *f = fopen("/proc/self/mountinfo", "r");
    if (!f)
        return false;
    bool found = false;
    while (!found && fgets(row, sizeof row, f)) {
        unsigned maj, min;
        char root[256], point[512];
        if (sscanf(row, "%*d %*d %u:%u %255s %511s", &maj, &min, root, point) == 4
            && maj == major(st.st_rdev) && min == minor(st.st_rdev) && !strcmp(root, "/")) {
            snprintf(out, size, "%s", point);
            found = true;
        }
    }
    fclose(f);
    return found;
}

static bool exists(const char *root, const char *rel)
{
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s%s", root, rel);
    return access(path, F_OK) == 0;
}

/* Windows 10 or 11, from the version stamped into its boot manager. That
 * file sits outside the Windows disk itself, so this works with BitLocker
 * on too. 11 starts at build 22000. */
static void windows_name(const char *root, char *out, size_t size)
{
    snprintf(out, size, "Windows");
    char path[PATH_MAX];
    snprintf(path, sizeof path, "%s/EFI/Microsoft/Boot/bootmgfw.efi", root);
    FILE *f = fopen(path, "rb");
    if (!f)
        return;
    static unsigned char buf[8 << 20];
    size_t got = fread(buf, 1, sizeof buf, f);
    fclose(f);
    for (size_t i = 0; i + 16 <= got; i += 4) {
        if (buf[i] != 0xBD || buf[i + 1] != 0x04 || buf[i + 2] != 0xEF || buf[i + 3] != 0xFE)
            continue;
        unsigned major_v = buf[i + 10] | buf[i + 11] << 8;
        unsigned build = buf[i + 14] | buf[i + 15] << 8;
        if (major_v == 10)
            snprintf(out, size, build >= 22000 ? "Windows 11" : "Windows 10");
        return;
    }
}

static const char *linux_name(const char *dir)
{
    static const struct { const char *dir, *name; } known[] = {
        { "ubuntu", "Ubuntu" }, { "debian", "Debian" }, { "fedora", "Fedora" },
        { "opensuse", "openSUSE" }, { "arch", "Arch Linux" }, { "manjaro", "Manjaro" },
        { "pop", "Pop!_OS" }, { "zorin", "Zorin OS" }, { "cachyos", "CachyOS" },
        { "endeavouros", "EndeavourOS" }, { "nixos", "NixOS" }, { "systemd", "Linux" },
        { "kali", "Kali Linux" }, { "elementary", "elementary OS" }, { "wildos", "Wild OS" },
    };
    for (unsigned i = 0; i < sizeof known / sizeof *known; i++)
        if (!strncasecmp(dir, known[i].dir, strlen(known[i].dir)))
            return known[i].name;
    return NULL;
}

static struct sm_target *add(struct sm_target *out, int *n, int max, enum sm_kind kind,
                             const struct part *p, const char *path, const char *loader)
{
    if (*n >= max)
        return NULL;
    struct sm_target *t = &out[(*n)++];
    memset(t, 0, sizeof *t);
    t->kind = kind;
    snprintf(t->fs_uuid, sizeof t->fs_uuid, "%s", p->uuid);
    snprintf(t->part_uuid, sizeof t->part_uuid, "%s", p->part_uuid);
    snprintf(t->path, sizeof t->path, "%s", path);
    snprintf(t->loader, sizeof t->loader, "%s", loader);
    snprintf(t->disk, sizeof t->disk, "%s", p->disk);
    snprintf(t->id, sizeof t->id, "%s:%s:%s", loader, p->uuid, path);
    t->disk_bytes = p->disk_bytes;
    t->removable = p->removable;
    t->boot_num = -1;
    return t;
}

static void look_inside(const struct sm_host *host, const struct part *p, const char *root,
                        struct sm_target *out, int *n, int max)
{
    bool ours = !strcasecmp(p->uuid, host->esp_uuid);
    bool named = false;
    struct sm_target *t;

    if (exists(root, "/EFI/Microsoft/Boot/bootmgfw.efi")
        && (t = add(out, n, max, K_WINDOWS, p, "/EFI/Microsoft/Boot/bootmgfw.efi", "efi"))) {
        windows_name(root, t->name, sizeof t->name);
        named = true;
    }

    char efi[PATH_MAX];
    snprintf(efi, sizeof efi, "%s/EFI", root);
    DIR *d = opendir(efi);
    for (struct dirent *e; d && (e = readdir(d));) {
        if (e->d_name[0] == '.' || !strcasecmp(e->d_name, "Microsoft")
            || !strcasecmp(e->d_name, "BOOT") || (ours && !strcasecmp(e->d_name, "wildos")))
            continue;
        static const char *const loaders[] = { "shimx64.efi", "grubx64.efi", "systemd-bootx64.efi" };
        for (unsigned i = 0; i < 3; i++) {
            char rel[300];
            snprintf(rel, sizeof rel, "/EFI/%s/%s", e->d_name, loaders[i]);
            if (!exists(root, rel))
                continue;
            if ((t = add(out, n, max, K_LINUX, p, rel, "efi"))) {
                const char *name = linux_name(e->d_name);
                snprintf(t->name, sizeof t->name, "%s", name ? name : e->d_name);
                if (!name)
                    t->name[0] = (char)toupper((unsigned char)t->name[0]);
            }
            named = true;
            break;
        }
    }
    if (d)
        closedir(d);

    /* The fallback loader stands for a system only where nothing above
     * named one: a USB stick, or a disk whose system registers no folder
     * of its own. On Wild OS's own partition it is Wild OS again. */
    if (!named && !ours && exists(root, "/EFI/BOOT/BOOTX64.EFI")
        && (t = add(out, n, max, p->removable ? K_USB : K_LINUX, p, "/EFI/BOOT/BOOTX64.EFI", "efi")))
        snprintf(t->name, sizeof t->name, "%s", p->removable ? tr("USB stick") : tr("Another system"));
}

static const char *ordinal(int rank)
{
    switch (rank) {
    case 0: return tr("On the main disk");
    case 1: return tr("On the second disk");
    case 2: return tr("On the third disk");
    default: return tr("On another disk");
    }
}

int sm_scan(const struct sm_host *host, struct sm_target *out, int max)
{
    static struct part parts[MAX_PARTS];
    int count = list_parts(parts), n = 0;
    bool efi = sm_is_efi();

    char main_disk[32] = "";
    for (int i = 0; i < count; i++)
        if (*host->boot_uuid && !strcasecmp(parts[i].uuid, host->boot_uuid))
            snprintf(main_disk, sizeof main_disk, "%s", parts[i].disk);

    memset(&out[n], 0, sizeof out[n]);
    out[n].kind = K_WILDOS;
    out[n].boot_num = -1;
    snprintf(out[n].name, sizeof out[n].name, "Wild OS");
    snprintf(out[n].id, sizeof out[n].id, "wildos");
    snprintf(out[n].disk, sizeof out[n].disk, "%s", main_disk);
    snprintf(out[n].where, sizeof out[n].where, "%s", ordinal(0));
    for (int i = 0; i < count; i++)
        if (!strcmp(parts[i].disk, main_disk))
            out[n].disk_bytes = parts[i].disk_bytes;
    n++;

    mkdir("/run/wildos-startup", 0700);
    mkdir(MNT, 0700);
    for (int i = 0; i < count; i++) {
        const struct part *p = &parts[i];
        bool fat = !strcmp(p->type, "vfat");
        bool ntfs = !efi && !strcmp(p->type, "ntfs");
        if (!fat && !ntfs)
            continue;
        char root[512];
        bool mine = false;
        if (!mounted_at(p->name, root, sizeof root)) {
            char dev[64];
            snprintf(dev, sizeof dev, "/dev/%s", p->name);
            if (mount(dev, MNT, fat ? "vfat" : "ntfs3", MS_RDONLY | MS_NOEXEC | MS_NOSUID, NULL))
                continue;
            snprintf(root, sizeof root, MNT);
            mine = true;
        }
        if (fat && efi) {
            look_inside(host, p, root, out, &n, max);
        } else if (ntfs && exists(root, "/bootmgr")) {
            struct sm_target *t = add(out, &n, max, K_WINDOWS, p, "/bootmgr", "ntldr");
            if (t)
                snprintf(t->name, sizeof t->name, "Windows");
        }
        if (mine)
            umount(MNT);
    }

    /* Disks in a steady order, the one Wild OS starts from first. */
    char disks[8][32];
    int disk_count = 0;
    if (*main_disk)
        snprintf(disks[disk_count++], sizeof disks[0], "%s", main_disk);
    for (int i = 0; i < count && disk_count < 8; i++) {
        bool seen = parts[i].removable;
        for (int j = 0; j < disk_count; j++)
            seen = seen || !strcmp(disks[j], parts[i].disk);
        if (!seen)
            snprintf(disks[disk_count++], sizeof disks[0], "%s", parts[i].disk);
    }

    for (int i = 1; i < n; i++) {
        struct sm_target *t = &out[i];
        for (int j = 0; j < count; j++)
            if (!strcmp(parts[j].disk, t->disk) && !strcmp(parts[j].type, "BitLocker"))
                t->bitlocker = true;
        if (efi)
            t->boot_num = sm_efi_find_entry(t->part_uuid, t->path);
        if (t->removable) {
            /* An installer stick is an ISO image: its name is the disk's
             * own label, not the small FAT partition's. */
            struct part whole = { 0 };
            char dev[64];
            snprintf(dev, sizeof dev, "/dev/%s", t->disk);
            probe(dev, &whole);
            const char *label = *whole.label ? whole.label : NULL;
            for (int j = 0; j < count && !label; j++)
                if (!strcmp(parts[j].disk, t->disk) && !strcasecmp(parts[j].uuid, t->fs_uuid)
                    && *parts[j].label)
                    label = parts[j].label;
            if (label && !strncasecmp(label, "WILDOS", 6))
                snprintf(t->where, sizeof t->where, "%s", tr("Wild OS installer"));
            else if (label)
                snprintf(t->where, sizeof t->where, "%s", label);
            else
                snprintf(t->where, sizeof t->where, "%s", tr("Plugged in now"));
            t->disk_rank = -1;
            continue;
        }
        t->disk_rank = 3;
        for (int j = 0; j < disk_count; j++)
            if (!strcmp(disks[j], t->disk))
                t->disk_rank = j;
        snprintf(t->where, sizeof t->where, "%s", ordinal(t->disk_rank));
    }

    /* Windows before other systems, USB sticks last. */
    for (int i = 1; i < n; i++)
        for (int j = i + 1; j < n; j++)
            if (out[j].kind < out[i].kind) {
                struct sm_target swap = out[i];
                out[i] = out[j];
                out[j] = swap;
            }
    return n;
}

static void json_string(const char *s)
{
    putchar('"');
    for (; *s; s++) {
        if (*s == '"' || *s == '\\')
            printf("\\%c", *s);
        else if ((unsigned char)*s < 0x20)
            printf("\\u%04x", *s);
        else
            putchar(*s);
    }
    putchar('"');
}

void sm_scan_json(const struct sm_target *t, int n)
{
    static const char *const kinds[] = { "wildos", "windows", "linux", "usb", "restore" };
    printf("[");
    for (int i = 0; i < n; i++) {
        printf("%s{\"kind\":\"%s\",\"name\":", i ? "," : "", kinds[t[i].kind]);
        json_string(t[i].name);
        printf(",\"id\":");
        json_string(t[i].id);
        printf(",\"fs_uuid\":");
        json_string(t[i].fs_uuid);
        printf(",\"path\":");
        json_string(t[i].path);
        printf(",\"loader\":");
        json_string(t[i].loader);
        printf(",\"disk\":");
        json_string(t[i].disk);
        printf(",\"disk_rank\":%d,\"disk_bytes\":%llu,\"boot_num\":%d,\"bitlocker\":%s,\"removable\":%s}",
               t[i].disk_rank, (unsigned long long)t[i].disk_bytes, t[i].boot_num,
               t[i].bitlocker ? "true" : "false", t[i].removable ? "true" : "false");
    }
    printf("]\n");
}
