#! /usr/bin/perl -w
# $Id: network-table.pl,v 1.2 2005/10/07 14:53:20 mschimek Exp $

use strict;
use XML::Simple;   # http://search.cpan.org/search?query=XML::Simple
use Data::Dumper;

my $xml = XMLin ("-",
		 ForceContent => 1,
		 ForceArray => ["network"]);

# print Dumper ($xml);

print "/* Generated from http://zapping.sf.net/zvbi-0.3/networks.xml */

const struct vbi_cni_entry
vbi_cni_table[] = {
";

for (@{$xml->{country}}) {
    my $crecord = $_;
    my $cc = $_->{"country-code"}->{content};

    for (@{$_->{"network"}}) {
	my $nrecord = $_;

	if (!(defined ($nrecord->{"cni-8301"}->{content}) ||
	      defined ($nrecord->{"cni-8302"}->{content}) ||
	      defined ($nrecord->{"cni-pdc-b"}->{content}) ||
	      defined ($nrecord->{"cni-vps"}->{content}))) {
	    next;
	}

	print "\t{ ", substr ($nrecord->{"id"}, 1),
	      ", \"", $cc,
	      "\", \"", $nrecord->{"name"}->{content}, "\"";

	for (qw/cni-8301 cni-8302 cni-pdc-b cni-vps/) {
	    if (defined ($nrecord->{$_}->{content})) {
		my $value = hex ($nrecord->{$_}->{content});

		printf ", 0x%04X", $value;
	    } else {
		print ", 0x0000";
	    }
	}

	print " },\n";
    }
}

print "\t{ 0, \"\", 0, 0, 0, 0, 0 }\n};\n\n";
