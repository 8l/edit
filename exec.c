/* acme snake oil */

#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unicode.h"
#include "edit.h"
#include "win.h"
#include "exec.h"
#include "evnt.h"

void die(char *);

typedef struct ecmd ECmd;
struct ecmd {
	char *name;
	int (*f)(W *, EBuf *, unsigned);
};

static ECmd *lookup(Buf *, unsigned, unsigned *);
static unsigned skipb(Buf *, unsigned, int);
static int get(W *, EBuf *, unsigned);
static int look(W *, EBuf *, unsigned);
static int run(W *, EBuf *, unsigned);

ECmd etab[] = {
	{ "Get", get },
	{ "Look", look },
	{ 0, run },
};


/* ex_run - Execute a command in the current window at
 * position [p0].  The command is first searched among
 * the list of builtins, if not found, it is run in a shell.
 */
int
ex_run(unsigned p0)
{
	extern W *curwin;
	unsigned p1;
	ECmd *e;

	e = lookup(&curwin->eb->b, p0, &p1);
	if (e && e->f(win_text(curwin), curwin->eb, p1))
	if (win_text(curwin) != curwin)
		curwin = win_tag_toggle(curwin);
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
	}
	return p == -1u;
}


/* static functions */

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
		} while (risascii(r) && r == (Rune)*s++);
	}
	*p1 = p0;
	return e;
}

static char *
buftobytes(Buf *b, unsigned p0, unsigned p1)
{
	char *s;
	unsigned char *t;
	unsigned n, p;

	n = 0;
	for (p=p0; p<p1; p++)
		n += utf8_rune_len(buf_get(b, p));
	s = malloc(n+1);
	assert(s);
	for (t=(unsigned char *)s, p=p0; p<p1; p++)
		t += utf8_encode_rune(buf_get(b, p), t, 8); /* XXX 8 */
	*t = 0;
	return s;
}


/* builtin commands */

static int
get(W *w, EBuf *eb, unsigned p0)
{
	char *f, *p;
	unsigned p1;
	long ln;

	ln = 1;
	p1 = 1 + skipb(&eb->b, buf_eol(&eb->b, p0) - 1, -1);
	if (p0 < p1) {
		f = buftobytes(&eb->b, p0, p1);
		if ((p = strchr(f, ':'))) {
			*p = 0;
			ln = strtol(p+1, 0, 10);
			if (ln > INT_MAX || ln < 0)
				ln = 0;
		}
	} else
		f = w->eb->path;

	if (eb_read(w->eb, f) == 0) {
		w->cu = buf_setlc(&w->eb->b, ln-1, 0);
		return 1;
	} else
		return 0;
}

static int
look(W *w, EBuf *eb, unsigned p0)
{
	YBuf b = {0,0,0,0};
	unsigned p1;

	if (buf_get(&eb->b, p0) == '\n')
		return 0;
	p1 = 1 + skipb(&eb->b, buf_eol(&eb->b, p0) - 1, -1);
	eb_yank(eb, p0, p1, &b);
	ex_look(w, b.r, b.nr);
	free(b.r);
	return 1;
}

typedef struct Run Run;
struct Run {
	EBuf *eb;	/* 0 if no more to read */
	unsigned p;	/* write offset in eb */
	char *ob;	/* nul terminated input to the command, 0 if none */
	unsigned snt;	/* number of bytes sent */
	unsigned nin;	/* numbers of bytes in the in array */
	char in[8];	/* input buffer for partial utf8 sequences XXX 8 */
};

static int
runev(int fd, int flag, void *data)
{
	Run *rn;
	int n, dec;
	unsigned char buf[2048], *p;
	unsigned p0;
	Rune r;

	rn = data; /* XXX rn->eb can be invalid */
	if (flag & ERead) {
		assert(rn->eb);
		memcpy(buf, rn->in, rn->nin);
		n = read(fd, &buf[rn->nin], sizeof buf - rn->nin);
		if (n <= 0) {
			close(fd);
			rn->eb = 0;
			goto Reset;
		}
		p = buf;
		p0 = rn->p;
		while ((dec = utf8_decode_rune(&r, p, n))) {
			buf_ins(&rn->eb->b, rn->p++, r);
			p += dec;
			n -= dec;
		}
		assert((unsigned)n <= sizeof rn->in);
		rn->nin = n;
		memcpy(rn->in, p, n);
		eb_setmark(rn->eb, SelBeg, p0);
		eb_setmark(rn->eb, SelEnd, rn->p);
		win_redraw_frame();
	}
	if (flag & EWrite) {
		close(fd);
		free(rn->ob);
		rn->ob = 0;
		goto Reset;
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
	int pin[2], pout[2], perr[2];
	Run *r;

	/* ***
	clear (and possibly delete) selection,
	get the "insertion" position and set a mark for it in the
	edit buffer (Acme does not do this, it just stores an offset)

	what happens when eb is deleted/changed
	during the command execution?
	+	refcount ebs and make eb_free free the
			data and have eb contain simply the refcount
	+	when a dummy eb is detected in the callback,
			just abort the IO operation
	*** */

	eol = buf_eol(&eb->b, p0);
	p1 = 1 + skipb(&eb->b, eol-1, -1);
	if (p1 == p0)
		return 0;
	cmd = buftobytes(&eb->b, p0, p1);
	ctyp = cmd[0];
	if (strchr("<>|", ctyp))
		cmd++;
	else
		ctyp = 0;
	if (ctyp != 0) {
		s0 = eb_getmark(w->eb, SelBeg);
		s1 = eb_getmark(w->eb, SelEnd);
		if (s1 <= s0 || s0 == -1u || s1 == -1u)
			s0 = s1 = w->cu;
	}
	pipe(pin);
	pipe(pout);
	pipe(perr);
	if (!fork()) {
		close(pin[1]);
		close(pout[0]);
		close(perr[0]);
		argv[0] = "/bin/sh";
		argv[1] = "-c";
		argv[2] = cmd;
		argv[3] = 0;
		/* XXX do not leak file descriptors */
		dup2(pin[0], 0);
		dup2(pout[1], 1);
		dup2(perr[1], 2);
		execv(argv[0], argv);
		die("cannot exec");
	}
	close(pin[0]);
	close(pout[1]);
	close(perr[1]);
	r = calloc(1, sizeof *r);
	assert(r);
	switch (ctyp) {
	case '>':
		r->eb = eb;
		r->p = eol+1;
		r->ob = buftobytes(&w->eb->b, s0, s1);
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
		r->ob = buftobytes(&w->eb->b, s0, s1);
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
	if (ctyp != 0)
	if (s0 != s1) {
		eb_setmark(w->eb, SelBeg, -1u); /* clear selection */
		eb_setmark(w->eb, SelEnd, -1u);
	}
	eb_commit(w->eb);
	if (r->ob)
		ev_register((E){pin[1], EWrite, runev, r});
	else
		close(pin[1]);
	ev_register((E){pout[0], ERead, runev, r});
	close(perr[0]); /* XXX errors ignored... */

	return 0;
}
