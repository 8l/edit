#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "unicode.h"

static void del(Buf *, unsigned);
static Rune *hend(Page *);
static void hmove(Page *, long int);
static void ins(Buf *, unsigned, Rune);
static void move(Rune *, Rune *, unsigned);
static Page *newpage(void);
static Page *page(Buf *, unsigned *);
static void setcol(Page *, Page *);
static void setnl(Page *);

Buf *
buf_new(char path[])
{
	Buf *b;

	b = malloc(sizeof *b);
	if (!b)
		return 0;
	snprintf(b->path, PathLen, "%s", path);
	b->p = newpage();
	b->last = 0;
	return b;
}

/* buf_del - Delete rune after position [pos].
 */
void
buf_del(Buf *b, unsigned pos)
{
	del(b, pos);
}

/* buf_ins - Insert rune [r] at position [pos].
 */
void
buf_ins(Buf *b, unsigned pos, Rune r)
{
	ins(b, pos, r);
}

/* buf_ins_utf8 - Insert raw utf8 encoded text in buffer [b]
 * at position [pos]. The number of bytes processed is returned.
 */
int
buf_ins_utf8(Buf *b, unsigned pos, unsigned char *data, int len)
{
	Rune r;
	int rd, total;

	total = 0;
	while ((rd = utf8_decode_rune(&r, data, len))) {
		ins(b, pos++, r);
		data += rd;
		len -= rd;
		total += rd;
	}
	return total;
}

Rune
buf_get(Buf *b, unsigned off)
{
	Page *p;

	p = page(b, &off);

	if (off >= p->len) {
		p = p->n;
		off = 0;
		if (p == 0)
			return '\n';
	}

	if (off < p->hbeg - p->buf)
		return p->buf[off];
	else
		return p->buf[off + (PageLen - p->len)];
}

void
buf_getlc(Buf *b, unsigned pos, int *l, int *c)
{
	unsigned off;
	Page *p;
	int line, col;

	line = 0;
	off = 0;

	for (p=b->p; p->n; p=p->n) {
		if (off + p->len > pos)
			break;
		off += p->len;
		line += p->nl;
	}

	col = p->col;

	for (; off < pos; off++)
		if (buf_get(b, off) == '\n') {
			line++;
			col = 0;
		} else
			col++;

	*l = line;
	*c = col;
}

unsigned
buf_bol(Buf *b, unsigned off)
{
	do {
		if (off == 0)
			return 0;
		--off;
	} while (buf_get(b, off) != '\n');

	return off+1;
}

unsigned
buf_eol(Buf *b, unsigned off)
{
	Rune r;

	while ((r = buf_get(b, off)) != '\n')
		off++;

	return off;
}

unsigned
buf_setlc(Buf *b, int l, int c)
{
	unsigned off;
	Page *p;
	int line, col;

	line = 0;
	off = 0;

	for (p=b->p; p->n; p=p->n) {
		if (line + p->nl >= l)
			break;
		off += p->len;
		line += p->nl;
	}

	col = p->col;

	for (; line < l || col < c; off++) {
		if (buf_get(b, off) == '\n') {
			if (line == l)
				return off;
			line++;
			col = 0;
		} else
			col++;
	}

	return off;
}

/* static functions */

/* del - Delete the rune right after position [pos].
 *
 *     a b c d e
 *    0 1 2 3 4
 *      ^ pos == 1 will delete b
 */
static void
del(Buf *b, unsigned pos)
{
	Page *q, *p, *old;
	unsigned off;
	long int shft;
	int fixcol;

	q = 0;
	off = pos;
	p = page(b, &off);

	if (off == p->len) {
		q = p;
		p = p->n;
		off = 0;
		if (p == 0)
			return;
	}

	shft = off+1 - (p->hbeg - p->buf);
	if (shft)
		hmove(p, shft);

	if (p->hbeg[-1] == '\n')
		p->nl--;
	p->hbeg--;
	p->len--;

	if (p->len == 0 && (p != b->p || p->n)) {
		if (!q) {
			for (q=b->p; q && q->n!=p; q=q->n)
				;
		}

		old = p;
		if (q)
			p = q->n = p->n;
		else
			p = b->p = p->n;
		free(old);

		if (b->last == old)
			b->last = p; /* should do it... */
		fixcol = q != 0 && q->n != 0;
	}
	else if (p->n) {
		q = p;
		fixcol = p->len+1 - off - 1 <= p->n->col;
	}
	else
		fixcol = 0;

	if (fixcol)
		do {
			setcol(q, q->n);
			q = q->n;
		} while (q && q->n && q->nl == 0);
}

/* hend - Get a pointer on the end of the hole in page [p].
 */
static Rune *
hend(Page *p)
{
	return p->hbeg + (PageLen - p->len);
}

/* hmove - Move the hole in the page [p] of [s] runes. If [s]
 * is positive the hole is moved towards the end, if it
 * is negative it is moved towards the beginning.
 */
static void
hmove(Page *p, long int s)
{
	assert(s != 0);

	if (s>0)
		move(p->hbeg, hend(p), s);
	else
		move(hend(p) + s, p->hbeg + s, -s);

	p->hbeg += s;
}

/* ins - Insert a rune in the buffer [b] at position [pos].
 */
static void
ins(Buf *b, unsigned pos, Rune r)
{
	Page *p;
	unsigned off;
	long int shft;

	off = pos;
	p = page(b, &off);

	if (p->len == PageLen) { /* bad luck, grow a page */
		Page *q;
		enum { l = PageLen/2 };

		q = newpage();
		q->n = p->n;
		p->n = q;

		move(q->buf, &p->buf[l], PageLen-l);
		q->hbeg = q->buf + PageLen-l;
		q->len = PageLen-l;

		p->hbeg = p->buf + l;
		p->len = l;

		setnl(p);
		setnl(q);
		setcol(p, q);
		ins(b, pos, r);
		return;
	}

	shft = off - (p->hbeg - p->buf);
	if (shft)
		hmove(p, shft);

	/* there we go */
	*p->hbeg++ = r;
	p->len++;
	if (r == '\n')
		p->nl++;


	if (p->n)
	if (p->len-1 - off <= p->n->col)
		do {
			setcol(p, p->n);
			p = p->n;
		} while (p && p->n && p->nl == 0);
}

/* move - Move some runes in an array, works like
 * memmove the zones must not overlap.
 */
static void
move(Rune *dest, Rune *from, unsigned cnt)
{
	memmove(dest, from, cnt * sizeof(Rune));
}

static Page *
newpage()
{
	Page *p;

	p = malloc(sizeof *p);
	assert(p);
	p->hbeg = p->buf;
	p->len = 0;
	p->nl = 0;
	p->col = 0;
	p->n = 0;
	return p;
}

/* page - Get a page containing the [*ppos]'th position.
 * *ppos is also modified to represent the offset in the
 * page. After return, the [b->last] cache points to the
 * returned page.
 */
static Page *
page(Buf *b, unsigned *ppos)
{
	Page *p;
	unsigned off, pos;

	pos = *ppos;
	if (b->last) {
		if (pos < b->lastbeg) {
			off = 0;
			p = b->p;
		}
		else if (pos > b->lastbeg + b->last->len) {
			off = b->lastbeg;
			p = b->last;
		}
		else {
			*ppos -= b->lastbeg;
			return b->last;
		}
	} else {
		off = 0;
		p = b->p;
	}

	for (; p->n; p=p->n) {
		if (pos >= off && pos <= off + p->len)
			break;
		off += p->len;
	}

	b->lastbeg = off;
	b->last = p;

	pos -= off;
	if (pos > p->len)
		pos = p->len;
	*ppos = pos;
	return p;
}

/* setcol - Update the start column of page [p],
 * the start column of the preceding page [q] must
 * be correct.
 */
static void
setcol(Page *q, Page *p)
{
	int col;
	Rune *r;

	assert(q->n == p);

	col = q->col;
	for (r = q->buf; r < q->hbeg; r++, col++)
		if (*r == '\n')
			col = -1;

	for (r = hend(q); r < &q->buf[PageLen]; r++, col++)
		if (*r == '\n')
			col = -1;

	p->col = col;
}

/* setnl - Update the line count of a page.
 */
static void
setnl(Page *p)
{
	Rune *r;
	int cnt;

	cnt = 0;
	for (r = p->buf; r < p->hbeg; r++)
		if (*r == '\n')
			cnt++;

	for (r = hend(p); r < &p->buf[PageLen]; r++)
		if (*r == '\n')
			cnt++;

	p->nl = cnt;
}
