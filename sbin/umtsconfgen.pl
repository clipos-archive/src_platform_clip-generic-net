#!/usr/bin/perl
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright © 2008-2018 ANSSI. All Rights Reserved.
# Copyright 2009-2013 SGDSN/ANSSI
# Author: Vincent Strubel <clipos@ssi.gouv.fr>
# Author: Florent Chabaud <clipos@ssi.gouv.fr>
# All rights reserved.

use strict;
use warnings;
use utf8;


# Match valid field names to valid values (as regexp).
my %fields = (	
	# pin: 4-8 decimal digits
	"UMTS_PIN"	=>	
		[ "[[:digit:]]{4,8}", 0, "pin" ],

	# apn: string
	"UMTS_APN"	=>	
		[ "\\S+", 1, "apn" ],
	
	# identity: string
	"UMTS_IDENTITY"	=>	
		[ "\\S+", 1, "identity" ],
	
	# password: string
	"UMTS_PASSWORD"	=>	
		[ "\\S+", 1, "password" ],
);

my @fieldnames = qw(UMTS_PIN UMTS_APN UMTS_IDENTITY UMTS_PASSWORD);
my @emptyfields = qw(UMTS_IDENTITY UMTS_PASSWORD);

my $input = $ARGV[0];
my $output = $ARGV[1];

my %curfields;

die "Missing argument" unless (defined($output));

open IN, "<:encoding(UTF-8)", $input or die "Could not open $input";
my @lines = <IN>;
close IN;

foreach my $line (@lines) {
	next unless ($line =~ /^([^=]+)=(.+)$/);

	my $var = $1;
	my $val = $2;
	next unless (defined $fields{$var});

	my $re = $fields{$var}->[0];

	if ($val =~ /^($re)/) {
		$curfields{$var} = $1;
	} else {
		print STDOUT "$var: $val - unsupported value\n";
	}
}

foreach my $var (@emptyfields) {
	$curfields{$var} = "." unless (defined $curfields{$var});
}

open OUT, ">:encoding(UTF-8)", $output or die "Could not open $output";
foreach my $var (@fieldnames) {
	if ($fields{$var}->[1]) {
		print OUT "$fields{$var}->[2]: \"$curfields{$var}\"\n";
	} else {
		print OUT "$fields{$var}->[2]: $curfields{$var}\n";
	}
}

close OUT;
