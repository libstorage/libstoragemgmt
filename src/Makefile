CC = gcc
INCLUDE = -I../include -I/usr/include/libxml2
LIBS = -lxml2 -ldl
CFLAGS = -g -Wall -fPIC -shared

libstoragemgmt.so: lsm_mgmt.o lsm_datatypes.o
	$(CC) $(INCLUDE) $(CFLAGS) $(LIBS)  lsm_mgmt.o lsm_datatypes.o -o libstoragemgmt.so

lsm_mgmt.o:	lsm_mgmt.c
	$(CC) $(INCLUDE) $(CFLAGS) -c lsm_mgmt.c

lsm_datatypes.o:	lsm_datatypes.c
	$(CC) $(INCLUDE) $(CFLAGS) -c lsm_datatypes.c 

clean:
	rm -f *.so *.o
