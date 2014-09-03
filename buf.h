/* Buffer management functions */

typedef struct page Page;
typedef struct buf  Buf;

enum {
	PageLen = 11,
};

struct page {
	int len;
	int nl;
	int col;
	Rune *hbeg;
	Rune buf[PageLen];
	Page *p;
	Page *n;
};

struct buf {
	Page *p;
	Page *last;
	unsigned lastbeg;
	unsigned limbo;
};

void buf_init(Buf *);
void buf_clr(Buf *);
void buf_del(Buf *, unsigned);
void buf_ins(Buf *, unsigned, Rune);
Rune buf_get(Buf *, unsigned);
void buf_getlc(Buf *, unsigned, int *, int *);
unsigned buf_bol(Buf *, unsigned);
unsigned buf_eol(Buf *, unsigned);
unsigned buf_setlc(Buf *, int, int);
