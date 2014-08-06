#ifndef EVNT_H
#define EVNT_H

#include <sys/time.h>

enum {
	ERead = 1,
	EWrite = 2,

	MaxAlrms = 31,
};

typedef struct alrm Alrm;
typedef struct evnt Evnt;

struct alrm {
	time_t t;
	void (*f)(time_t, void *);
	void *p;
};


struct evnt {
	int fd;
	int flags;
	int (*f)(int, int, void *);
	void *p;
};

int ev_alarm(Alrm);
void ev_register(Evnt);
void ev_loop(void);

#endif /* ndef EVNT_H */
