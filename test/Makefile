CC = gcc
INCLUDE = -I../include 
LIBDIR = -L../src
LIBS = -lstoragemgmt -lcheck
CFLAGS = -g -Wall 

testcase:	tester.o
	$(CC) $(INCLUDE) $(CFLAGS) $(LIBDIR) $(LIBS) tester.o -o tester


tester.o:	tester.c
	$(CC) $(INCLUDE) $(CFLAGS) -c tester.c

clean:
	rm -f *.o testcase
