#include <grp.h>
#include <pwutil.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define GROUPF "/etc/group"

static struct group g_gr;
static char   g_name[64];
static char   g_passwd[2] = "x";
static char  *g_mem[1];
static long   g_ent_off;

static struct group *fill(const char *name, gid_t gid)
{
    snprintf(g_name, sizeof g_name, "%s", name);
    g_mem[0]      = NULL;
    g_gr.gr_name  = g_name;
    g_gr.gr_passwd = g_passwd;
    g_gr.gr_gid   = gid;
    g_gr.gr_mem   = g_mem;
    return &g_gr;
}

static struct group *scan_file(const char *want_name, gid_t want_gid, long *off)
{
    FILE *f = fopen(GROUPF, "r");
    if (!f) return NULL;
    if (off && *off > 0) fseek(f, *off, SEEK_SET);

    char line[256];
    struct group *found = NULL;
    while (fgets(line, sizeof line, f)) {
        char *nl = strchr(line, '\n');
        if (nl) *nl = '\0';
        if (!line[0] || line[0] == '#') continue;

        char *name = strtok(line, ":");
        char *pw   = strtok(NULL, ":");
        char *gids = strtok(NULL, ":");
        (void)pw;
        if (!name || !gids) continue;
        gid_t gid = (gid_t)strtoul(gids, NULL, 10);

        if (off)                      { found = fill(name, gid); *off = ftell(f); break; }
        if (want_name && !strcmp(name, want_name)) { found = fill(name, gid); break; }
        if (!want_name && gid == want_gid)         { found = fill(name, gid); break; }
    }
    fclose(f);
    return found;
}

struct group *getgrgid(gid_t gid)
{
    struct group *g = scan_file(NULL, gid, NULL);
    if (g) return g;

    char uname[64];
    if (pw_lookup_uid((uint32_t)gid, uname, sizeof uname, 0, 0, 0, 0) == 0 && uname[0])
        return fill(uname, gid);
    return NULL;
}

struct group *getgrnam(const char *name)
{
    if (!name) return NULL;
    struct group *g = scan_file(name, 0, NULL);
    if (g) return g;

    uint32_t uid = 0, gid = 0;
    if (pw_lookup_name(name, &uid, &gid, 0, 0, 0, 0) == 0)
        return fill(name, (gid_t)gid);
    return NULL;
}

void setgrent(void) { g_ent_off = 0; }

struct group *getgrent(void)
{
    long off = g_ent_off;
    struct group *g = scan_file(NULL, 0, &off);
    if (!g) return NULL;
    g_ent_off = off;
    return g;
}

void endgrent(void) { g_ent_off = 0; }

int getgrouplist(const char *user, gid_t group, gid_t *groups, int *ngroups)
{
    (void)user;
    if (!ngroups) return -1;
    int cap = *ngroups;
    *ngroups = 1;
    if (cap < 1 || !groups) return -1;
    groups[0] = group;
    return 1;
}
