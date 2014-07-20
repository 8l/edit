#include <assert.h>
#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmd.h"
#include "edit.h"
#include "evnt.h"
#include "gui.h"
#include "win.h"

W *curwin;
int scrolling;

static struct gui *g;

void
die(char *m)
{
	fprintf(stderr, "dying, %s\n", m);
	exit(1);
}

static int
gev(int fd, int flag, void *unused)
{
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
		case GMouseClick:
			if (e.mouse.button == GBLeft)
				win_set_cursor(curwin, e.mouse.x, e.mouse.y);
			break;
		default:
			break;
		}

		if (curwin->cu >= curwin->l[curwin->nl])
			curwin->cu = curwin->l[curwin->nl-1];
		if (curwin->cu < curwin->l[0])
			curwin->cu = curwin->l[0];
	}

	win_redraw_frame();
	g->sync();
	return 0;
}

int
main(int ac, char *av[])
{
	int guifd;
	EBuf *eb;

	g = &gui_x11;
	guifd = g->init();
	ev_register((E){guifd, ERead, gev, 0});
	win_init(g);

	eb = eb_new();
	eb_read(eb, eb->path = ac > 1 ? av[1] : "dummy.txt");
	curwin = win_new(eb);

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
