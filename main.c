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

static struct gui *g;

void
die(char *m)
{
	fprintf(stderr, "dying, %s\n", m);
	abort();
}

static int
gev(int fd, int flag, void *unused)
{
	static int issel;
	GEvent e;

	(void) fd;
	assert(flag == ERead && unused == 0);

	while (g->nextevent(&e) != 0) {
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
			if (e.mouse.button == GBLeft)
				win_set_cursor(curwin, e.mouse.x, e.mouse.y);
			else if (e.mouse.button == GBWheelUp)
				win_scroll(curwin, -4);
			else if (e.mouse.button == GBWheelDown)
				win_scroll(curwin, +4);
			break;
		case GMouseSelect:
			win_set_cursor(curwin, e.mouse.x, e.mouse.y);
			if (issel)
				eb_setmark(curwin->eb, SelEnd, curwin->cu);
			else
				eb_setmark(curwin->eb, SelBeg, curwin->cu);
			issel = 1;
			goto Select;
		default:
			break;
		}
		issel = 0;
	Select:

		if (curwin->cu >= curwin->l[curwin->nl])
			curwin->cu = curwin->l[curwin->nl-1];
		if (curwin->cu < curwin->l[0])
			curwin->cu = curwin->l[0];
	}

	win_redraw_frame();
	return 0;
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

	gev(0, ERead, 0);
	ev_loop();
}

#if 0
void
dump(Buf *b)
{
	Page *p = b->p;

	for (; p; p = p->n) {
		Rune *r;

		printf("New page:\n-----\n  len: %d\n  nl: %d\n\n", p->len, p->nl);
		for (r=p->buf; r < p->hbeg; r++)
			putchar(*r);
		printf("\n-- HOLE\n");
		for (r=p->hbeg + (PageLen-p->len); r < &p->buf[PageLen]; r++)
			putchar(*r);
		printf("\n---- END OF PAGE\n");
	}
}
#endif
