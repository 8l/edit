#ifndef EVNT_H
#define EVNT_H

#include <sys/time.h>

typedef struct evnt Evnt;

enum {
	MaxAlarms = 15, /* max number of concurrent alarms */
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

int ev_alarm(int, void (*)(void));
void ev_register(Evnt);
void ev_loop(void);

#endif /* ndef EVNT_H */
