all:	libstoragemgmt.so lib_plugin_smis.so lsmcli tester

libstoragemgmt.so:
	(cd src; $(MAKE))

lib_plugin_smis.so:
	(cd plugin; $(MAKE))

lsmcli:
	(cd tools/lsmcli; $(MAKE))

tester:
	(cd test; $(MAKE))

clean:
	(cd src; $(MAKE) clean)
	(cd plugin; $(MAKE) clean)
	(cd tools/lsmcli; $(MAKE) clean)
	(cd test; $(MAKE) clean)
	rm -rf doc/srcdoc

#Source code documentation
docs:
	doxygen doc/doxygen.conf
