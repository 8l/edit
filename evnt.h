#ifndef EVNT_H
#define EVNT_H

#include <sys/time.h>

enum {
	MaxAlrms = 15, /* max number of concurrent alarms */
};

typedef struct alrm Alrm;
typedef struct evnt Evnt;

struct alrm {
	struct timeval t;
	void (*f)(struct timeval *, void *);
	void *p;
};

enum {
	ERead = 1,
	EWrite = 2,
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
