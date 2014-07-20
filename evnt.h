#ifndef EVNT_H
#define EVNT_H

typedef struct e E;

enum {
	ERead = 1,
	EWrite = 2,
};

struct e {
	int fd;
	int flags;
	int (*f)(int, int, void *);
	void *p;
};

void ev_register(E);
void ev_loop(void);

#endif /* ndef EVNT_H */
