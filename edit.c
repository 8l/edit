/*% clang -Wall -g -DTEST -std=c99 obj/{unicode.o,buf.o} % -o #
 */
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "unicode.h"
#include "buf.h"

enum {
	MaxBuf = 4,	/* maximal string stored in rbuf */
};

typedef struct log Log;

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
log_undo(Log *l, Buf *b, Log *redo)
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

/* static functions */

/* pushlog - The invariant preserved is that
 * the topmost log entry has a rune array of
 * MaxBuf runes.
 */
void
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

void
do_insert(Buf *b, Log *l, unsigned p0, unsigned char *buf, int len)
{
	int n;
	n = buf_ins_utf8(b, p0, buf, len);
	log_insert(l, p0, p0+n);
}

void
do_delete(Buf *b, Log *l, unsigned p0, unsigned p1)
{
	log_delete(l, b, p0, p1);
	for (; p0 < p1; p1--)
		buf_del(b, p1-1);
}

int
main() {
	char line[1024], *p, *q;
	long int p0, p1, i;
	size_t len;
	Log *l;
	Buf *b;

	l = log_new();
	b = buf_new();

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
			do_insert(b, l, p0, (unsigned char *)p+1, len);
			break;
		case '-':
			p0 = strtol(line+1, &p, 0);
			if (line+1 == p || *p != ' ')
				goto Syntax;
			p1 = strtol(p+1, &q, 0);
			if (p+1 == q || (*q != '\n' && *q != 0))
				goto Syntax;
			do_delete(b, l, p0, p1);
			break;
		case '!':
			if (l->type != Commit) {
				fputs("warn: adding commit to log\n", stderr);
				log_commit(l);
			}
			log_undo(l, b, 0);
			break;
		case '?':
			dumplog(l);
			break;
		case 'p':
			i = 0;
			do
				putrune(buf_get(b, i++));
			while (buf_get(b, i-1) != '\n');
			break;
		case 'c':
			log_commit(l);
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
