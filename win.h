#ifndef WIN_H
#define WIN_H

#include "unicode.h"
#include "buf.h"
#include "edit.h"
#include "gui.h"

typedef struct w W;

enum {
	FScale = 16384,     /* fixed point scale for fractions */
	TabWidth = 8,       /* tabulation width */
	MaxWidth = 500,     /* maximum width of the screen */
	HMargin = 12,       /* horizontal margin */
	VMargin = 2,        /* vertical margin */
};

struct w {
	unsigned start, stop;  /* offset of the first/last character displayed */
	unsigned cu;           /* cursor offset */
	int hrig, height;      /* horizontal rigidity and height of the window */
	EBuf *eb;              /* underlying buffer object */
	GWin *gw;              /* graphical window associated */
};

enum CursorLoc { CTop, CMid, CBot };

void win_init(struct gui *g);
W *win_new(EBuf *eb);
void win_delete(W *);
void win_redraw(W *);
void win_resize_frame(int w, int h);
void win_redraw_frame(void);
void win_scroll(W *, int);
void win_show_cursor(W *, enum CursorLoc);
void win_toggle_tag(W *);

#endif /* ndef WIN_H */
