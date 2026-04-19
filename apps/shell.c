#include "../apps/cervus_user.h"

#ifndef VFS_MAX_PATH
#define VFS_MAX_PATH 1024
#endif

#define TIOCGWINSZ  0x5413
#define TIOCGCURSOR 0x5480

#define HIST_MAX 1024
#define LINE_MAX 1024
#define MAX_ARGS 32

#define ENV_MAX_VARS 128
#define ENV_NAME_MAX  64
#define ENV_VAL_MAX  512

#define MAX_ENTRIES     128
#define MAX_SEGMENTS    64
#define MAX_COMPLETIONS 64

#define MBR_TYPE_FAT32_LBA  0x0C
#define MBR_TYPE_LINUX      0x83
#define MBR_TYPE_LINUX_SWAP 0x82

typedef struct { uint16_t ws_row, ws_col, ws_xpixel, ws_ypixel; } cervus_winsize_t;
typedef struct { uint32_t row, col; } cervus_cursor_pos_t;

static inline int ioctl(int fd, unsigned long req, void *arg)
{
	return (int)syscall3(SYS_IOCTL, fd, req, arg);
}

static void safe_strcpy(char *dst, size_t dsz, const char *src)
{
	if (!dst || dsz == 0) return;
	if (!src) { dst[0] = '\0'; return; }
	size_t i = 0;
	while (i + 1 < dsz && src[i]) { dst[i] = src[i]; i++; }
	dst[i] = '\0';
}

static int g_cols = 80;
static int g_rows = 25;

static void term_update_size(void)
{
	cervus_winsize_t ws;
	if (ioctl(1, TIOCGWINSZ, &ws) == 0 && ws.ws_col >= 8 && ws.ws_row >= 2) {
		g_cols = (int)ws.ws_col;
		g_rows = (int)ws.ws_row;
	}
}

static int term_get_cursor_row(void)
{
	cervus_cursor_pos_t cp;
	if (ioctl(1, TIOCGCURSOR, &cp) == 0) return (int)cp.row;
	return 0;
}

static void vt_goto(int row, int col)
{
	char b[24];
	snprintf(b, sizeof(b), "\x1b[%d;%dH", row + 1, col + 1);
	ws(b);
}

static void vt_eol(void) { ws("\x1b[K"); }

static char history[HIST_MAX][LINE_MAX];
static int  hist_count = 0, hist_head = 0;
static const char *g_hist_file = NULL;

static void hist_store(int idx, const char *s)
{
	memset(history[idx], 0, LINE_MAX);
	safe_strcpy(history[idx], LINE_MAX, s);
}

static void hist_load(const char *path)
{
	int fd = open(path, O_RDONLY, 0);
	if (fd < 0) return;
	char line[LINE_MAX];
	int li = 0;
	char ch;
	while (read(fd, &ch, 1) > 0) {
		if (ch == '\n' || li >= LINE_MAX - 1) {
			line[li] = '\0';
			if (li > 0) {
				int idx = (hist_head + hist_count) % HIST_MAX;
				hist_store(idx, line);
				if (hist_count < HIST_MAX) hist_count++;
				else hist_head = (hist_head + 1) % HIST_MAX;
			}
			li = 0;
		} else {
			line[li++] = ch;
		}
	}
	close(fd);
}

static void hist_save_entry(const char *path, const char *l)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_APPEND, 0600);
	if (fd < 0) return;
	size_t n = strlen(l);
	write(fd, l, n);
	write(fd, "\n", 1);
	close(fd);
}

static void hist_push(const char *l)
{
	if (!l[0]) return;
	if (hist_count > 0) {
		int last = (hist_head + hist_count - 1) % HIST_MAX;
		if (strcmp(history[last], l) == 0) return;
	}
	int idx = (hist_head + hist_count) % HIST_MAX;
	hist_store(idx, l);
	if (hist_count < HIST_MAX) hist_count++;
	else hist_head = (hist_head + 1) % HIST_MAX;
	if (g_hist_file) hist_save_entry(g_hist_file, l);
}

static const char *hist_get(int n)
{
	if (n < 1 || n > hist_count) return NULL;
	return history[(hist_head + hist_count - n) % HIST_MAX];
}

typedef struct { char name[ENV_NAME_MAX]; char value[ENV_VAL_MAX]; } env_var_t;

static env_var_t g_env[ENV_MAX_VARS];
static int       g_env_count = 0;

static int env_find(const char *name)
{
	for (int i = 0; i < g_env_count; i++)
		if (strcmp(g_env[i].name, name) == 0) return i;
	return -1;
}

static const char *env_get(const char *name)
{
	int i = env_find(name);
	return i >= 0 ? g_env[i].value : "";
}

static void env_set(const char *name, const char *value)
{
	int i = env_find(name);
	if (i >= 0) {
		safe_strcpy(g_env[i].value, ENV_VAL_MAX, value);
		return;
	}
	if (g_env_count >= ENV_MAX_VARS) return;
	safe_strcpy(g_env[g_env_count].name,  ENV_NAME_MAX, name);
	safe_strcpy(g_env[g_env_count].value, ENV_VAL_MAX,  value);
	g_env_count++;
}

static void env_unset(const char *name)
{
	int i = env_find(name);
	if (i < 0) return;
	g_env[i] = g_env[--g_env_count];
}

static int g_last_rc = 0;

static void expand_vars(const char *src, char *dst, size_t dsz)
{
	size_t di = 0;
	int in_squote = 0;
	int in_dquote = 0;
	for (const char *p = src; *p && di + 1 < dsz; ) {
		char c = *p;
		if (!in_squote && c == '\\' && p[1]) {
			dst[di++] = *p++;
			if (di + 1 < dsz) dst[di++] = *p++;
			continue;
		}
		if (!in_dquote && c == '\'') {
			in_squote = !in_squote;
			dst[di++] = *p++;
			continue;
		}
		if (!in_squote && c == '"') {
			in_dquote = !in_dquote;
			dst[di++] = *p++;
			continue;
		}
		if (in_squote || c != '$') {
			dst[di++] = *p++;
			continue;
		}
		p++;
		if (*p == '?') {
			char tmp[12];
			int len = snprintf(tmp, sizeof(tmp), "%d", g_last_rc);
			for (int x = 0; x < len && di + 1 < dsz; x++) dst[di++] = tmp[x];
			p++;
			continue;
		}
		int braced = (*p == '{');
		if (braced) p++;
		char name[ENV_NAME_MAX];
		int ni = 0;
		while (*p && ni + 1 < (int)ENV_NAME_MAX) {
			char nc = *p;
			if (braced) { if (nc == '}') { p++; break; } }
			else if (!isalnum((unsigned char)nc) && nc != '_') break;
			name[ni++] = nc;
			p++;
		}
		name[ni] = '\0';
		if (ni == 0) { dst[di++] = '$'; continue; }
		const char *val = env_get(name);
		for (; *val && di + 1 < dsz; val++) dst[di++] = *val;
	}
	dst[di] = '\0';
}

static char cwd[VFS_MAX_PATH];
static int  prompt_len = 0;

static const char *display_path(void)
{
	static char dpbuf[VFS_MAX_PATH];
	const char *home = env_get("HOME");
	size_t hlen = home ? strlen(home) : 0;
	if (hlen > 1 && strncmp(cwd, home, hlen) == 0 &&
	    (cwd[hlen] == '/' || cwd[hlen] == '\0')) {
		dpbuf[0] = '~';
		safe_strcpy(dpbuf + 1, sizeof(dpbuf) - 1, cwd + hlen);
		if (dpbuf[1] == '\0') { dpbuf[0] = '~'; dpbuf[1] = '\0'; }
		return dpbuf;
	}
	return cwd;
}

static void print_prompt(void)
{
	const char *dp = display_path();
	ws(C_GREEN "cervus" C_RESET ":" C_BLUE);
	ws(dp);
	ws(C_RESET "$ ");
	prompt_len = 9 + (int)strlen(dp);
}

static int g_start_row = 0;

static void sync_start_row(int cur_logical_pos)
{
	int real_row = term_get_cursor_row();
	int row_offset = (prompt_len + cur_logical_pos) / g_cols;
	g_start_row = real_row - row_offset;
	if (g_start_row < 0) g_start_row = 0;
}

static void input_pos_to_screen(int pos, int *row, int *col)
{
	int abs = prompt_len + pos;
	*row = g_start_row + abs / g_cols;
	*col = abs % g_cols;
}

static void cursor_to(int pos)
{
	int row, col;
	input_pos_to_screen(pos, &row, &col);
	if (row >= g_rows) row = g_rows - 1;
	if (row < 0) row = 0;
	vt_goto(row, col);
}

static int last_row_of(int len)
{
	int abs = prompt_len + len;
	return g_start_row + (abs > 0 ? (abs - 1) : 0) / g_cols;
}

static void redraw(const char *buf, int from, int new_len, int old_len, int pos)
{
	cursor_to(from);
	if (new_len > from) write(1, buf + from, new_len - from);
	sync_start_row(new_len);
	if (old_len > new_len) {
		int old_last = last_row_of(old_len);
		int new_last = last_row_of(new_len);
		cursor_to(new_len); vt_eol();
		for (int r = new_last + 1; r <= old_last; r++) {
			if (r >= g_rows) break;
			vt_goto(r, 0); vt_eol();
		}
	}
	cursor_to(pos);
}

static void replace_line(char *buf, int *len, int *pos, const char *newtext, int newlen)
{
	int old_len = *len;
	for (int i = 0; i < newlen; i++) buf[i] = newtext[i];
	buf[newlen] = '\0';
	*len = newlen;
	*pos = newlen;
	redraw(buf, 0, newlen, old_len, newlen);
}

static void insert_str(char *buf, int *len, int *pos, int maxlen, const char *s, int slen)
{
	if (*len + slen >= maxlen) return;
	for (int i = *len; i >= *pos; i--) buf[i + slen] = buf[i];
	for (int i = 0; i < slen; i++) buf[*pos + i] = s[i];
	*len += slen;
	buf[*len] = '\0';
	cursor_to(*pos);
	write(1, buf + *pos, *len - *pos);
	sync_start_row(*len);
	*pos += slen;
	cursor_to(*pos);
}

static int find_word_start(const char *buf, int pos)
{
	int p = pos;
	while (p > 0 && buf[p - 1] != ' ') p--;
	return p;
}

static int have_match(char matches[][256], int nmatch, const char *name)
{
	for (int i = 0; i < nmatch; i++)
		if (strcmp(matches[i], name) == 0) return 1;
	return 0;
}

static void list_dir_matches(const char *dir, const char *prefix, int plen,
                             char matches[][256], int *nmatch, int max,
                             int files_only, int skip_hidden)
{
	int fd = open(dir, O_RDONLY | O_DIRECTORY, 0);
	if (fd < 0) return;
	cervus_dirent_t de;
	while (*nmatch < max && readdir(fd, &de) == 0) {
		if (skip_hidden && de.d_name[0] == '.') continue;
		if (files_only && de.d_type == 1) continue;
		if (strncmp(de.d_name, prefix, plen) != 0) continue;
		if (have_match(matches, *nmatch, de.d_name)) continue;
		safe_strcpy(matches[*nmatch], 256, de.d_name);
		(*nmatch)++;
	}
	close(fd);
}

static void do_tab_complete(char *buf, int *len, int *pos, int maxlen)
{
	int ws_start = find_word_start(buf, *pos);
	int wlen = *pos - ws_start;
	if (wlen <= 0) {
		insert_str(buf, len, pos, maxlen, "    ", 4);
		return;
	}
	char word[256];
	if (wlen > 255) wlen = 255;
	memcpy(word, buf + ws_start, wlen);
	word[wlen] = '\0';

	int is_first_word = 1;
	for (int i = 0; i < ws_start; i++) {
		if (buf[i] != ' ') { is_first_word = 0; break; }
	}

	int skip_hidden = (word[0] != '.');

	char matches[MAX_COMPLETIONS][256];
	int nmatch = 0;

	if (is_first_word) {
		const char *pathvar = env_get("PATH");
		char ptmp[ENV_VAL_MAX];
		safe_strcpy(ptmp, sizeof(ptmp), pathvar);
		char *p = ptmp;
		while (*p && nmatch < MAX_COMPLETIONS) {
			char *seg = p;
			while (*p && *p != ':') p++;
			if (*p == ':') *p++ = '\0';
			if (seg[0])
				list_dir_matches(seg, word, wlen, matches, &nmatch,
				                 MAX_COMPLETIONS, 1, skip_hidden);
		}
		const char *builtins[] = {"help","exit","cd","export","unset",NULL};
		for (int i = 0; builtins[i] && nmatch < MAX_COMPLETIONS; i++) {
			if (strncmp(builtins[i], word, wlen) == 0 &&
			    !have_match(matches, nmatch, builtins[i])) {
				safe_strcpy(matches[nmatch], 256, builtins[i]);
				nmatch++;
			}
		}
	} else {
		char dirp[VFS_MAX_PATH];
		const char *prefix = word;
		char *last_slash = NULL;
		for (int i = 0; word[i]; i++)
			if (word[i] == '/') last_slash = &word[i];
		if (last_slash) {
			int dlen = (int)(last_slash - word);
			char raw_dir[256];
			if (dlen >= (int)sizeof(raw_dir)) dlen = sizeof(raw_dir) - 1;
			memcpy(raw_dir, word, dlen);
			raw_dir[dlen] = '\0';
			if (raw_dir[0] == '\0') raw_dir[0] = '/', raw_dir[1] = '\0';
			resolve_path(cwd, raw_dir, dirp, sizeof(dirp));
			prefix = last_slash + 1;
			if (prefix[0] == '.') skip_hidden = 0;
		} else {
			safe_strcpy(dirp, sizeof(dirp), cwd);
		}
		int plen = (int)strlen(prefix);
		list_dir_matches(dirp, prefix, plen, matches, &nmatch,
		                 MAX_COMPLETIONS, 0, skip_hidden);
	}

	if (nmatch == 0) {
		insert_str(buf, len, pos, maxlen, "    ", 4);
		return;
	}
	if (nmatch == 1) {
		const char *m = matches[0];
		int mlen = (int)strlen(m);
		int tail = mlen - wlen;
		if (tail > 0)
			insert_str(buf, len, pos, maxlen, m + wlen, tail);
		return;
	}
	int common = (int)strlen(matches[0]);
	for (int i = 1; i < nmatch; i++) {
		int j = 0;
		while (j < common && matches[0][j] == matches[i][j]) j++;
		common = j;
	}
	int extra = common - wlen;
	if (extra > 0) {
		insert_str(buf, len, pos, maxlen, matches[0] + wlen, extra);
		return;
	}
	wn();
	for (int i = 0; i < nmatch; i++) {
		ws("  ");
		ws(matches[i]);
	}
	wn();
	print_prompt();
	write(1, buf, *len);
	sync_start_row(*len);
	cursor_to(*pos);
}

static int readline_edit(char *buf, int maxlen)
{
	term_update_size();
	{
		int real_row = term_get_cursor_row();
		g_start_row = real_row - prompt_len / g_cols;
		if (g_start_row < 0) g_start_row = 0;
	}
	int len = 0, pos = 0, hidx = 0;
	static char saved[LINE_MAX];
	saved[0] = '\0';
	buf[0] = '\0';

	for (;;) {
		char c;
		if (read(0, &c, 1) <= 0) return -1;

		if (c == '\x1b') {
			char s[4];
			if (read(0, &s[0], 1) <= 0) continue;
			if (s[0] != '[') continue;
			if (read(0, &s[1], 1) <= 0) continue;
			if (s[1] == 'A') {
				if (hidx == 0) safe_strcpy(saved, LINE_MAX, buf);
				if (hidx < hist_count) {
					hidx++;
					const char *h = hist_get(hidx);
					if (h) {
						int hl = (int)strlen(h);
						if (hl > maxlen - 1) hl = maxlen - 1;
						replace_line(buf, &len, &pos, h, hl);
					}
				}
				continue;
			}
			if (s[1] == 'B') {
				if (hidx > 0) {
					hidx--;
					const char *h = hidx == 0 ? saved : hist_get(hidx);
					if (!h) h = "";
					int hl = (int)strlen(h);
					if (hl > maxlen - 1) hl = maxlen - 1;
					replace_line(buf, &len, &pos, h, hl);
				}
				continue;
			}
			if (s[1] == 'C') { if (pos < len) { pos++; cursor_to(pos); } continue; }
			if (s[1] == 'D') { if (pos > 0)  { pos--; cursor_to(pos); } continue; }
			if (s[1] == 'H') { pos = 0; cursor_to(0); continue; }
			if (s[1] == 'F') { pos = len; cursor_to(len); continue; }
			if (s[1] >= '0' && s[1] <= '9') {
				char extra[8];
				int ei = 0;
				while (ei < (int)sizeof(extra) - 1) {
					if (read(0, &extra[ei], 1) <= 0) break;
					if (extra[ei] == '~') { ei++; break; }
					if (!(extra[ei] >= '0' && extra[ei] <= '9') && extra[ei] != ';') { ei++; break; }
					ei++;
				}
				if (ei > 0 && extra[ei - 1] == '~') {
					if (s[1] == '3' && pos < len) {
						for (int i = pos; i < len - 1; i++) buf[i] = buf[i + 1];
						len--;
						buf[len] = '\0';
						redraw(buf, pos, len, len + 1, pos);
					} else if (s[1] == '1') {
						pos = 0;
						cursor_to(0);
					} else if (s[1] == '4') {
						pos = len;
						cursor_to(len);
					}
				}
			}
			continue;
		}

		if (c == '\n' || c == '\r') {
			buf[len] = '\0';
			cursor_to(len);
			wn();
			return len;
		}
		if (c == 3) { ws("^C"); wn(); buf[0] = '\0'; return 0; }
		if (c == 4) { if (len == 0) return -1; continue; }
		if (c == 1) { pos = 0; cursor_to(0); continue; }
		if (c == 5) { pos = len; cursor_to(len); continue; }
		if (c == '\t') {
			do_tab_complete(buf, &len, &pos, maxlen);
			continue;
		}
		if (c == 11) {
			if (pos < len) {
				int old_len = len;
				len = pos;
				buf[len] = '\0';
				redraw(buf, pos, len, old_len, pos);
			}
			continue;
		}
		if (c == 21) {
			if (pos > 0) {
				int old_len = len, del = pos;
				for (int i = 0; i < len - del; i++) buf[i] = buf[i + del];
				len -= del;
				pos = 0;
				buf[len] = '\0';
				redraw(buf, 0, len, old_len, 0);
			}
			continue;
		}
		if (c == 23) {
			if (pos > 0) {
				int p = pos;
				while (p > 0 && buf[p - 1] == ' ') p--;
				while (p > 0 && buf[p - 1] != ' ') p--;
				int old_len = len, del = pos - p;
				for (int i = p; i < len - del; i++) buf[i] = buf[i + del];
				len -= del;
				pos = p;
				buf[len] = '\0';
				redraw(buf, p, len, old_len, p);
			}
			continue;
		}
		if (c == '\b' || c == 0x7F) {
			if (pos > 0) {
				int old_len = len;
				for (int i = pos - 1; i < len - 1; i++) buf[i] = buf[i + 1];
				len--;
				pos--;
				buf[len] = '\0';
				redraw(buf, pos, len, old_len, pos);
			}
			continue;
		}
		if (c >= 0x20 && c < 0x7F) {
			if (len >= maxlen - 1) continue;
			for (int i = len; i > pos; i--) buf[i] = buf[i - 1];
			buf[pos] = c;
			len++;
			buf[len] = '\0';
			cursor_to(pos);
			write(1, buf + pos, len - pos);
			sync_start_row(len);
			pos++;
			cursor_to(pos);
		}
	}
}

static int tokenize(char *line, char *argv[], int maxargs)
{
	int argc = 0;
	char *p = line;
	while (*p) {
		while (isspace((unsigned char)*p)) p++;
		if (!*p) break;
		if (argc >= maxargs - 1) {
			ws(C_YELLOW "warning: too many arguments, truncated\n" C_RESET);
			break;
		}
		char *out = p;
		argv[argc++] = out;
		int in_dquote = 0, in_squote = 0;
		while (*p) {
			if (!in_squote && *p == '\\' && p[1]) {
				p++;
				*out++ = *p++;
				continue;
			}
			if (in_dquote) {
				if (*p == '"') { in_dquote = 0; p++; }
				else { *out++ = *p++; }
			} else if (in_squote) {
				if (*p == '\'') { in_squote = 0; p++; }
				else { *out++ = *p++; }
			} else {
				if (*p == '"') { in_dquote = 1; p++; }
				else if (*p == '\'') { in_squote = 1; p++; }
				else if (isspace((unsigned char)*p)) { p++; break; }
				else { *out++ = *p++; }
			}
		}
		*out = '\0';
		if (in_dquote || in_squote) { argv[argc] = NULL; return -1; }
	}
	argv[argc] = NULL;
	return argc;
}

static void cmd_help(void)
{
	wn();
	ws("  " C_CYAN "Cervus Shell" C_RESET " - commands\n");
	ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
	ws("  " C_BOLD "help" C_RESET "          show this message\n");
	ws("  " C_BOLD "cd" C_RESET " <dir>      change directory\n");
	ws("  " C_BOLD "export" C_RESET " N=V    set variable N to value V\n");
	ws("  " C_BOLD "unset" C_RESET " N       delete variable N\n");
	ws("  " C_BOLD "env" C_RESET "           list variables\n");
	ws("  " C_BOLD "exit" C_RESET "          quit shell\n");
	ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
	ws("  " C_BOLD "Programs:" C_RESET " ls, cat, echo, pwd, clear, uname\n");
	ws("  meminfo, cpuinfo, ps, kill, find, stat, wc, yes, sleep\n");
	ws("  mount, umount, mkfs, lsblk, mv, rm, mkdir, touch, diskinfo\n");
	ws("  " C_RED "shutdown" C_RESET ", " C_CYAN "reboot" C_RESET "\n");
	ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
	ws("  " C_BOLD "Operators:" C_RESET "  cmd1 " C_YELLOW ";" C_RESET
	   " cmd2   " C_YELLOW "&&" C_RESET "   " C_YELLOW "||" C_RESET "\n");
	ws("  " C_BOLD "Tab" C_RESET "       auto-complete / 4 spaces\n");
	ws("  " C_BOLD "Ctrl+C" C_RESET "    interrupt\n");
	ws("  " C_BOLD "Ctrl+A/E" C_RESET "  beginning/end of line\n");
	ws("  " C_BOLD "Ctrl+K/U" C_RESET "  delete to end/beginning\n");
	ws("  " C_BOLD "Ctrl+W" C_RESET "    delete word\n");
	ws("  " C_BOLD "Arrows" C_RESET "    cursor / history\n");
	ws("  " C_GRAY "-----------------------------------" C_RESET "\n");
	wn();
}

static int cmd_cd(const char *path)
{
	if (!path || !path[0] || strcmp(path, "~") == 0) {
		const char *home = env_get("HOME");
		path = (home && home[0]) ? home : "/";
	}
	char np[VFS_MAX_PATH];
	resolve_path(cwd, path, np, sizeof(np));
	cervus_stat_t st;
	if (stat(np, &st) < 0) {
		ws(C_RED "cd: not found: " C_RESET);
		ws(path);
		wn();
		return 1;
	}
	if (st.st_type != 1) {
		ws(C_RED "cd: not a dir: " C_RESET);
		ws(path);
		wn();
		return 1;
	}
	safe_strcpy(cwd, sizeof(cwd), np);
	return 0;
}

static int valid_varname(const char *s)
{
	if (!s || !*s) return 0;
	if (!isalpha((unsigned char)*s) && *s != '_') return 0;
	for (s++; *s; s++)
		if (!isalnum((unsigned char)*s) && *s != '_') return 0;
	return 1;
}

static int cmd_export(int argc, char *argv[])
{
	if (argc < 2) { ws(C_RED "export: usage: export NAME=VALUE\n" C_RESET); return 1; }
	if (argc > 2) { ws(C_RED "export: invalid syntax\n" C_RESET); return 1; }
	char *arg = argv[1];
	char *eq_pos = strchr(arg, '=');
	if (!eq_pos) {
		if (!valid_varname(arg)) {
			ws(C_RED "export: not a valid identifier\n" C_RESET);
			return 1;
		}
		env_set(arg, "");
		return 0;
	}
	*eq_pos = '\0';
	const char *name = arg;
	const char *val  = eq_pos + 1;
	if (!valid_varname(name)) {
		*eq_pos = '=';
		ws(C_RED "export: not a valid identifier\n" C_RESET);
		return 1;
	}
	env_set(name, val);
	*eq_pos = '=';
	return 0;
}

static int cmd_unset(int argc, char *argv[])
{
	if (argc < 2) { ws(C_RED "unset: usage: unset NAME\n" C_RESET); return 1; }
	for (int i = 1; i < argc; i++) env_unset(argv[i]);
	return 0;
}

static int find_in_path(const char *cmd, char *out, size_t outsz)
{
	const char *pathvar = env_get("PATH");
	if (!pathvar || !pathvar[0]) {
		path_join("/bin", cmd, out, outsz);
		cervus_stat_t st;
		return stat(out, &st) == 0 && st.st_type != 1;
	}
	char tmp[ENV_VAL_MAX];
	safe_strcpy(tmp, sizeof(tmp), pathvar);
	char *p = tmp;
	while (*p) {
		char *seg = p;
		while (*p && *p != ':') p++;
		if (*p == ':') *p++ = '\0';
		if (!seg[0]) continue;
		char candidate[VFS_MAX_PATH];
		path_join(seg, cmd, candidate, sizeof(candidate));
		cervus_stat_t st;
		if (stat(candidate, &st) == 0 && st.st_type != 1) {
			safe_strcpy(out, outsz, candidate);
			return 1;
		}
	}
	return 0;
}

static int run_single(char *line)
{
	char expanded[LINE_MAX];
	expand_vars(line, expanded, sizeof(expanded));
	char buf[LINE_MAX];
	safe_strcpy(buf, sizeof(buf), expanded);
	char *argv[MAX_ARGS];
	int argc = tokenize(buf, argv, MAX_ARGS);
	if (argc < 0) { ws(C_RED "syntax error: unclosed quote\n" C_RESET); return 1; }
	if (!argc) return 0;
	const char *cmd = argv[0];

	if (strcmp(cmd, "help")   == 0) { cmd_help(); return 0; }
	if (strcmp(cmd, "exit")   == 0) { ws("Goodbye!\n"); exit(0); }
	if (strcmp(cmd, "cd")     == 0) return cmd_cd(argc > 1 ? argv[1] : NULL);
	if (strcmp(cmd, "export") == 0) return cmd_export(argc, argv);
	if (strcmp(cmd, "unset")  == 0) return cmd_unset(argc, argv);

	char binpath[VFS_MAX_PATH];
	if (cmd[0] == '/' || cmd[0] == '.') {
		safe_strcpy(binpath, sizeof(binpath), cmd);
	} else {
		if (!find_in_path(cmd, binpath, sizeof(binpath))) {
			char t_cwd[VFS_MAX_PATH];
			resolve_path(cwd, cmd, t_cwd, sizeof(t_cwd));
			cervus_stat_t st;
			if (stat(t_cwd, &st) == 0 && st.st_type != 1) {
				safe_strcpy(binpath, sizeof(binpath), t_cwd);
			} else {
				ws(C_RED "not found: " C_RESET);
				ws(cmd);
				wn();
				return 127;
			}
		}
	}

#define REAL_ARGV_MAX (MAX_ARGS + ENV_MAX_VARS + 4)
	char *real_argv_buf[REAL_ARGV_MAX];
	static char _cwd_flag[VFS_MAX_PATH + 8];
	static char _env_flags[ENV_MAX_VARS][ENV_NAME_MAX + ENV_VAL_MAX + 8];

	int ri = 0;
	real_argv_buf[ri++] = binpath;
	for (int i = 1; i < argc && ri < REAL_ARGV_MAX - 2; i++)
		real_argv_buf[ri++] = argv[i];
	snprintf(_cwd_flag, sizeof(_cwd_flag), "--cwd=%s", cwd);
	real_argv_buf[ri++] = _cwd_flag;
	for (int ei = 0; ei < g_env_count && ri < REAL_ARGV_MAX - 1; ei++) {
		snprintf(_env_flags[ei], sizeof(_env_flags[ei]), "--env:%s=%s",
		         g_env[ei].name, g_env[ei].value);
		real_argv_buf[ri++] = _env_flags[ei];
	}
	real_argv_buf[ri] = NULL;

	pid_t child = fork();
	if (child < 0) { ws(C_RED "fork failed" C_RESET "\n"); return 1; }
	if (child == 0) {
		execve(binpath, (const char **)real_argv_buf, NULL);
		ws(C_RED "exec failed: " C_RESET);
		ws(binpath);
		wn();
		exit(127);
	}
	int status = 0;
	waitpid(child, &status, 0);
	return (status >> 8) & 0xFF;
}

typedef enum { CH_NONE = 0, CH_SEQ, CH_AND, CH_OR } chain_t;

static void run_command(char *line)
{
	char work[LINE_MAX];
	safe_strcpy(work, sizeof(work), line);
	char *segs[MAX_SEGMENTS];
	chain_t ops[MAX_SEGMENTS];
	int ns = 1;
	segs[0] = work;
	ops[0] = CH_NONE;
	char *p = work;
	while (*p && ns < MAX_SEGMENTS) {
		if (*p == '\\' && p[1]) { p += 2; continue; }
		if (*p == '"')  { p++; while (*p && *p != '"')  { if (*p == '\\' && p[1]) p++; p++; } if (*p) p++; continue; }
		if (*p == '\'') { p++; while (*p && *p != '\'') p++; if (*p) p++; continue; }
		if (*p == '&' && *(p+1) == '&') {
			*p = '\0';
			p += 2;
			while (isspace((unsigned char)*p)) p++;
			ops[ns] = CH_AND;
			segs[ns++] = p;
			continue;
		}
		if (*p == '|' && *(p+1) == '|') {
			*p = '\0';
			p += 2;
			while (isspace((unsigned char)*p)) p++;
			ops[ns] = CH_OR;
			segs[ns++] = p;
			continue;
		}
		if (*p == ';') {
			*p = '\0';
			p++;
			while (isspace((unsigned char)*p)) p++;
			ops[ns] = CH_SEQ;
			segs[ns++] = p;
			continue;
		}
		p++;
	}
	int rc = 0;
	for (int i = 0; i < ns; i++) {
		char *s = segs[i];
		while (isspace((unsigned char)*s)) s++;
		size_t sl = strlen(s);
		while (sl > 0 && isspace((unsigned char)s[sl - 1])) s[--sl] = '\0';
		if (!s[0]) continue;
		if (i > 0) {
			if (ops[i] == CH_AND && rc != 0) continue;
			if (ops[i] == CH_OR  && rc == 0) continue;
		}
		rc = run_single(s);
	}
	g_last_rc = rc;
}

static void print_motd(void)
{
	int fd = open("/mnt/etc/motd", O_RDONLY, 0);
	if (fd < 0) fd = open("/etc/motd", O_RDONLY, 0);
	if (fd < 0) {
		wn();
		ws("  Cervus OS v0.0.2\n  Type 'help' for commands.\n");
		wn();
		return;
	}
	char buf[1024];
	ssize_t n = read(fd, buf, sizeof(buf) - 1);
	close(fd);
	if (n > 0) {
		buf[n] = '\0';
		write(1, buf, n);
	}
}

static int g_installed = 0;

static int sys_disk_mount(const char *dev, const char *path)
{
	return (int)syscall2(SYS_DISK_MOUNT, dev, path);
}

static int sys_disk_format(const char *dev, const char *label)
{
	return (int)syscall2(SYS_DISK_FORMAT, dev, label);
}

static int sys_disk_mkfs_fat32(const char *dev, const char *label)
{
	return (int)syscall2(SYS_DISK_MKFS_FAT32, dev, label);
}

static int sys_disk_partition(const char *dev, const void *specs, uint64_t n)
{
	return (int)syscall3(SYS_DISK_PARTITION, dev, specs, n);
}

static int sys_disk_umount(const char *path)
{
	return (int)syscall1(SYS_DISK_UMOUNT, path);
}

static int ensure_dir(const char *path)
{
	cervus_stat_t st;
	if (stat(path, &st) == 0) return 0;
	return (int)syscall2(SYS_MKDIR, path, 0755);
}

static int ensure_parent_dir(const char *path)
{
	char tmp[512];
	size_t len = strlen(path);
	if (len >= sizeof(tmp)) return -1;
	int last_slash = -1;
	for (int i = (int)len - 1; i >= 0; i--) {
		if (path[i] == '/') { last_slash = i; break; }
	}
	if (last_slash <= 0) return 0;
	for (int i = 0; i < last_slash; i++) tmp[i] = path[i];
	tmp[last_slash] = '\0';
	int depth = 0;
	int starts[32];
	starts[depth++] = 0;
	for (int i = 1; i < last_slash && depth < 32; i++) {
		if (tmp[i] == '/') starts[depth++] = i;
	}
	for (int d = 0; d < depth; d++) {
		int end = (d + 1 < depth) ? starts[d + 1] : last_slash;
		char part[512];
		for (int i = 0; i < end; i++) part[i] = tmp[i];
		part[end] = '\0';
		if (part[0] == '\0') continue;
		cervus_stat_t st;
		if (stat(part, &st) != 0) syscall2(SYS_MKDIR, part, 0755);
	}
	return 0;
}

static int copy_one_file_progress(const char *src, const char *dst, const char *display_name)
{
	int sfd = open(src, O_RDONLY, 0);
	if (sfd < 0) return sfd;
	ensure_parent_dir(dst);
	int dfd = open(dst, O_WRONLY | O_CREAT | O_TRUNC, 0755);
	if (dfd < 0) { close(sfd); return dfd; }

	cervus_stat_t st;
	uint64_t total = 0;
	if (stat(src, &st) == 0) total = st.st_size;

	static char fbuf[4096];
	ssize_t n;
	int rc = 0;
	uint64_t written = 0;
	int last_pct = -1;
	int spinner = 0;

	while ((n = read(sfd, fbuf, sizeof(fbuf))) > 0) {
		ssize_t w = write(dfd, fbuf, (size_t)n);
		if (w < 0) { rc = (int)w; break; }
		written += (uint64_t)w;
		if (total > 0 && display_name) {
			int pct = (int)((written * 100) / total);
			if (pct != last_pct) {
				static const char glyphs[4] = { '|', '/', '-', '\\' };
				ws("\r\033[K       ");
				wc(glyphs[spinner & 3]);
				ws(" ");
				ws(display_name);
				ws(" ");
				char pb[8];
				int bi = 0;
				if (pct >= 100) { pb[bi++]='1'; pb[bi++]='0'; pb[bi++]='0'; }
				else if (pct >= 10) { pb[bi++]=(char)('0'+pct/10); pb[bi++]=(char)('0'+pct%10); }
				else { pb[bi++]=(char)('0'+pct); }
				pb[bi++]='%';
				pb[bi]='\0';
				ws(pb);
				spinner++;
				last_pct = pct;
			}
		}
	}
	close(sfd);
	close(dfd);

	if (display_name) {
		ws("\r\033[K       ");
		ws(display_name);
		ws(rc < 0 ? " FAILED\n" : " done\n");
	}
	return rc;
}

static int copy_one_file(const char *src, const char *dst)
{
	return copy_one_file_progress(src, dst, NULL);
}

typedef struct {
	char name[64];
	uint8_t type;
} dir_entry_t;

static int read_dir_entries(const char *path, dir_entry_t *out, int max)
{
	int fd = open(path, O_RDONLY | O_DIRECTORY, 0);
	if (fd < 0) return 0;
	int count = 0;
	cervus_dirent_t de;
	while (count < max && readdir(fd, &de) == 0) {
		if (de.d_name[0] == '.' && (de.d_name[1] == '\0' ||
		    (de.d_name[1] == '.' && de.d_name[2] == '\0'))) continue;
		safe_strcpy(out[count].name, sizeof(out[count].name), de.d_name);
		out[count].type = de.d_type;
		count++;
	}
	close(fd);
	return count;
}

static void copy_tree(const char *src_dir, const char *dst_dir)
{
	dir_entry_t entries[MAX_ENTRIES];
	int n = read_dir_entries(src_dir, entries, MAX_ENTRIES);
	if (n == 0) return;
	ensure_dir(dst_dir);
	for (int i = 0; i < n; i++) {
		char sp[256], dp[256];
		path_join(src_dir, entries[i].name, sp, sizeof(sp));
		path_join(dst_dir, entries[i].name, dp, sizeof(dp));
		if (entries[i].type == 1) {
			ensure_dir(dp);
			copy_tree(sp, dp);
		} else {
			copy_one_file_progress(sp, dp, sp);
		}
	}
}

static uint64_t get_disk_sectors(const char *dev)
{
	struct {
		char     name[32];
		uint64_t sectors;
		uint64_t size_bytes;
		char     model[41];
		uint8_t  present;
		uint8_t  _pad[6];
	} info;
	memset(&info, 0, sizeof(info));
	if ((int)syscall2(SYS_DISK_INFO, 0, &info) < 0) return 0;
	(void)dev;
	return info.sectors;
}

static int write_limine_conf(const char *path)
{
	int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
	if (fd < 0) return fd;
	const char *conf =
		"timeout: 5\n"
		"default_entry: 1\n"
		"interface_branding: Cervus\n"
		"wallpaper: boot():/boot/wallpaper.png\n"
		"\n"
		"/Cervus v0.0.2 Alpha\n"
		"    protocol: limine\n"
		"    path: boot():/kernel\n"
		"    module_path: boot():/shell.elf\n"
		"    module_cmdline: init\n";
	write(fd, conf, strlen(conf));
	close(fd);
	return 0;
}

typedef struct {
	char     name[32];
	char     model[41];
	uint64_t sectors;
	uint64_t size_bytes;
} disk_summary_t;

static int list_disks(disk_summary_t out[4])
{
	int found = 0;
	for (int i = 0; i < 4; i++) {
		struct {
			char     name[32];
			uint64_t sectors;
			uint64_t size_bytes;
			char     model[41];
			uint8_t  present;
			uint8_t  _pad[6];
		} info;
		memset(&info, 0, sizeof(info));
		int r = (int)syscall2(SYS_DISK_INFO, (uint64_t)i, (uint64_t)&info);
		if (r < 0 || !info.present) continue;
		size_t nlen = strlen(info.name);
		int is_part = 0;
		for (size_t k = 0; k < nlen; k++) {
			if (info.name[k] >= '0' && info.name[k] <= '9') { is_part = 1; break; }
		}
		if (is_part) continue;
		safe_strcpy(out[found].name,  sizeof(out[found].name),  info.name);
		safe_strcpy(out[found].model, sizeof(out[found].model), info.model);
		out[found].sectors    = info.sectors;
		out[found].size_bytes = info.size_bytes;
		found++;
		if (found >= 4) break;
	}
	return found;
}

static int ask_choose_disk(char *out_name, size_t out_cap)
{
	disk_summary_t disks[4];
	int n = list_disks(disks);
	if (n == 0) {
		ws(C_RED "  No disks detected!" C_RESET "\n");
		return -1;
	}
	ws("\n");
	ws(C_CYAN "  Available disks:" C_RESET "\n");
	for (int i = 0; i < n; i++) {
		ws("    ");
		wc((char)('1' + i));
		ws(") ");
		ws(C_BOLD);
		ws(disks[i].name);
		ws(C_RESET);
		ws("  ");
		uint64_t mb = disks[i].size_bytes / (1024 * 1024);
		char buf[32];
		int bi = 0;
		if (mb == 0) {
			buf[bi++] = '0';
		} else {
			char rev[32];
			int ri = 0;
			uint64_t v = mb;
			while (v) { rev[ri++] = (char)('0' + (v % 10)); v /= 10; }
			while (ri) buf[bi++] = rev[--ri];
		}
		buf[bi] = '\0';
		ws(buf);
		ws(" MB  ");
		ws(disks[i].model);
		ws("\n");
	}
	ws("\n  Select disk [1-");
	wc((char)('0' + n));
	ws("]: ");
	char c = 0;
	while (1) {
		if (read(0, &c, 1) <= 0) continue;
		if (c >= '1' && c <= (char)('0' + n)) { wc(c); wn(); break; }
		if (c == 'q' || c == 'Q') { wc(c); wn(); return -1; }
	}
	int idx = c - '1';
	safe_strcpy(out_name, out_cap, disks[idx].name);
	return 0;
}

static void progress_done(const char *msg)
{
	wc('\r');
	ws(C_GREEN "       ");
	ws(msg);
	ws(C_RESET "                       \n");
}

static void progress_fail(const char *msg)
{
	wc('\r');
	ws(C_RED "       ");
	ws(msg);
	ws(C_RESET "                       \n");
}

static void run_installer(void)
{
	ws("\n");
	ws(C_CYAN "  Cervus OS Installer" C_RESET "\n");
	ws(C_GRAY "  -----------------------------------" C_RESET "\n");

	char chosen_disk_name[32];
	if (ask_choose_disk(chosen_disk_name, sizeof(chosen_disk_name)) < 0) {
		ws("\n  Cancelled. Running in " C_YELLOW "Live Mode" C_RESET ".\n\n");
		return;
	}

	ws("\n  Target disk: " C_BOLD);
	ws(chosen_disk_name);
	ws(C_RESET "\n");
	ws("  Layout: ESP (FAT32, 64 MB) + root (ext2) + swap (16 MB)\n");
	ws("\n");
	ws(C_RED "  WARNING: This will erase ALL data on " C_RESET);
	ws(C_BOLD);
	ws(chosen_disk_name);
	ws(C_RESET C_RED "!" C_RESET "\n\n");
	ws("  Continue? [y/n]: ");

	char c = 0;
	while (1) {
		if (read(0, &c, 1) <= 0) continue;
		if (c == 'y' || c == 'Y' || c == 'n' || c == 'N') { wc(c); wn(); break; }
	}

	if (c == 'n' || c == 'N') {
		ws("\n  Cancelled. Running in " C_YELLOW "Live Mode" C_RESET ".\n\n");
		return;
	}

	disk_summary_t disks[4];
	int n_disks = list_disks(disks);
	uint64_t total_sectors = 0;
	for (int i = 0; i < n_disks; i++) {
		if (strcmp(disks[i].name, chosen_disk_name) == 0) {
			total_sectors = disks[i].sectors;
			break;
		}
	}
	if (total_sectors < 300000) {
		ws(C_RED "  Disk too small (need at least 150 MB)" C_RESET "\n\n");
		return;
	}

	uint32_t esp_start  = 2048;
	uint32_t esp_size   = 131072;
	uint32_t swap_size  = 32768;
	uint32_t root_start = esp_start + esp_size;
	uint32_t avail      = (uint32_t)total_sectors - root_start - swap_size;
	uint32_t root_size  = avail;
	uint32_t swap_start = root_start + root_size;

	ws("\n  [1/8] Writing partition table...\n");
	cervus_mbr_part_t specs[4];
	memset(specs, 0, sizeof(specs));
	specs[0].boot_flag    = 1;
	specs[0].type         = MBR_TYPE_FAT32_LBA;
	specs[0].lba_start    = esp_start;
	specs[0].sector_count = esp_size;
	specs[1].boot_flag    = 0;
	specs[1].type         = MBR_TYPE_LINUX;
	specs[1].lba_start    = root_start;
	specs[1].sector_count = root_size;
	specs[2].boot_flag    = 0;
	specs[2].type         = MBR_TYPE_LINUX_SWAP;
	specs[2].lba_start    = swap_start;
	specs[2].sector_count = swap_size;
	if (sys_disk_partition(chosen_disk_name, specs, 3) < 0) {
		progress_fail("Failed to write partition table!");
		return;
	}
	progress_done("partition table written");

	char part1[32], part2[32];
	snprintf(part1, sizeof(part1), "%s1", chosen_disk_name);
	snprintf(part2, sizeof(part2), "%s2", chosen_disk_name);

	ws("  [2/8] Formatting ");
	ws(part1);
	ws(" as FAT32 (ESP)...\n");
	if (sys_disk_mkfs_fat32(part1, "CERVUS-ESP") < 0) {
		progress_fail("mkfs.fat32 failed!");
		return;
	}
	progress_done("FAT32 ESP created");

	ws("  [3/8] Formatting ");
	ws(part2);
	ws(" as ext2 (root)...\n");
	if (sys_disk_format(part2, "cervus-root") < 0) {
		progress_fail("mkfs.ext2 failed!");
		return;
	}
	progress_done("ext2 root created");

	ws("  [4/8] Mounting partitions...\n");
	ensure_dir("/mnt/esp");
	ensure_dir("/mnt/root");
	if (sys_disk_mount(part1, "/mnt/esp") < 0) {
		progress_fail("mount ESP failed");
		return;
	}
	if (sys_disk_mount(part2, "/mnt/root") < 0) {
		progress_fail("mount root failed");
		sys_disk_umount("/mnt/esp");
		return;
	}
	progress_done("mounted");

	ws("  [5/8] Copying boot files to ESP...\n");
	ensure_dir("/mnt/esp/boot");
	ensure_dir("/mnt/esp/boot/limine");
	ensure_dir("/mnt/esp/EFI");
	ensure_dir("/mnt/esp/EFI/BOOT");

	struct { const char *src; const char *dst; int required; } boot_files[] = {
		{ "/boot/kernel",              "/mnt/esp/boot/kernel",                        1 },
		{ "/boot/kernel",              "/mnt/esp/kernel",                             0 },
		{ "/boot/shell.elf",           "/mnt/esp/boot/shell.elf",                     1 },
		{ "/boot/shell.elf",           "/mnt/esp/shell.elf",                          0 },
		{ "/boot/limine-bios.sys",     "/mnt/esp/boot/limine/limine-bios.sys",        0 },
		{ "/boot/limine-bios-hdd.bin", "/mnt/esp/boot/limine/limine-bios-hdd.bin",    0 },
		{ "/boot/BOOTX64.EFI",         "/mnt/esp/EFI/BOOT/BOOTX64.EFI",               0 },
		{ "/boot/BOOTIA32.EFI",        "/mnt/esp/EFI/BOOT/BOOTIA32.EFI",              0 },
		{ "/boot/wallpaper.png",       "/mnt/esp/boot/wallpaper.png",                 0 },
		{ "/boot/wallpaper.png",       "/mnt/esp/wallpaper.png",                      0 },
		{ NULL, NULL, 0 }
	};
	for (int i = 0; boot_files[i].src; i++) {
		cervus_stat_t st;
		if (stat(boot_files[i].src, &st) != 0) {
			if (boot_files[i].required) {
				ws(C_RED "       MISSING required: " C_RESET);
				ws(boot_files[i].src);
				wn();
			} else {
				ws(C_YELLOW "       skip (missing): " C_RESET);
				ws(boot_files[i].src);
				wn();
			}
			continue;
		}
		if (copy_one_file_progress(boot_files[i].src, boot_files[i].dst, boot_files[i].src) < 0) {
			ws(C_RED "       FAILED: " C_RESET);
			ws(boot_files[i].dst);
			wn();
		} else {
			ws(C_GREEN "       " C_RESET);
			ws(boot_files[i].dst);
			wn();
		}
	}

	ws("  [6/8] Writing limine.conf...\n");
	int ok1 = write_limine_conf("/mnt/esp/boot/limine/limine.conf");
	int ok2 = write_limine_conf("/mnt/esp/EFI/BOOT/limine.conf");
	int ok3 = write_limine_conf("/mnt/esp/limine.conf");
	if (ok1 < 0 && ok2 < 0 && ok3 < 0)
		progress_fail("failed to write limine.conf");
	else
		progress_done("limine.conf written (3 locations)");

	ws("  [7/8] Populating root filesystem...\n");
	const char *rdirs[] = {
		"/mnt/root/bin", "/mnt/root/apps", "/mnt/root/etc",
		"/mnt/root/home", "/mnt/root/tmp", "/mnt/root/var",
		"/mnt/root/usr", "/mnt/root/usr/bin",
		"/mnt/root/usr/lib", "/mnt/root/usr/include",
		NULL
	};
	for (int i = 0; rdirs[i]; i++) ensure_dir(rdirs[i]);

	ws("       copying /bin...\n");
	copy_tree("/bin",  "/mnt/root/bin");
	ws("       copying /apps...\n");
	copy_tree("/apps", "/mnt/root/apps");

	cervus_stat_t est;
	if (stat("/etc", &est) == 0) {
		ws("       copying /etc...\n");
		static dir_entry_t etc_entries[MAX_ENTRIES];
		int nn = read_dir_entries("/etc", etc_entries, MAX_ENTRIES);
		for (int i = 0; i < nn; i++) {
			const char *nm = etc_entries[i].name;
			size_t nl = strlen(nm);
			int is_txt = (nl >= 5 && strcmp(nm + nl - 4, ".txt") == 0);
			if (etc_entries[i].type == 0) {
				char sp[256], dp[256];
				path_join("/etc", nm, sp, sizeof(sp));
				if (is_txt) path_join("/mnt/root/home", nm, dp, sizeof(dp));
				else        path_join("/mnt/root/etc",  nm, dp, sizeof(dp));
				copy_one_file(sp, dp);
			} else if (etc_entries[i].type == 1) {
				char sp[256], dp[256];
				path_join("/etc", nm, sp, sizeof(sp));
				path_join("/mnt/root/etc", nm, dp, sizeof(dp));
				copy_tree(sp, dp);
			}
		}
	}

	cervus_stat_t hst;
	if (stat("/home", &hst) == 0) {
		ws("       copying /home...\n");
		copy_tree("/home", "/mnt/root/home");
	}

	ws("  [8/8] Installing BIOS stage1 to MBR...\n");
	{
		static uint8_t sys_buf[300 * 1024];
		int fd = open("/mnt/esp/boot/limine/limine-bios-hdd.bin", O_RDONLY, 0);
		if (fd < 0) {
			progress_fail("limine-bios-hdd.bin not found on ESP");
		} else {
			cervus_stat_t st;
			int sr = stat("/mnt/esp/boot/limine/limine-bios-hdd.bin", &st);
			uint32_t sys_size = (sr == 0) ? (uint32_t)st.st_size : 0;

			if (sys_size == 0 || sys_size > sizeof(sys_buf)) {
				close(fd);
				progress_fail("bad limine-bios-hdd.bin size");
			} else {
				uint32_t got = 0;
				int ok = 1;
				while (got < sys_size) {
					ssize_t n = read(fd, sys_buf + got, sys_size - got);
					if (n <= 0) { ok = 0; break; }
					got += (uint32_t)n;
				}
				close(fd);
				if (!ok || got != sys_size) {
					progress_fail("short read on limine-bios-hdd.bin");
				} else {
					long r = disk_bios_install(chosen_disk_name, sys_buf, sys_size);
					if (r < 0) progress_fail("BIOS install syscall failed");
					else       progress_done("BIOS stage1 installed");
				}
			}
		}
	}

	sys_disk_umount("/mnt/esp");
	sys_disk_umount("/mnt/root");

	ws("\n" C_GREEN "  Installation complete!" C_RESET "\n");
	ws("  The system will reboot in 3 seconds.\n\n");

	for (int i = 3; i > 0; i--) {
		ws("  Rebooting in ");
		wc((char)('0' + i));
		ws("...\r");
		syscall1(SYS_SLEEP_NS, 1000000000ULL);
	}
	ws("\n");
	syscall0(SYS_REBOOT);
	g_installed = 1;
}

__attribute__((naked)) void _start(void)
{
	asm volatile(
		"mov %%rsp, %%rdi\n"
		"and $-16, %%rsp\n"
		"call _start_main\n"
		"xor %%rdi, %%rdi\n"
		"xor %%rax, %%rax\n"
		"syscall\n"
		".byte 0xf4\n"
		"jmp . - 1\n" ::: "memory");
}

__attribute__((noreturn)) void _start_main(uint64_t *initial_rsp)
{
	(void)initial_rsp;

	cervus_stat_t dev_st;
	int has_disk = (stat("/dev/hda", &dev_st) == 0);
	int has_hda2 = (stat("/dev/hda2", &dev_st) == 0);
	int disk_mounted = 0;

	if (has_hda2) {
		if (sys_disk_mount("hda2", "/mnt") == 0) {
			disk_mounted = 1;
			g_installed = 1;
		}
	} else if (has_disk) {
		if (sys_disk_mount("hda", "/mnt") == 0) {
			disk_mounted = 1;
			g_installed = 1;
		} else {
			run_installer();
			if (g_installed) disk_mounted = 1;
		}
	}

	if (disk_mounted) {
		safe_strcpy(cwd, sizeof(cwd), "/mnt/home");
		env_set("HOME", "/mnt/home");
		env_set("PATH", "/mnt/bin:/mnt/apps:/mnt/usr/bin");
		env_set("SHELL", "/mnt/bin/shell");
	} else {
		safe_strcpy(cwd, sizeof(cwd), "/");
		env_set("HOME", "/");
		env_set("PATH", "/bin:/apps");
		env_set("SHELL", "/bin/shell");
	}

	if (!disk_mounted || !has_disk) env_set("MODE", "live");
	else                            env_set("MODE", "installed");

	print_motd();

	{
		static char hist_path[VFS_MAX_PATH];
		const char *h = env_get("HOME");
		if (h && h[0]) {
			path_join(h, ".history", hist_path, sizeof(hist_path));
			g_hist_file = hist_path;
			hist_load(hist_path);
		}
	}

	if (!has_disk)
		ws(C_YELLOW " [Live Mode]" C_RESET " No disk detected. All changes are in RAM.\n\n");
	else if (!disk_mounted)
		ws(C_YELLOW " [Live Mode]" C_RESET " Disk not mounted.\n\n");

	char line[LINE_MAX];
	for (;;) {
		print_prompt();
		int n = readline_edit(line, LINE_MAX);
		if (n < 0) {
			ws("\nSession ended. Restarting shell...\n");
			const char *h = env_get("HOME");
			safe_strcpy(cwd, sizeof(cwd), (h && h[0]) ? h : "/");
			print_motd();
			continue;
		}
		int len = (int)strlen(line);
		while (len > 0 && isspace((unsigned char)line[len - 1])) line[--len] = '\0';
		if (len > 0) {
			hist_push(line);
			run_command(line);
		}
	}
}