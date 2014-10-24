/* Buffer management functions */

typedef struct page Page;
typedef struct buf  Buf;

enum {
	PageLen = 1024 - 8,
};

struct page {
	unsigned len;       /* page length */
	unsigned short nl;  /* number of \n in page */
	unsigned short col; /* column of the first rune */
	Rune *hbeg;         /* start of the hole */
	Page *p;            /* link to previous */
	Page *n;            /* link to next */
	Rune buf[PageLen];  /* buffer */
};

struct buf {
	Page *p;           /* first page */
	Page *last;        /* cached page */
	unsigned lastbeg;  /* buffer offset of last */
	unsigned limbo;    /* limbo offset */
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
