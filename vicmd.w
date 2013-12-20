% qcar 2013 -- first attempt at literate programming

\nocon % omit table of contents
% \datethis % print date on listing
\def\bull{\item{$\bullet$}}
\def\ASCII{{\sc ASCII}}

@ This module provides an implementation of \.{vi} commands.  We try to
provide an implementation roughly \.{POSIX} compliant of the \.{vi} text
editor.  The only important function exported by this module accepts
unicode runes and parse them to construct commands, these commands are
then executed on the currently focused window.  We try to follow the
\.{POSIX} standard as closely as a simple implementation allows us.

@c
@<Header files to include@>@/
@<External variables@>@/
@<Local types@>@/
@<Predeclared functions@>@/
@<File local variables@>@/
@<Subroutines and motion commands@>@/
@<Definition of the parsing function |cmd_parse|@>

@ We will need to edit buffers, have the rune and window types available. 
Our own header file is also included to allow the compiler to check
consistency between definitions and declarations.  For debugging
purposes we also include \.{stdio.h}.

@f Rune int /* the type for runes is provided by \.{unicode.h} */
@f W int /* the window type, provided by \.{win.h} */
@f EBuf int /* the buffer type, provided by \.{edit.h} */

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

@<File local variables...@>=
enum {
	Command = 'c',
	Insert = 'i'
};
static int mode = Command;


@** Parsing of commands. We structure the parsing function as a
simple state machine.  The state must be persistent across function
calls so we must make it static.  Depending on the rune we just
got, this state needs to be updated.  Errors during the state
update are handled by a |goto err| statement which resets the
parsing state and outputs an error message.

@<Definition of the parsing fun...@>=
void
cmd_parse(Rune r)
{
	@<Initialize the persistent state of |cmd_parse|@>;
	switch (mode) {
	case Insert: insert(r); @+break;
	case Command: @<Update parsing state@>; @+break;
	}
	return;
err:	puts("! invalid command");
@.invalid command@>
	@<Reset parsing state@>;
}


@ Usual \.{vi} commands will consist of at most four parts described
below.

\yskip\bull The buffer---which can be any latin letter or digit---on which
	the command should act.  A buffer can be specified by starting
	with a \." character.
\bull The count which indicates how many times a command needs to
	be performed. We have to be careful here because there is a
	special case: \.0 is a command.
\bull The actual main command character which can be almost any letter.
	Some commands require an argument, for instance the \.m command.
\bull An optional motion that is designating the area of text on
	which the main command must act. This motion is also a command
	and can have its own count and argument.

\yskip\noindent The structure defined below is a ``light'' command which
cannot store a motion component.  We make this choice to permit
factoring of the data structures. A complex command will be composed
of two such structures, one is the main command, the other is the
motion command.


@<Local typ...@>=
typedef struct {
	unsigned short count;
	unsigned char chr;
	Rune arg;
} Cmd;

@ @<Initialize the pers...@>=
static char buf;
static Cmd c, m, *pcmd = &c;
static enum {
	BufferDQuote,	/* expecting a double quote */
	BufferName,	/* expecting the buffer name */
	CmdChar,	/* expecting a command char or count */
	CmdDouble,	/* expecting the second char of a command */
	CmdArg		/* expecting the command argument */
} state = BufferDQuote;

@ Updating the |cmd_parse| internal state is done by looking first
at our current state and then at the rune |r| we were just given.
If the input rune is |GKEsc| we cancel any partial parsing by
resetting the state.

@<Update pars...@>=
if (r == GKEsc) @<Reset pars...@>@;
else
	@+switch (state) {
	case BufferDQuote: @<Get optional double quote@>; @+break;
	case BufferName: @<Input current buffer name@>; @+break;
	case CmdChar: @<Input command count and name@>; @+break;
	case CmdDouble: @<Get the second character of a command@>; @+break;
	case CmdArg: @<Get the command argument@>; @+break;
	default: abort();
	}

@ When parsing a command, one buffer can be specified if the double
quote character is used.  If we get any other rune, we directly
retry to parse it as a command character by switching the state
to |CmdChar|.

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

@d risascii(r) ((r) < 0x7f)
@d risbuf(r) (risascii(r) && (islower(r) || isdigit(r)))

@<Input current buffer name@>=
if (!risbuf(r)) goto err;
buf = r;
state = CmdChar;

@ The |CmdChar| state needs to handle both the count and the command
name.  Depending on the command kind (double char, expecting an
argument, ...) we have to update the state differently.  To get this
information about the command we use the array of flags |keys|.

@<Input command count and name@>=
if (!risascii(r)) goto err;

if (isdigit(r) && (r != '0' || pcmd->count)) {
	pcmd->count = 10 * pcmd->count + (r - '0');
} else {
	pcmd->chr = r;
	if (keys[pcmd->chr].flags & CIsDouble) {
		state = CmdDouble; @+break;
	}
gotdbl:
	if (keys[pcmd->chr].flags & CHasArg) {
		state = CmdArg; @+break;
	}
gotarg:
	if (keys[pcmd->chr].flags & CHasMotion) {
		assert(pcmd == &c);
		pcmd = &m; @+break;
	}
	if (pcmd == &m && !(keys[pcmd->chr].flags & CIsMotion))
		goto err;
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

@ @<Get the command arg...@>=
pcmd->arg = r;
goto gotarg;

@ The internal state is reset by zeroing the |count| field of the
commands, this is necessary since |@<Input command count...@>| relies
on it to determine if a received |'0'| is part of the count or is the
command name.  We also need to change the state back to |BufferDQuote|.

@<Reset parsing state@>=
{	m.count = c.count = 0;
	buf = 0;
	pcmd = &c;
	state = BufferDQuote;
}

@ The |keys| table contains a set of flags used to specify the proper
parsing and interpretation of each \.{vi} command.  It also contains
a description of the action to do that we postpone for later. The \.{vi}
commands are \ASCII\ characters so the table only needs 128 entries.

@d CIsDouble 1 /* is the command a double character command */
@d CHasArg 2 /* is the command expecting an argument */
@d CHasMotion 4 /* is the command expecting a motion */
@d CIsMotion 8 /* is this a motion command */

@<File local variables...@>=
static struct {
	int flags;
	@<Other key fields@>@;
} keys[128] = { @<Key definitions@> };


@** Execution of the parsed commands.
Two major kind of commands must be considered here: {\sl destructive}
commands and {\sl motion} commands.


@ {\bf Move me} Buffers can be in two modes: {\sl character} mode and {\sl text}
mode.  This distinction entails a semantic difference when the text is
copied from buffers.  If the buffer is in line mode, atomic elements of
the document are lines, thus, a copy will affect only entire lines.  If the
buffer is in character mode, parts of lines can be altered by a copy.  The
mode of a buffer is determined when text is assigned to it, most often the
motion command used as parameter of a destructive command is responsible
for setting the buffer mode.

@ The commands act on the active window.  This window is accessible
via a global program variable.

@<External...@>=
extern W *curwin;

@* Insertion mode. The switch into the insertion mode is triggered by an
insertion command; it can be \.i, \.a or \.c for instance.  Some of these
commands take a count that indicates how many times the typed text must
be repeated.  To implement this behavior we maintain a variable that gives
the length of the current insert and one that gives the number of times
this insert needs to be done.

@<File local...@>=
static unsigned nins; /* length of the current insert */
static unsigned short cins; /* count of the current insert */

@ When running in insertion mode, the runes are directly written in the current
buffer.  We need to take care of special runes which have a distinguished
meaning.  The key |GKEsc| leaves the insertion mode and goes back to command
mode, |GKBackspace| will erase the previous character if it is part of the
current insertion.

@<Sub...@>=
static void insert(Rune r)
{
	EBuf *eb = curwin->eb;
	switch (r) {
	case GKEsc: @<Repeat insert |cins-1| times; leave insert mode@>; @+break;
	case GKBackspace:
		if (nins > 0) {
			eb_del(eb, curwin->cu-1, curwin->cu);
			curwin->cu--, nins--;
		}
		break;
	default: eb_ins(eb, curwin->cu++, r), nins++;@+break;
	}
}

@ When we are about to switch from insertion to command mode, we mark the
buffer as being in a clean state by committing it.  This will add the finished
insertion into the modification log used to undo changes.

@<Repeat insert...@>=
assert(cins != 0);
while (--cins)
	for (unsigned cnt=nins; cnt--;) {
		r = buf_get(&eb->b, curwin->cu - nins);
		eb_ins(eb, curwin->cu++, r);
	}
if (buf_get(&eb->b, curwin->cu-1) != '\n') curwin->cu--;
eb_commit(eb), mode = Command;

@* Motion commands. They can be used as parameters for destructive commands,
they almost always have two semantics, one when they are used bare
to move the cursor and one when they are used as parameter.  All motion
commands defined below will return 0 if they succeed and 1 if they fail.

Because of the different behaviors a motion command can have, we use a
structure to store the motion action that has both the beginning and end
of the region affected, and the final cursor position if used bare.

We also need a field of flags

\yskip\bull |MLinewise| is set if the motion operates on full lines or on characters.
	At first sight this is more related to the motion command
	than the motion result, so it should be in |keys| rather than in
	this structure.  But this would not be precise enough: The standard mandates
	that certain commands, depending on the invocation context, give linewise or
	character wise motions.  This is for instance the case for \.\}.
\bull |MError| is set to indicate if the command must signal an error
	when called to move the cursor.  This is used, for instance, when executing
	\.l at the end of a line.  Even if this flag is set, the contents of the
	motion structure should make sense, to the contrary of what happens when
	1 is returned by the motion command.

\yskip\noindent When a motion command is called, |mbeg| is set to the current cursor
postion and |mflags == 0|.

@d MLinewise 1
@d MError 2

@<Local typ...@>=
typedef struct {
	unsigned beg, end;
	unsigned mov;
	int flags;
} Motion;

@ The most elementary cursor motions in \.{vi} are \.h \.j \.k and \.l.
We must note that the \.{POSIX} specification mandates a subtle difference
of behaviors between vertical and horizontal motions.  When a count
is specified, the horizontal motion must succeed even if the count is
too big while vertical motions must fail in this corner case.  In this
implementation files do not have ends, so a vertical motion towards the
end of the buffer will always succeed.

@d curb (&curwin->eb->b)
@f line x /* do not make line a keyword */

@<Predecl...@>=
static int m_hl(Cmd, Motion *);
static int m_jk(Cmd, Motion *);

@ @<Subr...@>=
static int m_hl(Cmd c, Motion *m)
{
	int line, col;
	buf_getlc(curb, m->beg, &line, &col);
	if (c.chr == 'h') {
		if (col == 0) return 1;
		m->mov = buf_setlc(curb, line, col - c.count);
		m->end = m->beg; @+m->beg = m->mov;
	} else {
		if (buf_get(curb, m->beg) == '\n') return 1;
		m->mov = buf_setlc(curb, line, col + c.count);
		m->end = m->mov; @+m->beg = curwin->cu;
		if (buf_get(curb, m->beg+1) == '\n') {
			m->mov = m->beg;
			m->flags |= MError;
		}
	}
	return 0;
}

@ Be careful to signal an error if the motion hits the top of the buffer.

@<Subr...@>=
static int m_jk(Cmd c, Motion *m)
{
	int line, col;
	buf_getlc(curb, m->beg, &line, &col);
	if (c.chr == 'j') {
		m->mov = buf_setlc(curb, line + c.count, col);
		m->beg = buf_bol(curb, m->beg); @+m->end = buf_eol(curb, m->mov);
	} else {
		if (c.count > line) return 1;
		m->mov = buf_setlc(curb, line - c.count, col);
		m->end = buf_eol(curb, m->beg); @+m->beg = buf_bol(curb, m->mov);
	}
	m->flags |= MLinewise;
	return 0;
}


@* Key definitions for motions.

@ @<Other key fields@>=
int @[@] (*motion)(Cmd, Motion *);

@ @<Key def...@>=
['h'] = {CIsMotion, m_hl}, ['l'] = {CIsMotion, m_hl},@/
['j'] = {CIsMotion, m_jk}, ['k'] = {CIsMotion, m_jk},


@** Index.
