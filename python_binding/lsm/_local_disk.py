# Copyright (C) 2016 Red Hat, Inc.
# (C) Copyright 2017 Hewlett Packard Enterprise Development LP
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

import six

from lsm import LsmError, ErrorNumber

from lsm._clib import (_local_disk_vpd83_search, _local_disk_vpd83_get,
                       _local_disk_health_status_get,
                       _local_disk_rpm_get, _local_disk_list,
                       _local_disk_link_type_get, _local_disk_ident_led_on,
                       _local_disk_ident_led_off, _local_disk_fault_led_on,
                       _local_disk_fault_led_off, _local_disk_serial_num_get,
                       _local_disk_led_status_get, _local_disk_link_speed_get)


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
    def serial_num_get(disk_path):
        """
        lsm.LocalDisk.serial_num_get(disk_path)

        Version:
            1.4
        Usage:
            Query the SCSI VPD80 serial number of given disk path.
        Parameters:
            disk_path (string)
                The disk path, example '/dev/sdb'.
        Returns:
            serial_num (string)
                String of VPD80 serial number. Empty string if not supported.
                The string format regex is:
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
        return _use_c_lib_function(_local_disk_serial_num_get, disk_path)

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
    def health_status_get(disk_path):
        """
        lsm.LocalDisk.health_status_get(disk_path)

        Version:
            1.5
        Usage:
            Retrieve the health status of given disk path.
        Parameters:
            disk_path (string)
                The disk path, example '/dev/sdb'.
        Returns:
            health_status (integer)
                Disk health status:
                    -1 (lsm.Disk.HEALTH_STATUS_UNKNOWN):
                        Unknown health status
                     0 (lsm.Disk.HEALTH_STATUS_FAIL):
                        health status indicates failure
                     1 (lsm.Disk.HEALTH_STATUS_WARN):
                        health status warns of near failure
                     2 (lsm.Disk.HEALTH_STATUS_GOOD):
                        health status indicates good health
        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
                ErrorNumber.INVALID_ARGUMENT
                    Invalid disk_path. Should be like '/dev/sdb'
                ErrorNumber.NOT_FOUND_DISK
                    Provided disk is not found.
                ErrorNumber.NO_SUPPORT
                    Not supported.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_health_status_get, disk_path)

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
                ErrorNumber.NO_SUPPORT
                    Not supported.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_rpm_get, disk_path)

    @staticmethod
    def list():
        """
        Version:
            1.3
        Usage:
            Query local disk paths. Currently, only SCSI, ATA and NVMe disks
            will be included.
        Parameters:
            N/A
        Returns:
            [disk_path]
                List of string. Empty list if not disk found.
                The disk_path string format is '/dev/sd[a-z]+' for SCSI and
                ATA disks, '/dev/nvme[0-9]+n[0-9]+' for NVMe disks.
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
        (disk_paths, err_no, err_msg) = _local_disk_list()
        if err_no != ErrorNumber.OK:
            raise LsmError(err_no, err_msg)
        return disk_paths

    @staticmethod
    def link_type_get(disk_path):
        """
        Version:
            1.3
        Usage:
            Query the disk link type of given disk path.
            For SATA disks connected to SAS enclosure, will return
            lsm.Disk.LINK_TYPE_ATA.
            Require permission to open disk_path(root user or disk group).
        Parameters:
            disk_path (string)
                The disk path, example '/dev/sdb'.
        Returns:
            link_type (integer)
                Link type, possible values are:
                    lsm.Disk.LINK_TYPE_UNKNOWN
                        Failed to detect link type
                    lsm.Disk.LINK_TYPE_FC
                        Fibre Channel
                    lsm.Disk.LINK_TYPE_SSA
                        Serial Storage Architecture, Old IBM tech.
                    lsm.Disk.LINK_TYPE_SBP
                        Serial Bus Protocol, used by IEEE 1394.
                    lsm.Disk.LINK_TYPE_SRP
                        SCSI RDMA Protocol
                    lsm.Disk.LINK_TYPE_ISCSI
                        Internet Small Computer System Interface
                    lsm.Disk.LINK_TYPE_SAS
                        Serial Attached SCSI
                    lsm.Disk.LINK_TYPE_ADT
                        Automation/Drive Interface Transport
                        Protocol, often used by Tape.
                    lsm.Disk.LINK_TYPE_ATA
                        PATA/IDE or SATA.
                    lsm.Disk.LINK_TYPE_USB
                        USB disk.
                    lsm.Disk.LINK_TYPE_SOP
                        SCSI over PCI-E
                    lsm.Disk.LINK_TYPE_PCIE
                        PCI-E, e.g. NVMe
        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
                ErrorNumber.INVALID_ARGUMENT
                    Invalid disk_path. Should be like '/dev/sdb'.
                ErrorNumber.NOT_FOUND_DISK
                    Provided disk is not found.
                ErrorNumber.NO_SUPPORT
                    Provided disk does not support SCSI SPC.
                ErrorNumber.PERMISSION_DENIED
                    Insufficient permission to access provided disk path.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_link_type_get, disk_path)

    @staticmethod
    def ident_led_on(disk_path):
        """
        Version:
            1.3
        Usage:
            Turn on the identification LED for specified disk.
        Parameters:
            disk_path (string)
                The disk path, example '/dev/sdb'.
        Returns:
            None
        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
                ErrorNumber.NOT_FOUND_DISK
                    Provided disk is not found.
                ErrorNumber.NO_SUPPORT
                    Provided disk does not support SCSI SPC.
                ErrorNumber.PERMISSION_DENIED
                    No sufficient permission to access provided disk path.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_ident_led_on, disk_path)

    @staticmethod
    def ident_led_off(disk_path):
        """
        Version:
            1.3
        Usage:
            Turn off the identification LED for specified disk.
        Parameters:
            disk_path (string)
                The disk path, example '/dev/sdb'.
        Returns:
            None
        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
                ErrorNumber.NOT_FOUND_DISK
                    Provided disk is not found.
                ErrorNumber.NO_SUPPORT
                    Provided disk does not support SCSI SPC.
                ErrorNumber.PERMISSION_DENIED
                    No sufficient permission to access provided disk path.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_ident_led_off, disk_path)

    @staticmethod
    def fault_led_on(disk_path):
        """
        Version:
            1.3
        Usage:
            Turn on the fault LED for specified disk.
        Parameters:
            disk_path (string)
                The disk path, example '/dev/sdb'.
        Returns:
            None
        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
                ErrorNumber.NOT_FOUND_DISK
                    Provided disk is not found.
                ErrorNumber.NO_SUPPORT
                    Provided disk does not support SCSI SPC.
                ErrorNumber.PERMISSION_DENIED
                    No sufficient permission to access provided disk path.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_fault_led_on, disk_path)

    @staticmethod
    def fault_led_off(disk_path):
        """
        Version:
            1.3
        Usage:
            Turn off the fault LED for specified disk.
        Parameters:
            disk_path (string)
                The disk path, example '/dev/sdb'.
        Returns:
            None
        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
                ErrorNumber.INVALID_ARGUMENT
                    Invalid disk_path. Should be like '/dev/sdb'.
                ErrorNumber.NOT_FOUND_DISK
                    Provided disk is not found.
                ErrorNumber.NO_SUPPORT
                    Provided disk does not support SCSI SPC.
                ErrorNumber.PERMISSION_DENIED
                    No sufficient permission to access provided disk path.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_fault_led_off, disk_path)

    @staticmethod
    def led_status_get(disk_path):
        """
        Version:
            1.3
        Usage:
            Get LED status for specified disk.
        Parameters:
            disk_path (string)
                The disk path, example '/dev/sdb'.
        Returns:
            led_status (integer, bit map)
                Could be combination of these values:
                    lsm.Disk.LED_STATUS_UNKNOWN
                    lsm.Disk.LED_STATUS_IDENT_ON
                    lsm.Disk.LED_STATUS_IDENT_OFF
                    lsm.Disk.LED_STATUS_IDENT_UNKNOWN
                        Has identification LED, but status is unknown.
                        If certain disk has no identification LED,
                        'led_status' should not contain
                        'lsm.Disk.LED_STATUS_IDENT_ON' or
                        'lsm.Disk.LED_STATUS_IDENT_OFF' or
                        'lsm.Disk.LED_STATUS_IDENT_UNKNOWN'
                    lsm.Disk.LED_STATUS_FAULT_ON
                    lsm.Disk.LED_STATUS_FAULT_OFF
                    lsm.Disk.LED_STATUS_FAULT_UNKNOWN
                        Has fault LED, but status is unknown.
                        If certain disk has no fault LED,
                        'led_status' should not contain
                        'lsm.Disk.LED_STATUS_FAULT_ON' or
                        'lsm.Disk.LED_STATUS_FAULT_OFF' or
                        'lsm.Disk.LED_STATUS_FAULT_UNKNOWN'
        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
                ErrorNumber.INVALID_ARGUMENT
                    Invalid disk_path. Should be like '/dev/sdb'.
                ErrorNumber.NOT_FOUND_DISK
                    Provided disk is not found.
                ErrorNumber.NO_SUPPORT
                    Provided disk does not support SCSI SPC.
                ErrorNumber.PERMISSION_DENIED
                    No sufficient permission to access provided disk path.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_led_status_get, disk_path)

    @staticmethod
    def link_speed_get(disk_path):
        """
        Version:
            1.4
        Usage:
            Get current negotiated logical link speed for specified disk.
        Parameters:
            disk_path (string)
                The disk path, example '/dev/sdb'.
        Returns:
            link_speed
                Integer for link speed in Mbps. For example, '3.0 Gbps' will
                get 3000.
        SpecialExceptions:
            LsmError
                ErrorNumber.LIB_BUG
                    Internal bug.
                ErrorNumber.INVALID_ARGUMENT
                    Invalid disk_path. Should be like '/dev/sdb'.
                ErrorNumber.NOT_FOUND_DISK
                    Provided disk is not found.
                ErrorNumber.NO_SUPPORT
                    Provided disk is not supported yet.
                ErrorNumber.PERMISSION_DENIED
                    No sufficient permission to access provided disk path.
        Capability:
            N/A
                No capability required as this is a library level method.
        """
        return _use_c_lib_function(_local_disk_link_speed_get, disk_path)
