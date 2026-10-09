#include <libintl.h>
#include <locale.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>
#include <errno.h>
#include <limits.h>
#include <fcntl.h>
#include <unistd.h>
#include <ctype.h>
#include <pthread.h>
#include <sys/stat.h>
#include <sys/mman.h>
#include "pleval.h"

#define DEFAULT_LOCALEDIR "/usr/share/locale"

struct binding {
	struct binding *next;
	int dirlen;
	volatile int active;
	char *domainname;
	char *dirname;
	char buf[];
};

struct msgcat {
	struct msgcat *next;
	const void *map;
	size_t map_size;
	const char *plural_rule;
	unsigned long nplurals;
	struct binding *binding;
	int cat;
	char locname[];
};

static struct binding *volatile bindings;
static struct msgcat *volatile cats;
static pthread_mutex_t lock = PTHREAD_MUTEX_INITIALIZER;
static char *current_domain;

static const char *const catnames[] = {
	[LC_CTYPE] = "LC_CTYPE",
	[LC_NUMERIC] = "LC_NUMERIC",
	[LC_TIME] = "LC_TIME",
	[LC_COLLATE] = "LC_COLLATE",
	[LC_MONETARY] = "LC_MONETARY",
	[LC_MESSAGES] = "LC_MESSAGES",
};

static struct binding *find_binding(const char *domainname)
{
	for (struct binding *p = bindings; p; p = p->next)
		if (p->active && !strcmp(p->domainname, domainname)) return p;
	return 0;
}

char *bindtextdomain(const char *domainname, const char *dirname)
{
	struct binding *p, *q;

	if (!domainname || !*domainname) {
		errno = EINVAL;
		return 0;
	}
	if (!dirname) {
		p = find_binding(domainname);
		return p ? p->dirname : (char *)DEFAULT_LOCALEDIR;
	}

	size_t domlen = strnlen(domainname, NAME_MAX+1);
	size_t dirlen = strnlen(dirname, PATH_MAX);
	if (domlen > NAME_MAX || dirlen >= PATH_MAX) {
		errno = EINVAL;
		return 0;
	}

	pthread_mutex_lock(&lock);

	for (p=bindings; p; p=p->next)
		if (!strcmp(p->domainname, domainname) && !strcmp(p->dirname, dirname))
			break;

	if (!p) {
		p = calloc(sizeof *p + domlen + dirlen + 2, 1);
		if (!p) {
			pthread_mutex_unlock(&lock);
			return 0;
		}
		p->next = bindings;
		p->dirlen = dirlen;
		p->domainname = p->buf;
		p->dirname = p->buf + domlen + 1;
		memcpy(p->domainname, domainname, domlen+1);
		memcpy(p->dirname, dirname, dirlen+1);
		__atomic_store_n(&bindings, p, __ATOMIC_RELEASE);
	}

	__atomic_store_n(&p->active, 1, __ATOMIC_RELEASE);
	for (q=bindings; q; q=q->next)
		if (q != p && !strcmp(q->domainname, domainname))
			__atomic_store_n(&q->active, 0, __ATOMIC_RELEASE);

	pthread_mutex_unlock(&lock);
	return p->dirname;
}

char *bind_textdomain_codeset(const char *domainname, const char *codeset)
{
	(void)domainname;
	if (codeset && strcasecmp(codeset, "UTF-8") && strcasecmp(codeset, "UTF8")) {
		errno = EINVAL;
		return 0;
	}
	return "UTF-8";
}

static char *gettextdomain(void)
{
	return current_domain ? current_domain : "messages";
}

char *textdomain(const char *domainname)
{
	if (!domainname) return gettextdomain();

	size_t domlen = strlen(domainname);
	if (domlen > NAME_MAX) {
		errno = EINVAL;
		return 0;
	}
	if (!domlen) domainname = "messages", domlen = 8;

	if (!current_domain) {
		current_domain = malloc(NAME_MAX+1);
		if (!current_domain) return 0;
	}
	memcpy(current_domain, domainname, domlen+1);
	return current_domain;
}

static const void *map_file(const char *name, size_t *size)
{
	int fd = open(name, O_RDONLY | O_CLOEXEC);
	if (fd < 0) return 0;
	struct stat st;
	void *map = MAP_FAILED;
	if (!fstat(fd, &st) && st.st_size >= 20)
		map = mmap(0, (size_t)st.st_size, PROT_READ, MAP_PRIVATE, fd, 0);
	close(fd);
	if (map == MAP_FAILED) return 0;
	*size = (size_t)st.st_size;
	return map;
}

static struct msgcat *load_cat(struct binding *q, const char *locname, size_t loclen,
                               int category, const char *domainname, size_t domlen)
{
	struct msgcat *p;

	for (p=cats; p; p=p->next)
		if (p->binding == q && p->cat == category &&
		    !strncmp(p->locname, locname, loclen) && !p->locname[loclen])
			return p;

	const char *catname = catnames[category];
	size_t catlen = strlen(catname);
	const char *modname = memchr(locname, '@', loclen);
	if (!modname) modname = locname + loclen;
	size_t modlen = loclen - (size_t)(modname - locname);
	size_t alt_modlen = modlen;
	size_t baselen = (size_t)(modname - locname);
	const char *csp = memchr(locname, '.', baselen);
	if (csp) baselen = (size_t)(csp - locname);

	char name[q->dirlen + 1 + loclen + 1 + catlen + 1 + domlen + 4];
	const void *map = 0;
	size_t map_size = 0;
	const char *locp;
	for (;;) {
		snprintf(name, sizeof name, "%s/%.*s%.*s/%s/%s.mo",
			q->dirname, (int)baselen, locname,
			(int)alt_modlen, modname, catname, domainname);
		if ((map = map_file(name, &map_size))) break;
		if (alt_modlen) {
			alt_modlen = 0;
		} else if ((locp = memchr(locname, '_', baselen))) {
			baselen = (size_t)(locp - locname);
			alt_modlen = modlen;
		} else {
			break;
		}
	}

	p = calloc(sizeof *p + loclen + 1, 1);
	if (!p) {
		if (map) munmap((void *)map, map_size);
		return 0;
	}
	p->binding = q;
	p->cat = category;
	memcpy(p->locname, locname, loclen);
	p->map = map;
	p->map_size = map_size;

	const char *rule = "n!=1;";
	unsigned long np = 2;
	const char *r = map ? __mo_lookup(map, map_size, "") : 0;
	char *z;
	while (r && strncmp(r, "Plural-Forms:", 13)) {
		z = strchr(r, '\n');
		r = z ? z+1 : 0;
	}
	if (r) {
		r += 13;
		while (isspace((unsigned char)*r)) r++;
		if (!strncmp(r, "nplurals=", 9)) {
			np = strtoul(r+9, &z, 10);
			r = z;
		}
		while (*r && *r != ';') r++;
		if (*r) {
			r++;
			while (isspace((unsigned char)*r)) r++;
			if (!strncmp(r, "plural=", 7))
				rule = r+7;
		}
	}
	p->nplurals = np;
	p->plural_rule = rule;

	pthread_mutex_lock(&lock);
	for (struct msgcat *o=cats; o; o=o->next) {
		if (o->binding == q && o->cat == category && !strcmp(o->locname, p->locname)) {
			pthread_mutex_unlock(&lock);
			if (map) munmap((void *)map, map_size);
			free(p);
			return o;
		}
	}
	p->next = cats;
	__atomic_store_n(&cats, p, __ATOMIC_RELEASE);
	pthread_mutex_unlock(&lock);
	return p;
}

static const char *translate(struct msgcat *p, const char *msgid1, const char *msgid2, unsigned long n)
{
	if (!p || !p->map) return 0;
	const char *trans = __mo_lookup(p->map, p->map_size, msgid1);
	if (!trans) return 0;
	if (msgid2 && p->nplurals) {
		unsigned long plural = __pleval(p->plural_rule, n);
		if (plural >= p->nplurals) return 0;
		while (plural--) {
			size_t rem = p->map_size - (size_t)(trans - (const char *)p->map);
			size_t l = strnlen(trans, rem);
			if (l+1 >= rem) return 0;
			trans += l+1;
		}
	}
	return trans;
}

char *dcngettext(const char *domainname, const char *msgid1, const char *msgid2, unsigned long int n, int category)
{
	int old_errno = errno;
	const char *trans = 0;

	if (!msgid1) goto notrans;
	if (category < 0 || (size_t)category >= sizeof catnames / sizeof *catnames || !catnames[category])
		goto notrans;
	if (!domainname) domainname = gettextdomain();
	size_t domlen = strnlen(domainname, NAME_MAX+1);
	if (!domlen || domlen > NAME_MAX) goto notrans;

	const char *locname = setlocale(category, 0);
	if (!locname || !*locname || !strcmp(locname, "POSIX") ||
	    (locname[0] == 'C' && (!locname[1] || locname[1] == '.')))
		goto notrans;

	struct binding *q = find_binding(domainname);
	if (!q) {
		bindtextdomain(domainname, DEFAULT_LOCALEDIR);
		q = find_binding(domainname);
		if (!q) goto notrans;
	}

	const char *list = getenv("LANGUAGE");
	if (list && *list) {
		while (*list && !trans) {
			const char *end = strchr(list, ':');
			size_t len = end ? (size_t)(end - list) : strlen(list);
			if (len && len <= NAME_MAX)
				trans = translate(load_cat(q, list, len, category, domainname, domlen), msgid1, msgid2, n);
			list += len;
			if (*list == ':') list++;
		}
	} else {
		trans = translate(load_cat(q, locname, strlen(locname), category, domainname, domlen), msgid1, msgid2, n);
	}
	if (trans) {
		errno = old_errno;
		return (char *)trans;
	}

notrans:
	errno = old_errno;
	return (char *)((n == 1 || !msgid2) ? msgid1 : msgid2);
}

char *dcgettext(const char *domainname, const char *msgid, int category)
{
	return dcngettext(domainname, msgid, 0, 1, category);
}

char *dngettext(const char *domainname, const char *msgid1, const char *msgid2, unsigned long int n)
{
	return dcngettext(domainname, msgid1, msgid2, n, LC_MESSAGES);
}

char *dgettext(const char *domainname, const char *msgid)
{
	return dcngettext(domainname, msgid, 0, 1, LC_MESSAGES);
}

char *gettext(const char *msgid)
{
	return dgettext(0, msgid);
}

char *ngettext(const char *msgid1, const char *msgid2, unsigned long int n)
{
	return dngettext(0, msgid1, msgid2, n);
}
