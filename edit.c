/*% clang -Wall -g -DTEST -std=c99 obj/{unicode.o,buf.o} % -o #
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "unicode.h"
#include "buf.h"
#include "edit.h"

enum {
	MaxBuf = 4,	/* maximal string stored in rbuf */
	YankSize = 128,	/* initial size of a yank buffer */
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
static void ybini(YBuf *);

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
	//if (l->type != Commit)
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
	if (!l)
		return;

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
	int i;

	eb = malloc(sizeof *eb);
	buf_init(&eb->b);
	eb->undo = log_new();
	eb->redo = log_new();
	for (i=0; i<9; i++) {
		eb->nb[i].r = 0;
		ybini(&eb->nb[i]);
	}
	eb->ntip = 0;
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
		eb_ins(eb, p0++, r);
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
}

void
eb_undo(EBuf *eb, int undo, unsigned *pp)
{
	Log *u, *r;

	if (undo)
		u = eb->undo, r = eb->redo;
	else
		u = eb->redo, r = eb->undo;

	log_undo(u, &eb->b, r, pp);
}

void
eb_yank(EBuf *eb, unsigned p0, unsigned p1, YBuf *yb)
{
	Rune *pr;

	assert(p0 <= p1);

	if (!yb) {
		if (--eb->ntip < 0)
			eb->ntip += 9;
		yb = &eb->nb[eb->ntip];
	}

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
ybini(YBuf *yb)
{
	assert(yb->r == 0);

	yb->r = malloc(YankSize * sizeof(Rune));
	assert(yb->r);
	yb->sz = YankSize;
	yb->nr = 0;
	yb->linemode = 0;
}


#ifdef TEST
#include <stdio.h>

void
putrune(Rune r)
{
	unsigned char buf[10];
	utf8_encode_rune(r, buf, 10);
	printf("%.*s", utf8_rune_len(r), buf);
}

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
				putrune(l->rbuf[i]);
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
				eb_clean(eb);
			eb_undo(eb, 1, 0);
			break;
		case '?':
			dumplog(eb->undo);
			break;
		case 'p':
			i = 0;
			do
				putrune(buf_get(&eb->b, i++));
			while (buf_get(&eb->b, i-1) != '\n');
			break;
		case 'c':
			eb_clean(eb);
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
