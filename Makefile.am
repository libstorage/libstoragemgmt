## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

DISTCHECK_CONFIGURE_FLAGS = --with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)

SUBDIRS= src plugin doc include tools lsm packaging

if BUILD_C_UNIT
SUBDIRS += test
endif

EXTRA_DIST = \
	libstoragemgmt.pc.in \
	libstoragemgmt.pc \
	libstoragemgmt.spec.in \
	libstoragemgmt.spec

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libstoragemgmt.pc

#Source code documentation
docs:
	doxygen doc/doxygen.conf

rpm: clean
	@(unset CDPATH ; $(MAKE) dist && rpmbuild -ta $(distdir).tar.gz)

MAINTAINERCLEANFILES = .git-module-status
