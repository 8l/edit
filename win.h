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
	MaxWins = 6,        /* maximal number of windows */
};

struct w {
	unsigned start, stop;  /* offset of the first/last character displayed */
	unsigned cu;           /* cursor offset */
	int hrig;              /* horizontal rigidity */
	EBuf *eb;              /* underlying buffer object */
	GRect gr;              /* location on the screen */
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
W *win_tag_win(void);
W *win_tag_owner(void);
void win_tag_toggle(W *);

#endif /* ndef WIN_H */
