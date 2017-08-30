#!/bin/bash

# # To debug, try:
# docker pull fedora
# docker run --privileged --rm=false --tty=true --interactive=true \
#    -v `pwd`:/libstoragemgmt-code:rw fedora \
#    /bin/bash -c /libstoragemgmt-code/test/docker_travis_test.sh
if [ "CHK$(rpm -E %{?fedora})" != "CHK" ];then
    IS_FEDORA=1
elif [ "CHK$(rpm -E %{?el7})" != "CHK" ];then
    IS_RHEL=1
fi

cd /libstoragemgmt-code

getent group libstoragemgmt >/dev/null || \
    groupadd -r libstoragemgmt || exit 1
getent passwd libstoragemgmt >/dev/null || \
    useradd -r -g libstoragemgmt -d /var/run/lsm \
    -s /sbin/nologin \
    -c "daemon account for libstoragemgmt" libstoragemgmt || exit 1

if [ "CHK$IS_FEDORA" == "CHK1" ];then
    dnf install `cat ./rh_rpm_dependency` rpm-build -y || exit 1
    dnf install python3-six python3-devel python3-pywbem python3-pyudev -y \
        || exit 1
elif [ "CHK$IS_RHEL" == "CHK1" ];then
    yum install `cat ./rh_rpm_dependency` rpm-build -y || exit 1
else
    echo "Not supported yet";
    exit 1;
fi

./autogen.sh || exit 1
if [ "CHK$IS_FEDORA" == "CHK1" ];then
    ./configure --with-python3 || exit 1
else
    ./configure || exit 1
fi

make || exit 1

make check || { cat test-suite.log; exit 1; }

if [ "CHK$IS_FEDORA" == "CHK1" ];then
make rpm || exit 1
fi
