#!/usr/bin/perl
#
#  Copyright (C) 2002-2004 Michael H. Schimek 
#  inspired by a LXR script http://lxr.linux.no/
#
#  This program is free software; you can redistribute it and/or modify
#  it under the terms of the GNU General Public License as published by
#  the Free Software Foundation; either version 2 of the License, or
#  (at your option) any later version.
#
#  This program is distributed in the hope that it will be useful,
#  but WITHOUT ANY WARRANTY; without even the implied warranty of
#  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
#  GNU General Public License for more details.
#
#  You should have received a copy of the GNU General Public License
#  along with this program; if not, write to the Free Software
#  Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
#  --------------------------------------------------------------------------
#
#  This script turns a C header file into functions printing
#  and checking ioctl arguments. It's part of the debugging
#  routines of the Zapping tv viewer http://zapping.sf.net.
#
#  Perl and C gurus cover your eyes. This is one of my first
#  attempts in this funny tongue and far from a proper C parser.

# $Id: structpr.pl,v 1.7 2007/11/27 17:55:04 mschimek Exp $

$number		= '[0-9]+';
$ident		= '\~?_*[a-zA-Z][a-zA-Z0-9_]*';
$signed		= '((signed)?(char|short|int|long))|__s8|__s16|__s32|__s64|signed';
$unsigned	= '(((unsigned\s*)|u|u_)(char|short|int|long))|__u8|__u16|__u32|__u64|unsigned';
$define		= '^\s*\#\s*define\s+';

$printfn	= 'fprint_ioctl_arg';

#
# Syntax of arguments, in brief:
#
# "struct" is the name of a structure. "\.field" is the name of a field
# of this structure, can be "(\.substruct)*\.field" too.
#
# struct.field=SYM_
#   struct.field contains symbolic values starting with SYM_. Only needed
#   for flags, automatically determined if struct.field is an enum type.
#   FIXME we must permit more than one prefix.
# struct.field=string|hex|fourcc
#   Print that field appropriately. If not given the script tries to
#   guess from the field name.
# typedef=blah
#   As above, for simple typedef'ed types.
# struct=mode
# struct.substruct=mode
# struct.field=mode
#   If ioctl is WR, this is an R (input) or W (output parameter)
#   or WR (both). If ioctl is R or W, all parameters are input or output
#   respectively.
# struct.field=FOO:foo
#   Only when struct.field == FOO, print member foo.
# struct.field=R,SYM_, SYM_FOO:foo
#   Combines the hints above.
# struct={ fprintf(fp, "<$s>", t->foo); }
#   Print like this.
#
while (@ARGV) {
    $arg = shift (@ARGV);

    while ("," eq substr ($arg, -1) && @ARGV) {
	$arg .= shift (@ARGV);
    }

    if ($arg =~ m/printfn\=($ident)/) {
	$printfn = $1;
    } elsif ($arg =~ m/(($ident)(\.$ident)?)\={(.*)}/) {
	$print_func{$1} = $4;
    } elsif ($arg =~ m/(($ident)(\.($ident))?)\=(.*)/) {
	$item = $1;
	$container = $2;
	$member = $4;

        foreach (split (',', $5)) {
	    if ($_ =~ m/($ident):(($ident)\.($ident))\s*/) {
#		print "$member == $1 -> $container.$2\n";
		$selector{"$container.$2"} = {
		    key => $member,
		    symbol => $1
		};
	    } elsif ($_ eq "WR" || $_ eq "R" || $_ eq "W") {
		$mode_hint{$item} = $_;
	    } else {
		$symbolic{$item} = $_;
	    }
	}
    } else {
	print "$arg ??\n";
	exit 1;
    }
}

$_ = $/; 
undef($/); 
$contents = <>;
$/ = $_;

#
#  Step I - comb the source and filter out #defines
#

sub wash {
    my $t = $_[0];
    $t =~ s/[^\n]+//gs;
    return ($t);
}

# Remove comments.
$contents =~ s/\/\*(.*?)\*\//&wash($1)/ges;
$contents =~ s/\/\/[^\n]*//g; # C++

# Unwrap continuation lines.
$contents =~ s/\\\s*\n/$1\05/gs;
while ($contents =~ s/\05([^\n\05]+)\05/$1\05\05/gs) {}
$contents =~ s/(\05+)([^\n]*)/"$2"."\n" x length($1)/ges;

sub add_ioctl_check {
    my ($name, $dir, $type) = @_;

    $ioctl_check .= "static __inline__ void IOCTL_ARG_TYPE_CHECK_$name ";
    
    if ($dir eq "W") {
	$ioctl_check .= "(const $type *arg __attribute__ ((unused))) {}\n";
    } else {
	$ioctl_check .= "($type *arg __attribute__ ((unused))) {}\n";
    }
}

sub add_ioctl {
    my ($name, $dir, $i_type, $real_type) = @_;

    $ioctl_cases{$i_type} .= "case $name:\n"
	. "if (!arg) { fputs (\"$name\", fp); return; }\n";

    &add_ioctl_check ($name, $dir, $real_type);
}

# Find macro definitions, create ioctl & symbol table.
$t = "";
$skip = 0;
foreach ($contents =~ /^(.*)/gm) {
    if ($skip) {
	if (/^\s*#\s*endif/) {
	    $skip = 0;
	}

	next;
    }

    # #if 0
    if (/^\s*#\s*if\s+0/) {
	$skip = 1;
    # Ioctls
    } elsif (/$define($ident)\s+_IO(WR|R|W).*\(.*,\s*$number\s*,\s*(struct|union)\s*($ident)\s*\)\s*$/) {
	&add_ioctl ($1, $2, "$3 $4", "$3 $4");
    } elsif (/$define($ident)\s+_IO(WR|R|W).*\(.*,\s*$number\s*,\s*(($signed)|($unsigned))\s*\)\s*$/) {
	if ($symbolic{$1}) {
	    $int_ioctls{$1} = $3;
	    &add_ioctl ($1, $2, $1, $3);
	} else {
	    &add_ioctl ($1, $2, $3, $3);
	}
    } elsif (/$define($ident)\s+_IO(WR|R|W).*\(.*,\s*$number\s*,\s*($ident)\s*\)\s*$/) {
	&add_ioctl ($1, $2, $3, $3);
    } elsif (/$define($ident)\s+_IO(WR|R|W).*\(.*,\s*$number\s*,\s*([^*]+)\s*\)\s*$/) {
	&add_ioctl_check ($1, $2, $3);
    # Define 
    } elsif (/$define($ident)/) {
	push @global_symbols, $1;
    # Other text
    } elsif (!/^\s*\#/) {
	$_ =~ s/\s+/ /g;
	$t="$t$_ ";
    }
}

# Split field lists: struct { ... } foo, bar; int x, y;
$t =~ s/({|;)\s*((struct\s*{[^}]*})\s*($ident))\s*,/\1 \2; \3 /gm;
$t =~ s/({|;)\s*(([^,;}]*)\s+($ident))\s*,/\1 \2; \3 /gm;

# Function pointers are just pointers.
$t =~ s/\(\s*\*\s*($ident)\s*\)\s*\([^)]*\)\s*;/void *\1;/gm;

# Split after ,;{
$t =~ s/(,|;|{)/\1\n/gm;
@contents = split ('\n', $t);

#
#  Step II - parse structs, unions and enums
#

# fieldn = structname\.(field1\.)*fieldn
sub field {
    my ($item) = @_;

    $item =~ s/^($ident\.)*//;
    return $item;
}

# (field1\.)*fieldn = structname\.(field1\.)*fieldn
sub trail {
    my ($item) = @_;

    $item =~ s/^$ident\.//;
    return $item;
}

sub test_cond {
    my ($text, $item) = @_;
    my ($mode, $key, $sym, $sel, $i);
    
    $mode = "WR";
    $i = "$item.dummy";

    while ($i =~ s/\.$ident$//) {
	if ($mode_hint{$i}) {
	    $mode = $mode_hint{$i};
	    last;
	}
    }

    $key = "0";
    $sym = "0";

    if ($selector{$item}) {
	$key = $selector{$item}->{key};
	$sym = $selector{$item}->{symbol};
	$sel = "$sym == t->$key";
    }

#    print "test_cond $item: $mode $key $sym (was $last_cond)\n";

    if ($last_cond ne "$mode $key $sym") {
	$$text .= &flush_args;
	
	if ($last_cond ne "WR 0 0") {
	    $$text .= "}\n";
	}

        if ("R" eq $mode) {
	    if ($selector{$item}) {
		$$text .= "if ((1 & rw) && $sel) {\n";
	    } else {
		$$text .= "if (1 & rw) {\n";
	    }
	} elsif ("W" eq $mode) {
	    if ($selector{$item}) {
		$$text .= "if ((2 & rw) && $sel) {\n";
	    } else {
		$$text .= "if (2 & rw) {\n";
	    }
	} elsif ($selector{$item}) {
	    $$text .= "if ($sel) {\n";
	}

	$last_cond = "$mode $key $sym";
    }
}

# Build a fprintf() with $templ and $args. &flush_args finalizes
# the function.

# text .= "unsigned int", "structname.field1.flags", "%x"
sub add_arg {
    my ($text, $type, $item, $template) = @_;
    my $flush = 0;

    $templ .= &field ($item) . "=$template ";
    $args .= "($type) t->" . &trail ($item) . ", ";
}

# text .= "unsigned int", "structname.field1.flags", "%x"
sub add_ref_arg {
    my ($text, $type, $item, $template) = @_;
    my $flush = 0;

    $templ .= &field ($item) . "=$template ";
    $args .= "($type) & t->" . &trail ($item) . ", ";
}

# text .= functions this depends upon, "struct foo", "structname.field1.foo"
sub add_arg_func {
    my ($text, $deps, $type, $item) = @_;
    my $flush = 0;

    if ($funcs{$type}) {
	my ($lp, $rp, $ref);

	if ($type =~ m/^(struct|union)/) {
	    $lp = "{";
	    $rp = "}";
	    $ref = "&";
	} else {
	    $lp = "";
	    $rp = "";
	    $ref = "";
	}

	push @$deps, $type;

	$type =~ s/ /_/g;

	$templ .= &field ($item) . "=$lp";
	$$text .= &flush_args;

	&test_cond ($text, $item);

	$$text .= "fprint_$type (fp, rw, "
	    . $ref . "t->" . &trail ($item) . ");\n";

	$templ .= "$rp ";
    } else {
	&test_cond ($text, $item);
	$templ .= &field ($item) . "=? ";
    }
}

# text .= functions this depends upon,
#     enum mode (see fprint_symbolic()),
#     "FLAG_", "structname.field1.flags"
sub add_symbolic {
    my ($text, $deps, $enum_mode, $prefix, $item) = @_;
    my ($sbody, $count);

    $count = 0;

    foreach (@global_symbols) {
	if (/^$prefix/) {
	    $str = $_;
	    $str =~ s/^$prefix//;
	    $sbody .= "\"$str\", (unsigned long) $_,\n";
	    ++$count;
	}
    }

    $prefix = lc $prefix;

    if ($count > 3) {
	my $type = "symbol $prefix";

	# No switch() such that fprint_symbolic() can determine if
	# these are flags or enum.
	$funcs{$type} = {
	    text => "static void\n"
		. "fprint_symbol_$prefix (FILE *fp, "
		. "int rw __attribute__ ((unused)), unsigned long value)\n"
		. "{\nfprint_symbolic (fp, $enum_mode, value,\n"
		. $sbody . "(void *) 0);\n}\n\n",
	    deps => []
	};

	&add_arg_func ($text, $deps, $type, $item);
    } else {
	# Inline symbolic

	$templ .= &field ($item) . "=";
	$$text .= &flush_args;

	&test_cond ($text, $item);

	$templ .= " ";
	$$text .= "fprint_symbolic (fp, $enum_mode, t->" . &trail ($item)
	    . ",\n" . $sbody . "(void *) 0);\n";
    }
}

sub flush_args {
    my $text;

    $templ =~ s/^ (\"\n\")/ /;
    $templ =~ s/(\"\n\")$//;

    $args =~ s/^(\s|\n)+//;
    $args =~ s/,?(\s|\n)+$//;

    $text = "";

    if ($templ) {
	if ($args) {
    	    $text .= "fprintf (fp, \"$templ\",\n$args);\n";
	} else {
    	    $text .= "fputs (\"$templ\", fp);\n";
	}
    }

    $templ = "";
    $args = "";

#    print "flush >>$text<<\n";

    return $text;
}

# text .= functions this depends upon,
# 	"struct", "v4l_foo", "WR", 0
# (name can be structname(\.field)+ if nested inline struct or union)
sub aggregate_body {
    my ($text, $deps, $kind, $name, $skip) = @_;

    if ($name ne "?" && $print_func{$name}) {
	$$text .= $print_func{$name} . "\n";
	$skip = 1;
    }

    while (@contents) {
        $_ = shift (@contents);
#	print "<<$name<<$_<<\n";

	# End of aggregate
	if (/^\s*}\s*;/) {
	    $$text .= &flush_args;
	    return "";
	# End of substruct or union
	} if (/^\s*}\s*($ident)\s*;/) {
	    $$text .= &flush_args;
	    return $1;
	# Enum.
	} elsif (/^\s*enum\s+($ident)\s+($ident);/) {
	    if (!$skip) {
		&test_cond ($text, "$name.$2");
		&add_arg_func ($text, $deps, "enum $1", "$name.$2");
	    }
	# Substruct or union.
	} elsif (/^\s*(struct|union)\s+($ident)\s+($ident);/) {
	    if (!$skip) {
		&test_cond ($text, "$name.$3");
		&add_arg_func ($text, $deps, "$1 $2", "$name.$3");
	    }
	# Substruct or union inline definition w/o declaration
	# Why don't you just shoot me...
	} elsif (/^\s*(struct|union)\s+{/) {
	    my $kind = $1;
	    my ($field, $subtext, @temp);

	    $$text .= &flush_args;

	    $subtext = "";
	    @temp = @contents;
	    # skip to determine field name
	    $field = &aggregate_body (\$subtext, $deps, $kind, "?", 1);

	    if ($skip) {
		next;
	    }

	    if ($field ne "") {
		$subtext = "";
		@contents = @temp;
		&test_cond ($text, "$name.$field");
		$templ .= "$field={";
		&aggregate_body (\$subtext, $deps, $kind, "$name.$field", 0);
		$$text .= &flush_args . $subtext;
		&test_cond ($text, "$name.$field");
		$templ .= "} ";
	    } else {
	        $templ .= "? ";
	    }
	# Other stuff, simplified
	} elsif (/^\s*($ident(\s+$ident)*)(\*|\s)+($ident)\s*(\[([a-zA-Z0-9_]+)\]*\s*)?;/) {
	    my $type = $1;
	    my $ptr = $3;
	    my $field = $4;
	    my $size = $6;
	    my $hint = "";
	    my $item = "$name.$field";

	    if ($typedefs{$type}) {
		$hint = $symbolic{$type};
		$type = $typedefs{$type};
	    } elsif ($symbolic{$item}) {
		$hint = $symbolic{$item};
	    }

#	    print "$type $ptr $name.$field [$size] $hint\n";

	    if ($skip) {
		next;
	    }

	    &test_cond ($text, $item);

	    if (0) {
	    # Wisdom: a reserved field contains nothing useful.
	    } elsif ($field =~ "^reserved.*") {
		if ($size ne "") {
		    $templ .= "$field\[\] ";
		} else {
		    $templ .= "$field ";
		}
	    # Pointer
	    } elsif ($ptr eq "*") {
		# Array of pointers?
		if ($size ne "") {
		    # Not smart enough, ignore
		    $templ .= "$field\[\]=? ";
	        # Wisdom: char pointer is probably a string.
		} elsif ($type eq "char" || $field eq "name" || $hint eq "string") {
		    &add_arg ($text, "const char *", $item, "\\\"%s\\\"");
		# Other pointer
		} else {
		    &add_arg ($text, "const void *", $item, "%p");
		}
	    # Array of something
	    } elsif ($size ne "") {
	        # Wisdom: a char array contains a string.
		# "Names" are also commonly strings.
		if ($type eq "char" || $field eq "name" || $hint eq "string") {
		    $args .= "$size, ";
		    &add_arg ($text, "const char *", $item, "\\\"%.*s\\\"");
		# So this is some other kind of array, what now?
		} else {
		    # ignore
		    $templ .= "$field\[\]=? ";
		}
	    # Wisdom: a field named flags typically contains flags.
	    } elsif ($field eq "flags") {
	        if ($hint ne "") {
		    &add_symbolic ($text, $deps, 2, $hint, $item);
		} else {
		    # flags in hex
		    &add_arg ($text, "unsigned long", $item, "0x%lx");
		}
	    # Hint: something funny
	    } elsif ($hint eq "hex") {
		&add_arg ($text, "unsigned long", $item, "0x%lx");
	    } elsif ($hint eq "fourcc") {
		&add_ref_arg ($text, "const char *", $item,
			      "\\\"%.4s\\\"=0x%lx");
		$args .= "(unsigned long) t->$field, ";
	    # Field contains symbols, could be flags or enum or both
	    } elsif ($hint ne "") {
	        &add_symbolic ($text, $deps, 0, $hint, $item);
	    # Miscellaneous integers. Suffice to distinguish signed and
	    # unsigned, compiler will convert to long automatically
	    } elsif ($type =~ m/$unsigned/) {
	        &add_arg ($text, "unsigned long", $item, "%lu");
	    } elsif ($type =~ m/$signed/) {
	        &add_arg ($text, "long", $item, "%ld");
	    # The Spanish Inquisition.
    	    } else {
	        $templ .= "$field=? ";
	    }

	    $templ .= "\"\n\"";
	    $args .= "\n";
	}
    }
}

sub aggregate {
    my ($kind, $name) = @_;
    my ($text, @deps);
    my $type = "$kind $name";

    $funcs{$type} = {
	text => "static void\nfprint_$kind\_$name "
	    . "(FILE *fp, int rw __attribute__ ((unused)), const $type *t)\n{\n",
	deps => []
    };

    $last_cond = "WR 0 0";

    aggregate_body (\$funcs{$type}->{text},
		    $funcs{$type}->{deps},
		    $kind, $name, 0);

    if ($last_cond ne "WR 0 0") {
	$funcs{$type}->{text} .= "}\n";
    }

    $funcs{$type}->{text} .= "}\n\n";
}

sub common_prefix {
    my $prefix = @_[0];
    my $symbol;

    foreach $symbol (@_) {
	while (length ($prefix) > 0) {
	    if (index ($symbol, $prefix) == 0) {
	        last;
	    } else {
	        $prefix = substr ($prefix, 0, -1);
	    }
	}
    }

    return ($prefix);
}

sub enumeration {
    my $name = @_[0];
    my $type = "enum $name";
    my @symbols;

    $funcs{$type} = {
	text => "static void\nfprint_enum_$name (FILE *fp, "
	    . "int rw __attribute__ ((unused)), int value)\n"
	    . "{\nfprint_symbolic (fp, 1, value,\n",
	deps => []
    };

    while (@contents) {
	$_ = shift(@contents);
	if (/^\s*\}\s*;/) {
	    last;
	} elsif (/^\s*($ident)\s*(=\s*.*)\,/) {
	    push @symbols, $1;
	}
    }

    $prefix = &common_prefix (@symbols);

    foreach $symbol (@symbols) {
	$funcs{$type}->{text} .=
	    "\"" . substr ($symbol, length ($prefix))
	    . "\", (unsigned long) $symbol,\n";
    }

    $funcs{$type}->{text} .= "(void *) 0);\n}\n\n";
}

# Let's parse

while (@contents) {
    $_ = shift(@contents);
    # print ">>$_<<\n";

    if (/^\s*(struct|union)\s*($ident)\s*\{/) {
	&aggregate ($1, $2);
    } elsif (/^\s*enum\s*($ident)\s*\{/) {
	&enumeration ($1);
    } elsif (/^\s*typedef\s*([^;]+)\s+($ident)\s*;/) {
	$typedefs{$2} = $1;
    }
}

#
# Step III - create the file
#

print "/* Generated file, do not edit! */

#include <stdio.h>
#include \"io.h\"

#ifndef __GNUC__
#undef __attribute__
#define __attribute__(x)
#endif

";

while (($name, $type) = each %int_ioctls) {
    my $prefix;
    my $sbody;

    $prefix = $symbolic{$name};

    foreach (@global_symbols) {
	if (/^$prefix/) {
	    $str = $_;
	    $str =~ s/^$prefix//;
	    $sbody .= "\"$str\", (unsigned long) $_,\n";
	}
    }

    # No switch() such that fprint_symbolic() can determine if
    # these are flags or enum.
    $funcs{$name} = {
	text => "static void\n"
	    . "fprint_$name (FILE *fp, "
	    . "int rw __attribute__ ((unused)), $type *arg)\n"
	    . "{\nfprint_symbolic (fp, 0, (unsigned long) *arg,\n"
	    . $sbody . "(void *) 0);\n}\n\n",
	deps => []
    };
}

sub print_type {
    my ($type) = @_;

    if (!$printed{$type}) {
	foreach $dependency (@{$funcs{$type}->{deps}}) {
	    &print_type ($dependency);
	}

	print $funcs{$type}->{text};

	$printed{$type} = TRUE;
    }
}

$text = "static void\n$printfn (FILE *fp, unsigned int cmd, int rw, void *arg)\n"
    . "{\nswitch (cmd) {\n";

while (($type, $case) = each %ioctl_cases) {
    if ($typedefs{$type}) {
	if ($symbolic{$type}) {
	    &print_type ($type);
	    $prefix = lc $symbolic{$type};
	    $type = $typedefs{$type};
	    $text .= "$case fprint_symbol_$prefix ";
	    $text .= "(fp, rw, * ($type *) arg);\nbreak;\n";
	    next;
	}

	$type = $typedefs{$type};
    }

    if ($funcs{$type}) {
	&print_type ($type);
	$type =~ s/ /_/;
	$text .= "$case fprint_$type (fp, rw, arg);\nbreak;\n";
    } elsif ($type =~ m/$unsigned/) {
	$text .= "$case fprintf (fp, \"%lu\", "
	    . "(unsigned long) * ($type *) arg);\nbreak;\n";
    } elsif ($type =~ m/$signed/) {
	$text .= "$case fprintf (fp, \"%ld\", "
	    . "(long) * ($type *) arg);\nbreak;\n";
    } else {
	$text .= "$case break; /* $type */\n";
    }
}

$text .= "\tdefault:\n"
    . "\t\tif (!arg) { fprint_unknown_ioctl (fp, cmd, arg); return; }\n"
    . "\t\tbreak;\n";
$text .= "\t}\n\}\n\n";

print $text;

print $ioctl_check;
print "\n";
