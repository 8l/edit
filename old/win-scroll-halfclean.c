/*% clang -DWIN_TEST -Wall -g -I/usr/include/freetype2 -lX11 -lXft obj/unicode.o obj/buf.o obj/x11.o % -o #
 *
 * Windowing module
 */

#include <assert.h>
#include <string.h>

#include "unicode.h"
#include "buf.h"
#include "gui.h"
#include "win.h"

enum CursorPos { Top, Bot };
enum Focus { FocusBol, FocusEol, FocusCu };

enum { RingSize = 32 };
struct lineinfo {
	int beg, len;           /* begining and len of the sl ring buffer */
	unsigned sl[RingSize];  /* ring buffer of screen line offsets */
	int cul;                /* cursor line in sl */
};

static void pushrune(GWin *gw, Rune r, int x, int y, int w, int cu);
static void draw(W *w);
static unsigned lineinfo(W *w, unsigned off, struct lineinfo *li, enum Focus f);

static W wins[MaxWins];
static int nwins;
static struct gui *g;
static GFont font;
static int fwidth, fheight;

/* win_init - initialize the module using [gui] as a
 * graphical backed.
 */
void
win_init(struct gui *gui)
{
	g = gui;

	g->init();
	g->getfont(&font);

	/* the gui module does not give a way to access the screen
	 * dimension, instead, the first event generated will always
	 * be a GResize, so we can adjust these dummy values with
	 * win_resize_frame
	 */
	fwidth = fheight = 10;
}

/* win_new - insert a new window if possible and
 * return it. In case of error (more than MaxWins
 * windows), 0 is returned.
 */
W *
win_new(Buf *b)
{
	if (nwins!=0)
		return 0;

	memset(&wins[nwins], 0, sizeof(W));
	wins[nwins].b = b;
	wins[nwins].gw = g->newwin(0, 0, fwidth, fheight);
	wins[nwins].vfrac = FScale;

	nwins = 1;
	return &wins[0];
}

/* win_delete - delete a window created by win_new.
 */
void
win_delete(W *w)
{
	assert(w >= wins);
	assert(w < wins+nwins);
	nwins = 0;
}

/* win_resize_frame - called when the whole frame
 * is resized.
 */
void
win_resize_frame(int w, int h)
{
	GColor white = { 255, 255, 255 };
	int x, ww;
	W *pw;

	if (w!=0 && h!=0) {
		fwidth = w;
		fheight = h;
	}

	for (x=0, pw=wins; pw-wins<nwins; pw++) {
		pw->height = fheight;
		ww = (fwidth * pw->vfrac) / FScale;
		g->movewin(pw->gw, x, 0, ww, fheight);
		g->drawrect(pw->gw, 0, 0, ww, fheight, white);
		draw(pw);
		g->putwin(pw->gw);
		x += ww;
	}
}

/* win_redraw_frame - redraw the whole frame.
 */
void
win_redraw_frame()
{
	win_resize_frame(0, 0);
}

/* win_scroll - scroll the window by [n] lines.
 * If [n] is negative it will scroll backwards.
 * The algorithm is guaranteed to work properly
 * when RingSize is larger than the number of
 * lines on a screen. In any cases, it should
 * not crash.
 */
void
win_scroll(W *w, int n)
{

	struct lineinfo li;
	unsigned start, bol, eol;
	int dir;

	if (n == 0)
		return;

	dir = n;
	li.cul = -1;

	if (n < 0) {
		start = w->start;
		do {
			int top;

			if (start == 0)
				/* already at the top */
				break;
			bol = buf_bol(w->b, start-1);

			li.beg = li.len = 0;
			lineinfo(w, bol, &li, FocusEol);
			top = li.len - 1;
			for (; n<0; top--) {
				if (top < 0) {
					/* move to prev line */
					start = bol;
					n++;
					break;
				}
				start = li.sl[(li.beg + top) % RingSize];
				if (start < w->start)
					n++;
			}
		} while (n<0);
		w->start = start;
	} else {
		start = w->start;
		do {
			int top;

			li.beg = li.len = 0;
			eol = lineinfo(w, start, &li, FocusBol);
			top = 0;
			for (; n>0; top++, n--) {
				if (top >= li.len) {
					/* move to next line */
					start = eol;
					n--;
					break;
				}
				start = li.sl[(li.beg + top) % RingSize];
			}
		} while (n>0);
		w->start = start;
	}
}

/* win_show_cursor - find the cursor in [w] and adjust
 * the text view so that the cursor is displayed on the
 * screen. Depending on [where], the cursor will be
 * displayed at the top or at the bottom of the screen.
 */
void
win_show_cursor(W *w, enum CursorPos where)
{
	// XXX implement me
}

/* static functions */

typedef int (*LineFn)(void *data, unsigned off, Rune r, int x, int rw, int sl);

static unsigned
line(W *w, unsigned off, LineFn f, void *data)
{
	Rune r;
	int l, x, rw;

	x = 0;
	l = 0;

	for (;; off++) {
		r = buf_get(w->b, off);
		if (r == NORUNE)
			break;
		if (r == '\n') {
			off++;
			break;
		}

		if (r == '\t') {
			int tw;

			tw = TabWidth * font.width;
			rw = tw - x % tw;
		} else
			rw = g->textwidth(&r, 1);

		if (x+rw > w->gw->w)
		if (x != 0) { /* force progress */
			x = 0;
			l++;
		}

		if (!f(data, off, r, x, rw, l)) {
			off++;
			break;
		}

		x += rw;
	}

	return off;
}

/* aggregate all calls to [drawtext] into batches, it
 * will flush if the current character is \n or \t.
 */
static void
pushrune(GWin *gw, Rune r, int x, int y, int w, int cu)
{
	static int fragx = -1, fragy, cx = -1, cy, cw;
	static Rune frag[MaxWidth], *p = frag;
	GColor color = { .blue = 255 },
	       xor   = { .x = 1 };

	assert(r == '\n' || fragx == -1 || y == fragy);

	if (fragx == -1) {
		fragx = x;
		fragy = y;
	}

	if (cu) {
		cx = x;
		cy = y - font.ascent;
		cw = w;
	}

	if (r == '\t' || r == '\n') {
#if 0
		printf("flushing: fragx = %d\n"
		       "          fragy = %d\n"
		       "          r = %u\n"
		       "          len = %td\n",
		       fragx, fragy, r, p-frag);
#endif
		assert(fragx != -1);
		g->drawtext(gw, frag, p-frag, fragx, fragy, color);
		if (cx != -1)
			g->drawrect(gw, cx, cy, cw, font.height, xor);

		fragx = cx = -1;
		p = frag;
		return;
	}

	assert(p-frag < MaxWidth);
	*p++ = r;
}

struct dstatus {
	W *w;
	int begl, curl;
	unsigned stop;
};

/* to be called as a LineFn by the [line] function */
static int
drawfn(void *data, unsigned off, Rune r, int x, int rw, int sl)
{
	int y;
	struct dstatus *ds = data;

	y = (ds->begl + sl) * font.height + font.ascent;
	if (y > ds->w->height) {
		ds->stop = off;
		return 0;
	}

	if (ds->curl != sl) { /* need a flush, we changed screen line */
		assert(x == 0);
		pushrune(ds->w->gw, '\n', 0, 0, 0, 0);
		ds->curl = sl;
	}

	pushrune(ds->w->gw, r, x, y, rw, off == ds->w->cu);
	return 1;
}

static void
draw(W *w)
{
	unsigned boff, eoff;
	struct dstatus ds;

	ds.w = w;
	ds.begl = 0;
	ds.curl = 0;
	ds.stop = -1;
	eoff = w->start;

	do {
		boff = eoff;
		eoff = line(w, boff, drawfn, &ds);
		pushrune(w->gw, '\n', 0, 0, 0, 0); /* flush */
		ds.begl += ds.curl + 1;
	} while (ds.stop == -1u && eoff != boff);

	if (eoff == boff)
		w->stop = eoff;
	else
		w->stop = ds.stop;
}

static void
pushoff(struct lineinfo *li, unsigned off)
{
	assert(li->len <= RingSize);

	if (li->len == RingSize) {
		li->sl[li->beg] = off;
		li->beg++;
		li->beg %= RingSize;
		if (li->cul != -1)
			li->cul--;
	} else {
		int n;

		n = (li->beg + li->len) % RingSize;
		li->sl[n] = off;
		li->len++;
	}
}

struct lstatus {
	enum Focus f;
	unsigned cu;
	struct lineinfo *li;
	int curl;
};

static int
lineinfofn(void *data, unsigned off,
           __attribute__((unused)) Rune r,
           __attribute__((unused)) int x,
           __attribute__((unused)) int rw,
           int sl)
{
	struct lstatus *ls = data;

	if (ls->curl != sl) {
		assert(ls->curl == sl-1);

		if (ls->f == FocusEol
		|| (ls->f == FocusBol && ls->li->len < RingSize)
		|| (ls->f == FocusCu && ls->li->cul == -1))
			pushoff(ls->li, off);

		ls->curl = sl;
	}

	if (ls->f == FocusCu && off == ls->cu)
		ls->li->cul = ls->li->len - 1;

	return 1;
}

/* the api is not yet perfect: we should be able to specify
 * a maximum offset that makes lineinfofn return 0 to block
 * the line function
 */
static unsigned
lineinfo(W *w, unsigned off, struct lineinfo *li, enum Focus f)
{
	struct lstatus ls;

	ls.f = f;
	ls.cu = w->cu;
	ls.li = li;
	ls.curl = 0;

	return line(w, off, lineinfofn, &ls);
}

#ifdef WIN_TEST
/* test */

#include <stdlib.h>

void die(char *m) { exit(1); }

int main()
{
	GEvent e;
	Buf *b;
	W *w;
	enum CursorPos cpos;
	unsigned char s[] =
	"je suis\n"
	"\tQuentin\n"
	"tab\ttest\n"
	"une longue longue longue longue longue longue longue longue longue longue longue longue longue ligne\n"
	"un peu d'unicode: ä æ ç\n"
	"et voila!\n";

	b = buf_new("*");
	win_init(&gui_x11);
	w = win_new(b);

	for (int i=0; i<20; i++)
		buf_ins_utf8(b, 0, s, sizeof s - 1);

	do {
		g->nextevent(&e);
		if (e.type == GResize)
			win_resize_frame(e.resize.width, e.resize.height);
		if (e.type == GKey) {
			switch (e.key) {
			case 'l': ++w->cu; cpos = Bot; break;
			case 'h': --w->cu; cpos = Top; break;
			case 'e'-'a' + 1: win_scroll(w,  1); break;
			case 'y'-'a' + 1: win_scroll(w, -1); break;
			default: continue;
			}
			win_redraw_frame();
		}
	} while (e.type != GKey || e.key != 'q');

	g->fini();
	return 0;
}

#endif
