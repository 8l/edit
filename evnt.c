#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>
#include <sys/time.h>

#include "evnt.h"

void die(char *);
int exiting;

static void popalrm(void);

static Alrm *ah[MaxAlrms + 1];
static int na;
static Evnt *elist;
static int ne;
static time_t curtime;

int
ev_alarm(Alrm a)
{
	Alrm *a1, *t;
	int i, j;

	if (na >= MaxAlrms)
		return 1;
	a1 = malloc(sizeof (Alrm));
	assert(a1);
	*a1 = a;
	i = ++na;
	ah[i] = a1;
	while ((j = i / 2) && ah[j]->t > ah[i]->t) {
		t = ah[j];
		ah[j] = ah[i];
		ah[i] = t;
		i = j;
	}
	return 0;
}

void
ev_register(Evnt e)
{
	elist = realloc(elist, (ne + 1) * sizeof (Evnt));
	assert(elist);
	elist[ne] = e;
	ne++;
}

time_t
ev_time()
{
	assert(curtime);
	return curtime;
}

void
ev_loop()
{
	struct timeval tv;
	Alrm *a;
	fd_set rfds, wfds;
	int i, maxfd, flags;

	tv.tv_usec = 0;
	while (!exiting) {
		maxfd = -1;
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		for (i=0; i < ne; i++) {
			if (elist[i].flags & ERead)
				FD_SET(elist[i].fd, &rfds);
			if (elist[i].flags & EWrite)
				FD_SET(elist[i].fd, &wfds);
			if (elist[i].fd > maxfd)
				maxfd = elist[i].fd;
		}
		if (na) {
			gettimeofday(&tv, 0);
			if (ah[1]->t <= tv.tv_sec)
				tv.tv_sec = 0;
			else
				tv.tv_sec = ah[1]->t - tv.tv_sec;
		} else
			tv.tv_sec = 10000;
		if (select(maxfd+1, &rfds, &wfds, 0, &tv) == -1) {
			if (errno == EINTR)
				continue;
			die("select error");
		}
		gettimeofday(&tv, 0);
		curtime = tv.tv_sec;
		while (na && (a = ah[1])->t <= curtime) {
			popalrm();
			a->f(a->t, a->p);
			free(a);
		}
		for (i=0; i < ne;) {
			flags = 0;
			if (FD_ISSET(elist[i].fd, &rfds))
				flags |= ERead;
			if (FD_ISSET(elist[i].fd, &wfds))
				flags |= EWrite;
			if (flags == 0) {
				i++;
				continue;
			}
			if (elist[i].f(elist[i].fd, flags, elist[i].p)) {
				ne--;
				memmove(&elist[i], &elist[i+1], (ne - i) * sizeof (Evnt));
				continue;
			} else
				i++;
		}
	}
}


/* clear the first alarm out of the heap */
static void
popalrm()
{
	Alrm *t;
	int i, j;

	assert(na);
	i = 1;
	ah[i] = ah[na--];
	while ((j = 2 * i) <= na) {
		if (ah[j+1]->t < ah[j]->t)
			j++;
		if (ah[i]->t <= ah[j]->t)
			return;
		t = ah[j];
		ah[j] = ah[i];
		ah[i] = t;
		i = j;
	}
}
