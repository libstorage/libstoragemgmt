CC = g++
INCLUDE = -I../../include/
LIBS = -lboost_regex -lstoragemgmt
LIBDIR = -L../../src/
CFLAGS = -g -Wall

LIBINC = $(shell ls ../../include/libstoragemgmt/*)

lsmcli:	lsmcli.cpp lsmcli_func.o arguments.o $(LIBINC)
	$(CC) $(INCLUDE) $(LIBDIR) $(CFLAGS) $(LIBS) lsmcli_func.o arguments.o lsmcli.cpp -o lsmcli

arguments.o:	arguments.h arguments.cpp
	$(CC) $(INCLUDE) $(CFLAGS) -c arguments.cpp

lsmcli_func.o: lsmcli_func.cpp lsmcli_func.h
	$(CC) $(INCLUDE) $(CFLAGS) -c lsmcli_func.cpp

clean:
	rm -f lsmcli
	rm -f *.o
