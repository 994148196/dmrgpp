#!/usr/bin/perl
=pod
Copyright (c) 2009-2015, UT-Battelle, LLC
All rights reserved

[DMRG++, Version 4.0]

*********************************************************
THE SOFTWARE IS SUPPLIED BY THE COPYRIGHT HOLDERS AND
CONTRIBUTORS "AS IS" AND ANY EXPRESS OR IMPLIED
WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
PARTICULAR PURPOSE ARE DISCLAIMED.

Please see full open source license included in file LICENSE.
*********************************************************

=cut
use warnings;
use strict;

use lib "../../../PsimagLite/scripts";
use Make;

my @names = qw/KronUtilComplex
	coord2csr
	csc_nnz
	csc_kron_mult
	csc_matmul_post
	csc_matmul_pre
	csr_den_kron_mult
	csr_nnz
	csr_is_eye
	csr_is_zeros
	csr_transpose
	csr_kron_mult
	csr_kron_submatrix
	csr_matmul_post
	csr_matmul_pre
	csr_submatrix
	csr_eye
	den_csr_kron_mult
	den_copymat
	den_zeros
	den_transpose
	den_submatrix
        den_gen_matrix
	den_kron_submatrix
	den_kron_form
	den_kron_mult
	den_matmul_post
	den_matmul_pre
	den_nnz
	den_eye
	den_is_eye
	den_is_zeros
	estimate_kron_cost/;


my @drivers;
my $dotos = "";
my $total= scalar(@names);
for (my $i = 0; $i < $total; ++$i) {
	my $name = $names[$i];
	print STDERR "$name\n";
	my %dmrgDriver = (name => $name, aux => 1);
	push @drivers,\%dmrgDriver;
	$dotos .= " $name.o ";
}

createMakefile();

sub createMakefile
{
	Make::backupMakefile();

	my $fh;
	open($fh,">Makefile") or die "Cannot open Makefile for writing: $!\n";

	Make::newMake($fh,\@drivers,"KronUtil"," "," ","libkronutil.a test1 test2","../");

	local *FH = $fh;
print FH<<EOF;

libkronutil.a: $dotos ../Config.make Makefile
	ar rc libkronutil.a $dotos
	ranlib libkronutil.a

test1: libkronutil.a test1.o $dotos ../Config.make
	\$(CXX) \$(CFLAGS) -o test1 test1.o \$(LDFLAGS) libkronutil.a

test2: libkronutil.a test2.o ../Config.make
	\$(CXX) \$(CFLAGS) -o test2 test2.o \$(LDFLAGS) libkronutil.a

EOF

	close($fh);
	print STDERR "$0: File Makefile has been written\n";
}
