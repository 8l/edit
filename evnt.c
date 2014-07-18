#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <sys/select.h>

#include "evnt.h"

							// mail a jb

void die(char *);

int exiting;

static E *elist;
static int ne;

void
ev_register(E e)
{
	elist = realloc(elist, (ne + 1) * sizeof(E));
	assert(elist);
	elist[ne] = e;
	ne++;
}

void
ev_loop()
{
	fd_set rfds, wfds;
	int maxfd, flags;
	E *e;

	while (!exiting) {
		maxfd = -1;
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		for (e=elist; e-elist < ne; e++) {
			if (e->flags & ERead)
				FD_SET(e->fd, &rfds);
			if (e->flags & EWrite)
				FD_SET(e->fd, &wfds);
			if (e->fd > maxfd)
				maxfd = e->fd;
		}
		if (select(maxfd+1, &rfds, &wfds, 0, 0) == -1) {
			if (errno == EINTR)
				continue;
			die("select error");
		}
		for (e=elist; e-elist < ne;) {
			flags = 0;
			if (FD_ISSET(e->fd, &rfds))
				flags |= ERead;
			if (FD_ISSET(e->fd, &wfds))
				flags |= EWrite;
			if (flags == 0)
				continue;
			if (e->f(flags, e->p)) {
				ne--;
				memmove(e, e+1, (ne - (e-elist)) * sizeof(E));
			} else
				e++;
		}
	}
}
