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

COMPONENT = ['Adapter', 'Physical Device', 'Logical Device', 'Array', 'All', 'Unknown']


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


def _sys_status_of(ctrl_status):
    """
    Based on data of "arcconf getconfig "
    """
    status_info = ''
    status = System.STATUS_UNKNOWN
    check_list = [
        'Controller Status', 'Cache Status', 'Battery/Capacitor Status']
    for key_name in check_list:
        if key_name in ctrl_status:
            if ctrl_status[key_name] == 'Not Configured':
                status = System.STATUS_OK
            elif ctrl_status[key_name] == 'OK':
                status = System.STATUS_OK
            elif ctrl_status[key_name] == 'Optimal':
                status = System.STATUS_OK
            else:
                status = System.STATUS_OTHER
            status_info += ' "%s"=[%s]' % (str(key_name),
                                          str(ctrl_status[key_name]))

    return status, status_info


def _parse_arcconf_output(output, component):

    getconfig_cntrl_info_section = ['Controller Status',
                                    'Controller Mode',
                                    'Controller Model',
                                    'Physical Slot',
                                    'Channel description',
                                    'Controller Model',
                                    'Controller Serial Number',
                                    'Controller World Wide Name',
                                    'Firmware',
                                    'Driver']

    getconfig_physical_info_section = ['State',
                                       'Block Size',
                                       'Transfer Speed',
                                       'Reported Location',
                                       'Model',
                                       'Serial number',
                                       'Reserved Size',
                                       'Unused Size',
                                       'Total Size',
                                       'SSD',
                                       'Slot',
                                       'Connector']

    getconfig_logical_info_section =  ['Logical Device name',
                                       'Block Size of member drives',
                                       'RAID level',
                                       'Unique Identifier',
                                       'Status of Logical Device',
                                       'Size',
                                       'Interface Type',
                                       'Device Type',
                                       'Read-cache setting',
                                       'Read-cache status',
                                       'Write-cache setting',
                                       'Write-cache status',
                                       'Partitioned']

    output_lines = [
        l for l in output.split("\n")]
    cntrl_count = 0
    cntrl_data = {}
    physical_data = {}
    logical_data = {}
    list_data = []

    for line_num in range(len(output_lines)):
        cur_line = output_lines[line_num]
        if cur_line.find(":") == -1:
            continue
        else:
            tmp = cur_line.split(":")

        if tmp[0].strip() == 'Controllers found':
            cntrl_count = int(tmp[1].strip())
            continue

        temp_list = cur_line.split(": ")
        temp_list[0] = temp_list[0].strip(":")
        temp_list[0] = temp_list[0].strip()


        if component == "Adapter" :
            for info_section in getconfig_cntrl_info_section:
                if info_section == temp_list[0]:
                    cntrl_data.update({temp_list[0] : temp_list[1]})
        
        if component == 'Physical Device':       
            for info_section in getconfig_physical_info_section:
                if info_section == temp_list[0]:
                    if temp_list[0] == 'Reported Location':
                        temp_location = temp_list[1].split(',')
                        if temp_list[1].find('Connector Not Applicable') > 0:
                            physical_data.update({temp_list[0] : temp_location[0]})
                            continue
                        else:
                            physical_data.update({temp_list[0] : temp_location[0]})
                            physical_data.update({'Slot' : temp_location[1].split('(')[0]})
                            physical_data.update({'Connector' : temp_location[1].split('(')[1]})
                            check_line = output_lines[line_num - 1]
                            check_line = check_line.split(":")
                            channel_device = check_line[2].split(",")
                            channel = channel_device[0]
                            device = channel_device[1].split("(")[0]
                            physical_data.update({'Channel':channel})
                            physical_data.update({'Device':device})
                    elif temp_list[0] == 'Model':
                        check_line = output_lines[line_num - 2]
                        if check_line.find("Type") > 0:
                            continue
                        else:
                            physical_data.update({temp_list[0] : temp_list[1]})
                    else:
                        physical_data.update({temp_list[0] : temp_list[1]})
                    #if temp_list[0] == 'Drive Unique ID':
                    if temp_list[0] == 'SSD':
                        #list_data.append(list(physical_data), physical_data[0])
                        list_data.append(physical_data.copy())
                        physical_data = {}
                else:
                    continue

        if component == 'Logical Device':
            for info_section in getconfig_logical_info_section:
                if info_section == temp_list[0]:
                    logical_data.update({temp_list[0] : temp_list[1]})
                elif temp_list[0].find('Segment ') != -1:
                    serialNo = temp_list[1].split(')')[1]
                    serialNo = serialNo.strip()
                    logical_data.update({'Serial number' : serialNo})
                    list_data.append(logical_data.copy())
                    break
                else:
                    continue    

    if component == "Adapter":
        return cntrl_data
    return list_data


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

def _lsm_size_to_arcconf_MB(lsm_size):
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
    if arcconf_array[0]['Status'] == 'Optimal':
        return Pool.STATUS_OK, ''
    if arcconf_array[0]['Status'] == 'Ready':
        return Pool.STATUS_OK, ''
    if arcconf_array[0]['Status'] == 'Online':
        return Pool.STATUS_OK, ''
    else:
        # TODO(Gris Ge): Try degrade a RAID or fail a RAID.
        return Pool.STATUS_OTHER, arcconf_array['Status']


def _pool_id_of(sys_id, array_name):
    return "%s:%s" % (sys_id, array_name.replace(' ', ''))


def _disk_type_of(arcconf_disk):
    disk_interface = arcconf_disk['interfaceType']
    isSSD = arcconf_disk['nonSpinning']

    if isSSD == True:
        return Disk.TYPE_SSD 

    if disk_interface == 1:
        return Disk.TYPE_SATA
    elif disk_interface == 4:
        return Disk.TYPE_SAS

    return Disk.TYPE_UNKNOWN


def _disk_status_of(arcconf_disk, flag_free):
    # TODO(Gris Ge): Need more document or test for non-OK disks.
    state = arcconf_disk['state']

    if state == 0 or state == 1:
        disk_status = Disk.STATUS_OK
    else:
        disk_status = Disk.STATUS_OTHER

    if flag_free:
        disk_status |= Disk.STATUS_FREE

    return disk_status


def _disk_link_type_of(arcconf_disk):
    disk_interface = arcconf_disk['interfaceType']
    if disk_interface == 1:
        return Disk.TYPE_SATA
    elif disk_interface == 4:
        return Disk.TYPE_SAS

    return Disk.LINK_TYPE_UNKNOWN


_ARCCONF_RAID_LEVEL_CONV = {
    'simple_volume': Volume.RAID_TYPE_RAID0,
    '1': Volume.RAID_TYPE_RAID1,
    '5': Volume.RAID_TYPE_RAID5,
    '6': Volume.RAID_TYPE_RAID6,
    '1+0': Volume.RAID_TYPE_RAID10,
    '50': Volume.RAID_TYPE_RAID50,
    '60': Volume.RAID_TYPE_RAID60,
}


_ARCCONF_VENDOR_RAID_LEVELS = ['1adm', '1+0adm']


_LSM_RAID_TYPE_CONV = dict(
    list(zip(list(_ARCCONF_RAID_LEVEL_CONV.values()), list(_ARCCONF_RAID_LEVEL_CONV.keys()))))


def _arcconf_raid_level_to_lsm(arcconf_ld):
    """
    Based on this property:
        Fault Tolerance: 0/1/5/6/1+0
    """
    arcconf_raid_level = arcconf_ld['Fault Tolerance']

    if arcconf_raid_level in _ARCCONF_VENDOR_RAID_LEVELS:
        return Volume.RAID_TYPE_OTHER

    return _ARCCONF_RAID_LEVEL_CONV.get(arcconf_raid_level, Volume.RAID_TYPE_UNKNOWN)


def _lsm_raid_type_to_arcconf(raid_type):
    try:
        if raid_type == '0':
            return "simple_volume"
        return "simple_volume"
        #return _LSM_RAID_TYPE_CONV[raid_type]
    except KeyError:
        raise LsmError(
            ErrorNumber.NO_SUPPORT,
            "Not supported raid type %d" % raid_type)


_BATTERY_STATUS_CONV = {
    "Recharging": Battery.STATUS_CHARGING,
    "Failed (Replace Batteries/Capacitors)": Battery.STATUS_ERROR,
    "OK": Battery.STATUS_OK,
}


def _arcconf_battery_status_to_lsm(ctrl_data):
    try:
        return _BATTERY_STATUS_CONV[ctrl_data["Battery Status"]]
    except KeyError:
        return Battery.STATUS_UNKNOWN


def _sys_id_of_ctrl_data(ctrl_data):
    try:
        return ctrl_data['Controller Serial Number']
    except KeyError:
        # Dynamic Smart Array does not expose a serial number
        return ctrl_data['Host Serial Number']


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
        #raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

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
        cap.set(Capabilities.SYS_READ_CACHE_PCT_GET)
        cap.set(Capabilities.DISK_LOCATION)
        cap.set(Capabilities.DISK_VPD83_GET)
        cap.set(Capabilities.BATTERIES)
        if system.read_cache_pct >= 0:
            cap.set(Capabilities.SYS_READ_CACHE_PCT_UPDATE)
        if system.mode != System.MODE_HBA:
            cap.set(Capabilities.POOL_MEMBER_INFO)
            cap.set(Capabilities.VOLUME_RAID_INFO)
            cap.set(Capabilities.VOLUMES)
            cap.set(Capabilities.VOLUME_LED)
            cap.set(Capabilities.VOLUME_RAID_CREATE)
            cap.set(Capabilities.VOLUME_DELETE)
            cap.set(Capabilities.VOLUME_ENABLE)
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

    def _arcconf_exec(self, arcconf_cmds, component, flag_convert=True, flag_force=False):
        """
        If flag_convert is True, convert data into dict.
        """
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

        if flag_convert:
            return _parse_arcconf_output(output, component)
        else:
            return output

    @_handle_errors
    def systems(self, flags=0):
        """
        Depend on command:
            arcconf getconfig <cntrl_no>  
        """
        rc_lsm_syss = []
        getconfig_cntrls_info = []
        cntrl_info = {}
        total_cntrl = 0
        test = self._arcconf_exec(['list' ], COMPONENT[5], False)
        output_lines = [
        l for l in test.split("\n")]
        for line_num in range(len(output_lines)):
            cur_line = output_lines[line_num]
            if cur_line.find(":") == -1:
                continue
            else:
                tmp = cur_line.split(":")

            if tmp[0].strip() == 'Controllers found':
                total_cntrl = int(tmp[1].strip())
                break

        for cntrl in range(total_cntrl):
            cntrl_info = self._arcconf_exec([ "getconfigJSON", str(cntrl+1)], COMPONENT[5], False)
            cntrl_info_json = cntrl_info.split("\n")[1]
            decoded_json = json.loads(cntrl_info_json)
            getconfig_cntrls_info.append(decoded_json)

        for cntrl in range(total_cntrl):
            ctrl_name = getconfig_cntrls_info[cntrl]['Controller']['deviceVendor']
            ctrl_name = ctrl_name + ' ' + getconfig_cntrls_info[cntrl]['Controller']['deviceName']
            sys_id = getconfig_cntrls_info[cntrl]['Controller']['serialNumber']
            status = getconfig_cntrls_info[cntrl]['Controller']['controllerStatus']
            if status == 0:
                status = System.STATUS_OK 
            status_info = '"Controller Status"=[%s]' % (status)
            fw_ver = getconfig_cntrls_info[cntrl]['Controller']['firmwareVersion']
            plugin_data = getconfig_cntrls_info[cntrl]['Controller']['physicalSlot']
            read_cache_pct = System.READ_CACHE_PCT_UNKNOWN
            
            hwraid_mode = getconfig_cntrls_info[cntrl]['Controller']['functionalMode']
            if hwraid_mode == 0 or hwraid_mode == 3:
                mode = System.MODE_HARDWARE_RAID
            elif hwraid_mode == 2:
                mode = System.MODE_HBA
            else:
                mode = System.MODE_UNKNOWN
            status_info += ' mode=[%s]' % str(hwraid_mode)
            
            rc_lsm_syss.append(System(sys_id, ctrl_name, status, status_info, plugin_data, _fw_version=fw_ver, _mode=mode, _read_cache_pct=read_cache_pct))

        return rc_lsm_syss

    @_handle_errors
    def system_read_cache_pct_update(self, system, read_pct,
                                     flags=Client.FLAG_RSVD):
        return None

    @staticmethod
    def _arcconf_array_to_lsm_pool(arcconf_array, disk_name, sys_id, ctrl_num):
        disk_name = disk_name.strip()
        pool_id = '%s:%s' %(ctrl_num,disk_name)
        name = disk_name
        elem_type = Pool.ELEMENT_TYPE_VOLUME | Pool.ELEMENT_TYPE_VOLUME_FULL
        unsupported_actions = 0
        free_space = 0
        total_space = 0

        if arcconf_array['state'] == 0 or arcconf_array['state'] == 1:
            free_space += int( int(arcconf_array['fsaNumUsableBlocks']) * int(arcconf_array['fsaBytesPerBlock']))
            total_space += int(int(arcconf_array['fsaNumBlocks']) * int(arcconf_array['fsaBytesPerBlock']))
        status = Pool.STATUS_OK
        status_info = ''
        plugin_data = "%s:%s" % (ctrl_num, disk_name)

        return Pool(
            pool_id, name, elem_type, unsupported_actions,
            total_space, free_space, status, status_info,
            sys_id, plugin_data)

    @_handle_errors
    def pools(self, search_key=None, search_value=None,
              flags=Client.FLAG_RSVD):
        """
        """
        lsm_pools = []
        controllerInfo = {'Status':'Optimal', 'Slot':'255', 'Mode':'HBA', 'Name':'Adaptec', 'Serial Number':'1234'}
        getconfig_disks_info = []
        total_cntrl = 0
        test = self._arcconf_exec(['list' ], COMPONENT[5], False)
        output_lines = [
        l for l in test.split("\n")]

        sys_id = ''
        for line_num in range(len(output_lines)):
            cur_line = output_lines[line_num]
            tmp = cur_line.split(":")
            if tmp[0].strip() == 'Controllers found':
                total_cntrl = int(tmp[1].strip())

        for cntrl in range(total_cntrl):
            for line_num in range(len(output_lines)):
                cur_line = output_lines[line_num]
                tmp = cur_line.split(":")
                cntrl_num = 'Controller %d' %(cntrl + 1)
                if tmp[0].strip() == cntrl_num :
                    cntrl_info = tmp[2].split(",")
                    controllerInfo['Status'] = cntrl_info[0].strip()
                    controllerInfo['Slot'] = cntrl_info[1].strip()
                    controllerInfo['Mode'] = cntrl_info[2].strip()
                    controllerInfo['Name'] = cntrl_info[3].strip()
                    controllerInfo['Serial Number'] = cntrl_info[4].strip()
                    sys_id = controllerInfo['Serial Number']
                    cntrl_num = int(cntrl + 1)
            cntrl_num = str(int(cntrl + 1))

            disks_info = self._arcconf_exec([ "getconfigJSON", str(cntrl+1)], COMPONENT[5], False)
            disk_info_json = disks_info.split("\n")[1]
            decoded_json = json.loads(disk_info_json)

            if controllerInfo['Mode'] != 'HBA':
                for channel in range(len(decoded_json['Controller']['Channel'])) :
                    if 'HardDrive' in decoded_json['Controller']['Channel'][channel]:
                        hd_disks_num = len(decoded_json['Controller']['Channel'][channel]['HardDrive'])
                        if hd_disks_num > 0:
                            for hd_disk in range(hd_disks_num):
                                state = decoded_json['Controller']['Channel'][channel]['HardDrive'][hd_disk]['state']
                                if state == 0 or \
                                   state == 1:
                                    lsm_pools.append(Arcconf._arcconf_array_to_lsm_pool(decoded_json['Controller']['Channel'][channel]['HardDrive'][hd_disk], \
                                                     decoded_json['Controller']['Channel'][channel]['HardDrive'][hd_disk]['serialNumber'], sys_id, cntrl_num))



        return search_property(lsm_pools, search_key, search_value)

    @staticmethod
    def _arcconf_ld_to_lsm_vol(arcconf_ld, pool_id, sys_id, ctrl_num, array_num,
                          arcconf_ld_name):
        """
        raises DeviceNotFoundError
        """
        ld_num = arcconf_ld_name
        vpd83 = arcconf_ld['Unique Identifier'].lower()
        vpd83 = ''
        #Need to find the right parameter for getting the vpd83

        block_size =  int(arcconf_ld['Block Size of member drives'].strip(' Bytes'))
        num_of_blocks = int(int_div(int(_arcconf_size_to_lsm(arcconf_ld['Size'])), block_size))
        vol_name = arcconf_ld_name

        if len(vpd83) > 0:
            blk_paths = LocalDisk.vpd83_search(vpd83)
            if len(blk_paths) > 0:
                blk_path = blk_paths[0]
                vol_name += ": %s" % " ".join(blk_paths)
                device = Device.from_device_file(_CONTEXT, blk_path)
                attributes = device.attributes
                try:
                    block_size = attributes.asint("Block Size of member drives")
                    num_of_blocks = attributes.asint("Size")
                except (KeyError, UnicodeDecodeError, ValueError):
                    pass

        if 'Failed' in arcconf_ld['Status of Logical Device']:
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
        total_cntrl = 0
        controllerInfo = {'Status':'Optimal', 'Slot':'255', 'Mode':'RAID', 'Name':'Adaptec', 'Serial Number':'1234' }
        getconfig_lds_info = []

        cntrl_info = self._arcconf_exec(['list'], COMPONENT[4], False)
        #Array and pool concept does not exist in ARC controllers (upto 8 series controllers)
        pool_id = ''
        sys_id = ''

        output_lines = [
        l for l in cntrl_info.split("\n")]

        for line_num in range(len(output_lines)):
            cur_line = output_lines[line_num]
            tmp = cur_line.split(":")
            if tmp[0].strip() == 'Controllers found':
                total_cntrl = int(tmp[1].strip())


            for cntrl in range(total_cntrl):
                cntrl_num = 'Controller %d' %(cntrl + 1)
                if tmp[0].strip() == cntrl_num :
                    cntrl_info = tmp[2].split(",")
                    controllerInfo['Status'] = cntrl_info[0].strip()
                    controllerInfo['Slot'] = cntrl_info[1].strip()
                    controllerInfo['Mode'] = cntrl_info[2].strip()
                    controllerInfo['Name'] = cntrl_info[3].strip()
                    controllerInfo['Serial Number'] = cntrl_info[4].strip()
                else:
                    continue
                if controllerInfo['Mode'] == "HBA":
                    continue
                else:
                    getconfig_lds_info = self._arcconf_exec([ "getconfig", str(cntrl+1), "ld"], COMPONENT[2])
                    sys_id = controllerInfo['Serial Number']
                    cntrl_num = controllerInfo['Slot']
                    cnt = int(cntrl + 1)
            
                    for ld in getconfig_lds_info:
                        try:
                            ld_num = (ld['Logical Device name'].strip("LogicalDrv "))
                            pool_id = '%s:%s' %((cntrl + 1),ld['Serial number'])
                            lsm_vol = Arcconf._arcconf_ld_to_lsm_vol(ld, pool_id, sys_id, cnt, ld_num, ld['Logical Device name'])
                        except DeviceNotFoundError:
                            pass
                        else:
                            lsm_vols.append(lsm_vol)

        return search_property(lsm_vols, search_key, search_value)




    @staticmethod
    def _arcconf_disk_to_lsm_disk(arcconf_disk, sys_id, ctrl_num, flag_free=False):
        disk_id = arcconf_disk['serialNumber'].strip()
        
        disk_name = "%s" % (arcconf_disk['model'])
        disk_type = _disk_type_of(arcconf_disk)
        blk_size = int(arcconf_disk['fsaBytesPerBlock'])
        blk_count = int(arcconf_disk['fsaNumBlocks'])

        status = _disk_status_of(arcconf_disk, flag_free)
        disk_reported_slot = arcconf_disk['fsaSlotNum']
        disk_channel = arcconf_disk['channelID']
        disk_device = arcconf_disk['deviceID']
        disk_location = "%s %s %s" % ( disk_reported_slot, disk_channel, disk_device)
        plugin_data = "%s,%s,%s,%s" % (ctrl_num, disk_channel, disk_device, status)

        disk_path = arcconf_disk['physicalDriveName']  #Series 7 controllers don't have this disk name or path. To be added for Series 8 controllers.
        disk_path = ''
        if disk_path:
            vpd83 = LocalDisk.vpd83_get(disk_path)
        else:
            vpd83 = ''
        rpm = arcconf_disk['rotationalSpeed']
        rpm = 7200
        link_type = _disk_link_type_of(arcconf_disk)


        return Disk(
            disk_id, disk_name, disk_type, blk_size, blk_count,
            status, sys_id, _plugin_data=plugin_data, _vpd83=vpd83,
            _location=disk_location, _rpm=rpm, _link_type=link_type)

    @_handle_errors
    def disks(self, search_key=None, search_value=None,
              flags=Client.FLAG_RSVD):

        rc_lsm_disks = []
        controllerList = []
        controllerInfo = {'Status':'Optimal', 'Slot':'255', 'Mode':'HBA', 'Name':'Adaptec', 'Serial Number':'1234'}

        getconfig_disks_info = []
        total_cntrl = 0
        test = self._arcconf_exec(['list' ], COMPONENT[5], False)
        output_lines = [
        l for l in test.split("\n")]

        sys_id = ''
        for line_num in range(len(output_lines)):
            cur_line = output_lines[line_num]
            tmp = cur_line.split(":")
            if tmp[0].strip() == 'Controllers found':
                total_cntrl = int(tmp[1].strip())

        for cntrl in range(total_cntrl):
            for line_num in range(len(output_lines)):
                cur_line = output_lines[line_num]
                tmp = cur_line.split(":")
                cntrl_num = 'Controller %d' %(cntrl + 1)
                if tmp[0].strip() == cntrl_num :
                    cntrl_info = tmp[2].split(",")
                    controllerInfo['Status'] = cntrl_info[0].strip()
                    controllerInfo['Slot'] = cntrl_info[1].strip()
                    controllerInfo['Mode'] = cntrl_info[2].strip()
                    controllerInfo['Name'] = cntrl_info[3].strip()
                    controllerInfo['Serial Number'] = cntrl_info[4].strip()
                    sys_id = controllerInfo['Serial Number']
            cntrl_num = int(cntrl + 1)
            disks_info = self._arcconf_exec([ "getconfigJSON", str(cntrl+1)], COMPONENT[5], False)
            disk_info_json = disks_info.split("\n")[1]
            decoded_json = json.loads(disk_info_json) 

            for channel in range(len(decoded_json['Controller']['Channel'])) :
                if 'HardDrive' in decoded_json['Controller']['Channel'][channel]:
                    hd_disks_num = len(decoded_json['Controller']['Channel'][channel]['HardDrive'])
                    if hd_disks_num > 0:
                        for hd_disk in range(hd_disks_num):
                            rc_lsm_disks.append(Arcconf._arcconf_disk_to_lsm_disk(decoded_json['Controller']['Channel'][channel]['HardDrive'][hd_disk], sys_id, cntrl_num, flag_free=False))            


        return search_property(rc_lsm_disks, search_key, search_value)

    @_handle_errors
    def volume_raid_info(self, volume, flags=Client.FLAG_RSVD):
        #To be implemented
        return None

    @_handle_errors
    def pool_member_info(self, pool, flags=Client.FLAG_RSVD):
        #To be implemented
        return None

    @_handle_errors
    def volume_raid_create_cap_get(self, system, flags=Client.FLAG_RSVD):
        """
            To be implemented
        """
        return 
    @_handle_errors
    def volume_raid_create(self, name, raid_type, disks, strip_size,
                           flags=Client.FLAG_RSVD):
        #To be implemented
        return None


    @_handle_errors
    def volume_create(self, args, name, size, flags=Client.FLAG_RSVD):
        arcconf_disk_ids = []
        ctrl_num = None

        valid_disk_count = 0
        total_volumes = 0
        vol_id = ''
        vol_created = 0
        valid_disk_list = []
        for valid_disk in self.disks():
            if valid_disk.status ==  Disk.STATUS_OK:
                if valid_disk.id == args.name:
                    valid_disk_list.append(valid_disk)    
                    valid_disk_count += 1
             
        for volume in self.volumes():
            total_volumes += 1
            
        max_volumes = valid_disk_count * 4    

        for cur_disk in valid_disk_list:
            if total_volumes == max_volumes: 
                raise LsmError(ErrorNumber.NOT_ENOUGH_SPACE, "Reached the limit on Volume Creation")
            if not cur_disk.plugin_data:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT, "Illegal input disks argument: missing plugin_data property")
            (cur_ctrl_num, disk_channel, disk_device, disk_status) = cur_disk.plugin_data.split(',')
            if ctrl_num and cur_ctrl_num != ctrl_num:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT, "Illegal input disks argument: disks are not from the same controller/system.")
            if vol_created > 0:
                break

            ctrl_num = cur_ctrl_num
            arcconf_raid_level = 'simple_volume'
            disk_channel = str(disk_channel.strip())
            disk_device = str(disk_device.strip())
            vol_size = _lsm_size_to_arcconf_MB(size)
            
            try:
                if int(disk_status) == Disk.STATUS_OK:
                    cmds = ['create', str(ctrl_num), 'logicaldrive', str(vol_size), arcconf_raid_level, disk_channel, disk_device]
                    try:
                        if vol_created == 0:
                            self._arcconf_exec( cmds, COMPONENT[5], flag_convert=False, flag_force=True)
                            vol_created = 1
                            vol_id = str(vol_id + 1)
                    except:
                        continue
                else:
                    continue
            except ExecError:
            # Check whether disk is free
                requested_disk_ids = [d.id for d in self.disks()]
            
                if cur_disk.id in requested_disk_ids and \
                    not cur_disk.status & Disk.STATUS_FREE:
                    raise LsmError(ErrorNumber.DISK_NOT_FREE, "Disk %s is not in STATUS_FREE state" % cur_disk.id)
            
            
        pool_id  = str(args.id)
        vol_list = self.volumes()
        vol_last = vol_list[-1]
        vol_id = vol_last.id
        lsm_vols = self.volumes(search_key='id', search_value=vol_id)
        
        if len(lsm_vols) == 0:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "volume_create(): Got unexpected count(not > 0) of new "
                "volumes: %s" % lsm_vols)

        return vol_id, lsm_vols[-1]
        

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
            self._arcconf_exec(['delete', ctrl_num, 'logicaldrive', array_num], COMPONENT[5], flag_convert=False, flag_force=True)
        except ExecError:
            raise LsmError(
                ErrorNumber.NOT_FOUND_VOLUME,
                "Volume not found")

        return None

    def volume_enable(self, volume, flags=Client.FLAG_RSVD):
        return None

    @_handle_errors
    def volume_ident_led_on(self, volume, flags=Client.FLAG_RSVD):
        return None

    @_handle_errors
    def volume_ident_led_off(self, volume, flags=Client.FLAG_RSVD):
        return None

    @_handle_errors
    def batteries(self, search_key=None, search_value=None,
                  flags=Client.FLAG_RSVD):
        return None

    def _cal_of_lsm_vol(self, lsm_vol):
        return None

    @_handle_errors
    def volume_cache_info(self, volume, flags=Client.FLAG_RSVD):
        return None

    def _is_ssd_volume(self, volume):
        ssd_disk_ids = list(d.id for d in self.disks()
                            if d.disk_type == Disk.TYPE_SSD)
        pool = self.pools(search_key='id', search_value=volume.pool_id)[0]
        disk_ids = self.pool_member_info(pool)[2]
        return len(set(disk_ids) & set(ssd_disk_ids)) != 0

    @_handle_errors
    def volume_physical_disk_cache_update(self, volume, pdc,
                                          flags=Client.FLAG_RSVD):
        return None

    @_handle_errors
    def volume_write_cache_policy_update(self, volume, wcp,
                                         flags=Client.FLAG_RSVD):
        return None

    @_handle_errors
    def volume_read_cache_policy_update(self, volume, rcp,
                                        flags=Client.FLAG_RSVD):
        return None

