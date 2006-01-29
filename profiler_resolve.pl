#! /usr/bin/perl -w

use strict;

my @files = <*.profile>;

die "No *.profile files found\n" if !@files;

for my $file (@files) {
	print STDERR "processing $file.\n";

	open my ($f), '<', $file or die "open $file: $!";
	open my ($fnew), '>', "$file.names" or die "create $file.names: $!";

	my ($prog) = $file =~ /^(.*)\.profile\z/ or die;
	open my ($nm), "nm -n $prog |" or die;

	my %nm;
	while (<$nm>) {
		next if /^\s/;
		/^([0-9a-f]{8}) . (.*)/ or die "bad nm";
		$nm{$1} = sprintf "%-30s", $2;
	}

	while (<$f>) {
		s/^([0-9a-f]{8})/$nm{$1} || $1/e;
		print $fnew $_;
	}
}
