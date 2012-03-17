## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

DISTCHECK_CONFIGURE_FLAGS = --with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)

SUBDIRS= src plugin doc include tools lsm packaging

#EXTRA_DIST = \
#  autogen.sh


#Source code documentation
docs:
	doxygen doc/doxygen.conf

MAINTAINERCLEANFILES = .git-module-status
