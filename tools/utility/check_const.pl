#!/usr/bin/perl
# Copyright (C) 2014 Red Hat, Inc.
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301
# USA
#
# Author: Gris Ge <fge@redhat.com>
#

# This script compare public constants of lsm Python library files with C
# library include files.

# Naming scheme:
#   py_name     # Constant name used in Python, example:
#               #   'lsm.System.STATUS_OK'
#
#   py_value    # The value of python constant.
#
#   c_name      # Constant name used in C, example:
#               #   'LSM_SYSTEM_STATUS_OK'
#
#   c_value     # The value of C constant. We stored the raw string.

use strict;
use warnings;

use File::Basename;
use Cwd 'abs_path';
use Data::Dumper;

my $LSM_CODE_BASE_DIR = dirname( dirname( dirname( abs_path($0) ) ) );
my $PYTHON_LIB_DIR    = "$LSM_CODE_BASE_DIR/python_binding/lsm";
my $C_LIB_HEADER = "$LSM_CODE_BASE_DIR"
                   . "/c_binding/include/libstoragemgmt/libstoragemgmt.h";

my $REGEX_VALUE_FORMAT = qr/
    (?<NUM>(?&NUM_PAT))

    (?(DEFINE)
        # integer number
        (?<NUM_INT>
            [0-9]+
        )
        # Bit shift:
        #   1 << 9
        (?<NUM_BIT_SHIFT>
            1
            [\ \t]+
            <<
            [\ \t]+
            [0-9]+
        )
        # Hex number
        (?<NUM_HEX>
            0x[0-9]+
        )
        (?<NUM_PAT>
            (?&NUM_BIT_SHIFT) | (?&NUM_HEX) | (?&NUM_INT)
        )
    )
/x;

my $REGEX_C_CONST_FORMAT = qr/
    ^
    (?: (?&HEADER_PAT))
    (?<CNAME>(?&CNAME_PAT))
    (?: (?&SPLITER_PAT))
    (?<NUM>(?&NUM_PAT))

    (?(DEFINE)
        # integer number
        (?<NUM_INT>
            [0-9]+
        )
        # Bit shift:
        #   1 << 9
        (?<NUM_BIT_SHIFT>
            1
            [\ \t]+
            <<
            [\ \t]+
            [0-9]+
        )
        # Hex number
        #   0x0000000000000001
        (?<NUM_HEX>
            0x[0-9]+
        )
        (?<NUM_PAT>
            (?&NUM_BIT_SHIFT) | (?&NUM_HEX) | (?&NUM_INT)
        )
        (?<CNAME_PAT>
            [A-Z][A-Z_]+
        )
        (?<HEADER1>
            [\ \t]*
        )
        (?<HEADER2>
            \#define[\ \t]+
        )
        (?<HEADER_PAT>
            (?&HEADER1) | (?&HEADER2)
        )
        (?<SPLITER_PAT>
            [\ \t]*
            [=]*
            [\ \t]*
        )
    )
/x;

my %PY_CLASS_NAME_CONV = (
    'Capabilities' => 'CAP',
    'ErrorNumber' => 'ERR',
    'JobStatus' => 'JOB',
    'ErrorLevel' => 'ERR_LEVEL',
);

my $REF_RESULT = {
    'pass'               => {},
    'fail'               => {},
    'c_missing'          => {},
    'py_missing'         => {},
    'c_const_hash'       => {},
    'py_const_hash'      => {},
    'known_c_to_py_name' => {},
    'known_py_to_c_name' => {},
};

# $REF_RESULT = {
#   'pass' => {
#       $py_name => 1,  # Just for deduplication.
#   },
#   'fail' => {
#       $py_name => 1,  # Just for deduplication.
#   },
#   'c_missing' => {
#       $py_name => 1,
#   },
#   'py_missing' => {
#       $c_name => 1,
#   },
#   'py_const_hash' => {
#       $py_name => $py_value,
#   },
#   'c_const_hash' => {
#       $c_name => $c_value,
#   },
#   'known_c_to_py_name' => {
#       $c_name => $py_name,
#   }
# }
#

$|++;

sub is_in_array($$) {
    my $ref_array = shift;
    my $item      = shift;
    return 1 if grep { $_ eq $item } @{$ref_array};
    return undef;
}

sub py_name_2_c_name($) {

    # We do these conversion:
    #   1. Convert CaMel to CA_MEL
    #   2. Convert System to SYSTEM
    #   3. Convert Capabilities to CAP and etc using %PY_CLASS_NAME_CONV;
    my $py_name = shift;
    if ( $py_name =~ /^lsm\.([a-zA-Z]+)\.([A-Z_]+)$/ ) {
        my $py_class_name = $1;
        my $py_var_name   = $2;

        # Convert camel class name
        if (defined $PY_CLASS_NAME_CONV{$py_class_name}){
            return sprintf "LSM_%s_%s",
                $PY_CLASS_NAME_CONV{$py_class_name}, $py_var_name;
        }
        if ( $py_class_name =~ /^[A-Z][a-z]+$/ ) {
            $py_class_name =~ tr/[a-z]/[A-Z]/;
            return sprintf "LSM_%s_%s", $py_class_name, $py_var_name;
        }
        if ( $py_class_name =~ /^([A-Z][a-z]+)([A-Z][a-z]+)$/ ) {
            $py_class_name = sprintf "%s_%s", $1, $2;
            $py_class_name =~ tr/[a-z]/[A-Z]/;
            return sprintf "LSM_%s_%s", $py_class_name, $py_var_name;
        }
    }

    die "FAIL: Ilegal python constant name '$py_name'.\n";
}

sub _parse_c_init_header($){
    # Take initial C header file and read its sub header files
    # Return a reference of array containing file path.
    my $init_header = shift;
    my $folder_path = dirname($init_header);
    open my $init_header_fd, "<", $init_header
      or die "FAIL: Failed to open $init_header $!\n";
    my @rc = ();
    map{
        push @rc, "$folder_path/$1" if /#include "([^"]+)"/;
    }<$init_header_fd>;
    return \@rc;
}

sub _get_c_constants($){
    my $c_header = shift;
    open my $c_header_fd, "<", $c_header
      or die "FAIL: Failed to open $c_header $!\n";
    my %rc = ();
    map{
        $rc{$+{'CNAME'}} = $+{'NUM'} if /$REGEX_C_CONST_FORMAT/;
    }<$c_header_fd>;
    return \%rc;
}

sub parse_out_c_const() {

    # Return a reference like this:
    #       {
    #           $c_name => $value,
    #       }
    my $ref_sub_c_headers = _parse_c_init_header($C_LIB_HEADER);
    my $ref_c_name_2_value = {};
    foreach my $cur_c_header (@{$ref_sub_c_headers}){
        my $ref_tmp = _get_c_constants($cur_c_header);
        foreach my $key_name (keys %{$ref_tmp}){
            $ref_c_name_2_value->{$key_name} = $ref_tmp->{$key_name};
        }
    }
    return $ref_c_name_2_value;
}

sub _parse_py_init_file($) {

    # Return a reference of array containging file path of sub python module.
    my $init_file = shift;
    open my $init_fd, "<", $init_file
      or die "FAIL: Failed to open $init_file: $!\n";
    my $folder_path = dirname($init_file);
    my @rc1         = ();
    my @rc2         = ();
    my @lines       = ();

    # Merge multiline codes
    foreach my $line (<$init_fd>) {
        chomp $line;
        if ( $line =~ /^[^ ]/ ) {
            push @lines, $line;
        }
        else {
            $lines[-1] .= $line;
        }
    }
    close $init_fd;

    foreach my $line (@lines) {
        if ( $line =~ /from ([^ ]+) import (.+)$/ ) {
            push @rc1, sprintf "%s/%s.py", $folder_path, $1;
            my $class_line = $2;
            while ( $class_line =~ /([A-Z][a-zA-Z]+)[, \\]*/g ) {
                push @rc2, $1;
            }
        }
    }
    return \@rc1, \@rc2;
}

sub _get_py_class_consts($$){
    # Take $file_path and $ref_classes
    # Return reference of hash:
    #   {
    #       $py_name => $value,
    #   }
    my $py_file = shift;
    my $ref_classes = shift;

    open my $py_fd, "<", $py_file
      or die "FAIL: Failed to open $py_file: $!\n";
    my %rc_hash = ();
    my $cur_class_name = undef;
    my $current_idention = undef;
    foreach my $line (<$py_fd>){
        chomp $line;
        if ($line =~ /^([ ]*)class[ ]+([^\(]+)\(/){
            $current_idention = $1;
            $cur_class_name = $2;
            unless (is_in_array($ref_classes, $cur_class_name)){
                $cur_class_name = undef;
                next;
            }
        }
        unless(defined $cur_class_name){
            next;
        }
        if ($line =~ /^$current_idention
                [\ ]+
                ([A-Z][A-Z\_]+)
                [\ ]*=[\ ]*
                ($REGEX_VALUE_FORMAT)/x){
            my $var_name = $1;
            my $py_value = $2;
            my $py_name = sprintf "lsm.%s.%s", $cur_class_name, $var_name;
            $rc_hash{$py_name} = $py_value;
        }
    }
    close $py_fd;
    return \%rc_hash;
}

sub parse_out_py_const() {

    # Return a reference like this:
    #       {
    #           $py_name => $value,
    #       }
    my ( $ref_sub_files, $ref_classes ) =
      _parse_py_init_file("$PYTHON_LIB_DIR/__init__.py");

    my $ref_py_name_2_value = {};
    foreach my $cur_py_file (@{$ref_sub_files}){
        my $ref_tmp = _get_py_class_consts($cur_py_file, $ref_classes);
        foreach my $key_name (keys %{$ref_tmp}){
            $ref_py_name_2_value->{$key_name} = $ref_tmp->{$key_name};
        }
    }
    return $ref_py_name_2_value;
}

sub value_str_to_int($) {
    my $raw_value = shift;
    unless ( defined $raw_value ) {
        return undef;
    }
    if ( $raw_value =~ /^[0-9]+$/ ) {
        return $raw_value;
    }
    if ( $raw_value =~ /^0x[0-9]+$/ ) {
        return hex $raw_value;
    }
    if ( $raw_value =~ /^([0-9]+) +<< +([0-9]+)$/ ) {
        return $1 << $2;
    }
    die "FAIL: Failed to convert $raw_value to integer\n";
}

sub record_result($$$$) {

    # Take ($py_name, $py_value, $c_name, $c_value)
    # Update $REF_RESULT
    my $py_name       = shift;
    my $py_value      = shift;
    my $c_name        = shift;
    my $c_value       = shift;
    my $real_py_value = undef;
    my $real_c_value  = undef;

    if ( ( defined $py_name ) && ( defined $py_value ) ) {
        $real_py_value = value_str_to_int($py_value);
        $REF_RESULT->{'py_const_hash'}->{$py_name} = sprintf "%s(%s)",
          $py_value, $real_py_value;
    }
    if ( ( defined $c_name ) && ( defined $c_value ) ) {
        $real_c_value = value_str_to_int($c_value);
        $REF_RESULT->{'c_const_hash'}->{$c_name} = sprintf "%s(%s)", $c_value,
          $real_c_value;
    }

    unless ($py_name) {
        my $known_py_name = $REF_RESULT->{'known_c_to_py_name'}->{$c_name};
        return 1 if $known_py_name;    # Already checked.
        $REF_RESULT->{'py_missing'}->{$c_name} = 'unknown';
        return 1;
    }

    unless ($c_name) {

        # ilegal python variable name, result already updated by
        # py_name_2_c_name()
        return 1;
    }

    $REF_RESULT->{'known_c_to_py_name'}->{$c_name}  = $py_name;
    $REF_RESULT->{'known_py_to_c_name'}->{$py_name} = $c_name;

    unless ( defined $py_value ) {

        # value for py_value will never be undef, just in case.
        $REF_RESULT->{'py_missing'}->{$c_name} = $py_value;
        return 1;
    }

    unless ( defined $c_value ) {
        $REF_RESULT->{'c_missing'}->{$py_name} = $c_name;
        return 1;
    }
    if ( $real_py_value == $real_c_value ) {
        $REF_RESULT->{'pass'}->{$py_name} = 1;
    }
    else {
        $REF_RESULT->{'fail'}->{$py_name} = 1;
    }
    1;
}

sub show_result() {
    my $format               = "%-10s%-60s %s\n";
    my @pass_py_names        = sort keys %{ $REF_RESULT->{'pass'} };
    my @fail_py_names        = sort keys %{ $REF_RESULT->{'fail'} };
    my @py_missing_c_names   = sort keys %{ $REF_RESULT->{'py_missing'} };
    my @c_missing_py_names   = sort keys %{ $REF_RESULT->{'c_missing'} };
    my $ref_py_name_2_c_name = $REF_RESULT->{'known_py_to_c_name'};
    my $ref_py_name_2_value  = $REF_RESULT->{'py_const_hash'};
    my $ref_c_name_2_value   = $REF_RESULT->{'c_const_hash'};

    # Header
    printf $format, '#'x8, 'Name', 'Value';
    print "\n";
    foreach my $py_name (@pass_py_names) {
        my $py_value = $ref_py_name_2_value->{$py_name};
        my $c_name   = $ref_py_name_2_c_name->{$py_name};
        my $c_value  = $ref_c_name_2_value->{$c_name};
        printf ($format, "PASS", $py_name, $py_value);
        printf ($format, "    ", $c_name, $c_value);
    }
    foreach my $c_name (@py_missing_c_names) {
        my $py_name  = '-' x 8;
        my $py_value = '-' x 8;
        my $c_value  = $ref_c_name_2_value->{$c_name};
        printf ($format, "PY_MISS", $c_name, $c_value);
    }
    foreach my $py_name (@c_missing_py_names) {
        my $c_name   = '-' x 8;
        my $c_value  = '-' x 8;
        my $py_value = $ref_py_name_2_value->{$py_name};
        printf ($format, "C_MISS", $py_name, $py_value);
    }
    foreach my $py_name (@fail_py_names) {
        my $py_value = $ref_py_name_2_value->{$py_name};
        my $c_name   = $ref_py_name_2_c_name->{$py_name};
        my $c_value  = $ref_c_name_2_value->{$c_name};
        printf ($format, "FAIL", $py_name, $py_value);
        printf ($format, "    ", $c_name, $c_value);
    }
    1;
}

sub main() {
    my $ref_py_const_hash = parse_out_py_const();
    my $ref_c_const_hash  = parse_out_c_const();
    map {
        my $py_name = $_;
        my $c_name  = py_name_2_c_name($py_name);
        record_result(
            $py_name, $ref_py_const_hash->{$py_name},
            $c_name,  $ref_c_const_hash->{$c_name}
          )
    } keys %{$ref_py_const_hash};

    map {
        my $c_name = $_;

        # We don't have a way to convert C constant name to python one.
        # We just treat all C constant as missing if not marked by previous
        # check.
        record_result( undef, undef, $c_name, $ref_c_const_hash->{$c_name} )
    } keys %{$ref_c_const_hash};
    show_result();
    exit 1
      if ( %{ $REF_RESULT->{'fail'} }
        || %{ $REF_RESULT->{'c_missing'} }
        || %{ $REF_RESULT->{'py_missing'} } );
    exit 0;
}

main();
