use IPC::Open2;
use strict;
use warnings;

# issue commands to the edit test program
# and check that the buffer modifications
# are performed correctly

# valgrind
my $vg = "";

# number of rounds in the test
my $niters = (shift or 42);

# start the edit process
-x 'edit' or die "edit not found";
open2 \*EDIT_OUT, \*EDIT_IN, "$vg ./edit";

# open the trace file
open \*TRACE, '>', 'trace.txt';

# the buffer state history
my @history;

# the current buffer state
my $buf = "";

# generate a random string
sub rndStr{ join'', @_[ map{ rand @_ } 1 .. shift ] }

# generate a random insertion
sub gins{
	my $p0 = int (rand (length ($buf) + 1));
	my $str = rndStr shift, 'a'..'z', '0'..'9', ' ', ' ', ' ', ' ';

	print EDIT_IN "+$p0 $str\n";
	print TRACE   "+$p0 $str\n";
	substr $buf, $p0, 0, $str;

	print "+";
}

# generate a random deletion
sub gdel{
	my $p0 = int (rand (length ($buf) + 1));
	my $p1 = int (rand (length ($buf) + 1));
	($p0, $p1) = ($p1, $p0) if($p0 > $p1);

	print EDIT_IN "-$p0 $p1\n";
	print TRACE   "-$p0 $p1\n";
	substr $buf, $p0, $p1-$p0, '';

	print "-";
}

# commit
sub gcom{
	unshift @history, $buf;
	print EDIT_IN "c\n";
	print TRACE   "c\n";
	print TRACE   "# $buf\n";

	print "c";
}


### perform test

foreach(1..$niters){
	gins 8 if(rand() < 0.5);
	gdel if(rand() < 0.3);
	gcom if(rand() < 0.3);
}

# finish by a commit
gcom;

my $err = 0;
print "\nchecking undo";
for(@history){
	print EDIT_IN "p\n!\n";
	my $line = <EDIT_OUT>;
	chomp $line;
	if($line ne $_){
		$err++;
		print "\nerror: \n got:  '$line'\n want: '$_'\n";
	}else{
		print ".";
	}
}

close TRACE;
print "\n";
exit $err;
