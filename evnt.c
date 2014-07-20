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
	int i, maxfd, flags;

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
		if (select(maxfd+1, &rfds, &wfds, 0, 0) == -1) {
			if (errno == EINTR)
				continue;
			die("select error");
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
				memmove(&elist[i], &elist[i+1], (ne - i) * sizeof(E));
				continue;
			} else
				i++;
		}
	}
}
