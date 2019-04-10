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
                    "No Controllers detected by arcconf.")
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


def _arcconf_raid_level_to_lsm(arcconf_raid_level):

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
            sys_id = ctrl_info['serialNumber']
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
    def _arcconf_array_to_lsm_pool(arcconf_array):
        sys_id = arcconf_array['sys_id']
        array_id = arcconf_array['arrayID']
        array_name = arcconf_array['arrayName']
        block_size = arcconf_array['blockSize']
        total_size = arcconf_array['totalSize']
        unused_size = arcconf_array['unUsedSpace']
        ctrl_num = arcconf_array['ctrl_num']

        pool_id = '%s:%s' % (sys_id, array_id)
        name = 'Array ' + str(array_name)
        elem_type = Pool.ELEMENT_TYPE_VOLUME | Pool.ELEMENT_TYPE_VOLUME_FULL
        unsupported_actions = 0
        free_space = int(block_size) * int(unused_size)
        total_space = int(block_size) * int(total_size)

        status = Pool.STATUS_OK
        status_info = ''
        plugin_data = '%s:%s' % (ctrl_num, array_id)

        return Pool(
            pool_id, name, elem_type, unsupported_actions,
            total_space, free_space, status, status_info,
            sys_id, plugin_data)

    @_handle_errors
    def pools(self, search_key=None, search_value=None,
              flags=Client.FLAG_RSVD):
        lsm_pools = []
        getconfig_cntrls_info = self._get_detail_info_list()
        cntrl = 0

        for decoded_json in getconfig_cntrls_info:
            sys_id = decoded_json['Controller']['serialNumber']
            arcconf_ctrl_num = str(cntrl + 1)
            cntrl += 1

            if 'Array' in decoded_json['Controller']:
                array_infos = decoded_json['Controller']['Array']
                num_array = len(array_infos)
                arcconf_array = {}
                for array in range(num_array):
                    arcconf_array['arrayID'] = array_infos[array]['arrayID']
                    arcconf_array['arrayName'] = array_infos[array]['arrayName']
                    arcconf_array['blockSize'] = array_infos[array]['blockSize']
                    arcconf_array['totalSize'] = array_infos[array]['totalSize']
                    arcconf_array['unUsedSpace'] = \
                        array_infos[array]['unUsedSpace']
                    arcconf_array['sys_id'] = sys_id
                    arcconf_array['ctrl_num'] = arcconf_ctrl_num
                    lsm_pools.append(Arcconf._arcconf_array_to_lsm_pool(
                        arcconf_array))
        return search_property(lsm_pools, search_key, search_value)

    @staticmethod
    def _arcconf_ld_to_lsm_vol(arcconf_ld, array_num, sys_id, ctrl_num):
        ld_id = arcconf_ld['logicalDriveID']
        raid_level = arcconf_ld['raidLevel']
        vpd83 = str(arcconf_ld['volumeUniqueID']).lower()
        pool_id = "%s:%s" % (sys_id, array_num)

        block_size = arcconf_ld['BlockSize']
        num_of_blocks = int(arcconf_ld['dataSpace'])
        vol_name = arcconf_ld['name']

        if vpd83:
            blk_paths = LocalDisk.vpd83_search(vpd83)
            if blk_paths:
                vol_name += ": %s" % " ".join(blk_paths)

        if int(arcconf_ld['state']) != LOGICAL_DEVICE_OK:
            admin_status = Volume.ADMIN_STATE_DISABLED
        else:
            admin_status = Volume.ADMIN_STATE_ENABLED
        
        stripe_size = arcconf_ld['StripeSize']
        full_stripe_size = arcconf_ld['fullStripeSize']
        ld_state = arcconf_ld['state']
        plugin_data = "%s:%s:%s:%s:%s:%s:%s" % (ld_state, raid_level,
                                                stripe_size, full_stripe_size,
                                                ld_id, array_num, ctrl_num)
        volume_id = "%s:%s" % (sys_id, ld_id)
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
        consumer_array_id = ''
        cntrl = 0

        for decoded_json in getconfig_cntrls_info:
            sys_id = decoded_json['Controller']['serialNumber']
            arcconf_ctrl_num = str(cntrl+1)
            cntrl += 1

            if 'LogicalDrive' in decoded_json['Controller']:
                ld_infos = decoded_json['Controller']['LogicalDrive']
                num_lds = len(ld_infos)
                for ld in range(num_lds):
                    ld_info = ld_infos[ld]
                    chunk_data = ld_info['Chunk']
                    for array_id in chunk_data:
                        # consumerArrayID in all the chunk will be same
                        consumer_array_id = array_id['consumerArrayID']
                    lsm_vol = Arcconf._arcconf_ld_to_lsm_vol(ld_info,
                                                             consumer_array_id,
                                                             sys_id,
                                                             arcconf_ctrl_num)
                    lsm_vols.append(lsm_vol)

        return search_property(lsm_vols, search_key, search_value)

    @staticmethod
    def _arcconf_disk_to_lsm_disk(arcconf_disk, sys_id, ctrl_num):
        disk_id = arcconf_disk['serialNumber'].strip()

        disk_name = "%s - %s" % (arcconf_disk['vendor'], arcconf_disk['model'])
        disk_type = _disk_type_of(arcconf_disk)
        link_type = _disk_link_type_of(arcconf_disk)

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

        disk_path = arcconf_disk['physicalDriveName']
        if disk_path != 'Not Applicable':
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

        getconfig_cntrls_info = self._get_detail_info_list()
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

    def _arcconf_cap_get(self):
        supported_raid_types = [
            Volume.RAID_TYPE_RAID0, Volume.RAID_TYPE_RAID1,
            Volume.RAID_TYPE_RAID5, Volume.RAID_TYPE_RAID50,
            Volume.RAID_TYPE_RAID10, Volume.RAID_TYPE_RAID6,
            Volume.RAID_TYPE_RAID60]

        supported_strip_sizes = [
            16 * 1024, 32 * 1024, 64 * 1024, 128 * 1024, 256 * 1024, 512 * 1024,
            1024 * 1024]

        return supported_raid_types, supported_strip_sizes

    @_handle_errors
    def volume_raid_create_cap_get(self, system, flags=Client.FLAG_RSVD):
        """
        By default, RAID 0, 1, 10, 5, 50, 6, 60 are supported depending on the
        number of free disks available
        """
        if not system.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input system argument: missing plugin_data property")
        return self._arcconf_cap_get()

    @_handle_errors
    def volume_raid_create(self, name, raid_type, disks, strip_size,
                           flags=Client.FLAG_RSVD):
        """

        :param name: name of volume (this will be ignored)
        :param raid_type: RAID Type of the LD
        :param disks: Physical device
        :param strip_size: Strip size
        :param flags: for future use
        :return: volume object of newly created volume

        Depends on command:
            arcconf create <ctrlNo> logicaldrive max <RAID LEVEL> <channel ID>
            <Device ID> <channel ID> <Device ID> <channel ID> <Device ID>...
        """

        arcconf_raid_level = _lsm_raid_type_to_arcconf(raid_type)
        arcconf_disk_ids = []
        ctrl_num = None

        for disk in disks:
            if not disk.plugin_data:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: missing plugin_data ")
            (cur_ctrl_num, disk_channel, disk_device) = \
                disk.plugin_data.split(',')[:3]

            if ctrl_num is None:
                ctrl_num = cur_ctrl_num
            elif ctrl_num != cur_ctrl_num:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: "
                    "disks are not from the same controller/system.")

            disk_channel = str(disk_channel.strip())
            disk_device = str(disk_device.strip())
            arcconf_disk_ids.append({'Channel': disk_channel,
                                     'Device': disk_device})

        cmds = ["create", ctrl_num, "logicaldrive", "max", arcconf_raid_level]
        for disk_channel_device in arcconf_disk_ids:
            cmds.append(disk_channel_device['Channel'])
            cmds.append(disk_channel_device['Device'])

        try:
            self._arcconf_exec(cmds, flag_force=True)
        except ExecError:
            # Check whether disk is free
            requested_disk_ids = [d.id for d in disks]
            for cur_disk in self.disks():
                if cur_disk.id in requested_disk_ids and not \
                        cur_disk.status & Disk.STATUS_FREE:
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

        volume_info = volume.plugin_data.split(":")
        ctrl_num = volume_info[6]
        ld_id = volume_info[4]

        try:
            self._arcconf_exec(['delete', ctrl_num, 'logicaldrive', ld_id],
                               flag_force=True)
        except ExecError:
            ctrl_info = self._get_detail_info_list()[int(ctrl_num) - 1]
            for ld in range(len(ctrl_info['LogicalDrive'])):
                ld_info = ctrl_info['LogicalDrive']
                # TODO (Raghavendra) Need to find the scenarios when this can
                # occur. If volume is detected correctly, but deletion of
                # volume fails due to arcconf delete command failure.
                if ld_id == ld_info[ld]['logicalDriveID']:
                    raise LsmError(ErrorNumber.PLUGIN_BUG,
                                   "volume_delete failed unexpectedly")
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                           "Volume not found")
        return None

    @_handle_errors
    def volume_raid_info(self, volume, flags=Client.FLAG_RSVD):
        """
        :param volume: volume id - Volume to query
        :param flags: optional. Reserved for future use.
        :return: [raid_type, strip_size, disk_count, min_io_size, opt_io_size]

        Depends on command:
            arcconf getconfigjson <ctrlNo> array <arrayNo>
        """

        if not volume.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input volume argument: missing plugin_data property")

        volume_info = volume.plugin_data.split(':')
        ctrl_id = str(volume_info[6])
        array_id = str(volume_info[5])
        volume_raid_level = str(volume_info[1])
        # convert to Kibibyte
        stripe_size = int(volume_info[2]) * 1024
        # convert to Kibibyte
        full_stripe_size = int(volume_info[3]) * 1024
        device_count = 0

        array_info = self._arcconf_exec(['GETCONFIGJSON', ctrl_id, 'ARRAY',
                                         array_id], flag_force=True)
        array_json_info = self._filter_cmd_output(array_info)['Array']
        for chunk in array_json_info['Chunk']:
            if 'deviceID' in chunk.keys():
                device_count += 1
            else:
                continue

        raid_level = _arcconf_raid_level_to_lsm(volume_raid_level)

        if device_count == 0:
            if stripe_size == Volume.STRIP_SIZE_UNKNOWN:
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "volume_raid_info(): Got logical drive %s entry, "
                    "but no physicaldrive entry" % volume.id)

            raise LsmError(
                ErrorNumber.NOT_FOUND_VOLUME,
                "Volume not found")

        return [raid_level, stripe_size, device_count, stripe_size,
                full_stripe_size]

    @_handle_errors
    def pool_member_info(self, pool, flags=Client.FLAG_RSVD):
        """

        :param pool: pool id -  Pool to query
        :param flags: Optional
        :return: [raid_type, member_type, member_ids]

        Depends on command:
            arcconf getconfigjson <ctrlNo>
        """

        if not pool.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input pool argument: missing plugin_data property")

        pool_info = pool.plugin_data.split(':')
        ctrl_id = pool_info[0]
        array_id = int(pool_info[1])
        consumer_array_id = None
        raid_level = None
        device_id = []

        ctrl_info = self._arcconf_exec(['GETCONFIGJSON', ctrl_id])
        ctrl_json_info = self._filter_cmd_output(ctrl_info)
        device_info = ctrl_json_info['Controller']['Channel']
        volume_info = ctrl_json_info['Controller']['LogicalDrive']

        for volume in volume_info:
            chunk_data = volume['Chunk']
            for chunk in chunk_data:
                # consumerArrayID in all the chunk will be same
                consumer_array_id = chunk['consumerArrayID']
            if consumer_array_id == array_id:
                raid_level = volume['raidLevel']

        for device in device_info:
            if 'HardDrive'in device.keys():
                for hard_drive in device['HardDrive']:
                    chunk_data = hard_drive['Chunk']
                    for chunk in chunk_data:
                        if ('consumerArrayID' in chunk.keys()) and (
                                chunk['consumerArrayID'] == array_id):
                            device_id.append(
                                str(hard_drive['serialNumber'].strip()))

        lsm_raid_level = _arcconf_raid_level_to_lsm(str(raid_level))

        return [lsm_raid_level, Pool.MEMBER_TYPE_DISK, device_id]

    @_handle_errors
    def volume_enable(self, volume, flags=Client.FLAG_RSVD):
        """

        :param volume: volume id to be enabled/change-state-to-optimal
        :param flags: for future use
        :return: None

        Depends on command:
            arcconf setstate <ctrlNo> logicaldrive <ldNo> optimal
        """

        if not volume.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input volume argument: missing plugin_data property")

        volume_info = volume.plugin_data.split(':')
        ctrl_id = str(volume_info[6])
        volume_id = str(volume_info[4])
        volume_state = int(volume_info[0])

        try:
            if volume_state == LOGICAL_DEVICE_OK:
                raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                               'Volume is already in Optimal state!')
            else:
                self._arcconf_exec(['SETSTATE', ctrl_id, 'LOGICALDRIVE',
                                    volume_id, 'OPTIMAL'], flag_force=True)
        except ExecError:
            volume_data = self._arcconf_exec(['GETCONFIGJSON', ctrl_id,
                                              'LOGICALDRIVE', volume_id])
            volume_json_data = self._filter_cmd_output(volume_data)[
                'LogicalDrive']
            volume_state = volume_json_data['state']
            if volume_state == LOGICAL_DEVICE_OFFLINE:
                raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                               'Volume state has not changed!')

            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           'Volume-enable failed unexpectedly')
        return None

    @_handle_errors
    def volume_ident_led_on(self, volume, flags=Client.FLAG_RSVD):
        """

        :param volume: volume id to be identified
        :param flags: for future use
        :return:
        Depends on command:
            arcconf identify <ctrlNo> logicaldrive <ldNo> time 3600

            default led blink time is set to 1 hour
        """
        if not volume.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input volume argument: missing plugin_data property")

        volume_info = volume.plugin_data.split(':')
        ctrl_id = str(volume_info[6])
        volume_id = str(volume_info[4])

        try:
            self._arcconf_exec(['IDENTIFY', ctrl_id, 'LOGICALDRIVE', volume_id,
                                'TIME', '3600'], flag_force=True)
        except ExecError:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           'Volume-ident-led-on failed unexpectedly')

        return None

    @_handle_errors
    def volume_ident_led_off(self, volume, flags=Client.FLAG_RSVD):
        """

        :param volume: volume id to stop identification
        :param flags: for future use
        :return:
        Depends on command:
            arcconf identify <ctrlNo> logicaldrive <ldNo> stop
        """
        if not volume.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input volume argument: missing plugin_data property")

        volume_info = volume.plugin_data.split(':')
        ctrl_id = str(volume_info[6])
        volume_id = str(volume_info[4])

        try:
            self._arcconf_exec(['IDENTIFY', ctrl_id, 'LOGICALDRIVE', volume_id,
                                'STOP'], flag_force=True)
        except ExecError:
            raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                           'Looks like none of the LEDs are blinking.')

        return None
