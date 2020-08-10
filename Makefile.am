## Process this file with automake to produce Makefile.in

ACLOCAL_AMFLAGS = -I m4

AM_DISTCHECK_CONFIGURE_FLAGS = \
	--with-systemdsystemunitdir=$$dc_install_base/$(systemdsystemunitdir)
AM_DISTCHECK_CONFIGURE_FLAGS += \
	--with-bash-completion-dir=$$dc_install_base/$(bashcompletiondir)
# The default for systemdtmpfilesdir is intentionally not
# prefix-sensitive, but we need to adjust it in order for distcheck to pass.
AM_DISTCHECK_CONFIGURE_FLAGS += \
	--with-systemd-tmpfilesdir=$${dc_install_base}/$(systemdtmpfilesdir)

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

deb: clean
	debuild -i -us -uc -b && ls $(top_srcdir)/../*.deb

# Use clang-format for C/C++ code
cformat:
	find . -type f \( -name \*.h -o -name \*.c -o -name \*.hpp -o -name \*.cpp \) -exec clang-format -i {} \;

if WITH_TEST
TESTS = test/runtests.sh
endif

MAINTAINERCLEANFILES = .git-module-status
