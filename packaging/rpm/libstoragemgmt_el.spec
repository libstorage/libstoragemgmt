Name:           libstoragemgmt
Version:        0.0.5
Release:        1%{?dist}
Summary:        A library for storage array management
Group:          System Environment/Libraries
License:        LGPLv2+
URL:            http://sourceforge.net/projects/libstoragemgmt/ 
Source0:        http://sourceforge.net/projects/libstoragemgmt/files/Alpha/libstoragemgmt-0.0.5.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  boost-devel yajl-devel libxml2-devel tog-pegasus-devel python2-devel pywbem
Requires:       pywbem

%description
The libStorageMgmt library will provide a vendor agnostic open source storage
application programming interface (API) that will allow management of storage 
arrays.  The library includes a command line interface for interactive use and
scripting (command lsmcli).  The library also has a daemon that is used for
executing plug-ins in a separate process (lsmd).

%package        devel
Summary:        Development files for %{name}
Group:          Development/Libraries
Requires:       %{name} = %{version}-%{release}

%description    devel
The %{name}-devel package contains libraries and header files for
developing applications that use %{name}.


%prep
%setup -q

%build
%configure --disable-static
make %{?_smp_mflags}


%install
rm -rf %{buildroot}
make install DESTDIR=%{buildroot}
find %{buildroot} -name '*.la' -exec rm -f {} ';'

#Need these to exist at install so we can start the daemon
mkdir -p %{buildroot}/etc/rc.d/init.d
install packaging/daemon/libstoragemgmtd %{buildroot}/etc/rc.d/init.d/libstoragemgmtd

#Need these to exist at install so we can start the daemon
mkdir -p %{buildroot}%{_localstatedir}/run/lsm/ipc

%clean
rm -rf %{buildroot}

%pre
getent group libstoragemgmt >/dev/null || groupadd -r libstoragemgmt
getent passwd libstoragemgmt >/dev/null || \
    useradd -r -g libstoragemgmt -d /var/run/lsm -s /sbin/nologin \
    -c "daemon account for libstoragemgmt" libstoragemgmt

%post 
/sbin/ldconfig
if [ $1 -eq 1 ]; then
    /sbin/chkconfig --add libstoragemgmtd
    /etc/rc.d/init.d/libstoragemgmtd start > /dev/null 2>&1 || :
fi

%preun
if [ $1 -eq 0 ]; then
    /etc/rc.d/init.d/libstoragemgmtd stop > /dev/null 2>&1 || :
    /sbin/chkconfig --del libstoragemgmtd
fi

%postun
/sbin/ldconfig
/bin/systemctl daemon-reload >/dev/null 2>&1 || :
if [ $1 -ge 1 ] ; then
    #Restart the daemond
    /etc/rc.d/init.d/libstoragemgmtd restart  >/dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%doc README COPYING.LIB
%{_libdir}/*.so.*
%{_bindir}/*

#Python library files
%{python_sitelib}


%dir %attr(0755, libstoragemgmt, libstoragemgmt) %{_localstatedir}/run/lsm/
%dir %attr(0755, libstoragemgmt, libstoragemgmt) %{_localstatedir}/run/lsm/ipc
%attr(0755, root, root) /etc/rc.d/init.d/libstoragemgmtd

%files devel
%defattr(-,root,root,-)
%doc README COPYING.LIB
%{_includedir}/*
%{_libdir}/*.so


%changelog
* Mon Mar 26 2012 Tony Asleson <tasleson@redhat.com> 0.0.4-1
- Restore from snapshot
- Job identifiers string instead of integer
- Updated license address

* Wed Mar 14 2012 Tony Asleson <tasleson@redhat.com> 0.0.3-1
- Changes to installer, daemon uid, gid, /var/run/lsm/*
- NFS improvements and bug fixes
- Python library clean up (rpmlint errors)

* Sun Mar 11 2012 Tony Asleson <tasleson@redhat.com> 0.0.2-1
- Added NetApp native plugin

* Mon Feb 6 2012 Tony Asleson <tasleson@redhat.com>  0.0.1alpha-1
- Initial version of package
