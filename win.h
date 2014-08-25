#ifndef WIN_H
#define WIN_H

#include "unicode.h"
#include "buf.h"
#include "edit.h"
#include "gui.h"

typedef struct w W;

#define TagInit \
	"Look \n" \
	"Get \n" \
	"|fmt\n"

enum {
	TabWidth = 8,       /* tabulation width */
	MaxWidth = 500,     /* maximum number of characters on a line */
	MaxHeight = 500,    /* maximum number of lines */
	MaxWins = 6,        /* maximum number of windows */
	TagRatio = 3,       /* fraction of the screen for the tag */
};

struct w {
	unsigned l[MaxHeight]; /* line start offsets */
	int nl;                /* current number of lines */
	unsigned rev;          /* on-screen revision or 0 if dirty */
	unsigned cu;           /* cursor offset */
	EBuf *eb;              /* underlying buffer object */
	GRect gr;              /* location on the screen */
};

enum CursorLoc { CTop, CMid, CBot };

void win_init(struct gui *g);
W *win_new(EBuf *eb);
void win_delete(W *);
W *win_locus(int, int, unsigned *);
void win_resize(W *, int x, int y);
void win_resize_frame(int w, int h);
void win_redraw_frame(void);
void win_scroll(W *, int);
void win_show_cursor(W *, enum CursorLoc);
W *win_tag_toggle(W *);
W *win_text(W *);
void win_update(W *);

#endif /* ndef WIN_H */
