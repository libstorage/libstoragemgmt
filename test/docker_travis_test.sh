#!/bin/bash

# # To debug, try:
# docker pull fedora
# docker run --privileged --rm=false --tty=true --interactive=true \
#    -v `pwd`:/libstoragemgmt-code:rw fedora \
#    /bin/bash -c /libstoragemgmt-code/test/docker_travis_test.sh
if [ -e "/etc/debian_version" ];then
    IS_DEB=1
elif [ "CHK$(rpm -E "%{?fedora}")" != "CHK" ];then
    IS_FEDORA=1
elif [ "CHK$(rpm -E "%{?el8}")" != "CHK" ];then
    IS_FEDORA=1
    IS_RHEL8=1
elif [ "CHK$(rpm -E "%{?el7}")" != "CHK" ];then
    IS_RHEL=1
elif [ "CHK$(rpm -E "%{?el6}")" != "CHK" ];then
    IS_RHEL=1
    IS_RHEL6=1
else
    echo "Unsupported distribution"
    exit 1;
fi

if [ "CHK$IS_DEB" == "CHK1" ] ;then
    cp -a /libstoragemgmt-code /tmp/ || exit 1
    cd /tmp/libstoragemgmt-code || exit 1
else
    cd /libstoragemgmt-code || exit 1
fi

getent group libstoragemgmt >/dev/null || \
    groupadd -r libstoragemgmt || exit 1
getent passwd libstoragemgmt >/dev/null || \
    useradd -r -g libstoragemgmt -d /var/run/lsm \
    -s /sbin/nologin \
    -c "daemon account for libstoragemgmt" libstoragemgmt || exit 1


# libconfig-devel is located in "PowerTools", add the plugin-core
# to allow enabling.
if [ "CHK$IS_RHEL8" == "CHK1" ];then
    dnf install dnf-plugins-core -y || exit 1
    dnf config-manager --set-enabled PowerTools -y || exit 1
fi


if [ "CHK$IS_FEDORA" == "CHK1" ];then
    # shellcheck disable=SC2046
    dnf install $(cat ./rh_py3_rpm_dependency) rpm-build -y || exit 1
elif [ "CHK$IS_RHEL" == "CHK1" ];then
    # shellcheck disable=SC2046
    yum install $(cat ./rh_py2_rpm_dependency) rpm-build -y || exit 1
elif [ "CHK$IS_DEB" = "CHK1" ];then
    export DEBIAN_FRONTEND="noninteractive"
    apt-get update
    apt-get install -y tzdata
    ln -fs /usr/share/zoneinfo/GMT /etc/localtime
    dpkg-reconfigure --frontend noninteractive tzdata
    # shellcheck disable=SC2046
    apt-get install $(cat ./deb_dependency) -y -q || exit 1
else
    echo "Not supported yet";
    exit 1;
fi

if [ "CHK$IS_RHEL6" == "CHK1" ];then
    yum install python-argparse -y || exit 1
fi

./autogen.sh || exit 1

# Configure is almost doing the "right thing" by default in most cases,
# but not for all.
if [ "CHK$IS_DEB" = "CHK1" ];then
    ./configure --with-python2 || exit 1
else
    ./configure || exit 1
fi

make || exit 1
make check || { cat test-suite.log; exit 1; }

if [ "CHK$IS_FEDORA" == "CHK1" ];then
    make rpm || exit 1
fi

if [ "CHK$IS_DEB" == "CHK1" ];then
    make deb || exit 1
fi
