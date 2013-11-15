/* A GUI module for X11 */

#include <assert.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include "gui.h"

void die(char *);

//#define FONTNAME "DejaVu Sans Mono:pixelsize=12"
#define FONTNAME "Liberation Mono:pixelsize=12"

enum {
	Width = 640,
	Height = 480,
};

typedef struct xwin XWin;
struct xwin {
	GWin gw;
	Pixmap p;
	XftDraw *xft;
	int used;
};

static Display *d;
static Visual *visual;
static Colormap cmap;
static unsigned int depth;
static int screen;
static GC gc;
static XftFont *font;
static Window win;
static int w, h;
static XWin wins[MaxWins];

static void
init()
{
	XWindowAttributes wa;
	XSetWindowAttributes swa;
	XGCValues gcv;
	Window root;
	XConfigureEvent ce;

	d = XOpenDisplay(0);
	if (!d)
		die("cannot open display");
	root = DefaultRootWindow(d);
	XGetWindowAttributes(d, root, &wa);
	visual = wa.visual;
	cmap = wa.colormap;
	screen = DefaultScreen(d);
	depth = DefaultDepth(d, screen);

	/* create the main window */
	win = XCreateSimpleWindow(d, root, 0, 0, Width, Height, 0, 0,
	                          WhitePixel(d, screen));
	swa.backing_store = WhenMapped;
	swa.bit_gravity = NorthWestGravity;
	XChangeWindowAttributes(d, win, CWBackingStore|CWBitGravity, &swa);
	XStoreName(d, win, "ED");
	XSelectInput(d, win, StructureNotifyMask|ButtonPressMask|KeyPressMask|ExposureMask);

	/* simulate an initial resize and map the window */
	ce.type = ConfigureNotify;
	ce.width = Width;
	ce.height = Height;
	XSendEvent(d, win, False, StructureNotifyMask, (XEvent *)&ce);
	XMapWindow(d, win);

	/* allocate font */
	font = XftFontOpenName(d, screen, FONTNAME);
	if (!font)
		die("cannot open default font");

	/* initialize gc */
	gcv.foreground = WhitePixel(d, screen);
	gcv.graphics_exposures = False;
	gc = XCreateGC(d, win, GCForeground|GCGraphicsExposures, &gcv);
}

static void
fini()
{
	XCloseDisplay(d);
}

static void
getfont(GFont *ret)
{
	ret->data = font;
	ret->ascent = font->ascent;
	ret->descent = font->descent;
	ret->width = font->max_advance_width;
	ret->height = font->height;
}

static void
movewin(GWin *gw, int x, int y, int w, int h)
{
	XWin *xw;

	gw->x = x;
	gw->y = y;
	if (gw->w == w & gw->h == h)
		return;

	xw = (XWin *)gw;
	gw->w = w;
	gw->h = h;
	if (xw->xft) {
		XftDrawDestroy(xw->xft);
		XFreePixmap(d, xw->p);
	}

	xw->p = XCreatePixmap(d, win, w, h, depth);
	xw->xft = XftDrawCreate(d, xw->p, visual, cmap);
	XFillRectangle(d, xw->p, gc, 0, 0, w, h);
}

static GWin *
newwin(int x, int y, int w, int h)
{
	int i;
	XWin *xw;

	assert(w != 0 || h != 0);

	xw = 0;
	for (i=0; i<MaxWins; i++) {
		if (wins[i].used)
			continue;
		xw = &wins[i];
		break;
	}
	if (!xw)
		return 0;

	xw->used = 1;
	xw->gw.w = 0;
	xw->gw.h = 0;
	xw->xft = NULL;
	movewin((GWin *)xw, x, y, w, h);
	return (GWin *)xw;
}

static void
delwin(GWin *gw)
{
	XWin *xw;

	xw = (XWin *)gw;
	XftDrawDestroy(xw->xft);
	XFreePixmap(d, xw->p);
	xw->used = 0;
}

static void
xftcolor(XftColor *xc, GColor c)
{
	xc->color.red = c.red << 8;
	xc->color.green = c.green << 8;
	xc->color.blue = c.blue << 8;
	xc->color.alpha = 65535;
	XftColorAllocValue(d, visual, cmap, &xc->color, xc);
}

static void
drawtext(GWin *gw, Rune *str, int len, int x, int y, GColor c)
{
	XftColor col;
	XWin *xw;

	xw = (XWin *)gw;
	xftcolor(&col, c);
	XftDrawString32(xw->xft, &col, font, x, y, (FcChar32 *)str, len);
}

static void
drawrect(GWin *gw, int x, int y, int w, int h, GColor c)
{
	XWin *xw;

	xw = (XWin *)gw;
	if (c.x) {
		XGCValues gcv;
		GC gc;

		gcv.foreground = WhitePixel(d, screen);
		gcv.function = GXxor;
		gc = XCreateGC(d, xw->p, GCFunction|GCForeground, &gcv);
		XFillRectangle(d, xw->p, gc, x, y, w, h);
		XFreeGC(d, gc);
	} else {
		XftColor col;

		xftcolor(&col, c);
		XftDrawRect(xw->xft, &col, x, y, w, h);
	}
}

static void
putwin(GWin *gw)
{
	 XWin *xw;

	 xw = (XWin *)gw;
	 XCopyArea(d, xw->p, win, gc, 0, 0, gw->w, gw->h, gw->x, gw->y);
}

static int
textwidth(Rune *str, int len)
{
	XGlyphInfo gi;

	XftTextExtents32(d, font, (FcChar32 *)str, len, &gi);
	return gi.xOff;
}

static void
nextevent(GEvent *gev)
{
	XEvent e;

	do {
		XNextEvent(d, &e);
		switch (e.type) {

		case Expose:
		{
			XWin *w;

			/* we could suck all exposes here */
			for (w=wins; w-wins<MaxWins; w++) {
				if (!w->used)
					continue;
				putwin((GWin *)w);
			}
			continue;
		}

		case ConfigureNotify:
			if (e.xconfigure.width == w)
			if (e.xconfigure.height == h)
				continue;

			w = e.xconfigure.width;
			h = e.xconfigure.height;

			gev->type = GResize;
			gev->resize.width = w;
			gev->resize.height = h;
			break;

		case ButtonPress:
		case ButtonRelease:
			if (e.type == ButtonPress)
				gev->type = GButPress;
			else
				gev->type = GButRelease;
			switch (e.xbutton.button) {
			case Button1:
				gev->button = GBLeft;
				break;
			case Button2:
				gev->button = GBMiddle;
				break;
			case Button3:
				gev->button = GBRight;
				break;
			case Button4:
				gev->button = GBWheelUp;
				break;
			case Button5:
				gev->button = GBWheelDown;
				break;
			default:
				continue;
			}
			break;

		case KeyPress:
		{
			int len;
			char buf[8];
			KeySym key;

			gev->type = GKey;
			len = XLookupString(&e.xkey, buf, 8, &key, 0);
			switch (key) {
			case XK_F1:
			case XK_F2:
			case XK_F3:
			case XK_F4:
			case XK_F5:
			case XK_F6:
			case XK_F7:
			case XK_F8:
			case XK_F9:
			case XK_F10:
			case XK_F11:
			case XK_F12:
				gev->key = GKF1 + (key - XK_F1);
				break;
			case XK_Up:
				gev->key = GKUp;
				break;
			case XK_Down:
				gev->key = GKDown;
				break;
			case XK_Left:
				gev->key = GKLeft;
				break;
			case XK_Right:
				gev->key = GKRight;
				break;
			case XK_Prior:
				gev->key = GKPageUp;
				break;
			case XK_Next:
				gev->key = GKPageDown;
				break;
			case XK_BackSpace:
				gev->key = GKBackspace;
				break;
			default:
				if (len == 0)
					continue;
				if (buf[0] == '\r')
					buf[0] = '\n';
				gev->key = buf[0];
				break;
			}
			break;
		}

		default:
			continue;
		}
		return;
	} while (1);
}

struct gui gui_x11 = {
	.init		= init,
	.fini		= fini,
	.getfont	= getfont,
	.newwin		= newwin,
	.movewin	= movewin,
	.delwin		= delwin,
	.drawtext	= drawtext,
	.drawrect	= drawrect,
	.putwin		= putwin,
	.textwidth	= textwidth,
	.nextevent	= nextevent,
};
