# Copyright (C) 2015-2017 Red Hat, Inc.
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
import json
import re
import errno
import math
import tempfile
import shutil

from lsm import (uri_parse, search_property, size_human_2_size_bytes,
                 Capabilities, LsmError, ErrorNumber, System, Client,
                 Disk, VERSION, IPlugin, Pool, Volume, Battery, int_div)

from lsm.plugin.megaraid.utils import cmd_exec, ExecError

# Naming scheme
#   mega_sys_path   /c0
#   mega_disk_path  /c0/e64/s0
#   lsi_disk_id    0:64:0


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
    'UGood': Disk.STATUS_FREE | Disk.STATUS_OK,
    'UBad': Disk.STATUS_FREE | Disk.STATUS_ERROR,
    'Rbld': Disk.STATUS_RECONSTRUCT,
    'JBOD': Disk.STATUS_OK,
}


def _disk_status_of(disk_show_basic_dict, disk_show_stat_dict):
    disk_status = _DISK_STATE_MAP.get(disk_show_basic_dict['State'], 0)

    if disk_show_stat_dict['Media Error Count'] or \
       disk_show_stat_dict['S.M.A.R.T alert flagged by drive'] != 'No':
        disk_status = (disk_status & ~Disk.STATUS_OK) | Disk.STATUS_ERROR

    elif disk_show_stat_dict['Predictive Failure Count']:
        disk_status = (disk_status & ~Disk.STATUS_OK) | \
            Disk.STATUS_PREDICTIVE_FAILURE

    if disk_show_basic_dict['Sp'] == 'D':
        disk_status |= Disk.STATUS_STOPPED

    if disk_show_basic_dict['Sp'] == 'F':
        disk_status |= Disk.STATUS_OTHER

    if disk_status == 0:
        disk_status = Disk.STATUS_UNKNOWN

    return disk_status


def _mega_size_to_lsm(mega_size):
    """
    LSI Using 'TB, GB, MB, KB' and etc, for LSM, they are 'TiB' and etc.
    Return int of block bytes
    """
    re_regex = re.compile("^([0-9.]+) *([EPTGMK])B$")
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
    'Dgrd': Pool.STATUS_DEGRADED | Pool.STATUS_OK,
    'Pdgd': Pool.STATUS_DEGRADED | Pool.STATUS_OK,
    'Offln': Pool.STATUS_ERROR,
    'Rbld': Pool.STATUS_RECONSTRUCTING | Pool.STATUS_DEGRADED | Pool.STATUS_OK,
    'Optl': Pool.STATUS_OK,
}


def _pool_status_of(dg_top):
    """
    Return status
    """
    if dg_top['State'] in list(_POOL_STATUS_MAP.keys()):
        return _POOL_STATUS_MAP[dg_top['State']]
    return Pool.STATUS_UNKNOWN


def _pool_id_of(dg_id, sys_id):
    return "%s:DG%s" % (sys_id, dg_id)


_RAID_TYPE_MAP = {
    'RAID0': Volume.RAID_TYPE_RAID0,
    'RAID1': Volume.RAID_TYPE_RAID1,
    'RAID5': Volume.RAID_TYPE_RAID5,
    'RAID6': Volume.RAID_TYPE_RAID6,
    'RAID00': Volume.RAID_TYPE_RAID0,
    # Some MegaRAID only support max 16 disks in each span.
    # To support 16+ disks in on group, MegaRAI has RAID00 or even RAID000.
    # All of them are considered as RAID0
    'RAID10': Volume.RAID_TYPE_RAID10,
    'RAID50': Volume.RAID_TYPE_RAID50,
    'RAID60': Volume.RAID_TYPE_RAID60,
}

_LSM_RAID_TYPE_CONV = {
    Volume.RAID_TYPE_RAID0: 'RAID0',
    Volume.RAID_TYPE_RAID1: 'RAID1',
    Volume.RAID_TYPE_RAID5: 'RAID5',
    Volume.RAID_TYPE_RAID6: 'RAID6',
    Volume.RAID_TYPE_RAID50: 'RAID50',
    Volume.RAID_TYPE_RAID60: 'RAID60',
    Volume.RAID_TYPE_RAID10: 'RAID10',
}


def _mega_raid_type_to_lsm(vd_basic_info, vd_prop_info):
    raid_type = _RAID_TYPE_MAP.get(
        vd_basic_info['TYPE'], Volume.RAID_TYPE_UNKNOWN)

    # In LSI, four disks or more RAID1 is actually a RAID10.
    if raid_type == Volume.RAID_TYPE_RAID1 and \
       int(vd_prop_info['Number of Drives Per Span']) >= 4:
        raid_type = Volume.RAID_TYPE_RAID10

    return raid_type


def _lsm_raid_type_to_mega(lsm_raid_type):
    try:
        return _LSM_RAID_TYPE_CONV[lsm_raid_type]
    except KeyError:
        raise LsmError(
            ErrorNumber.NO_SUPPORT,
            "RAID type %d not supported" % lsm_raid_type)


def _disk_rpm_of(disk_show_basic_dict):
    disk_media = disk_show_basic_dict['Med']
    if disk_media == 'HDD':
        return Disk.RPM_ROTATING_UNKNOWN_SPEED
    elif disk_media == 'SSD':
        return Disk.RPM_NON_ROTATING_MEDIUM
    return Disk.RPM_UNKNOWN


def _disk_link_type_of(disk_show_basic_dict):
    """
    Return the 'Drive /c0/e64/s0' entry of '/c0/e64/s0 show all'
    """
    disk_interface = disk_show_basic_dict['Intf']
    if disk_interface == 'SATA':
        return Disk.LINK_TYPE_ATA
    elif disk_interface == 'SAS':
        return Disk.LINK_TYPE_SAS
    elif disk_interface == 'Parallel SCSI':
        return Disk.LINK_TYPE_UNKNOWN
    elif disk_interface == 'FC':
        return Disk.LINK_TYPE_FC

    return Disk.LINK_TYPE_UNKNOWN


def _fix_bbu_cv_output(output):
    """
    Command 'storcli /c0/bbu show all' and 'storcli /c0/cv show all' provide
    output like these list:
            {
                "Property" : "Type",
                "Value" : "BBU"
            },
            {
                "Property" : "Voltage",
                "Value" : "4003 mV"
            },
    Convert them to dict like:
        {
            'Type': 'BBU',
            'Voltage': '4003 mV',
        }
    """
    tmp_dict = dict()
    for a in output:
        tmp_dict[a['Property']] = a['Value']
    return tmp_dict


_BBU_STATUS_MAP = {
    "Optimal": Battery.STATUS_OK,
    "Missing": Battery.STATUS_ERROR,
    "Failed": Battery.STATUS_ERROR,
    "Degraded (need attention)": Battery.STATUS_DEGRADED,
    # ^ These values are copy from LSI document, except Optimal all other
    #   values are not tested.
}


def _bbu_status_to_lsm(bbu_status):
    return _BBU_STATUS_MAP.get(bbu_status, Battery.STATUS_UNKNOWN)


_CV_STATUS_MAP = {
    "Optimal": Battery.STATUS_OK,
    # TODO(Gris Ge): Need document for all possible values of CacheVault
    # status.
}


def _cv_status_to_lsm(cv_status):
    return _CV_STATUS_MAP.get(cv_status, Battery.STATUS_UNKNOWN)


def _mega_bbu_to_lsm(sys_id, bbu_show_all_output):
    design_info = _fix_bbu_cv_output(bbu_show_all_output['BBU_Design_Info'])
    bbu_info = _fix_bbu_cv_output(bbu_show_all_output['BBU_Info'])

    battery_type = Battery.TYPE_CHEMICAL
    name = "LSI BBU: %s %s %s %s %s %s" % (
        design_info['Manufacture Name'],
        design_info['Device Name'],
        design_info['Device Chemistry'],
        design_info['Design Capacity'],
        design_info['Design Voltage'],
        design_info['Date of Manufacture'])
    battery_id = "%s_BBU_%s" % (
        sys_id, design_info['Serial Number'])
    status = _bbu_status_to_lsm(bbu_info['Battery State'])
    plugin_data = None

    return Battery(battery_id, name, battery_type, status, sys_id, plugin_data)


def _mega_cv_to_lsm(sys_id, cv_show_all_output):
    design_info = _fix_bbu_cv_output(cv_show_all_output['Design_Info'])
    cv_info = _fix_bbu_cv_output(cv_show_all_output['Cachevault_Info'])

    battery_type = Battery.TYPE_CAPACITOR
    battery_id = "%s_CV_%s" % (sys_id, design_info['Serial Number'])
    name = "LSI CacheVault: %s %s %s" % (
        design_info['Device Name'],
        design_info['Design Capacity'],
        design_info['Date of Manufacture'])
    status = _cv_status_to_lsm(cv_info['State'])

    plugin_data = None

    return Battery(battery_id, name, battery_type, status, sys_id, plugin_data)


def _vd_path_of_lsm_vol(lsm_vol):
    if not lsm_vol.plugin_data:
        raise LsmError(
            ErrorNumber.INVALID_ARGUMENT,
            "Illegal input volume argument: missing plugin_data property")
    return str(lsm_vol.plugin_data)


class MegaRAID(IPlugin):
    _DEFAULT_BIN_PATHS = [
        "/opt/MegaRAID/storcli/storcli64", "/opt/MegaRAID/storcli/storcli",
        "/opt/MegaRAID/perccli/perccli64", "/opt/MegaRAID/perccli/perccli"]
    _CMD_JSON_OUTPUT_SWITCH = 'J'

    def __init__(self):
        self._storcli_bin = None
        self._tmo_ms = 3000    # TODO(Gris Ge): Not implemented yet.
        self._tmp_dir = tempfile.mkdtemp()

    def __del__(self):
        shutil.rmtree(self._tmp_dir)

    def _find_storcli(self):
        """
        Try _DEFAULT_BIN_PATHS
        """
        working_bins = []
        for cur_path in MegaRAID._DEFAULT_BIN_PATHS:
            if os.path.lexists(cur_path) and os.access(cur_path, os.X_OK):
                self._storcli_bin = cur_path
                try:
                    self._storcli_exec(['-v'], flag_json=False)
                    working_bins.append(cur_path)
                except Exception:
                    pass

        if len(working_bins) == 1:
            self._storcli_bin = working_bins[0]
            return
        # Server might have both storcli and perccli installed.
        elif len(working_bins) >= 2:
            for cur_path in working_bins:
                self._storcli_bin = cur_path
                try:
                    if len(self.systems()) >= 1:
                        return
                except Exception:
                    pass
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Both storcli and perccli are installed, but none "
                           "could find a valid MegaRAID card")

        raise LsmError(
            ErrorNumber.INVALID_ARGUMENT,
            "MegaRAID storcli or perccli is not installed correctly")

    @_handle_errors
    def plugin_register(self, uri, password, timeout, flags=Client.FLAG_RSVD):
        if os.geteuid() != 0:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "This plugin requires root privilege both daemon and client")
        uri_parsed = uri_parse(uri)
        self._storcli_bin = uri_parsed.get('parameters', {}).get('storcli')
        # change working dir to tmp folder as storcli will create a log file
        # named as 'MegaSAS.log'.

        os.chdir(self._tmp_dir)
        if self._storcli_bin:
            self._storcli_exec(['-v'], flag_json=False)
        else:
            self._find_storcli()

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
        self._tmo_ms = ms  # TODO(Gris Ge): Not implemented yet.

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
        cap.set(Capabilities.DISK_VPD83_GET)
        cap.set(Capabilities.VOLUMES)
        cap.set(Capabilities.VOLUME_RAID_INFO)
        cap.set(Capabilities.POOL_MEMBER_INFO)
        cap.set(Capabilities.VOLUME_RAID_CREATE)
        cap.set(Capabilities.BATTERIES)
        cap.set(Capabilities.VOLUME_CACHE_INFO)
        cap.set(Capabilities.VOLUME_PHYSICAL_DISK_CACHE_UPDATE)
        cap.set(Capabilities.VOLUME_WRITE_CACHE_POLICY_UPDATE_WRITE_BACK)
        cap.set(Capabilities.VOLUME_WRITE_CACHE_POLICY_UPDATE_AUTO)
        cap.set(Capabilities.VOLUME_WRITE_CACHE_POLICY_UPDATE_WRITE_THROUGH)
        cap.set(Capabilities.VOLUME_DELETE)
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

        output = re.sub("[^\x20-\x7e]", " ", output)

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
                detail_status = rc_status['Detailed Status'][0]
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "MegaRAID storcli failed with error %d: %s" %
                    (detail_status['ErrCd'], detail_status['ErrMsg']))
            real_data = ctrl_output[0].get('Response Data')
            if real_data and 'Response Data' in list(real_data.keys()):
                return real_data['Response Data']

            return real_data
        else:
            return output

    def _ctrl_count(self):
        ctrl_count = self._storcli_exec(
            ["show", "ctrlcount"]).get("Controller Count")
        if ctrl_count < 1:
            raise LsmError(
                ErrorNumber.NOT_FOUND_SYSTEM,
                "No MegaRAID controller detected by %s" % self._storcli_bin)
        return ctrl_count

    def _lsm_status_of_ctrl(self, ctrl_show_all_output):
        lsi_status_info = ctrl_show_all_output['Status']
        status_info = ''
        status = System.STATUS_UNKNOWN
        if lsi_status_info['Controller Status'] == 'Optimal':
            status = System.STATUS_OK
        else:
            # TODO(Gris Ge): Try pull a disk off to check whether this change.
            status_info = "%s: " % lsi_status_info['Controller Status']
            for key_name in list(lsi_status_info.keys()):
                if key_name == 'Controller Status':
                    continue
                if lsi_status_info[key_name] != 0 and \
                   lsi_status_info[key_name] != 'No' and \
                   lsi_status_info[key_name] != 'NA':
                    status_info += " %s:%s" % (
                        key_name, lsi_status_info[key_name])

        return status, status_info

    def _sys_id_of_ctrl_num(self, ctrl_num, ctrl_show_all_output=None):
        if ctrl_show_all_output is None:
            return self._storcli_exec(
                ["/c%d" % ctrl_num, "show"])['Serial Number']
        else:
            return ctrl_show_all_output['Basics']['Serial Number']

    @_handle_errors
    def systems(self, flags=Client.FLAG_RSVD):
        rc_lsm_syss = []
        for ctrl_num in range(self._ctrl_count()):
            ctrl_show_all_output = self._storcli_exec(
                ["/c%d" % ctrl_num, "show", "all"])
            sys_id = self._sys_id_of_ctrl_num(ctrl_num, ctrl_show_all_output)
            sys_name = "%s %s %s" % (
                ctrl_show_all_output['Basics']['Model'],
                ctrl_show_all_output['Bus']['Host Interface'],
                ctrl_show_all_output['Basics']['PCI Address'])
            (status, status_info) = self._lsm_status_of_ctrl(
                ctrl_show_all_output)
            plugin_data = "/c%d" % ctrl_num
            # Since PCI slot sequence might change.
            # This string just stored for quick system verification.

            fw_ver = "Package: %s, BIOS: %s, FW: %s" % (
                ctrl_show_all_output["Version"]["Firmware Package Build"],
                ctrl_show_all_output["Version"]["Bios Version"],
                ctrl_show_all_output["Version"]["Firmware Version"])

            if ctrl_show_all_output["Capabilities"]["Enable JBOD"] == "Yes":
                mode = System.MODE_HBA
            else:
                mode = System.MODE_HARDWARE_RAID
            # Notes for JBOD/HBA mode of MegaRAID:
            # "storcli /c0/e9/s1 set jbod" require "storcli /c0 set jbod=on"
            # be excuted first.

            rc_lsm_syss.append(
                System(sys_id, sys_name, status, status_info, plugin_data,
                       _fw_version=fw_ver, _mode=mode))

        return rc_lsm_syss

    @_handle_errors
    def disks(self, search_key=None, search_value=None,
              flags=Client.FLAG_RSVD):
        rc_lsm_disks = []
        mega_disk_path_regex = re.compile(
            r"""
                ^Drive \ (
                \/c[0-9]+\/             # Controller ID
                (:?e[0-9]+\/){0,1}      # Enclosure ID(optional)
                s[0-9]+                 # Slot ID
                )\ -\ Detailed\ Information$""", re.X)

        for ctrl_num in range(self._ctrl_count()):
            sys_id = self._sys_id_of_ctrl_num(ctrl_num)

            try:
                disk_show_output = self._storcli_exec(
                    ["/c%d/eall/sall" % ctrl_num, "show", "all"])
            except ExecError:
                disk_show_output = {}

            try:
                disk_show_output.update(
                    self._storcli_exec(
                        ["/c%d/sall" % ctrl_num, "show", "all"]))
            except (ExecError, TypeError):
                pass

            for drive_name in list(disk_show_output.keys()):
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

                plugin_data = "%s:%s" % (
                    ctrl_num, disk_show_basic_dict['EID:Slt'])
                vpd83 = disk_show_attr_dict["WWN"].lower()
                if vpd83 == 'na':
                    vpd = ''
                rpm = _disk_rpm_of(disk_show_basic_dict)
                link_type = _disk_link_type_of(disk_show_basic_dict)

                rc_lsm_disks.append(
                    Disk(
                        disk_id, disk_name, disk_type, blk_size, blk_count,
                        status, sys_id, plugin_data, _vpd83=vpd83, _rpm=rpm,
                        _link_type=link_type))

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

    def _dg_top_to_lsm_pool(self, dg_top, free_space_list, ctrl_num,
                            dg_show_output, is_in_cc):
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

        for dg_drv in dg_show_output['DG Drive LIST']:
            if dg_drv['DG'] != dg_top['DG']:
                continue
            if dg_drv['State'] == 'Rbld':
                status |= Pool.STATUS_RECONSTRUCTING

        if is_in_cc:
            status |= Pool.STATUS_VERIFYING

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
            cc_vd_ids = []
            cc_dg_ids = []
            dg_show_output = self._storcli_exec(
                ["/c%d/dall" % ctrl_num, "show", "all"])
            consist_check_output = self._storcli_exec(
                ["/c%d/vall" % ctrl_num, "show", "cc"])
            free_space_list = dg_show_output.get("FREE SPACE DETAILS", [])

            if "TOPOLOGY" not in dg_show_output:
                continue

            for vd_stat in consist_check_output["VD Operation Status"]:
                if vd_stat["Status"] == "In progress":
                    cc_vd_ids.append(int(vd_stat["VD"]))

            for vd in dg_show_output["VD LIST"]:
                (dg_id, vd_id) = vd["DG/VD"].split("/")
                if int(vd_id) in cc_vd_ids:
                    cc_dg_ids.append(int(dg_id))

            for dg_top in dg_show_output['TOPOLOGY']:
                if dg_top['Arr'] != '-':
                    continue
                if dg_top['DG'] == '-':
                    continue
                lsm_pools.append(
                    self._dg_top_to_lsm_pool(
                        dg_top, free_space_list, ctrl_num, dg_show_output,
                        int(dg_top['DG']) in cc_dg_ids))

        return search_property(lsm_pools, search_key, search_value)

    @staticmethod
    def _vd_to_lsm_vol(vd_id, dg_id, sys_id, vd_basic_info, vd_pd_info_list,
                       vd_prop_info, vd_path):

        vol_id = "%s:VD%d" % (sys_id, vd_id)
        name = "VD %d" % vd_id
        if 'Name' in list(vd_basic_info.keys()) and vd_basic_info['Name']:
            name += ": %s" % vd_basic_info['Name']

        vpd83 = vd_prop_info.get('SCSI NAA Id', '')
        if (len(vpd83) != 0):
            vol_id = vpd83
        block_size = size_human_2_size_bytes(vd_pd_info_list[0]['SeSz'])
        num_of_blocks = vd_prop_info['Number of Blocks']
        admin_state = Volume.ADMIN_STATE_ENABLED
        if vd_prop_info['Exposed to OS'] != 'Yes' or \
           vd_basic_info['Access'] != 'RW':
            admin_state = Volume.ADMIN_STATE_DISABLED
        pool_id = _pool_id_of(dg_id, sys_id)
        plugin_data = vd_path
        return Volume(
            vol_id, name, vpd83, block_size, num_of_blocks, admin_state,
            sys_id, pool_id, plugin_data)

    @_handle_errors
    def volumes(self, search_key=None, search_value=None,
                flags=Client.FLAG_RSVD):
        lsm_vols = []
        for ctrl_num in range(self._ctrl_count()):
            vol_show_output = self._storcli_exec(
                ["/c%d/vall" % ctrl_num, "show", "all"])
            sys_id = self._sys_id_of_ctrl_num(ctrl_num)
            if vol_show_output is None or len(vol_show_output) == 0:
                continue
            for key_name in list(vol_show_output.keys()):
                if key_name.startswith('/c'):
                    vd_basic_info = vol_show_output[key_name][0]
                    (dg_id, vd_id) = vd_basic_info['DG/VD'].split('/')
                    dg_id = int(dg_id)
                    vd_id = int(vd_id)
                    vd_pd_info_list = vol_show_output['PDs for VD %d' % vd_id]

                    vd_prop_info = vol_show_output['VD%d Properties' % vd_id]

                    lsm_vols.append(
                        MegaRAID._vd_to_lsm_vol(
                            vd_id, dg_id, sys_id, vd_basic_info,
                            vd_pd_info_list, vd_prop_info, key_name))

        return search_property(lsm_vols, search_key, search_value)

    @_handle_errors
    def volume_raid_info(self, volume, flags=Client.FLAG_RSVD):
        if not volume.plugin_data:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Ilegal input volume argument: missing plugin_data property")

        vd_path = _vd_path_of_lsm_vol(volume)
        vol_show_output = self._storcli_exec([vd_path, "show", "all"])
        vd_basic_info = vol_show_output[vd_path][0]
        vd_id = int(vd_basic_info['DG/VD'].split('/')[-1])
        vd_prop_info = vol_show_output['VD%d Properties' % vd_id]

        raid_type = _mega_raid_type_to_lsm(vd_basic_info, vd_prop_info)
        strip_size = _mega_size_to_lsm(vd_prop_info['Strip Size'])
        disk_count = (
            int(vd_prop_info['Number of Drives Per Span']) *
            int(vd_prop_info['Span Depth']))
        if raid_type == Volume.RAID_TYPE_RAID0:
            strip_count = disk_count
        elif raid_type == Volume.RAID_TYPE_RAID1:
            strip_count = 1
        elif raid_type == Volume.RAID_TYPE_RAID5:
            strip_count = disk_count - 1
        elif raid_type == Volume.RAID_TYPE_RAID6:
            strip_count = disk_count - 2
        elif raid_type == Volume.RAID_TYPE_RAID50:
            strip_count = (
                (int(vd_prop_info['Number of Drives Per Span']) - 1) *
                int(vd_prop_info['Span Depth']))
        elif raid_type == Volume.RAID_TYPE_RAID60:
            strip_count = (
                (int(vd_prop_info['Number of Drives Per Span']) - 2) *
                int(vd_prop_info['Span Depth']))
        elif raid_type == Volume.RAID_TYPE_RAID10:
            strip_count = (
                int(vd_prop_info['Number of Drives Per Span']) / 2 *
                int(vd_prop_info['Span Depth']))
        else:
            # MegaRAID does not support 15 or 16 yet.
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "volume_raid_info(): Got unexpected RAID type: %s" %
                vd_basic_info['TYPE'])

        return [
            raid_type, strip_size, disk_count, strip_size,
            strip_size * strip_count]

    @_handle_errors
    def pool_member_info(self, pool, flags=Client.FLAG_RSVD):
        lsi_dg_path = pool.plugin_data
        # Check whether pool exists.
        try:
            dg_show_all_output = self._storcli_exec(
                [lsi_dg_path, "show", "all"])
        except ExecError as exec_error:
            try:
                json_output = json.loads(exec_error.stdout)
                detail_error = json_output[
                    'Controllers'][0]['Command Status']['Detailed Status']
            except Exception:
                raise exec_error

            if detail_error and detail_error[0]['Status'] == 'Not found':
                raise LsmError(
                    ErrorNumber.NOT_FOUND_POOL,
                    "Pool not found")
            raise

        ctrl_num = lsi_dg_path.split('/')[1][1:]
        lsm_disk_map = {}
        disk_ids = []
        for lsm_disk in self.disks():
            lsm_disk_map[lsm_disk.plugin_data] = lsm_disk.id

        for dg_disk_info in dg_show_all_output['DG Drive LIST']:
            cur_lsi_disk_id = "%s:%s" % (ctrl_num, dg_disk_info['EID:Slt'])
            if cur_lsi_disk_id in list(lsm_disk_map.keys()):
                disk_ids.append(lsm_disk_map[cur_lsi_disk_id])
            else:
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "pool_member_info(): Failed to find disk id of %s" %
                    cur_lsi_disk_id)

        raid_type = Volume.RAID_TYPE_UNKNOWN
        dg_num = lsi_dg_path.split('/')[2][1:]
        for dg_top in dg_show_all_output['TOPOLOGY']:
            if dg_top['Arr'] == '-' and \
               dg_top['Row'] == '-' and \
               int(dg_top['DG']) == int(dg_num):
                raid_type = _RAID_TYPE_MAP.get(
                    dg_top['Type'], Volume.RAID_TYPE_UNKNOWN)
                break

        if raid_type == Volume.RAID_TYPE_RAID1 and len(disk_ids) >= 4:
            raid_type = Volume.RAID_TYPE_RAID10

        return raid_type, Pool.MEMBER_TYPE_DISK, disk_ids

    def _vcr_cap_get(self, mega_sys_path):
        cap_output = self._storcli_exec(
            [mega_sys_path, "show", "all"])['Capabilities']

        mega_raid_types = \
            cap_output['RAID Level Supported'].replace(', \n', '').split(', ')

        supported_raid_types = []
        for cur_mega_raid_type in list(_RAID_TYPE_MAP.keys()):
            if cur_mega_raid_type in mega_raid_types:
                supported_raid_types.append(
                    _RAID_TYPE_MAP[cur_mega_raid_type])

        supported_raid_types = sorted(list(set(supported_raid_types)))

        min_strip_size = _mega_size_to_lsm(cap_output['Min Strip Size'])
        max_strip_size = _mega_size_to_lsm(cap_output['Max Strip Size'])

        supported_strip_sizes = list(
            min_strip_size * (2 ** i)
            for i in range(
                0, int(math.log(int_div(max_strip_size, min_strip_size), 2)
                       + 1)))

        # ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^
        # The math above is to generate a list like:
        #   min_strip_size, ... n^2 , max_strip_size

        return supported_raid_types, supported_strip_sizes

    @_handle_errors
    def volume_raid_create_cap_get(self, system, flags=Client.FLAG_RSVD):
        """
        Depend on the 'Capabilities' section of "storcli /c0 show all" output.
        """
        cur_lsm_syss = list(s for s in self.systems() if s.id == system.id)
        if len(cur_lsm_syss) != 1:
            raise LsmError(
                ErrorNumber.NOT_FOUND_SYSTEM,
                "System not found")

        lsm_sys = cur_lsm_syss[0]
        return self._vcr_cap_get(lsm_sys.plugin_data)

    @_handle_errors
    def volume_raid_create(self, name, raid_type, disks, strip_size,
                           flags=Client.FLAG_RSVD):
        """
        Work flow:
            1. Create RAID volume
                storcli /c0 add vd RAID10 drives=252:1-4 pdperarray=2 J
            2. Find out pool/DG base on one disk.
                storcli /c0/e252/s1 show J
            3. Find out the volume/VD base on pool/DG using self.volumes()
        """
        mega_raid_type = _lsm_raid_type_to_mega(raid_type)
        ctrl_num = None
        slot_nums = []
        enclosure_str = None

        for disk in disks:
            if not disk.plugin_data:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: missing plugin_data "
                    "property")
            # Disk should from the same controller.
            # Please be informed, the enclosure_str could be a empty(space).
            (cur_ctrl_num, cur_enclosure_str, slot_num) = \
                disk.plugin_data.split(':')

            cur_ctrl_num = int(cur_ctrl_num)

            if ctrl_num is not None and cur_ctrl_num != ctrl_num:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: disks are not from the "
                    "same controller/system.")

            if enclosure_str is not None and \
               cur_enclosure_str != enclosure_str:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: disks are not from the "
                    "same disk enclosure.")

            ctrl_num = cur_ctrl_num
            enclosure_str = cur_enclosure_str
            slot_nums.append(slot_num)

        # Handle request volume name, LSI only allow 15 characters.
        name = re.sub('[^0-9a-zA-Z_\-]+', '', name)[:15]

        if enclosure_str == ' ':
            drives_str = "drives=%s" % ','.join(slot_nums)
        else:
            drives_str = "drives=%s:%s" % (enclosure_str, ','.join(slot_nums))

        cmds = [
            "/c%s" % ctrl_num, "add", "vd", mega_raid_type,
            'size=all', "name=%s" % name, drives_str]

        if raid_type == Volume.RAID_TYPE_RAID10 or \
           raid_type == Volume.RAID_TYPE_RAID50 or \
           raid_type == Volume.RAID_TYPE_RAID60:
            cmds.append("pdperarray=%d" % int(int_div(len(disks), 2)))

        if strip_size != Volume.VCR_STRIP_SIZE_DEFAULT:
            cmds.append("strip=%d" % int(int_div(strip_size, 1024)))

        try:
            self._storcli_exec(cmds)
        except ExecError:
            req_disk_ids = [d.id for d in disks]
            for cur_disk in self.disks():
                if cur_disk.id in req_disk_ids and \
                   not cur_disk.status & Disk.STATUS_FREE:
                    raise LsmError(
                        ErrorNumber.DISK_NOT_FREE,
                        "Disk %s is not in STATUS_FREE state" % cur_disk.id)
            # Check whether got unsupported RAID type or stripe size
            supported_raid_types, supported_strip_sizes = \
                self._vcr_cap_get("/c%s" % ctrl_num)

            if raid_type not in supported_raid_types:
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "Provided 'raid_type' is not supported")

            if strip_size != Volume.VCR_STRIP_SIZE_DEFAULT and \
               strip_size not in supported_strip_sizes:
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "Provided 'strip_size' is not supported")

            raise

        # Find out the DG ID from one disk.
        dg_show_output = self._storcli_exec(
            ["/c%s/e%s/s%s" % tuple(disks[0].plugin_data.split(":")), "show"])

        dg_id = dg_show_output['Drive Information'][0]['DG']
        if dg_id == '-':
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "volume_raid_create(): No error found in output, "
                "but RAID is not created: %s" % list(dg_show_output.items()))
        else:
            dg_id = int(dg_id)

        pool_id = _pool_id_of(dg_id, self._sys_id_of_ctrl_num(ctrl_num))

        lsm_vols = self.volumes(search_key='pool_id', search_value=pool_id)
        if len(lsm_vols) != 1:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "volume_raid_create(): Got unexpected volume count(not 1) "
                "when creating RAID volume")

        return lsm_vols[0]

    @_handle_errors
    def batteries(self, search_key=None, search_value=None,
                  flags=Client.FLAG_RSVD):
        """
        Depending on these commands:
            storcli /c0/bbu show all J
            storcli /c0/cv show all J
        """
        lsm_bats = []
        for ctrl_num in range(self._ctrl_count()):
            ctrl_show_all_output = self._storcli_exec(
                ["/c%d" % ctrl_num, "show", "all"])
            sys_id = self._sys_id_of_ctrl_num(ctrl_num, ctrl_show_all_output)

            try:
                bbu_show_all_output = self._storcli_exec(
                    ["/c%d/bbu" % ctrl_num, "show", "all"])
            except ExecError as exec_error:
                bbu_show_all_output = None

            if bbu_show_all_output:
                lsm_bats.append(_mega_bbu_to_lsm(sys_id, bbu_show_all_output))

            # Capacitor
            try:
                cv_show_all_output = self._storcli_exec(
                    ["/c%d/cv" % ctrl_num, "show", "all"])
            except ExecError as exec_error:
                cv_show_all_output = None

            if cv_show_all_output:
                lsm_bats.append(_mega_cv_to_lsm(sys_id, cv_show_all_output))

        return search_property(lsm_bats, search_key, search_value)

    @_handle_errors
    def volume_cache_info(self, volume, flags=Client.FLAG_RSVD):
        """
        Depending on these commands:
            storcli /c0/v0 show all J
        """
        flag_has_ram = False
        flag_battery_ok = False

        vd_path = _vd_path_of_lsm_vol(volume)

        vol_show_output = self._storcli_exec([vd_path, "show", "all"])
        vd_basic_info = vol_show_output[vd_path][0]
        vd_id = int(vd_basic_info['DG/VD'].split('/')[-1])
        vd_prop_info = vol_show_output['VD%d Properties' % vd_id]

        sys_all_output = self._storcli_exec(
            ["/%s" % vd_path.split('/')[1], "show", "all"])

        ram_size = _mega_size_to_lsm(
            sys_all_output['HwCfg'].get('On Board Memory Size', '0 KB'))
        if ram_size > 0:
            flag_has_ram = True

        lsm_bats = self.batteries()
        for lsm_bat in lsm_bats:
            if lsm_bat.status == Battery.STATUS_OK:
                flag_battery_ok = True

        lsi_cache_setting = vd_basic_info['Cache']
        # According to MegaRAID document, read I/O is always cached for direct
        # I/O and cache I/O.
        read_cache_policy = Volume.READ_CACHE_POLICY_ENABLED
        write_cache_status = Volume.WRITE_CACHE_STATUS_WRITE_THROUGH
        read_cache_status = Volume.READ_CACHE_STATUS_DISABLED

        if lsi_cache_setting.endswith('D'):
            # Direct I/O
            if 'AWB' in lsi_cache_setting:
                write_cache_policy = Volume.WRITE_CACHE_POLICY_WRITE_BACK
            elif 'WB' in lsi_cache_setting:
                write_cache_policy = Volume.WRITE_CACHE_POLICY_AUTO
            elif 'WT' in lsi_cache_setting:
                write_cache_policy = Volume.WRITE_CACHE_POLICY_WRITE_THROUGH
            else:
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "Unknown write cache %s for volume %s" %
                    (lsi_cache_setting, vd_path))
        elif lsi_cache_setting.endswith('C'):
            # cache I/O always caches write and read and ignore changes.
            write_cache_policy = Volume.WRITE_CACHE_POLICY_WRITE_BACK
        else:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Unknown I/O type %s for volume %s" %
                (lsi_cache_setting, vd_path))

        if flag_has_ram:
            read_cache_status = Volume.READ_CACHE_STATUS_ENABLED
            if write_cache_policy == Volume.WRITE_CACHE_POLICY_WRITE_BACK:
                write_cache_status = Volume.WRITE_CACHE_STATUS_WRITE_BACK
            elif write_cache_policy == Volume.WRITE_CACHE_POLICY_AUTO:
                if flag_battery_ok:
                    write_cache_status = Volume.WRITE_CACHE_STATUS_WRITE_BACK

        # TODO(Gris Ge): When 'Block SSD Write Disk Cache Change' of
        #                'Supported Adapter Operations' is 'Yes'
        lsi_disk_cache_setting = vd_prop_info['Disk Cache Policy']
        if lsi_disk_cache_setting == 'Disabled':
            phy_disk_cache = Volume.PHYSICAL_DISK_CACHE_DISABLED
        elif lsi_disk_cache_setting == 'Enabled':
            phy_disk_cache = Volume.PHYSICAL_DISK_CACHE_ENABLED
        elif lsi_disk_cache_setting == "Disk's Default":
            phy_disk_cache = Volume.PHYSICAL_DISK_CACHE_USE_DISK_SETTING
        else:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Unknown disk cache policy '%s' for volume %s" %
                (lsi_disk_cache_setting, vd_path))

        return [write_cache_policy, write_cache_status,
                read_cache_policy, read_cache_status, phy_disk_cache]

    @_handle_errors
    def volume_physical_disk_cache_update(self, volume, pdc,
                                          flags=Client.FLAG_RSVD):
        """
        Depending on "storcli /c0/vX set pdcache=<on|off>" command.
        """
        cmd = [_vd_path_of_lsm_vol(volume), "set"]
        if pdc == Volume.PHYSICAL_DISK_CACHE_ENABLED:
            cmd.append("pdcache=on")
        elif pdc == Volume.PHYSICAL_DISK_CACHE_DISABLED:
            cmd.append("pdcache=off")
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Got unknown pdc: %d" % pdc)
        try:
            self._storcli_exec(cmd)
            # On SSD disk, the command will return 0 for failure, only
            # json output will indicate error.
        except LsmError as lsm_err:
            if lsm_err.code == ErrorNumber.PLUGIN_BUG and \
               "SSD Pd is present" in lsm_err.msg:
                raise LsmError(
                    ErrorNumber.NO_SUPPORT,
                    "Changing SSD physical disk cache is not allowed "
                    "on MegaRAID")
            raise

    @_handle_errors
    def volume_write_cache_policy_update(self, volume, wcp,
                                         flags=Client.FLAG_RSVD):
        """
        Depending on "storcli /c0/vX set wrcache=<wt|wb|awb>" command.
        """
        vd_path = _vd_path_of_lsm_vol(volume)
        # Check whether we are working on cache I/O which ignore write cache
        # setting and always cache write.
        vol_show_output = self._storcli_exec([vd_path, "show", "all"])
        vd_basic_info = vol_show_output[vd_path][0]
        lsi_cache_setting = vd_basic_info['Cache']
        if lsi_cache_setting.endswith('C'):
            flag_cache_io = True
        else:
            flag_cache_io = False

        cmd = [vd_path, "set"]
        if wcp == Volume.WRITE_CACHE_POLICY_WRITE_BACK:
            cmd.append("wrcache=awb")
        elif wcp == Volume.WRITE_CACHE_POLICY_AUTO:
            if flag_cache_io:
                self._storcli_exec([vd_path, "set", "iopolicy=Direct"])
            cmd.append("wrcache=wb")
        elif wcp == Volume.WRITE_CACHE_POLICY_WRITE_THROUGH:
            if flag_cache_io:
                self._storcli_exec([vd_path, "set", "iopolicy=Direct"])
            cmd.append("wrcache=wt")
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Got unknown wcp: %d" % wcp)
        self._storcli_exec(cmd)

    @_handle_errors
    def volume_read_cache_policy_update(self, volume, rcp,
                                        flags=Client.FLAG_RSVD):
        """
        storcli always enable read cache and no way to change it
        """
        raise LsmError(ErrorNumber.NO_SUPPORT,
                       "MegaRAID always enable read cache and refused to "
                       "change that. You can change read ahead cache "
                       "setting via storcli/perccli")

    @_handle_errors
    def volume_delete(self, volume, flags=Client.FLAG_RSVD):
        """
        Depending on this command:
            storcli /cx/vx del force
        """
        lsm_vols = self.volumes(search_key='id', search_value=volume.id)
        if len(lsm_vols) == 0:
            raise LsmError(
                ErrorNumber.NOT_FOUND_VOLUME,
                "Volume not found")

        cmd = [lsm_vols[0].plugin_data, 'del', 'force']
        self._storcli_exec(cmd)
