#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <readline.h>
#include "darwin.h"

static char *d_rl_readline_name;
static void *d_rl_instream;
static void *d_rl_outstream;
static char *d_rl_line_buffer;
static int d_history_length;
static int d_rl_point;
static int d_rl_end;
static void *d_rl_completion_entry_function;
static void *d_rl_attempted_completion_function;

DSYM("_rl_readline_name", &d_rl_readline_name);
DSYM("_rl_instream", &d_rl_instream);
DSYM("_rl_outstream", &d_rl_outstream);
DSYM("_rl_line_buffer", &d_rl_line_buffer);
DSYM("_history_length", &d_history_length);
DSYM("_rl_point", &d_rl_point);
DSYM("_rl_end", &d_rl_end);
DSYM("_rl_completion_entry_function", &d_rl_completion_entry_function);
DSYM("_rl_attempted_completion_function", &d_rl_attempted_completion_function);

static char *d_readline(const char *prompt)
{
    fflush(stdout);
    return readline(prompt ? prompt : "");
}

static int d_add_history(const char *line)
{
    if (line && *line) {
        readline_add_history(line);
        d_history_length++;
    }
    return 0;
}

static void d_using_history(void) {}
static int d_zero_path(const char *p) { (void)p; return 0; }
static void d_clear_history(void) { readline_clear_history(); d_history_length = 0; }
static int d_stifle_history(int n) { (void)n; return 0; }
static int d_rl_initialize(void) { return 0; }
static int d_rl_bind_key(int k, void *f) { (void)k; (void)f; return 0; }
static void d_rl_reset_terminal(const char *t) { (void)t; }

DSYM("_readline", d_readline);
DSYM("_add_history", d_add_history);
DSYM("_using_history", d_using_history);
DSYM("_read_history", d_zero_path);
DSYM("_write_history", d_zero_path);
DSYM("_clear_history", d_clear_history);
DSYM("_stifle_history", d_stifle_history);
DSYM("_rl_initialize", d_rl_initialize);
DSYM("_rl_bind_key", d_rl_bind_key);
DSYM("_rl_reset_terminal", d_rl_reset_terminal);

#define EL_PROMPT     0
#define EL_PROMPT_ESC 21
#define H_ENTER       10

struct editline {
    char *(*prompt)(struct editline *);
    char *line;
    void *client;
};

static struct editline *d_el_init(const char *prog, void *in, void *out, void *err)
{
    (void)prog;
    (void)in;
    (void)out;
    (void)err;
    return calloc(1, sizeof(struct editline));
}

static void d_el_end(struct editline *e)
{
    if (e) {
        free(e->line);
        free(e);
    }
}

static int d_el_set(struct editline *e, int op, ...)
{
    va_list ap;
    va_start(ap, op);
    if (op == EL_PROMPT || op == EL_PROMPT_ESC)
        e->prompt = va_arg(ap, char *(*)(struct editline *));
    va_end(ap);
    return 0;
}

static int d_el_get(struct editline *e, int op, ...)
{
    va_list ap;
    va_start(ap, op);
    if (op == EL_PROMPT) {
        char *(**out)(struct editline *) = va_arg(ap, char *(**)(struct editline *));
        *out = e->prompt;
    }
    va_end(ap);
    return 0;
}

static const char *d_el_gets(struct editline *e, int *count)
{
    const char *p = e->prompt ? e->prompt(e) : "";
    free(e->line);
    e->line = NULL;
    char *l = d_readline(p);
    if (!l) {
        if (count)
            *count = 0;
        return NULL;
    }
    size_t n = strlen(l);
    e->line = malloc(n + 2);
    if (!e->line) {
        free(l);
        return NULL;
    }
    memcpy(e->line, l, n);
    e->line[n] = '\n';
    e->line[n + 1] = 0;
    free(l);
    if (count)
        *count = (int)n + 1;
    return e->line;
}

static int d_el_source(struct editline *e, const char *f) { (void)e; (void)f; return 0; }
static void d_el_reset(struct editline *e) { (void)e; }
static int d_el_parse(struct editline *e, int c, const char **v) { (void)e; (void)c; (void)v; return 0; }

static void *d_history_init(void) { return calloc(1, 16); }
static void d_history_end(void *h) { free(h); }

static int d_history(void *h, void *ev, int op, ...)
{
    (void)h;
    (void)ev;
    va_list ap;
    va_start(ap, op);
    if (op == H_ENTER)
        d_add_history(va_arg(ap, const char *));
    va_end(ap);
    return 0;
}

DSYM("_el_init", d_el_init);
DSYM("_el_end", d_el_end);
DSYM("_el_set", d_el_set);
DSYM("_el_get", d_el_get);
DSYM("_el_gets", d_el_gets);
DSYM("_el_source", d_el_source);
DSYM("_el_reset", d_el_reset);
DSYM("_el_parse", d_el_parse);
DSYM("_history_init", d_history_init);
DSYM("_history_end", d_history_end);
DSYM("_history", d_history);
