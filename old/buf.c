#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "buf.h"
#include "unicode.h"

static Rune *hend(Page *);
static void ins(Buf *, unsigned, Rune);
static void move(Rune *, Rune *, unsigned);
static Page *newpage(void);
static Page *page(Buf *, unsigned *);
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

unsigned
buf_read(Buf *b, unsigned off, Rune *rs, unsigned len)
{
	unsigned n, m;
	Page *p;

	if (len == 0)
		return 0;

	p = page(b, &off);

	if (off >= p->len) {
		if (p->n) {
			p = p->n;
			off = 0;
		} else
			return 0;
	}

	n = 0;
	do {
		m = p->hbeg - p->buf;
		if (off < m) {
			if (m - off > len - n)
				m = len - n;
			else
				m -= off;
			move(rs+n, p->buf+off, m);
			off += m;
			n += m;
		} else {
			off -= m; // offset after hole
			m = p->len - m;
			if (m - off > len - n)
				m = len - n;
			else
				m -= off;
			move(rs+n, hend(p)+off, m);
			n += m;
			if (p->n) {
				p = p->n;
				off = 0;
			} else
				return n;
		}
	} while (n < len);

	return n;
}

Rune
buf_get(Buf *b, unsigned pos)
{
	Rune r;

	r = NORUNE;
	buf_read(b, pos, &r, 1);
	return r;
}

/* static functions */

/* hend - Get a pointer on the end of the hole in page [p].
 */
static Rune *
hend(Page *p)
{
	return p->hbeg + (PageLen - p->len);
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
		ins(b, pos, r);
		return;
	}

	shft = off - (p->hbeg - p->buf);
	if (shft) {
		if (shft>0)
			move(p->hbeg, hend(p), shft);
		else
			move(hend(p) + shft, p->hbeg + shft, -shft);

		p->hbeg += shft;
	}

	/* there we go */
	*p->hbeg++ = r;
	p->len++;
	if (r == '\n')
		p->nl++;
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
newpage(void)
{
	Page *p;

	p = malloc(sizeof *p);
	assert(p);
	p->hbeg = p->buf;
	p->len = 0;
	p->nl = 0;
	p->n = 0;
	return p;
}

/* page - Get a page containing the [*ppos]'th position.
 * *ppos is also modified to represent the offset in the
 * page.
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
