## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

SUBDIRS= src plugin doc include tools lsm

#EXTRA_DIST = \
#  autogen.sh


#Source code documentation
docs:
	doxygen doc/doxygen.conf

MAINTAINERCLEANFILES = .git-module-status
