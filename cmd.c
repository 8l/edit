#include <assert.h>
#include <ctype.h>
#include <limits.h>
#include <string.h>
#include <stdio.h>

#include "unicode.h"
#include "edit.h"
#include "gui.h"
#include "win.h"
#include "cmd.h"

extern int exiting;
extern W *curwin;

enum {
	Command = 'c',
	Insert = 'i',
};

struct cmd {
	unsigned short count;
	unsigned char c, arg;
};

enum {
	CDouble = 1,               /* expect a doubled char ('[') */
	CMotion = CDouble << 1,    /* expect a motion argument */
	CArgument = CMotion << 1,  /* expect an argument */
};

static struct { int flags; } cmds[] = {
	['d'] = { CMotion },
	['g'] = { CDouble },
	['m'] = { CArgument },
	['['] = { CDouble },
	['\''] = { CArgument },
	['~'] = { 0 } /* keep it */
};

static int mode = Command;

static int insert(Rune r);
static int motion(struct cmd *c, unsigned *dst, int *linewise);
static unsigned mvnext(Buf *b, unsigned cu, int in(Rune), int end);
static unsigned mvprev(Buf *b, unsigned cu, int in(Rune));
static void perform(char buf, struct cmd *c, struct cmd *m);
static int risalpha(Rune r);
static int risascii(Rune r);
static int risbigword(Rune r);
static int risreg(Rune r);
static int risword(Rune r);

void
cmd_parse(Rune r)
{
	static char buf;
	static struct cmd c, m, *p = &c;
	static enum {
		SBuf0, SBuf1,
		SCmd,
		SDbl,
		SArg,
	} state;


	if (mode == Insert) {
		mode = insert(r);
		return;
	}

	if (r == GKEsc)
		goto reset;

	switch (state) {
	case SBuf1:
		if (!risreg(r))
			goto error;
		buf = r;
		state = SCmd;
		p = &c;
		break;

	case SBuf0:
		if (r == '"') {
			state = SBuf1;
			break;
		}

		state = SCmd;
		p = &c;
		/* fall thru */

	case SCmd:
		if (!risascii(r))
			goto error;

		if (r != '0' || p->count != 0)
		if (isdigit((unsigned char)r)) {
			p->count *= 10;
			p->count += r-'0';
			break;
		}

		p->c = r;
		if (cmds[p->c].flags & CDouble) {
			state = SDbl;
			break;
		}

	gotdbl:
		if (cmds[p->c].flags & CArgument) {
			state = SArg;
			break;
		}

	gotarg:
		if (cmds[p->c].flags & CMotion) {
			assert(p == &c);
			p = &m;
			state = SCmd;
			break;
		}

		perform(buf, &c, &m);
		goto reset;

	case SDbl:
		if (r != p->c)
			goto error;

		goto gotdbl;

	case SArg:
		if (r > 127)
			goto error;

		p->arg = r;
		goto gotarg;
	}

	return;

error:
	puts("erroneous command");
reset:
	buf = 0;
	memset(&c, 0, sizeof c);
	memset(&m, 0, sizeof m);
	state = SBuf0;
}

/* static functions */

static int
insert(Rune r)
{
	EBuf *eb;

	eb = curwin->eb;

	if (r == GKEsc) {
		if (curwin->cu > 0)
			curwin->cu--;
		eb_clean(eb);
		return Command;
	}

	if (r == GKBackspace) {
		if (curwin->cu > 0) {
			eb_del(eb, curwin->cu-1, curwin->cu);
			curwin->cu--;
		}
	} else
		eb_ins(eb, curwin->cu++, r);

	return Insert;
}

static int
motion(struct cmd *c, unsigned *pcu, int *linewise)
{
	char *lw = "jk\'{}[]";
	Buf *b;
	unsigned cu;
	int line, col;

	b = &curwin->eb->b;
	cu = *pcu;
	buf_getlc(b, cu, &line, &col);

	if (!c->count)
		c->count = 1;

	switch (c->c) {

	/* base motions */
	case 'h':
		cu = buf_setlc(b, line, col-c->count);
		break;

	case 'j':
		cu = buf_setlc(b, line+c->count, col);
		break;

	case 'k':
		cu = buf_setlc(b, line-c->count, col);
		break;

	case ' ':
	case 'l':
		cu = buf_setlc(b, line, col+c->count);
		break;

	/* word motions */
	case 'w':
		while (c->count--)
			cu = mvnext(b, cu, risword, 0);
		break;

	case 'e':
		while (c->count--)
			cu = mvnext(b, cu, risword, 1);
		break;

	case 'W':
		while (c->count--)
			cu = mvnext(b, cu, risbigword, 0);
		break;

	case 'E':
		while (c->count--)
			cu = mvnext(b, cu, risbigword, 1);
		break;

	case 'b':
		while (c->count--)
			cu = mvprev(b, cu, risword);
		break;

	case 'B':
		while (c->count--)
			cu = mvprev(b, cu, risbigword);
		break;

	/* other line motions */
	case '0':
		cu = buf_setlc(b, line, 0);
		break;

	case '$': {
		unsigned eol;

		eol = buf_eol(b, cu);
		if (eol != cu)
			cu = eol-1;
		break;
	}

	case '`':
	case '\'':
		break;

	default:
		return 0;
	}

	if (linewise)
		*linewise = strchr(lw, c->c) != 0;
	*pcu = cu;
	return 1;
}

static unsigned
mvnext(Buf *b, unsigned cu, int in(Rune), int end)
{
	int st, nx, i;
	Rune r;

	assert(end == 0 || end == 1);
	//if (end && buf_get(b, cu) == '\n')
		//return cu;

	i = 0;
	nx = in(buf_get(b, cu-- + end));
	do {
		st = nx;
		r = buf_get(b, ++cu + end);
		//if (r == '\n')
			//break;
		nx = in(r);
		i += (nx != st);
	} while (i<2 && (nx==end || i==0));

	return cu;
}

static unsigned // later we can just use regexps!
mvprev(Buf *b, unsigned cu, int in(Rune))
{
	for (; cu && !in(buf_get(b, cu-1)); cu--)
		;
	for (; cu && in(buf_get(b, cu-1)); cu--)
		;

	return cu;
}

static void
perform(char buf, struct cmd *c, struct cmd *m)
{
	if (0) {
		printf("performing");
		if (buf) printf(" on buffer %c", buf);
		printf(" CNT %d CMD %c", c->count, c->c);
		if (c->arg) printf(" ARG %c", c->arg);
		if (m->c) {
			printf(" with motion");
			printf(" CNT %d CMD %c", m->count, m->c);
			if (m->arg) printf(" ARG %c", m->arg);
		}
		printf("\n");
	}

	if (motion(c, &curwin->cu, 0))
		return;

	static int u;

	switch (c->c) {
	case 'q'-'a' + 1:
		exiting = 1;
		break;
	case 'i':
		mode = Insert;
		break;
	case 'u':
		u = !u;
	case '.':
		eb_undo(curwin->eb, u);
		break;
	}
}

static int
risalpha(Rune r)
{
	/* stupid home brewed latin detection */
	return (risascii(r) && isalpha((unsigned char)r))
	    || (r >= 0xc0 && r < 0x100);
}

static int
risascii(Rune r)
{
	return r <= '~';
}

static int
risbigword(Rune r)
{
	return !risascii(r) || !isspace((unsigned char)r);
}

static int
risreg(Rune r)
{
	return risascii(r) && islower((unsigned char)r);
}

static int
risword(Rune r)
{
	return risalpha(r)
	    || (r >= '0' && r <= '9')
	    || r == '_';
}
