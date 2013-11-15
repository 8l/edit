#ifndef WIN_H
#define WIN_H

#include "unicode.h"
#include "buf.h"
#include "gui.h"

enum {
	FScale = 16384,     /* fixed point scale */
	TabWidth = 8,       /* tabulation width */
	MaxLines = 1000,    /* max number of lines on screen */
	MaxWidth = 500,     /* max width of the screen */
};

typedef struct w W;
struct w {
	unsigned start, stop;  /* offset of the first/last character displayed */
	unsigned coff;         /* cursor offset */
	int cl;                /* cursor line (on screen) */
	int nls;               /* number of displayed lines on screen */
	int vfrac, height;     /* vertical fraction and height of the window */
	Buf *b;                /* underlying buffer object */
	GWin *gw;              /* graphical window associated */
};

void win_init(struct gui *g);
W *win_new(Buf *b);
void win_resize_frame(int w, int h);

#endif /* ndef WIN_H */
