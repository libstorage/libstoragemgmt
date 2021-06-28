#!/bin/bash

#Clean stuff up to ensure a clean autobuild
rm -rf autom4te.cache/* || exit 1
rm -rf build-aux/* || exit 1
rm -f m4/l* || exit 1
rm -f aclocal.m4 || exit 1

echo "running autoreconf ..."
autoreconf -v --warnings=all -f -i || exit 1
exit 0
