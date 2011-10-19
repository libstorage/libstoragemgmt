CC = g++
INCLUDES = -I../include -I/usr/include/libxml2
CFLAGS = -g -Wall -fPIC -shared 
LIBS = -lxml2 -ldl -lpegcommon -lpegclient

ARCH = $(shell uname -m)
LIBINC = $(shell ls ../include/libstoragemgmt/*)

ifeq	($(ARCH), x86_64)
	DEFS = -DPEGASUS_PLATFORM_LINUX_X86_64_GNU
else
	DEFS = -DPEGASUS_PLATFORM_LINUX_IX86_GNU
endif

lsm_plugin_smis.so:	lsm_plugin_smis.cpp smis.o qparams.o misc.o $(LIBINC)
	$(CC) $(DEFS) $(INCLUDES) $(CFLAGS) $(LIBS) lsm_plugin_smis.cpp smis.o qparams.o misc.o -o lsm_plugin_smis.so

smis.o:	smis.cpp smis.h $(LIBINC)
	$(CC) $(DEFS) $(INCLUDES) $(CFLAGS) -c smis.cpp

qparams.o: util/qparams.c util/qparams.h
	$(CC) $(INCLUDES) $(CFLAGS) -c util/qparams.c

misc.o:	util/misc.cpp util/misc.h util/qparams.h
	$(CC) $(INCLUDES) $(CFLAGS) -c util/misc.cpp

clean:
	rm -f *.o *.so
