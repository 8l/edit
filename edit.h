#include <sys/time.h>

/* special marks */
#define SelBeg ((Rune) 'i')	/* selection start */
#define SelEnd ((Rune) 'o')	/* selection end */

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
	time_t ftime;   /* last mtime when written/read */
	unsigned frev;  /* last revision written */
	int refs;       /* ref count, if <0 zombie buffer */
};

EBuf *eb_new(void);
void eb_kill(EBuf *);
void eb_clr(EBuf *, int);
unsigned eb_revision(EBuf *);
void eb_del(EBuf *, unsigned, unsigned);
void eb_ins(EBuf *, unsigned, Rune);
int eb_ins_utf8(EBuf *, unsigned, unsigned char *, int);
void eb_commit(EBuf *);
void eb_undo(EBuf *, int, unsigned *);
void eb_yank(EBuf *, unsigned, unsigned, YBuf *);
void eb_setmark(EBuf *, Rune, unsigned);
unsigned eb_getmark(EBuf *, Rune);
unsigned eb_look(EBuf *, unsigned, Rune *, unsigned);
void eb_write(EBuf *, int);
