Name:           libstoragemgmt
Version:        0.0.4
Release:        1%{?dist}
Summary:        A library for storage array management
Group:          System Environment/Libraries
License:        LGPLv2+
URL:            http://sourceforge.net/projects/libstoragemgmt/ 
Source0:        http://sourceforge.net/projects/libstoragemgmt/files/Alpha/libstoragemgmt-0.0.4.tar.gz
BuildRoot:      %{_tmppath}/%{name}-%{version}-%{release}-root-%(%{__id_u} -n)

BuildRequires:  boost-devel yajl-devel libxml2-devel tog-pegasus-devel python2-devel pywbem
Requires:       pywbem initscripts
Requires(post): systemd-units
Requires(preun): systemd-units
Requires(postun): systemd-units

%description
The libStorageMgmt library will provide a vendor agnostic open source storage
application programming interface (API) that will allow management of storage 
arrays. 

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
rm -rf $RPM_BUILD_ROOT
make install DESTDIR=$RPM_BUILD_ROOT
find $RPM_BUILD_ROOT -name '*.la' -exec rm -f {} ';'

install -d -m755 $RPM_BUILD_ROOT/%{_unitdir}
install -m644 packaging/daemon/libstoragemgmt.service $RPM_BUILD_ROOT/%{_unitdir}/libstoragemgmt.service

#tempfiles.d configuration for /var/run
mkdir -p %{buildroot}%{_sysconfdir}/tmpfiles.d
install -m 0644 packaging/daemon/lsm-tmpfiles.conf %{buildroot}%{_sysconfdir}/tmpfiles.d/%{name}.conf

#Need these to exist at install so we can start the daemon
mkdir -p %{buildroot}%{_localstatedir}/run/lsm/ipc

%clean
rm -rf $RPM_BUILD_ROOT

%pre
getent group libstoragemgmt >/dev/null || groupadd -r libstoragemgmt
getent passwd libstoragemgmt >/dev/null || \
    useradd -r -g libstoragemgmt -d /var/run/lsm -s /sbin/nologin \
    -c "daemon account for libstoragemgmt" libstoragemgmt

%post 
/sbin/ldconfig
if [ $1 -eq 1 ]; then
    /bin/systemctl enable libstoragemgmt.service >/dev/null 2>&1 || :
    /bin/systemctl start libstoragemgmt.service >/dev/null 2>&1 || :
fi

%preun
if [ $1 -eq 0 ]; then
        # On uninstall (not upgrade), disable and stop the units
        /bin/systemctl --no-reload disable libstoragemgmt.service >/dev/null 2>&1 || :
        /bin/systemctl stop libstoragemgmt.service >/dev/null 2>&1 || :
fi

%postun
/sbin/ldconfig
/bin/systemctl daemon-reload >/dev/null 2>&1 || :
if [ $1 -ge 1 ] ; then
        # On upgrade (not uninstall), optionally, restart the daemon
        /bin/systemctl try-restart libstoragemgmt.service >/dev/null 2>&1 || :
fi

%files
%defattr(-,root,root,-)
%doc README COPYING.LIB
%{_libdir}/*.so.*
%{_bindir}/lsmcli
%{_bindir}/lsmd
%{_bindir}/sim_lsmplugin
%{_bindir}/smis_lsmplugin
%{_bindir}/ontap_lsmplugin
%{_bindir}/smispy_lsmplugin

#Python library files
%{python_sitelib}/lsm/__init__.py
%{python_sitelib}/lsm/__init__.pyc
%{python_sitelib}/lsm/__init__.pyo
%{python_sitelib}/lsm/client.py
%{python_sitelib}/lsm/client.pyc
%{python_sitelib}/lsm/client.pyo
%{python_sitelib}/lsm/cmdline.py
%{python_sitelib}/lsm/cmdline.pyc
%{python_sitelib}/lsm/cmdline.pyo
%{python_sitelib}/lsm/common.py
%{python_sitelib}/lsm/common.pyc
%{python_sitelib}/lsm/common.pyo
%{python_sitelib}/lsm/data.py
%{python_sitelib}/lsm/data.pyc
%{python_sitelib}/lsm/data.pyo
%{python_sitelib}/lsm/external/__init__.py
%{python_sitelib}/lsm/external/__init__.pyc
%{python_sitelib}/lsm/external/__init__.pyo
%{python_sitelib}/lsm/external/daemon.py
%{python_sitelib}/lsm/external/daemon.pyc
%{python_sitelib}/lsm/external/daemon.pyo
%{python_sitelib}/lsm/external/enumeration.py
%{python_sitelib}/lsm/external/enumeration.pyc
%{python_sitelib}/lsm/external/enumeration.pyo
%{python_sitelib}/lsm/external/xmltodict.py
%{python_sitelib}/lsm/external/xmltodict.pyc
%{python_sitelib}/lsm/external/xmltodict.pyo
%{python_sitelib}/lsm/iplugin.py
%{python_sitelib}/lsm/iplugin.pyc
%{python_sitelib}/lsm/iplugin.pyo
%{python_sitelib}/lsm/na.py
%{python_sitelib}/lsm/na.pyc
%{python_sitelib}/lsm/na.pyo
%{python_sitelib}/lsm/pluginrunner.py
%{python_sitelib}/lsm/pluginrunner.pyc
%{python_sitelib}/lsm/pluginrunner.pyo
%{python_sitelib}/lsm/transport.py
%{python_sitelib}/lsm/transport.pyc
%{python_sitelib}/lsm/transport.pyo

%{_unitdir}/libstoragemgmt.service

%dir %attr(0755, libstoragemgmt, libstoragemgmt) %{_localstatedir}/run/lsm/
%dir %attr(0755, libstoragemgmt, libstoragemgmt) %{_localstatedir}/run/lsm/ipc
%config(noreplace) %{_sysconfdir}/tmpfiles.d/%{name}.conf

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
