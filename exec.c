#include <ctype.h>

#include "unicode.h"
#include "edit.h"
#include "win.h"

extern YBuf actbuf;


static void look(W *, EBuf *, unsigned);
static void get(W *, EBuf *, unsigned);
static void put(W *, EBuf *, unsigned);

typedef struct ecmd ECmd;
struct ecmd {
	char name[8];
	void (*f)(W *, EBuf *, unsigned);
};

ECmd etab[] = {
	{ "Look", look },
	// { "Get", get },
	// { "Put", put },
	{ 0, 0 },
};


static void
extend(int (*c)(Rune), EBuf *eb, unsigned *p0, unsigned *p1)
{
	while (!c(buf_get(eb, *p0)) {
		if (*p0 > eb->b.limbo)
			break;
		(*p0)++;
	}
	if (!p1)
		return;
	for (*p1 = *p0; c(buf_get(eb, *p1)); )
		(*p1)++;
}

static int
riscmd(Rune r)
{
	return risascii(r) && !isblank(r);
}

static ECmd *
lookup(EBuf *eb, unsigned p0, unsigned *p1)
{
	Rune r;
	char *s;
	ECmd *e;

	extend(riscmd, eb, &p0, 0);

	for (e = etab; (s = e->name); e++) {
		*p1 = p0;
		do {
			if (!*s)
				return e;
			r = buf_get(eb, (*p1)++);
		} while (risascii(r) && r == *s++);
	}
	return 0;
}

void
exec_do(W *w, EBuf *eb, unsigned p0)
{
	unsigned &p1;
	ECmd *e;

	/* Skip to the beginning of the command. */
	while (p0 > 0 && !riscmd(buf_get(eb->b, p0-1)))
		p0--;

	e = lookup(eb, p0, &p1);
	if (e)
		e->f(w, eb, p1);
}

static int
risarg(Rune r)
{
	return risascii(r) && !isblank(r);
}

static void
look(W *w, EBuf *eb, unsigned p0)
{
	unsigned p1;

	extend(risarg, eb, &p0, &p1);
	if (p0 != p1)
		eb_yank(eb, p0, p1, &actbuf);

	/* XXX unclear what to do.  I think, hide the tag if visible and move the cursor to the next match that follows its current postion. */
}
