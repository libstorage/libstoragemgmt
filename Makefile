all:	libstoragemgmt.so lib_plugin_smis.so

libstoragemgmt.so:
	(cd src; $(MAKE))

lib_plugin_smis.so:
	(cd plugin; $(MAKE))

clean:
	(cd src; $(MAKE) clean)
	(cd plugin; $(MAKE) clean)
	rm -rf doc/srcdoc

#Source code documentation
docs:
	doxygen doc/doxygen.conf
