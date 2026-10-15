#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <ctype.h>
#include <dirent.h>
#include <regex.h>
#include <fnmatch.h>
#include <getopt.h>
#include <sys/stat.h>
#include <cervus_util.h>

typedef struct {
    char   *text;
    size_t  len;
    regex_t re;
    int     re_ok;
} pattern_t;

typedef struct {
    char  *line;
    size_t len;
    long   lineno;
} held_t;

enum { MODE_BASIC, MODE_EXTENDED, MODE_FIXED };

static pattern_t *g_pats;
static int        g_npats;
static int        g_mode = MODE_BASIC;
static int        g_icase, g_invert, g_word, g_xline;
static int        g_count, g_list, g_list_missing, g_quiet, g_silent;
static int        g_lineno, g_only, g_with_name = -1, g_recursive, g_deref;
static int        g_binary_text, g_binary_skip;
static long       g_max_count = -1;
static long       g_after, g_before;
static char     **g_include, **g_exclude, **g_exclude_dir;
static int        g_ninclude, g_nexclude, g_nexclude_dir;
static int        g_error;
static int        g_printed_any;
static int        g_file_has_out;
static long       g_last_out_line;
static int        g_implicit_dot;

static void *xrealloc(void *p, size_t n) {
    void *q = realloc(p, n ? n : 1);
    if (!q) { fputs("grep: out of memory\n", stderr); exit(2); }
    return q;
}

static void add_list(char ***list, int *n, const char *s) {
    *list = xrealloc(*list, sizeof(char *) * (size_t)(*n + 1));
    (*list)[(*n)++] = strdup(s);
}

static void add_pattern(const char *s, size_t len) {
    g_pats = xrealloc(g_pats, sizeof(pattern_t) * (size_t)(g_npats + 1));
    pattern_t *p = &g_pats[g_npats++];
    memset(p, 0, sizeof *p);
    p->text = xrealloc(NULL, len + 1);
    memcpy(p->text, s, len);
    p->text[len] = '\0';
    p->len = len;
}

static void add_patterns(const char *s) {
    for (;;) {
        const char *nl = strchr(s, '\n');
        if (!nl) { add_pattern(s, strlen(s)); return; }
        add_pattern(s, (size_t)(nl - s));
        s = nl + 1;
    }
}

static int add_pattern_file(const char *path) {
    FILE *f = strcmp(path, "-") == 0 ? stdin : fopen(path, "r");
    if (!f) { fprintf(stderr, "grep: %s: cannot open\n", path); return -1; }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    while ((n = getline(&line, &cap, f)) >= 0) {
        if (n > 0 && line[n - 1] == '\n') line[--n] = '\0';
        add_pattern(line, (size_t)n);
    }
    free(line);
    if (f != stdin) fclose(f);
    return 0;
}

static int compile_patterns(void) {
    if (g_mode == MODE_FIXED) return 0;
    for (int i = 0; i < g_npats; i++) {
        pattern_t *p = &g_pats[i];
        int cflags = (g_mode == MODE_EXTENDED ? REG_EXTENDED : 0) | (g_icase ? REG_ICASE : 0);
        const char *src = p->text;
        char *wrapped = NULL;
        if (g_xline) {
            size_t n = p->len + 8;
            wrapped = xrealloc(NULL, n);
            if (g_mode == MODE_EXTENDED) snprintf(wrapped, n, "^(%s)$", p->text);
            else snprintf(wrapped, n, "^\\(%s\\)$", p->text);
            src = wrapped;
        }
        int rc = regcomp(&p->re, src, cflags);
        free(wrapped);
        if (rc != 0) {
            char err[128];
            regerror(rc, &p->re, err, sizeof err);
            fprintf(stderr, "grep: %s: %s\n", p->text, err);
            return -1;
        }
        p->re_ok = 1;
    }
    return 0;
}

static int is_word_char(unsigned char c) {
    return isalnum(c) || c == '_';
}

static int word_ok(const char *line, size_t len, size_t so, size_t eo) {
    if (so > 0 && is_word_char((unsigned char)line[so - 1])) return 0;
    if (eo < len && is_word_char((unsigned char)line[eo])) return 0;
    return 1;
}

static int fixed_at(const char *s, const char *p, size_t n) {
    if (!g_icase) return memcmp(s, p, n) == 0;
    for (size_t i = 0; i < n; i++)
        if (tolower((unsigned char)s[i]) != tolower((unsigned char)p[i])) return 0;
    return 1;
}

static int find_one(pattern_t *p, const char *line, size_t len, size_t from, size_t *so, size_t *eo) {
    if (g_mode == MODE_FIXED) {
        if (g_xline) {
            if (from || p->len != len || !fixed_at(line, p->text, len)) return 0;
            *so = 0; *eo = len;
            return 1;
        }
        for (size_t i = from; i + p->len <= len; i++) {
            if (!fixed_at(line + i, p->text, p->len)) continue;
            if (g_word && !word_ok(line, len, i, i + p->len)) continue;
            *so = i; *eo = i + p->len;
            return 1;
        }
        return 0;
    }
    size_t start = from;
    while (start <= len) {
        regmatch_t m;
        int ef = start > 0 ? REG_NOTBOL : 0;
        if (regexec(&p->re, line + start, 1, &m, ef) != 0) return 0;
        size_t s = start + (size_t)m.rm_so, e = start + (size_t)m.rm_eo;
        if (!g_word || word_ok(line, len, s, e)) {
            *so = s; *eo = e;
            return 1;
        }
        start = s + 1;
    }
    return 0;
}

static int find_any(const char *line, size_t len, size_t from, size_t *so, size_t *eo) {
    int found = 0;
    size_t bs = 0, be = 0;
    for (int i = 0; i < g_npats; i++) {
        size_t s, e;
        if (!find_one(&g_pats[i], line, len, from, &s, &e)) continue;
        if (!found || s < bs || (s == bs && e > be)) { bs = s; be = e; found = 1; }
    }
    if (found) { *so = bs; *eo = be; }
    return found;
}

static char  *g_mbuf;
static size_t g_mcap;

static const char *match_view(const char *line, size_t len) {
    if (!memchr(line, '\0', len)) return line;
    if (g_mcap < len + 1) {
        g_mcap = len + 1;
        g_mbuf = xrealloc(g_mbuf, g_mcap);
    }
    for (size_t i = 0; i < len; i++) g_mbuf[i] = line[i] ? line[i] : '\n';
    g_mbuf[len] = '\0';
    return g_mbuf;
}

static void print_prefix(const char *name, long lineno, char sep) {
    if (name) { fputs(name, stdout); putchar(sep); }
    if (g_lineno) { printf("%ld", lineno); putchar(sep); }
}

static void emit_line(const char *name, const char *line, size_t len, long lineno, char sep) {
    if (g_after || g_before) {
        if (g_printed_any && (!g_file_has_out || lineno > g_last_out_line + 1))
            fputs("--\n", stdout);
        g_printed_any = 1;
        g_file_has_out = 1;
        g_last_out_line = lineno;
    }
    print_prefix(name, lineno, sep);
    fwrite(line, 1, len, stdout);
    putchar('\n');
}

static void emit_only(const char *name, const char *line, size_t len, long lineno) {
    const char *view = match_view(line, len);
    size_t from = 0;
    while (from <= len) {
        size_t so, eo;
        if (!find_any(view, len, from, &so, &eo)) break;
        if (eo > so) {
            print_prefix(name, lineno, ':');
            fwrite(line + so, 1, eo - so, stdout);
            putchar('\n');
            from = eo;
        } else {
            from = so + 1;
        }
    }
}

static int line_selected(const char *line, size_t len) {
    size_t so, eo;
    int m = find_any(match_view(line, len), len, 0, &so, &eo);
    return g_invert ? !m : m;
}

static int grep_stream(FILE *f, const char *display) {
    const char *name = g_with_name ? display : NULL;
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    long lineno = 0, selected = 0, after_left = 0;
    int binary = 0;
    held_t *ring = NULL;
    long ring_n = 0, ring_head = 0;
    if (g_before > 0) ring = calloc((size_t)g_before, sizeof(held_t));
    int stop = 0;

    g_file_has_out = 0;
    if (!g_binary_text) {
        char head[4096];
        ssize_t hn = pread(fileno(f), head, sizeof head, 0);
        if (hn > 0 && memchr(head, '\0', (size_t)hn)) {
            binary = 1;
            if (g_binary_skip) { free(ring); return 0; }
        }
    }
    while (!stop && (n = getline(&line, &cap, f)) >= 0) {
        lineno++;
        size_t len = (size_t)n;
        if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
        if (!binary && !g_binary_text && memchr(line, '\0', len)) {
            binary = 1;
            if (g_binary_skip) break;
        }
        int sel = line_selected(line, len);
        if (sel) {
            selected++;
            if (g_quiet) { stop = 1; break; }
            if (!g_count && !g_list && !g_list_missing) {
                if (binary) {
                    fflush(stdout);
                    fprintf(stderr, "grep: %s: binary file matches\n", display);
                    stop = 1;
                    break;
                }
                for (long k = 0; k < ring_n; k++) {
                    held_t *h = &ring[(ring_head + k) % g_before];
                    emit_line(name, h->line, h->len, h->lineno, '-');
                }
                ring_n = 0;
                if (g_only) {
                    if (!g_invert) emit_only(name, line, len, lineno);
                } else {
                    emit_line(name, line, len, lineno, ':');
                }
                after_left = g_after;
            }
            if (g_list) break;
            if (g_max_count >= 0 && selected >= g_max_count) {
                if (after_left == 0) break;
                stop = 2;
            }
        } else if (after_left > 0 && !g_count && !g_list && !g_list_missing && !binary) {
            emit_line(name, line, len, lineno, '-');
            after_left--;
        } else if (ring) {
            held_t *h;
            if (ring_n < g_before) {
                h = &ring[(ring_head + ring_n) % g_before];
                ring_n++;
            } else {
                h = &ring[ring_head];
                ring_head = (ring_head + 1) % g_before;
            }
            free(h->line);
            h->line = xrealloc(NULL, len + 1);
            memcpy(h->line, line, len + 1);
            h->len = len;
            h->lineno = lineno;
        }
        if (stop == 2) {
            while (after_left > 0 && (n = getline(&line, &cap, f)) >= 0) {
                lineno++;
                len = (size_t)n;
                if (len > 0 && line[len - 1] == '\n') line[--len] = '\0';
                if (line_selected(line, len)) break;
                emit_line(name, line, len, lineno, '-');
                after_left--;
            }
            break;
        }
    }
    if (ring) {
        for (long k = 0; k < g_before; k++) free(ring[k].line);
        free(ring);
    }
    free(line);

    if (g_count) {
        if (name) { fputs(name, stdout); putchar(':'); }
        printf("%ld\n", selected);
    }
    if (g_list && selected) puts(display);
    if (g_list_missing && !selected) puts(display);
    fflush(stdout);
    return selected > 0;
}

static int name_matches(char **list, int n, const char *path) {
    const char *base = strrchr(path, '/');
    base = base ? base + 1 : path;
    for (int i = 0; i < n; i++)
        if (fnmatch(list[i], base, 0) == 0) return 1;
    return 0;
}

static int grep_path(const char *path, int top);

static int grep_dir(const char *dir) {
    DIR *d = opendir(dir);
    if (!d) {
        if (!g_silent) fprintf(stderr, "grep: %s: cannot open directory\n", dir);
        g_error = 1;
        return 0;
    }
    int any = 0;
    struct dirent *de;
    while ((de = readdir(d)) != NULL) {
        const char *nm = de->d_name;
        if (nm[0] == '.' && (nm[1] == '\0' || (nm[1] == '.' && nm[2] == '\0'))) continue;
        size_t need = strlen(dir) + strlen(nm) + 2;
        char *child = xrealloc(NULL, need);
        if (g_implicit_dot && strcmp(dir, ".") == 0) snprintf(child, need, "%s", nm);
        else if (strcmp(dir, "/") == 0) snprintf(child, need, "/%s", nm);
        else snprintf(child, need, "%s/%s", dir, nm);
        any |= grep_path(child, 0);
        free(child);
        if (any && g_quiet) break;
    }
    closedir(d);
    return any;
}

static int grep_path(const char *path, int top) {
    if (strcmp(path, "-") == 0 && top) return grep_stream(stdin, "(standard input)");
    struct stat st;
    int rc = (top || g_deref) ? stat(path, &st) : lstat(path, &st);
    if (rc != 0) {
        if (!g_silent) fprintf(stderr, "grep: %s: No such file or directory\n", path);
        g_error = 1;
        return 0;
    }
    if (S_ISLNK(st.st_mode)) return 0;
    if (S_ISDIR(st.st_mode)) {
        if (!g_recursive) {
            if (!g_silent) fprintf(stderr, "grep: %s: Is a directory\n", path);
            return 0;
        }
        if (!top && name_matches(g_exclude_dir, g_nexclude_dir, path)) return 0;
        return grep_dir(path);
    }
    if (!top || g_recursive) {
        if (g_ninclude && !name_matches(g_include, g_ninclude, path)) return 0;
        if (name_matches(g_exclude, g_nexclude, path)) return 0;
    }
    FILE *f = fopen(path, "r");
    if (!f) {
        if (!g_silent) fprintf(stderr, "grep: %s: cannot open\n", path);
        g_error = 1;
        return 0;
    }
    int r = grep_stream(f, path);
    fclose(f);
    return r;
}

static const char USAGE[] =
    "Usage: grep [OPTION]... PATTERNS [FILE]...\n"
    "Search for PATTERNS in each FILE.\n\n"
    "  -E, --extended-regexp     PATTERNS are extended regular expressions\n"
    "  -F, --fixed-strings       PATTERNS are strings\n"
    "  -G, --basic-regexp        PATTERNS are basic regular expressions\n"
    "  -e, --regexp=PATTERNS     use PATTERNS for matching (repeatable)\n"
    "  -f, --file=FILE           take PATTERNS from FILE\n"
    "  -i, --ignore-case         ignore case distinctions\n"
    "  -w, --word-regexp         match only whole words\n"
    "  -x, --line-regexp         match only whole lines\n"
    "  -v, --invert-match        select non-matching lines\n"
    "  -c, --count               print only a count of selected lines per FILE\n"
    "  -l, --files-with-matches  print only names of FILEs with selected lines\n"
    "  -L, --files-without-match print only names of FILEs with no selected lines\n"
    "  -m, --max-count=NUM       stop after NUM selected lines\n"
    "  -o, --only-matching       show only the matching parts of lines\n"
    "  -q, --quiet, --silent     suppress all normal output\n"
    "  -s, --no-messages         suppress error messages\n"
    "  -n, --line-number         print line number with output lines\n"
    "  -H, --with-filename       print file name with output lines\n"
    "  -h, --no-filename         suppress the file name prefix on output\n"
    "  -A, --after-context=NUM   print NUM lines of trailing context\n"
    "  -B, --before-context=NUM  print NUM lines of leading context\n"
    "  -C, --context=NUM         print NUM lines of output context\n"
    "  -a, --text                process a binary file as if it were text\n"
    "  -I                        skip binary files\n"
    "  -r, --recursive           search directories recursively\n"
    "  -R, --dereference-recursive  likewise, following all symlinks\n"
    "      --include=GLOB        search only files that match GLOB\n"
    "      --exclude=GLOB        skip files that match GLOB\n"
    "      --exclude-dir=GLOB    skip directories that match GLOB\n";

enum { OPT_INCLUDE = 256, OPT_EXCLUDE, OPT_EXCLUDE_DIR, OPT_COLOR, OPT_IGNORED };

static long parse_num(const char *s, const char *opt) {
    char *end;
    long v = strtol(s, &end, 10);
    if (*s == '\0' || *end != '\0' || v < 0) {
        fprintf(stderr, "grep: invalid %s argument '%s'\n", opt, s);
        exit(2);
    }
    return v;
}

int main(int argc, char **argv) {
    if (cervus_check_help_version(argc, argv, USAGE, "grep")) return 0;

    static const struct option longopts[] = {
        { "extended-regexp", no_argument, 0, 'E' },
        { "fixed-strings", no_argument, 0, 'F' },
        { "basic-regexp", no_argument, 0, 'G' },
        { "regexp", required_argument, 0, 'e' },
        { "file", required_argument, 0, 'f' },
        { "ignore-case", no_argument, 0, 'i' },
        { "word-regexp", no_argument, 0, 'w' },
        { "line-regexp", no_argument, 0, 'x' },
        { "invert-match", no_argument, 0, 'v' },
        { "count", no_argument, 0, 'c' },
        { "files-with-matches", no_argument, 0, 'l' },
        { "files-without-match", no_argument, 0, 'L' },
        { "max-count", required_argument, 0, 'm' },
        { "only-matching", no_argument, 0, 'o' },
        { "quiet", no_argument, 0, 'q' },
        { "silent", no_argument, 0, 'q' },
        { "no-messages", no_argument, 0, 's' },
        { "line-number", no_argument, 0, 'n' },
        { "with-filename", no_argument, 0, 'H' },
        { "no-filename", no_argument, 0, 'h' },
        { "after-context", required_argument, 0, 'A' },
        { "before-context", required_argument, 0, 'B' },
        { "context", required_argument, 0, 'C' },
        { "text", no_argument, 0, 'a' },
        { "recursive", no_argument, 0, 'r' },
        { "dereference-recursive", no_argument, 0, 'R' },
        { "include", required_argument, 0, OPT_INCLUDE },
        { "exclude", required_argument, 0, OPT_EXCLUDE },
        { "exclude-dir", required_argument, 0, OPT_EXCLUDE_DIR },
        { "color", optional_argument, 0, OPT_COLOR },
        { "colour", optional_argument, 0, OPT_COLOR },
        { "line-buffered", no_argument, 0, OPT_IGNORED },
        { 0, 0, 0, 0 }
    };

    int have_pat = 0;
    int opt;
    while ((opt = getopt_long(argc, argv, "EFGe:f:iywxvclLm:oqsnHhA:B:C:aIrR", longopts, NULL)) != -1) {
        switch (opt) {
        case 'E': g_mode = MODE_EXTENDED; break;
        case 'F': g_mode = MODE_FIXED; break;
        case 'G': g_mode = MODE_BASIC; break;
        case 'e': add_patterns(optarg); have_pat = 1; break;
        case 'f': if (add_pattern_file(optarg) < 0) return 2; have_pat = 1; break;
        case 'i': case 'y': g_icase = 1; break;
        case 'w': g_word = 1; break;
        case 'x': g_xline = 1; break;
        case 'v': g_invert = 1; break;
        case 'c': g_count = 1; break;
        case 'l': g_list = 1; break;
        case 'L': g_list_missing = 1; break;
        case 'm': g_max_count = parse_num(optarg, "max count"); break;
        case 'o': g_only = 1; break;
        case 'q': g_quiet = 1; break;
        case 's': g_silent = 1; break;
        case 'n': g_lineno = 1; break;
        case 'H': g_with_name = 1; break;
        case 'h': g_with_name = 0; break;
        case 'A': g_after = parse_num(optarg, "context length"); break;
        case 'B': g_before = parse_num(optarg, "context length"); break;
        case 'C': g_after = g_before = parse_num(optarg, "context length"); break;
        case 'a': g_binary_text = 1; break;
        case 'I': g_binary_skip = 1; break;
        case 'r': g_recursive = 1; break;
        case 'R': g_recursive = 1; g_deref = 1; break;
        case OPT_INCLUDE: add_list(&g_include, &g_ninclude, optarg); break;
        case OPT_EXCLUDE: add_list(&g_exclude, &g_nexclude, optarg); break;
        case OPT_EXCLUDE_DIR: add_list(&g_exclude_dir, &g_nexclude_dir, optarg); break;
        case OPT_COLOR: case OPT_IGNORED: break;
        default:
            fputs(USAGE, stderr);
            return 2;
        }
    }

    if (!have_pat) {
        if (optind >= argc) { fputs(USAGE, stderr); return 2; }
        add_patterns(argv[optind++]);
    }
    if (g_only && (g_after || g_before)) g_after = g_before = 0;
    if (compile_patterns() < 0) return 2;

    int nfiles = argc - optind;
    if (g_with_name < 0) g_with_name = nfiles > 1 || g_recursive;

    int any = 0;
    if (g_max_count == 0) {
        any = 0;
    } else if (nfiles == 0) {
        if (g_recursive) { g_implicit_dot = 1; any = grep_dir("."); }
        else any = grep_stream(stdin, "(standard input)");
    } else {
        for (int i = optind; i < argc; i++) {
            any |= grep_path(argv[i], 1);
            if (any && g_quiet) break;
        }
    }

    for (int i = 0; i < g_npats; i++) {
        if (g_pats[i].re_ok) regfree(&g_pats[i].re);
        free(g_pats[i].text);
    }
    free(g_pats);
    if (any && g_quiet) return 0;
    if (g_error) return 2;
    return any ? 0 : 1;
}
