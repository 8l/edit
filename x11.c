/* A GUI module for X11 */

#include <assert.h>
#include <stdio.h>
#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/Xft/Xft.h>

#include "gui.h"

void die(char *);

#define FONTNAME "Source Code Pro:pixelsize=12"

enum {
	Width = 640,
	Height = 480,
};

static Display *d;
static Visual *visual;
static Colormap cmap;
static unsigned int depth;
static int screen;
static GC gc;
static XftFont *font;
static Window win;
static Pixmap pbuf;
XftDraw *xft;
static int w, h;
static int dirty;

static int
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
	XSelectInput(d, win, StructureNotifyMask|ButtonPressMask|Button1MotionMask|KeyPressMask|ExposureMask);

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

	/* initialize back buffer and Xft drawing context */
	pbuf = XCreatePixmap(d, win, Width, Height, depth);
	xft = XftDrawCreate(d, pbuf, visual, cmap);

	return XConnectionNumber(d);
}

static void
fini()
{
	if (pbuf != None) {
		XftDrawDestroy(xft);
		XFreePixmap(d, pbuf);
	}
	XCloseDisplay(d);
}

static void
getfont(GFont *ret)
{
	ret->ascent = font->ascent;
	ret->descent = font->descent;
	ret->height = font->height;
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
drawtext(GRect *clip, Rune *str, int len, int x, int y, GColor c)
{
	XftColor col;

	x += clip->x;
	y += clip->y;

	// set clip!
	xftcolor(&col, c);
	XftDrawString32(xft, &col, font, x, y, (FcChar32 *)str, len);
	dirty = 1;
}

static void
drawrect(GRect *clip, int x, int y, int w, int h, GColor c)
{
	if (x + w > clip->w)
		w = clip->w - x;
	if (y + h > clip->h)
		h = clip->h - y;

	x += clip->x;
	y += clip->y;

	if (c.x) {
		XGCValues gcv;
		GC gc;

		gcv.foreground = WhitePixel(d, screen);
		gcv.function = GXxor;
		gc = XCreateGC(d, pbuf, GCFunction|GCForeground, &gcv);
		XFillRectangle(d, pbuf, gc, x, y, w, h);
		XFreeGC(d, gc);
	} else {
		XftColor col;

		xftcolor(&col, c);
		XftDrawRect(xft, &col, x, y, w, h);
	}
	dirty = 1;
}

static int
textwidth(Rune *str, int len)
{
	XGlyphInfo gi;

	XftTextExtents32(d, font, (FcChar32 *)str, len, &gi);
	return gi.xOff;
}

static void
sync()
{
	if (dirty) {
		XCopyArea(d, pbuf, win, gc, 0, 0, w, h, 0, 0);
		XFlush(d);
		dirty = 0;
	}
}

static int
nextevent(GEvent *gev)
{
	XEvent e;

	while (XEventsQueued(d, QueuedAfterFlush)) {

		XNextEvent(d, &e);
		switch (e.type) {

		case Expose:
			dirty = 1;
			sync();
			continue;

		case ConfigureNotify:
			if (e.xconfigure.width == w)
			if (e.xconfigure.height == h)
				continue;

			w = e.xconfigure.width;
			h = e.xconfigure.height;

			pbuf = XCreatePixmap(d, win, w, h, depth);
			xft = XftDrawCreate(d, pbuf, visual, cmap);

			gev->type = GResize;
			gev->resize.width = w;
			gev->resize.height = h;
			break;

		case MotionNotify:
			gev->type = GMouseSelect;
			gev->mouse.button = GBLeft;
			gev->mouse.x = e.xmotion.x;
			gev->mouse.y = e.xmotion.y;
			break;

		case ButtonPress:
			gev->type = GMouseClick;

			switch (e.xbutton.button) {
			case Button1:
				gev->mouse.button = GBLeft;
				break;
			case Button2:
				gev->mouse.button = GBMiddle;
				break;
			case Button3:
				gev->mouse.button = GBRight;
				break;
			case Button4:
				gev->mouse.button = GBWheelUp;
				break;
			case Button5:
				gev->mouse.button = GBWheelDown;
				break;
			default:
				continue;
			}

			gev->mouse.x = e.xbutton.x;
			gev->mouse.y = e.xbutton.y;
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
		return 1;
	}
	return 0;
}

struct gui gui_x11 = {
	.init		= init,
	.fini		= fini,
	.sync		= sync,
	.getfont	= getfont,
	.drawtext	= drawtext,
	.drawrect	= drawrect,
	.textwidth	= textwidth,
	.nextevent	= nextevent,
};
