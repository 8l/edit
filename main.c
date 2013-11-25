#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

#include "cmd.h"
#include "gui.h"
#include "win.h"

W *curwin;
int exiting;

void
die(char *m)
{
	fprintf(stderr, "dying, %s\n", m);
	exit(1);
}

int
main(void)
{
	struct gui *g;
	GEvent e;

	g = &gui_x11;
	win_init(g);

	curwin = win_new(buf_new());

	while (!exiting) {
		g->nextevent(&e);
		switch (e.type) {
		case GResize:
			win_resize_frame(e.resize.width, e.resize.height);
			break;
		case GKey:
			cmd_parse(e.key);
			if (curwin->cu >= curwin->stop)
				win_show_cursor(curwin, CBot);
			if (curwin->cu < curwin->start)
				win_show_cursor(curwin, CTop);
			win_redraw_frame();
			break;
		default:
			break;
		}
	}
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
