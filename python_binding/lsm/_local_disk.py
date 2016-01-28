# Copyright (C) 2016 Red Hat, Inc.
# This library is free software; you can redistribute it and/or
# modify it under the terms of the GNU Lesser General Public
# License as published by the Free Software Foundation; either
# version 2.1 of the License, or any later version.
#
# This library is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
# Lesser General Public License for more details.
#
# You should have received a copy of the GNU Lesser General Public
# License along with this library; If not, see <http://www.gnu.org/licenses/>.
#
# Author: Gris Ge <fge@redhat.com>

from lsm._clib import (_local_disk_vpd83_search, _local_disk_vpd83_get,
                       _local_disk_rpm_get)
from lsm import LsmError, ErrorNumber


def _use_c_lib_function(func_ref, arg):
    (data, err_no, err_msg) = func_ref(arg)
    if err_no != ErrorNumber.OK:
        raise LsmError(err_no, err_msg)
    return data


class LocalDisk(object):

    @staticmethod
    def vpd83_search(vpd83):
        """
        lsm.LocalDisk.vpd83_search(vpd83)

        Version:
            1.3
        Usage:
            Find out the disk paths for given SCSI VPD page 0x83 NAA type
            ID. Considering multipath, certain VPD83 might have multiple disks
            associated.
        Parameters:
            vpd83 (string)
                The VPD83 NAA type ID.
        Returns:
            [disk_path]
                List of string. Empty list if not disk found.
                The disk_path string format is '/dev/sd[a-z]+' for SCSI and
                ATA disks.
        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_vpd83_search, vpd83)

    @staticmethod
    def vpd83_get(disk_path):
        """
        lsm.LocalDisk.vpd83_get(disk_path)

        Version:
            1.3
        Usage:
            Query the SCSI VPD83 NAA ID of given disk path.
        Parameters:
            disk_path (string)
                The disk path, example '/dev/sdb'.
        Returns:
            vpd83 (string)
                String of VPD83 NAA ID. Empty string if not supported.
                The string format regex is:
                    (?:^6[0-9a-f]{31})|(?:^[235][0-9a-f]{15})$
        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
                ErrorNumber.INVALID_ARGUMENT
                    Invalid disk_path. Should be like '/dev/sdb'
                ErrorNumber.NOT_FOUND_DISK
                    Provided disk is not found.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_vpd83_get, disk_path)

    @staticmethod
    def rpm_get(disk_path):
        """
        Version:
            1.3
        Usage:
            Query the disk rotation speed - revolutions per minute (RPM) of
            given disk path.
            Require permission to open disk path as read-only and non-block,
            normally it's root or disk group privilege.
        Parameters:
            disk_path (string)
                The disk path, example '/dev/sdb', '/dev/nvme0n1'.
        Returns:
            rpm (integer)
                Disk rotation speed:
                    -1 (lsm.Disk.RPM_UNKNOWN):
                        Unknown RPM
                     0 (lsm.Disk.RPM_NON_ROTATING_MEDIUM):
                        Non-rotating medium (e.g., SSD)
                     1 (lsm.Disk.RPM_ROTATING_UNKNOWN_SPEED):
                        Rotational disk with unknown speed
                    >1:
                        Normal rotational disk (e.g., HDD)

        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
                ErrorNumber.INVALID_ARGUMENT
                    Invalid disk_path. Should be like '/dev/sdb'.
                ErrorNumber.NOT_FOUND_DISK
                    Provided disk is not found.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_rpm_get, disk_path)
