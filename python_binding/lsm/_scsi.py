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

from lsm._scsi_clib import (_vpd83_of_disk_path, _disk_paths_of_vpd83)
from lsm import LsmError, ErrorNumber


def _use_c_lib_function(func_ref, arg):
    (data, err_no, err_msg) = func_ref(arg)
    if err_no != ErrorNumber.OK:
        raise LsmError(err_no, err_msg)
    return data


class SCSI(object):
    @staticmethod
    def disk_paths_of_vpd83(vpd83):
        """
        lsm.SCSI.disk_paths_of_vpd83(vpd83)

        Version:
            1.3
        Usage:
            Find out the /dev/sdX paths for given SCSI VPD page 0x83 NAA type
            ID. Considering multipath, certain VPD83 might have multiple disks
            associated.
        Parameters:
            vpd83 (string)
                The VPD83 NAA type ID.
        Returns:
            [disk_path]
                List of string. Empty list if not disk found.
                The disk_path string format is '/dev/sd[a-z]+'.
        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_disk_paths_of_vpd83, vpd83)

    @staticmethod
    def vpd83_of_disk_path(disk_path):
        """
        lsm.SCSI.vpd83_of_disk_path(disk_path)

        Version:
            1.3
        Usage:
            Query the SCSI VPD83 NAA ID of given scsi disk path.
        Parameters:
            sd_path (string)
                The SCSI disk path, example '/dev/sdb'.
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
        return _use_c_lib_function(_vpd83_of_disk_path, disk_path)
