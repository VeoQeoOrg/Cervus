#include <getopt.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern int __cervus_optpos;

static const char *prog_name(char *const argv[])
{
    return argv[0] ? argv[0] : "?";
}

static int is_option(const char *s)
{
    return s && s[0] == '-' && s[1] != '\0';
}

static const struct option *find_long(const struct option *longopts, const char *name,
                                      size_t len, int *index, int *ambiguous)
{
    const struct option *hit = NULL;
    *ambiguous = 0;
    for (int i = 0; longopts && longopts[i].name; i++) {
        if (strncmp(longopts[i].name, name, len) != 0) continue;
        if (strlen(longopts[i].name) == len) {
            *index = i;
            *ambiguous = 0;
            return &longopts[i];
        }
        if (hit && (hit->has_arg != longopts[i].has_arg || hit->flag != longopts[i].flag ||
                    hit->val != longopts[i].val))
            *ambiguous = 1;
        if (!hit) {
            hit = &longopts[i];
            *index = i;
        }
    }
    return *ambiguous ? NULL : hit;
}

static int short_takes_arg(const char *opts, char c)
{
    const char *p = strchr(opts, c);
    if (!p || c == ':') return 0;
    if (p[1] != ':') return 0;
    return p[2] == ':' ? 2 : 1;
}

static int consumes_next(const char *arg, const char *opts, const struct option *longopts,
                         int long_only)
{
    if (strcmp(arg, "--") == 0) return 0;
    const char *name = NULL;
    if (arg[1] == '-') name = arg + 2;
    else if (long_only && arg[2] != '\0') name = arg + 1;
    if (name) {
        size_t len = strcspn(name, "=");
        int idx, amb;
        const struct option *o = find_long(longopts, name, len, &idx, &amb);
        if (o) return name[len] == '\0' && o->has_arg == required_argument;
        if (arg[1] == '-') return 0;
    }
    for (const char *c = arg + 1; *c; c++) {
        int kind = short_takes_arg(opts, *c);
        if (kind == 1) return c[1] == '\0';
        if (kind == 2) return 0;
    }
    return 0;
}

static void rotate_to(char **argv, int to, int from, int count)
{
    for (int k = 0; k < count; k++) {
        char *moved = argv[from + k];
        for (int i = from + k; i > to + k; i--) argv[i] = argv[i - 1];
        argv[to + k] = moved;
    }
}

static int parse_long(int argc, char *const argv[], const char *name, const char *opts,
                      const struct option *longopts, int *longindex, int colon_mode, int dashes)
{
    size_t len = strcspn(name, "=");
    int idx = 0, amb = 0;
    const struct option *o = find_long(longopts, name, len, &idx, &amb);
    const char *dash = dashes == 2 ? "--" : "-";
    optind++;
    if (!o) {
        if (opterr && !colon_mode) {
            if (amb)
                fprintf(stderr, "%s: option '%s%.*s' is ambiguous\n", prog_name(argv), dash,
                        (int)len, name);
            else
                fprintf(stderr, "%s: unrecognized option '%s%.*s'\n", prog_name(argv), dash,
                        (int)len, name);
        }
        optopt = 0;
        return '?';
    }
    if (longindex) *longindex = idx;
    if (name[len] == '=') {
        if (o->has_arg == no_argument) {
            if (opterr && !colon_mode)
                fprintf(stderr, "%s: option '%s%s' doesn't allow an argument\n",
                        prog_name(argv), dash, o->name);
            optopt = o->flag ? 0 : o->val;
            return '?';
        }
        optarg = (char *)name + len + 1;
    } else if (o->has_arg == required_argument) {
        if (optind >= argc) {
            if (opterr && !colon_mode)
                fprintf(stderr, "%s: option '%s%s' requires an argument\n",
                        prog_name(argv), dash, o->name);
            optopt = o->flag ? 0 : o->val;
            return colon_mode ? ':' : '?';
        }
        optarg = argv[optind++];
    }
    (void)opts;
    if (o->flag) {
        *o->flag = o->val;
        return 0;
    }
    return o->val;
}

static int getopt_long_impl(int argc, char *const argv[], const char *optstring,
                            const struct option *longopts, int *longindex, int long_only)
{
    if (!optstring) optstring = "";
    if (optind == 0) {
        optind = 1;
        __cervus_optpos = 1;
    }
    int permute = 1;
    if (*optstring == '+' || *optstring == '-') {
        permute = 0;
        optstring++;
    } else if (getenv("POSIXLY_CORRECT")) {
        permute = 0;
    }
    int colon_mode = *optstring == ':';
    optarg = NULL;

    if (__cervus_optpos > 1) return getopt(argc, argv, optstring);
    if (optind >= argc) return -1;

    if (!is_option(argv[optind])) {
        if (!permute) return -1;
        int j = optind + 1;
        while (j < argc && !is_option(argv[j])) j++;
        if (j >= argc) return -1;
        int count = 1;
        if (j + 1 < argc && consumes_next(argv[j], optstring, longopts, long_only)) count = 2;
        rotate_to((char **)argv, optind, j, count);
    }

    const char *cur = argv[optind];
    if (strcmp(cur, "--") == 0) {
        optind++;
        return -1;
    }
    if (cur[1] == '-')
        return parse_long(argc, argv, cur + 2, optstring, longopts, longindex, colon_mode, 2);
    if (long_only) {
        size_t len = strcspn(cur + 1, "=");
        int idx, amb;
        int single_short = len == 1 && strchr(optstring, cur[1]) != NULL;
        if (!single_short && (find_long(longopts, cur + 1, len, &idx, &amb) || amb))
            return parse_long(argc, argv, cur + 1, optstring, longopts, longindex, colon_mode, 1);
    }
    return getopt(argc, argv, optstring);
}

int getopt_long(int argc, char *const argv[], const char *optstring,
                const struct option *longopts, int *longindex)
{
    return getopt_long_impl(argc, argv, optstring, longopts, longindex, 0);
}

int getopt_long_only(int argc, char *const argv[], const char *optstring,
                     const struct option *longopts, int *longindex)
{
    return getopt_long_impl(argc, argv, optstring, longopts, longindex, 1);
}
