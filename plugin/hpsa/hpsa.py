# Copyright (C) 2015 Red Hat, Inc.
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

import os
import errno
import re

from lsm import (
    IPlugin, Client, Capabilities, VERSION, LsmError, ErrorNumber, uri_parse,
    System, Pool, size_human_2_size_bytes, search_property, Volume, Disk)

from lsm.plugin.hpsa.utils import cmd_exec, ExecError, file_read


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
        if key_name in hp_ctrl_status and hp_ctrl_status[key_name] != 'OK':
            # TODO(Gris Ge): Beg HP for possible values
            status = System.STATUS_OTHER
            status_info += hp_ctrl_status[key_name]

    if status != System.STATUS_OTHER:
        status = System.STATUS_OK

    return status, status_info


def _parse_hpssacli_output(output):
    """
    Got a output string of hpssacli to dictionary(nested).
    Skipped these line:
        1. Starts with 'Note:'
           This is just a message right after controller. We don't neet it
           yet.
        2. The 'Physical Drives' line.
           It should indented after 'Internal Drive Cage' like.
           If not ignored, we might got duplication line error.
           After ignored, it's phsycial disks will directly stored as
           key of 'Internal Drive Cage' dictionary.
    """
    output_lines = [
        l for l in output.split("\n")
        if l and not l.startswith('Note:') and
        not l.strip() == 'Physical Drives']

    data = {}

    # Detemine indention level
    top_indention_level = sorted(
        set(
            len(line) - len(line.lstrip())
            for line in output_lines))[0]

    indent_2_data = {
        top_indention_level: data
    }

    for line_num in range(len(output_lines)):
        cur_line = output_lines[line_num]
        if cur_line.strip() == 'None attached':
            continue
        if line_num + 1 == len(output_lines):
            nxt_line = ''
        else:
            nxt_line = output_lines[line_num + 1]

        cur_indent_count = len(cur_line) - len(cur_line.lstrip())
        nxt_indent_count = len(nxt_line) - len(nxt_line.lstrip())

        cur_line_splitted = cur_line.split(": ")
        cur_data_pointer = indent_2_data[cur_indent_count]

        if nxt_indent_count > cur_indent_count:
            nxt_line_splitted = nxt_line.split(": ")
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
            if len(cur_line_splitted) == 1:
                cur_data_pointer[cur_line.lstrip()] = None
            else:
                cur_data_pointer[cur_line_splitted[0].lstrip()] = \
                    ": ".join(cur_line_splitted[1:]).strip()
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
        return cap

    def _sacli_exec(self, sacli_cmds, flag_convert=True):
        """
        If flag_convert is True, convert data into dict.
        """
        sacli_cmds.insert(0, self._sacli_bin)
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
            sys_id = ctrl_data['Serial Number']
            (status, status_info) = _sys_status_of(ctrl_all_status[ctrl_name])

            plugin_data = "%s" % ctrl_data['Slot']

            rc_lsm_syss.append(
                System(sys_id, ctrl_name, status, status_info, plugin_data))

        return rc_lsm_syss

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
            sys_id = ctrl_data['Serial Number']
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
        ld_num = hp_ld_name[len("Logical Drive: "):]
        vpd83 = hp_ld['Unique Identifier'].lower()
        # No document or command output indicate block size
        # of volume. So we try to read from linux kernel, if failed
        # try 512 and roughly calculate the sector count.
        regex_match = re.compile("/dev/(sd[a-z]+)").search(hp_ld['Disk Name'])
        vol_name = hp_ld_name
        if regex_match:
            sd_name = regex_match.group(1)
            block_size = int(file_read(
                "/sys/block/%s/queue/logical_block_size" % sd_name))
            num_of_blocks = int(file_read("/sys/block/%s/size" % sd_name))
            vol_name += ": /dev/%s" % sd_name
        else:
            block_size = 512
            num_of_blocks = int(_hp_size_to_lsm(hp_ld['Size']) / block_size)

        plugin_data = "%s:%s:%s" % (ctrl_num, array_num, ld_num)

        # HP SmartArray does not allow disabling volume.
        return Volume(
            vpd83, vol_name, vpd83, block_size, num_of_blocks,
            Volume.ADMIN_STATE_ENABLED, sys_id, pool_id, plugin_data)

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
            sys_id = ctrl_data['Serial Number']
            for key_name in ctrl_data.keys():
                if not key_name.startswith("Array:"):
                    continue
                pool_id = _pool_id_of(sys_id, key_name)
                array_num = key_name[len('Array: '):]
                for array_key_name in ctrl_data[key_name].keys():
                    if not array_key_name.startswith("Logical Drive"):
                        continue
                    lsm_vols.append(
                        SmartArray._hp_ld_to_lsm_vol(
                            ctrl_data[key_name][array_key_name],
                            pool_id, sys_id, ctrl_num, array_num,
                            array_key_name))

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
        status = _disk_status_of(hp_disk, flag_free)
        plugin_data = "%s:%s" % (ctrl_num, disk_num)

        return Disk(
            disk_id, disk_name, disk_type, blk_size, blk_count,
            status, sys_id, plugin_data)

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
            sys_id = ctrl_data['Serial Number']
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

                if key_name == 'unassigned':
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

        try:
            self._sacli_exec(cmds, flag_convert=False)
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

        sys_id = sys_output.values()[0]['Serial Number']
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
