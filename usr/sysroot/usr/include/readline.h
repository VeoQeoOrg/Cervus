#ifndef _READLINE_H
#define _READLINE_H

#define RL_MAX_COMPLETIONS 256

typedef struct {
    char items[RL_MAX_COMPLETIONS][128];
    int  is_dir[RL_MAX_COMPLETIONS];
    int  count;
    int  word_start;
} rl_completions_t;

typedef void (*rl_complete_fn)(const char *buf, int pos, rl_completions_t *out);
typedef int  (*rl_suggest_fn)(const char *buf, int len, const char **out);

char *readline(const char *prompt);

void  readline_add_history(const char *line);
void  readline_set_history_file(const char *path);
void  readline_clear_history(void);
int   readline_history_count(void);
const char *readline_history_get(int n);

void  readline_set_completion(rl_complete_fn cb);
void  readline_set_suggest(rl_suggest_fn cb);
void  readline_set_input_color(const char *seq);

#endif
