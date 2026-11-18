/* The manager's words in the PC's language.
 *
 * The start-up files have no gettext catalogues, so each language is one
 * small file: the English line, a tab, the translation.
 */
#include "sm.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct { char *from, *to; } *words;
static int word_count;

static bool load(const char *dir, const char *lang)
{
    char path[400], row[1024];
    snprintf(path, sizeof path, "%s/i18n/%s.tsv", dir, lang);
    FILE *f = fopen(path, "r");
    if (!f)
        return false;
    while (fgets(row, sizeof row, f)) {
        row[strcspn(row, "\r\n")] = 0;
        char *tab = strchr(row, '\t');
        if (!tab || row[0] == '#' || !tab[1])
            continue;
        *tab = 0;
        words = realloc(words, sizeof *words * (size_t)(word_count + 1));
        words[word_count].from = strdup(row);
        words[word_count].to = strdup(tab + 1);
        word_count++;
    }
    fclose(f);
    return true;
}

void sm_tr_load(const char *dir, const char *lang)
{
    char code[16];
    snprintf(code, sizeof code, "%s", lang);
    code[strcspn(code, ".@")] = 0;
    if (!*code || load(dir, code))
        return;
    code[strcspn(code, "_")] = 0;
    load(dir, code);
}

const char *tr(const char *english)
{
    for (int i = 0; i < word_count; i++)
        if (!strcmp(words[i].from, english))
            return words[i].to;
    return english;
}
