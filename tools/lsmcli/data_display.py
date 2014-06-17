# Copyright (C) 2014 Red Hat, Inc.
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
# USA
#
# Author: Gris Ge <fge@redhat.com>
import sys
from collections import OrderedDict
from datetime import datetime

from lsm import (size_bytes_2_size_human, LsmError, ErrorNumber,
                 System, Pool, Disk, Volume, AccessGroup,
                 FileSystem, FsSnapshot, NfsExport)

BIT_MAP_STRING_SPLITTER = ','


## Users are reporting errors with broken pipe when piping output
# to another program.  This appears to be related to this issue:
# http://bugs.python.org/issue11380
# Unable to reproduce, but hopefully this will address it.
# @param msg    The message to be written to stdout
def out(msg):
    try:
        sys.stdout.write(str(msg))
        sys.stdout.write("\n")
        sys.stdout.flush()
    except IOError:
        sys.exit(1)


def _txt_a(txt, append):
    if len(txt):
        return txt + BIT_MAP_STRING_SPLITTER + append
    else:
        return append


def _bit_map_to_str(bit_map, conv_dict):
    rc = ''
    bit_map = int(bit_map)
    for cur_enum in conv_dict.keys():
        if cur_enum & bit_map:
            rc = _txt_a(rc, conv_dict[cur_enum])
    if rc == '':
        return 'Unknown(%s)' % hex(bit_map)
    return rc


def _enum_type_to_str(int_type, conv_dict):
    rc = ''
    int_type = int(int_type)

    if int_type in conv_dict.keys():
        return conv_dict[int_type]
    return 'Unknown(%d)' % int_type


def _str_to_enum(type_str, conv_dict):
    keys = [k for k, v in conv_dict.items() if v.lower() == type_str.lower()]
    if len(keys) > 0:
        return keys[0]
    raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                   "Failed to convert %s to lsm type" % type_str)


_SYSTEM_STATUS_CONV = {
    System.STATUS_UNKNOWN: 'Unknown',
    System.STATUS_OK: 'OK',
    System.STATUS_ERROR: 'Error',
    System.STATUS_DEGRADED: 'Degraded',
    System.STATUS_PREDICTIVE_FAILURE: 'Predictive failure',
    System.STATUS_STRESSED: 'Stressed',
    System.STATUS_STARTING: 'Starting',
    System.STATUS_STOPPING: 'Stopping',
    System.STATUS_STOPPED: 'Stopped',
    System.STATUS_OTHER: 'Other',
}


def system_status_to_str(system_status):
    return _bit_map_to_str(system_status, _SYSTEM_STATUS_CONV)


_POOL_STATUS_CONV = {
    Pool.STATUS_UNKNOWN: 'UNKNOWN',
    Pool.STATUS_OK: 'OK',
    Pool.STATUS_OTHER: 'OTHER',
    Pool.STATUS_STRESSED: 'STRESSED',
    Pool.STATUS_DEGRADED: 'DEGRADED',
    Pool.STATUS_ERROR: 'ERROR',
    Pool.STATUS_STARTING: 'STARTING',
    Pool.STATUS_STOPPING: 'STOPPING',
    Pool.STATUS_STOPPED: 'STOPPED',
    Pool.STATUS_READ_ONLY: 'READ_ONLY',
    Pool.STATUS_DORMANT: 'DORMANT',
    Pool.STATUS_RECONSTRUCTING: 'RECONSTRUCTING',
    Pool.STATUS_VERIFYING: 'VERIFYING',
    Pool.STATUS_INITIALIZING: 'INITIALIZING',
    Pool.STATUS_GROWING: 'GROWING',
    Pool.STATUS_SHRINKING: 'SHRINKING',
    Pool.STATUS_DESTROYING: 'DESTROYING',
}


def pool_status_to_str(pool_status):
    return _bit_map_to_str(pool_status, _POOL_STATUS_CONV)


_POOL_ELEMENT_TYPE_CONV = {
    Pool.ELEMENT_TYPE_UNKNOWN: 'UNKNOWN',
    Pool.ELEMENT_TYPE_POOL: 'POOL',
    Pool.ELEMENT_TYPE_VOLUME: 'VOLUME',
    Pool.ELEMENT_TYPE_FS: 'FILE_SYSTEM',
    Pool.ELEMENT_TYPE_SYS_RESERVED: 'SYSTEM_RESERVED',
}


def pool_element_type_to_str(element_type):
    return _bit_map_to_str(element_type, _POOL_ELEMENT_TYPE_CONV)


_POOL_RAID_TYPE_CONV = {
    Pool.RAID_TYPE_RAID0: 'RAID0',  # stripe
    Pool.RAID_TYPE_RAID1: 'RAID1',  # mirror
    Pool.RAID_TYPE_RAID3: 'RAID3',  # byte-level striping with dedicated
                                    # parity
    Pool.RAID_TYPE_RAID4: 'RAID4',  # block-level striping with dedicated
                                    # parity
    Pool.RAID_TYPE_RAID5: 'RAID5',  # block-level striping with distributed
                                    # parity
    Pool.RAID_TYPE_RAID6: 'RAID6',  # AKA, RAID-DP.
    Pool.RAID_TYPE_RAID10: 'RAID10',  # stripe of mirrors
    Pool.RAID_TYPE_RAID15: 'RAID15',  # parity of mirrors
    Pool.RAID_TYPE_RAID16: 'RAID16',  # dual parity of mirrors
    Pool.RAID_TYPE_RAID50: 'RAID50',  # stripe of parities
    Pool.RAID_TYPE_RAID60: 'RAID60',  # stripe of dual parities
    Pool.RAID_TYPE_RAID51: 'RAID51',  # mirror of parities
    Pool.RAID_TYPE_RAID61: 'RAID61',  # mirror of dual parities
    Pool.RAID_TYPE_JBOD: 'JBOD',      # Just Bunch of Disks
    Pool.RAID_TYPE_UNKNOWN: 'UNKNOWN',
    Pool.RAID_TYPE_NOT_APPLICABLE: 'NOT_APPLICABLE',
    Pool.RAID_TYPE_MIXED: 'MIXED',  # a Pool are having 2+ RAID groups with
                                    # different RAID type
}


def pool_raid_type_to_str(raid_type):
    return _enum_type_to_str(raid_type, _POOL_RAID_TYPE_CONV)


def pool_raid_type_str_to_type(raid_type_str):
    return _str_to_enum(raid_type_str, _POOL_RAID_TYPE_CONV)


_POOL_MEMBER_TYPE_CONV = {
    Pool.MEMBER_TYPE_UNKNOWN: 'UNKNOWN',
    Pool.MEMBER_TYPE_DISK: 'DISK',       # Pool was created from Disk(s).
    Pool.MEMBER_TYPE_DISK_MIX: 'DISK_MIX',   # Has two or more types of disks.
    Pool.MEMBER_TYPE_DISK_ATA: 'DISK_ATA',
    Pool.MEMBER_TYPE_DISK_SATA: 'DISK_SATA',
    Pool.MEMBER_TYPE_DISK_SAS: 'DISK_SAS',
    Pool.MEMBER_TYPE_DISK_FC: 'DISK_FC',
    Pool.MEMBER_TYPE_DISK_SOP: 'DISK_SOP',
    Pool.MEMBER_TYPE_DISK_SCSI: 'DISK_SCSI',
    Pool.MEMBER_TYPE_DISK_NL_SAS: 'DISK_NL_SAS',
    Pool.MEMBER_TYPE_DISK_HDD: 'DISK_HDD',
    Pool.MEMBER_TYPE_DISK_SSD: 'DISK_SSD',
    Pool.MEMBER_TYPE_DISK_HYBRID: 'DISK_HYBRID',
    Pool.MEMBER_TYPE_POOL: 'POOL',       # Pool was created from other Pool(s).
    Pool.MEMBER_TYPE_VOLUME: 'VOLUME',   # Pool was created from Volume(s).
}


def pool_member_type_to_str(member_type):
    return _enum_type_to_str(member_type, _POOL_MEMBER_TYPE_CONV)


def pool_member_type_str_to_type(member_type_str):
    return _str_to_enum(member_type_str, _POOL_MEMBER_TYPE_CONV)


_POOL_THINP_TYPE_CONV = {
    Pool.THINP_TYPE_UNKNOWN: 'UNKNOWN',
    Pool.THINP_TYPE_THIN: 'THIN',
    Pool.THINP_TYPE_THICK: 'THICK',
    Pool.THINP_TYPE_NOT_APPLICABLE: 'NOT_APPLICABLE',
}


def pool_thinp_type_to_str(thinp_type):
    return _enum_type_to_str(thinp_type, _POOL_THINP_TYPE_CONV)


def pool_thinp_type_str_to_type(thinp_type_str):
    return _str_to_enum(thinp_type_str, _POOL_THINP_TYPE_CONV)


_VOL_STATUS_CONV = {
    Volume.STATUS_UNKNOWN: 'Unknown',
    Volume.STATUS_OK: 'OK',
    Volume.STATUS_DEGRADED: 'Degraded',
    Volume.STATUS_DORMANT: 'Dormant',
    Volume.STATUS_ERR: 'Error',
    Volume.STATUS_STARTING: 'Starting',
}


_VOL_PROVISION_CONV = {
    Volume.PROVISION_DEFAULT: 'DEFAULT',
    Volume.PROVISION_FULL: 'FULL',
    Volume.PROVISION_THIN: 'THIN',
    Volume.PROVISION_UNKNOWN: 'UNKNOWN',
}


def vol_provision_str_to_type(vol_provision_str):
    return _str_to_enum(vol_provision_str, _VOL_PROVISION_CONV)


_VOL_REP_TYPE_CONV = {
    Volume.REPLICATE_SNAPSHOT: 'SNAPSHOT',
    Volume.REPLICATE_CLONE: 'CLONE',
    Volume.REPLICATE_COPY: 'COPY',
    Volume.REPLICATE_MIRROR_SYNC: 'MIRROR_SYNC',
    Volume.REPLICATE_MIRROR_ASYNC: 'MIRROR_ASYNC',
    Volume.REPLICATE_UNKNOWN: 'UNKNOWN',
}


def vol_rep_type_str_to_type(vol_rep_type_str):
    return _str_to_enum(vol_rep_type_str, _VOL_REP_TYPE_CONV)


def vol_status_to_str(vol_status):
    return _bit_map_to_str(vol_status, _VOL_STATUS_CONV)


_DISK_TYPE_CONV = {
    Disk.DISK_TYPE_UNKNOWN: 'UNKNOWN',
    Disk.DISK_TYPE_OTHER: 'OTHER',
    Disk.DISK_TYPE_NOT_APPLICABLE: 'NOT_APPLICABLE',
    Disk.DISK_TYPE_ATA: 'ATA',
    Disk.DISK_TYPE_SATA: 'SATA',
    Disk.DISK_TYPE_SAS: 'SAS',
    Disk.DISK_TYPE_FC: 'FC',
    Disk.DISK_TYPE_SOP: 'SCSI Over PCI-E(SSD)',
    Disk.DISK_TYPE_NL_SAS: 'NL_SAS',
    Disk.DISK_TYPE_HDD: 'HDD',
    Disk.DISK_TYPE_SSD: 'SSD',
    Disk.DISK_TYPE_HYBRID: 'HYBRID',
    Disk.DISK_TYPE_LUN: 'Remote LUN',
}


def disk_type_to_str(disk_type):
    return _enum_type_to_str(disk_type, _DISK_TYPE_CONV)


_DISK_STATUS_CONV = {
    Disk.STATUS_UNKNOWN: 'UNKNOWN',
    Disk.STATUS_OK: 'OK',
    Disk.STATUS_OTHER: 'OTHER',
    Disk.STATUS_PREDICTIVE_FAILURE: 'PREDICTIVE_FAILURE',
    Disk.STATUS_ERROR: 'ERROR',
    Disk.STATUS_OFFLINE: 'OFFLINE',
    Disk.STATUS_STARTING: 'STARTING',
    Disk.STATUS_STOPPING: 'STOPPING',
    Disk.STATUS_STOPPED: 'STOPPED',
    Disk.STATUS_INITIALIZING: 'INITIALIZING',
}


def disk_status_to_str(disk_status):
    return _bit_map_to_str(disk_status, _DISK_STATUS_CONV)


_AG_INIT_TYPE_CONV = {
    AccessGroup.INIT_TYPE_UNKNOWN: 'Unknown',
    AccessGroup.INIT_TYPE_OTHER: 'Other',
    AccessGroup.INIT_TYPE_WWPN: 'WWPN',
    AccessGroup.INIT_TYPE_WWNN: 'WWNN',
    AccessGroup.INIT_TYPE_HOSTNAME: 'Hostname',
    AccessGroup.INIT_TYPE_ISCSI_IQN: 'iSCSI',
    AccessGroup.INIT_TYPE_SAS: 'SAS',
    AccessGroup.INIT_TYPE_ISCSI_WWPN_MIXED: 'iSCSI/WWPN Mixed',
}


def ag_init_type_to_str(init_type):
    return _enum_type_to_str(init_type, _AG_INIT_TYPE_CONV)


def ag_init_type_str_to_lsm(init_type_str):
    return _str_to_enum(init_type_str, _AG_INIT_TYPE_CONV)


class PlugData(object):
    def __init__(self, description, plugin_version):
            self.desc = description
            self.version = plugin_version


class DisplayData(object):

    def __init__(self):
        pass

    DISPLAY_WAY_COLUMN = 0
    DISPLAY_WAY_SCRIPT = 1

    DISPLAY_WAY_DEFAULT = DISPLAY_WAY_COLUMN

    DEFAULT_SPLITTER = ' | '

    VALUE_CONVERT = {}

    # lsm.System
    SYSTEM_MAN_HEADER = OrderedDict()
    SYSTEM_MAN_HEADER['id'] = 'ID'
    SYSTEM_MAN_HEADER['name'] = 'Name'
    SYSTEM_MAN_HEADER['status'] = 'Status'
    SYSTEM_MAN_HEADER['status_info'] = 'Status Info'

    SYSTEM_OPT_HEADER = OrderedDict()

    SYSTEM_COLUMN_KEYS = SYSTEM_MAN_HEADER.keys()
    # SYSTEM_COLUMN_KEYS should be subset of SYSTEM_MAN_HEADER.keys()
    # XXX_COLUMN_KEYS contain a list of mandatory properties which will be
    # displayed in column way. It was used to limit the output of properties
    # in sure the column display way does not exceeded the column width 78.
    # All mandatory_headers will be displayed in script way.
    # if '-o' define, both mandatory_headers and optional_headers will be
    # displayed in script way.

    SYSTEM_VALUE_CONV_ENUM = {
        'status': system_status_to_str,
    }

    SYSTEM_VALUE_CONV_HUMAN = []

    VALUE_CONVERT[System] = {
        'mandatory_headers': SYSTEM_MAN_HEADER,
        'column_keys': SYSTEM_COLUMN_KEYS,
        'optional_headers': SYSTEM_OPT_HEADER,
        'value_conv_enum': SYSTEM_VALUE_CONV_ENUM,
        'value_conv_human': SYSTEM_VALUE_CONV_HUMAN,
    }

    PLUG_DATA_MAN_HEADER = OrderedDict()
    PLUG_DATA_MAN_HEADER['desc'] = 'Description'
    PLUG_DATA_MAN_HEADER['version'] = 'Version'

    PLUG_DATA_COLUMN_KEYS = PLUG_DATA_MAN_HEADER.keys()

    PLUG_DATA_OPT_HEADER = OrderedDict()
    PLUG_DATA_VALUE_CONV_ENUM = {}
    PLUG_DATA_VALUE_CONV_HUMAN = []

    VALUE_CONVERT[PlugData] = {
        'mandatory_headers': PLUG_DATA_MAN_HEADER,
        'column_keys': PLUG_DATA_COLUMN_KEYS,
        'optional_headers': PLUG_DATA_OPT_HEADER,
        'value_conv_enum': PLUG_DATA_VALUE_CONV_ENUM,
        'value_conv_human': PLUG_DATA_VALUE_CONV_HUMAN,
    }

    # lsm.Pool
    POOL_MAN_HEADER = OrderedDict()
    POOL_MAN_HEADER['id'] = 'ID'
    POOL_MAN_HEADER['name'] = 'Name'
    POOL_MAN_HEADER['total_space'] = 'Total Space'
    POOL_MAN_HEADER['free_space'] = 'Free Space'
    POOL_MAN_HEADER['status'] = 'Status'
    POOL_MAN_HEADER['status_info'] = 'Status Info'
    POOL_MAN_HEADER['system_id'] = 'System ID'

    POOL_COLUMN_KEYS = POOL_MAN_HEADER.keys()

    POOL_OPT_HEADER = OrderedDict()
    POOL_OPT_HEADER['raid_type'] = 'RAID Type'
    POOL_OPT_HEADER['member_type'] = 'Member Type'
    POOL_OPT_HEADER['member_ids'] = 'Member IDs'
    POOL_OPT_HEADER['thinp_type'] = 'Provision Type'
    POOL_OPT_HEADER['element_type'] = 'Element Type'

    POOL_VALUE_CONV_ENUM = {
        'status': pool_status_to_str,
        'raid_type': pool_raid_type_to_str,
        'member_type': pool_member_type_to_str,
        'thinp_type': pool_thinp_type_to_str,
        'element_type': pool_element_type_to_str,
    }

    POOL_VALUE_CONV_HUMAN = ['total_space', 'free_space']

    VALUE_CONVERT[Pool] = {
        'mandatory_headers': POOL_MAN_HEADER,
        'column_keys': POOL_COLUMN_KEYS,
        'optional_headers': POOL_OPT_HEADER,
        'value_conv_enum': POOL_VALUE_CONV_ENUM,
        'value_conv_human': POOL_VALUE_CONV_HUMAN,
    }

    # lsm.Volume
    VOL_MAN_HEADER = OrderedDict()
    VOL_MAN_HEADER['id'] = 'ID'
    VOL_MAN_HEADER['name'] = 'Name'
    VOL_MAN_HEADER['vpd83'] = 'SCSI VPD 0x83'
    VOL_MAN_HEADER['block_size'] = 'Block Size'
    VOL_MAN_HEADER['num_of_blocks'] = '#blocks'
    VOL_MAN_HEADER['size_bytes'] = 'Size'
    VOL_MAN_HEADER['status'] = 'Status'
    VOL_MAN_HEADER['pool_id'] = 'Pool ID'
    VOL_MAN_HEADER['system_id'] = 'System ID'

    VOL_COLUMN_KEYS = []
    for key_name in VOL_MAN_HEADER.keys():
        # Skip these keys for column display
        if key_name not in ['block_size', 'num_of_blocks', 'system_id']:
            VOL_COLUMN_KEYS.extend([key_name])

    VOL_OPT_HEADER = OrderedDict()

    VOL_VALUE_CONV_ENUM = {
        'status': vol_status_to_str,
    }

    VOL_VALUE_CONV_HUMAN = ['size_bytes', 'block_size']

    VALUE_CONVERT[Volume] = {
        'mandatory_headers': VOL_MAN_HEADER,
        'column_keys': VOL_COLUMN_KEYS,
        'optional_headers': VOL_OPT_HEADER,
        'value_conv_enum': VOL_VALUE_CONV_ENUM,
        'value_conv_human': VOL_VALUE_CONV_HUMAN,
    }

    # lsm.Disk
    DISK_MAN_HEADER = OrderedDict()
    DISK_MAN_HEADER['id'] = 'ID'
    DISK_MAN_HEADER['name'] = 'Name'
    DISK_MAN_HEADER['disk_type'] = 'Type'
    DISK_MAN_HEADER['block_size'] = 'Block Size'
    DISK_MAN_HEADER['num_of_blocks'] = '#blocks'
    DISK_MAN_HEADER['size_bytes'] = 'Size'
    DISK_MAN_HEADER['status'] = 'Status'
    DISK_MAN_HEADER['system_id'] = 'System ID'

    DISK_COLUMN_KEYS = []
    for key_name in DISK_MAN_HEADER.keys():
        # Skip these keys for column display
        if key_name not in ['block_size', 'num_of_blocks']:
            DISK_COLUMN_KEYS.extend([key_name])

    DISK_OPT_HEADER = OrderedDict()
    DISK_OPT_HEADER['sn'] = 'Serial Number'
    DISK_OPT_HEADER['part_num'] = 'Part Number'
    DISK_OPT_HEADER['vendor'] = 'Vendor'
    DISK_OPT_HEADER['model'] = 'Model'

    DISK_VALUE_CONV_ENUM = {
        'status': disk_status_to_str,
        'disk_type': disk_type_to_str,
    }

    DISK_VALUE_CONV_HUMAN = ['size_bytes', 'block_size']

    VALUE_CONVERT[Disk] = {
        'mandatory_headers': DISK_MAN_HEADER,
        'column_keys': DISK_COLUMN_KEYS,
        'optional_headers': DISK_OPT_HEADER,
        'value_conv_enum': DISK_VALUE_CONV_ENUM,
        'value_conv_human': DISK_VALUE_CONV_HUMAN,
    }

    # lsm.AccessGroup
    AG_MAN_HEADER = OrderedDict()
    AG_MAN_HEADER['id'] = 'ID'
    AG_MAN_HEADER['name'] = 'Name'
    AG_MAN_HEADER['init_ids'] = 'Initiator IDs'
    AG_MAN_HEADER['init_type'] = 'Type'
    AG_MAN_HEADER['system_id'] = 'System ID'

    AG_COLUMN_KEYS = AG_MAN_HEADER.keys()

    AG_OPT_HEADER = OrderedDict()

    AG_VALUE_CONV_ENUM = {
        'init_type': ag_init_type_to_str,
    }

    AG_VALUE_CONV_HUMAN = []

    VALUE_CONVERT[AccessGroup] = {
        'mandatory_headers': AG_MAN_HEADER,
        'column_keys': AG_COLUMN_KEYS,
        'optional_headers': AG_OPT_HEADER,
        'value_conv_enum': AG_VALUE_CONV_ENUM,
        'value_conv_human': AG_VALUE_CONV_HUMAN,
    }

    # lsm.FileSystem
    FS_MAN_HEADER = OrderedDict()
    FS_MAN_HEADER['id'] = 'ID'
    FS_MAN_HEADER['name'] = 'Name'
    FS_MAN_HEADER['total_space'] = 'Total Space'
    FS_MAN_HEADER['free_space'] = 'Free Space'
    FS_MAN_HEADER['pool_id'] = 'Pool ID'
    FS_MAN_HEADER['system_id'] = 'System ID'

    FS_COLUMN_KEYS = []
    for key_name in FS_MAN_HEADER.keys():
        # Skip these keys for column display
        if key_name not in ['system_id']:
            FS_COLUMN_KEYS.extend([key_name])

    FS_OPT_HEADER = OrderedDict()

    FS_VALUE_CONV_ENUM = {
    }

    FS_VALUE_CONV_HUMAN = ['total_space', 'free_space']

    VALUE_CONVERT[FileSystem] = {
        'mandatory_headers': FS_MAN_HEADER,
        'column_keys': FS_COLUMN_KEYS,
        'optional_headers': FS_OPT_HEADER,
        'value_conv_enum': FS_VALUE_CONV_ENUM,
        'value_conv_human': FS_VALUE_CONV_HUMAN,
    }

    # lsm.FsSnapshot
    FS_SNAP_MAN_HEADER = OrderedDict()
    FS_SNAP_MAN_HEADER['id'] = 'ID'
    FS_SNAP_MAN_HEADER['name'] = 'Name'
    FS_SNAP_MAN_HEADER['ts'] = 'Time Stamp'

    FS_SNAP_COLUMN_KEYS = FS_SNAP_MAN_HEADER.keys()

    FS_SNAP_OPT_HEADER = OrderedDict()

    FS_SNAP_VALUE_CONV_ENUM = {
        'ts': datetime.fromtimestamp
    }

    FS_SNAP_VALUE_CONV_HUMAN = []

    VALUE_CONVERT[FsSnapshot] = {
        'mandatory_headers': FS_SNAP_MAN_HEADER,
        'column_keys': FS_SNAP_COLUMN_KEYS,
        'optional_headers': FS_SNAP_OPT_HEADER,
        'value_conv_enum': FS_SNAP_VALUE_CONV_ENUM,
        'value_conv_human': FS_SNAP_VALUE_CONV_HUMAN,
    }

    # lsm.NfsExport
    NFS_EXPORT_MAN_HEADER = OrderedDict()
    NFS_EXPORT_MAN_HEADER['id'] = 'ID'
    NFS_EXPORT_MAN_HEADER['fs_id'] = 'FileSystem ID'
    NFS_EXPORT_MAN_HEADER['export_path'] = 'Export Path'
    NFS_EXPORT_MAN_HEADER['auth'] = 'Auth Type'
    NFS_EXPORT_MAN_HEADER['root'] = 'Root Hosts'
    NFS_EXPORT_MAN_HEADER['rw'] = 'RW Hosts'
    NFS_EXPORT_MAN_HEADER['ro'] = 'RO Hosts'
    NFS_EXPORT_MAN_HEADER['anonuid'] = 'Anonymous UID'
    NFS_EXPORT_MAN_HEADER['anongid'] = 'Anonymous GID'
    NFS_EXPORT_MAN_HEADER['options'] = 'Options'

    NFS_EXPORT_COLUMN_KEYS = []
    for key_name in NFS_EXPORT_MAN_HEADER.keys():
        # Skip these keys for column display
        if key_name not in ['root', 'anonuid', 'anongid', 'auth']:
            NFS_EXPORT_COLUMN_KEYS.extend([key_name])

    NFS_EXPORT_OPT_HEADER = OrderedDict()

    NFS_EXPORT_VALUE_CONV_ENUM = {}

    NFS_EXPORT_VALUE_CONV_HUMAN = []

    VALUE_CONVERT[NfsExport] = {
        'mandatory_headers': NFS_EXPORT_MAN_HEADER,
        'column_keys': NFS_EXPORT_COLUMN_KEYS,
        'optional_headers': NFS_EXPORT_OPT_HEADER,
        'value_conv_enum': NFS_EXPORT_VALUE_CONV_ENUM,
        'value_conv_human': NFS_EXPORT_VALUE_CONV_HUMAN,
    }

    @staticmethod
    def _get_man_pro_value(obj, key, value_conv_enum, value_conv_human,
                           flag_human, flag_enum):
        value = getattr(obj, key)
        if not flag_enum:
            if key in value_conv_enum.keys():
                value = value_conv_enum[key](value)
        if flag_human:
            if key in value_conv_human:
                value = size_bytes_2_size_human(value)
        return value

    @staticmethod
    def _get_opt_pro_value(obj, key, value_conv_enum, value_conv_human,
                           flag_human, flag_enum):
        value = obj.optional_data.get(key)
        if not flag_enum:
            if key in value_conv_enum.keys():
                value = value_conv_enum[key](value)
        if flag_human:
            if key in value_conv_human:
                value = size_bytes_2_size_human(value)
        return value

    @staticmethod
    def _find_max_width(two_d_list, column_index):
        max_width = 1
        for row_index in range(0, len(two_d_list)):
            row_data = two_d_list[row_index]
            if len(row_data[column_index]) > max_width:
                max_width = len(row_data[column_index])
        return max_width

    @staticmethod
    def _data_dict_gen(obj, flag_human, flag_enum, display_way,
                       extra_properties=None, flag_dsp_all_data=False):
        data_dict = OrderedDict()
        value_convert = DisplayData.VALUE_CONVERT[type(obj)]
        mandatory_headers = value_convert['mandatory_headers']
        optional_headers = value_convert['optional_headers']
        value_conv_enum = value_convert['value_conv_enum']
        value_conv_human = value_convert['value_conv_human']

        if flag_dsp_all_data:
            display_way = DisplayData.DISPLAY_WAY_SCRIPT

        display_keys = []

        if display_way == DisplayData.DISPLAY_WAY_COLUMN:
            display_keys = value_convert['column_keys']
        elif display_way == DisplayData.DISPLAY_WAY_SCRIPT:
            display_keys = mandatory_headers.keys()

        for key in display_keys:
            key_str = mandatory_headers[key]
            value = DisplayData._get_man_pro_value(
                obj, key, value_conv_enum, value_conv_human, flag_human,
                flag_enum)
            data_dict[key_str] = value

        if flag_dsp_all_data:
            cur_support_opt_keys = obj.optional_data.keys()
            for key in optional_headers.keys():
                if key not in cur_support_opt_keys:
                    continue
                key_str = optional_headers[key]
                value = DisplayData._get_opt_pro_value(
                    obj, key, value_conv_enum, value_conv_human, flag_human,
                    flag_enum)
                data_dict[key_str] = value

        if extra_properties:
            cur_support_opt_keys = obj.optional_data.keys()
            for key in extra_properties:
                if key in data_dict.keys():
                    # already contained
                    continue
                if key in mandatory_headers.keys():
                    key_str = mandatory_headers[key]
                    value = DisplayData._get_man_pro_value(
                        obj, key, value_conv_enum, value_conv_human,
                        flag_human, flag_enum)
                    data_dict[key_str] = value
                elif key in optional_headers.keys():
                    if key not in cur_support_opt_keys:
                        continue
                    key_str = optional_headers[key]
                    value = DisplayData._get_opt_pro_value(
                        obj, key, value_conv_enum, value_conv_human,
                        flag_human, flag_enum)
                    data_dict[key_str] = value

        return data_dict

    @staticmethod
    def display_data(objs, display_way=None,
                     flag_human=True, flag_enum=False,
                     extra_properties=None,
                     splitter=None,
                     flag_with_header=True,
                     flag_dsp_all_data=False):
        if len(objs) == 0:
            return None

        if display_way is None:
            display_way = DisplayData.DISPLAY_WAY_DEFAULT

        if splitter is None:
            splitter = DisplayData.DEFAULT_SPLITTER

        data_dict_list = []
        if type(objs[0]) in DisplayData.VALUE_CONVERT.keys():
            for obj in objs:
                data_dict = DisplayData._data_dict_gen(
                    obj, flag_human, flag_enum, display_way,
                    extra_properties, flag_dsp_all_data)
                data_dict_list.extend([data_dict])
        else:
            return None
        if display_way == DisplayData.DISPLAY_WAY_SCRIPT:
            DisplayData._display_data_script_way(data_dict_list, splitter)
        elif display_way == DisplayData.DISPLAY_WAY_COLUMN:
            DisplayData._display_data_column_way(
                data_dict_list, splitter, flag_with_header)
        return True

    @staticmethod
    def _display_data_script_way(data_dict_list, splitter):
        key_column_width = 1
        value_column_width = 1

        for data_dict in data_dict_list:
            for key_name in data_dict.keys():
                # find the max column width of key
                cur_key_width = len(key_name)
                if cur_key_width > key_column_width:
                    key_column_width = cur_key_width
                # find the max column width of value
                cur_value = data_dict[key_name]
                cur_value_width = 0
                if isinstance(cur_value, list):
                    if len(cur_value) == 0:
                        continue
                    cur_value_width = len(str(cur_value[0]))
                else:
                    cur_value_width = len(str(cur_value))
                if cur_value_width > value_column_width:
                    value_column_width = cur_value_width

        row_format = '%%-%ds%s%%-%ds' % (key_column_width,
                                         splitter,
                                         value_column_width)
        sub_row_format = '%s%s%%-%ds' % (' ' * key_column_width,
                                         splitter,
                                         value_column_width)
        obj_splitter = '%s%s%s' % ('-' * key_column_width,
                                   '-' * len(splitter),
                                   '-' * value_column_width)

        for data_dict in data_dict_list:
            out(obj_splitter)
            for key_name in data_dict:
                value = data_dict[key_name]
                if isinstance(value, list):
                    flag_first_data = True
                    for sub_value in value:
                        if flag_first_data:
                            out(row_format % (key_name, str(sub_value)))
                            flag_first_data = False
                        else:
                            out(sub_row_format % str(sub_value))
                else:
                    out(row_format % (key_name, str(value)))
        out(obj_splitter)

    @staticmethod
    def _display_data_column_way(data_dict_list, splitter, flag_with_header):
        if len(data_dict_list) == 0:
            return
        two_d_list = []

        item_count = len(data_dict_list[0].keys())

        # determine how many lines we will print
        row_width = 0
        for data_dict in data_dict_list:
            cur_max_wd = 0
            for key_name in data_dict.keys():
                if isinstance(data_dict[key_name], list):
                    cur_row_width = len(data_dict[key_name])
                    if cur_row_width > cur_max_wd:
                        cur_max_wd = cur_row_width
                else:
                    pass
            if cur_max_wd == 0:
                cur_max_wd = 1
            row_width += cur_max_wd

        if flag_with_header:
            # first line for header
            row_width += 1

        # init 2D list
        for raw in range(0, row_width):
            new = []
            for column in range(0, item_count):
                new.append('')
            two_d_list.append(new)

        # header
        current_row_num = -1
        if flag_with_header:
            two_d_list[0] = data_dict_list[0].keys()
            current_row_num = 0

        # Fill the 2D list with data_dict_list
        for data_dict in data_dict_list:
            current_row_num += 1
            save_row_num = current_row_num
            values = data_dict.values()
            for index in range(0, len(values)):
                value = values[index]
                if isinstance(value, list):
                    for sub_index in range(0, len(value)):
                        tmp_row_num = save_row_num + sub_index
                        two_d_list[tmp_row_num][index] = str(value[sub_index])

                    if save_row_num + len(value) > current_row_num:
                        current_row_num = save_row_num + len(value) - 1
                else:
                    two_d_list[save_row_num][index] = str(value)

        # display two_list
        row_formats = []
        header_splitter = ''
        for column_index in range(0, len(two_d_list[0])):
            max_width = DisplayData._find_max_width(two_d_list, column_index)
            row_formats.extend(['%%-%ds' % max_width])
            header_splitter += '-' * max_width
            if column_index != (len(two_d_list[0]) - 1):
                header_splitter += '-' * len(splitter)

        row_format = splitter.join(row_formats)
        for row_index in range(0, len(two_d_list)):
            out(row_format % tuple(two_d_list[row_index]))
            if row_index == 0 and flag_with_header:
                out(header_splitter)
