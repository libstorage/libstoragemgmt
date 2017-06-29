# Copyright (C) 2015-2016 Red Hat, Inc.
# (C) Copyright 2015-2016 Hewlett Packard Enterprise Development LP
# (C) Copyright 2016-2017 Microsemi Corporation
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
#         Joe Handzik <joseph.t.handzik@hpe.com>
#         Raghavendra Basvan <raghavendra.br@microsemi.com>

import os
import errno
import re
import json

from pyudev import Context, Device, DeviceNotFoundError

from lsm import (
    IPlugin, Client, Capabilities, VERSION, LsmError, ErrorNumber, uri_parse,
    System, Pool, size_human_2_size_bytes, search_property, Volume, Disk,
    LocalDisk, Battery, int_div, JobStatus)

from lsm.plugin.arcconf.utils import cmd_exec, ExecError

_CONTEXT = Context()

ZERO = 0

# Drive states
DRIVE_READY = 0
DRIVE_ONLINE = 1
DRIVE_RAW = 10
DRIVE_STATE_UNKNOWN = 20

# Controller states
CONTROLLER_STATUS_WORKING = 0
CONTROLLER_STATUS_FAILED = 1
CONTROLLER_STATUS_INACCESSIBLE = 5
CONTROLLER_STATUS_UNKNOWN = 6
CONTROLLER_STATUS_LOCKED_UP = 9
CONTROLLER_STATUS_SYS_PQI_DRIVER_CONFLICT = 10

# RAID modes
HW_RAID_EXPOSE_RAW_DEVICES = 0
HBA_MODE = 2
HW_RAID_HIDE_RAW_DEVICES = 3

# Arcconf Disk Interface
DISK_INTERFACE_TYPE_SATA = 1
DISK_INTERFACE_TYPE_SAS = 4


def _handle_errors(method):
    def _wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except LsmError:
            raise
        except KeyError as key_error:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Expected key missing from arcconf output:%s" %
                key_error)
        except ExecError as exec_error:
            if 'No controllers detected' in exec_error.stdout:
                raise LsmError(
                    ErrorNumber.NOT_FOUND_SYSTEM,
                    "No Controllers deteceted by arcconf.")
            else:
                raise LsmError(ErrorNumber.PLUGIN_BUG, str(exec_error))
        except Exception as common_error:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Got unexpected error %s" % common_error)

    return _wrapper


def _arcconf_size_to_lsm(arcconf_size):
    """
    ARCCONF displays the disk size in terms of  'TB, GB, MB, KB'
    for LSM, they are 'TiB' and etc.
    Returning int of block bytes
    """
    re_regex = re.compile("^([0-9.]+) +([EPTGMK])B")
    re_match = re_regex.match(arcconf_size)
    if re_match:
        return size_human_2_size_bytes(
            "%s%siB" % (re_match.group(1), re_match.group(2)))

    raise LsmError(
        ErrorNumber.PLUGIN_BUG,
        "_arcconf_size_to_lsm(): Got unexpected size string %s" %
        arcconf_size)


def _lsm_size_bytes_to_arcconf_mb(lsm_size):
    """
    Arcconf normally follows the size to be in MB
    """
    return ((lsm_size/1024)/1024)


def _pool_status_of(arcconf_array):
    """
    Return (status, status_info)
    """
    if arcconf_array[0]['Status'] == 'OK':
        return Pool.STATUS_OK, ''
    elif arcconf_array[0]['Status'] == 'Optimal':
        return Pool.STATUS_OK, ''
    elif arcconf_array[0]['Status'] == 'Ready':
        return Pool.STATUS_OK, ''
    elif arcconf_array[0]['Status'] == 'Online':
        return Pool.STATUS_OK, ''
    else:
        # TODO(Raghavendra): Try degrade a RAID or fail a RAID.
        return Pool.STATUS_OTHER, arcconf_array['Status']


def _pool_id_of(sys_id, array_name):
    return "%s:%s" % (sys_id, array_name.replace(' ', ''))


def _disk_type_of(arcconf_disk):
    disk_interface = arcconf_disk['interfaceType']
    isSSD = arcconf_disk['nonSpinning']

    if isSSD is True:
        return Disk.TYPE_SSD

    if disk_interface == DISK_INTERFACE_TYPE_SATA:
        return Disk.TYPE_SATA
    elif disk_interface == DISK_INTERFACE_TYPE_SAS:
        return Disk.TYPE_SAS

    return Disk.TYPE_UNKNOWN


def _disk_status_of(arcconf_disk, flag_free):
    state = arcconf_disk['state']

    if state == DRIVE_READY or state == DRIVE_ONLINE:
        disk_status = Disk.STATUS_OK
    else:
        disk_status = Disk.STATUS_OTHER

    if flag_free:
        disk_status |= Disk.STATUS_FREE

    return disk_status


_ARCCONF_RAID_LEVEL_CONV = {
    '0': Volume.RAID_TYPE_RAID0,
    '1': Volume.RAID_TYPE_RAID1,
    '5': Volume.RAID_TYPE_RAID5,
    '6': Volume.RAID_TYPE_RAID6,
    '1+0': Volume.RAID_TYPE_RAID10,
    '50': Volume.RAID_TYPE_RAID50,
    '60': Volume.RAID_TYPE_RAID60,
}


_ARCCONF_VENDOR_RAID_LEVELS = ['1adm', '1+0adm']


_LSM_RAID_TYPE_CONV = dict(
    list(zip(list(_ARCCONF_RAID_LEVEL_CONV.values()),
             list(_ARCCONF_RAID_LEVEL_CONV.keys()))))


def _arcconf_raid_level_to_lsm(arcconf_ld):
    """
    Based on this property:
        Fault Tolerance: 0/1/5/6/1+0
    """
    arcconf_raid_level = arcconf_ld['Fault Tolerance']

    if arcconf_raid_level in _ARCCONF_VENDOR_RAID_LEVELS:
        return Volume.RAID_TYPE_OTHER

    return _ARCCONF_RAID_LEVEL_CONV.get(arcconf_raid_level,
                                        Volume.RAID_TYPE_UNKNOWN)


def _lsm_raid_type_to_arcconf(raid_type):
    try:
        if raid_type == "simple_volume":
            return Volume.RAID_TYPE_RAID0
        else:
            return _LSM_RAID_TYPE_CONV[raid_type]
    except KeyError:
        raise LsmError(
            ErrorNumber.NO_SUPPORT,
            "Not supported raid type %d" % raid_type)


class Arcconf(IPlugin):
    _DEFAULT_BIN_PATHS = [
        "/usr/bin/arcconf",
        "/usr/sbin/arcconf"]

    def __init__(self):
        self._arcconf_bin = None
        self._tmo_ms = 30000

    def _find_arcconf(self):
        """
        Try _DEFAULT_MDADM_BIN_PATHS
        """
        for cur_path in Arcconf._DEFAULT_BIN_PATHS:
            if os.path.lexists(cur_path):
                self._arcconf_bin = cur_path

        if not self._arcconf_bin:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "arcconf is not installed correctly")

    @_handle_errors
    def plugin_register(self, uri, password, timeout, flags=Client.FLAG_RSVD):
        if os.geteuid() != 0:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "This plugin requires root privilege both daemon and client")
        uri_parsed = uri_parse(uri)
        self._arcconf_bin = uri_parsed.get('parameters', {}).get('arcconf')
        if not self._arcconf_bin:
            self._find_arcconf()

    @_handle_errors
    def plugin_unregister(self, flags=Client.FLAG_RSVD):
        pass

    @_handle_errors
    def job_status(self, job_id, flags=0):
        return (JobStatus.COMPLETE, 100, None)

    @_handle_errors
    def job_free(self, job_id, flags=Client.FLAG_RSVD):
        pass

    @_handle_errors
    def plugin_info(self, flags=Client.FLAG_RSVD):
        return "Arcconf Plugin", VERSION

    @_handle_errors
    def time_out_set(self, ms, flags=Client.FLAG_RSVD):
        self._tmo_ms = ms

    @_handle_errors
    def time_out_get(self, flags=Client.FLAG_RSVD):
        return self._tmo_ms

    @_handle_errors
    def capabilities(self, system, flags=Client.FLAG_RSVD):
        cur_lsm_syss = self.systems()
        if system.id not in list(s.id for s in cur_lsm_syss):
            raise LsmError(
                ErrorNumber.NOT_FOUND_SYSTEM,
                "System not found")
        cap = Capabilities()
        cap.set(Capabilities.DISKS)
        cap.set(Capabilities.SYS_FW_VERSION_GET)
        cap.set(Capabilities.SYS_MODE_GET)
        cap.set(Capabilities.DISK_LOCATION)
        cap.set(Capabilities.DISK_VPD83_GET)
        if system.mode != System.MODE_HBA:
            cap.set(Capabilities.VOLUMES)
            cap.set(Capabilities.VOLUME_RAID_CREATE)
            cap.set(Capabilities.VOLUME_DELETE)

        return cap

    def _arcconf_exec(self, arcconf_cmds, flag_force=False):
        arcconf_cmds.insert(0, self._arcconf_bin)
        if flag_force:
            arcconf_cmds.append('noprompt')
        try:
            output = cmd_exec(arcconf_cmds)
        except OSError as os_error:
            if os_error.errno == errno.ENOENT:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "arcconf binary '%s' does not exist." %
                    self._arcconf_bin)
            else:
                raise

        return output

    def _get_arcconf_controllers_count(self):
        total_cntrls = 0
        try:
            test = self._arcconf_exec(['list'])
            output_lines = [l for l in test.split("\n")]
            first_line = output_lines[0]
            tmp = first_line.split(":")

            if tmp[0].strip() == 'Controllers found':
                total_cntrls = int(tmp[1].strip())
        except:
            raise

        return total_cntrls

    @_handle_errors
    def systems(self, flags=0):
        """
        Depend on command:
            arcconf getconfig <cntrl_no>
        """
        rc_lsm_syss = []
        getconfig_cntrls_info = []
        cntrl_info = {}
        total_cntrl = self._get_arcconf_controllers_count()

        for cntrl in range(total_cntrl):
            cntrl_info = self._arcconf_exec(["getconfigJSON", str(cntrl+1)])
            cntrl_info_json = cntrl_info.split("\n")[1]
            decoded_json = json.loads(cntrl_info_json)
            getconfig_cntrls_info.append(decoded_json)

        for cntrl in range(total_cntrl):
            ctrl_name = getconfig_cntrls_info[cntrl]['Controller']['deviceVendor']
            ctrl_name = ctrl_name + ' ' + (
                            getconfig_cntrls_info[cntrl]['Controller']['deviceName'])
            sys_id = str(cntrl + 1)
            status = getconfig_cntrls_info[cntrl]['Controller']['controllerStatus']
            if status == CONTROLLER_STATUS_WORKING:
                status = System.STATUS_OK
            status_info = '"Controller Status"=[%s]' % (status)
            fw_ver = getconfig_cntrls_info[cntrl]['Controller']['firmwareVersion']
            plugin_data = getconfig_cntrls_info[cntrl]['Controller']['physicalSlot']
            read_cache_pct = System.READ_CACHE_PCT_UNKNOWN
            hwraid_mode = getconfig_cntrls_info[cntrl]['Controller']['functionalMode']
            if (hwraid_mode == HW_RAID_EXPOSE_RAW_DEVICES) or (
                    hwraid_mode == HW_RAID_HIDE_RAW_DEVICES):
                mode = System.MODE_HARDWARE_RAID
            elif hwraid_mode == HBA_MODE:
                mode = System.MODE_HBA
            else:
                mode = System.MODE_UNKNOWN
            status_info += ' mode=[%s]' % str(hwraid_mode)

            rc_lsm_syss.append(System(sys_id,
                                      ctrl_name,
                                      status,
                                      status_info,
                                      plugin_data,
                                      _fw_version=fw_ver,
                                      _mode=mode,
                                      _read_cache_pct=read_cache_pct))

        return rc_lsm_syss

    @staticmethod
    def _arcconf_array_to_lsm_pool(arcconf_array, ld_name, sys_id, ctrl_num):
        pool_id = '%s:%s' % (ctrl_num, ld_name)
        name = 'Array' + str(ld_name)
        elem_type = Pool.ELEMENT_TYPE_VOLUME | Pool.ELEMENT_TYPE_VOLUME_FULL
        unsupported_actions = 0
        free_space = 0
        total_space = 0
        # TODO Need to have a logic for finding free space and total space.

        status = Pool.STATUS_OK
        status_info = ''
        plugin_data = "%s:%s" % (ctrl_num, ld_name)

        return Pool(
            pool_id, name, elem_type, unsupported_actions,
            total_space, free_space, status, status_info,
            sys_id, plugin_data)

    @_handle_errors
    def pools(self, search_key=None, search_value=None,
              flags=Client.FLAG_RSVD):
        lsm_pools = []
        total_cntrl = self._get_arcconf_controllers_count()
        sys_id = ''

        for cntrl in range(total_cntrl):
            cntrl_num = str(int(cntrl + 1))
            sys_id = cntrl_num

            disks_info = self._arcconf_exec(["getconfigJSON", str(cntrl+1)])
            disk_info_json = disks_info.split("\n")[1]
            decoded_json = json.loads(disk_info_json)

            if 'LogicalDrive' in decoded_json['Controller']:
                for ld in range(len(decoded_json['Controller']['LogicalDrive'])):
                    lsm_pools.append(
                    Arcconf._arcconf_array_to_lsm_pool(
                    decoded_json['Controller']['LogicalDrive'][ld]['logicalDriveID'],
                    decoded_json['Controller']['LogicalDrive'][ld]['logicalDriveID'],
                    sys_id,
                    cntrl_num))
        return search_property(lsm_pools, search_key, search_value)

    @staticmethod
    def _arcconf_ld_to_lsm_vol(arcconf_ld, pool_id, sys_id, ctrl_num, array_num,
                          arcconf_ld_name):
        """
        raises DeviceNotFoundError
        """
        ld_num = arcconf_ld['logicalDriveID']
        vpd83 = str(arcconf_ld['volumeUniqueID']).lower()

        block_size = arcconf_ld['BlockSize']
        num_of_blocks = int(arcconf_ld['dataSpace']) / int(block_size)
        vol_name = arcconf_ld_name

        if len(vpd83) > 0:
            blk_paths = LocalDisk.vpd83_search(vpd83)
            if len(blk_paths) > 0:
                blk_path = blk_paths[0]
                vol_name += ": %s" % " ".join(blk_paths)
                device = Device.from_device_file(_CONTEXT, blk_path)
                attributes = device.attributes
                try:
                    block_size = attributes.asint("BlockSize")
                    num_of_blocks = attributes.asint("dataSpace")
                except (KeyError, UnicodeDecodeError, ValueError):
                    pass

        if int(arcconf_ld['state']) > 2:
            admin_status = Volume.ADMIN_STATE_DISABLED
        else:
            admin_status = Volume.ADMIN_STATE_ENABLED
        plugin_data = "%s:%s:%s" % (ctrl_num, array_num, ld_num)

        volume_id = array_num
        return Volume(
            volume_id, vol_name, vpd83, block_size, num_of_blocks,
            admin_status, sys_id, pool_id, plugin_data)

    @_handle_errors
    def volumes(self, search_key=None, search_value=None,
                flags=Client.FLAG_RSVD):
        """
        """
        lsm_vols = []
        total_cntrl = self._get_arcconf_controllers_count()
        getconfig_lds_info = []

        pool_id = ''
        sys_id = ''

        for cntrl in range(total_cntrl):
            getconfig_lds_info = self._arcconf_exec(["getconfigJSON", str(cntrl+1)])
            sys_id = str(cntrl+1)
            cnt = int(cntrl + 1)

            getconfig_lds_info_json = getconfig_lds_info.split("\n")[1]
            decoded_json = json.loads(getconfig_lds_info_json)

            try:
                if 'LogicalDrive' in decoded_json['Controller']:
                    for ld in range(len(decoded_json['Controller']['LogicalDrive'])):
                        ld_num = decoded_json['Controller']['LogicalDrive'][ld]['logicalDriveID']
                        ld_name = decoded_json['Controller']['LogicalDrive'][ld]['name']
                        pool_id = '%s:%s' % (sys_id, ld_num)
                        ld_info = decoded_json['Controller']['LogicalDrive'][ld]
                        lsm_vol = Arcconf._arcconf_ld_to_lsm_vol(ld_info, pool_id, sys_id, cnt, str(ld_num), ld_name)
                        lsm_vols.append(lsm_vol)
            except DeviceNotFoundError:
                pass

        return search_property(lsm_vols, search_key, search_value)

    @staticmethod
    def _arcconf_disk_to_lsm_disk(arcconf_disk, sys_id, ctrl_num, flag_free=False):
        disk_id = arcconf_disk['serialNumber'].strip()

        disk_name = "%s" % (arcconf_disk['model'])
        disk_type = _disk_type_of(arcconf_disk)
        link_type = disk_type
        blk_size = int(arcconf_disk['physicalBlockSize'])
        blk_count = int(arcconf_disk['numOfUsableBlocks'])
        print 'Blk size = %d, Blk count  = %d' % (blk_size, blk_count)

        status = _disk_status_of(arcconf_disk, flag_free)
        disk_reported_slot = arcconf_disk['fsaSlotNum']
        disk_channel = arcconf_disk['channelID']
        disk_device = arcconf_disk['deviceID']
        disk_location = "%s %s %s" % (disk_reported_slot, disk_channel, disk_device)
        plugin_data = "%s,%s,%s,%s" % (ctrl_num, disk_channel, disk_device, status)

        disk_path = ''
        # TODO(Raghavendra) Need to find better way of getting the vpd83 info.
        # Just providing disk_path did not yield the correct vpd info.
        if disk_path:
            vpd83 = LocalDisk.vpd83_get(disk_path)
        else:
            vpd83 = ''
        rpm = arcconf_disk['rotationalSpeed']

        return Disk(
            disk_id, disk_name, disk_type, blk_size, blk_count,
            status, sys_id, _plugin_data=plugin_data, _vpd83=vpd83,
            _location=disk_location, _rpm=rpm, _link_type=link_type)

    @_handle_errors
    def disks(self, search_key=None, search_value=None,
              flags=Client.FLAG_RSVD):

        rc_lsm_disks = []
        controllerList = []

        getconfig_disks_info = []
        total_cntrl = self._get_arcconf_controllers_count()
        sys_id = ''

        for cntrl in range(total_cntrl):
            sys_id = cntrl + 1
            cntrl_num = int(cntrl + 1)
            disks_info = self._arcconf_exec(["getconfigJSON", str(cntrl+1)])
            disk_info_json = disks_info.split("\n")[1]
            decoded_json = json.loads(disk_info_json)

            for channel in range(len(decoded_json['Controller']['Channel'])):
                if 'HardDrive' in decoded_json['Controller']['Channel'][channel]:
                    hd_disks_num = len(decoded_json['Controller']['Channel'][channel]['HardDrive'])
                    if hd_disks_num > 0:
                        for hd_disk in range(hd_disks_num):
                            rc_lsm_disks.append(Arcconf._arcconf_disk_to_lsm_disk(decoded_json['Controller']['Channel'][channel]['HardDrive'][hd_disk], sys_id, cntrl_num, flag_free=True))

        return search_property(rc_lsm_disks, search_key, search_value)

    @_handle_errors
    def volume_raid_create(self, name, raid_type, disks, strip_size,
                           flags=Client.FLAG_RSVD):

        arcconf_raid_level = _lsm_raid_type_to_arcconf(raid_type)
        arcconf_disk_ids = []
        ctrl_num = None
        disk_channel = ''
        disk_device = ''
        disk_dict = {'Channel': '0', 'Device': '1'}
        disk_list_str = ''
        vol_id = ''
        lsm_vols = []

        for disk in self.disks():
            if not disk.plugin_data:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: missing plugin_data "
                    "property")
            (cur_ctrl_num, disk_channel, disk_device, disk_status) = disk.plugin_data.split(',')
            if ctrl_num and cur_ctrl_num != ctrl_num:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: disks are not from the "
                    "same controller/system.")

            requested_disks = [d.name for d in disks]
            for disk_name in requested_disks:
                if str(disk_name) == str(disk.name):
                    disk_channel = str(disk_channel.strip())
                    disk_device = str(disk_device.strip())
                    disk_dict.update({'Channel': disk_channel, 'Device': disk_device})
                    arcconf_disk_ids.append(disk_dict.copy())
                    disk_dict = {}
                    ctrl_num = cur_ctrl_num

        cmds = ["create", ctrl_num, "logicaldrive", "1024", arcconf_raid_level]
        for disk_channel_device in arcconf_disk_ids:
            cmds.append(disk_channel_device['Channel'])
            cmds.append(disk_channel_device['Device'])

        try:
            self._arcconf_exec(cmds, flag_force=True)
        except ExecError:
            # Check whether disk is free
            requested_disk_ids = [d.id for d in disks]
            for cur_disk in self.disks():
                if cur_disk.id in requested_disk_ids and \
                   not ((cur_disk.status & Disk.STATUS_FREE) or (cur_disk.status & Disk.STATUS_OK)):
                    raise LsmError(
                        ErrorNumber.DISK_NOT_FREE,
                        "Disk %s is not in STATUS_FREE state" % cur_disk.id)
            raise

        # Generate pool_id from system id and array.
        getconfig_lds_info = self._arcconf_exec(['getconfigJSON', ctrl_num])
        getconfig_lds_info_json = getconfig_lds_info.split("\n")[1]
        decoded_json = json.loads(getconfig_lds_info_json)

        sys_id = ctrl_num
        cnt = ctrl_num
        try:
            latest_ld = len(decoded_json['Controller']['LogicalDrive']) - 1
            ld_num = decoded_json['Controller']['LogicalDrive'][latest_ld]['logicalDriveID']
            ld_name = decoded_json['Controller']['LogicalDrive'][latest_ld]['name']
            pool_id = '%s:%s' % (ctrl_num, ld_num)
        except DeviceNotFoundError:
            pass

        lsm_vols = self.volumes(search_key='pool_id', search_value=pool_id)

        if len(lsm_vols) < 1:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "volume_raid_create(): Got unexpected count(not 1) of new "
                "volumes: %s" % lsm_vols)
        return lsm_vols[0]

    @_handle_errors
    def volume_delete(self, volume, flags=0):
        """
        Depends on command:
            arcconf delete <ctrlNo> logicaldrive <ld#> noprompt
        """
        if not volume.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input volume argument: missing plugin_data property")

        (ctrl_num, array_num, ld_num) = volume.plugin_data.split(":")

        try:
            self._arcconf_exec(['delete', ctrl_num, 'logicaldrive', array_num], flag_force=True)
        except ExecError:
            raise LsmError(
                ErrorNumber.NOT_FOUND_VOLUME,
                "Volume not found")
        return None

    def _cal_of_lsm_vol(self, lsm_vol):
        """
        Retrieve controller slot number, array number, logical disk number
        of given volume. Also validate the existence.
        Return (ctrl_num, array_num, ld_num)
        """
        try:
            new_lsm_vol = \
                self.volumes(search_key='id', search_value=lsm_vol.id)[0]
        except IndexError:
            raise LsmError(
                ErrorNumber.NOT_FOUND_VOLUME,
                "Volume not found")

        return new_lsm_vol.plugin_data.split(":")
