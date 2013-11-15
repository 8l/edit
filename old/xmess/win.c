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

enum CursorPos {
	Top,
	Bot,
};

enum {
	Draw = 1,
	Line = 2,
	BLine = 4  | Line,
	ELine = 8  | Line,
	CLine = 16 | Line,
};

static void put(W *w, int flags);

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
		pw->height = fheight / font.height;
		ww = (fwidth * pw->vfrac) / FScale;
		g->movewin(pw->gw, x, 0, ww, fheight);
		g->drawrect(pw->gw, 0, 0, ww, fheight, white);
		put(pw, Draw);
		g->putwin(pw->gw);
		x += ww;
	}
}

/* win_redraw_frame - redraw the whole frame.
 */
void
win_redraw_frame(void)
{
	win_resize_frame(0, 0);
}

/* win_scroll - scroll the window by [n] lines.
 * If [n] is negative it will scroll backwards.
 * The number of lines scrolled must not be
 * bigger than the number of lines currently on
 * the screen.
 */
void
win_scroll(W *w, int n)
{
	int top;
	unsigned off, bol;
	W wscratch;

	if (w->nls == 0 || n == 0)
		return;

	assert(n < 0 || n <= w->nls);

	wscratch.b = w->b;
	wscratch.gw = w->gw;

	if (n<0) {
		off = w->loff[0];

		do {
			if (off == 0)
				break;

			bol = buf_bol(w->b, off-1);
			wscratch.loff[0] = bol;
			put(&wscratch, ELine);

			top = wscratch.nls - 1;
			do {
				off = wscratch.loff[top--];
				if (off < w->loff[0])
					++n;
			} while (off >= w->loff[0] || (top>=0 && n<0));
		} while (n<0);
	} else
		off = w->loff[n];

	w->loff[0] = off;
	put(w, 0); /* update line offsets */

	if (w->cl == -1) { /* fix the cursor position */
		if (n>0)
			w->cl = 0;
		else
			w->cl = w->nls-1;
		w->coff = w->loff[w->cl];
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
	unsigned bol;
	W wscratch;

	wscratch.b = w->b;
	wscratch.gw = w->gw;
	wscratch.coff = w->coff;

	bol = buf_bol(w->b, w->coff);
	wscratch.loff[0] = bol;

	put(&wscratch, CLine); /* find cursor line */
	assert(wscratch.cl < wscratch.nls);
	assert(wscratch.cl >= 0);

	w->loff[0] = wscratch.loff[wscratch.cl];

	put(w, 0);
	if (where == Bot)
		win_scroll(w, -w->height + 1);
}

/* static functions */


/* this function tries to factor calls to the pretty expensive
 * [drawtext] function of the gui module, we basically try to aggregate
 * all potential calls into one. Sometimes it is necessary to flush,
 * for instance when the are displaying a new line or when a tabulation
 * has to be printed. Tabs are treated specially because they have
 * a width the drawtext function does not know a priori.
 *
 * Calling this function with [r == NORUNE] will force a flush.
 *
 * Before using this I called drawtext for each char, on a big screen
 * the slowdown was significative.
 */
static void
draw(GWin *gw, Rune r, int x, int y, int rw, int cu)
{
	static int fragx = -1, fragy, cx = -1, cy, cw;
	static Rune frag[MaxWidth], *p = frag;
	int flush;
	GColor color = { .blue = 255 },
	       xor   = { .x = 1 };

	flush = 0;

	if (r != NORUNE && fragx == -1) {
		fragx = x;
		fragy = y;
	}

	if (r != NORUNE && y == fragy) {
		if (cu) {
			cx = x;
			cy = y-font.ascent;
			cw = rw;
		}

		if (r != '\t') {
			assert(p-frag < MaxWidth);
			*p++ = r;
		} else
			flush = 1;
	} else
		flush = 1;

	/* we flush the current fragment if one
	 * of the following conditions is met
	 *   - the current rune is a tab
	 *   - the current rune is a NORUNE (signaling eof)
	 *   - the rune's y is different from the fragment's y
	 *     (signaling a newline)
	 */
	if (flush) {
#if 0
		printf("flushing: fragx = %d\n"
		       "          fragy = %d\n"
		       "          r = %u\n"
		       "          len = %zd\n",
		       fragx, fragy, r, p-frag);
#endif
		if (fragx == -1)
			return;

		g->drawtext(gw, frag, p-frag, fragx, fragy, color);
		if (cx != -1)
			g->drawrect(gw, cx, cy, cw, font.height, xor);

		fragx = cx = -1;
		p = frag;

		if (r != NORUNE && r != '\n' && fragy != y)
			/* push r on frag with a recursive call */
			draw(gw, r, x, y, rw, cu);
	}
}

static void
put(W *w, int flags)
{
	unsigned off, i, nl;
	int rw, x, y, lnum;
	Rune r;
	GWin *gw;

	off = w->loff[0];
	gw = w->gw;
	x = 0;
	y = font.ascent;
	lnum = 0;
	w->loff[lnum++] = off;
	w->cl = -1;

	for (i=0; y < gw->h || (flags & Line); i++) {
		r = buf_get(w->b, off+i);
		if (r == NORUNE) {
			lnum++;
			break;
		}

		if (w->coff == off+i)
			w->cl = lnum - 1;

		if (r == '\t') {
			rw = TabWidth * font.width;
			rw -= x % (TabWidth * font.width);
		} else {
			nl = 0;
			if (r != '\n') {
				if (unicode_rune_width(r) == 0)
					continue;
				rw = g->textwidth(&r, 1);
				if (x+rw > gw->w)
					nl = off+i;
			} else if ((flags & Line) == 0) {
				nl = off+i+1;
				rw = 0;
			} else {
				lnum++;
				break;
			}

			if (nl) {
				y += font.height;
				x = 0;
				if (lnum < MaxLines)
					w->loff[lnum++] = nl;
				else if ((flags & ELine)
				     || ((flags & CLine) && w->cl == -1)) {
					/* should use a ring buffer... */
					assert(lnum == MaxLines);
					memmove(w->loff, w->loff+1,
					        (MaxLines-1) * sizeof(int));
					w->loff[MaxLines-1] = nl;
				}
			}
		}

		if (flags & Draw)
			draw(gw, r, x, y, rw, w->coff == off+i);

		x += rw;
	}

	if (flags & Draw)
		draw(gw, NORUNE, 0, 0, 0, 0);
	w->nls = lnum - 1;
}

#ifdef WIN_TEST
/* test */

#include <stdlib.h>

void die(char *m) { exit(1); }

int main(void)
{
	GEvent e;
	Buf *b;
	W *w;
	enum CursorPos cpos;
	unsigned char s[] =
	"je suis\n"
	"\t\tQuentin\n"
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
			case 'l': ++w->coff; cpos = Bot; break;
			case 'h': --w->coff; cpos = Top; break;
			case 'e'-'a' + 1: win_scroll(w,  1); break;
			case 'y'-'a' + 1: win_scroll(w, -1); break;
			default: continue;
			}
			win_redraw_frame();
			if (w->cl == -1) {
				win_show_cursor(w, cpos);
				win_redraw_frame();
			}
		}
	} while (e.type != GKey || e.key != 'q');

	g->fini();
	return 0;
}

#endif
