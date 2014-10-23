extern int mode;

void cmd_parse(Rune r);
#define cmd_reset() cmd_parse(GKEsc);
