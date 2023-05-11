#!/usr/bin/perl
#
# SPDX-License-Identifier: GPL-3.0-or-later
#
# Copyright (C) 2016 Red Hat, Inc.
#
# Author: Gris Ge <fge@redhat.com>

use strict;

my @REMOVE_KEY_LIST=("LSM_DLL_EXPORT");

while (<>) {
    for my $key (@REMOVE_KEY_LIST) {
        (s/$key//g);
    }
    print;
}
