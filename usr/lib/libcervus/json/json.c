#include <json.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct json {
    json_type_t type;
    union {
        int    boolean;
        double number;
        char  *string;
        struct {
            json_t **items;
            char   **keys;
            size_t   count;
            size_t   cap;
        } list;
    } u;
};

typedef struct {
    const char   *p;
    const char   *end;
    int           line;
    const char   *line_start;
    int           depth;
    json_error_t *err;
} parser_t;

static json_t *parse_value(parser_t *ps);

static json_t *node_new(json_type_t t) {
    json_t *v = calloc(1, sizeof *v);
    if (v) v->type = t;
    return v;
}

static void fail(parser_t *ps, const char *msg) {
    if (!ps->err || ps->err->message[0]) return;
    ps->err->line = ps->line;
    ps->err->column = (int)(ps->p - ps->line_start) + 1;
    snprintf(ps->err->message, sizeof ps->err->message, "%s", msg);
}

static int failed(parser_t *ps) {
    return ps->err && ps->err->message[0];
}

static void skip_ws(parser_t *ps) {
    while (ps->p < ps->end) {
        char c = *ps->p;
        if (c == '\n') {
            ps->line++;
            ps->p++;
            ps->line_start = ps->p;
        } else if (c == ' ' || c == '\t' || c == '\r') {
            ps->p++;
        } else {
            break;
        }
    }
}

static int list_grow(json_t *v, int with_keys) {
    if (v->u.list.count < v->u.list.cap) return 0;
    size_t cap = v->u.list.cap ? v->u.list.cap * 2 : 8;
    json_t **items = realloc(v->u.list.items, cap * sizeof *items);
    if (!items) return -1;
    v->u.list.items = items;
    if (with_keys) {
        char **keys = realloc(v->u.list.keys, cap * sizeof *keys);
        if (!keys) return -1;
        v->u.list.keys = keys;
    }
    v->u.list.cap = cap;
    return 0;
}

static int utf8_put(char *out, unsigned cp) {
    if (cp < 0x80) {
        out[0] = (char)cp;
        return 1;
    }
    if (cp < 0x800) {
        out[0] = (char)(0xC0 | (cp >> 6));
        out[1] = (char)(0x80 | (cp & 0x3F));
        return 2;
    }
    if (cp < 0x10000) {
        out[0] = (char)(0xE0 | (cp >> 12));
        out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
        out[2] = (char)(0x80 | (cp & 0x3F));
        return 3;
    }
    out[0] = (char)(0xF0 | (cp >> 18));
    out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
    out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[3] = (char)(0x80 | (cp & 0x3F));
    return 4;
}

static int hex4(const char *p, unsigned *out) {
    unsigned v = 0;
    for (int i = 0; i < 4; i++) {
        char c = p[i];
        v <<= 4;
        if (c >= '0' && c <= '9')      v |= (unsigned)(c - '0');
        else if (c >= 'a' && c <= 'f') v |= (unsigned)(c - 'a' + 10);
        else if (c >= 'A' && c <= 'F') v |= (unsigned)(c - 'A' + 10);
        else return -1;
    }
    *out = v;
    return 0;
}

static char *parse_string_raw(parser_t *ps) {
    ps->p++;
    const char *start = ps->p;
    size_t worst = 1;
    for (const char *q = start; q < ps->end && *q != '"'; q++) {
        worst += (*q == '\\') ? 4 : 1;
        if (*q == '\\' && q + 1 < ps->end) q++;
    }

    char *out = malloc(worst);
    if (!out) {
        fail(ps, "out of memory");
        return NULL;
    }
    size_t n = 0;

    while (ps->p < ps->end) {
        unsigned char c = (unsigned char)*ps->p;
        if (c == '"') {
            ps->p++;
            out[n] = '\0';
            return out;
        }
        if (c == '\n') {
            fail(ps, "a string may not contain a raw newline");
            free(out);
            return NULL;
        }
        if (c != '\\') {
            out[n++] = (char)c;
            ps->p++;
            continue;
        }

        ps->p++;
        if (ps->p >= ps->end) break;
        char e = *ps->p++;
        switch (e) {
            case '"':  out[n++] = '"';  break;
            case '\\': out[n++] = '\\'; break;
            case '/':  out[n++] = '/';  break;
            case 'b':  out[n++] = '\b'; break;
            case 'f':  out[n++] = '\f'; break;
            case 'n':  out[n++] = '\n'; break;
            case 'r':  out[n++] = '\r'; break;
            case 't':  out[n++] = '\t'; break;
            case 'u': {
                unsigned cp;
                if (ps->p + 4 > ps->end || hex4(ps->p, &cp) != 0) {
                    fail(ps, "\\u needs four hex digits");
                    free(out);
                    return NULL;
                }
                ps->p += 4;
                if (cp >= 0xD800 && cp <= 0xDBFF &&
                    ps->p + 6 <= ps->end && ps->p[0] == '\\' && ps->p[1] == 'u') {
                    unsigned lo;
                    if (hex4(ps->p + 2, &lo) == 0 && lo >= 0xDC00 && lo <= 0xDFFF) {
                        cp = 0x10000 + ((cp - 0xD800) << 10) + (lo - 0xDC00);
                        ps->p += 6;
                    }
                }
                n += (size_t)utf8_put(out + n, cp);
                break;
            }
            default:
                fail(ps, "unknown escape after a backslash");
                free(out);
                return NULL;
        }
    }

    fail(ps, "a string was never closed");
    free(out);
    return NULL;
}

static json_t *parse_number(parser_t *ps) {
    const char *start = ps->p;
    if (ps->p < ps->end && (*ps->p == '-' || *ps->p == '+')) ps->p++;

    int digits = 0;
    while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') { ps->p++; digits++; }
    if (ps->p < ps->end && *ps->p == '.') {
        ps->p++;
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') { ps->p++; digits++; }
    }
    if (!digits) {
        fail(ps, "a number with no digits");
        return NULL;
    }
    if (ps->p < ps->end && (*ps->p == 'e' || *ps->p == 'E')) {
        const char *save = ps->p;
        ps->p++;
        if (ps->p < ps->end && (*ps->p == '-' || *ps->p == '+')) ps->p++;
        int ed = 0;
        while (ps->p < ps->end && *ps->p >= '0' && *ps->p <= '9') { ps->p++; ed++; }
        if (!ed) ps->p = save;
    }

    size_t len = (size_t)(ps->p - start);
    char buf[64];
    if (len >= sizeof buf) len = sizeof buf - 1;
    memcpy(buf, start, len);
    buf[len] = '\0';

    json_t *v = node_new(JSON_NUMBER);
    if (!v) {
        fail(ps, "out of memory");
        return NULL;
    }
    v->u.number = strtod(buf, NULL);
    return v;
}

static json_t *parse_array(parser_t *ps) {
    json_t *v = node_new(JSON_ARRAY);
    if (!v) {
        fail(ps, "out of memory");
        return NULL;
    }
    ps->p++;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == ']') {
        ps->p++;
        return v;
    }

    for (;;) {
        json_t *item = parse_value(ps);
        if (!item) {
            json_free(v);
            return NULL;
        }
        if (json_append(v, item) != 0) {
            json_free(item);
            json_free(v);
            fail(ps, "out of memory");
            return NULL;
        }
        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;
            skip_ws(ps);
            continue;
        }
        if (ps->p < ps->end && *ps->p == ']') {
            ps->p++;
            return v;
        }
        fail(ps, "expected , or ] in an array");
        json_free(v);
        return NULL;
    }
}

static json_t *parse_object(parser_t *ps) {
    json_t *v = node_new(JSON_OBJECT);
    if (!v) {
        fail(ps, "out of memory");
        return NULL;
    }
    ps->p++;
    skip_ws(ps);
    if (ps->p < ps->end && *ps->p == '}') {
        ps->p++;
        return v;
    }

    for (;;) {
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != '"') {
            fail(ps, "expected a quoted key in an object");
            json_free(v);
            return NULL;
        }
        char *key = parse_string_raw(ps);
        if (!key) {
            json_free(v);
            return NULL;
        }
        skip_ws(ps);
        if (ps->p >= ps->end || *ps->p != ':') {
            fail(ps, "expected : after a key");
            free(key);
            json_free(v);
            return NULL;
        }
        ps->p++;

        json_t *val = parse_value(ps);
        if (!val) {
            free(key);
            json_free(v);
            return NULL;
        }
        if (list_grow(v, 1) != 0) {
            free(key);
            json_free(val);
            json_free(v);
            fail(ps, "out of memory");
            return NULL;
        }
        v->u.list.keys[v->u.list.count] = key;
        v->u.list.items[v->u.list.count] = val;
        v->u.list.count++;

        skip_ws(ps);
        if (ps->p < ps->end && *ps->p == ',') {
            ps->p++;
            continue;
        }
        if (ps->p < ps->end && *ps->p == '}') {
            ps->p++;
            return v;
        }
        fail(ps, "expected , or } in an object");
        json_free(v);
        return NULL;
    }
}

static json_t *parse_value(parser_t *ps) {
    if (ps->depth > 200) {
        fail(ps, "nested too deeply");
        return NULL;
    }
    skip_ws(ps);
    if (ps->p >= ps->end) {
        fail(ps, "the text ended where a value was expected");
        return NULL;
    }

    char c = *ps->p;
    json_t *v = NULL;

    if (c == '{' || c == '[') {
        ps->depth++;
        v = (c == '{') ? parse_object(ps) : parse_array(ps);
        ps->depth--;
        return v;
    }
    if (c == '"') {
        char *s = parse_string_raw(ps);
        if (!s) return NULL;
        v = node_new(JSON_STRING);
        if (!v) {
            free(s);
            fail(ps, "out of memory");
            return NULL;
        }
        v->u.string = s;
        return v;
    }
    if ((size_t)(ps->end - ps->p) >= 4 && memcmp(ps->p, "true", 4) == 0) {
        ps->p += 4;
        v = node_new(JSON_BOOL);
        if (v) v->u.boolean = 1;
        return v;
    }
    if ((size_t)(ps->end - ps->p) >= 5 && memcmp(ps->p, "false", 5) == 0) {
        ps->p += 5;
        v = node_new(JSON_BOOL);
        if (v) v->u.boolean = 0;
        return v;
    }
    if ((size_t)(ps->end - ps->p) >= 4 && memcmp(ps->p, "null", 4) == 0) {
        ps->p += 4;
        return node_new(JSON_NULL);
    }
    if (c == '-' || c == '+' || (c >= '0' && c <= '9')) return parse_number(ps);

    fail(ps, "not a value: expected {, [, a string, a number, true, false or null");
    return NULL;
}

json_t *json_parse_len(const char *text, size_t len, json_error_t *err) {
    if (err) {
        err->line = 0;
        err->column = 0;
        err->message[0] = '\0';
    }
    if (!text) {
        if (err) snprintf(err->message, sizeof err->message, "no text to parse");
        return NULL;
    }

    parser_t ps;
    ps.p = text;
    ps.end = text + len;
    ps.line = 1;
    ps.line_start = text;
    ps.depth = 0;
    ps.err = err;

    json_t *v = parse_value(&ps);
    if (!v) return NULL;

    skip_ws(&ps);
    if (ps.p != ps.end) {
        fail(&ps, "trailing text after the value");
        json_free(v);
        return NULL;
    }
    if (failed(&ps)) {
        json_free(v);
        return NULL;
    }
    return v;
}

json_t *json_parse(const char *text, json_error_t *err) {
    return json_parse_len(text, text ? strlen(text) : 0, err);
}

json_t *json_parse_file(const char *path, json_error_t *err) {
    if (err) {
        err->line = 0;
        err->column = 0;
        err->message[0] = '\0';
    }
    FILE *f = fopen(path, "rb");
    if (!f) {
        if (err) snprintf(err->message, sizeof err->message, "cannot open %s", path);
        return NULL;
    }
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz < 0) {
        fclose(f);
        if (err) snprintf(err->message, sizeof err->message, "cannot measure %s", path);
        return NULL;
    }
    char *buf = malloc((size_t)sz + 1);
    if (!buf) {
        fclose(f);
        if (err) snprintf(err->message, sizeof err->message, "out of memory");
        return NULL;
    }
    size_t got = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[got] = '\0';

    json_t *v = json_parse_len(buf, got, err);
    free(buf);
    return v;
}

void json_free(json_t *v) {
    if (!v) return;
    if (v->type == JSON_STRING) {
        free(v->u.string);
    } else if (v->type == JSON_ARRAY || v->type == JSON_OBJECT) {
        for (size_t i = 0; i < v->u.list.count; i++) {
            json_free(v->u.list.items[i]);
            if (v->u.list.keys) free(v->u.list.keys[i]);
        }
        free(v->u.list.items);
        free(v->u.list.keys);
    }
    free(v);
}

json_type_t json_type(const json_t *v) { return v ? v->type : JSON_NULL; }
int json_is_null(const json_t *v)      { return !v || v->type == JSON_NULL; }

const char *json_string(const json_t *v, const char *fallback) {
    return (v && v->type == JSON_STRING) ? v->u.string : fallback;
}

double json_number(const json_t *v, double fallback) {
    if (!v) return fallback;
    if (v->type == JSON_NUMBER) return v->u.number;
    if (v->type == JSON_BOOL)   return v->u.boolean ? 1.0 : 0.0;
    if (v->type == JSON_STRING) return strtod(v->u.string, NULL);
    return fallback;
}

long json_int(const json_t *v, long fallback) {
    if (!v) return fallback;
    if (v->type == JSON_NUMBER) return (long)v->u.number;
    if (v->type == JSON_BOOL)   return v->u.boolean ? 1 : 0;
    if (v->type == JSON_STRING) return strtol(v->u.string, NULL, 10);
    return fallback;
}

int json_bool(const json_t *v, int fallback) {
    if (!v) return fallback;
    if (v->type == JSON_BOOL)   return v->u.boolean;
    if (v->type == JSON_NUMBER) return v->u.number != 0.0;
    if (v->type == JSON_NULL)   return 0;
    if (v->type == JSON_STRING) return v->u.string[0] != '\0';
    return fallback;
}

size_t json_len(const json_t *v) {
    if (!v) return 0;
    if (v->type == JSON_ARRAY || v->type == JSON_OBJECT) return v->u.list.count;
    if (v->type == JSON_STRING) return strlen(v->u.string);
    return 0;
}

json_t *json_at(const json_t *arr, size_t index) {
    if (!arr || arr->type != JSON_ARRAY || index >= arr->u.list.count) return NULL;
    return arr->u.list.items[index];
}

json_t *json_get(const json_t *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return NULL;
    for (size_t i = 0; i < obj->u.list.count; i++)
        if (strcmp(obj->u.list.keys[i], key) == 0) return obj->u.list.items[i];
    return NULL;
}

const char *json_key(const json_t *obj, size_t index) {
    if (!obj || obj->type != JSON_OBJECT || index >= obj->u.list.count) return NULL;
    return obj->u.list.keys[index];
}

json_t *json_value_at(const json_t *obj, size_t index) {
    if (!obj || obj->type != JSON_OBJECT || index >= obj->u.list.count) return NULL;
    return obj->u.list.items[index];
}

json_t *json_query(const json_t *root, const char *path) {
    const json_t *cur = root;
    if (!cur || !path) return NULL;

    while (*path && cur) {
        if (*path == '.') {
            path++;
            continue;
        }
        if (*path == '[') {
            path++;
            size_t idx = 0;
            int digits = 0;
            while (*path >= '0' && *path <= '9') {
                idx = idx * 10 + (size_t)(*path - '0');
                path++;
                digits++;
            }
            if (!digits || *path != ']') return NULL;
            path++;
            cur = json_at(cur, idx);
            continue;
        }
        char name[128];
        size_t n = 0;
        while (*path && *path != '.' && *path != '[' && n + 1 < sizeof name)
            name[n++] = *path++;
        name[n] = '\0';
        cur = json_get(cur, name);
    }
    return (json_t *)cur;
}

json_t *json_new_null(void)   { return node_new(JSON_NULL); }

json_t *json_new_bool(int b) {
    json_t *v = node_new(JSON_BOOL);
    if (v) v->u.boolean = b ? 1 : 0;
    return v;
}

json_t *json_new_number(double n) {
    json_t *v = node_new(JSON_NUMBER);
    if (v) v->u.number = n;
    return v;
}

json_t *json_new_string(const char *s) {
    json_t *v = node_new(JSON_STRING);
    if (!v) return NULL;
    size_t n = s ? strlen(s) : 0;
    v->u.string = malloc(n + 1);
    if (!v->u.string) {
        free(v);
        return NULL;
    }
    if (n) memcpy(v->u.string, s, n);
    v->u.string[n] = '\0';
    return v;
}

json_t *json_new_array(void)  { return node_new(JSON_ARRAY); }
json_t *json_new_object(void) { return node_new(JSON_OBJECT); }

int json_append(json_t *arr, json_t *item) {
    if (!arr || arr->type != JSON_ARRAY || !item) return -1;
    if (list_grow(arr, 0) != 0) return -1;
    arr->u.list.items[arr->u.list.count++] = item;
    return 0;
}

int json_set(json_t *obj, const char *key, json_t *value) {
    if (!obj || obj->type != JSON_OBJECT || !key || !value) return -1;

    for (size_t i = 0; i < obj->u.list.count; i++) {
        if (strcmp(obj->u.list.keys[i], key) == 0) {
            json_free(obj->u.list.items[i]);
            obj->u.list.items[i] = value;
            return 0;
        }
    }
    if (list_grow(obj, 1) != 0) return -1;

    size_t n = strlen(key);
    char *copy = malloc(n + 1);
    if (!copy) return -1;
    memcpy(copy, key, n + 1);

    obj->u.list.keys[obj->u.list.count] = copy;
    obj->u.list.items[obj->u.list.count] = value;
    obj->u.list.count++;
    return 0;
}

int json_remove(json_t *obj, const char *key) {
    if (!obj || obj->type != JSON_OBJECT || !key) return -1;
    for (size_t i = 0; i < obj->u.list.count; i++) {
        if (strcmp(obj->u.list.keys[i], key) != 0) continue;
        free(obj->u.list.keys[i]);
        json_free(obj->u.list.items[i]);
        for (size_t j = i + 1; j < obj->u.list.count; j++) {
            obj->u.list.keys[j - 1] = obj->u.list.keys[j];
            obj->u.list.items[j - 1] = obj->u.list.items[j];
        }
        obj->u.list.count--;
        return 0;
    }
    return -1;
}

typedef struct {
    char  *buf;
    size_t len;
    size_t cap;
    int    oom;
} sink_t;

static void sink_put(sink_t *s, const char *data, size_t n) {
    if (s->oom) return;
    if (s->len + n + 1 > s->cap) {
        size_t cap = s->cap ? s->cap * 2 : 256;
        while (cap < s->len + n + 1) cap *= 2;
        char *nb = realloc(s->buf, cap);
        if (!nb) {
            s->oom = 1;
            return;
        }
        s->buf = nb;
        s->cap = cap;
    }
    memcpy(s->buf + s->len, data, n);
    s->len += n;
    s->buf[s->len] = '\0';
}

static void sink_str(sink_t *s, const char *text) { sink_put(s, text, strlen(text)); }

static void sink_quoted(sink_t *s, const char *text) {
    sink_put(s, "\"", 1);
    for (const unsigned char *p = (const unsigned char *)text; *p; p++) {
        switch (*p) {
            case '"':  sink_str(s, "\\\""); break;
            case '\\': sink_str(s, "\\\\"); break;
            case '\b': sink_str(s, "\\b");  break;
            case '\f': sink_str(s, "\\f");  break;
            case '\n': sink_str(s, "\\n");  break;
            case '\r': sink_str(s, "\\r");  break;
            case '\t': sink_str(s, "\\t");  break;
            default:
                if (*p < 0x20) {
                    char esc[8];
                    snprintf(esc, sizeof esc, "\\u%04x", *p);
                    sink_str(s, esc);
                } else {
                    sink_put(s, (const char *)p, 1);
                }
        }
    }
    sink_put(s, "\"", 1);
}

static void sink_number(sink_t *s, double n) {
    char buf[40];
    if (n == (double)(long)n && n < 1e15 && n > -1e15)
        snprintf(buf, sizeof buf, "%ld", (long)n);
    else
        snprintf(buf, sizeof buf, "%.17g", n);
    sink_str(s, buf);
}

static void sink_indent(sink_t *s, int indent, int depth) {
    if (indent <= 0) return;
    sink_put(s, "\n", 1);
    for (int i = 0; i < indent * depth; i++) sink_put(s, " ", 1);
}

static void dump_value(sink_t *s, const json_t *v, int indent, int depth) {
    if (!v) {
        sink_str(s, "null");
        return;
    }
    switch (v->type) {
        case JSON_NULL:   sink_str(s, "null"); break;
        case JSON_BOOL:   sink_str(s, v->u.boolean ? "true" : "false"); break;
        case JSON_NUMBER: sink_number(s, v->u.number); break;
        case JSON_STRING: sink_quoted(s, v->u.string); break;
        case JSON_ARRAY:
            if (v->u.list.count == 0) {
                sink_str(s, "[]");
                break;
            }
            sink_put(s, "[", 1);
            for (size_t i = 0; i < v->u.list.count; i++) {
                if (i) sink_put(s, ",", 1);
                sink_indent(s, indent, depth + 1);
                dump_value(s, v->u.list.items[i], indent, depth + 1);
            }
            sink_indent(s, indent, depth);
            sink_put(s, "]", 1);
            break;
        case JSON_OBJECT:
            if (v->u.list.count == 0) {
                sink_str(s, "{}");
                break;
            }
            sink_put(s, "{", 1);
            for (size_t i = 0; i < v->u.list.count; i++) {
                if (i) sink_put(s, ",", 1);
                sink_indent(s, indent, depth + 1);
                sink_quoted(s, v->u.list.keys[i]);
                sink_str(s, indent > 0 ? ": " : ":");
                dump_value(s, v->u.list.items[i], indent, depth + 1);
            }
            sink_indent(s, indent, depth);
            sink_put(s, "}", 1);
            break;
    }
}

char *json_dump(const json_t *v, int indent) {
    sink_t s = { NULL, 0, 0, 0 };
    sink_put(&s, "", 0);
    dump_value(&s, v, indent, 0);
    if (s.oom) {
        free(s.buf);
        return NULL;
    }
    return s.buf;
}

int json_dump_file(const json_t *v, const char *path, int indent) {
    char *text = json_dump(v, indent);
    if (!text) return -1;
    FILE *f = fopen(path, "w");
    if (!f) {
        free(text);
        return -1;
    }
    size_t n = strlen(text);
    size_t put = fwrite(text, 1, n, f);
    if (indent > 0) fputc('\n', f);
    int rc = (fclose(f) == 0 && put == n) ? 0 : -1;
    free(text);
    return rc;
}
