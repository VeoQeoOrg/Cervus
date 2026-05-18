#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cervus_util.h>

typedef struct {
    int number_all;
    int number_nonblank;
    int squeeze_blank;
    int show_ends;
    int show_tabs;
    int show_nonprinting;
} cat_opts_t;

static void emit(const char *buf, ssize_t n, const cat_opts_t *o,
                 int *line_no, int *blank_run, int *at_line_start)
{
    char out[8192];
    size_t oi = 0;
    for (ssize_t i = 0; i < n; i++) {
        unsigned char c = (unsigned char)buf[i];

        if (*at_line_start) {
            if (o->squeeze_blank) {
                if (c == '\n') {
                    if (*blank_run >= 1) continue;
                    (*blank_run)++;
                } else *blank_run = 0;
            }
            if (o->number_all || (o->number_nonblank && c != '\n')) {
                char nb[16];
                int nbn = snprintf(nb, sizeof(nb), "%6d\t", ++(*line_no));
                if (oi + (size_t)nbn >= sizeof(out)) { write(1, out, oi); oi = 0; }
                memcpy(out + oi, nb, nbn); oi += nbn;
            }
            *at_line_start = 0;
        }

        if (o->show_tabs && c == '\t') {
            if (oi + 2 >= sizeof(out)) { write(1, out, oi); oi = 0; }
            out[oi++] = '^'; out[oi++] = 'I';
        } else if (o->show_nonprinting && (c < 32 && c != '\t' && c != '\n')) {
            if (oi + 2 >= sizeof(out)) { write(1, out, oi); oi = 0; }
            out[oi++] = '^'; out[oi++] = (char)(c + 64);
        } else if (o->show_nonprinting && c == 127) {
            if (oi + 2 >= sizeof(out)) { write(1, out, oi); oi = 0; }
            out[oi++] = '^'; out[oi++] = '?';
        } else if (o->show_nonprinting && c >= 128) {
            if (oi + 4 >= sizeof(out)) { write(1, out, oi); oi = 0; }
            out[oi++] = 'M'; out[oi++] = '-';
            unsigned char x = c & 0x7F;
            if (x < 32) { out[oi++] = '^'; out[oi++] = (char)(x + 64); }
            else if (x == 127) { out[oi++] = '^'; out[oi++] = '?'; }
            else { out[oi++] = (char)x; }
        } else {
            if (oi + 1 >= sizeof(out)) { write(1, out, oi); oi = 0; }
            out[oi++] = (char)c;
        }

        if (c == '\n') {
            if (o->show_ends) {
                if (oi + 2 >= sizeof(out)) { write(1, out, oi); oi = 0; }
                out[oi - 1] = '$'; out[oi++] = '\n';
            }
            *at_line_start = 1;
        }
    }
    if (oi > 0) write(1, out, oi);
}

static int cat_fd(int fd, const cat_opts_t *o, int *line_no,
                  int *blank_run, int *at_line_start)
{
    int has_opts = o->number_all || o->number_nonblank || o->squeeze_blank ||
                   o->show_ends || o->show_tabs || o->show_nonprinting;
    char buf[4096];
    ssize_t n;
    while ((n = read(fd, buf, sizeof(buf))) > 0) {
        if (has_opts) emit(buf, n, o, line_no, blank_run, at_line_start);
        else          write(1, buf, (size_t)n);
    }
    return n < 0 ? 1 : 0;
}

static const char USAGE[] =
    "Usage: cat [-AbeEnstTuv] [file ...]\nConcatenate FILE(s) to standard output.\nWith no FILE, or - as FILE, read standard input.\n\n  -A   equivalent to -vET\n  -b   number nonempty output lines\n  -e   equivalent to -vE\n  -E   display $ at end of each line\n  -n   number all output lines\n  -s   suppress repeated empty lines\n  -t   equivalent to -vT\n  -T   display TAB characters as ^I\n  -v   display non-printing characters\n";

static void usage(void) { fputs(USAGE, stderr); }

int main(int argc, char **argv)
{
    if (cervus_check_help_version(argc, argv, USAGE, "cat")) return 0;
    const char *cwd = get_cwd_flag(argc, argv);
    argc = cervus_filter_args(argc, argv);

    cat_opts_t o;
    memset(&o, 0, sizeof(o));

    int opt;
    while ((opt = getopt(argc, argv, "AbeEnstTuv")) != -1) {
        switch (opt) {
            case 'A': o.show_ends = 1; o.show_tabs = 1; o.show_nonprinting = 1; break;
            case 'b': o.number_nonblank = 1; o.number_all = 0; break;
            case 'e': o.show_ends = 1; o.show_nonprinting = 1; break;
            case 'E': o.show_ends = 1; break;
            case 'n': if (!o.number_nonblank) o.number_all = 1; break;
            case 's': o.squeeze_blank = 1; break;
            case 't': o.show_tabs = 1; o.show_nonprinting = 1; break;
            case 'T': o.show_tabs = 1; break;
            case 'u': break;
            case 'v': o.show_nonprinting = 1; break;
            default:  usage(); return 1;
        }
    }

    int line_no = 0, blank_run = 0, at_line_start = 1;
    int rc = 0;

    if (optind >= argc) return cat_fd(0, &o, &line_no, &blank_run, &at_line_start);

    for (int i = optind; i < argc; i++) {
        if (strcmp(argv[i], "-") == 0) {
            if (cat_fd(0, &o, &line_no, &blank_run, &at_line_start) != 0) rc = 1;
            continue;
        }
        char resolved[512];
        resolve_path(cwd, argv[i], resolved, sizeof(resolved));
        struct stat st;
        if (stat(resolved, &st) == 0 && st.st_type == 1) {
            fprintf(stderr, "cat: %s: Is a directory\n", argv[i]);
            rc = 1; continue;
        }
        int fd = open(resolved, O_RDONLY);
        if (fd < 0) {
            fprintf(stderr, "cat: cannot open: %s\n", argv[i]);
            rc = 1; continue;
        }
        if (cat_fd(fd, &o, &line_no, &blank_run, &at_line_start) != 0) rc = 1;
        close(fd);
    }
    return rc;
}
