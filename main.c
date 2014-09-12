#include <assert.h>
#include <ctype.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "unicode.h"
#include "cmd.h"
#include "buf.h"
#include "edit.h"
#include "gui.h"
#include "win.h"
#include "exec.h"
#include "evnt.h"

W *curwin;
int scrolling;

static struct gui *g;
static int needsredraw;
static int clicks;


static int
risword(Rune r)
{
	return risascii(r) && (isalnum(r) || r == '_');
}

static void
redraw()
{
	assert(needsredraw);
	win_redraw_frame();
	needsredraw = 0;
}

static void
resetclicks()
{
	clicks = 0;
}

static int
gev(int fd, int flag, void *unused)
{
	enum {
		RedrawDelay = 16, /* in milliseconds */
		DoubleClick = 200,
	};
	static unsigned selbeg;
	static W *mousewin;
	static int resizing;
	unsigned p0, p1;
	GEvent e;
	Buf *b;

	(void)fd; (void)flag; (void)unused;
	while (g->nextevent(&e)) {
		if (!needsredraw) {
			ev_alarm(RedrawDelay, redraw);
			needsredraw = 1;
		}
		switch (e.type) {
		case GResize:
			win_resize_frame(e.resize.width, e.resize.height);
			break;
		case GKey:
			cmd_parse(e.key);
			win_update(curwin);
			if (!scrolling)
			if (curwin->cu >= curwin->l[curwin->nl]
			|| curwin->cu < curwin->l[0])
				win_show_cursor(curwin, CMid);
			scrolling = 0;
			break;
		case GMouseDown:
			mousewin = win_which(e.mouse.x, e.mouse.y);
			if (!mousewin)
				break;
			if (e.mouse.button == GBLeft) {
				if (e.mouse.x - mousewin->rectx < g->actionr.w)
				if (e.mouse.y - mousewin->recty < g->actionr.h) {
					g->setpointer(GPResize);
					resizing = 1;
					break;
				}
				curwin = mousewin;
				p0 = win_at(mousewin, e.mouse.x, e.mouse.y);
				selbeg = p0;
				ev_alarm(DoubleClick, resetclicks);
				clicks++;
				if (clicks > 1) {
					p1 = p0 + 1;
					b = &curwin->eb->b;
					while (p0 && risword(buf_get(b, p0-1)))
						p0--;
					while (risword(buf_get(b, p1)))
						p1++;
					eb_setmark(curwin->eb, SelBeg, p0);
					eb_setmark(curwin->eb, SelEnd, p1);
				}
				goto Setcursor;
			} else if (e.mouse.button == GBMiddle) {
				p0 = win_at(mousewin, e.mouse.x, e.mouse.y);
				ex_run(mousewin, p0);
			} else if (e.mouse.button == GBWheelUp) {
				win_scroll(mousewin, -4);
			} else if (e.mouse.button == GBWheelDown) {
				win_scroll(mousewin, +4);
			}
			break;
		case GMouseUp:
			if (resizing) {
				resizing = 0;
				win_move(mousewin, e.mouse.x, e.mouse.y);
				g->setpointer(GPNormal);
			}
			break;
		case GMouseSelect:
			if (resizing)
				break;
			p0 = win_at(mousewin, e.mouse.x, e.mouse.y);
			if (p0 != selbeg) {
				eb_setmark(curwin->eb, SelBeg, selbeg);
				eb_setmark(curwin->eb, SelEnd, p0);
			}
			goto Setcursor;
		default:
			break;
		}
		selbeg = -1u;
		if (curwin->cu >= curwin->l[curwin->nl])
			curwin->cu = curwin->l[curwin->nl-1];
		if (curwin->cu < curwin->l[0])
			curwin->cu = curwin->l[0];
		continue;
	Setcursor:
		curwin->cu = p0;
		curwin->dirty = 1;
	}
	return 0;
}

int
main(int ac, char *av[])
{
	int guifd;

	g = &gui_x11;
	guifd = g->init();
	ev_register((Evnt){guifd, ERead, gev, 0});
	win_init(g);
	curwin = win_new(eb_new());
	if (ac > 1)
		ex_get(curwin, av[1]);
	gev(0, 0, 0);
	ev_loop();
}

void
die(char *m)
{
	fprintf(stderr, "dying, %s\n", m);
	abort();
}
