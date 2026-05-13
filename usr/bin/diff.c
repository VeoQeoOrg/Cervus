#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <cervus_util.h>

#define DIFF_MAX_LINES 4096
#define DIFF_MAX_FSIZE (1 << 20)
#define DIFF_LOOKAHEAD 64

typedef struct {
    char  *data;
    size_t size;
    char  **lines;
    int    nlines;
    int    cap;
} diff_file_t;

static int load_file(const char *path, diff_file_t *out)
{
    int fd = open(path, O_RDONLY, 0);
    if (fd < 0) {
        fputs(C_RED "diff: cannot open: " C_RESET, stdout);
        fputs(path, stdout); putchar('\n');
        return -1;
    }
    struct stat st;
    if (fstat(fd, &st) < 0) {
        close(fd);
        fputs(C_RED "diff: stat failed: " C_RESET, stdout);
        fputs(path, stdout); putchar('\n');
        return -1;
    }
    if ((size_t)st.st_size > DIFF_MAX_FSIZE) {
        close(fd);
        fputs(C_RED "diff: file too large: " C_RESET, stdout);
        fputs(path, stdout); putchar('\n');
        return -1;
    }
    size_t sz = (size_t)st.st_size;
    char *buf = (char *)malloc(sz + 1);
    if (!buf) {
        close(fd);
        fputs(C_RED "diff: out of memory\n" C_RESET, stdout);
        return -1;
    }
    size_t off = 0;
    while (off < sz) {
        ssize_t n = read(fd, buf + off, sz - off);
        if (n <= 0) break;
        off += (size_t)n;
    }
    close(fd);
    buf[off] = '\0';

    out->data = buf;
    out->size = off;
    out->nlines = 0;
    out->cap = 128;
    out->lines = (char **)malloc(sizeof(char *) * (size_t)out->cap);
    if (!out->lines) {
        fputs(C_RED "diff: out of memory\n" C_RESET, stdout);
        return -1;
    }

    char *p = buf;
    char *end = buf + off;
    while (p < end) {
        if (out->nlines >= DIFF_MAX_LINES) {
            fputs(C_RED "diff: too many lines: " C_RESET, stdout);
            fputs(path, stdout); putchar('\n');
            return -1;
        }
        if (out->nlines >= out->cap) {
            int new_cap = out->cap * 2;
            if (new_cap > DIFF_MAX_LINES) new_cap = DIFF_MAX_LINES;
            char **nl = (char **)malloc(sizeof(char *) * (size_t)new_cap);
            if (!nl) {
                fputs(C_RED "diff: out of memory\n" C_RESET, stdout);
                return -1;
            }
            memcpy(nl, out->lines, sizeof(char *) * (size_t)out->nlines);
            free(out->lines);
            out->lines = nl;
            out->cap = new_cap;
        }
        out->lines[out->nlines++] = p;
        while (p < end && *p != '\n') p++;
        if (p < end) { *p = '\0'; p++; }
    }
    return 0;
}

static void free_file(diff_file_t *f) {
    if (f->data) { free(f->data); f->data = NULL; }
    if (f->lines) { free(f->lines); f->lines = NULL; }
}

static void print_block(const char *prefix, char *const *lines, int from, int to) {
    for (int i = from; i < to; i++) {
        fputs(prefix, stdout);
        fputs(lines[i], stdout);
        putchar('\n');
    }
}

static void print_hunk_header(int a_from, int a_to, int b_from, int b_to) {
    int a_count = a_to - a_from;
    int b_count = b_to - b_from;
    char op;
    if (a_count == 0)      op = 'a';
    else if (b_count == 0) op = 'd';
    else                   op = 'c';

    char left[32], right[32];
    if (a_count <= 1) snprintf(left, sizeof(left), "%d", a_from + (a_count == 0 ? 0 : 1));
    else              snprintf(left, sizeof(left), "%d,%d", a_from + 1, a_to);
    if (b_count <= 1) snprintf(right, sizeof(right), "%d", b_from + (b_count == 0 ? 0 : 1));
    else              snprintf(right, sizeof(right), "%d,%d", b_from + 1, b_to);

    fputs(C_BOLD, stdout);
    fputs(left, stdout);
    putchar(op);
    fputs(right, stdout);
    fputs(C_RESET "\n", stdout);
}

static void emit_diff_chunk(diff_file_t *A, int a_from, int a_to,
                            diff_file_t *B, int b_from, int b_to)
{
    if (a_from == a_to && b_from == b_to) return;
    print_hunk_header(a_from, a_to, b_from, b_to);
    if (a_to > a_from) {
        fputs(C_RED, stdout);
        print_block("< ", A->lines, a_from, a_to);
        fputs(C_RESET, stdout);
    }
    if (a_to > a_from && b_to > b_from) {
        fputs(C_GRAY "---" C_RESET "\n", stdout);
    }
    if (b_to > b_from) {
        fputs(C_GREEN, stdout);
        print_block("> ", B->lines, b_from, b_to);
        fputs(C_RESET, stdout);
    }
}

static int run_diff(diff_file_t *A, diff_file_t *B) {
    int i = 0, j = 0;
    int diffs = 0;

    while (i < A->nlines || j < B->nlines) {
        if (i < A->nlines && j < B->nlines &&
            strcmp(A->lines[i], B->lines[j]) == 0) {
            i++; j++; continue;
        }

        int best_di = -1, best_dj = -1;
        for (int sum = 1; sum <= DIFF_LOOKAHEAD; sum++) {
            for (int di = 0; di <= sum && best_di < 0; di++) {
                int dj = sum - di;
                if (i + di > A->nlines || j + dj > B->nlines) continue;
                if (i + di == A->nlines && j + dj == B->nlines) {
                    best_di = di; best_dj = dj; break;
                }
                if (i + di < A->nlines && j + dj < B->nlines &&
                    strcmp(A->lines[i + di], B->lines[j + dj]) == 0) {
                    best_di = di; best_dj = dj; break;
                }
            }
            if (best_di >= 0) break;
        }

        if (best_di < 0) {
            emit_diff_chunk(A, i, A->nlines, B, j, B->nlines);
            diffs++;
            i = A->nlines; j = B->nlines;
            break;
        }

        emit_diff_chunk(A, i, i + best_di, B, j, j + best_dj);
        diffs++;
        i += best_di; j += best_dj;
    }

    return diffs ? 1 : 0;
}

static void usage(void) {
    fputs(C_RED "usage: diff <file1> <file2>\n" C_RESET, stdout);
}

int main(int argc, char **argv) {
    const char *cwd = get_cwd_flag(argc, argv);

    const char *p1 = NULL, *p2 = NULL;
    for (int i = 1; i < argc; i++) {
        if (is_shell_flag(argv[i])) continue;
        if (!p1) p1 = argv[i];
        else if (!p2) p2 = argv[i];
    }
    if (!p1 || !p2) { usage(); return 2; }

    char r1[1024], r2[1024];
    resolve_path(cwd, p1, r1, sizeof(r1));
    resolve_path(cwd, p2, r2, sizeof(r2));

    diff_file_t A = {0}, B = {0};
    if (load_file(r1, &A) < 0) { free_file(&A); return 2; }
    if (load_file(r2, &B) < 0) { free_file(&A); free_file(&B); return 2; }

    int rc = run_diff(&A, &B);

    free_file(&A);
    free_file(&B);
    return rc;
}
