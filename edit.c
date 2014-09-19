/*% clang -Wall -g -DTEST -std=c99 obj/{unicode.o,buf.o} % -o #
 */
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

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
	unsigned p0;	/* location of the change/revision (for commits) */
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
static void rebase(Mark *, int, unsigned, unsigned);
static void geteb(EBuf *, int);
static void puteb(EBuf *, int);
static void putrune(Rune, int);

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

unsigned
log_revision(Log *l)
{
	if (l->type != Commit)
		return 0;
	return l->p0;
}

void
log_commit(Log *l, unsigned rev)
{
	static unsigned grev = 1;

	if (!rev)
		rev = ++grev;
	pushlog(l, Commit);
	l->p0 = rev;
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

	p0 = top->p0;
	top->p0 = l->p0;
	top->next = l->next;
	free(l);
	if (redo)
		log_commit(redo, p0);
}

Log *
log_new()
{
	Log *l;

	l = malloc(sizeof *l + MaxBuf*sizeof(Rune));
	assert(l);
	l->type = Commit;
	l->p0 = 1; /* initial revision */
	l->np = 0;
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
eb_new(int fd)
{
	EBuf *eb;

	eb = malloc(sizeof *eb);
	buf_init(&eb->b);
	eb->undo = log_new();
	eb->redo = log_new();
	eb->ml = 0;
	eb->path = 0;
	eb->tasks = 0;
	if (fd != -1)
		geteb(eb, fd);
	return eb;
}

void
eb_kill(EBuf *eb)
{
	Mark *m;

	assert(!eb->tasks);
	buf_clr(&eb->b);
	log_clr(eb->undo);
	log_clr(eb->redo);
	while ((m=eb->ml)) {
		eb->ml = m->next;
		free(m);
	}
	free(eb->b.p);
	free(eb->undo);
	free(eb->redo);
	free(eb->path);
	free(eb);
}

void
eb_del(EBuf *eb, unsigned p0, unsigned p1)
{
	rebase(eb->ml, Delete, p0, p1-p0);
	log_clr(eb->redo);
	log_delete(eb->undo, &eb->b, p0, p1);
	while (p0 < p1)
		buf_del(&eb->b, --p1);
}

void
eb_ins(EBuf *eb, unsigned p0, Rune r)
{
	rebase(eb->ml, Insert, p0, 1);
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
	if (eb->undo->type != Commit)
		log_commit(eb->undo, 0);
}

unsigned
eb_revision(EBuf *eb)
{
	return log_revision(eb->undo);
}

void
eb_undo(EBuf *eb, int undo, unsigned *pp)
{
	Log *u, *r, *l;
	int ty;

	if (undo)
		u = eb->undo, r = eb->redo;
	else
		u = eb->redo, r = eb->undo;

	if ((l = u->next) != 0) {
		for (; l->type != Commit; l=l->next) {
			if (l->type == Insert)
				ty = Delete;
			else
				ty = Insert;
			rebase(eb->ml, ty, l->p0, l->np);
		}
		log_undo(u, &eb->b, r, pp);
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

unsigned
eb_look(EBuf *eb, unsigned p, Rune *str, unsigned n)
{
	unsigned i, j;

	assert(str);

	while (p<eb->b.limbo) {
		i = p++;
		j = 0;
		while (buf_get(&eb->b, i++) == str[j++])
			if (j == n)
				return p-1;
	}
	return -1u;
}

void
eb_write(EBuf *eb, int fd)
{
	puteb(eb, fd);
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
rebase(Mark *m, int type, unsigned p0, unsigned np)
{
	switch(type) {
	case Insert:
		for (; m; m=m->next)
			if (m->p >= p0)
				m->p += np;
		break;
	case Delete:
		for (; m; m=m->next)
			if (m->p >= p0) {
				if (m->p < p0 + np)
					m->p = p0;
				else
					m->p -= np;
			}
		break;
	default:
		abort();
	}
}

void
geteb(EBuf *eb, int fd)
{
	unsigned char buf[11], *beg;
	int rd, in, ins;

	beg = buf;
	for (;;) {
		rd = read(fd, beg, sizeof buf - (beg-buf));
		in = rd + (beg-buf);
		ins = eb_ins_utf8(eb, eb->b.limbo, buf, in);

		assert(rd != 0 || in == ins); /* XXX */
		if (rd == 0)
			break;
		memmove(buf, buf+ins, in-ins);
		beg = buf + (in-ins);
	}
}

static void
puteb(EBuf *eb, int fd)
{
	enum { Munching, Spitting } state = Munching;
	unsigned munchb = 0, munche = 0, nl = 0;
	Rune r;

	while (munche < eb->b.limbo)
	switch (state) {

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
			putrune(r, fd);
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
		putrune(r, fd);
		munchb++;
		continue;

	}

	putrune('\n', fd); // always terminate file with a newline
}

static void
putrune(Rune r, int fd)
{
	unsigned char uni[8]; /* XXX 8 */
	int i, n;

	n = utf8_encode_rune(r, uni, 8);
	for (i=0; i<n; i++)
		write(fd, &uni[i], 1);
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
				putrune(l->rbuf[i], STDOUT_FILENO);
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
			log_insert(eb->undo, p0, p0+len);
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
				log_commit(eb->undo, 0);
			eb_undo(eb, 1, 0);
			break;
		case '?':
			dumplog(eb->undo);
			break;
		case 'p':
			i = 0;
			do
				putrune(buf_get(&eb->b, i++), STDOUT_FILENO);
			while (buf_get(&eb->b, i-1) != '\n');
			break;
		case 'c':
			log_commit(eb->undo, 0);
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
