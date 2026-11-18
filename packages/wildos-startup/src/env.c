/* GRUB's environment block and the manager's own key=value files.
 *
 * The block is what GRUB and the manager hand each other: which system
 * starts by default, and what to start once at the next start. It is 1024
 * bytes exactly: a header line, name=value lines, then '#' to the end. GRUB
 * writes it in place, so the size must never change.
 */
#include "sm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#define BLOCK 1024
#define HEADER "# GRUB Environment Block\n"
#define MAX_VARS 48

struct sm_env {
    char path[300];
    char key[MAX_VARS][48];
    char value[MAX_VARS][200];
    int count;
};

struct sm_env *sm_env_load(const char *path)
{
    struct sm_env *e = calloc(1, sizeof *e);
    snprintf(e->path, sizeof e->path, "%s", path);
    char block[BLOCK + 1] = "";
    FILE *f = fopen(path, "r");
    if (!f)
        return e;
    size_t got = fread(block, 1, BLOCK, f);
    fclose(f);
    block[got] = 0;
    for (char *row = strtok(block, "\n"); row; row = strtok(NULL, "\n")) {
        char *eq = strchr(row, '=');
        if (row[0] == '#' || !eq || e->count == MAX_VARS)
            continue;
        *eq = 0;
        snprintf(e->key[e->count], sizeof e->key[0], "%s", row);
        snprintf(e->value[e->count], sizeof e->value[0], "%s", eq + 1);
        e->count++;
    }
    return e;
}

const char *sm_env_get(const struct sm_env *e, const char *key)
{
    for (int i = 0; i < e->count; i++)
        if (!strcmp(e->key[i], key))
            return e->value[i];
    return "";
}

/* An empty value removes the name, which is how GRUB reads "not set". */
void sm_env_set(struct sm_env *e, const char *key, const char *value)
{
    for (int i = 0; i < e->count; i++) {
        if (strcmp(e->key[i], key))
            continue;
        if (*value) {
            snprintf(e->value[i], sizeof e->value[0], "%s", value);
        } else {
            e->count--;
            memmove(e->key[i], e->key[i + 1], sizeof e->key[0] * (size_t)(e->count - i));
            memmove(e->value[i], e->value[i + 1], sizeof e->value[0] * (size_t)(e->count - i));
        }
        return;
    }
    if (*value && e->count < MAX_VARS) {
        snprintf(e->key[e->count], sizeof e->key[0], "%s", key);
        snprintf(e->value[e->count], sizeof e->value[0], "%s", value);
        e->count++;
    }
}

bool sm_env_save(const struct sm_env *e)
{
    char block[BLOCK];
    size_t at = strlen(HEADER);
    memset(block, '#', BLOCK);
    memcpy(block, HEADER, at);
    for (int i = 0; i < e->count; i++) {
        size_t need = strlen(e->key[i]) + strlen(e->value[i]) + 2;
        /* sprintf also writes a terminator, which must stay inside the block. */
        if (at + need >= BLOCK)
            return false;
        at += (size_t)sprintf(block + at, "%s=%s\n", e->key[i], e->value[i]);
        block[at] = '#';
    }
    /* Written over the old block, not replaced: GRUB finds the file by the
     * disk blocks it had when grub.cfg was made. */
    FILE *f = fopen(e->path, "r+");
    if (!f)
        f = fopen(e->path, "w");
    if (!f)
        return false;
    bool ok = fwrite(block, 1, BLOCK, f) == BLOCK;
    ok = fflush(f) == 0 && ok;
    fsync(fileno(f));
    fclose(f);
    return ok;
}

void sm_env_free(struct sm_env *e)
{
    free(e);
}

bool sm_read_kv(const char *path, void (*each)(const char *k, const char *v, void *u), void *u)
{
    char row[600];
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    while (fgets(row, sizeof row, f)) {
        row[strcspn(row, "\r\n")] = 0;
        char *eq = strchr(row, '=');
        if (row[0] == '#' || !eq)
            continue;
        *eq = 0;
        each(row, eq + 1, u);
    }
    fclose(f);
    return true;
}
