/*% clang -DN=2 -DWIN_TEST -Wall -g $(pkg-config --libs x11 xft) obj/{unicode,buf,edit,x11}.o % -o #
 */

#include <assert.h>
#include <string.h>

#include "unicode.h"
#include "buf.h"
#include "edit.h"
#include "gui.h"
#include "win.h"
#include "exec.h"

enum { RingSize = 1 }; /* bigger is (a bit) faster */

struct lineinfo {
	int beg, len;
	unsigned sl[RingSize]; /* beginning of line offsets */
};

static int dirty(W *);
static void draw(W *w, GColor bg, W *focus, int insert);
static void move(W *pw, int x, int y, int w, int h);
static void lineinfo(W *w, unsigned off, unsigned lim, struct lineinfo *li);
static int runewidth(Rune r, int x);

static W wins[MaxWins];
static W *screen[MaxWins + 1];
static struct {
	W win;
	W *owner;
	int visible;
} tag;
static struct gui *g;
static GFont font;
static int fwidth, fheight;
static int tabw;


/* win_init - Initialize the module using [gui] as a
 * graphical backed.
 */
void
win_init(struct gui *gui)
{
	(g = gui)->getfont(&font);

	/* initialize tab width */
	tabw = TabWidth * g->textwidth((Rune[]){' '}, 1);

	/* initialize the tag */
	tag.win.eb = eb_new(-1);
	eb_ins_utf8(tag.win.eb, 0, (unsigned char *)TagInit, sizeof TagInit - 1);

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
win_new()
{
	W *w, *w1;
	int i, x, size;

	for (w1=wins;; w1++) {
		if (w1 - wins >= MaxWins)
			return 0;
		if (!w1->eb)
			break;
	}
	for (i=0; (w = screen[i]) && screen[i+1]; i++)
		;
	if (!w) {
		x = 0;
		size = fwidth;
	} else {
		size = w->rectw - w->rectw/2 - g->border;
		move(w, w->rectx, 0, w->rectw/2, fheight);
		x = w->rectx + w->rectw + g->border;
		i++;
	}
	w1->eb = eb_new(-1);
	move(w1, x, 0, size, fheight);
	screen[i] = w1;
	screen[i+1] = 0;
	return w1;
}

/* win_kill - Delete a window created by win_new.
 * An adjacent window is returned.
 */
W *
win_kill(W *w)
{
	W *w1, **sw;
	int rx;

	if (!screen[1])
		return 0;
	for (sw = screen; *sw != w; sw++)
		assert(*sw);
	if (sw == screen) {
		w1 = sw[1];
		rx = 0;
	} else {
		w1 = sw[-1];
		rx = w1->rectx;
	}
	move(w1, rx, 0, w->rectw+g->border+w1->rectw, fheight);
	while (w->eb->tasks)
		ex_cancel(w->eb->tasks);
	eb_kill(w->eb);
	memset(w, 0, sizeof(W));
	w->eb = 0;
	memmove(sw, sw+1, (MaxWins-(sw-screen))*sizeof(W*));
	if (tag.visible && tag.owner == w)
		tag.visible = 0;
	return w1;
}

/* win_at - Return the buffer offset at the specified location,
 * if no offset is found the cursor position is returned.
 */
unsigned
win_at(W *w, int x1, int y1)
{
	int x;
	unsigned p;

	y1 = (y1 - g->vmargin - w->recty) / font.height;
	if (y1 < 0 || y1 >= w->nl)
		return w->cu;
	p = w->l[y1];
	x = 0;
	for (; p < w ->l[y1+1] - 1; p++) {
		x += runewidth(buf_get(&w->eb->b, p), x);
		if (x + w->rectx + g->hmargin >= x1)
			break;
	}
	return p;
}

/* win_which - Find the window at the specified position, null
 * is returned if no window is found.
 */
W *
win_which(int x1, int y1)
{
	W *w;
	int i;

	for (i=0; (w = screen[i]); i++)
		if (x1 < w->rectx + w->rectw)
			break;
	if (tag.visible && tag.owner == w)
	if (y1 >= tag.win.recty)
		return &tag.win;
	return w;
}

/* win_edge - Find the edge window in the specified direction.
 */
W *
win_edge(W *w, int dir)
{
	W *w1;
	int i;

	w1 = win_text(w);
	if (dir == 'j' || dir == 'k') {
		if (tag.visible && w == tag.owner)
			return &tag.win;
		return w1;
	}
	for (i=0; w1 != screen[i]; i++)
		;
	switch (dir) {
	case 'h':
		if (--i < 0)
		while (screen[i+1])
			i++;
		break;
	case 'l':
		if (!screen[++i])
			i = 0;
		break;
	}
	return screen[i];
}

/* win_move - Resize or move a window to approximately set its
 * upper-left corner to the specified location.
 */
void
win_move(W *w, int x, int y)
{
	W *w1;
	int i, j, dx;

	if (x < 0)
		x = 0;
	if (x > fwidth - g->hmargin)
		x = fwidth - g->hmargin;
	if (y < 0)
		y = 0;
	if (y > fheight - g->vmargin)
		y = fheight - g->vmargin;
	if (w == &tag.win) {
		if (y > w->recty)
			tag.owner->dirty = 1;
		move(w, w->rectx, y, w->rectw, fheight - y);
		return;
	}
	for (i=j=0; screen[i+1]; i++)
		if (screen[i] == w) {
			screen[i] = screen[i+1];
			screen[i+1] = w;
			j++;
		}
	for (; i>0 && x < screen[i-1]->rectx; i--) {
		w1 = screen[i-1];
		screen[i-1] = screen[i];
		screen[i] = w1;
		j--;
	}
	if (j != 0)
		/* window swap */
		for (i=0, x=0; (w = screen[i]); i++) {
			move(w, x, 0, w->rectw, fheight);
			x += w->rectw + g->border;
		}
	else if (i != 0) {
		/* window resize */
		w1 = screen[i-1];
		dx = x - w->rectx;
		move(w, x, 0, w->rectw - dx, fheight);
		move(w1, w1->rectx, 0, w1->rectw + dx, fheight);
	}
}

/* win_resize_frame - Called when the whole frame
 * is resized.
 */
void
win_resize_frame(int w1, int h1)
{
	int i, x, w;

	assert(w1!=0 && h1!=0);
	for (i=0, x=0; screen[i]; i++) {
		w = (screen[i]->rectw * w1) / fwidth;
		if (!screen[i+1])
			w = w1 - x;
		move(screen[i], x, 0, w, h1);
		x += w + g->border;
	}
	fwidth = w1;
	fheight = h1;
}

/* win_redraw_frame - Redraw the whole frame.
 */
void
win_redraw_frame(W *focus, int insert)
{
	GRect b;
	W *w;
	int i;

	b = (GRect){ 0, 0, g->border, fheight };
	for (i=0; (w = screen[i]); i++) {
		assert(!screen[i+1] || w->rectx + w->rectw + g->border == screen[i+1]->rectx);
		if (dirty(w)) {
			if (screen[i+1]) {
				b.x = w->rectx + w->rectw;
				g->drawrect(&b, 0, 0, b.w, b.h, GGray);
			}
			draw(w, GPaleYellow, focus, insert);
			if (tag.owner == w)
				tag.win.dirty = 1;
		}
	}
	if (tag.visible && dirty(&tag.win)) {
		b = tag.win.rect;
		b.y -= g->border;
		g->drawrect(&b, 0, 0, b.w, g->border, GGray);
		draw(&tag.win, GPaleGreen, focus, insert);
	}
	g->sync();
}

/* win_scroll - Scroll the window by [n] lines.
 * If [n] is negative it will scroll backwards.
 */
void
win_scroll(W *w, int n)
{
	struct lineinfo li;
	unsigned start, bol;
	int top;

	if (n == 0)
		return;

	start = w->l[0];

	if (n < 0) {
		do {
			if (start == 0)
				/* already at the top */
				break;
			bol = buf_bol(&w->eb->b, start-1);

			lineinfo(w, bol-1, start-1, &li);
			assert(li.len > 0);
			top = li.len - 1;
			for (; n<0 && top>=0; top--, n++) {
				start = li.sl[(li.beg + top) % RingSize];
				assert(start < w->l[0]);
			}
		} while (n<0);
	} else {
		do {
			lineinfo(w, start, -1, &li);
			assert(li.len > 0);
			top = 0;
			for (; n>0 && top<li.len; top++, n--) {
				start = li.sl[(li.beg + top) % RingSize];
				assert(start > w->l[0] || w->l[0] >= w->eb->b.limbo);
			}
		} while (n>0);
	}

	w->l[0] = start;
	win_update(w);
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

	bol = buf_bol(&w->eb->b, w->cu);
	lineinfo(w, bol-1, w->cu, &li);
	assert(li.len > 0);
	w->l[0] = li.sl[(li.beg + li.len-1) % RingSize];
	if (where == CBot)
		win_scroll(w, -w->nl + 1);
	else if (where == CMid)
		win_scroll(w, -w->nl / 2);
	else
		win_update(w);
}

W *
win_tag_toggle(W *w)
{
	if (tag.visible) {
		tag.visible = 0;
		tag.owner->dirty = 1;
		if (w == &tag.win)
			return tag.owner;
	}

	tag.visible = 1;
	tag.owner = w;
	move(&tag.win, w->rectx, w->recth - w->recth/TagRatio, w->rectw, w->recth/TagRatio);
	w->dirty = 1;

	return &tag.win;
}

/* win_text - If the window is a tag window, return its corresponding
 * text window.  Otherwise, the passed window is returned.
 */
W *
win_text(W *w)
{
	if (w == &tag.win)
		return tag.owner;
	return w;
}

/* win_update - Recompute the appearance of the window,
 * this should be called whenever the content of the underlying
 * buffer changes or when the dimensions of the window have
 * changed.
 */
void
win_update(W *w)
{
	struct lineinfo li;
	int l, top;

	for (l=1; l<=w->nl;) {
		lineinfo(w, w->l[l-1], -1, &li);
		assert(li.len > 0);
		top = 0;
		for (; top<li.len; top++, l++)
			w->l[l] = li.sl[(li.beg + top) % RingSize];
	}
	w->rev = eb_revision(w->eb);
	w->dirty = 1;
}

/* static functions */

static int
dirty(W *w)
{
	if (w->rev != eb_revision(w->eb))
		win_update(w);
	return w->dirty;
}

/* runewidth - returns the width of a given
 * rune, if called on '\n', it returns 0.
 */
static int
runewidth(Rune r, int x)
{
	int rw;

	if (r == '\t') {
		rw = tabw - x % tabw;
	} else if (r == '\n') {
		rw = 0;
	} else
		rw = g->textwidth(&r, 1);

	return rw;
}

struct frag {
	Rune b[MaxWidth];
	int n;
	int x, y, w;
};

static void
pushfrag(struct frag *f, Rune r, int rw)
{
	assert(f->n < MaxWidth);
	f->b[f->n++] = r;
	f->w += rw;
}

static void
flushfrag(struct frag *f, W *w, int x, int y, int sel)
{
	if (sel)
		g->drawrect(&w->rect, f->x, f->y-font.ascent, f->w, font.height, GPaleBlue);
	g->drawtext(&w->rect, f->b, f->n, f->x, f->y, GBlack);
	f->n = 0;
	f->x = x;
	f->y = y;
	f->w = 0;
}

static void
draw(W *w, GColor bg, W *focus, int insert)
{
	struct frag f;
	int x, y, cx, cy, cw, rw, sel;
	unsigned *next, c, s0, s1;
	Rune r;

	s0 = eb_getmark(w->eb, SelBeg);
	s1 = eb_getmark(w->eb, SelEnd);
	if (s0 == -1u || s1 == -1u)
		s0 = s1 = 0;

	g->drawrect(&w->rect, 0, 0, w->rectw, w->recth, bg);

	sel = 0;
	cw = 0;
	x = g->hmargin;
	y = g->vmargin + font.ascent;
	f = (struct frag){.x=x, .y=y};
	next = &w->l[1];

	for (c=w->l[0];; c++) {
		if (c >= *next) {
			assert(c == *next);
			x = g->hmargin;
			y += font.height;
			next++;
			flushfrag(&f, w, x, y, sel);
			if (next - w->l > w->nl)
				break;
		}

		if (sel ^ (s0 <= c && c < s1)) {
			flushfrag(&f, w, x, y, sel);
			sel = !sel;
		}

		r = buf_get(&w->eb->b, c);
		rw = runewidth(r, x - g->hmargin);

		if (c == w->cu) {
			cx = x;
			cy = y - font.ascent;
			cw = rw ? rw : 4;
		}

		x += rw;
		if (r == '\t') {
			pushfrag(&f, ' ', rw);
			flushfrag(&f, w, x, y, sel);
		} else if (r != '\n')
			pushfrag(&f, r, rw);
	}

	if (cw != 0 && w == focus)
		g->drawcursor(&w->rect, insert, cx, cy, cw);
	g->decorate(&w->rect, w->eb->path && w->eb->frev != eb_revision(w->eb), GGray);
	w->dirty = 0;
}

/* move - Resize and recompute appearance
 * of a window, dimensions must be non-zero.
 */
void
move(W *pw, int x, int y, int w, int h)
{
	int tagh;

	assert(w!=0 && h!=0);
	pw->nl = (h - g->vmargin) / font.height;
	if (pw->nl == 0)
		pw->nl = 1;
	assert(pw->nl < MaxHeight);
	if (tag.visible && tag.owner == pw) {
		tagh = (tag.win.recth * h) / pw->recth;
		move(&tag.win, x, h - tagh, w, tagh);
	}
	pw->rect = (GRect){x, y, w, h};
	win_update(pw);
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

/* lineinfo
 *
 * post: if lim != -1u, then li.len>1 at exit.
 * note: lim == off is okay, only lim will be
 * in the lineinfo offset list in this case.
 * note: there is a special case when off == -1u
 * to avoid special cases for the first line
 * in the buffer.
 */
static void
lineinfo(W *w, unsigned off, unsigned lim, struct lineinfo *li)
{
	Rune r;
	int x, rw;

	li->beg = li->len = 0;
	x = 0;

	for (;;) {
		r = buf_get(&w->eb->b, off);
		rw = runewidth(r, x);

		if (g->hmargin+x+rw > w->rectw)
		if (x != 0) { /* force progress */
			if (pushoff(li, off, lim != -1u) == 0)
				break;
			x = 0;
			continue;
		}

		/* the termination check is after the
		 * line length check to handle long
		 * broken lines, if we don't do this,
		 * line breaks are undetected in the
		 * following configuration:
		 *
		 * |xxxxxxxxx|
		 * |xxxxxx\n |
		 * |^        |
		 *   lim
		 */
		if (off != -1u && off >= lim)
			break;

		x += rw;
		off++;

		if (r == '\n') {
			if (pushoff(li, off, lim != -1u) == 0)
				break;
			x = 0;
		}
	}
}


#ifdef WIN_TEST
/* test */

#include <stdlib.h>
#include <sys/select.h>
#include <sys/time.h>

void ex_cancel(Task *t) { (void)t; }
void die(char *m) { exit(1); }

int main()
{
	GEvent e;
	EBuf *eb;
	W *ws[N], *w;
	int i;
	enum CursorLoc cloc;
	unsigned char s[] =
	"je suis\n"
	"x\tQcar\n"
	"tab\ttest\n"
	"une longue longue longue longue longue longue longue longue longue longue longue longue longue ligne\n"
	"un peu d'unicode: ä æ ç\n"
	"et voila!\n\n";

	eb = eb_new(-1);
	gui_x11.init();
	win_init(&gui_x11);
	for (i=0; i<N; i++) {
		ws[i] = win_new();
		eb_kill(ws[i]->eb);
		ws[i]->eb = eb;
	}
	w = ws[0];

	for (i=0; i<5; i++)
		eb_ins_utf8(eb, 0, s, sizeof s - 1);
	for (i=0; i<5; i++)
		eb_ins_utf8(tag.win.eb, 0, (unsigned char *)"TAG WINDOW\n", 10);

	do {
		select(0, 0, 0, 0, &(struct timeval){ 0, 30000 });
		win_redraw_frame(w, 0);
		if (!g->nextevent(&e))
			continue;
		if (e.type == GResize)
			win_resize_frame(e.resize.width, e.resize.height);
		if (e.type == GKey) {
			switch (e.key) {
			case 'l': ++w->cu; cloc = CBot; break;
			case 'h': if (w->cu) --w->cu; cloc = CTop; break;
			case 'e'-'a' + 1: win_scroll(w,  1); break;
			case 'y'-'a' + 1: win_scroll(w, -1); break;
			// case '+': if (w->hrig < 25000) w->hrig += 1 + w->hrig/10; break;
			// case '-': if (w->hrig > 10) w->hrig -= 1 + w->hrig/10; break;
			case 'l'-'a'+1: win_show_cursor(w, CMid); break;
			default:
				if (e.key >= '1' && e.key <= '9') {
					int n = e.key - '1';
					if (n < N)
						win_tag_toggle(&w[n]);
				}
				continue;
			}
			win_update(w);
			if (e.key == 'l' || e.key == 'h') {
				if (w->cu < w->l[0] || w->cu >= w->l[w->nl])
						win_show_cursor(w, cloc);
			}
		}
	} while (e.type != GKey || e.key != 'q');

	g->fini();
	return 0;
}

#endif
