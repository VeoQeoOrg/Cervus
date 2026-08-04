#include <http.h>
#include <string.h>
#include <stdio.h>

static int ci_eq(const char *a, const char *b, int n) {
    for (int i = 0; i < n; i++) {
        char x = a[i], y = b[i];
        if (x >= 'A' && x <= 'Z') x += 32;
        if (y >= 'A' && y <= 'Z') y += 32;
        if (x != y) return 0;
        if (!x) return 1;
    }
    return 1;
}

static void copy_field(char *dst, int cap, const char *src, int len) {
    if (len >= cap) len = cap - 1;
    if (len < 0) len = 0;
    memcpy(dst, src, len);
    dst[len] = 0;
}

static int host_matches(const char *host, const char *domain) {
    if (domain[0] == '.') domain++;
    if (!strcmp(host, domain)) return 1;
    int hl = (int)strlen(host), dl = (int)strlen(domain);
    if (hl > dl && host[hl - dl - 1] == '.' && !strcmp(host + hl - dl, domain)) return 1;
    return 0;
}

void http_jar_set(http_cookie_jar *j, const char *host, const char *setcookie) {
    if (!j) return;
    const char *p = setcookie;
    while (*p == ' ') p++;
    const char *eq = strchr(p, '=');
    if (!eq) return;
    const char *semi = strchr(p, ';');
    const char *vend = semi ? semi : p + strlen(p);

    char name[64], value[256], domain[128];
    copy_field(name, sizeof name, p, (int)(eq - p));
    copy_field(value, sizeof value, eq + 1, (int)(vend - (eq + 1)));
    strncpy(domain, host, sizeof domain - 1); domain[sizeof domain - 1] = 0;

    const char *d = semi;
    while (d) {
        while (*d == ';' || *d == ' ') d++;
        if (!*d) break;
        if (ci_eq(d, "domain=", 7)) {
            const char *ds = d + 7;
            const char *de = strchr(ds, ';');
            copy_field(domain, sizeof domain, ds, de ? (int)(de - ds) : (int)strlen(ds));
        }
        d = strchr(d, ';');
    }
    if (domain[0] == '.') memmove(domain, domain + 1, strlen(domain));

    for (int i = 0; i < j->n; i++) {
        if (!strcmp(j->c[i].name, name) && !strcmp(j->c[i].domain, domain)) {
            strncpy(j->c[i].value, value, sizeof j->c[i].value - 1);
            j->c[i].value[sizeof j->c[i].value - 1] = 0;
            return;
        }
    }
    if (j->n < HTTP_MAX_COOKIES) {
        http_cookie *c = &j->c[j->n++];
        strncpy(c->domain, domain, sizeof c->domain - 1); c->domain[sizeof c->domain - 1] = 0;
        strncpy(c->name, name, sizeof c->name - 1); c->name[sizeof c->name - 1] = 0;
        strncpy(c->value, value, sizeof c->value - 1); c->value[sizeof c->value - 1] = 0;
    }
}

int http_jar_header(http_cookie_jar *j, const char *host, char *out, int cap) {
    if (!j) { out[0] = 0; return 0; }
    int len = 0, first = 1;
    for (int i = 0; i < j->n; i++) {
        if (!host_matches(host, j->c[i].domain)) continue;
        int need = (int)strlen(j->c[i].name) + (int)strlen(j->c[i].value) + 4;
        if (len + need >= cap) break;
        if (!first) { out[len++] = ';'; out[len++] = ' '; }
        first = 0;
        len += snprintf(out + len, cap - len, "%s=%s", j->c[i].name, j->c[i].value);
    }
    out[len] = 0;
    return len;
}
