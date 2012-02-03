INCLUDES = -I../include -I/usr/include/libxml2
CFLAGS = -g -Wall 
LIBS = -lxml2 -ldl -lpegcommon -lpegclient -lstoragemgmt
LIBDIR = -L../src

ARCH = $(shell uname -m)
LIBINC = $(shell ls ../include/libstoragemgmt/*)

ifeq	($(ARCH), x86_64)
	DEFS = -DPEGASUS_PLATFORM_LINUX_X86_64_GNU
else
	DEFS = -DPEGASUS_PLATFORM_LINUX_IX86_GNU
endif

smis_lsmplugin:	lsm_plugin_smis.cpp smis.o qparams.o misc.o  $(LIBINC)
	$(CXX) $(DEFS) $(INCLUDES) $(CFLAGS) $(LIBDIR) $(LIBS) lsm_plugin_smis.cpp smis.o qparams.o misc.o -o smis_lsmplugin

smis.o:	smis.cpp smis.h $(LIBINC)
	$(CXX) $(DEFS) $(INCLUDES) $(CFLAGS) -c smis.cpp

qparams.o: util/qparams.c util/qparams.h
	$(CXX) $(INCLUDES) $(CFLAGS) -c util/qparams.c

misc.o:	util/misc.cpp util/misc.h util/qparams.h
	$(CXX) $(INCLUDES) $(CFLAGS) -c util/misc.cpp

clean:
	rm -f *.o smis.lsmplugin
