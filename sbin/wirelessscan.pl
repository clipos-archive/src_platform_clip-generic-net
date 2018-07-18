#!/usr/bin/perl
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright © 2008-2018 ANSSI. All Rights Reserved.
# List active wireless cells, and write a formated list to
# /usr/local/var/wifiscan.txt.
# Author: Vincent Strubel <clipos@ssi.gouv.fr>
# Copyright (C) 2009 SGDN/ANSSI
# All rights reserved.

use strict; 
use warnings;
use CLIP::Logger ':all';
use Sys::Syslog ':macros';
use utf8;

$g_log_debug = 0;
$g_log_syslog = 1;
$g_log_prefix = "wirelessscan";
$CLIP::Logger::g_facilities->{"warn"} = LOG_DAEMON;
$CLIP::Logger::g_facilities->{"log"} = LOG_DAEMON;

my $g_interface = "";
my $g_up_initially = 0;
my $g_exit_code = 1;

my $g_output_path = "/usr/local/var/wifiscan.txt";

sub set_up() {
	# List interfaces, find a wireless one
	unless (open IN, "/sbin/iwconfig 2>/dev/null |") {
		clip_warn "failed to run iwconfig";
		return 0;
	}
	my @iwconf = <IN>;
	unless (close IN) {
		clip_warn "iwconfig returned an error";
		foreach (@iwconf) {
			clip_warn "iwconfig output: $_";
		}
		return 0;
	}

IWLOOP:
	foreach (@iwconf) {
		if (/^(\S+)\s+(:?IEEE )?802.11/) {
			$g_interface = $1;
			last IWLOOP;
		}
	}

	unless ($g_interface) {
		clip_warn "no wireless interface found";
		return 0;
	}

	# See if that interface is already up ...
	unless (open IN, "/sbin/ip link show $g_interface 2>&1 |") {
		clip_warn "failed to run ip link show $g_interface";
		return 0;
	}
	my @iplink = <IN>;
	unless (close IN) {
		clip_warn "ip link show $g_interface returned an error";
		foreach (@iplink) {
			clip_warn "ip link show output: $_";
		}
		return 0;
	}

	foreach (@iplink) {
		if (/[^_]UP/) {
			$g_up_initially = 1;
			return 1; # k, we're done here
		}
	}

	# ... if not, try to set it up for the scan
	unless (open IN, "/sbin/iwconfig $g_interface essid off 2>&1 |") {
		clip_warn "failed to set essid off";
		return 0;
	}
	my @output = <IN>;
	unless (close IN) {
		clip_warn "error setting essid off on $g_interface";
		foreach (@output) {
			clip_warn "iwconfig essid off output: $_";
			return 0;
		}
	}

	unless (open IN, "/sbin/ip link set $g_interface up 2>&1 |") {
		clip_warn "failed to turn up $g_interface";
		return 0;
	}
	my @output = <IN>;
	unless (close IN) {
		clip_warn "error turning up $g_interface";
		foreach (@output) {
			clip_warn "ip link set up output: $_";
			return 0;
		}
	}

	return 1;
}

sub clean_up() {
	return 1 if ($g_up_initially);

	unless (open IN, "/sbin/ip link set $g_interface down 2>&1 |") {
		clip_warn "failed to turn down $g_interface";
		return 0;
	}
	my @output = <IN>;
	unless (close IN) {
		clip_warn "error turning down $g_interface";
		foreach (@output) {
			clip_warn "ip link set down output: $_";
			return 0;
		}
	}
	return 1;
}

sub read_cell($$) {
	my ($iref, $oref) = @_;

	my $more = 0;
	my %desc = (
		"enc"	=>	"",
		"essid"	=>	"",
		"qual"	=>	"",
		"qover" =>	"",
		"mode"	=>	"",
		"wpa"	=>	"",
	);
LOOP:
	while (my $line = shift @{$iref}) {
		if ($line =~ /^\s+Cell/) {
			$more = 1;
			last LOOP;
		}
		if ($line =~ /^\s+Mode:(Managed|Ad-Hoc|Master)$/) {
			$desc{"mode"} = $1;
			next LOOP;
		}
		if ($line =~ /^\s+ESSID:"([^"]+)"$/) {
			$desc{"essid"} = $1;
			next LOOP;
		}
		if ($line =~ /^\s+Quality=(\d+)\/(\d+)\s/) {
			$desc{"qual"} = $1;
			$desc{"qover"} = $2;
			next LOOP;
		}
		if ($line =~ /^\s+Encryption key:on$/) {
			$desc{"enc"} = "y";
			next LOOP;
		}
		if ($line =~ /^\s+IE: WPA/) {
			$desc{"wpa"} = "y";
			next LOOP;
		}
		if ($line =~ /^\s+IE: IEEE 802.11i\/WPA2/) {
			$desc{"wpa"} = "y";
			next LOOP;
		}
	}

	# Return marker for hidden essid
	$desc{"essid"} = "??" unless ($desc{"essid"});
	$desc{"mode"} = "Managed" if ($desc{"mode"} eq "Master");

	return $more unless ($desc{"qual"});

	push @{$oref}, (\%desc);

	return $more;
}	

sub write_desc($$) {
	my ($desc, $outref) = @_;
	my $ret = "$desc->{'essid'}\"$desc->{'qual'}/$desc->{'qover'}";

	if ($desc->{'enc'}) {
		if ($desc->{'wpa'}) {
			$ret .= "\"wpa";
		} else {
			$ret .= "\"wep";
		}
	} else {
		$ret .= "\"none";
	}
	$ret .= "\"$desc->{'mode'}";
	print "ret: $ret\n";

	push @{$outref}, ($ret);
}

set_up() or exit 0;

unless (open IN, "/sbin/iwlist scan 2>&1 |") { 
	clip_warn "failed to run iwlist scan";
	goto CLEANUP;
}
my @input = <IN>;
unless (close IN) {
	clip_warn "iwlist scan returned an error";
	foreach (@input) {
		clip_warn "iwlist scan input";
	}
	goto CLEANUP;
}

my @output = ();

PRELOOP:
while (my $line = shift @input) {
	last PRELOOP if ($line =~ /\s+Cell/);
}

while (read_cell(\@input, \@output)) {;}

my @to_write = ();
foreach my $desc (sort { $b->{'qual'} <=> $a->{'qual'} } @output) {
	print "essid: $desc->{'essid'}\n";
	write_desc($desc, \@to_write);
}

unless (open OUT, ">:encoding(UTF-8)", $g_output_path) {
	clip_warn "failed to open $g_output_path for writing";
	goto CLEANUP;
}
chmod 0644, $g_output_path;

foreach (@to_write) {
	print OUT "$_\n";
}
close OUT or clip_warn "error closing $g_output_path";
$g_exit_code = 0;

CLEANUP:
clean_up();
exit $g_exit_code;
