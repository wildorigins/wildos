/* The PC firmware's own start-up list, through efivarfs.
 *
 * When the firmware has an entry for the system being started, the manager
 * asks the firmware to start it at the next start (BootNext) and restarts.
 * Windows then starts exactly as it does without Wild OS on the PC, which
 * is what BitLocker needs: started through another boot loader it asks for
 * its recovery key. Systems the firmware has no entry for go through GRUB.
 */
#include "sm.h"

#include <ctype.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/fs.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/ioctl.h>
#include <sys/mount.h>
#include <unistd.h>

#define VARS "/sys/firmware/efi/efivars"
#define GLOBAL "8be4df61-93ca-11d2-aa0d-00e098032b8c"

static void ensure_mounted(void)
{
    if (access(VARS "/BootOrder-" GLOBAL, F_OK) && access(VARS "/BootCurrent-" GLOBAL, F_OK))
        mount("efivarfs", VARS, "efivarfs", MS_NOSUID | MS_NODEV | MS_NOEXEC, NULL);
}

static int read_var(const char *name, unsigned char *out, int size)
{
    char path[300];
    snprintf(path, sizeof path, VARS "/%s-" GLOBAL, name);
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return -1;
    int got = (int)read(fd, out, (size_t)size);
    close(fd);
    return got;
}

/* The first four bytes of every variable file are its attributes. */
static bool write_var(const char *name, const unsigned char *data, int size)
{
    char path[300];
    unsigned char buf[64] = { 7, 0, 0, 0 };
    if (size > (int)sizeof buf - 4)
        return false;
    memcpy(buf + 4, data, (size_t)size);
    snprintf(path, sizeof path, VARS "/%s-" GLOBAL, name);
    /* The kernel marks variables immutable so a stray rm cannot brick a PC. */
    int fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd >= 0) {
        int flags = 0;
        if (!ioctl(fd, FS_IOC_GETFLAGS, &flags) && (flags & FS_IMMUTABLE_FL)) {
            flags &= ~FS_IMMUTABLE_FL;
            ioctl(fd, FS_IOC_SETFLAGS, &flags);
        }
        close(fd);
    }
    fd = open(path, O_WRONLY | O_CREAT | O_CLOEXEC, 0644);
    if (fd < 0)
        return false;
    bool ok = write(fd, buf, (size_t)size + 4) == size + 4;
    close(fd);
    return ok;
}

static void guid_text(const unsigned char *g, char *out, size_t size)
{
    snprintf(out, size, "%02x%02x%02x%02x-%02x%02x-%02x%02x-%02x%02x-%02x%02x%02x%02x%02x%02x",
             g[3], g[2], g[1], g[0], g[5], g[4], g[7], g[6], g[8], g[9], g[10], g[11], g[12],
             g[13], g[14], g[15]);
}

/* Does this load option start `path` on the partition `part_uuid`? */
static bool option_matches(const unsigned char *o, int size, const char *part_uuid, const char *path)
{
    if (size < 10 || !(o[4] & 1))
        return false;
    int list_len = o[8] | o[9] << 8, at = 10;
    while (at + 1 < size && (o[at] || o[at + 1]))
        at += 2;
    at += 2;
    int end = at + list_len;
    if (end > size)
        end = size;
    bool part_ok = false, path_ok = false;
    while (at + 4 <= end) {
        int type = o[at], sub = o[at + 1], len = o[at + 2] | o[at + 3] << 8;
        if (len < 4 || at + len > end)
            break;
        if (type == 4 && sub == 1 && len >= 42 && o[at + 41] == 2) {
            char guid[48];
            guid_text(o + at + 24, guid, sizeof guid);
            part_ok = !strcasecmp(guid, part_uuid);
        } else if (type == 4 && sub == 4) {
            char text[256];
            int n = 0;
            for (int i = at + 4; i + 1 < at + len && n < 255; i += 2) {
                if (!o[i] && !o[i + 1])
                    break;
                text[n++] = o[i] == '\\' ? '/' : (char)o[i];
            }
            text[n] = 0;
            path_ok = !strcasecmp(text, text[0] == '/' ? path : path + 1);
        } else if (type == 0x7f) {
            break;
        }
        at += len;
    }
    return part_ok && path_ok;
}

int sm_efi_find_entry(const char *part_uuid, const char *path)
{
    if (!sm_is_efi() || !*part_uuid)
        return -1;
    ensure_mounted();
    DIR *d = opendir(VARS);
    if (!d)
        return -1;
    int found = -1;
    for (struct dirent *e; found < 0 && (e = readdir(d));) {
        const char *n = e->d_name;
        if (strncmp(n, "Boot", 4) || strlen(n) != 8 + 1 + strlen(GLOBAL) || strcmp(n + 9, GLOBAL)
            || !isxdigit((unsigned char)n[4]) || !isxdigit((unsigned char)n[5])
            || !isxdigit((unsigned char)n[6]) || !isxdigit((unsigned char)n[7]))
            continue;
        char name[16];
        unsigned char data[4096];
        snprintf(name, sizeof name, "%.8s", n);
        int size = read_var(name, data, sizeof data);
        if (size > 0 && option_matches(data, size, part_uuid, path))
            found = (int)strtol(name + 4, NULL, 16);
    }
    closedir(d);
    return found;
}

bool sm_efi_set_boot_next(int num)
{
    ensure_mounted();
    unsigned char v[2] = { (unsigned char)(num & 255), (unsigned char)(num >> 8) };
    return write_var("BootNext", v, 2);
}

bool sm_efi_firmware_setup_supported(void)
{
    if (!sm_is_efi())
        return false;
    ensure_mounted();
    unsigned char v[12];
    return read_var("OsIndicationsSupported", v, sizeof v) == 12 && (v[4] & 1);
}

bool sm_efi_request_firmware_setup(void)
{
    unsigned char v[12] = { 0 };
    read_var("OsIndications", v, sizeof v);
    v[4] |= 1;
    return write_var("OsIndications", v + 4, 8);
}
