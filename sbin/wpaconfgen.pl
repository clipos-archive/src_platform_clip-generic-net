#!/usr/bin/perl
# SPDX-License-Identifier: LGPL-2.1-or-later
# Copyright © 2008-2018 ANSSI. All Rights Reserved.
# Copyright 2009 SGDN/DCSSI
# Copyright 2010 ANSSI
# Author: Vincent Strubel <clipos@ssi.gouv.fr>
# All rights reserved

use strict;
use warnings;
use utf8;

sub genpsk($) {
	my $fields = shift;

	return 1 unless (defined($fields->{'WPA_PSK'}) and defined($fields->{'WIRELESS_ESSID'}));

	my $essid = $fields->{'WIRELESS_ESSID'};
	my $psk = $fields->{'WPA_PSK'};

	# 64 hex digits => pre-hashed key, no hashing 
	# Caveat : relies on the fact that the parsing regexp for WPA_PSK
	# makes sure that all 64 chars are hex digits.
	return 1 if (length $psk == 64);

	open PIPE, "wpa_passphrase \'$essid\' \'$psk\'|";
	my @output = <PIPE>;
	close PIPE;
	if ($?) {
		print STDERR "wpa_passphrase failed\n";
		foreach (@output) {
			print STDERR "wpa_passphrase output: $_";
		}
		return 0;
	}
	my $secret="";
	LOOP:
	foreach my $line (@output) {
		next LOOP unless ($line =~ /\spsk=(.*)$/);
		$secret = $1;
		last LOOP;
	}
	return 0 unless ($secret);
	$fields->{'WPA_PSK'} = $secret;
	return 1;
}
# Match valid field names to valid values (as regexp).
my %wpa_fields = (
	# scan_ssid: 0 / 1
	"WPA_SCAN_SSID" =>	
		[ "0|1", 0, "scan_ssid" ],

	# proto: either WPA, RSN, or WPA RSN
	"WPA_PROTO"	=> 	
		[ "(?:WPA|RSN)(?: WPA| RSN)?", 0, "proto" ],

	# key_mgmt: either WPA-PSK or WPA-EAP mutually exclusive
	"WPA_KEY_MGMT"	=> 	
		[ "(?:WPA-PSK|WPA-EAP)", 0, "key_mgmt" ],

	# When key_mgmt is set to WPA-EAP, then the protocol usually is one of
	# either TLS, TTLS (EAP-MD5), or PEAP. CLIP forbids TTLS (EAP-MD5) and PEAP (MS stuff).
	# So when WPA-EAP is set, wpa_supplicant parameter eap is forced to TLS.
	# The network configuration MUST have a subdirectory named certs
	# This directory MUST contain three files named
	# certs/ac.pem
	# certs/priv.pem
	# certs/pub.pem
	# Conversion to PEM/DER format can be achieved with openssl. For instance
	# if original files are un pkcs12 PFX format :
	# openssl pkcs12 -in client.pfx -out pub.pem -clcerts -nokeys
	# openssl pkcs12 -in client.pfx -out priv.pem -clcerts
	# openssl pkcs12 -in CA.pfx -out ac.pem -cacerts -nokeys	
	# Semantic of WPA_PASSWORD is changed : it becomes the password phrase of the private key.

	# pairwise: either CCMP, TKIP, or both (NONE excluded) 
	"WPA_PAIRWISE"	=> 	
		[ "(?:CCMP|TKIP)(?: CCMP| TKIP)?", 0, "pairwise" ],

	# group: combination of CCMP, TKIP, WEP104, WEP40
	"WPA_GROUP" 	=>	
		[ "(?:CCMP|TKIP|WEP104|WEP40)(?: CCMP| TKIP| WEP104| WEP40)*", 
				0, "group" ],
	
	# psk: either 64 hex digits, or 8 to 63 ASCII chars
	"WPA_PSK"	=>	
		[ "[[:xdigit:]]{64}?|[[:ascii:]]{8,63}", 0, "psk" ],

	# identity: string
	"WPA_IDENTITY"	=>	
		[ "\\S+", 1, "identity" ],
	
	# password: string (EAP)
	"WPA_PASSWORD"	=>	
		[ "\\S+", 1, "private_key_passwd" ],

	"WIRELESS_ESSID" =>
		[ "[[:graph:] ]+", 1, "ssid" ],
);

my %open_fields = (	
	"WIRELESS_ESSID" =>
		[ "[[:graph:] ]+", 1, "ssid" ],
	"WIRELESS_MODE" =>
		[ "Managed|Ad-Hoc", 0, "mode" ],
);

my %wep_fields = (	
	"WIRELESS_ESSID" =>
		[ "[[:graph:] ]+", 1, "ssid" ],
	"WIRELESS_WEPKEY" =>
		[ "[[:graph:] ]+", 0, "wep_key0" ],
	"WIRELESS_MODE" =>
		[ "Managed|Ad-Hoc", 0, "mode" ],
);

my $fields;

my %fprios;
my @fieldnames = qw(WIRELESS_ESSID WIRELESS_MODE WIRELESS_WEPKEY WPA_SCAN_SSID WPA_PROTO WPA_KEY_MGMT WPA_PAIRWISE WPA_GROUP WPA_PSK WPA_IDENTITY WPA_PASSWORD);

my $i = 100;
grep ( $fprios{$_} = $i--, @fieldnames);

my $input = $ARGV[0];
my $output = $ARGV[1];
my $ifname = $ARGV[2];
my $encr = $ARGV[3];

my %curfields;
my $ap_scan = "1";

die "Missing argument" unless (defined($encr));

if ($encr eq "wpa") {
	$fields = \%wpa_fields;
} elsif ($encr eq "wep") {
	$fields = \%wep_fields;
} elsif ($encr eq "none") {
	$fields = \%open_fields;
} else {
	die "unsupported encryption scheme: $encr";
}

open IN, "<:encoding(UTF-8)", $input or die "Could not open $input";
my @lines = <IN>;
close IN;

foreach my $line (@lines) {
	next unless ($line =~ /^([^=]+)=(.+)$/);

	my $var = $1;
	my $val = $2;
	next unless (defined $fields->{$var});

	my $re = $fields->{$var}->[0];

	if ($val =~ /^($re)/) {
		$curfields{$var} = $1;
	} else {
		print STDOUT "$var: $val - unsupported value\n";
	}
}

if ($encr eq "wpa") {
	genpsk(\%curfields) or die "genpsk failed";
} else {
	if ($encr eq "wep") {
		die "missing wepkey" unless defined($curfields{"WIRELESS_WEPKEY"});
		my $key = $curfields{"WIRELESS_WEPKEY"};
		if ($key =~ /^s:(.*)$/) {
			$curfields{"WIRELESS_WEPKEY"} = "\"$1\"";
		}
	}
	if (defined($curfields{"WIRELESS_MODE"})) {
		my $mode = $curfields{"WIRELESS_MODE"};
		if ($mode eq "Ad-Hoc") {
			$curfields{"WIRELESS_MODE"} = "1";
			$ap_scan = "2";
		} else {
			$curfields{"WIRELESS_MODE"} = "0";
		}
	}
}

open OUT, ">:encoding(UTF-8)", $output or die "Could not open $output";
print OUT "ctrl_interface=/var/run/wpa_supplicant\n";
print OUT "eapol_version=1\n";
print OUT "ap_scan=$ap_scan\n\n";
print OUT "network={\n";
if ($encr ne "wpa") {
	print OUT "\tkey_mgmt=NONE\n";
}
foreach my $var (sort {$fprios{$b} <=> $fprios{$a} } keys %curfields) {
	if ($fields->{$var}->[1]) {
		print OUT "\t$fields->{$var}->[2]=\"$curfields{$var}\"\n";
	} else {
		print OUT "\t$fields->{$var}->[2]=$curfields{$var}\n";
	}
}

if (defined($curfields{"WPA_KEY_MGMT"}) and $curfields{"WPA_KEY_MGMT"} eq "WPA-EAP") {
	print OUT "\teap=TLS\n";
	print OUT "\teap_workaround=1\n";
	print OUT "\tca_cert=\"/etc/admin/conf.d/netconf/certs/ac.pem\"\n";
	print OUT "#\tca_path=\"/etc/admin/conf.d/netconf/certs\"\n";
	print OUT "\tclient_cert=\"/etc/admin/conf.d/netconf/certs/pub.pem\"\n";
	print OUT "\tprivate_key=\"/etc/admin/conf.d/netconf/certs/priv.pem\"\n";
}

if ($encr eq "wep") {
	print OUT "\twep_tx_keyidx=0\n";
	print OUT "\tauth_alg=SHARED\n";
}

print OUT "\tscan_ssid=1\n";
print OUT "}\n";

close OUT;
