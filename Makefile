LD     := $(CC)
TANGLE := ctangle

V = @
OBJDIR := obj

LDFLAGS += $$(pkg-config --libs x11 xft)
CFLAGS  += -g --std=c99 -Wall -Wextra -I. $$(pkg-config --cflags x11 xft)

SRCFILES := unicode.c evnt.c x11.c buf.c edit.c win.c exec.c vicmd.w main.c
OBJFILES := $(patsubst %.w, $(OBJDIR)/%.o, $(SRCFILES:%.c=$(OBJDIR)/%.o))

all: $(OBJDIR)/edit

# Build rules

clean:
	rm -fr $(OBJDIR)

$(OBJDIR)/.deps: $(OBJDIR) $(SRCFILES:%.w=$(OBJDIR)/%.c) $(wildcard *.h)
	@$(CC) -MM $(CFLAGS) $(OBJDIR)/*.c *.c \
		| sed -e "s,^.*:,$(OBJDIR)/&," > $@

-include $(OBJDIR)/.deps

%.o $(OBJDIR)/%.o: %.c
	@echo cc $<
	$(V)$(CC) $(CFLAGS) -c -o $@ $<

$(OBJDIR)/%.c: %.w
	@echo tangle $<
	$(V)$(TANGLE) $< - $@

$(OBJDIR)/edit: $(OBJDIR) $(OBJFILES)
	@echo ld $@
	$(V)$(LD) -o $@ $(OBJFILES) $(LDFLAGS)

$(OBJDIR):
	@mkdir -p $@

.PHONY: all clean
