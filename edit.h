#ifndef EDIT_H
#define EDIT_H

#include "unicode.h"
#include "buf.h"

typedef struct log  Log;
typedef struct ebuf EBuf;

struct ebuf {
	Buf b;		/* base text buffer */
	Log *undo;	/* undo redo logs */
	Log *redo;
};

EBuf *eb_new(void);
void eb_del(EBuf *, unsigned, unsigned);
void eb_ins(EBuf *, unsigned, Rune);
int eb_ins_utf8(EBuf *, unsigned, unsigned char *, int);
void eb_clean(EBuf *);
void eb_undo(EBuf *, int, unsigned *);

#endif /* ndef EDIT_H */
