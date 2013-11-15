/*% clang -DWIN_TEST -Wall -g $(pkg-config --libs x11 xft) obj/{unicode,buf,x11}.o % -o #
 *
 * Windowing module
 */

#include <assert.h>
#include <string.h>

#include "tools.h"
#include "unicode.h"
#include "buf.h"
#include "gui.h"
#include "win.h"

enum { RingSize = 2 };          /* bigger is (a bit) faster */
static_assert(RingSize >= 2, "RingSize must be at least 2");

struct lineinfo {
	int beg, len;
	unsigned sl[RingSize];  /* screen line offsets */
};

static void draw(W *w);
static void lineinfo(W *w, unsigned off, unsigned lim, struct lineinfo *li);

static W wins[MaxWins];
static int nwins;
static struct gui *g;
static GFont font;
static int fwidth, fheight;

/* win_init - Initialize the module using [gui] as a
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

/* win_new - Insert a new window if possible and
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

/* win_delete - Delete a window created by win_new.
 */
void
win_delete(W *w)
{
	assert(w >= wins);
	assert(w < wins+nwins);
	nwins = 0;
}

/* win_resize_frame - Called when the whole frame
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

/* win_redraw_frame - Redraw the whole frame.
 */
void
win_redraw_frame()
{
	win_resize_frame(0, 0);
}

/* win_scroll - Scroll the window by [n] lines.
 * If [n] is negative it will scroll backwards.
 */
void
win_scroll(W *w, int n)
{

	struct lineinfo li;
	unsigned start, bol;
	int dir;

	if (n == 0)
		return;

	dir = n; // XXX this is for cursor handling, do we do this here?

	if (n < 0) {
		start = w->start;
		do {
			int top;

			if (start == 0)
				/* already at the top */
				break;
			bol = buf_bol(w->b, start-1);

			li.beg = li.len = 0;
			lineinfo(w, bol, start-1, &li);
			top = li.len - 2;
			assert(top >= 0);
			for (; n<0 && top>=0; top--, n++) {
				start = li.sl[(li.beg + top) % RingSize];
				assert(start < w->start);
			}
		} while (n<0);
		w->start = start;
	} else {
		start = w->start;
		do {
			int top;

			li.beg = li.len = 0;
			lineinfo(w, start, -1, &li);
			top = 1;
			assert(top < li.len);
			for (; n>0 && top<li.len; top++, n--) {
				start = li.sl[(li.beg + top) % RingSize];
				assert(start > w->start
				    || buf_get(w->b, w->start) == '\n'); // change this to test size
			}
		} while (n>0);
		w->start = start;
	}
}

/* win_show_cursor - Find the cursor in [w] and adjust
 * the text view so that the cursor is displayed on the
 * screen. Depending on [where], the cursor will be
 * displayed at the top or at the bottom of the screen.
 */
void
win_show_cursor(W *w, enum CursorLoc where)
{
	struct lineinfo li;
	unsigned bol;

	bol = buf_bol(w->b, w->cu);
	li.beg = li.len = 0;
	lineinfo(w, bol, w->cu, &li);
	assert(li.len >= 2);
	w->start = li.sl[(li.beg + li.len-2) % RingSize];
	if (where == CBot)
		win_scroll(w, -w->height/font.height + 1);
}

/* static functions */

typedef int LineFn(void *data, unsigned off, Rune r, int x, int rw, int sl);

static unsigned
line(W *w, unsigned off, LineFn f, void *data)
{
	Rune r;
	int l, x, rw;

	r = 0;
	l = 0;
	x = 0;

	for (; r != '\n'; x+=rw, off++) {
		r = buf_get(w->b, off);

		if (r == '\t') {
			int tw;

			tw = TabWidth * font.width;
			rw = tw - x % tw;
		} else if (r == '\n') {
			rw = 0;
		} else
			rw = g->textwidth(&r, 1);

		if (x+rw > w->gw->w)
		if (x != 0) { /* force progress */
			x = 0;
			l++;
		}

		if (!f(data, off, r, x, rw, l))
			break;
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
		cw = w ? w : 4;
	}

	if (r == '\t' || r == '\n') {
#if 0
		printf("flushing: fragx = %d\n"
		       "          fragy = %d\n"
		       "          r = %u\n"
		       "          len = %td\n"
		       "          frag = '",
		       fragx, fragy, r, p-frag);
		for (int i=0; i<p-frag; i++)
			printf("%c", (char)frag[i]);
		printf("'\n");
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
};

/* to be called as a LineFn by the [line] function */
static int
drawfn(void *data, unsigned off, Rune r, int x, int rw, int sl)
{
	int y;
	struct dstatus *ds = data;

	y = (ds->begl + sl) * font.height + font.ascent;

	if (ds->curl != sl) { /* need a flush, we changed screen line */
		assert(x == 0);
		ds->curl = sl;
		pushrune(ds->w->gw, '\n', 0, 0, 0, 0);
		if (y + font.descent > ds->w->height)
			return 0;
	}

	pushrune(ds->w->gw, r, x, y, rw, off == ds->w->cu);
	return 1;
}

static void
draw(W *w)
{
	int nls;
	unsigned off;
	struct dstatus ds;

	ds.w = w;
	ds.begl = 0;
	ds.curl = 0;
	off = w->start;
	nls = w->height/font.height;

	do {
		off = line(w, off, drawfn, &ds);
		ds.begl += ds.curl + 1;
		ds.curl = 0;
	} while (ds.begl < nls);

	w->stop = off;
}

static int
pushoff(struct lineinfo *li, unsigned off, int overwrite)
{
	assert(li->len <= RingSize);

	if (li->len == RingSize) {
		if (!overwrite)
			return 0;
		li->sl[li->beg] = off;
		li->beg++;
		li->beg %= RingSize;
	} else {
		int n;

		n = (li->beg + li->len) % RingSize;
		li->sl[n] = off;
		li->len++;
	}
	return 1;
}

struct lstatus {
	unsigned lim;
	struct lineinfo *li;
	int curl;
};

static int
lineinfofn(void *data, unsigned off, Rune r, int x, int rw, int sl)
{
	struct lstatus *ls = data;

	unused(r);
	unused(x);
	unused(rw);

	if (off > ls->lim)
		return 0;

	if (ls->curl != sl) {
		assert(ls->curl == sl-1);
		ls->curl = sl;
		return pushoff(ls->li, off, ls->lim != -1u);
	}

	return 1;
}

/* if [lim == -1] the lineinfo will only contain information
 * about the first RingSize screen lines
 */
static void
lineinfo(W *w, unsigned off, unsigned lim, struct lineinfo *li)
{
	struct lstatus ls;

	assert(RingSize >= 2);

	ls.lim  = lim;
	ls.li   = li;
	ls.curl = 0;

	pushoff(li, off, lim != -1u);
	off = line(w, off, lineinfofn, &ls);
	pushoff(li, off, lim != -1u);
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
	enum culoc cloc;
	unsigned char s[] =
	"je suis\n"
	"\tQcar\n"
	"tab\ttest\n"
	"une longue longue longue longue longue longue longue longue longue longue longue longue longue ligne\n"
	"un peu d'unicode: ä æ ç\n"
	"et voila!\n\n";

	b = buf_new("*");
	win_init(&gui_x11);
	w = win_new(b);

	for (int i=0; i<5; i++)
		buf_ins_utf8(b, 0, s, sizeof s - 1);

	do {
		g->nextevent(&e);
		if (e.type == GResize)
			win_resize_frame(e.resize.width, e.resize.height);
		if (e.type == GKey) {
			switch (e.key) {
			case 'l': ++w->cu; cloc = CBot; break;
			case 'h': --w->cu; cloc = CTop; break;
			case 'e'-'a' + 1: win_scroll(w,  1); break;
			case 'y'-'a' + 1: win_scroll(w, -1); break;
			default: continue;
			}
			win_redraw_frame();
			if (e.key == 'l' || e.key == 'h')
			if (w->cu < w->start || w->cu >= w->stop) {
				win_show_cursor(w, cloc);
				win_redraw_frame();
			}

		}
	} while (e.type != GKey || e.key != 'q');

	g->fini();
	return 0;
}

#endif
