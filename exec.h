#ifndef EXEC_H
#define EXEC_H

#include "unicode.h"
#include "win.h"

int ex_run(unsigned);
int ex_look(W *, Rune *, unsigned);
int ex_put(EBuf *, char *);
int ex_get(EBuf *, char *);

#endif /* ndef EXEC_H */
