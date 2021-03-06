#!/usr/bin/perl -w

use strict;
use warnings;

my ($divide,$tau,$onesite)=@ARGV;

defined($tau) or die "USAGE: $0 divide tau [site]\n";
defined($onesite) or $onesite = -1;

print STDERR "$0: command line: $divide $tau $onesite\n";

my $nsites=0;
my $biggestTimeSeen = 0;
my @value;
my ($times,$sites) = (0,0);

while(<STDIN>) {
	chomp;
	next if (/^#/);
	my @temp = split;
	if (scalar(@temp) != 5) {
		die "$0: Line $. does not have 5 columns\n";
	}

	my ($site,$val,$time,$label,$den) = @temp;

	defined($den) or die "$0: Line $_\n";
	if (!isAnInteger($site)) {
		die "$0: Line $. site $site is not an integer\n";
	}

	$_ = $time/$tau;
	
	my $timeIndex = sprintf("%0.f",$_);
	my @valri = (realPart($val),imagPart($val));
	if ($divide) {
		my $denReal = realPartStrict($den);
		$denReal = 1 if (realPart($denReal) == 0);
		divide(\@valri,$denReal);
	}

	$value[$timeIndex][$site] = \@valri;
	$times = $timeIndex if ($times < $timeIndex);
	$sites = $site if ($sites < $site);
}

$times++;
$sites++;
print STDERR "$0: Total sites = $sites\n";
print STDERR "$0: Total times = $times\n";

for (my $i = 0; $i < $times; ++$i) {
	my $time = $i*$tau;
	my $c = 0;
	my @temp;
	
	for (my $j = 0; $j < $sites; ++$j) {
		my $val = $value[$i][$j];
		defined($val) or next;
		my @valri = @$val;
		(scalar(@valri) == 2) or die "$0: Internal error for @valri\n";
		(defined($valri[0]) && defined($valri[1])) or next;
		$c++;
		$temp[$j] = $val;
	}

	print "$time ";
	for (my $j = 0; $j < $sites; ++$j) {
		next if ($onesite >=0 && $onesite != $j);
		my $val = $temp[$j];
		my $valr = "0.00";
		my $vali = "0.00";
		if (defined($val) && scalar(@$val) == 2) {
			$valr = $val->[0];
			$vali = $val->[1];
		}

		printf("%.6f %.3f ",$valr, $vali);
	}

	print "\n";
}

sub realPartStrict
{
	my ($t)=@_;
	my $it = imagPart($t);
	if (abs($it) > 1e-6) {
		die "$0: $t has non-zero imaginary part $it\n";
	}

	return realPart($t);
}

sub realPart
{
	my ($t)=@_;
	$_=$t;
	return "-1" if (!defined($_));
	s/\(//;
	s/\,.*$//;
	return $_;
}

sub imagPart
{
        my ($t)=@_;
        $_=$t;
        return "-1" if (!defined($_));
        s/\)//;
        s/^.*\,//;
        return $_;
}

sub isAnInteger
{
	my ($t)=@_;
	return 0 if (!defined($t));
	return 1 if ($t=~/^[0-9]+$/);
	return 0;
}

sub divide
{
	my ($realAndImag, $divisor) = @_;
	$realAndImag->[0] /= $divisor;
	$realAndImag->[1] /= $divisor;
}

