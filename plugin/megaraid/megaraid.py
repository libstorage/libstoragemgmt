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
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#
# Author: Gris Ge <fge@redhat.com>

import os
import json
from string import strip
import re
import errno

from lsm import (uri_parse, search_property, size_human_2_size_bytes,
                 Capabilities, LsmError, ErrorNumber, System, Client,
                 Disk, VERSION, search_property, IPlugin, Pool)

from lsm.plugin.megaraid.utils import cmd_exec, ExecError

_FLAG_RSVD = Client.FLAG_RSVD


# Naming scheme
#   mega_sys_path   /c0
#   mega_disk_path  /c0/e64/s0


def _handle_errors(method):
    def _wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except LsmError:
            raise
        except KeyError as key_error:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Expected key missing from MegaRAID storcli output:%s" %
                key_error)
        except ExecError as exec_error:
            raise LsmError(ErrorNumber.PLUGIN_BUG, str(exec_error))
        except Exception as common_error:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Got unexpected error %s" % common_error)

    return _wrapper


def _blk_count_of(mega_disk_size):
    blk_count_regex = re.compile("(0x[0-9a-f]+) Sectors")
    blk_count_search = blk_count_regex.search(mega_disk_size)
    if blk_count_search:
        return int(blk_count_search.group(1), 16)
    return Disk.BLOCK_COUNT_NOT_FOUND


def _disk_type_of(disk_show_basic_dict):
    """
    Return the 'Drive /c0/e64/s0' entry of '/c0/e64/s0 show all'
    """
    disk_media = disk_show_basic_dict['Med']
    disk_interface = disk_show_basic_dict['Intf']
    if disk_media == 'HDD':
        if disk_interface == 'SATA':
            return Disk.TYPE_SATA
        elif disk_interface == 'SAS':
            return Disk.TYPE_SAS
        elif disk_interface == 'Parallel SCSI':
            return Disk.TYPE_SCSI
        elif disk_interface == 'FC':
            return Disk.TYPE_FC
        else:
            return Disk.TYPE_HDD
    elif disk_media == 'SSD':
        return Disk.TYPE_SSD

    return Disk.TYPE_UNKNOWN

_DISK_STATE_MAP = {
    'Onln': Disk.STATUS_OK,
    'Offln': Disk.STATUS_ERROR,
    'GHS': Disk.STATUS_SPARE_DISK | Disk.STATUS_OK,
    'DHS': Disk.STATUS_SPARE_DISK | Disk.STATUS_OK,
    'UGood': Disk.STATUS_STOPPED | Disk.STATUS_OK,
    'UBad': Disk.STATUS_STOPPED | Disk.STATUS_ERROR,
}


def _disk_status_of(disk_show_basic_dict, disk_show_stat_dict):
    if disk_show_stat_dict['Media Error Count'] or \
       disk_show_stat_dict['Other Error Count'] or \
       disk_show_stat_dict['S.M.A.R.T alert flagged by drive'] != 'No':
        return Disk.STATUS_ERROR

    if disk_show_stat_dict['Predictive Failure Count']:
        return Disk.STATUS_PREDICTIVE_FAILURE

    if disk_show_basic_dict['Sp'] == 'D':
        return Disk.STATUS_STOPPED

    if disk_show_basic_dict['Sp'] == 'F':
        return Disk.STATUS_OTHER

    return _DISK_STATE_MAP.get(
        disk_show_basic_dict['State'], Disk.STATUS_UNKNOWN)


def _mega_size_to_lsm(mega_size):
    """
    LSI Using 'TB, GB, MB, KB' and etc, for LSM, they are 'TiB' and etc.
    Return int of block bytes
    """
    re_regex = re.compile("^([0-9\.]+) ([EPTGMK])B$")
    re_match = re_regex.match(mega_size)
    if re_match:
        return size_human_2_size_bytes(
            "%s%siB" % (re_match.group(1), re_match.group(2)))

    raise LsmError(
        ErrorNumber.PLUGIN_BUG,
        "_mega_size_to_lsm(): Got unexpected LSI size string %s" %
        mega_size)


_POOL_STATUS_MAP = {
    'Onln': Pool.STATUS_OK,
    'Dgrd': Pool.STATUS_DEGRADED,
    'Pdgd': Pool.STATUS_DEGRADED,
    'Offln': Pool.STATUS_ERROR,
    'Rbld': Pool.STATUS_RECONSTRUCTING,
    'Optl': Pool.STATUS_OK,
    # TODO(Gris Ge): The 'Optl' is undocumented, check with LSI.
}


def _pool_status_of(dg_top):
    """
    Return status
    """
    if dg_top['State'] in _POOL_STATUS_MAP.keys():
        return _POOL_STATUS_MAP[dg_top['State']]
    return Pool.STATUS_UNKNOWN


def _pool_id_of(dg_id, sys_id):
    return "%s:DG%s" % (sys_id, dg_id)


class MegaRAID(IPlugin):
    _DEFAULT_MDADM_BIN_PATHS = [
        "/opt/MegaRAID/storcli/storcli64", "/opt/MegaRAID/storcli/storcli"]
    _CMD_JSON_OUTPUT_SWITCH = 'J'

    def __init__(self):
        self._storcli_bin = None

    def _find_storcli(self):
        """
        Try _DEFAULT_MDADM_BIN_PATHS
        """
        for cur_path in MegaRAID._DEFAULT_MDADM_BIN_PATHS:
            if os.path.lexists(cur_path):
                self._storcli_bin = cur_path

        if not self._storcli_bin:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "MegaRAID storcli is not installed correctly")

    @_handle_errors
    def plugin_register(self, uri, password, timeout, flags=Client.FLAG_RSVD):
        if os.geteuid() != 0:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "This plugin requires root privilege both daemon and client")
        uri_parsed = uri_parse(uri)
        self._storcli_bin = uri_parsed.get('parameters', {}).get('storcli')
        if not self._storcli_bin:
            self._find_storcli()

        # change working dir to "/tmp" as storcli will create a log file
        # named as 'MegaSAS.log'.
        os.chdir("/tmp")
        self._storcli_exec(['-v'], flag_json=False)

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
        return "LSI MegaRAID Plugin", VERSION

    @_handle_errors
    def time_out_set(self, ms, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    @_handle_errors
    def time_out_get(self, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    @_handle_errors
    def capabilities(self, system, flags=_FLAG_RSVD):
        cur_lsm_syss = self.systems()
        if system.id not in list(s.id for s in cur_lsm_syss):
            raise LsmError(
                ErrorNumber.NOT_FOUND_SYSTEM,
                "System not found")
        cap = Capabilities()
        cap.set(Capabilities.DISKS)
        return cap

    def _storcli_exec(self, storcli_cmds, flag_json=True):
        storcli_cmds.insert(0, self._storcli_bin)
        if flag_json:
            storcli_cmds.append(MegaRAID._CMD_JSON_OUTPUT_SWITCH)
        try:
            output = cmd_exec(storcli_cmds)
        except OSError as os_error:
            if os_error.errno == errno.ENOENT:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "storcli binary '%s' is not exist or executable." %
                    self._storcli_bin)
            else:
                raise

        if flag_json:
            output_dict = json.loads(output)
            ctrl_output = output_dict.get('Controllers')
            if len(ctrl_output) != 1:
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "_storcli_exec(): Unexpected output from MegaRAID "
                    "storcli: %s" % output_dict)

            rc_status = ctrl_output[0].get('Command Status')
            if rc_status.get('Status') != 'Success':
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "MegaRAID storcli failed with error %d: %s" %
                    (rc_status['Status Code'], rc_status['Description']))
            real_data = ctrl_output[0].get('Response Data')
            if real_data and 'Response Data' in real_data.keys():
                return real_data['Response Data']

            return real_data
        else:
            return output

    def _ctrl_count(self):
        return self._storcli_exec(
            ["show", "ctrlcount"]).get("Controller Count")

    def _lsm_status_of_ctrl(self, ctrl_num):
        ctrl_health_output = self._storcli_exec(
            ["/c%d" % ctrl_num, 'show', 'health', 'all'])
        health_info = ctrl_health_output['Controller Health Info']
        # TODO(Gris Ge): Try pull a disk off to check whether this change.
        if health_info['Overall Health'] == 'GOOD':
            return System.STATUS_OK, ''

        return System.STATUS_UNKNOWN, "%s reason code %s" % (
            health_info['Overall Health'],
            health_info['Reason Code'])

    def _sys_id_of_ctrl_num(self, ctrl_num, ctrl_show_output=None):
        if ctrl_show_output is None:
            ctrl_show_output = self._storcli_exec(["/c%d" % ctrl_num, "show"])
        sys_id = ctrl_show_output['Serial Number']
        if not sys_id:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "_sys_id_of_ctrl_num(): Fail to get system id: %s" %
                ctrl_show_output.items())
        else:
            return sys_id

    @_handle_errors
    def systems(self, flags=0):
        rc_lsm_syss = []
        for ctrl_num in range(self._ctrl_count()):
            ctrl_show_output = self._storcli_exec(["/c%d" % ctrl_num, "show"])
            sys_id = self._sys_id_of_ctrl_num(ctrl_num, ctrl_show_output)
            sys_name = "%s %s %s:%s:%s" % (
                ctrl_show_output['Product Name'],
                ctrl_show_output['Host Interface'],
                format(ctrl_show_output['Bus Number'], '02x'),
                format(ctrl_show_output['Device Number'], '02x'),
                format(ctrl_show_output['Function Number'], '02x'))
            (status, status_info) = self._lsm_status_of_ctrl(ctrl_num)
            plugin_data = "/c%d"
            # Since PCI slot sequence might change.
            # This string just stored for quick system verification.

            rc_lsm_syss.append(
                System(sys_id, sys_name, status, status_info, plugin_data))

        return rc_lsm_syss

    @_handle_errors
    def disks(self, search_key=None, search_value=None, flags=_FLAG_RSVD):
        rc_lsm_disks = []
        mega_disk_path_regex = re.compile(
            r"^Drive (\/c[0-9]+\/e[0-9]+\/s[0-9]+) - Detailed Information$")

        for ctrl_num in range(self._ctrl_count()):
            sys_id = self._sys_id_of_ctrl_num(ctrl_num)

            disk_show_output = self._storcli_exec(
                ["/c%d/eall/sall" % ctrl_num, "show", "all"])
            for drive_name in disk_show_output.keys():
                re_match = mega_disk_path_regex.match(drive_name)
                if not re_match:
                    continue

                mega_disk_path = re_match.group(1)
                # Assuming only 1 disk attached to each slot.
                disk_show_basic_dict = disk_show_output[
                    "Drive %s" % mega_disk_path][0]
                disk_show_attr_dict = disk_show_output[drive_name][
                    'Drive %s Device attributes' % mega_disk_path]
                disk_show_stat_dict = disk_show_output[drive_name][
                    'Drive %s State' % mega_disk_path]

                disk_id = disk_show_attr_dict['SN'].strip()
                disk_name = "Disk %s %s %s" % (
                    disk_show_basic_dict['DID'],
                    disk_show_attr_dict['Manufacturer Id'].strip(),
                    disk_show_attr_dict['Model Number'])
                disk_type = _disk_type_of(disk_show_basic_dict)
                blk_size = size_human_2_size_bytes(
                    disk_show_basic_dict['SeSz'])
                blk_count = _blk_count_of(disk_show_attr_dict['Coerced size'])
                status = _disk_status_of(
                    disk_show_basic_dict, disk_show_stat_dict)

                plugin_data = mega_disk_path

                rc_lsm_disks.append(
                    Disk(
                        disk_id, disk_name, disk_type, blk_size, blk_count,
                        status, sys_id, plugin_data))

        return search_property(rc_lsm_disks, search_key, search_value)

    @staticmethod
    def _dg_free_size(dg_num, free_space_list):
        """
        Get information from 'FREE SPACE DETAILS' of /c0/dall show all.
        """
        for free_space in free_space_list:
            if int(free_space['DG']) == int(dg_num):
                return _mega_size_to_lsm(free_space['Size'])

        return 0

    def _dg_top_to_lsm_pool(self, dg_top, free_space_list, ctrl_num):
        sys_id = self._sys_id_of_ctrl_num(ctrl_num)
        pool_id = _pool_id_of(dg_top['DG'], sys_id)
        name = '%s Disk Group %s' % (dg_top['Type'], dg_top['DG'])
        elem_type = Pool.ELEMENT_TYPE_VOLUME | Pool.ELEMENT_TYPE_VOLUME_FULL
        unsupported_actions = 0
        # TODO(Gris Ge): contact LSI to get accurate total space and free
        #                space. The size we are using here is not what host
        #                got.
        total_space = _mega_size_to_lsm(dg_top['Size'])
        free_space = MegaRAID._dg_free_size(dg_top['DG'], free_space_list)
        status = _pool_status_of(dg_top)
        status_info = ''
        if status == Pool.STATUS_UNKNOWN:
            status_info = dg_top['State']

        plugin_data = "/c%d/d%s" % (ctrl_num, dg_top['DG'])

        return Pool(
            pool_id, name, elem_type, unsupported_actions,
            total_space, free_space, status, status_info,
            sys_id, plugin_data)

    @_handle_errors
    def pools(self, search_key=None, search_value=None,
              flags=Client.FLAG_RSVD):
        lsm_pools = []
        for ctrl_num in range(self._ctrl_count()):
            dg_show_output = self._storcli_exec(
                ["/c%d/dall" % ctrl_num, "show", "all"])
            free_space_list = dg_show_output.get('FREE SPACE DETAILS', [])
            for dg_top in dg_show_output['TOPOLOGY']:
                if dg_top['Arr'] != '-':
                    continue
                if dg_top['DG'] == '-':
                    continue
                lsm_pools.append(
                    self._dg_top_to_lsm_pool(
                        dg_top, free_space_list, ctrl_num))

        return search_property(lsm_pools, search_key, search_value)
