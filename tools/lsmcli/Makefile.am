INCLUDES = -I$(top_srcdir)/include -I@srcdir@/include

noinst_PROGRAMS = lsmcli
#bin_PROGRAMS = lsmcli
lsmcli_LDADD = ../../src/libstoragemgmt.la 
lsmcli_SOURCES = lsmcli.cpp \
                 arguments.h arguments.cpp \
                 lsmcli_func.h lsmcli_func.cpp
                 
