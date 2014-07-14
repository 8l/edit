#include <stdlib.h>
#include <ctype.h>

#include "unicode.h"
#include "edit.h"
#include "win.h"
#include "exec.h"

typedef struct ecmd ECmd;
struct ecmd {
	char *name;
	int (*f)(W *, EBuf *, unsigned);
};

static ECmd *lookup(EBuf *, unsigned, unsigned *);
static int look(W *, EBuf *, unsigned);

ECmd etab[] = {
	{ "Look", look },
	{ 0, 0 },
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

	e = lookup(curwin->eb, p0, &p1);
	if (e && e->f(win_text(curwin), curwin->eb, p1))
	if (win_text(curwin) != curwin)
		curwin = win_tag_toggle(curwin);

	return e != 0;
}

/* ex_look - Look for a string [s] in window [w] and jump
 * to the first match after the cursor position.  If a null pointer
 * is given for [s], the string searched is the current
 * selection.  The caller is responsible to free the [s] buffer.
 */
int
ex_look(W *w, Rune *s, unsigned n)
{
	YBuf lb = {s,n,n,0};
	unsigned s0, s1, p;

	if (!s) {
		s0 = eb_getmark(w->eb, SelBeg);
		s1 = eb_getmark(w->eb, SelEnd);
		if (s0 > s1 || s0 == -1u || s1 == -1u)
			return 1;
		eb_yank(w->eb, s0, s1, &lb);
	}

	p = eb_look(w->eb, w->cu+1, lb.r, lb.nr);
	if (p == -1u)
		p = eb_look(w->eb, 0, lb.r, lb.nr);

	if (p != -1u) {
		w->cu = p;
		eb_setmark(w->eb, SelBeg, p);
		eb_setmark(w->eb, SelEnd, p+lb.nr);
	}

	if (!s)
		free(lb.r);
	return p == -1u;
}


/* static functions */

static int
riscmd(Rune r)
{
	return risascii(r) && !isblank(r);
}

static int
risarg(Rune r)
{
	return risascii(r) && !isspace(r);
}

static void
extend(int (*c)(Rune), Buf *b, unsigned *p0, unsigned *p1)
{
	while (!c(buf_get(b, *p0))) {
		if (*p0 > b->limbo)
			break;
		(*p0)++;
	}
	if (!p1)
		return;
	for (*p1 = *p0; c(buf_get(b, *p1)); )
		(*p1)++;
}

static ECmd *
lookup(EBuf *eb, unsigned p0, unsigned *p1)
{
	Rune r;
	char *s;
	ECmd *e;

	p0 = buf_bol(&eb->b, p0);
	extend(riscmd, &eb->b, &p0, 0);

	for (e = etab; (s = e->name); e++) {
		*p1 = p0;
		do {
			if (!*s)
				return e;
			r = buf_get(&eb->b, (*p1)++);
		} while (risascii(r) && r == (Rune)*s++);
	}
	return 0;
}


/* builtin commands */

static int
look(W *w, EBuf *eb, unsigned p0)
{
	YBuf yb = {0,0,0,0};
	unsigned p1;

	extend(risarg, &eb->b, &p0, &p1);
	if (p0 < p1) {
		eb_yank(eb, p0, p1, &yb);
		ex_look(w, yb.r, yb.nr);
		free(yb.r);
	} else
		ex_look(w, 0, 0);

	return 1;
}
