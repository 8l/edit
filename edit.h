#ifndef EDIT_H
#define EDIT_H

#include "unicode.h"
#include "buf.h"

typedef struct log  Log;
typedef struct mark Mark;
typedef struct ebuf EBuf;
typedef struct ybuf YBuf;

struct ybuf {
	Rune *r;
	unsigned nr;
	unsigned sz;
	int linemode;
};

struct ebuf {
	Buf b;		/* base text buffer */
	Log *undo;	/* undo redo logs */
	Log *redo;
	Mark *ml;	/* buffer marks */
	char *path;	/* file path */
};

EBuf *eb_new(void);
void eb_del(EBuf *, unsigned, unsigned);
void eb_ins(EBuf *, unsigned, Rune);
int eb_ins_utf8(EBuf *, unsigned, unsigned char *, int);
void eb_commit(EBuf *);
void eb_undo(EBuf *, int, unsigned *);
void eb_yank(EBuf *, unsigned, unsigned, YBuf *);
void eb_setmark(EBuf *, Rune, unsigned);
unsigned eb_getmark(EBuf *, Rune);
unsigned eb_look(EBuf *, unsigned, Rune *, int);
int eb_read(EBuf *, char *);
int eb_write(EBuf *);

#endif /* ndef EDIT_H */
