## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

AM_DISTCHECK_CONFIGURE_FLAGS = \
	--with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)
AM_DISTCHECK_CONFIGURE_FLAGS += \
	--with-bash-completion-dir=$$dc_install_base/$(bashcompletiondir)

SUBDIRS= c_binding python_binding plugin doc tools daemon packaging config test

EXTRA_DIST = \
	libstoragemgmt.pc.in \
	libstoragemgmt.pc \
	autogen.sh
	test/runtest.sh

pkgconfigdir = $(libdir)/pkgconfig
pkgconfig_DATA = libstoragemgmt.pc

#Source code documentation
docs:
	doxygen doc/doxygen.conf

rpm: clean
	@(unset CDPATH ; $(MAKE) dist && rpmbuild -ta $(distdir).tar.gz)

if WITH_TEST
TESTS = test/runtests.sh
endif

MAINTAINERCLEANFILES = .git-module-status
