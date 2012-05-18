INCLUDES = -I$(top_srcdir)/include -I@srcdir@/include


noinst_PROGRAMS = tester
tester_CFLAGS = $(LIBCHECK_CFLAGS)
tester_LDADD = ../src/libstoragemgmt.la $(LIBCHECK_LIBS)
tester_SOURCES = tester.c
