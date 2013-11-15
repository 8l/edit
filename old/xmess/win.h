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
	unsigned loff[MaxLines];
	unsigned coff;
	int cl;
	int nls;
	int vfrac, height;
	Buf *b;
	GWin *gw;
};

void win_init(struct gui *g);
W *win_new(Buf *b);
void win_resize_frame(int w, int h);

#endif /* ndef WIN_H */
