enum {
	MaxAlarms = 15, /* max number of concurrent alarms */
};

enum {
	ERead = 1,
	EWrite = 2,
};

int ev_alarm(int, void (*)(void));
void ev_register(int, int, void (*)(int, int, void *), void *);
void ev_cancel(int);
void ev_loop(void);
