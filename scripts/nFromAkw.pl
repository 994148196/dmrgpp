#!/usr/bin/perl

use strict;
use warnings;
use utf8;
use Math::Trig;
use OmegaUtils;

my $pi = Math::Trig::pi;

use Getopt::Long qw(:config no_ignore_case);

my $usage = "USAGE: $0 -f dollarizedInput [-m mu] [-p] [-z] minusSpectrum plusSpectrum\n";

my $templateInput;
my $isPeriodic;
my $zeroAtCenter = 0;
my $mu;

GetOptions('f=s' => \$templateInput,
           'm=f' => \$mu,
           'p' => \$isPeriodic,
           'z' => \$zeroAtCenter) or die "$usage\n";

(defined($templateInput) and defined($isPeriodic)) or die "$usage\n";

my $geometry;
my $sites;
my $hptr = {"GeometryKind" => \$geometry, "TotalNumberOfSites" => \$sites};
OmegaUtils::getLabels($hptr,$templateInput);

my ($fmString, $fpString) = @ARGV;
defined($fpString) or die "$usage\n";

my (@filesMinus, @filesPlus);

getFiles(\@filesMinus, $fmString);
getFiles(\@filesPlus, $fpString);

my $numberKs;
my %specMinus;
for (my $i = 0; $i < scalar(@filesMinus); ++$i) {
	readSpectrum(\%specMinus, \$numberKs, $filesMinus[$i]);
}

my %specPlus;
for (my $i = 0; $i < scalar(@filesPlus); ++$i) {
	readSpectrum(\%specPlus, \$numberKs, $filesPlus[$i]);
}

my %specFull;
addSpectrum(\%specFull, \%specMinus);
addSpectrum(\%specFull, \%specPlus);
OmegaUtils::printGnuplot(\%specFull, $geometry,  $isPeriodic, $zeroAtCenter);

if ($geometry eq "ladder") {
	my @nkxpi;
	sumOverOmega(\@nkxpi, \%specMinus, 1);
	printVsQ("outnkxpi.dat", \@nkxpi);
}

my @nkx0;
sumOverOmega(\@nkx0, \%specMinus, 0);
printVsQ("outnkx0.dat", \@nkx0);

my $totalMy = ($geometry eq "ladder") ? 2 : 1;

my %fullVsOmega;
for (my $mp = 0; $mp < 2; ++$mp) { #mp = 0 is -, mp=1 is +
	my $ptr = ($mp == 0) ? \%specMinus : \%specPlus;
	for (my $my = 0; $my < $totalMy; ++$my) {
		my %h;
		sumOverKx(\%h, $ptr, $my);
		printVsOmega("nVsOmegaky$my"."Sector$mp.dat", \%h);
		addToFullOmega(\%fullVsOmega, \%h);
	}
}

printVsOmega("nVsOmega.dat", \%fullVsOmega);

if (defined($mu)) {
	sumWeight(\%fullVsOmega, $mu);
}

sub sumWeight
{
	my ($ptr, $mu) = @_;
	my ($below, $above) = (0, 0);
	my $max = 0;
	for my $omega (sort {$a <=> $b} keys %$ptr) {
		my $val = $ptr->{$omega};
		$max = $val if ($val > $max);
		$val = 0 if ($val < 0);
		if ($omega < $mu) {
			$below += $val;
		} else {
			$above += $val;
		}
	}

	my $fout = "mu.dat";
	open(FOUT, ">", "$fout") or die "$0: Cannot write to $fout : $!\n";
	print FOUT "$mu 0\n";
	print FOUT "$mu $max\n";	
	close(FOUT);
	print STDERR "File $fout written\n";

	my $factor = $below + $above;
	$factor = $sites/$factor;
	$below *= $factor;
	$above *= $factor;
	print STDERR "Below $mu : $below, above $mu: $above\n";


	
}

sub addToFullOmega
{
	my ($v, $ptr) = @_;
	for my $omega (sort {$a <=> $b} keys %$ptr) {
		my $val = $ptr->{$omega};
		$val = 0 if ($val < 0);
		if (!defined($v->{$omega})) {
			$v->{$omega} = $val;
		} else {
			$v->{$omega} += $val;
		}
	}
}

sub printVsOmega
{
	my ($fout, $ptr) = @_;
	open(FOUT, ">", "$fout") or die "$0: Cannot write to $fout : $!\n";
	for my $omega (sort {$a <=> $b} keys %$ptr) {
		my $val = $ptr->{$omega};
		#$val = 0 if ($val < 0);
		print FOUT "$omega $val\n";
	}

	close(FOUT);
	print STDERR "$0: File $fout has been written.\n";
}

sub sumOverKx
{
	my ($v, $ptr, $my) = @_;
	my $factor = 0;
	my @fileIndices=(0);
	if ($geometry eq "chain") {
		$factor = 0.5;
		die "$0: Chain does not have ky != 0\n" if ($my != 0)
	} elsif ($geometry eq "ladder") {
		$factor = 0.25;
		@fileIndices=(0,1);
	} else {
		die "$0: Unknown geometry $geometry\n";
	}

	my $fileIndex = $my;

	for my $omega (sort {$a <=> $b} keys %$ptr) {
		my $aptr = $ptr->{$omega};
		my $nks = scalar(@$aptr) - 1;
		my $numberOfQs = int($factor*$nks);
		for (my $m2 = 0; $m2 < $numberOfQs; ++$m2) {
			#my $realPart = $aptr->[2*$m2+1+2*$fileIndex*$numberOfQs];
			my $imagPart = $aptr->[2*$m2+2+2*$fileIndex*$numberOfQs];
			if (defined($v->{$omega})) {
				$v->{$omega} += $imagPart;
			} else {
				$v->{$omega} = $imagPart;
			}
		}
	}
	
}

sub printVsQ
{
	my ($fout, $v) = @_;
	my $numberOfQs = scalar(@$v);
	my $centerShift = ($numberOfQs & 1) ? ($numberOfQs - 1)/2 : $numberOfQs/2;
	$centerShift = 0 unless ($zeroAtCenter);

	open(FOUT, ">", "$fout") or die "$0: Cannot write to $fout : $!\n";
	for (my $m2 = 0; $m2 < $numberOfQs; ++$m2) {
		my $m = $m2 - $centerShift;
		$m += $numberOfQs if ($m < 0);
		my $q = getQ($m2 - $centerShift, $numberOfQs, $isPeriodic);
		print FOUT "$q ".$v->[$m2]."\n";
	}

	close(FOUT);
	print STDERR "$0: File $fout has been written.\n";
}

sub sumOverOmega
{

	my ($v, $ptr, $my) = @_;
	
	my $factor = 0;
	my @fileIndices=(0);
	if ($geometry eq "chain") {
		$factor = 0.5;
		die "$0: Chain does not have ky != 0\n" if ($my != 0)
	} elsif ($geometry eq "ladder") {
		$factor = 0.25;
		@fileIndices=(0,1);
	} else {
		die "$0: Unknown geometry $geometry\n";
	}

	my $fileIndex = $my;

	for my $omega (sort {$a <=> $b} keys %$ptr) { #no need to sort
		my $aptr = $ptr->{$omega};
		my $nks = scalar(@$aptr) - 1;
		my $numberOfQs = int($factor*$nks);
		for (my $m2 = 0; $m2 < $numberOfQs; ++$m2) {
			#my $realPart = $aptr->[2*$m2+1+2*$fileIndex*$numberOfQs];
			my $imagPart = $aptr->[2*$m2+2+2*$fileIndex*$numberOfQs];
			if (defined($v->[$m2])) {
				$v->[$m2] += $imagPart;
			} else {
				$v->[$m2] = $imagPart;
			}
		}
	}
}

sub readSpectrum
{
	my ($ptr, $ptrN, $file) = @_;

	my $isGood = 1;
	open(FILE, "<", $file) or die "$0: Cannot open $file : $!\n";
	while (<FILE>) {
		next if (/^#/);
		my @temp = split;
		my $n = scalar(@temp);
		if ($n < 2) {
			print STDERR "$0: Ignored line $. in $file, less than 2 cols\n";
			next;
		}

		$$ptrN = $n - 1 if (!defined($$ptrN));
		if ($n - 1 != $$ptrN) {
			$isGood = 0;
			print STDERR "$0: Line $. in $file with wrong cols\n";
			last;
		}

		my $omega = $temp[0];
		my $oldVal = $ptr->{$omega};
		if (defined($oldVal)) {
			for (my $i = 1; $i < $$ptrN; ++$i) {
				$temp[$i] += $oldVal->[$i];
			}
		}

		$ptr->{$omega} = \@temp;
	}

	close(FILE);

	return if ($isGood);

	die "$0: $file with at least 1 line with wrong number of cols, ".$$ptrN." expected\n";
}

sub addSpectrum
{
	my ($v, $ptr) = @_;
	for my $omega (sort {$a <=> $b} keys %$ptr) { #no need to sort
		my $oldVal = $ptr->{$omega};
		my $aptr = $v->{$omega};
		if (defined($aptr)) {
			my $n = scalar(@$aptr);
			for (my $i = 1; $i < $n; ++$i) {
				$v->{$omega}->[$i] += $oldVal->[$i];
			}
		} else {
			my @temp = @$oldVal;
			$v->{$omega} = \@temp;
		}
	}
}


sub getQ
{
	my ($m, $n, $isPeriodic) = @_;
	return ($isPeriodic) ? 2.0*$pi*$m/$n : $m*$pi/($n+1.0);
}

sub getFiles
{
	my ($fm, $string) = @_;
	my @temp = split(/,/, $string);
	@$fm = @temp;
}

