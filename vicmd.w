% qcar 2013 -- first attempt at literate programming

\nocon % omit table of contents
% \datethis % print date on listing
\def\bull{\item{$\bullet$}}
\def\ASCII{{\sc ASCII}}

@* Implementation of \.{vi} commands.  We try to provide an implementation
roughly \.{POSIX} compliant of the \.{vi} text editor.  The only important
function exported by this module will take unicode runes and parse them
to construct commands, these commands are then executed on the currently
edited buffer.  We try to follow the \.{POSIX} standard as closely as a
simple implementation allows us.

@c
@<Header files to include@>@/
@<External variables and structure definitions@>@/
@<Local variables@>@/
@<Subroutines@>@/
@<Definition of |cmd_parse|@>

@ We will need to edit buffers, have the rune and window types available. 
Our own header file is also included to allow the compiler to check
consistency between definitions and declarations.  For debugging
purposes we also include \.{stdio.h}.

@f Rune int /* Rune is a type provided by \.{unicode.h} */
@f W int /* W is the window type, provided by \.{win.h} */

@<Header files...@>=
#include <assert.h>
#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include "unicode.h"
#include "edit.h"
#include "win.h"
#include "cmd.h"

@ The \.{vi} editor is modal so we must keep track of the current
mode we are currently into.  When the editor starts it is in
command mode.

@<Local variables@>=
enum {
	Command = 'c',
	Insert = 'i'
};
static int mode = Command;


@* Parsing of commands. We structure the parsing function as a
simple state machine.  The state must be persistent across function
calls so we must make it static.  Depending on the rune we just
got, this state needs to be updated. We also need to remember the
rune we got to be able to repeat commands.

@<Definition of |cmd_parse|@>=
void
cmd_parse(Rune r)
{
	@<Initialize the internal state of |cmd_parse|@>;
	@<Memorize the rune |r|@>;
	if (mode == Insert)
		@<Interpret rune |r| in insert mode@>;
	if (r == GKEsc) { /* universal abort key */
		@<Reset parsing state@>;
		return;
	}
	@<Update internal state of |cmd_parse|@>;
	return;
err:	puts("invalid command!");
	@<Reset parsing state@>;
}


@ Usual \.{vi} commands will consist of at most four parts.

\yskip\bull The buffer (which can be any latin letter or digit) on which
	the command should act.  A buffer is specified if the commands
	starts with a double quote.
\bull The count which indicates how many times a command needs to
	be performed. We have to be careful here because there is a
	special case: \.0 is a command.
\bull The actual command character which can be almost any letter.
	Some commands take an argument, for instance the \.\% command.
\bull An optional motion that is designating the area of text on
	which the previous command must act. Note that this command
	can also take its own count.

\yskip\noindent The structure defined below is a ``light'' command which
cannot store a motion component.  We make this choice to permit
factoring of the data structures. A complex command will be composed
of two such structures, one is the main command, the other is the
motion command.


@<External...@>=
typedef struct {
	unsigned short count;
	unsigned char chr;
	Rune arg;
} Cmd;

@ @<Initialize the internal state...@>=
static char buf;
static Cmd c, m, *pcmd = &c;
static enum {
	BufferDQuote,	/* expecting a double quote */
	BufferName,	/* expecting the buffer name */
	CmdChar,	/* expecting a command char */
	CmdDouble,	/* expecting double char command */
	CmdArg		/* expecting the command argument */
} state = BufferDQuote;

@ Updating the |cmd_parse| internal state is done by looking first
at our current state and then at the rune |r| we were just given.

@<Update internal state...@>=
switch (state) {
case BufferDQuote: @<Get optional double quote@>; @+break;
case BufferName: @<Input current buffer name@>; @+break;
case CmdChar: @<Input command count and name@>; @+break;
case CmdDouble: @<Get the second character of a command@>; @+break;
case CmdArg: @<Get the command argument@>; @+break;
default: abort();
}

@ When parsing a command a buffer can be provided if the double
quote character is used.  If we get something different it means
that we got a command character directly so we retry parsing
in the state |CmdChar|.

@<Get optional double quote@>=
if (r == '"')
	state = BufferName;
else {
	state = CmdChar;
	cmd_parse(r);
}

@ Buffer names cannot be anything else than an \ASCII\ letter or
a digit.  If the rune we got is not one of these two we will
signal an error and abort the current command parsing.

@d risascii(r) ((r) <= '~')
@d risbuf(r) (risascii(r) && islower((unsigned char)(r)))

@<Input current buffer name@>=
if (!risbuf(r)) goto err;
buf = r;
state = CmdChar;

@ The |CmdChar| state needs to handle both the count and the command
name.  Depending on the command kind (double char, expecting an
argument, ...) we have to update the state differently.  To get this
information about the command we use the global array of flags |cmds|.

@<Input command count and name@>=
if (!risascii(r)) goto err;

if (isdigit((unsigned char)r) && (r != '0' || pcmd->count)) {
	pcmd->count = 10 * pcmd->count + (r - '0');
} else {
	pcmd->chr = r;
	if (cmds[pcmd->chr] & CDbl) {
		state = CmdDouble; @+break;
	}
gotdbl:
	if (cmds[pcmd->chr] & CArg) {
		state = CmdArg; @+break;
	}
gotarg:
	if (cmds[pcmd->chr] & CMot) {
		assert(pcmd == &c);
		pcmd = &m; @+break;
	}

	docmd(buf, c, m);
	@<Reset parsing state@>;
}

@ When a double character command is expected we ensure that the
second character received is the same as the first and resume
the processing performed in |@<Input command count...@>|.

@<Get the second char...@>=
if (r != pcmd->chr)
	goto err;
goto gotdbl;

@ The handling of arguments is similar and simple.

@<Get the command arg...@>=
pcmd->arg = r;
goto gotarg;

@ The internal state is reset by zeroing the |count| field of the
commands, this is necessary since |@<Input command count...@>| relies
on it to determine if a received |'0'| is part of the count or is the
command name.  We also need to change the state back to |BufferDQuote|.

@<Reset parsing state@>=
m.count = c.count = 0;
buf = 0;
pcmd = &c;
state = BufferDQuote;

@ The main parsing function needs to remember the runes it read.
We use a simple buffer that is grown on demand when the command gets
too long.

@<Local variables@>=
static struct {
	Rune *buf;
	unsigned end, size;
} curc;

@ @<Memorize the rune...@>=
if (curc.end >= curc.size) {
	curc.size = 2 * curc.size + 512;
	curc.buf = realloc(curc.buf, curc.size * sizeof(Rune));
}
curc.buf[curc.end++] = r;

@ When cleaning the parsing state we try to shrink the command buffer
if it is past a certain sane treshold defined here.  This avoids
having a fat memory footprint only because of one huge editing command.

@d FatTreshold 9000

@<Reset parsing state@>=
if (curc.size > FatTreshold) {
	free(curc.buf);
	curc.buf = 0;
	curc.size = 0;
}
curc.end = 0;

@ The |cmds| table contains a set of flags used to specify the proper
parsing of each \.{vi} command. These commands are \ASCII\ characters
thus the table only needs 128 entries.

@d CDbl 1 /* is the command a double character command */
@d CArg 2 /* is the command expecting an argument */
@d CMot 4 /* is the command expecting a motion */

@<Local variables@>=
static int cmds[128] = {@/
	['d'] = CMot,
	['m'] = CArg,
	['['] = CDbl,
	['\''] = CArg
};

@* Execution of the parsed commands.

@ The commands will act on the active window. This window can be
accessed using an external variable.

@<External...@>=
extern W *curwin;

@* Index.
