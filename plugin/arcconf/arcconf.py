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
#         Sharath TS <sharath.ts@microchip.com>

import os
import errno
import re
import json


from lsm import (
    IPlugin, Client, Capabilities, VERSION, LsmError, ErrorNumber, uri_parse,
    System, Pool, size_human_2_size_bytes, search_property, Volume, Disk,
    LocalDisk, Battery)

from lsm.plugin.arcconf.utils import cmd_exec, ExecError


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
CONTROLLER_STATUS_WORKING_DISPLAY = 'Optimal'

# RAID modes
HW_RAID_EXPOSE_RAW_DEVICES = 0
HBA_MODE = 2
HW_RAID_HIDE_RAW_DEVICES = 3
MIXED_MODE = 5
HW_RAID_EXPOSE_RAW_DEVICES_DISPLAY = 'HW RAID (Expose RAW)'
HBA_MODE_DISPLAY = 'HBA'
HW_RAID_HIDE_RAW_DEVICES_DISPLAY = 'HW RAID (Hide RAW)'
MIXED_MODE_DISPLAY = 'Mixed'

# Arcconf Disk Interface
DISK_INTERFACE_TYPE_SATA = 1
DISK_INTERFACE_TYPE_SAS = 4

# Logical Device states
LOGICAL_DEVICE_FREE = 0
LOGICAL_DEVICE_OFFLINE = 1
LOGICAL_DEVICE_OK = 2
LOGICAL_DEVICE_CRITICAL = 3


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
    return lsm_size/(1024*1024)


def _pool_status_of(arcconf_array):
    """
    Return (status, status_info)
    """
    if arcconf_array[0]['Status'] == 'OK' or \
       arcconf_array[0]['Status'] == 'Optimal' or \
       arcconf_array[0]['Status'] == 'Ready' or \
       arcconf_array[0]['Status'] == 'Online':
        return Pool.STATUS_OK, ''

    # TODO(Raghavendra): Try degrade a RAID or fail a RAID.
    return Pool.STATUS_OTHER, arcconf_array['Status']


def _pool_id_of(sys_id, array_name):
    return "%s:%s" % (sys_id, array_name.replace(' ', ''))


def _disk_type_of(arcconf_disk):
    disk_interface = arcconf_disk['interfaceType']
    is_ssd = arcconf_disk['nonSpinning']

    if is_ssd is True:
        return Disk.TYPE_SSD

    if disk_interface == DISK_INTERFACE_TYPE_SATA:
        return Disk.TYPE_SATA
    elif disk_interface == DISK_INTERFACE_TYPE_SAS:
        return Disk.TYPE_SAS

    return Disk.TYPE_UNKNOWN


def _disk_link_type_of(arcconf_disk):
    disk_interface = arcconf_disk['interfaceType']

    if disk_interface == DISK_INTERFACE_TYPE_SATA:
        return Disk.LINK_TYPE_ATA
    elif disk_interface == DISK_INTERFACE_TYPE_SAS:
        return Disk.LINK_TYPE_SAS

    return Disk.LINK_TYPE_UNKNOWN


def _disk_status_of(arcconf_disk):
    state = arcconf_disk['state']

    if state == DRIVE_READY:
        disk_status = Disk.STATUS_OK | Disk.STATUS_FREE
    elif state == DRIVE_ONLINE:
        disk_status = Disk.STATUS_OK
    else:
        disk_status = Disk.STATUS_OTHER

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

_BATTERY_STATUS_CONV = {
    "Recharging": Battery.STATUS_CHARGING,
    "Failed (Replace Batteries/Capacitors)": Battery.STATUS_ERROR,
    "Ok": Battery.STATUS_OK,
}


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
        return _LSM_RAID_TYPE_CONV[raid_type]
    except KeyError:
        raise LsmError(
            ErrorNumber.PLUGIN_BUG,
            "Not supported raid type %d" % raid_type)


class Arcconf(IPlugin):
    _DEFAULT_BIN_PATHS = [
        "/usr/bin/arcconf",
        "/usr/sbin/arcconf",
        "/usr/Arcconf/arcconf"]

    def __init__(self):
        self._arcconf_bin = None
        self._tmo_ms = 30000

    @staticmethod
    def find_arcconf():
        """
        Try _DEFAULT_BIN_PATHS, return None if not found.
        """
        for cur_path in Arcconf._DEFAULT_BIN_PATHS:
            if os.path.lexists(cur_path):
                return cur_path
        return None

    @_handle_errors
    def plugin_register(self, uri, password, timeout, flags=Client.FLAG_RSVD):
        if os.geteuid() != 0:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "This plugin requires root privilege both daemon and client")
        uri_parsed = uri_parse(uri)
        self._arcconf_bin = uri_parsed.get('parameters', {}).get('arcconf')
        if not self._arcconf_bin:
            self._arcconf_bin = Arcconf.find_arcconf()
            if not self._arcconf_bin:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "arcconf is not installed correctly")

        self._arcconf_exec(['list'])

    @_handle_errors
    def plugin_unregister(self, flags=Client.FLAG_RSVD):
        pass

    @_handle_errors
    def job_status(self, job_id, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    @_handle_errors
    def job_free(self, job_id, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

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
            cap.set(Capabilities.POOL_MEMBER_INFO)
            cap.set(Capabilities.VOLUME_RAID_INFO)
            cap.set(Capabilities.VOLUME_LED)
            cap.set(Capabilities.VOLUME_ENABLE)

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
        except ExecError:
            raise LsmError(
                ErrorNumber.NOT_FOUND_SYSTEM,
                "Failed to get controller count")

        return total_cntrls

    def _get_detail_info_list(self):
        detail_info_list = []
        ctrl_count = self._get_arcconf_controllers_count()

        for ctrl_no in range(ctrl_count):
            output = self._arcconf_exec(["getconfigJSON", str(ctrl_no + 1)])
            detail_info = json.loads(output.split("\n")[1])
            detail_info_list.append(detail_info)

        return detail_info_list

    def _filter_cmd_output(self, cmd_output):
        """
        :param cmd_output: arcconf command output obtained after executing a command
        :return: list of Json data
        """
        split_cmd_output = cmd_output.split("\n")[1]
        filter_cmd_output = json.loads(split_cmd_output)

        return filter_cmd_output

    @_handle_errors
    def systems(self, flags=0):
        """
        Depend on command:
            arcconf getconfig <cntrl_no>
        """
        rc_lsm_syss = []
        total_cntrl = self._get_arcconf_controllers_count()
        getconfig_cntrls_info = self._get_detail_info_list()
        mode_display = ''
        status_display = ''

        for cntrl in range(total_cntrl):
            ctrl_info = getconfig_cntrls_info[cntrl]['Controller']
            ctrl_name = "%s %s" % (ctrl_info['deviceVendor'], ctrl_info['deviceName'])
            sys_id = str(cntrl + 1)
            status = ctrl_info['controllerStatus']
            if status == CONTROLLER_STATUS_WORKING:
                status = System.STATUS_OK
                status_display = CONTROLLER_STATUS_WORKING_DISPLAY
            status_info = '"Controller Status"=[%s]' % status_display
            fw_ver = ctrl_info['firmwareVersion']
            plugin_data = ctrl_info['physicalSlot']
            read_cache_pct = ctrl_info['readCachePercentage']
            hwraid_mode = ctrl_info['functionalMode']
            if hwraid_mode == HW_RAID_EXPOSE_RAW_DEVICES:
                mode = System.MODE_HARDWARE_RAID
                mode_display = HW_RAID_EXPOSE_RAW_DEVICES_DISPLAY
            elif hwraid_mode == HW_RAID_HIDE_RAW_DEVICES:
                mode = System.MODE_HARDWARE_RAID
                mode_display = HW_RAID_HIDE_RAW_DEVICES_DISPLAY
            elif hwraid_mode == HBA_MODE:
                mode = System.MODE_HBA
                mode_display = HBA_MODE_DISPLAY
            elif hwraid_mode == MIXED_MODE:
                mode = System.MODE_UNKNOWN
                mode_display = MIXED_MODE_DISPLAY
            else:
                mode = System.MODE_UNKNOWN
            status_info += ' "mode"=[%s]' % str(mode_display)
            physical_slot = ctrl_info['physicalSlot']
            status_info += ' "slot"=[%s]' % str(physical_slot)

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
    def _arcconf_array_to_lsm_pool(ld_name, sys_id, ctrl_num):
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
        getconfig_cntrls_info = self._get_detail_info_list()
        sys_id = ''
        cntrl = 0

        for decoded_json in getconfig_cntrls_info:
            cntrl_num = str(int(cntrl + 1))
            sys_id = cntrl_num
            cntrl += 1

            if 'LogicalDrive' in decoded_json['Controller']:
                ld_infos = decoded_json['Controller']['LogicalDrive']
                num_lds = len(ld_infos)
                for ld in range(num_lds):
                    ld_name = ld_infos[ld]['logicalDriveID']
                    lsm_pools.append(
                        Arcconf._arcconf_array_to_lsm_pool(ld_name, sys_id,
                                                           cntrl_num))
        return search_property(lsm_pools, search_key, search_value)

    @staticmethod
    def _arcconf_ld_to_lsm_vol(arcconf_ld,
                               pool_id,
                               sys_id,
                               ctrl_num,
                               array_num,
                               arcconf_ld_name):
        ld_num = arcconf_ld['logicalDriveID']
        vpd83 = str(arcconf_ld['volumeUniqueID']).lower()

        block_size = arcconf_ld['BlockSize']
        num_of_blocks = int(arcconf_ld['dataSpace']) * 1024 / int(block_size)
        vol_name = arcconf_ld_name

        if vpd83:
            blk_paths = LocalDisk.vpd83_search(vpd83)
            if blk_paths:
                vol_name += ": %s" % " ".join(blk_paths)

        if int(arcconf_ld['state']) != LOGICAL_DEVICE_OK:
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
        getconfig_cntrls_info = self._get_detail_info_list()

        pool_id = ''
        sys_id = ''
        cntrl = 0

        for decoded_json in getconfig_cntrls_info:
            sys_id = str(cntrl+1)
            cnt = int(cntrl + 1)
            cntrl += 1

            if 'LogicalDrive' in decoded_json['Controller']:
                ld_infos = decoded_json['Controller']['LogicalDrive']
                num_lds = len(ld_infos)
                for ld in range(num_lds):
                    ld_info = ld_infos[ld]
                    ld_num = ld_info['logicalDriveID']
                    ld_name = ld_info['name']
                    pool_id = '%s:%s' % (sys_id, ld_num)
                    lsm_vol = \
                        Arcconf._arcconf_ld_to_lsm_vol(ld_info, pool_id,
                                                       sys_id, cnt,
                                                       str(ld_num),
                                                       ld_name)
                    lsm_vols.append(lsm_vol)

        return search_property(lsm_vols, search_key, search_value)

    @staticmethod
    def _arcconf_disk_to_lsm_disk(arcconf_disk, sys_id, ctrl_num):
        disk_id = arcconf_disk['serialNumber'].strip()

        disk_name = "%s" % (arcconf_disk['model'])
        disk_type = _disk_type_of(arcconf_disk)
        link_type = disk_type

        try:
            blk_size = int(arcconf_disk['physicalBlockSize'])
        except KeyError:
            blk_size = int(arcconf_disk['blockSize'])
        blk_count = int(arcconf_disk['numOfUsableBlocks'])

        status = _disk_status_of(arcconf_disk)
        disk_reported_slot = arcconf_disk['fsaSlotNum']
        disk_channel = arcconf_disk['channelID']
        disk_device = arcconf_disk['deviceID']
        disk_location = \
            "%s %s %s" % (disk_reported_slot, disk_channel, disk_device)
        plugin_data = \
            "%s,%s,%s,%s" % (ctrl_num, disk_channel, disk_device, status)

        # TODO(Raghavendra) Need to find better way of getting the vpd83 info.
        # Just providing disk_path did not yield the correct vpd info.
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

        getconfig_cntrls_info = self._get_detail_info_list()
        sys_id = ''
        cntrl = 0

        for decoded_json in getconfig_cntrls_info:
            sys_id = cntrl + 1
            cntrl_num = int(cntrl + 1)
            cntrl += 1

            for channel in range(len(decoded_json['Controller']['Channel'])):
                channel_range = decoded_json['Controller']['Channel'][channel]
                if 'HardDrive' in channel_range:
                    hd_disks_num = len(channel_range['HardDrive'])
                    if hd_disks_num > 0:
                        for hd_disk in range(hd_disks_num):
                            arcconf_disk = \
                                channel_range['HardDrive'][hd_disk]
                            rc_lsm_disks.append(
                                Arcconf._arcconf_disk_to_lsm_disk(
                                    arcconf_disk, sys_id, cntrl_num))

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
        lsm_vols = []

        for disk in disks:
            if not disk.plugin_data:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: missing plugin_data "
                    "property")
            (cur_ctrl_num, disk_channel, disk_device) = \
                disk.plugin_data.split(',')[:3]

            requested_disks = [d.name for d in disks]
            for disk_name in requested_disks:
                if str(disk_name) == str(disk.name):
                    disk_channel = str(disk_channel.strip())
                    disk_device = str(disk_device.strip())
                    disk_dict.update(
                        {'Channel': disk_channel, 'Device': disk_device})
                    arcconf_disk_ids.append(disk_dict.copy())
                    disk_dict = {}
                    if ctrl_num is None:
                        ctrl_num = cur_ctrl_num
                    elif ctrl_num != cur_ctrl_num:
                        raise LsmError(
                            ErrorNumber.INVALID_ARGUMENT,
                            "Illegal input disks argument: disks "
                            "are not from the same controller/system.")

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
                   not cur_disk.status & Disk.STATUS_FREE:
                    raise LsmError(
                        ErrorNumber.DISK_NOT_FREE,
                        "Disk %s is not in STATUS_FREE state" % cur_disk.id)
            raise

        # Generate pool_id from system id and array.
        decoded_json = self._get_detail_info_list()[int(ctrl_num) - 1]

        latest_ld = len(decoded_json['Controller']['LogicalDrive']) - 1
        ld_info = decoded_json['Controller']['LogicalDrive'][latest_ld]
        ld_num = ld_info['logicalDriveID']
        pool_id = '%s:%s' % (ctrl_num, ld_num)

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

        (ctrl_num, array_num) = volume.plugin_data.split(":")[:2]

        try:
            self._arcconf_exec(['delete', ctrl_num, 'logicaldrive', array_num],
                               flag_force=True)
        except ExecError:
            ctrl_info = self._get_detail_info_list()[int(ctrl_num) - 1]
            for ld in range(len(ctrl_info['LogicalDrive'])):
                ld_info = ctrl_info['LogicalDrive']
                # TODO (Raghavendra) Need to find the scenarios when this can
                # occur. If volume is detected correctly, but deletion of
                # volume fails due to arcconf delete command failure.
                if array_num == ld_info[ld]['logicalDriveID']:
                    raise LsmError(ErrorNumber.PLUGIN_BUG,
                                   "volume_delete failed unexpectedly")
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                           "Volume not found")
        return None
