#!/bin/bash

#Clean stuff up to ensure a clean autobuild
rm -rf autom4te.cache/*
rm -rf build-aux/*
rm -f m4/l*
rm -f aclocal.m4

autoreconf -f -i
