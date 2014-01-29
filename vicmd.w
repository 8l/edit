% qcar 2013 2014 -- first attempt at literate programming

\nocon % omit table of contents
% \datethis % print date on listing
\def\bull{\item{$\bullet$}}
\def\ASCII{{\sc ASCII}}
@f line x
@f Rune int
@f W int
@f EBuf int

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

@ We need to edit buffers, have the rune and window types available. 
This module header file is also included to allow the compiler to check
consistency between definitions and declarations.  For debugging
purposes we also include \.{stdio.h}.

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
	if (pcmd == &m && !(keys[pcmd->chr].flags & CIsMotion))
		goto err;
	if (keys[pcmd->chr].flags & CHasMotion) {
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
a description of the action to take, we postpone its definition for
later. Since the \.{vi} commands are \ASCII\ characters so the table
only needs 128 entries.

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

@d curb (&curwin->eb->b) /* convenient alias for the current buffer */

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
	case '\n': @<Insert a new line preserving the indentation@>; @+break;
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

@ @d risblank(r) (risascii(r) && isblank(r))
@<Insert a new line...@>=
{	@+eb_ins(eb, curwin->cu, r), nins++;
	for (
		unsigned bol = buf_bol(curb, curwin->cu++);
		risblank(r = buf_get(curb, bol));
		bol++
	)
		eb_ins(eb, curwin->cu++, r), nins++;
}

@* Motion commands. They can be used as parameters for destructive commands,
they almost always have two semantics, one when they are used bare
to move the cursor and one when they are used as parameter.  All motion
commands implemented below will return 0 if they succeed and 1 if they fail.

%
% TODO Fix the vocabulary issues, motion command/parameter, etc...
%      Hint, fit to posix
%

The motion functions defined take as argument an integer that specifies
if they are called as motion parameters or not.  Depending on this argument
the \Cee\ structure describing the motion to perform will be filled
differently.  If called as a motion parameter, the |beg| and |end| fields
will contain the region defined by the motion; otherwise, only the |end|
field will be relevant and it will store the final cursor position.  If the
function returns 1, the motion structure should not be used.

The structure also contains the following set of flags.

\yskip\bull |linewise| indicates if the motion operates on full lines or on characters.
	At first sight this is more related to the motion command
	than the motion result, so it should be in |keys| rather than in
	this structure.  But this would not be precise enough: The standard mandates
	that certain commands, depending on the invocation context, give linewise or
	character wise motions.  This is for instance the case for \.\}.

\yskip\noindent When a motion command is called, |beg| is set to the current cursor
postion and flags are zeroed.

@<Local typ...@>=
typedef struct {
	unsigned beg, end;
	int linewise : 1;
} Motion;

@ Motion commands often need to skip blanks, for instance, to find the first
non blank character of a line.  The following function will be of great help
with this.  It finds the end of a blank span starting at position |p|.

@<Subr...@>=
static unsigned blkspn(unsigned p)
{
	Rune r;
	do r = buf_get(curb, p++); while (risblank(r));
	return p-1;
}

@ The most elementary cursor motions in \.{vi} are \.h \.j \.k and \.l.
We must note that the \.{POSIX} specification mandates a subtle difference
of behaviors between vertical and horizontal motions.  When a count
is specified, the horizontal motion must succeed even if the count is
too big while vertical motions must fail in this case.  In this
implementation files do not end, so a vertical motion towards the end of
the buffer will always succeed.

@d swap(p0, p1) { unsigned _tmp = p0; p0 = p1, p1 = _tmp; }

@<Predecl...@>=
static int m_hl(int, Cmd, Motion *);
static int m_jk(int, Cmd, Motion *);

@ One special case needs to be handled for \.l here: If the cursor is
on the last column of the line and the command is called as a motion
command, the range selected is the last character of the line; however
if the command is not called as a motion command we must signal an
error.  This funny behavior contributes to what makes me think that
\.{vi}'s language is not as algebraic as it might appear at first
and maybe needs some revamp.

@<Subr...@>=
static int m_hl(int ismotion, Cmd c, Motion *m)
{
	int line, col;
	buf_getlc(curb, m->beg, &line, &col);
	if (c.chr == 'h') {
		if (col == 0) return 1;
		m->end = buf_setlc(curb, line, col - c.count);
		if (ismotion) swap(m->beg, m->end);
	} else {
		if (buf_get(curb, m->beg) == '\n') return 1;
		m->end = buf_setlc(curb, line, col + c.count);
		if (!ismotion && buf_get(curb, m->end) == '\n') return 1;
	}
	return 0;
}

@ For vertical motions, be careful to signal an error if the motion hits
the top of the buffer.

@<Subr...@>=
static int m_jk(int ismotion, Cmd c, Motion *m)
{
	int line, col;
	buf_getlc(curb, m->beg, &line, &col);
	if (c.chr == 'k') {
		if (c.count > line) return 1;
		m->end = buf_setlc(curb, line - c.count, col);
	} else
		m->end = buf_setlc(curb, line + c.count, col);
	if (ismotion) {
		if (c.chr == 'k') swap(m->beg, m->end);
		m->beg = buf_bol(curb, m->beg);
		m->end = buf_eol(curb, m->end) + 1;
	}
	m->linewise = 1;
	return 0;
}

@ Another family of useful and easy to implement motions are the line
oriented motions.  We start by implementing character lookup motions,
there are four commands of this kind \.t, \.f, \.T and \.F.  Uppercase
commands search backwards, lowercase run forward.

@<Subr...@>=
static int m_find(int ismotion, Cmd c, Motion *m)
{
	int dp = islower(c.chr) ? 1 : -1;
	unsigned p = m->beg;
	register Rune r;

	@<Save the searched rune and the command name@>;
	while (c.count--)
		while ((r = buf_get(curb, p += dp)) != c.arg)
			/* |buf_get| must return |'\n'| at position |-1u| */
			if (r == '\n') return 1;

	m->end = tolower(c.chr) == 'f' ? p : p-dp;
	if (ismotion) {
		if (dp == 1) m->end++;
		else swap(m->beg, m->end);
	}
	return 0;
}

@ The two commands \., and \.; repeat the last character search
motion.  So we need to store the searched rune and the command
name in a file local structure at each invocation of |m_find|.
This structure can be locked to prevent |m_find| from altering
it in certain special conditions.  The lock is used in the
implementation of the undo and the ``repeat find'' commands.

@<File local vari...@>=
static struct {@+char locked;@+char chr;@+Rune arg;@+} lastf;

@ @<Save the searched rune...@>=
if (!lastf.locked) lastf.chr = c.chr, lastf.arg = c.arg;
else lastf.locked = 0; // reset the lock

@ The \.; command repeats the last search in the same direction,
while \., inverts the direction.  We must take care of locking
the |lastf| structure to avoid an alternating behavior when
using the \., command repeatedly.

@<Subr...@>=
static int m_repf(int ismotion, Cmd c, Motion *m)
{
	Cmd cf = {c.count, lastf.chr, lastf.arg };

	if (lastf.chr == 0) return 1;
	if (c.chr == ',') cf.chr ^= 32; // flip case
	lastf.locked = 1;
	return m_find(ismotion, cf, m);
}


@ @<Predecl...@>=
static int m_find(int, Cmd, Motion *);
static int m_repf(int, Cmd, Motion *);

@ The \.{vi} command set provides two commands to move towards the
beginning of the line: \.0 and \.\^.  The latter moves to the first
non-blank character in the line while the former will move in the
first column, both commands do not accept a count and fail if used
as motion commands and do not move the cursor.

@<Subr...@>=
static int m_bol(int ismotion, Cmd c, Motion *m)
{
	m->end = buf_bol(curb, m->beg);
	if (c.chr == '^') m->end = blkspn(m->end);
	if (ismotion && m->end < m->beg) swap(m->beg, m->end);
	return ismotion && m->end == m->beg;
}
 
@ The \.\$ command moves to the end of line.  This command accepts
a count argument to move to the end of the $n$-th line after the
current one.  Note that it can be a linewise motion depending on
the initial cursor position.

@<Subr...@>=
static int m_eol(int ismotion, Cmd c, Motion  *m)
{
	unsigned bol = buf_bol(curb, m->beg);
	int l, x;

	if (c.count > 1 && blkspn(bol) >= m->beg)
		m->linewise = 1, m->beg = bol;

	buf_getlc(curb, m->beg, &l, &x);
	m->end = buf_eol(curb, buf_setlc(curb, l + c.count - 1, 0)) - 1;
	if (ismotion || buf_get(curb, m->end) == '\n') m->end++;
	if (ismotion && m->linewise)
		m->end++; // eat the last |'\n'| if the motion is linewise

	return ismotion && c.count == 1 && m->end == m->beg;
}

@ @<Predecl...@>=
static int m_bol(int, Cmd, Motion *);
static int m_eol(int, Cmd, Motion *);

@ Next, we implement word motions.  They can act on {\sl big} or
{\sl small} words.  Small words are sequences composed of alphanumeric
characters and the underscore \_ character.  Big words characters
are anyting that is not a space.  We will need two predicate functions
to recognize these two classes of characters.

@<Subr...@>=
static int risword(Rune r)
{
	return (risascii(r) && isalpha(r)) /* \ASCII\ alpha */
	    || (r >= 0xc0 && r < 0x100) /* attempt to detect
	                                   latin characters */
	    || (r >= '0' && r <= '9')
	    || r == '_';
}

static int risbigword(Rune r)
{
	return !risascii(r) || !isspace(r);
}

@ Word motions involve some kind of light parsing.  Since the buffer
implementation exposes infinite buffers we have to take care of
not hanging in a loop when going towards the end of the buffer.
To do this we rely on the |limbo| field of the \Cee\ buffer structure.
This field contains the offset at which limbo begins, if we get past
this offset during parsing we know we are heading straight to hell
and should stop reading input.

We use the following regular grammars to factor the code for the four
forward word motion commands.
$$
\vbox{\halign{\hfil#: &# \cr
\.w / \.W& $in^*; (\neg in)^*; in$ \cr
\.e / \.E& $(\neg in)^*; in^*; \neg in$ \cr
}}
$$
In the above figure, $in$ matches a big or small word rune (depending
on the command we implement).  The second grammar matches one rune
past the end of the next word.  I compiled these two grammars in
a deterministic automaton.  Since one grammar above is mapped to the
other by changing $in$ to $\neg in$, we only need to store one
automaton.

@<Predecl...@>=
static int m_ewEW(int, Cmd, Motion *);
static int m_bB(int, Cmd, Motion *);

@ @<Subr...@>=
static int m_ewEW(int ismotion, Cmd c, Motion *m)
{
	int @[@] (*in)(Rune) = islower(c.chr) ? risword : risbigword;
	int dfa[][2] = {{1, 0}, {1, 2}}, ise = tolower(c.chr) == 'e';
	unsigned p = m->beg;

	while (c.count--) {
		for (
			int s = 0;
			s != 2;
			s = dfa[s][ise ^ in(buf_get(curb, ise + p++))]
		)
			if (p >= curb->limbo + 1) break;
	}
	m->end = ismotion && ise ? p : p-1;
	return 0;
}

@ The backward word motion commands are implemented with the same
technique.

@<Subr...@>=
static int m_bB(int ismotion, Cmd c, Motion *m)
{
	int @[@] (*in)(Rune) = c.chr == 'b' ? risword : risbigword;
	int dfa[][2] = {{0, 1}, {2, 1}};
	unsigned p = m->beg;

	while (c.count--)
		for (
			int s = 0;
			s != 2 && p != -1u;
			s = dfa[s][in(buf_get(curb, --p))]
		);
	m->end = p+1;
	if (ismotion) swap(m->beg, m->end);
	return 0;
}

@ Paragraph motions \.\{ and \.\} are implemented next.  We recognize
consecutive blank lines and form feed characters as paragraph
separators.  Special care must be taken when these commands are used
as motion commands because they can be linewise or not: If the cursor
is at the beginning of a line on a blank character the motion is
linewise, otherwise it is not.

I will ignore all legacy features related to \.{nroff} editing since,
today, I prefer \TeX\ over it.  If you desperately need them, they are
easy to hack in (just treat them as form feeds).

@<Subr...@>=
static int m_par(int ismotion, Cmd c, Motion *m)
{
	int l, x, dl = c.chr == '{' ? -1 : 1;
	enum {@+Blank, FormFeed, Text@+} ltyp;
	int s, dfa[][3] = {
		{ 0, 3, 3 },
		{ 2, 2, 3 },
		{ 2, 9, 3 },
		{ 9, 9, 3 }
	};
	unsigned bol;

	buf_getlc(curb, m->beg, &l, &x);
	bol = buf_bol(curb, m->beg);
	@<Detect if paragraph motion is linewise@>;

	while (c.count--)
		for (
			s = c.chr == '{';
			l >= 0 && (bol < curb->limbo || c.chr == '{');
		) {
			@<Set |ltyp| to the line type of the current line@>;
			@<Update the state |s| and proceed to the next line@>;
		}

	m->end = bol;
	if (ismotion && c.chr == '{') swap(m->beg, m->end);
	return 0;
}

@ @<Set |ltyp|...@>=
switch (buf_get(curb, blkspn(bol))) {
case '\n': ltyp = Blank;@+break;
case '\f': ltyp = FormFeed;@+break;
default: ltyp = Text;@+break;
}

@ The only critical point when updating the state and moving on to
the next line is to check if the final state is reached before
updating |bol|.  Not doing this would make the implementation off
by one line.

@<Update the state |s| and...@>=
if ((s = dfa[s][ltyp]) == 9) break;
l += dl, bol = buf_setlc(curb, l, 0);

@ A paragraph motion is linewise when the cursor is at or before the
first non-blank rune of the line.  In this case, we change |m->beg| to
point to the very first character (blank or not) of the line so the
motion command acts on full lines.  This behavior conforms to Keith
Bostic's \.{nvi} for the forward paragraph motion but differs for the
backward motion.  I feel like the difference made little sense and
unified the two.

@<Detect if para...@>=
if (blkspn(bol) >= m->beg) {
	m->beg = bol;
	m->linewise = 1;
}

@ Next comes the \.\% motion that finds the matching character.
@<Subr...@>=
static int m_match(int ismotion, Cmd c, Motion *m)
{
	Rune match[] = { '<', '{', '(', '[', ']', ')', '}', '>' };
	int n, dp;
	unsigned p = m->beg;
	Rune beg, end, r;

	@<Find the search direction and the matching character@>;
	for (
		n = 0;
		(n += (r == beg) - (r == end)) != 0;
		r = buf_get(curb, p += dp)
	)
		if (p == -1u || p >= curb->limbo) return 1;
	m->end = p;
	if (ismotion)
		@<Detect if the motion is linewise and adjust it@>;
	return 0;
}

@ @<Find the sear...@>=
for (; (r = beg = buf_get(curb, p)) != '\n'; p++)
	for (n = 0; n < 8; n++)
		if (match[n] == r) goto found;
return 1;
found: dp = n >= 4 ? -1 : 1, end = match[7 - n];

@ @<Detect if the motion is line...@>=
{@+	if (dp == -1) swap(m->beg, m->end);
	m->end++;
	if (blkspn(buf_bol(curb, m->beg)) >= m->beg
	&& blkspn(m->end) == buf_eol(curb, m->end)) {
		m->linewise = 1;
		m->beg = buf_bol(curb, m->beg);
		m->end = buf_eol(curb, m->end) + 1;
	}
}

@ @<Predecl...@>=
static int m_par(int, Cmd, Motion *);
static int m_match(int, Cmd, Motion *);

@*1 Hacking the motion commands. Here is a short list of things you
want to know if you start hacking either the motion commands, or any
function used to implement them.

\yskip\bull Functions on buffers must be robust to funny arguments.  For
	instance in |m_hl| we rely on the fact that giving a negative
	column as argument to |buf_setlc| is valid and returns the offset
	of the first column in the buffer.  Dually, if the column count
	is too big we must get into the last column which is the one
	containing the newline character |'\n'|.

\bull Lines and columns are 0 indexed.

\bull All lines end in |'\n'|.  This must be guaranteed by the buffer
	implementation.

\bull Functions dealing with lines (|buf_bol|, |buf_eol|, ...) must count
	the trailing |'\n'| as part of the line. So, by the previous point,
	an empty line consists only of a newline character which marks its
	end.

\bull Files do not end.  There is an (almost) infinite amount of newline
	characters at the end.  This part is obviously not stored in
	memory, it is called {\sl limbo}.  Deletions in limbo must work
	and do nothing.


@* Key definitions for motions.

@ @<Other key fields@>=
union {
	int @[@] (*motion)(int, Cmd, Motion *);
	int @[@] (*cmd)(char, Cmd, Cmd);
};

@ @d Mtn(flags, f) {@+CIsMotion|flags, .motion = f}
@d Act(flags, f) {@+flags, .cmd = f}
@<Key def...@>=
['h'] = Mtn(0, m_hl), ['l'] = Mtn(0, m_hl),@/
['j'] = Mtn(0,m_jk), ['k'] = Mtn(0, m_jk),@/
['t'] = Mtn(CHasArg, m_find), ['f'] = Mtn(CHasArg, m_find),@/
['T'] = Mtn(CHasArg, m_find), ['F'] = Mtn(CHasArg, m_find),@/
[','] = Mtn(0, m_repf), [';'] = Mtn(0, m_repf),@/
['0'] = Mtn(0, m_bol), ['^'] = Mtn(0, m_bol),@/
['$'] = Mtn(0, m_eol),@/
['w'] = Mtn(0, m_ewEW), ['W'] = Mtn(0, m_ewEW),@/
['e'] = Mtn(0, m_ewEW), ['E'] = Mtn(0, m_ewEW),@/
['b'] = Mtn(0, m_bB), ['B'] = Mtn(0, m_bB),@/
['{'] = Mtn(0, m_par), ['}'] = Mtn(0, m_par),@/
['%'] = Mtn(0, m_match),@/
['d'] = Act(CHasMotion, a_d),

@ @<Subr...@>=
static int a_d(char buf, Cmd c, Cmd mc)
{
	Motion m = {curwin->cu, 0, 0};
	if (mc.count == 0) mc.count = 1;
	while (c.count--) {
		if (keys[mc.chr].motion(1, mc, &m)) return 1;
		eb_yank(curwin->eb, curwin->cu = m.beg, m.end, 0);
		eb_del(curwin->eb, m.beg, m.end);
		eb_commit(curwin->eb);
	}
	return 0;
}

@ @<Predecl...@>=
static int a_d(char, Cmd, Cmd);

@ @<Subr...@>=
static void docmd(char buf, Cmd c, Cmd m)
{
	if (c.count == 0)
		c.count = 1;

	if (c.chr == 'i' || c.chr == 'I'
	|| c.chr == 'a' || c.chr == 'A'
	|| c.chr == 'o' || c.chr == 'O') {
		if (c.chr == 'a' && curwin->cu != buf_eol(curb, curwin->cu))
			curwin->cu++;
		if (c.chr == 'A')
			curwin->cu = buf_eol(curb, curwin->cu);
		if (c.chr == 'I')
			curwin->cu = blkspn(buf_bol(curb, curwin->cu));
		nins = 0, cins = c.count;
		mode = Insert;
		return;
	}
	if (c.chr == 'q'-'a' + 1) {
		extern int exiting;
		exiting = 1;
		return;
	}
	if (keys[c.chr].flags & CIsMotion) {
		Motion m = {curwin->cu, 0, 0};
		if (keys[c.chr].motion(0, c, &m) == 0)
			curwin->cu = m.end;
	} else if (keys[c.chr].cmd != 0)
		keys[c.chr].cmd(buf, c, m);
}

@** Index.
