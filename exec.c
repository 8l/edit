/* acme snake oil */

#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <time.h>
#include <unistd.h>

#include "unicode.h"
#include "buf.h"
#include "edit.h"
#include "gui.h"
#include "win.h"
#include "exec.h"
#include "evnt.h"

extern W *curwin;
void die(char *);

typedef struct ecmd ECmd;
struct ecmd {
	char *name;
	int (*f)(W *, EBuf *, unsigned);
};

static ECmd *lookup(Buf *, unsigned, unsigned *);
static unsigned skipb(Buf *, unsigned, int);
static int get(W *, EBuf *, unsigned);
static int put(W *, EBuf *, unsigned);
static int look(W *, EBuf *, unsigned);
static int run(W *, EBuf *, unsigned);
static int new(W *, EBuf *, unsigned);
static int del(W *, EBuf *, unsigned);

static char *errstr;
static ECmd etab[] = {
	{ "Get", get },
	{ "Put", put },
	{ "Look", look },
	{ "New", new },
	{ "Del", del },
	{ 0, run },
};

/* ex_run - Execute a command in the current window at
 * position [p0].  The command is first searched among
 * the list of builtins, if not found, it is run in a shell.
 */
int
ex_run(W *w, unsigned p0)
{
	unsigned p1;
	ECmd *e;

	e = lookup(&w->eb->b, p0, &p1);
	if (e && e->f(win_text(w), w->eb, p1))
	if (w == curwin && win_text(w) != w)
		curwin = win_tag_toggle(w);
	return 0;
}

/* ex_look - Look for a string [s] in window [w] and jump
 * to the first match after the cursor position.  The caller
 * is responsible to free the [s] buffer.
 */
int
ex_look(W *w, Rune *s, unsigned n)
{
	unsigned p;

	if (n == 0)
		return 1;
	p = eb_look(w->eb, w->cu+1, s, n);
	if (p == -1u)
		p = eb_look(w->eb, 0, s, n);
	if (p != -1u) {
		w->cu = p;
		eb_setmark(w->eb, SelBeg, p);
		eb_setmark(w->eb, SelEnd, p+n);
		return 0;
	} else {
		errstr = "no match";
		return 1;
	}
}

/* ex_get - Load the file [file] in the window [w].  See ex_put
 * for more information on the path argument.
 */
int
ex_get(W *w, char *file)
{
	int fd;
	struct stat st;
	char *file1, *p;
	EBuf *eb;
	long ln;

	ln = 1;
	eb = w->eb;
	if (!file)
		file = eb->path;
	if (!file) {
		errstr = "no file to read from";
		return 1;
	}
	if ((p = strchr(file, ':'))) {
		*p = 0;
		ln = strtol(p+1, 0, 10);
		if (ln > INT_MAX || ln < 0)
			ln = 0;
	}
	if (eb->path && strcmp(eb->path, file) != 0)
	if (eb->frev != eb_revision(eb)) {
		errstr = "file not written";
		return 1;
	}
	fd = open(file, O_RDONLY);
	if (fd == -1) {
		errstr = "cannot open file";
		return 1;
	}
	file1 = malloc(strlen(file)+1);
	assert(file1);
	strcpy(file1, file);
	free(eb->path);
	eb->path = 0;
	eb_clr(eb, fd);
	close(fd);
	stat(file1, &st);
	eb->path = file1;
	eb->ftime = st.st_mtime;
	eb->frev = eb_revision(eb);
	w->cu = buf_setlc(&eb->b, ln-1, 0);
	return 0;
}

/* ex_put - Save the buffer [eb] in the file [file].  If [file] is
 * null the buffer path is used. One is returned if an error occurs
 * and zero is returned otherwise.  The caller is responsible to
 * free the [file] buffer.
 */
int
ex_put(EBuf *eb, char *file)
{
	int fd;
	struct stat st;

	if (file) {
		if (stat(file, &st) != -1) {
			errstr = "file exists";
			return 1;
		}
	} else {
		file = eb->path;
		if (!file) {
			errstr = "no file to write to";
			return 1;
		}
		if (stat(file, &st) != -1)
		if (st.st_mtime > eb->ftime) {
			errstr = "file changed on disk";
			return 1;
		}
	}
	fd = open(file, O_TRUNC|O_WRONLY|O_CREAT, 0644);
	if (fd == -1) {
		errstr = "cannot open file";
		return 1;
	}
	eb_write(eb, fd);
	close(fd);
	if (eb->path == 0) {
		eb->path = malloc(strlen(file)+1);
		assert(eb->path);
		strcpy(eb->path, file);
	}
	if (strcmp(eb->path, file) == 0) {
		stat(file, &st);
		eb->ftime = st.st_mtime;
		eb->frev = eb_revision(eb);
	}
	return 0;
}


/* static functions */

static void
err(EBuf *eb, unsigned p0, char *e)
{
	p0 = buf_eol(&eb->b, p0) + 1;
	while (*e)
		eb_ins(eb, p0++, *e++);
	eb_ins(eb, p0, '\n');
}

static int
risblank(Rune r)
{
	return risascii(r) && isblank(r);
}

static unsigned
skipb(Buf *b, unsigned p, int dir)
{
	assert(dir == -1 || dir == +1);
	while (risblank(buf_get(b, p)))
		p += dir;
	return p;
}

static ECmd *
lookup(Buf *b, unsigned p0, unsigned *p1)
{
	Rune r;
	char *s;
	ECmd *e;

	p0 = skipb(b, buf_bol(b, p0), +1);
	for (e = etab; (s = e->name); e++) {
		*p1 = p0;
		do {
			r = buf_get(b, *p1);
			if (!*s && (risblank(r) || r == '\n')) {
				*p1 = skipb(b, *p1, +1);
				return e;
			}
			(*p1)++;
		} while (r == (Rune)*s++);
	}
	*p1 = p0;
	return e;
}

static char *
buftobytes(Buf *b, unsigned p0, unsigned p1, unsigned *sz)
{
	unsigned char *s, *t;
	unsigned n, p;

	n = 0;
	for (p=p0; p<p1; p++)
		n += utf8_rune_len(buf_get(b, p));
	if (sz)
		*sz = n;
	s = malloc(n+1);
	assert(s);
	for (t=s, p=p0; p<p1; p++)
		t += utf8_encode_rune(buf_get(b, p), t, 8); /* XXX 8 */
	*t = 0;
	return (char *)s;
}


/* builtin commands */

static int
get(W *w, EBuf *eb, unsigned p0)
{
	char *f;
	unsigned p1;
	int e;

	f = 0;
	p1 = 1 + skipb(&eb->b, buf_eol(&eb->b, p0) - 1, -1);
	if (p0 < p1)
		f = buftobytes(&eb->b, p0, p1, 0);
	e = ex_get(w, f);
	free(f);
	if (e) {
		err(eb, p0, errstr);
		return 0;
	}
	return 1;
}

static int
put(W *w, EBuf *eb, unsigned p0)
{
	char *f;
	int e;
	unsigned p1;

	f = 0;
	p1 = 1 + skipb(&eb->b, buf_eol(&eb->b, p0) - 1, -1);
	if (p0 < p1)
		f = buftobytes(&eb->b, p0, p1, 0);
	e = ex_put(w->eb, f);
	free(f);
	if (e) {
		err(eb, p0, errstr);
		return 0;
	}
	return 1;
}

static int
look(W *w, EBuf *eb, unsigned p0)
{
	YBuf b = {0,0,0,0};
	int e;
	unsigned p1;

	if (buf_get(&eb->b, p0) == '\n')
		return 0;
	p1 = 1 + skipb(&eb->b, buf_eol(&eb->b, p0) - 1, -1);
	eb_yank(eb, p0, p1, &b);
	e = ex_look(w, b.r, b.nr);
	free(b.r);
	if (e) {
		err(eb, p0, errstr);
		return 0;
	}
	return 1;
}

static int
new(W *w, EBuf *eb, unsigned p0)
{
	w = win_new(eb_new());
	if (w)
		curwin = win_tag_toggle(w);
	else
		err(eb, p0, "no more windows");
	return 0;
}

static int
del(W *w, EBuf *eb, unsigned p0)
{
	w = win_kill(w);
	if (w)
		curwin = w;
	else
		err(eb, p0, "last window");
	return 0;
}

typedef struct run Run;
struct run {
	EBuf *eb;      /* 0 if no more to read */
	unsigned p;    /* insertion point in eb */
	unsigned ins;  /* numbers of runes written in eb */
	char *ob;      /* input to the command, 0 if none */
	unsigned no;   /* number of bytes in the ob array */
	unsigned snt;  /* number of bytes sent */
	char in[8];    /* input buffer for partial utf8 sequences XXX 8 */
	unsigned nin;  /* numbers of bytes in the in array */
};

static int
runev(int fd, int flag, void *data)
{
	Run *rn;
	int n, dec;
	unsigned char buf[2048], *p;
	Rune r;

	rn = data;
	n = rn->eb->refs;
	if (n < 0) {
		/* zombie buffer */
		rn->eb->refs++;
/* puts("tick"); */
		if (n == -1)
/* puts("tock!"), */
			free(rn->eb);
		rn->eb = 0;
		close(fd);
		goto Reset;
	}
	if (flag & ERead) {
		assert(rn->eb);
		memcpy(buf, rn->in, rn->nin);
		n = read(fd, &buf[rn->nin], sizeof buf - rn->nin);
		if (n <= 0) {
			close(fd);
			rn->eb->refs--;
			rn->eb = 0;
			goto Reset;
		}
		p = buf;
		while ((dec = utf8_decode_rune(&r, p, n))) {
			eb_ins(rn->eb, rn->p + rn->ins++, r);
			p += dec;
			n -= dec;
		}
		assert((unsigned)n <= sizeof rn->in);
		rn->nin = n;
		memcpy(rn->in, p, n);
		eb_setmark(rn->eb, SelBeg, rn->p);
		eb_setmark(rn->eb, SelEnd, rn->p + rn->ins);
		eb_commit(rn->eb);
		win_redraw_frame();
	}
	if (flag & EWrite) {
		assert(rn->ob);
		n = write(fd, &rn->ob[rn->snt], rn->no - rn->snt);
		rn->snt += n;
		if (n < 0 || rn->snt == rn->no) {
			close(fd);
			free(rn->ob);
			rn->ob = 0;
			goto Reset;
		}
	}
	return 0;
Reset:
	if (rn->eb == 0 && rn->ob == 0)
		free(rn);
	return 1;
}

static int
run(W *w, EBuf *eb, unsigned p0)
{
	unsigned p1, eol, s0, s1;
	char *argv[4], *cmd, ctyp;
	int pin[2], pout[2];
	Run *r;

	eol = buf_eol(&eb->b, p0);
	p1 = 1 + skipb(&eb->b, eol-1, -1);
	if (p1 == p0)
		return 0;
	cmd = buftobytes(&eb->b, p0, p1, 0);
	ctyp = cmd[0];
	if (!strchr("<>|", ctyp))
		ctyp = 0;
	else {
		s0 = eb_getmark(w->eb, SelBeg);
		s1 = eb_getmark(w->eb, SelEnd);
		if (s1 <= s0 || s0 == -1u || s1 == -1u)
			s0 = s1 = w->cu;
	}
	pipe(pin);
	pipe(pout);
	if (!fork()) {
		close(pin[1]);
		close(pout[0]);
		argv[0] = "/bin/sh";
		argv[1] = "-c";
		argv[2] = &cmd[ctyp ? 1 : 0];
		argv[3] = 0;
		/* XXX do not leak file descriptors */
		dup2(pin[0], 0);
		dup2(pout[1], 1);
		dup2(pout[1], 2);
		execv(argv[0], argv);
		die("cannot exec");
	}
	free(cmd);
	close(pin[0]);
	close(pout[1]);
	r = calloc(1, sizeof *r);
	assert(r);
	switch (ctyp) {
	case '>':
		r->eb = eb;
		r->p = eol+1;
		r->ob = buftobytes(&w->eb->b, s0, s1, &r->no);
		break;
	case '<':
		r->eb = w->eb;
		r->p = s0;
		r->ob = 0;
		eb_del(w->eb, s0, s1);
		break;
	case '|':
		r->eb = w->eb;
		r->p = s0;
		r->ob = buftobytes(&w->eb->b, s0, s1, &r->no);
		eb_del(w->eb, s0, s1);
		break;
	case 0:
		r->eb = eb;
		r->p = eol+1;
		r->ob = 0;
		break;
	default:
		abort();
	}
	r->eb->refs++;
	if (ctyp != 0 && ctyp != '>')
	if (s0 != s1) {
		eb_setmark(w->eb, SelBeg, -1u); /* clear selection */
		eb_setmark(w->eb, SelEnd, -1u);
	}
	eb_commit(w->eb);
	if (r->ob)
		ev_register((Evnt){pin[1], EWrite, runev, r});
	else
		close(pin[1]);
	ev_register((Evnt){pout[0], ERead, runev, r});

	return 0;
}
