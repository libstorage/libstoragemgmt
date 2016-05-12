# Copyright (C) 2015-2016 Red Hat, Inc.
# (C) Copyright 2015-2016 Hewlett Packard Enterprise Development LP
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

import os
import errno
import re

from pyudev import Context, Device, DeviceNotFoundError

from lsm import (
    IPlugin, Client, Capabilities, VERSION, LsmError, ErrorNumber, uri_parse,
    System, Pool, size_human_2_size_bytes, search_property, Volume, Disk,
    LocalDisk, Battery)

from lsm.plugin.hpsa.utils import cmd_exec, ExecError

_CONTEXT = Context()


def _handle_errors(method):
    def _wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except LsmError:
            raise
        except KeyError as key_error:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Expected key missing from SmartArray hpssacli output:%s" %
                key_error)
        except ExecError as exec_error:
            if 'No controllers detected' in exec_error.stdout:
                raise LsmError(
                    ErrorNumber.NOT_FOUND_SYSTEM,
                    "No HP SmartArray deteceted by hpssacli.")
            else:
                raise LsmError(ErrorNumber.PLUGIN_BUG, str(exec_error))
        except Exception as common_error:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Got unexpected error %s" % common_error)

    return _wrapper


def _sys_status_of(hp_ctrl_status):
    """
    Base on data of "hpssacli ctrl all show status"
    """
    status_info = ''
    status = System.STATUS_UNKNOWN
    check_list = [
        'Controller Status', 'Cache Status', 'Battery/Capacitor Status']
    for key_name in check_list:
        if key_name in hp_ctrl_status:
            if hp_ctrl_status[key_name] == 'Not Configured':
                status = System.STATUS_OK
            elif hp_ctrl_status[key_name] == 'OK':
                status = System.STATUS_OK
            else:
                status = System.STATUS_OTHER
            status_info += hp_ctrl_status[key_name]

    return status, status_info


# This fixes an HPSSACLI  bug where "Mirror Group N:" items are not
# properly indented. This will indent (shunt) them to the appropriate level.
def _fix_mirror_group_lines(output_lines):
    mg_indent_level = None
    for line_num in range(len(output_lines)):
        cur_line = output_lines[line_num]
        cur_indent_level = len(cur_line) - len(cur_line.lstrip())
        if cur_line.lstrip().startswith('Mirror Group '):
            mg_indent_level = cur_indent_level
        elif ((mg_indent_level is not None) and
              (cur_indent_level < mg_indent_level)):
            shunted_line = ' '*2*(mg_indent_level-cur_indent_level) + cur_line
            output_lines[line_num] = shunted_line
        elif mg_indent_level is not None:
            mg_indent_level = None


def _parse_hpssacli_output(output):
    """
    Got a output string of hpssacli to dictionary(nested).
    Workflow:
    0. Check out top and the second indention level's space count.
    1. Check current line and next line to determine whether current line is
       a start of new section.
    2. Skip all un-required sections and their data and sub-sections.
    3. If current line is the start of new section, create an empty dictionary
       where following subsections or data could be stored in.
    """
    required_sections = ['Array:', 'unassigned', 'HBA Drives', 'array']

    output_lines = [
        l for l in output.split("\n")
        if l and not l.startswith('Note:')]

    _fix_mirror_group_lines(output_lines)

    data = {}

    # Determine indention level
    (top_indention_level, second_indention_level) = sorted(
        set(
            len(line) - len(line.lstrip())
            for line in output_lines))[0:2]

    indent_2_data = {
        top_indention_level: data
    }

    flag_required_section = False

    for line_num in range(len(output_lines)):
        cur_line = output_lines[line_num]
        if line_num + 1 == len(output_lines):
            # The current line is the last line.
            nxt_line = ''
        else:
            nxt_line = output_lines[line_num + 1]

        cur_indent_count = len(cur_line) - len(cur_line.lstrip())
        nxt_indent_count = len(nxt_line) - len(nxt_line.lstrip())

        if cur_indent_count == top_indention_level:
            flag_required_section = True

        if cur_indent_count == second_indention_level:
            flag_required_section = False

            if nxt_indent_count == cur_indent_count:
                flag_required_section = True
            else:
                for required_section in required_sections:
                    if cur_line.lstrip().startswith(required_section):
                        flag_required_section = True

        if flag_required_section is False:
            continue

        cur_line_split = cur_line.split(": ")
        cur_data_pointer = indent_2_data[cur_indent_count]

        if nxt_indent_count > cur_indent_count:
            # Current line is new section title
            nxt_line_split = nxt_line.split(": ")
            new_data = {}

            if cur_line.lstrip() not in cur_data_pointer:
                cur_data_pointer[cur_line.lstrip()] = new_data
                indent_2_data[nxt_indent_count] = new_data
            else:
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "_parse_hpssacli_output(): Found duplicate line %s" %
                    cur_line)
        else:
            if len(cur_line_split) == 1:
                cur_data_pointer[cur_line.lstrip()] = None
            else:
                cur_data_pointer[cur_line_split[0].lstrip()] = \
                    ": ".join(cur_line_split[1:]).strip()

    return data


def _hp_size_to_lsm(hp_size):
    """
    HP Using 'TB, GB, MB, KB' and etc, for LSM, they are 'TiB' and etc.
    Return int of block bytes
    """
    re_regex = re.compile("^([0-9.]+) +([EPTGMK])B")
    re_match = re_regex.match(hp_size)
    if re_match:
        return size_human_2_size_bytes(
            "%s%siB" % (re_match.group(1), re_match.group(2)))

    raise LsmError(
        ErrorNumber.PLUGIN_BUG,
        "_hp_size_to_lsm(): Got unexpected HP size string %s" %
        hp_size)


def _pool_status_of(hp_array):
    """
    Return (status, status_info)
    """
    if hp_array['Status'] == 'OK':
        return Pool.STATUS_OK, ''
    else:
        # TODO(Gris Ge): Try degrade a RAID or fail a RAID.
        return Pool.STATUS_OTHER, hp_array['Status']


def _pool_id_of(sys_id, array_name):
    return "%s:%s" % (sys_id, array_name.replace(' ', ''))


def _disk_type_of(hp_disk):
    disk_interface = hp_disk['Interface Type']
    if disk_interface == 'SATA':
        return Disk.TYPE_SATA
    elif disk_interface == 'Solid State SATA':
        return Disk.TYPE_SSD
    elif disk_interface == 'SAS':
        return Disk.TYPE_SAS
    elif disk_interface == 'Solid State SAS':
        return Disk.TYPE_SSD

    return Disk.TYPE_UNKNOWN


def _disk_status_of(hp_disk, flag_free):
    # TODO(Gris Ge): Need more document or test for non-OK disks.
    if hp_disk['Status'] == 'OK':
        disk_status = Disk.STATUS_OK
    else:
        disk_status = Disk.STATUS_OTHER

    if flag_free:
        disk_status |= Disk.STATUS_FREE

    return disk_status


def _disk_link_type_of(hp_disk):
    disk_interface = hp_disk['Interface Type']
    if disk_interface == 'SATA':
        return Disk.LINK_TYPE_ATA
    elif disk_interface == 'Solid State SATA':
        return Disk.LINK_TYPE_ATA
    elif disk_interface == 'SAS':
        return Disk.LINK_TYPE_SAS
    elif disk_interface == 'Solid State SAS':
        return Disk.LINK_TYPE_SAS

    return Disk.LINK_TYPE_UNKNOWN


_HP_RAID_LEVEL_CONV = {
    '0': Volume.RAID_TYPE_RAID0,
    # TODO(Gris Ge): Investigate whether HP has 4 disks RAID 1.
    #                In LSM, that's RAID10.
    '1': Volume.RAID_TYPE_RAID1,
    '5': Volume.RAID_TYPE_RAID5,
    '6': Volume.RAID_TYPE_RAID6,
    '1+0': Volume.RAID_TYPE_RAID10,
    '50': Volume.RAID_TYPE_RAID50,
    '60': Volume.RAID_TYPE_RAID60,
}


_HP_VENDOR_RAID_LEVELS = ['1adm', '1+0adm']


_LSM_RAID_TYPE_CONV = dict(
    zip(_HP_RAID_LEVEL_CONV.values(), _HP_RAID_LEVEL_CONV.keys()))


def _hp_raid_level_to_lsm(hp_ld):
    """
    Based on this property:
        Fault Tolerance: 0/1/5/6/1+0
    """
    hp_raid_level = hp_ld['Fault Tolerance']

    if hp_raid_level in _HP_VENDOR_RAID_LEVELS:
        return Volume.RAID_TYPE_OTHER

    return _HP_RAID_LEVEL_CONV.get(hp_raid_level, Volume.RAID_TYPE_UNKNOWN)


def _lsm_raid_type_to_hp(raid_type):
    try:
        return _LSM_RAID_TYPE_CONV[raid_type]
    except KeyError:
        raise LsmError(
            ErrorNumber.NO_SUPPORT,
            "Not supported raid type %d" % raid_type)


_BATTERY_STATUS_CONV = {
    "Recharging": Battery.STATUS_CHARGING,
    "Failed (Replace Batteries/Capacitors)": Battery.STATUS_ERROR,
    "OK": Battery.STATUS_OK,
}


def _hp_battery_status_to_lsm(ctrl_data):
    try:
        return _BATTERY_STATUS_CONV[ctrl_data["Battery/Capacitor Status"]]
    except KeyError:
        return Battery.STATUS_UNKNOWN


def _sys_id_of_ctrl_data(ctrl_data):
    try:
        return ctrl_data['Serial Number']
    except KeyError:
        # Dynamic Smart Array does not expose a serial number
        return ctrl_data['Host Serial Number']


class SmartArray(IPlugin):
    _DEFAULT_BIN_PATHS = [
        "/usr/sbin/hpssacli", "/opt/hp/hpssacli/bld/hpssacli"]

    def __init__(self):
        self._sacli_bin = None

    def _find_sacli(self):
        """
        Try _DEFAULT_MDADM_BIN_PATHS
        """
        for cur_path in SmartArray._DEFAULT_BIN_PATHS:
            if os.path.lexists(cur_path):
                self._sacli_bin = cur_path

        if not self._sacli_bin:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "SmartArray sacli is not installed correctly")

    @_handle_errors
    def plugin_register(self, uri, password, timeout, flags=Client.FLAG_RSVD):
        if os.geteuid() != 0:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "This plugin requires root privilege both daemon and client")
        uri_parsed = uri_parse(uri)
        self._sacli_bin = uri_parsed.get('parameters', {}).get('hpssacli')
        if not self._sacli_bin:
            self._find_sacli()

        self._sacli_exec(['version'], flag_convert=False)

    @_handle_errors
    def plugin_unregister(self, flags=Client.FLAG_RSVD):
        pass

    @_handle_errors
    def job_status(self, job_id, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    @_handle_errors
    def job_free(self, job_id, flags=Client.FLAG_RSVD):
        pass

    @_handle_errors
    def plugin_info(self, flags=Client.FLAG_RSVD):
        return "HP SmartArray Plugin", VERSION

    @_handle_errors
    def time_out_set(self, ms, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    @_handle_errors
    def time_out_get(self, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    @_handle_errors
    def capabilities(self, system, flags=Client.FLAG_RSVD):
        cur_lsm_syss = self.systems()
        if system.id not in list(s.id for s in cur_lsm_syss):
            raise LsmError(
                ErrorNumber.NOT_FOUND_SYSTEM,
                "System not found")
        cap = Capabilities()
        cap.set(Capabilities.VOLUMES)
        cap.set(Capabilities.DISKS)
        cap.set(Capabilities.VOLUME_RAID_INFO)
        cap.set(Capabilities.POOL_MEMBER_INFO)
        cap.set(Capabilities.VOLUME_RAID_CREATE)
        cap.set(Capabilities.VOLUME_DELETE)
        cap.set(Capabilities.VOLUME_ENABLE)
        cap.set(Capabilities.SYS_FW_VERSION_GET)
        cap.set(Capabilities.SYS_MODE_GET)
        cap.set(Capabilities.SYS_READ_CACHE_PCT_UPDATE)
        cap.set(Capabilities.SYS_READ_CACHE_PCT_GET)
        cap.set(Capabilities.DISK_LOCATION)
        cap.set(Capabilities.VOLUME_LED)
        cap.set(Capabilities.BATTERIES)
        cap.set(Capabilities.VOLUME_CACHE_INFO)
        cap.set(Capabilities.VOLUME_PHYSICAL_DISK_CACHE_UPDATE)
        cap.set(Capabilities.VOLUME_PHYSICAL_DISK_CACHE_UPDATE_SYSTEM_LEVEL)
        cap.set(Capabilities.VOLUME_WRITE_CACHE_POLICY_UPDATE_WRITE_BACK)
        cap.set(Capabilities.VOLUME_WRITE_CACHE_POLICY_UPDATE_AUTO)
        cap.set(Capabilities.VOLUME_WRITE_CACHE_POLICY_UPDATE_WRITE_THROUGH)
        cap.set(Capabilities.VOLUME_WRITE_CACHE_POLICY_UPDATE_WB_IMPACT_OTHER)
        cap.set(Capabilities.VOLUME_WRITE_CACHE_POLICY_UPDATE_IMPACT_READ)
        cap.set(Capabilities.VOLUME_READ_CACHE_POLICY_UPDATE)
        cap.set(Capabilities.VOLUME_READ_CACHE_POLICY_UPDATE_IMPACT_WRITE)

        return cap

    def _sacli_exec(self, sacli_cmds, flag_convert=True, flag_force=False):
        """
        If flag_convert is True, convert data into dict.
        """
        sacli_cmds.insert(0, self._sacli_bin)
        if flag_force:
            sacli_cmds.append('forced')
        try:
            output = cmd_exec(sacli_cmds)
        except OSError as os_error:
            if os_error.errno == errno.ENOENT:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "hpssacli binary '%s' is not exist or executable." %
                    self._sacli_bin)
            else:
                raise

        if flag_convert:
            return _parse_hpssacli_output(output)
        else:
            return output

    @_handle_errors
    def systems(self, flags=0):
        """
        Depend on command:
            hpssacli ctrl all show detail
            hpssacli ctrl all show status
        """
        rc_lsm_syss = []
        ctrl_all_show = self._sacli_exec(
            ["ctrl", "all", "show", "detail"])
        ctrl_all_status = self._sacli_exec(
            ["ctrl", "all", "show", "status"])

        for ctrl_name in ctrl_all_show.keys():
            ctrl_data = ctrl_all_show[ctrl_name]
            sys_id = _sys_id_of_ctrl_data(ctrl_data)
            (status, status_info) = _sys_status_of(ctrl_all_status[ctrl_name])

            plugin_data = "%s" % ctrl_data['Slot']
            try:
                fw_ver = "%s" % ctrl_data['Firmware Version']
            except KeyError:
                # Dynamic Smart Array does not expose a firmware version
                fw_ver = "%s" % ctrl_data['RAID Stack Version']
            if 'Cache Ratio' in ctrl_data:
                cache_pct = re.findall(r'\d+', ctrl_data['Cache Ratio'])
                read_cache_pct = int(cache_pct[0])
            elif 'Accelerator Ratio' in ctrl_data:
                cache_pct = re.findall(r'\d+', ctrl_data['Accelerator Ratio'])
                read_cache_pct = int(cache_pct[0])
            else:
                # Some Smart Arrays don't have cache
                # This entry is also missing until a volume uses cache
                read_cache_pct = System.CACHE_PCT_UNKNOWN
            if 'Controller Mode' in ctrl_data:
                hwraid_mode = ctrl_data['Controller Mode']
                if hwraid_mode == 'RAID':
                    mode = System.MODE_HARDWARE_RAID
                elif hwraid_mode == 'HBA':
                    mode = System.MODE_HBA
                else:
                    raise LsmError(
                        ErrorNumber.PLUGIN_BUG,
                        "Invalid Controller Mode: '%s'" % hwraid_mode)
            else:
                # prior to late Gen8, all Smart Arrays were RAID mode only
                mode = System.MODE_HARDWARE_RAID

            rc_lsm_syss.append(System(sys_id, ctrl_name, status, status_info,
                                      plugin_data, _fw_version=fw_ver,
                                      _mode=mode,
                                      _read_cache_pct=read_cache_pct))

        return rc_lsm_syss

    @_handle_errors
    def system_read_cache_pct_update(self, system, read_pct,
                                     flags=Client.FLAG_RSVD):
        """
        Depends on command:
            hpssacli ctrl slot=# modify cacheratio=read_pct/100-read_pct
        """
        if not system.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input system argument: missing plugin_data property")

        slot_num = system.plugin_data

        if (read_pct < 0 or read_pct > 100):
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input read_pct: Percentage is invalid")

        try:
            self._sacli_exec(
                ["ctrl", "slot=%s" % slot_num, "modify",
                 "cacheratio=%s/%s" % (read_pct, 100-read_pct)],
                flag_convert=False)
        except ExecError:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "system_read_cache_pct_update failed unexpectedly,"
                " the system either does not support this operation"
                " or no volumes have been configured to use system cache")

    @staticmethod
    def _hp_array_to_lsm_pool(hp_array, array_name, sys_id, ctrl_num):
        pool_id = _pool_id_of(sys_id, array_name)
        name = array_name
        elem_type = Pool.ELEMENT_TYPE_VOLUME | Pool.ELEMENT_TYPE_VOLUME_FULL
        unsupported_actions = 0
        # TODO(Gris Ge): HP does not provide a precise number of bytes.
        free_space = _hp_size_to_lsm(hp_array['Unused Space'])
        total_space = free_space
        for key_name in hp_array.keys():
            if key_name.startswith('Logical Drive'):
                total_space += _hp_size_to_lsm(hp_array[key_name]['Size'])

        (status, status_info) = _pool_status_of(hp_array)

        plugin_data = "%s:%s" % (
            ctrl_num, array_name[len("Array: "):])

        return Pool(
            pool_id, name, elem_type, unsupported_actions,
            total_space, free_space, status, status_info,
            sys_id, plugin_data)

    @_handle_errors
    def pools(self, search_key=None, search_value=None,
              flags=Client.FLAG_RSVD):
        """
        Depend on command:
            hpssacli ctrl all show config detail
        """
        lsm_pools = []
        ctrl_all_conf = self._sacli_exec(
            ["ctrl", "all", "show", "config", "detail"])
        for ctrl_data in ctrl_all_conf.values():
            sys_id = _sys_id_of_ctrl_data(ctrl_data)
            ctrl_num = ctrl_data['Slot']
            for key_name in ctrl_data.keys():
                if key_name.startswith("Array:"):
                    lsm_pools.append(
                        SmartArray._hp_array_to_lsm_pool(
                            ctrl_data[key_name], key_name, sys_id, ctrl_num))

        return search_property(lsm_pools, search_key, search_value)

    @staticmethod
    def _hp_ld_to_lsm_vol(hp_ld, pool_id, sys_id, ctrl_num, array_num,
                          hp_ld_name):
        """
        raises DeviceNotFoundError
        """
        ld_num = hp_ld_name[len("Logical Drive: "):]
        vpd83 = hp_ld['Unique Identifier'].lower()
        # No document or command output indicate block size
        # of volume. So we try to read from linux kernel, if failed
        # try 512 and roughly calculate the sector count.
        device = Device.from_device_file(_CONTEXT, hp_ld['Disk Name'])
        vol_name = "%s: /dev/%s" % (hp_ld_name, device.sys_name)
        attributes = device.attributes
        try:
            block_size = attributes.asint("queue/logical_block_size")
            num_of_blocks = attributes.asint("size")
        except (KeyError, UnicodeDecodeError, ValueError):
            block_size = 512
            num_of_blocks = int(_hp_size_to_lsm(hp_ld['Size']) / block_size)

        if 'Failed' in hp_ld['Status']:
            admin_status = Volume.ADMIN_STATE_DISABLED
        else:
            admin_status = Volume.ADMIN_STATE_ENABLED
        plugin_data = "%s:%s:%s" % (ctrl_num, array_num, ld_num)

        # HP SmartArray does not allow disabling volume.
        return Volume(
            vpd83, vol_name, vpd83, block_size, num_of_blocks,
            admin_status, sys_id, pool_id, plugin_data)

    @_handle_errors
    def volumes(self, search_key=None, search_value=None,
                flags=Client.FLAG_RSVD):
        """
        Depend on command:
            hpssacli ctrl all show config detail
        """
        lsm_vols = []
        ctrl_all_conf = self._sacli_exec(
            ["ctrl", "all", "show", "config", "detail"])
        for ctrl_data in ctrl_all_conf.values():
            ctrl_num = ctrl_data['Slot']
            sys_id = _sys_id_of_ctrl_data(ctrl_data)
            for key_name in ctrl_data.keys():
                if not key_name.startswith("Array:"):
                    continue
                pool_id = _pool_id_of(sys_id, key_name)
                array_num = key_name[len('Array: '):]
                for array_key_name in ctrl_data[key_name].keys():
                    if not array_key_name.startswith("Logical Drive"):
                        continue

                    try:
                        lsm_vol = SmartArray._hp_ld_to_lsm_vol(
                           ctrl_data[key_name][array_key_name],
                           pool_id, sys_id, ctrl_num, array_num,
                           array_key_name)
                    except DeviceNotFoundError:
                        pass
                    else:
                        lsm_vols.append(lsm_vol)

        return search_property(lsm_vols, search_key, search_value)

    @staticmethod
    def _hp_disk_to_lsm_disk(hp_disk, sys_id, ctrl_num, key_name,
                             flag_free=False):
        disk_id = hp_disk['Serial Number']
        disk_num = key_name[len("physicaldrive "):]
        disk_name = "%s %s" % (hp_disk['Model'], disk_num)
        disk_type = _disk_type_of(hp_disk)
        blk_size = int(hp_disk['Native Block Size'])
        blk_count = int(_hp_size_to_lsm(hp_disk['Size']) / blk_size)
        disk_port, disk_box, disk_bay = disk_num.split(":")
        disk_location = "Port: %s Box: %s Bay: %s" % (
            disk_port, disk_box, disk_bay)

        status = _disk_status_of(hp_disk, flag_free)
        plugin_data = "%s:%s" % (ctrl_num, disk_num)
        disk_path = hp_disk.get('Disk Name')
        if disk_path:
            vpd83 = LocalDisk.vpd83_get(disk_path)
        else:
            vpd83 = ''
        rpm = int(hp_disk.get('Rotational Speed',
                              Disk.RPM_NON_ROTATING_MEDIUM))
        link_type = _disk_link_type_of(hp_disk)

        return Disk(
            disk_id, disk_name, disk_type, blk_size, blk_count,
            status, sys_id, _plugin_data=plugin_data, _vpd83=vpd83,
            _location=disk_location, _rpm=rpm, _link_type=link_type)

    @_handle_errors
    def disks(self, search_key=None, search_value=None,
              flags=Client.FLAG_RSVD):
        """
        Depend on command:
            hpssacli ctrl all show config detail
        """
        # TODO(Gris Ge): Need real test on spare disk.
        rc_lsm_disks = []
        ctrl_all_conf = self._sacli_exec(
            ["ctrl", "all", "show", "config", "detail"])
        for ctrl_data in ctrl_all_conf.values():
            sys_id = _sys_id_of_ctrl_data(ctrl_data)
            ctrl_num = ctrl_data['Slot']
            for key_name in ctrl_data.keys():
                if key_name.startswith("Array:"):
                    for array_key_name in ctrl_data[key_name].keys():
                        if array_key_name.startswith("physicaldrive"):
                            rc_lsm_disks.append(
                                SmartArray._hp_disk_to_lsm_disk(
                                    ctrl_data[key_name][array_key_name],
                                    sys_id, ctrl_num, array_key_name,
                                    flag_free=False))

                if key_name == 'unassigned' or key_name == 'HBA Drives':
                    for array_key_name in ctrl_data[key_name].keys():
                        if array_key_name.startswith("physicaldrive"):
                            rc_lsm_disks.append(
                                SmartArray._hp_disk_to_lsm_disk(
                                    ctrl_data[key_name][array_key_name],
                                    sys_id, ctrl_num, array_key_name,
                                    flag_free=True))

        return search_property(rc_lsm_disks, search_key, search_value)

    @_handle_errors
    def volume_raid_info(self, volume, flags=Client.FLAG_RSVD):
        """
        Depend on command:
            hpssacli ctrl slot=0 show config detail
        """
        if not volume.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Ilegal input volume argument: missing plugin_data property")

        (ctrl_num, array_num, ld_num) = volume.plugin_data.split(":")
        ctrl_data = self._sacli_exec(
            ["ctrl", "slot=%s" % ctrl_num, "show", "config", "detail"]
            ).values()[0]

        disk_count = 0
        strip_size = Volume.STRIP_SIZE_UNKNOWN
        stripe_size = Volume.OPT_IO_SIZE_UNKNOWN
        raid_type = Volume.RAID_TYPE_UNKNOWN
        for key_name in ctrl_data.keys():
            if key_name != "Array: %s" % array_num:
                continue
            for array_key_name in ctrl_data[key_name].keys():
                if array_key_name == "Logical Drive: %s" % ld_num:
                    hp_ld = ctrl_data[key_name][array_key_name]
                    raid_type = _hp_raid_level_to_lsm(hp_ld)
                    strip_size = _hp_size_to_lsm(hp_ld['Strip Size'])
                    stripe_size = _hp_size_to_lsm(hp_ld['Full Stripe Size'])
                elif array_key_name.startswith("physicaldrive"):
                    hp_disk = ctrl_data[key_name][array_key_name]
                    if hp_disk['Drive Type'] == 'Data Drive':
                        disk_count += 1

        if disk_count == 0:
            if strip_size == Volume.STRIP_SIZE_UNKNOWN:
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "volume_raid_info(): Got logical drive %s entry, " %
                    ld_num + "but no physicaldrive entry: %s" %
                    ctrl_data.items())

            raise LsmError(
                ErrorNumber.NOT_FOUND_VOLUME,
                "Volume not found")

        return [raid_type, strip_size, disk_count, strip_size, stripe_size]

    @_handle_errors
    def pool_member_info(self, pool, flags=Client.FLAG_RSVD):
        """
        Depend on command:
            hpssacli ctrl slot=0 show config detail
        """
        if not pool.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Ilegal input volume argument: missing plugin_data property")

        (ctrl_num, array_num) = pool.plugin_data.split(":")
        ctrl_data = self._sacli_exec(
            ["ctrl", "slot=%s" % ctrl_num, "show", "config", "detail"]
            ).values()[0]

        disk_ids = []
        raid_type = Volume.RAID_TYPE_UNKNOWN
        for key_name in ctrl_data.keys():
            if key_name == "Array: %s" % array_num:
                for array_key_name in ctrl_data[key_name].keys():
                    if array_key_name.startswith("Logical Drive: ") and \
                       raid_type == Volume.RAID_TYPE_UNKNOWN:
                        raid_type = _hp_raid_level_to_lsm(
                            ctrl_data[key_name][array_key_name])
                    elif array_key_name.startswith("physicaldrive"):
                        hp_disk = ctrl_data[key_name][array_key_name]
                        if hp_disk['Drive Type'] == 'Data Drive':
                            disk_ids.append(hp_disk['Serial Number'])
                break

        if len(disk_ids) == 0:
            raise LsmError(
                ErrorNumber.NOT_FOUND_POOL,
                "Pool not found")

        return raid_type, Pool.MEMBER_TYPE_DISK, disk_ids

    def _vrc_cap_get(self, ctrl_num):
        supported_raid_types = [
            Volume.RAID_TYPE_RAID0, Volume.RAID_TYPE_RAID1,
            Volume.RAID_TYPE_RAID5, Volume.RAID_TYPE_RAID50,
            Volume.RAID_TYPE_RAID10]

        supported_strip_sizes = [
            8 * 1024, 16 * 1024, 32 * 1024, 64 * 1024,
            128 * 1024, 256 * 1024, 512 * 1024, 1024 * 1024]

        ctrl_conf = self._sacli_exec([
            "ctrl", "slot=%s" % ctrl_num, "show", "config", "detail"]
            ).values()[0]

        if 'RAID 6 (ADG) Status' in ctrl_conf and \
           ctrl_conf['RAID 6 (ADG) Status'] == 'Enabled':
            supported_raid_types.extend(
                [Volume.RAID_TYPE_RAID6, Volume.RAID_TYPE_RAID60])

        return supported_raid_types, supported_strip_sizes

    @_handle_errors
    def volume_raid_create_cap_get(self, system, flags=Client.FLAG_RSVD):
        """
        Depends on this command:
            hpssacli ctrl slot=0 show config detail
        All hpsa support RAID 1, 10, 5, 50.
        If "RAID 6 (ADG) Status: Enabled", it will support RAID 6 and 60.
        For HP tribile mirror(RAID 1adm and RAID10adm), LSM does support
        that yet.
        No command output or document indication special or exceptional
        support of strip size, assuming all hpsa cards support documented
        strip sizes.
        """
        if not system.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Ilegal input system argument: missing plugin_data property")
        return self._vrc_cap_get(system.plugin_data)

    @_handle_errors
    def volume_raid_create(self, name, raid_type, disks, strip_size,
                           flags=Client.FLAG_RSVD):
        """
        Depends on these commands:
            1. Create LD
                hpssacli ctrl slot=0 create type=ld \
                    drives=1i:1:13,1i:1:14 size=max raid=1+0 ss=64
                NOTE: This now optionally appends arguments \
                if certain Client flags are set.
            2. Find out the system ID.

            3. Find out the pool fist disk belong.
                hpssacli ctrl slot=0 pd 1i:1:13 show

            4. List all volumes for this new pool.
                self.volumes(search_key='pool_id', search_value=pool_id)

        The 'name' argument will be ignored.
        TODO(Gris Ge): These code only tested for creating 1 disk RAID 0.
        """
        hp_raid_level = _lsm_raid_type_to_hp(raid_type)
        hp_disk_ids = []
        ctrl_num = None
        for disk in disks:
            if not disk.plugin_data:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: missing plugin_data "
                    "property")
            (cur_ctrl_num, hp_disk_id) = disk.plugin_data.split(':', 1)
            if ctrl_num and cur_ctrl_num != ctrl_num:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: disks are not from the "
                    "same controller/system.")

            ctrl_num = cur_ctrl_num
            hp_disk_ids.append(hp_disk_id)

        cmds = [
            "ctrl", "slot=%s" % ctrl_num, "create", "type=ld",
            "drives=%s" % ','.join(hp_disk_ids), 'size=max',
            'raid=%s' % hp_raid_level]

        if strip_size != Volume.VCR_STRIP_SIZE_DEFAULT:
            cmds.append("ss=%d" % int(strip_size / 1024))

        if flags == Client.FLAG_VOLUME_CREATE_USE_SYSTEM_CACHE:
            cmds.append("aa=enable")

        if flags == Client.FLAG_VOLUME_CREATE_USE_IO_PASSTHROUGH:
            cmds.append("ssdsmartpath=enable")

        if flags == Client.FLAG_VOLUME_CREATE_DISABLE_SYSTEM_CACHE:
            cmds.append("aa=disable")

        if flags == Client.FLAG_VOLUME_CREATE_DISABLE_IO_PASSTHROUGH:
            cmds.append("ssdsmartpath=disable")

        try:
            self._sacli_exec(cmds, flag_convert=False, flag_force=True)
        except ExecError:
            # Check whether disk is free
            requested_disk_ids = [d.id for d in disks]
            for cur_disk in self.disks():
                if cur_disk.id in requested_disk_ids and \
                   not cur_disk.status & Disk.STATUS_FREE:
                    raise LsmError(
                        ErrorNumber.DISK_NOT_FREE,
                        "Disk %s is not in STATUS_FREE state" % cur_disk.id)

            # Check whether got unsupported raid type or strip size
            supported_raid_types, supported_strip_sizes = \
                self._vrc_cap_get(ctrl_num)

            if raid_type not in supported_raid_types:
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "Provided raid_type is not supported")

            if strip_size != Volume.VCR_STRIP_SIZE_DEFAULT and \
               strip_size not in supported_strip_sizes:
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "Provided strip_size is not supported")

            raise

        # Find out the system id to gernerate pool_id
        sys_output = self._sacli_exec(
            ['ctrl', "slot=%s" % ctrl_num, 'show'])

        sys_id = _sys_id_of_ctrl_data(sys_output.values()[0])
        # API code already checked empty 'disks', we will for sure get
        # valid 'ctrl_num' and 'hp_disk_ids'.

        pd_output = self._sacli_exec(
            ['ctrl', "slot=%s" % ctrl_num, 'pd', hp_disk_ids[0], 'show'])

        if pd_output.values()[0].keys()[0].lower().startswith("array "):
            hp_array_id = pd_output.values()[0].keys()[0][len("array "):]
            hp_array_id = "Array:%s" % hp_array_id
        else:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "volume_raid_create(): Failed to find out the array ID of "
                "new array: %s" % pd_output.items())

        pool_id = _pool_id_of(sys_id, hp_array_id)

        lsm_vols = self.volumes(search_key='pool_id', search_value=pool_id)

        if len(lsm_vols) != 1:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "volume_raid_create(): Got unexpected count(not 1) of new "
                "volumes: %s" % lsm_vols)
        return lsm_vols[0]

    @_handle_errors
    def volume_delete(self, volume, flags=0):
        """
        Depends on command:
            hpssacli ctrl slot=# ld # delete forced
            hpssacli ctrl slot=# show config detail
        """
        if not volume.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input volume argument: missing plugin_data property")

        (ctrl_num, array_num, ld_num) = volume.plugin_data.split(":")

        try:
            self._sacli_exec(
                ["ctrl", "slot=%s" % ctrl_num, "ld %s" % ld_num, "delete"],
                flag_convert=False, flag_force=True)
        except ExecError:
            ctrl_data = self._sacli_exec(
                ["ctrl", "slot=%s" % ctrl_num, "show", "config", "detail"]
                ).values()[0]

            for key_name in ctrl_data.keys():
                if key_name != "Array: %s" % array_num:
                    continue
                for array_key_name in ctrl_data[key_name].keys():
                    if array_key_name == "Logical Drive: %s" % ld_num:
                        raise LsmError(
                            ErrorNumber.PLUGIN_BUG,
                            "volume_delete failed unexpectedly")
            raise LsmError(
                ErrorNumber.NOT_FOUND_VOLUME,
                "Volume not found")

        return None

    def volume_enable(self, volume, flags=Client.FLAG_RSVD):
        """
        Depend on command:
            hpssacli ctrl slot=# ld # modify reenable forced
            hpssacli ctrl slot=# show config detail
        """
        if not volume.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input volume argument: missing plugin_data property")

        (ctrl_num, array_num, ld_num) = volume.plugin_data.split(":")

        try:
            self._sacli_exec(
                ["ctrl", "slot=%s" % ctrl_num, "ld %s" % ld_num, "modify",
                 "reenable"], flag_convert=False, flag_force=True)
        except ExecError:
            ctrl_data = self._sacli_exec(
                ["ctrl", "slot=%s" % ctrl_num, "show", "config", "detail"]
                ).values()[0]

            for key_name in ctrl_data.keys():
                if key_name != "Array: %s" % array_num:
                    continue
                for array_key_name in ctrl_data[key_name].keys():
                    if array_key_name == "Logical Drive: %s" % ld_num:
                        raise LsmError(
                            ErrorNumber.PLUGIN_BUG,
                            "volume_enable failed unexpectedly")
            raise LsmError(
                ErrorNumber.NOT_FOUND_VOLUME,
                "Volume not found")

        return None

    @_handle_errors
    def volume_ident_led_on(self, volume, flags=Client.FLAG_RSVD):
        """
        Depend on command:
            hpssacli ctrl slot=# ld # modify led=on
            hpssacli ctrl slot=# show config detail
        """
        if not volume.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Ilegal input volume argument: missing plugin_data property")

        (ctrl_num, array_num, ld_num) = volume.plugin_data.split(":")

        try:
            self._sacli_exec(
                ["ctrl", "slot=%s" % ctrl_num, "ld %s" % ld_num, "modify",
                 "led=on"], flag_convert=False)
        except ExecError:
            ctrl_data = self._sacli_exec(
                ["ctrl", "slot=%s" % ctrl_num, "show", "config", "detail"]
                ).values()[0]

            for key_name in ctrl_data.keys():
                if key_name != "Array: %s" % array_num:
                    continue
                for array_key_name in ctrl_data[key_name].keys():
                    if array_key_name == "Logical Drive: %s" % ld_num:
                        raise LsmError(
                            ErrorNumber.PLUGIN_BUG,
                            "volume_ident_led_on failed unexpectedly")
            raise LsmError(
                ErrorNumber.NOT_FOUND_VOLUME,
                "Volume not found")

        return None

    @_handle_errors
    def volume_ident_led_off(self, volume, flags=Client.FLAG_RSVD):
        """
        Depend on command:
            hpssacli ctrl slot=# ld # modify led=off
            hpssacli ctrl slot=# show config detail
        """
        if not volume.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Ilegal input volume argument: missing plugin_data property")

        (ctrl_num, array_num, ld_num) = volume.plugin_data.split(":")

        try:
            self._sacli_exec(
                ["ctrl", "slot=%s" % ctrl_num, "ld %s" % ld_num, "modify",
                 "led=off"], flag_convert=False)
        except ExecError:
            ctrl_data = self._sacli_exec(
                ["ctrl", "slot=%s" % ctrl_num, "show", "config", "detail"]
                ).values()[0]

            for key_name in ctrl_data.keys():
                if key_name != "Array: %s" % array_num:
                    continue
                for array_key_name in ctrl_data[key_name].keys():
                    if array_key_name == "Logical Drive: %s" % ld_num:
                        raise LsmError(
                            ErrorNumber.PLUGIN_BUG,
                            "volume_ident_led_off failed unexpectedly")
            raise LsmError(
                ErrorNumber.NOT_FOUND_VOLUME,
                "Volume not found")

        return None

    @_handle_errors
    def batteries(self, search_key=None, search_value=None,
                  flags=Client.FLAG_RSVD):
        lsm_bs = []
        ctrl_all_show = self._sacli_exec(
            ["ctrl", "all", "show", "config", "detail"])

        for ctrl_name in ctrl_all_show.keys():
            ctrl_data = ctrl_all_show[ctrl_name]
            bat_count = int(ctrl_data.get('Battery/Capacitor Count', 0))
            if bat_count == 0:
                continue

            sys_id = _sys_id_of_ctrl_data(ctrl_data)

            battery_status = _hp_battery_status_to_lsm(ctrl_data)

            if ctrl_data["Cache Backup Power Source"] == "Capacitors":
                battery_type = Battery.TYPE_CAPACITOR
            elif ctrl_data["Cache Backup Power Source"] == "Batteries":
                battery_type = Battery.TYPE_CHEMICAL
            else:
                battery_type = Battery.TYPE_UNKNOWN

            for counter in range(0, bat_count):
                lsm_bs.append(
                    Battery(
                        "%s_BAT_%d" % (sys_id, counter),
                        "Battery %d of %s" % (counter, ctrl_name),
                        battery_type, battery_status, sys_id,
                        _plugin_data=None))

        return search_property(lsm_bs, search_key, search_value)

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

    @_handle_errors
    def volume_cache_info(self, volume, flags=Client.FLAG_RSVD):
        """
        Depend on command:
            hpssacli ctrl slot=0 show config detail
        """
        flag_battery_ok = False
        flag_ram_ok = False

        (ctrl_num, array_num, ld_num) = self._cal_of_lsm_vol(volume)
        ctrl_data = self._sacli_exec(
            ["ctrl", "slot=%s" % ctrl_num, "show", "config", "detail"]
            ).values()[0]

        lsm_bats = self.batteries()
        for lsm_bat in lsm_bats:
            if lsm_bat.status == Battery.STATUS_OK:
                flag_battery_ok = True

        if 'Total Cache Size' in ctrl_data and \
           _hp_size_to_lsm(ctrl_data['Total Cache Size']) > 0 and \
           ctrl_data['Cache Status'] == 'OK':
            flag_ram_ok = True

        ld_info = ctrl_data.get(
            "Array: %s" % array_num, {}).get("Logical Drive: %s" % ld_num, {})

        if not ld_info:
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME, "Volume not found")

        if ld_info['Caching'] == 'Disabled':
            write_cache_policy = Volume.WRITE_CACHE_POLICY_WRITE_THROUGH
            write_cache_status = Volume.WRITE_CACHE_STATUS_WRITE_THROUGH
            read_cache_policy = Volume.READ_CACHE_POLICY_DISABLED
            read_cache_status = Volume.READ_CACHE_STATUS_DISABLED
        elif ld_info['Caching'] == 'Enabled':
            read_cache_policy = Volume.READ_CACHE_POLICY_ENABLED
            if ctrl_data.get('No-Battery Write Cache', '') == 'Enabled':
                write_cache_policy = Volume.WRITE_CACHE_POLICY_WRITE_BACK
                if flag_ram_ok:
                    write_cache_status = Volume.WRITE_CACHE_STATUS_WRITE_BACK
                    read_cache_status = Volume.READ_CACHE_STATUS_ENABLED
                else:
                    write_cache_status = \
                        Volume.WRITE_CACHE_STATUS_WRITE_THROUGH
                    read_cache_status = Volume.READ_CACHE_STATUS_DISABLED
            else:
                write_cache_policy = Volume.WRITE_CACHE_POLICY_AUTO
                if flag_ram_ok:
                    read_cache_status = Volume.READ_CACHE_STATUS_ENABLED
                    if flag_battery_ok:
                        write_cache_status = \
                            Volume.WRITE_CACHE_STATUS_WRITE_BACK
                    else:
                        write_cache_status = \
                            Volume.WRITE_CACHE_POLICY_WRITE_THROUGH
                else:
                    read_cache_status = Volume.READ_CACHE_STATUS_DISABLED
                    write_cache_status = \
                        Volume.WRITE_CACHE_STATUS_WRITE_THROUGH
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Unknown 'Caching' property of logical volume %d" %
                           ld_num)

        if ctrl_data['Drive Write Cache'] == 'Disabled':
            phy_disk_cache = Volume.PHYSICAL_DISK_CACHE_DISABLED
        elif ctrl_data['Drive Write Cache'] == 'Enabled':
            phy_disk_cache = Volume.PHYSICAL_DISK_CACHE_ENABLED
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Unknown 'Drive Write Cache' property of "
                           "logical volume %d" % ld_num)

        return [write_cache_policy, write_cache_status, read_cache_policy,
                read_cache_status, phy_disk_cache]

    def _is_ssd_volume(self, volume):
        ssd_disk_ids = list(d.id for d in self.disks()
                            if d.disk_type == Disk.TYPE_SSD)
        pool = self.pools(search_key='id', search_value=volume.pool_id)[0]
        disk_ids = self.pool_member_info(pool)[2]
        return len(set(disk_ids) & set(ssd_disk_ids)) != 0

    @_handle_errors
    def volume_physical_disk_cache_update(self, volume, pdc,
                                          flags=Client.FLAG_RSVD):
        """
        Depending on "hpssacli ctrl slot=3 modify dwc=<disable|enable>"
        command.
        This will change all volumes' setting on physical disk cache.
        """
        ctrl_num = self._cal_of_lsm_vol(volume)[0]

        cmd = ["ctrl", "slot=%s" % ctrl_num, "modify"]

        if pdc == Volume.PHYSICAL_DISK_CACHE_ENABLED:
            cmd.append("dwc=enable")
        elif pdc == Volume.PHYSICAL_DISK_CACHE_DISABLED:
            cmd.append("dwc=disable")
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Got unknown pdc: %d" % pdc)
        self._sacli_exec(cmd, flag_force=True, flag_convert=False)

    @_handle_errors
    def volume_write_cache_policy_update(self, volume, wcp,
                                         flags=Client.FLAG_RSVD):
        """
        Depending on these commands:
            To disable both read and write cache:
                hpssacli ctrl slot=3 ld 1 modify aa=disable
            To enable no battery write cache:
                hpssacli ctrl slot=0 modify nbwc=enable
        """
        (ctrl_num, array_num, ld_num) = self._cal_of_lsm_vol(volume)

        cmd1 = ['ctrl', 'slot=%s' % ctrl_num, 'ld', ld_num]
        cmd2 = []
        if wcp == Volume.WRITE_CACHE_POLICY_WRITE_BACK:
            cmd1.extend(['modify', 'aa=enable'])
            cmd2 = ['ctrl', 'slot=%s' % ctrl_num, 'modify', 'nbwc=enable']
        elif wcp == Volume.WRITE_CACHE_POLICY_AUTO:
            cmd1.extend(['modify', 'aa=enable'])
            cmd2 = ['ctrl', 'slot=%s' % ctrl_num, 'modify', 'nbwc=disable']
        elif wcp == Volume.WRITE_CACHE_POLICY_WRITE_THROUGH:
            cmd1.extend(['modify', 'aa=disable'])
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Got unknown wcp: %d" % wcp)

        try:
            self._sacli_exec(cmd1, flag_force=True, flag_convert=False)
            if cmd2:
                self._sacli_exec(cmd2, flag_force=True, flag_convert=False)
        except ExecError as exec_error:
            # Check whether we got SSD volume
            if self._is_ssd_volume(volume):
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "HP SmartArray does not allow changing SSD volume's "
                    "cache policy while SmartPath is enabled")
            raise exec_error

    @_handle_errors
    def volume_read_cache_policy_update(self, volume, rcp,
                                        flags=Client.FLAG_RSVD):
        """
        Depending on this command:
            To disable both read and write cache:
                hpssacli ctrl slot=3 ld 1 modify aa=disable
        """
        (ctrl_num, array_num, ld_num) = self._cal_of_lsm_vol(volume)

        cmd = ['ctrl', 'slot=%s' % ctrl_num, 'ld', ld_num, 'modify']
        if rcp == Volume.READ_CACHE_POLICY_DISABLED:
            cmd.append('aa=disable')
        elif rcp == Volume.READ_CACHE_POLICY_ENABLED:
            cmd.append('aa=enable')
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Got unknown rcp: %d" % rcp)
        try:
            self._sacli_exec(cmd, flag_force=True, flag_convert=False)
        except ExecError as exec_error:
            # Check whether we got SSD volume
            if self._is_ssd_volume(volume):
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "HP SmartArray does not allow changing SSD volume's "
                    "cache policy while SmartPath is enabled")
            raise exec_error
