OBJDIR := obj

V = @

TOP = .

CC     := clang
LD     := clang
TANGLE := ctangle
STRIP  := strip

LDFLAGS += $$(pkg-config --libs x11 xft)
CFLAGS  += -g --std=c99 -Wall -Wextra $$(pkg-config --cflags x11 xft)

SRCFILES := unicode.c x11.c buf.c edit.c win.c cmd.c main.c
OBJFILES := $(patsubst %.c, $(OBJDIR)/%.o, $(SRCFILES))

all:

# Build rules

clean:
	rm -fr $(OBJDIR)

$(OBJDIR)/.deps: $(wildcard *.[ch])
	@mkdir -p $(@D)
	@$(CC) -MM $(CFLAGS) *.c \
		| sed -e "s,\\(.*\\):,$(OBJDIR)/\\1:," > $@

-include $(OBJDIR)/.deps

$(OBJDIR)/%.o: %.c
	@echo cc $<
	@mkdir -p $(@D)
	$(V)$(CC) $(CFLAGS) -c -o $@ $<

%.c: %.w
	@echo tangle $<
	$(V)$(TANGLE) $<

$(OBJDIR)/edit: $(OBJFILES)
	@echo ld $@
	$(V)$(LD) -o $@ $(LDFLAGS) $(OBJFILES)

all: $(OBJDIR)/edit

.PHONY: all clean
