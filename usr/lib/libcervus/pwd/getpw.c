#include <pwd.h>
#include <pwutil.h>
#include <string.h>
#include <stdio.h>

static struct passwd g_pw;
static char g_name[64];
static char g_home[128];
static char g_shell[128];
static char g_passwd_x[2] = "x";
static char g_gecos_empty[1] = "";
static uint32_t g_ent_next;

static struct passwd *fill(uint32_t uid, uint32_t gid)
{
    g_pw.pw_name   = g_name;
    g_pw.pw_passwd = g_passwd_x;
    g_pw.pw_uid    = (uid_t)uid;
    g_pw.pw_gid    = (gid_t)gid;
    g_pw.pw_gecos  = g_gecos_empty;
    g_pw.pw_dir    = g_home;
    g_pw.pw_shell  = g_shell;
    return &g_pw;
}

struct passwd *getpwuid(uid_t uid)
{
    g_name[0] = g_home[0] = g_shell[0] = '\0';
    if (pw_lookup_uid((uint32_t)uid, g_name, (int)sizeof g_name,
                      g_home, (int)sizeof g_home,
                      g_shell, (int)sizeof g_shell) != 0)
        return 0;
    return fill((uint32_t)uid, (uint32_t)uid);
}

struct passwd *getpwnam(const char *name)
{
    uint32_t uid = 0, gid = 0;
    g_home[0] = g_shell[0] = '\0';
    if (!name) return 0;
    if (pw_lookup_name(name, &uid, &gid,
                       g_home, (int)sizeof g_home,
                       g_shell, (int)sizeof g_shell) != 0)
        return 0;
    snprintf(g_name, sizeof g_name, "%s", name);
    return fill(uid, gid);
}

void setpwent(void) { g_ent_next = 0; }

struct passwd *getpwent(void)
{
    while (g_ent_next < 65536u) {
        uint32_t uid = g_ent_next++;
        struct passwd *pw = getpwuid((uid_t)uid);
        if (pw) return pw;
    }
    return 0;
}

void endpwent(void) { g_ent_next = 0; }
