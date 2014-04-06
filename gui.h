#ifndef GUI_H
#define GUI_H
/* GUI module definition */

#include "unicode.h"

typedef struct gcolor GColor;
typedef struct gfont  GFont;
typedef struct gwin   GWin;
typedef struct gevent GEvent;

enum {
	MaxWins = 9,
};

struct gcolor {
	unsigned char red;
	unsigned char green;
	unsigned char blue;
	unsigned char x;
};

#define GBlack      (GColor){ 0, 0, 0, 0 }
#define GPaleGreen  (GColor){ 231, 255, 221, 0 }
#define GPalePink   (GColor){ 255, 248, 221, 0 }
#define GPaleYellow (GColor){ 255, 255, 234, 0 }
#define GPinkLace   (GColor){ 255, 221, 244, 0 }
#define GXBlack     (GColor){ 0, 0, 0, 1 }

struct gfont {
	void *data;
	int ascent;
	int descent;
	int height;
};

struct gwin {
	int x, y;
	int w, h;
};

struct gevent {
	enum {
		GResize,
		GKey,
		GButPress,
		GButRelease,
	} type;

	union {
		struct {
			int width;
			int height;
		} resize;
		Rune key;
		enum {
			GBLeft,
			GBMiddle,
			GBRight,
			GBWheelUp,
			GBWheelDown,
		} button;
	};
};

enum {
	GKEsc = 0x1b,

	GKF1 = 0xe001, /* unicode private use area */
	GKF2,
	GKF3,
	GKF4,
	GKF5,
	GKF6,
	GKF7,
	GKF8,
	GKF9,
	GKF10,
	GKF11,
	GKF12,
	GKLeft,
	GKRight,
	GKUp,
	GKDown,
	GKPageUp,
	GKPageDown,
	GKBackspace,
};

struct gui {
	void (*init)(void);
	void (*fini)(void);
	void (*getfont)(GFont *fret);
	GWin *(*newwin)(int x, int y, int w, int h);
	void (*movewin)(GWin *gw, int x, int y, int w, int h);
	void (*delwin)(GWin *gw);
	void (*drawtext)(GWin *gw, Rune *str, int len,
	                 int x, int y, GColor color);
	void (*drawrect)(GWin *gw, int x, int y, int w, int h, GColor c);
	void (*putwin)(GWin *gw);
	int (*textwidth)(Rune *str, int len);
	void (*nextevent)(GEvent *eret);
};

/* Available gui modules */
extern struct gui gui_x11;

#endif /* ndef GUI_H */
