/*% clang -Wall -g -DTEST -std=c99 obj/{unicode.o,buf.o} % -o #
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "unicode.h"
#include "buf.h"
#include "edit.h"

enum {
	MaxBuf = 4,	/* maximal string stored in rbuf */
	YankSize = 128,	/* initial size of a yank buffer */
};

struct mark {
	Rune r;		/* mark name */
	unsigned p;	/* mark position */
	Mark *next;
};

struct log {
	enum {
		Insert = '+',
		Delete = '-',
		Commit = 'c',
	} type;
	unsigned p0;	/* location of the change */
	unsigned np;	/* size of the change */
	Log *next;
	Rune rbuf[];	/* rune buffer (data stored in reverse order) */
};

/* The Undo Log:
 *    Three types of log entries are present.  Insertions, which
 *    only store the range inserted (p0, p0+np).  Deletions storing
 *    the deleted text in the flexible array member rbuf.  And
 *    commits which mark a clean state in the edition sequence.
 *    The changes are chained in a simple linked list structure.
 */

static void pushlog(Log *, int);
static void rebase(Mark **, Log *);
static void puteb(EBuf *, FILE *);
static void putrune(Rune, FILE *);

void
log_insert(Log *l, unsigned p0, unsigned p1)
{
	assert(p0 <= p1);

	if (l->type != Insert || l->p0 + l->np != p0) {
		pushlog(l, Insert);
		l->p0 = p0;
	}

	l->np += p1 - p0;
}

void
log_delete(Log *l, Buf *b, unsigned p0, unsigned p1)
{
	assert(p0 <= p1);

	if (l->type != Delete || l->p0 != p1)
		pushlog(l, Delete);

	while (p0 < p1) {
		if (l->np >= MaxBuf) {
			l->p0 = p1;
			pushlog(l, Delete);
		}
		l->rbuf[l->np++] = buf_get(b, --p1);
	}

	l->p0 = p0;
}

void
log_commit(Log *l)
{
	pushlog(l, Commit);
}

void
log_undo(Log *l, Buf *b, Log *redo, unsigned *pp)
{
	Log *top, *n;
	unsigned p0, p1;

	assert(l->type == Commit);
	assert(redo == 0 || redo->type == Commit);

	top = l;
	l = l->next;
	assert(l);

	while (l->type != Commit) {
		p0 = l->p0;
		p1 = l->p0 + l->np;

		switch (l->type) {
		case Insert:
			if (redo)
				log_delete(redo, b, p0, p1);
			while (p0 < p1)
				buf_del(b, --p1);
			break;
		case Delete:
			if (redo)
				log_insert(redo, p0, p1);
			while (p0 < p1) {
				buf_ins(b, p0, l->rbuf[p1 - p0 - 1]);
				p0++;
			}
			break;
		default:
			abort();
		}
		if (pp)
			*pp = p0;

		n = l->next;
		free(l);
		l = n;
	}

	top->next = l->next;
	free(l);
	if (redo)
		log_commit(redo);
}

Log *
log_new()
{
	Log *l;

	l = malloc(sizeof *l + MaxBuf*sizeof(Rune));
	assert(l);
	l->type = Commit;
	l->p0 = l->np = 0;
	l->next = 0;
	return l;
}

void
log_clr(Log *l)
{
	Log *n;

	assert(l);
	assert(l->type == Commit);

	n = l->next;
	l->next = 0;
	for (l = n; l; l = n) {
		n = l->next;
		free(l);
	}
}

EBuf *
eb_new()
{
	EBuf *eb;

	eb = malloc(sizeof *eb);
	buf_init(&eb->b);
	eb->undo = log_new();
	eb->redo = log_new();
	eb->ml = 0;
	eb->path = 0;
	return eb;
}

void
eb_del(EBuf *eb, unsigned p0, unsigned p1)
{
	log_clr(eb->redo);
	log_delete(eb->undo, &eb->b, p0, p1);
	for (; p0 < p1; p1--)
		buf_del(&eb->b, p1-1);
}

void
eb_ins(EBuf *eb, unsigned p0, Rune r)
{
	log_clr(eb->redo);
	log_insert(eb->undo, p0, p0+1);
	buf_ins(&eb->b, p0, r);
}

int
eb_ins_utf8(EBuf *eb, unsigned p0, unsigned char *data, int len)
{
	Rune r;
	int rd, total;

	total = 0;
	while ((rd = utf8_decode_rune(&r, data, len))) {
		buf_ins(&eb->b, p0++, r);
		data += rd;
		len -= rd;
		total += rd;
	}
	return total;
}

void
eb_commit(EBuf *eb)
{
	log_commit(eb->undo);
	rebase(&eb->ml, eb->undo->next);
}

void
eb_undo(EBuf *eb, int undo, unsigned *pp)
{
	Log *u, *r;

	if (undo)
		u = eb->undo, r = eb->redo;
	else
		u = eb->redo, r = eb->undo;

	if (u->next != 0) {
		log_undo(u, &eb->b, r, pp);
		rebase(&eb->ml, r->next);
	}
}

void
eb_yank(EBuf *eb, unsigned p0, unsigned p1, YBuf *yb)
{
	Rune *pr;

	assert(p0 <= p1);
	assert(yb);

	yb->nr = p1 - p0;

	/* resize the yank buffer if it is
	 * either too big from a previous yank
	 * or too small to contain the data
	 */
	if (yb->nr > yb->sz || yb->sz > YankSize) {
		free(yb->r);
		yb->sz = yb->nr > YankSize ? yb->nr : YankSize;
		yb->r = malloc(yb->sz * sizeof(Rune));
		assert(yb->r);
	}

	for (pr = yb->r; p0 < p1; p0++, pr++)
		*pr = buf_get(&eb->b, p0);
}

void
eb_setmark(EBuf *eb, Rune name, unsigned pos)
{
	Mark *m;

	for (m=eb->ml; m && m->r != name; m=m->next)
		;

	if (m == 0) {
		m = malloc(sizeof *m);
		assert(m);
		m->r = name;
		m->next = eb->ml;
		eb->ml = m;
	}

	m->p = pos;
}

unsigned
eb_getmark(EBuf *eb, Rune name)
{
	Mark *m;

	for (m=eb->ml; m; m=m->next)
		if (m->r == name)
			return m->p;

	return -1u;
}

int
eb_write(EBuf *eb)
{
	FILE *fp = fopen(eb->path, "w");

	if (!fp)
		return -1;
	puteb(eb, fp);
	fclose(fp);
	return 0;
}


/* static functions */

/* pushlog - The invariant preserved is that
 * the topmost log entry has a rune array of
 * MaxBuf runes.
 */
static void
pushlog(Log *log, int type)
{
	size_t sz;
	Log *l;

	assert(log->type != Delete || log->np <= MaxBuf);
	sz = sizeof *l;
	if (log->type == Delete) /* only deletions carry data */
		sz += log->np*sizeof(Rune);
	l = malloc(sz);
	assert(l);
	memcpy(l, log, sz);

	log->type = type;
	log->p0 = log->np = 0;
	log->next = l;
}

static void
rebase(Mark **pm, Log *log)
{
	Mark *m;

	assert(log);

	if (log->type == Commit)
		/* we hit the previous commit, rebase is done */
		return;

	rebase(pm, log->next);

	switch(log->type) {
	case Insert:
		for (m=*pm; m; m=m->next)
			if (m->p >= log->p0)
				m->p += log->np;
		break;
	case Delete:
		while ((m=*pm)) {
			if (m->p >= log->p0) {
				if (m->p < log->p0 + log->np) {
					/* the mark was deleted */
					*pm = m->next;
					free(m);
					continue;
				}
				m->p -= log->np;
			}
			pm = &m->next;
		}
		break;
	default:
		abort();
	}
}

static void
puteb(EBuf *eb, FILE *fp)
{
	enum { Munching, Spitting } state = Munching;
	unsigned munchb = 0, munche = 0, nl = 0;
	Rune r;

	while (munche < eb->b.limbo) switch (state) {

	case Munching:
		r = buf_get(&eb->b, munche);
		if (r == ' ' || r == '\t' || r == '\n') {
			nl += (r == '\n');
			munche++;
			continue;
		}
		for (; munchb < munche; munchb++) {
			r = buf_get(&eb->b, munchb);
			if ((r == ' ' || r == '\t') && nl)
				continue;
			assert(nl == 0 || r == '\n');
			nl -= (r == '\n');
			putrune(r, fp);
		}
		assert(munchb == munche);
		state = Spitting;
		continue;

	case Spitting:
		r = buf_get(&eb->b, munchb);
		if (r == ' ' || r == '\t' || r == '\n') {
			munche = munchb;
			state = Munching;
			assert(nl == 0);
			continue;
		}
		putrune(r, fp);
		munchb++;
		continue;

	}

	putrune('\n', fp); // always terminate file with a newline
}

static void
putrune(Rune r, FILE *fp)
{
	unsigned char uni[16];
	int i, n;

	n = utf8_encode_rune(r, uni, 16);
	for (i=0; i<n; i++)
		putc(uni[i], fp);
}


#ifdef TEST

void
dumplog(Log *l)
{
	int i, n = 0;

	for (; l; l=l->next, ++n)
		switch (l->type) {
		case Insert:
			printf("%02d + (p0=%u np=%u)\n", n, l->p0, l->np);
			break;
		case Delete:
			printf("%02d - (p0=%u np=%u)\n", n, l->p0, l->np);
			printf("\t'");
			for (i=l->np-1; i>=0; --i)
				putrune(l->rbuf[i], stdout);
			printf("'\n");
			break;
		case Commit:
			printf("%02d c\n", n);
			break;
		}

	printf("%02d □\n", n);

}

int
main() {
	char line[1024], *p, *q;
	long int p0, p1, i;
	size_t len;
	EBuf *eb;

	eb = eb_new();

	while (fgets((char *)line, 1024, stdin)) {
		switch (line[0]) {
		case '+':
			p0 = strtol(line+1, &p, 0);
			if (line+1 == p || *p != ' ')
				goto Syntax;
			len = strlen(p+1);
			if (p[len] == '\n') {
				p[len] = 0;
				len--;
			}
			eb_ins_utf8(eb, p0, (unsigned char *)p+1, len);
			break;
		case '-':
			p0 = strtol(line+1, &p, 0);
			if (line+1 == p || *p != ' ')
				goto Syntax;
			p1 = strtol(p+1, &q, 0);
			if (p+1 == q || (*q != '\n' && *q != 0))
				goto Syntax;
			eb_del(eb, p0, p1);
			break;
		case '!':
			if (eb->undo->type != Commit)
				eb_commit(eb);
			eb_undo(eb, 1, 0);
			break;
		case '?':
			dumplog(eb->undo);
			break;
		case 'p':
			i = 0;
			do
				putrune(buf_get(&eb->b, i++), stdout);
			while (buf_get(&eb->b, i-1) != '\n');
			break;
		case 'c':
			eb_commit(eb);
			break;
		case '#':
			break;
		case 'q':
			exit(0);
		default:
			goto Syntax;
		}
		fflush(stdout);
		continue;
	Syntax:
		fputs("syntax error\n", stderr);
	}
	exit(0);
}

#endif /* def TEST */
