## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

DISTCHECK_CONFIGURE_FLAGS = --with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)

SUBDIRS= c_binding python_binding plugin doc tools daemon packaging

if BUILD_C_UNIT
SUBDIRS += test
endif

EXTRA_DIST = \
	libstoragemgmt.pc.in \
	libstoragemgmt.pc \
	autogen.sh

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libstoragemgmt.pc

#Source code documentation
docs:
	doxygen doc/doxygen.conf

rpm: clean
	@(unset CDPATH ; $(MAKE) dist && rpmbuild -ta $(distdir).tar.gz)

MAINTAINERCLEANFILES = .git-module-status
