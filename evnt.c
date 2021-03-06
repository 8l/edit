#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>

#include "evnt.h"

void die(char *);
int exiting;

#define tgt(a, b) \
	((a).tv_sec == (b).tv_sec ? \
	(a).tv_usec > (b).tv_usec : \
	(a).tv_sec > (b).tv_sec)

typedef struct evnt  Evnt;
typedef struct alarm Alarm;

struct evnt {
	int valid;
	int fd;
	int flags;
	void (*f)(int, int, void *);
	void *p;
};

struct alarm {
	struct timeval t;
	void (*f)();
};

static void pushalarm(Alarm);
static void popalarm(void);

static Alarm ah[MaxAlarms + 1];
static int na;
static Evnt *elist;
static int ne;
static struct timeval curtime;


void
ev_time(struct timeval *t)
{
	if (!curtime.tv_sec)
		gettimeofday(&curtime, 0);
	*t = curtime;
}

int
ev_alarm(int ms, void (*f)())
{
	Alarm a;

	if (na >= MaxAlarms)
		return 1;
	a.f = f;
	ev_time(&a.t);
	a.t.tv_usec += ms * 1000;
	if (a.t.tv_usec >= 1000000) {
		a.t.tv_sec++;
		a.t.tv_usec -= 1000000;
	}
	pushalarm(a);
	return 0;
}

void
ev_register(int fd, int flags, void (*f)(int, int, void *), void *p)
{
	elist = realloc(elist, (ne + 1) * sizeof(Evnt));
	assert(elist);
	elist[ne] = (Evnt){1, fd, flags, f, p};
	ne++;
}

void
ev_cancel(int fd)
{
	int i;

	for (i=0; i<ne; i++)
		if (elist[i].fd == fd) {
			elist[i].valid = 0;
			return;
		}
	assert(0);
}

void
ev_loop()
{
	struct timeval tv;
	Alarm a;
	fd_set rfds, wfds;
	int i, j, maxfd, flags;

	while (!exiting) {
		maxfd = -1;
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		for (i=0; i < ne; i++) {
			assert(elist[i].valid);
			if (elist[i].flags & ERead)
				FD_SET(elist[i].fd, &rfds);
			if (elist[i].flags & EWrite)
				FD_SET(elist[i].fd, &wfds);
			if (elist[i].fd > maxfd)
				maxfd = elist[i].fd;
		}
		if (na) {
			gettimeofday(&tv, 0);
			if (!tgt(ah[1].t, tv))
				tv = (struct timeval){0, 0};
			else {
				tv.tv_sec = ah[1].t.tv_sec - tv.tv_sec;
				tv.tv_usec = ah[1].t.tv_usec - tv.tv_usec;
				if (tv.tv_usec < 0) {
					tv.tv_sec--;
					tv.tv_usec += 1000000;
				}
			}
		} else
			tv = (struct timeval){10000, 0};
		if (select(maxfd+1, &rfds, &wfds, 0, &tv) == -1) {
			if (errno == EINTR)
				continue;
			die("select error");
		}
		gettimeofday(&curtime, 0);
		while (na && !tgt(ah[1].t, curtime)) {
			a = ah[1];
			popalarm();
			a.f();
		}
		for (i=0; i<ne; i++) {
			flags = 0;
			if (FD_ISSET(elist[i].fd, &rfds))
				flags |= ERead;
			if (FD_ISSET(elist[i].fd, &wfds))
				flags |= EWrite;
			if (flags == 0 || !elist[i].valid)
				continue;
			elist[i].f(elist[i].fd, flags, elist[i].p);
		}
		for (i=j=0; i<ne; i++)
			if (elist[i].valid)
				elist[j++] = elist[i];
		ne = j;
	}
}

static void
pushalarm(Alarm a)
{
	Alarm t;
	int i, j;

	i = ++na;
	ah[i] = a;
	while ((j = i / 2) && tgt(ah[j].t, ah[i].t)) {
		t = ah[j];
		ah[j] = ah[i];
		ah[i] = t;
		i = j;
	}
}

static void
popalarm()
{
	Alarm t;
	int i, j;

	assert(na);
	i = 1;
	ah[i] = ah[na--];
	while ((j = 2 * i) <= na) {
		if (j+1 <= na)
		if (tgt(ah[j].t, ah[j+1].t))
			j++;
		if (!tgt(ah[i].t, ah[j].t))
			return;
		t = ah[j];
		ah[j] = ah[i];
		ah[i] = t;
		i = j;
	}
}
