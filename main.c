#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "cmd.h"
#include "edit.h"
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
main(int ac, char *av[])
{
	struct gui *g;
	EBuf *eb;
	GEvent e;

	g = &gui_x11;
	win_init(g);

	eb = eb_new();
	curwin = win_new(eb);

	if (ac > 1) {
		FILE *fp = fopen((eb->path = av[1]), "r");

		if (!fp)
			die("cannot open input file");

		for (unsigned char buf[11], *beg=buf;;) {
			size_t rd = fread(beg, 1, sizeof buf - (beg-buf), fp);
			int ins;

			if (rd == 0) break;

			ins = eb_ins_utf8(eb, eb->b.limbo, buf, rd += (beg-buf));
			memmove(buf, buf+ins, rd-ins);
			beg = buf + (rd - ins);
		}
		fclose(fp);
	}

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
