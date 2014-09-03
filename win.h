#define rectx rect.x
#define recty rect.y
#define rectw rect.w
#define recth rect.h

typedef struct w W;

#define TagInit \
	"Look \n" \
	"Get \n" \
	"New\n" \
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
	unsigned rev;          /* buffer revision used for line offsets */
	unsigned cu;           /* cursor offset */
	EBuf *eb;              /* underlying buffer object */
	GRect rect;            /* rectangle on the screen */
	int dirty;             /* force redraw */
};

enum CursorLoc { CTop, CMid, CBot };

void win_init(struct gui *g);
W *win_new(EBuf *eb);
void win_delete(W *);
unsigned win_at(W *w, int x, int y);
W *win_which(int x, int y);
void win_move(W *, int x, int y);
void win_resize_frame(int w, int h);
void win_redraw_frame(void);
void win_scroll(W *, int);
void win_show_cursor(W *, enum CursorLoc);
W *win_tag_toggle(W *);
W *win_text(W *);
void win_update(W *);
