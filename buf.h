#ifndef BUF_H
#define BUF_H
/* Buffer management functions */

#include "unicode.h"

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
	Page *n;
};

struct buf {
	Page *p;
	Page *last;
	unsigned lastbeg;
};

void buf_init(Buf *);
void buf_del(Buf *, unsigned);
void buf_ins(Buf *, unsigned, Rune);
Rune buf_get(Buf *, unsigned);
void buf_getlc(Buf *, unsigned, int *, int *);
unsigned buf_bol(Buf *, unsigned);
unsigned buf_eol(Buf *, unsigned);
unsigned buf_setlc(Buf *, int, int);

#endif /* ndef BUF_H */
