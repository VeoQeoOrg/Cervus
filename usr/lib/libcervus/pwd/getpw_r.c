#include <pwd.h>
#include <grp.h>
#include <string.h>
#include <errno.h>

static char *put(char **p, size_t *left, const char *s)
{
    size_t n = strlen(s) + 1;
    if (n > *left) return NULL;
    char *r = *p;
    memcpy(r, s, n);
    *p += n;
    *left -= n;
    return r;
}

static int copy_pw(const struct passwd *src, struct passwd *pw, char *buf, size_t len, struct passwd **res)
{
    *res = NULL;
    if (!src) return errno == ERANGE ? ERANGE : 0;
    char *p = buf;
    size_t left = len;
    *pw = *src;
    if (!(pw->pw_name = put(&p, &left, src->pw_name)) ||
        !(pw->pw_passwd = put(&p, &left, src->pw_passwd)) ||
        !(pw->pw_gecos = put(&p, &left, src->pw_gecos)) ||
        !(pw->pw_dir = put(&p, &left, src->pw_dir)) ||
        !(pw->pw_shell = put(&p, &left, src->pw_shell)))
        return ERANGE;
    *res = pw;
    return 0;
}

int getpwuid_r(uid_t uid, struct passwd *pw, char *buf, size_t len, struct passwd **res)
{
    return copy_pw(getpwuid(uid), pw, buf, len, res);
}

int getpwnam_r(const char *name, struct passwd *pw, char *buf, size_t len, struct passwd **res)
{
    return copy_pw(getpwnam(name), pw, buf, len, res);
}

static int copy_gr(const struct group *src, struct group *gr, char *buf, size_t len, struct group **res)
{
    *res = NULL;
    if (!src) return 0;
    size_t nmem = 0;
    while (src->gr_mem && src->gr_mem[nmem]) nmem++;
    size_t align = (size_t)(-(uintptr_t)buf) & (sizeof(char *) - 1);
    size_t need = align + (nmem + 1) * sizeof(char *);
    if (need > len) return ERANGE;
    char **mem = (char **)(buf + align);
    char *p = buf + need;
    size_t left = len - need;
    *gr = *src;
    if (!(gr->gr_name = put(&p, &left, src->gr_name)) ||
        !(gr->gr_passwd = put(&p, &left, src->gr_passwd ? src->gr_passwd : "")))
        return ERANGE;
    for (size_t i = 0; i < nmem; i++)
        if (!(mem[i] = put(&p, &left, src->gr_mem[i]))) return ERANGE;
    mem[nmem] = NULL;
    gr->gr_mem = mem;
    *res = gr;
    return 0;
}

int getgrgid_r(gid_t gid, struct group *gr, char *buf, size_t len, struct group **res)
{
    return copy_gr(getgrgid(gid), gr, buf, len, res);
}

int getgrnam_r(const char *name, struct group *gr, char *buf, size_t len, struct group **res)
{
    return copy_gr(getgrnam(name), gr, buf, len, res);
}
