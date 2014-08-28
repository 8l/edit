#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmd.h"
#include "edit.h"
#include "evnt.h"
#include "exec.h"
#include "gui.h"
#include "win.h"

W *curwin;
int scrolling;

static void redraw(void);

static struct gui *g;
static int needsredraw;


static int
gev(int fd, int flag, void *unused)
{
	enum { RedrawDelay = 16 /* in milliseconds */ };
	static unsigned selbeg;
	static W *mousewin;
	static int resizing;
	unsigned pos, ne;
	GEvent e;

	(void)fd; (void)flag; (void)unused;
	for (ne = 0; g->nextevent(&e) != 0; ne |= 1) {
		switch (e.type) {
		case GResize:
			win_resize_frame(e.resize.width, e.resize.height);
			break;
		case GKey:
			cmd_parse(e.key);
			win_update(curwin);
			if (!scrolling) {
				if (curwin->cu >= curwin->l[curwin->nl]
				|| curwin->cu < curwin->l[0])
					win_show_cursor(curwin, CMid);
			}
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
				pos = win_at(mousewin, e.mouse.x, e.mouse.y);
				selbeg = pos;
				goto Setcursor;
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
			assert(selbeg != -1u);
			pos = win_at(mousewin, e.mouse.x, e.mouse.y);
			if (pos != selbeg) {
				eb_setmark(curwin->eb, SelBeg, selbeg);
				eb_setmark(curwin->eb, SelEnd, pos);
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
		curwin->cu = pos;
		curwin->rev = 0;
	}
	if (ne && !needsredraw) {
		ev_alarm(RedrawDelay, redraw);
		needsredraw = 1;
	}
	return 0;
}

static void
redraw()
{
	assert(needsredraw);
	win_redraw_frame();
	needsredraw = 0;
	gev(0, 0, 0);
}

int
main(int ac, char *av[])
{
	int guifd;
	EBuf *eb;

	g = &gui_x11;
	guifd = g->init();
	ev_register((Evnt){guifd, ERead, gev, 0});
	win_init(g);
	eb = eb_new();
	if (ac > 1)
		ex_get(eb, av[1]);
	curwin = win_new(eb);
	gev(0, 0, 0);
	ev_loop();
}

void
die(char *m)
{
	fprintf(stderr, "dying, %s\n", m);
	abort();
}
