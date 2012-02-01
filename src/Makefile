INCLUDE = -I../include -I/usr/include/libxml2
LIBS = -lxml2 -ldl -lyajl
CFLAGS = -g -Wall -fPIC -shared

libstoragemgmt.so: lsm_mgmt.o lsm_datatypes.o lsm_ipc.o lsm_convert.o lsm_plugin_ipc.o
	$(CXX) $(INCLUDE) $(CFLAGS) $(LIBS)  lsm_ipc.o lsm_convert.o lsm_mgmt.o lsm_datatypes.o lsm_plugin_ipc.o -o libstoragemgmt.so

lsm_mgmt.o:	lsm_mgmt.cpp
	$(CXX) $(INCLUDE) $(CFLAGS) -c lsm_mgmt.cpp

lsm_datatypes.o:	lsm_datatypes.cpp
	$(CXX) $(INCLUDE) $(CFLAGS) -c lsm_datatypes.cpp

lsm_ipc.o:	lsm_ipc.cpp
	$(CXX)  $(INCLUDE) $(CFLAGS) -c lsm_ipc.cpp

lsm_convert.o:	lsm_convert.cpp
	$(CXX) $(INCLUDE) $(CFLAGS) -c lsm_convert.cpp

lsm_plugin_ipc.o: lsm_plugin_ipc.cpp
	$(CXX) $(INCLUDE) $(CFLAGS) -c lsm_plugin_ipc.cpp

clean:
	rm -f *.so *.o
