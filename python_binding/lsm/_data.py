# Copyright (C) 2011-2014 Red Hat, Inc.
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
# Author: tasleson
#         Gris Ge <fge@redhat.com>

from abc import ABCMeta as _ABCMeta
import re

try:
    import simplejson as json
except ImportError:
    import json

from json.decoder import WHITESPACE
from _common import get_class, default_property, ErrorNumber, LsmError


class DataEncoder(json.JSONEncoder):
    """
    Custom json encoder for objects derived form ILsmData
    """

    def default(self, my_class):
        if not isinstance(my_class, IData):
            raise ValueError('incorrect class type:' + str(type(my_class)))
        else:
            return my_class._to_dict()


class DataDecoder(json.JSONDecoder):
    """
    Custom json decoder for objects derived from ILsmData
    """

    @staticmethod
    def __process_dict(d):
        """
        Processes a dictionary
        """
        rc = {}

        if 'class' in d:
            rc = IData._factory(d)
        else:
            for (k, v) in d.iteritems():
                rc[k] = DataDecoder.__decode(v)

        return rc

    @staticmethod
    def __process_list(l):
        """
        Processes a list
        """
        rc = []
        for elem, value in enumerate(l):
            if type(value) is list:
                rc.append(DataDecoder.__process_list(value))
            elif type(value) is dict:
                rc.append(DataDecoder.__process_dict(value))
            else:
                rc.append(value)
        return rc

    @staticmethod
    def __decode(e):
        """
        Decodes the parsed json
        """
        if type(e) is dict:
            return DataDecoder.__process_dict(e)
        elif type(e) is list:
            return DataDecoder.__process_list(e)
        else:
            return e

    def decode(self, json_string, _w=WHITESPACE.match):
        return DataDecoder.__decode(json.loads(json_string))


class IData(object):
    """
    Base class functionality of serializable
    classes.
    """
    __metaclass__ = _ABCMeta

    def _to_dict(self):
        """
        Represent the class as a dictionary
        """
        rc = {'class': self.__class__.__name__}

        #If one of the attributes is another IData we will
        #process that too, is there a better way to handle this?
        for (k, v) in self.__dict__.items():
            if isinstance(v, IData):
                rc[k[1:]] = v._to_dict()
            else:
                rc[k[1:]] = v

        return rc

    @staticmethod
    def _factory(d):
        """
        Factory for creating the appropriate class given a dictionary.
        This only works for objects that inherit from IData
        """
        if 'class' in d:
            class_name = d['class']
            del d['class']
            c = get_class(__name__ + '.' + class_name)

            #If any of the parameters are themselves an IData process them
            for k, v in d.items():
                if isinstance(v, dict) and 'class' in v:
                    d['_' + k] = IData._factory(d.pop(k))
                else:
                    d['_' + k] = d.pop(k)

            return c(**d)

    def __str__(self):
        """
        Used for human string representation.
        """
        return str(self._to_dict())


@default_property('id', doc="Unique identifier")
@default_property('name', doc="Disk name (aka. vendor)")
@default_property('disk_type', doc="Enumerated type of disk")
@default_property('block_size', doc="Size of each block")
@default_property('num_of_blocks', doc="Total number of blocks")
@default_property('status', doc="Enumerated status")
@default_property('system_id', doc="System identifier")
@default_property("plugin_data", doc="Private plugin data")
class Disk(IData):
    """
    Represents a disk.
    """
    SUPPORTED_SEARCH_KEYS = ['id', 'system_id']

    # We use '-1' to indicate we failed to get the requested number.
    # For example, when block found is undetectable, we use '-1' instead of
    # confusing 0.
    BLOCK_COUNT_NOT_FOUND = -1
    BLOCK_SIZE_NOT_FOUND = -1

    TYPE_UNKNOWN = 0
    TYPE_OTHER = 1
    TYPE_ATA = 3     # IDE disk which is seldomly used.
    TYPE_SATA = 4
    TYPE_SAS = 5
    TYPE_FC = 6
    TYPE_SOP = 7     # SCSI over PCIe(SSD)
    TYPE_SCSI = 8
    TYPE_LUN = 9   # Remote LUN was treated as a disk.

    # Due to complesity of disk types, we are defining these beside DMTF
    # standards:
    TYPE_NL_SAS = 51    # Near-Line SAS==SATA disk + SAS port.

    # in DMTF CIM 2.34.0+ CIM_DiskDrive['DiskType'], they also defined
    # SSD and HYBRID disk type. We use it as faillback.
    TYPE_HDD = 52    # Normal HDD
    TYPE_SSD = 53    # Solid State Drive
    TYPE_HYBRID = 54    # uses a combination of HDD and SSD

    STATUS_UNKNOWN = 1 << 0
    STATUS_OK = 1 << 1
    STATUS_OTHER = 1 << 2
    STATUS_PREDICTIVE_FAILURE = 1 << 3
    STATUS_ERROR = 1 << 4
    STATUS_REMOVED = 1 << 5
    STATUS_STARTING = 1 << 6
    STATUS_STOPPING = 1 << 7
    STATUS_STOPPED = 1 << 8
    STATUS_INITIALIZING = 1 << 9
    STATUS_MAINTENANCE_MODE = 1 << 10
    # In maintenance for bad sector scan, integerity check and etc
    # It might be combined with STATUS_OK or
    # STATUS_STOPPED for online maintenance or offline maintenance.
    STATUS_SPARE_DISK = 1 << 11
    # Indicate disk is a spare disk.
    STATUS_RECONSTRUCT = 1 << 12
    # Indicate disk is reconstructing data.
    STATUS_FREE = 1 << 13
    # New in version 1.2, indicate the whole disk is not holding any data or
    # acting as a dedicate spare disk.
    # This disk could be assigned as a dedicated spare disk or used for
    # creating pool.
    # If any spare disk(like those on NetApp ONTAP) does not require
    # any explicit action when assigning to pool, it should be treated as
    # free disk and marked as STATUS_FREE|STATUS_SPARE_DISK.

    def __init__(self, _id, _name, _disk_type, _block_size, _num_of_blocks,
                 _status, _system_id, _plugin_data=None):
        self._id = _id
        self._name = _name
        self._disk_type = _disk_type
        self._block_size = _block_size
        self._num_of_blocks = _num_of_blocks
        self._status = _status
        self._system_id = _system_id
        self._plugin_data = _plugin_data

    @property
    def size_bytes(self):
        """
        Disk size in bytes.
        """
        return self.block_size * self.num_of_blocks

    def __str__(self):
        return self.name


# Lets do this once outside of the class to minimize the number of
# times it needs to be compiled.
_vol_regex_vpd83 = re.compile('(?:^6[0-9a-f]{31})|(?:^[235][0-9a-f]{15})$')


@default_property('id', doc="Unique identifier")
@default_property('name', doc="User given name")
@default_property('vpd83', doc="Vital product page 0x83 identifier")
@default_property('block_size', doc="Volume block size")
@default_property('num_of_blocks', doc="Number of blocks")
@default_property('admin_state', doc="Enabled or disabled by administrator")
@default_property('system_id', doc="System identifier")
@default_property('pool_id', doc="Pool identifier")
@default_property("plugin_data", doc="Private plugin data")
class Volume(IData):
    """
    Represents a volume.
    """
    SUPPORTED_SEARCH_KEYS = ['id', 'system_id', 'pool_id']

    #Replication types
    REPLICATE_UNKNOWN = -1
    REPLICATE_CLONE = 2
    REPLICATE_COPY = 3
    REPLICATE_MIRROR_SYNC = 4
    REPLICATE_MIRROR_ASYNC = 5

    #Provisioning types
    PROVISION_UNKNOWN = -1
    PROVISION_THIN = 1
    PROVISION_FULL = 2
    PROVISION_DEFAULT = 3

    ADMIN_STATE_DISABLED = 0
    ADMIN_STATE_ENABLED = 1

    RAID_TYPE_UNKNOWN = -1
    # The plugin failed to detect the volume's RAID type.
    RAID_TYPE_RAID0 = 0
    # Stripe
    RAID_TYPE_RAID1 = 1
    # Mirror for two disks. For 4 disks or more, they are RAID10.
    RAID_TYPE_RAID3 = 3
    # Byte-level striping with dedicated parity
    RAID_TYPE_RAID4 = 4
    # Block-level striping with dedicated parity
    RAID_TYPE_RAID5 = 5
    # Block-level striping with distributed parity
    RAID_TYPE_RAID6 = 6
    # Block-level striping with two distributed parities, aka, RAID-DP
    RAID_TYPE_RAID10 = 10
    # Stripe of mirrors
    RAID_TYPE_RAID15 = 15
    # Parity of mirrors
    RAID_TYPE_RAID16 = 16
    # Dual parity of mirrors
    RAID_TYPE_RAID50 = 50
    # Stripe of parities
    RAID_TYPE_RAID60 = 60
    # Stripe of dual parities
    RAID_TYPE_RAID51 = 51
    # Mirror of parities
    RAID_TYPE_RAID61 = 61
    # Mirror of dual parities
    RAID_TYPE_JBOD = 20
    # Just bunch of disks, no parity, no striping.
    RAID_TYPE_MIXED = 21
    # This volume contains multiple RAID settings.
    RAID_TYPE_OTHER = 22
    # Vendor specific RAID type

    STRIP_SIZE_UNKNOWN = 0
    DISK_COUNT_UNKNOWN = 0
    MIN_IO_SIZE_UNKNOWN = 0
    OPT_IO_SIZE_UNKNOWN = 0

    VCR_STRIP_SIZE_DEFAULT = 0

    def __init__(self, _id, _name, _vpd83, _block_size, _num_of_blocks,
                 _admin_state, _system_id, _pool_id, _plugin_data=None):
        self._id = _id                        # Identifier
        self._name = _name                    # Human recognisable name
        if _vpd83 and not Volume.vpd83_verify(_vpd83):
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Incorrect format of VPD 0x83 NAA(3) string: '%s', "
                           "expecting 32 or 16 lower case hex characters" %
                           _vpd83)
        self._vpd83 = _vpd83                  # SCSI page 83 unique ID
        self._block_size = _block_size        # Block size
        self._num_of_blocks = _num_of_blocks  # Number of blocks
        self._admin_state = _admin_state      # enable or disabled by admin
        self._system_id = _system_id          # System id this volume belongs
        self._pool_id = _pool_id              # Pool id this volume belongs
        self._plugin_data = _plugin_data

    @property
    def size_bytes(self):
        """
        Volume size in bytes.
        """
        return self.block_size * self.num_of_blocks

    def __str__(self):
        return self.name

    @staticmethod
    def vpd83_verify(vpd):
        """
        Returns True if string is valid vpd 0x83 representation
        """
        if vpd and _vol_regex_vpd83.match(vpd):
            return True
        return False


@default_property('id', doc="Unique identifier")
@default_property('name', doc="User defined system name")
@default_property('status', doc="Enumerated status of system")
@default_property('status_info', doc="Detail status information of system")
@default_property("plugin_data", doc="Private plugin data")
class System(IData):
    STATUS_UNKNOWN = 1 << 0
    STATUS_OK = 1 << 1
    STATUS_ERROR = 1 << 2
    STATUS_DEGRADED = 1 << 3
    STATUS_PREDICTIVE_FAILURE = 1 << 4
    STATUS_OTHER = 1 << 5

    def __init__(self, _id, _name, _status, _status_info, _plugin_data=None):
        self._id = _id
        self._name = _name
        self._status = _status
        self._status_info = _status_info
        self._plugin_data = _plugin_data


@default_property('id', doc="Unique identifier")
@default_property('name', doc="User supplied name")
@default_property('total_space', doc="Total space in bytes")
@default_property('free_space', doc="Free space in bytes")
@default_property('status', doc="Enumerated status")
@default_property('status_info', doc="Text explaining status")
@default_property('system_id', doc="System identifier")
@default_property("plugin_data", doc="Plug-in private data")
@default_property("element_type", doc="What pool can be used for")
@default_property("unsupported_actions",
                  doc="What cannot be done with this pool")
class Pool(IData):
    """
    Pool specific information
    """
    SUPPORTED_SEARCH_KEYS = ['id', 'system_id']

    TOTAL_SPACE_NOT_FOUND = -1
    FREE_SPACE_NOT_FOUND = -1

    # Element Type indicate what kind of element could this pool create:
    #   * Another Pool
    #   * Volume (aka, LUN)
    #   * System Reserved Pool.
    ELEMENT_TYPE_POOL = 1 << 1
    ELEMENT_TYPE_VOLUME = 1 << 2
    ELEMENT_TYPE_FS = 1 << 3
    ELEMENT_TYPE_DELTA = 1 << 4
    ELEMENT_TYPE_VOLUME_FULL = 1 << 5
    ELEMENT_TYPE_VOLUME_THIN = 1 << 6
    ELEMENT_TYPE_SYS_RESERVED = 1 << 10     # Reserved for system use

    # Unsupported actions, what pool cannot be used for
    UNSUPPORTED_VOLUME_GROW = 1 << 0
    UNSUPPORTED_VOLUME_SHRINK = 1 << 1

    # Pool status could be any combination of these status.
    STATUS_UNKNOWN = 1 << 0
    STATUS_OK = 1 << 1
    STATUS_OTHER = 1 << 2
    STATUS_DEGRADED = 1 << 4
    STATUS_ERROR = 1 << 5
    STATUS_STOPPED = 1 << 9
    STATUS_RECONSTRUCTING = 1 << 12
    STATUS_VERIFYING = 1 << 13
    STATUS_INITIALIZING = 1 << 14
    STATUS_GROWING = 1 << 15

    MEMBER_TYPE_UNKNOWN = 0
    MEMBER_TYPE_OTHER = 1
    MEMBER_TYPE_DISK = 2
    MEMBER_TYPE_POOL = 3

    def __init__(self, _id, _name, _element_type, _unsupported_actions,
                 _total_space, _free_space,
                 _status, _status_info, _system_id, _plugin_data=None):
        self._id = _id                      # Identifier
        self._name = _name                  # Human recognisable name
        self._element_type = _element_type  # What pool can be used to create
        self._unsupported_actions = _unsupported_actions  # What pool cannot be
                                            # used for
        self._total_space = _total_space    # Total size
        self._free_space = _free_space      # Free space available
        self._status = _status              # Status of pool.
        self._status_info = _status_info    # Additional status text of pool
        self._system_id = _system_id        # System id this pool belongs
        self._plugin_data = _plugin_data    # Plugin private data


@default_property('id', doc="Unique identifier")
@default_property('name', doc="File system name")
@default_property('total_space', doc="Total space in bytes")
@default_property('free_space', doc="Free space available")
@default_property('pool_id', doc="What pool the file system resides on")
@default_property('system_id', doc="System ID")
@default_property("plugin_data", "Private plugin data")
class FileSystem(IData):
    SUPPORTED_SEARCH_KEYS = ['id', 'system_id', 'pool_id']

    def __init__(self, _id, _name, _total_space, _free_space, _pool_id,
                 _system_id, _plugin_data=None):
        self._id = _id
        self._name = _name
        self._total_space = _total_space
        self._free_space = _free_space
        self._pool_id = _pool_id
        self._system_id = _system_id
        self._plugin_data = _plugin_data


@default_property('id', doc="Unique identifier")
@default_property('name', doc="Snapshot name")
@default_property('ts', doc="Time stamp the snapshot was created")
@default_property("plugin_data", "Private plugin data")
class FsSnapshot(IData):

    def __init__(self, _id, _name, _ts, _plugin_data=None):
        self._id = _id
        self._name = _name
        self._ts = int(_ts)
        self._plugin_data = _plugin_data


@default_property('id', doc="Unique identifier")
@default_property('fs_id', doc="Filesystem that is exported")
@default_property('export_path', doc="Export path")
@default_property('auth', doc="Authentication type")
@default_property('root', doc="List of hosts with no_root_squash")
@default_property('rw', doc="List of hosts with Read & Write privileges")
@default_property('ro', doc="List of hosts with Read only privileges")
@default_property('anonuid', doc="UID for anonymous user id")
@default_property('anongid', doc="GID for anonymous group id")
@default_property('options', doc="String containing advanced options")
@default_property('plugin_data', doc="Plugin private data")
class NfsExport(IData):
    SUPPORTED_SEARCH_KEYS = ['id', 'fs_id']
    ANON_UID_GID_NA = -1
    ANON_UID_GID_ERROR = -2

    def __init__(self, _id, _fs_id, _export_path, _auth, _root, _rw, _ro,
                 _anonuid, _anongid, _options, _plugin_data=None):
        assert (_fs_id is not None)
        assert (_export_path is not None)

        self._id = _id
        self._fs_id = _fs_id          # File system exported
        self._export_path = _export_path     # Export path
        self._auth = _auth            # Authentication type
        self._root = _root            # List of hosts with no_root_squash
        self._rw = _rw                # List of hosts with read/write
        self._ro = _ro                # List of hosts with read/only
        self._anonuid = _anonuid      # uid for anonymous user id
        self._anongid = _anongid      # gid for anonymous group id
        self._options = _options      # NFS options
        self._plugin_data = _plugin_data


@default_property('src_block', doc="Source logical block address")
@default_property('dest_block', doc="Destination logical block address")
@default_property('block_count', doc="Number of blocks")
class BlockRange(IData):
    def __init__(self, _src_block, _dest_block, _block_count):
        self._src_block = _src_block
        self._dest_block = _dest_block
        self._block_count = _block_count


@default_property('id', doc="Unique instance identifier")
@default_property('name', doc="Access group name")
@default_property('init_ids', doc="List of initiator IDs")
@default_property('init_type', doc="Initiator type")
@default_property('system_id', doc="System identifier")
@default_property('plugin_data', doc="Plugin private data")
class AccessGroup(IData):
    SUPPORTED_SEARCH_KEYS = ['id', 'system_id']

    INIT_TYPE_UNKNOWN = 0
    INIT_TYPE_OTHER = 1
    INIT_TYPE_WWPN = 2
    INIT_TYPE_ISCSI_IQN = 5
    INIT_TYPE_ISCSI_WWPN_MIXED = 7

    def __init__(self, _id, _name, _init_ids, _init_type, _system_id,
                 _plugin_data=None):
        self._id = _id
        self._name = _name                # AccessGroup name
        self._init_ids = AccessGroup._standardize_init_list(_init_ids)
                                          # A list of Initiator ID strings.
        self._init_type = _init_type
        self._system_id = _system_id      # System id this group belongs
        self._plugin_data = _plugin_data

    @staticmethod
    def _standardize_init_list(init_ids):
        rc = []
        for i in init_ids:
            valid, init_type, init_id = AccessGroup.initiator_id_verify(i)

            if valid:
                rc.append(init_id)
            else:
                raise LsmError(LsmError.ErrorNumber.INVALID_ARGUMENT,
                               "Invalid initiator ID %s" % i)
        return rc

    _regex_wwpn = re.compile(r"""
        ^(0x|0X)?([0-9A-Fa-f]{2})
        (([\.:\-])?[0-9A-Fa-f]{2}){7}$
        """, re.X)

    @staticmethod
    def initiator_id_verify(init_id, init_type=None, raise_exception=False):
        """
        Public method which can be used to verify an initiator id
        :param init_id:
        :param init_type:
        :param raise_exception: Will throw a LsmError INVALID_ARGUMENT if
                                not a valid initiator address
        :return:(Bool, init_type, init_id)  Note: init_id will be returned in
                normalized format if it's a WWPN
        """
        if init_id.startswith('iqn') or init_id.startswith('eui') or\
                init_id.startswith('naa'):

            if init_type is None or \
                    init_type == AccessGroup.INIT_TYPE_ISCSI_IQN:
                return True, AccessGroup.INIT_TYPE_ISCSI_IQN, init_id
        if AccessGroup._regex_wwpn.match(str(init_id)):
            if init_type is None or \
                    init_type == AccessGroup.INIT_TYPE_WWPN:
                return (True, AccessGroup.INIT_TYPE_WWPN,
                        AccessGroup._wwpn_to_lsm_type(init_id))

        if raise_exception:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Initiator id '%s' is invalid" % init_id)

        return False, None, None

    @staticmethod
    def _wwpn_to_lsm_type(wwpn, raise_error=True):
        """
        Conver provided WWPN string into LSM standarded one:

        LSM WWPN format:
            ^(?:[0-9a-f]{2}:){7}[0-9a-f]{2}$
        LSM WWPN Example:
           10:00:00:00:c9:95:2f:de

        Acceptable WWPN format is:
            ^[0x|0X]{0,1}(:?[0-9A-Fa-f]{2}[\.\-:]{0,1}){7}[0-9A-Fa-f]{2}$
        Acceptable WWPN example:
           10:00:00:00:c9:95:2f:de
           10:00:00:00:C9:95:2F:DE
           10-00-00-00-C9-95-2F-DE
           10-00-00-00-c9-95-2f-de
           10.00.00.00.C9.95.2F.DE
           10.00.00.00.c9.95.2f.de
           0x10000000c9952fde
           0X10000000C9952FDE
           10000000c9952fde
           10000000C9952FDE
        Return the LSM WWPN
        Return None if raise_error is False and not a valid WWPN.
        """
        if AccessGroup._regex_wwpn.match(str(wwpn)):
            s = str(wwpn)
            s = s.lower()
            s = re.sub(r'0x', '', s)
            s = re.sub(r'[^0-9a-f]', '', s)
            s = ":".join(re.findall(r'..', s))
            return s
        if raise_error:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Invalid WWPN Initiator: %s" % wwpn)
        return None


@default_property('id', doc="Unique instance identifier")
@default_property('port_type', doc="Target port type")
@default_property('service_address', doc="Target port service address")
@default_property('network_address', doc="Target port network address")
@default_property('physical_address', doc="Target port physical address")
@default_property('physical_name', doc="Target port physical port name")
@default_property('system_id', doc="System identifier")
@default_property('plugin_data', doc="Plugin private data")
class TargetPort(IData):
    SUPPORTED_SEARCH_KEYS = ['id', 'system_id']

    TYPE_OTHER = 1
    TYPE_FC = 2
    TYPE_FCOE = 3
    TYPE_ISCSI = 4

    def __init__(self, _id, _port_type, _service_address,
                 _network_address, _physical_address, _physical_name,
                 _system_id, _plugin_data=None):
        self._id = _id
        self._port_type = _port_type
        self._service_address = _service_address
        # service_address:
        #   The address used by upper layer like FC and iSCSI:
        #       FC and FCoE:    WWPN
        #       iSCSI:          IQN
        #   String. Lower case, split with : every two digits if WWPN.
        self._network_address = _network_address
        # network_address:
        #   The address used by network layer like FC and TCP/IP:
        #       FC/FCoE:        WWPN
        #       iSCSI:          IPv4:Port
        #                       [IPv6]:Port
        #   String. Lower case, split with : every two digits if WWPN.
        self._physical_address = _physical_address
        # physical_address:
        #   The address used by physical layer like FC-0 and MAC:
        #       FC:             WWPN
        #       FCoE:           WWPN
        #       iSCSI:          MAC
        #   String. Lower case, split with : every two digits.
        self._physical_name = _physical_name
        # physical_name
        #   The name of physical port. Administrator could use this name to
        #   locate the port on storage system.
        #   String.
        self._system_id = _system_id
        self._plugin_data = _plugin_data


class Capabilities(IData):
    UNSUPPORTED = 0
    SUPPORTED = 1

    _NUM = 512              # Indicate the maximum capability integer

    _CAP_NUM_BEGIN = 20     # Indicate the first capability integer

    #Block operations
    VOLUMES = 20
    VOLUME_CREATE = 21
    VOLUME_RESIZE = 22

    VOLUME_REPLICATE = 23
    VOLUME_REPLICATE_CLONE = 24
    VOLUME_REPLICATE_COPY = 25
    VOLUME_REPLICATE_MIRROR_ASYNC = 26
    VOLUME_REPLICATE_MIRROR_SYNC = 27

    VOLUME_COPY_RANGE_BLOCK_SIZE = 28
    VOLUME_COPY_RANGE = 29
    VOLUME_COPY_RANGE_CLONE = 30
    VOLUME_COPY_RANGE_COPY = 31

    VOLUME_DELETE = 33

    VOLUME_ENABLE = 34
    VOLUME_DISABLE = 35

    VOLUME_MASK = 36
    VOLUME_UNMASK = 37
    ACCESS_GROUPS = 38
    ACCESS_GROUP_CREATE_WWPN = 39
    ACCESS_GROUP_DELETE = 40
    ACCESS_GROUP_INITIATOR_ADD_WWPN = 41
    # For empty access group, this indicate it can add WWPN into it.
    ACCESS_GROUP_INITIATOR_DELETE = 42

    VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP = 43
    ACCESS_GROUPS_GRANTED_TO_VOLUME = 44

    VOLUME_CHILD_DEPENDENCY = 45
    VOLUME_CHILD_DEPENDENCY_RM = 46

    ACCESS_GROUP_CREATE_ISCSI_IQN = 47
    ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN = 48
    # For empty access group, this indicate it can add iSCSI IQN into it.

    VOLUME_ISCSI_CHAP_AUTHENTICATION = 53

    VOLUME_RAID_INFO = 54

    VOLUME_THIN = 55

    #File system
    FS = 100
    FS_DELETE = 101
    FS_RESIZE = 102
    FS_CREATE = 103
    FS_CLONE = 104
    FILE_CLONE = 105
    FS_SNAPSHOTS = 106
    FS_SNAPSHOT_CREATE = 107
    FS_SNAPSHOT_DELETE = 109
    FS_SNAPSHOT_RESTORE = 110
    FS_SNAPSHOT_RESTORE_SPECIFIC_FILES = 111
    FS_CHILD_DEPENDENCY = 112
    FS_CHILD_DEPENDENCY_RM = 113
    FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES = 114

    #NFS
    EXPORT_AUTH = 120
    EXPORTS = 121
    EXPORT_FS = 122
    EXPORT_REMOVE = 123
    EXPORT_CUSTOM_PATH = 124

    POOLS_QUICK_SEARCH = 210
    VOLUMES_QUICK_SEARCH = 211
    DISKS_QUICK_SEARCH = 212
    ACCESS_GROUPS_QUICK_SEARCH = 213
    FS_QUICK_SEARCH = 214
    NFS_EXPORTS_QUICK_SEARCH = 215
    TARGET_PORTS = 216
    TARGET_PORTS_QUICK_SEARCH = 217

    DISKS = 220
    POOL_MEMBER_INFO = 221
    VOLUME_RAID_CREATE = 222

    def _to_dict(self):
        return {'class': self.__class__.__name__,
                'cap': ''.join(['%02x' % b for b in self._cap])}

    def __init__(self, _cap=None):
        if _cap is not None:
            self._cap = bytearray(_cap.decode('hex'))
        else:
            self._cap = bytearray(Capabilities._NUM)

    def supported(self, capability):
        return self.get(capability) == Capabilities.SUPPORTED

    def get(self, capability):
        if capability >= len(self._cap):
            return Capabilities.UNSUPPORTED
        return self._cap[capability]

    @staticmethod
    def _lsm_cap_to_str_dict():
        """
        Return a dict containing all valid capability:
            integer => string name
        """
        lsm_cap_to_str_conv = dict()
        for c_str, c_int in Capabilities.__dict__.items():
            if type(c_str) == str and type(c_int) == int and \
                    c_str[0] != '_' and \
                    Capabilities._CAP_NUM_BEGIN <= c_int <= Capabilities._NUM:
                lsm_cap_to_str_conv[c_int] = c_str
        return lsm_cap_to_str_conv

    def get_supported(self, all_cap=False):
        """
        Returns a hash of the supported capabilities in the form
        constant, name
        """
        all_caps = Capabilities._lsm_cap_to_str_dict()

        if all_cap:
            return all_caps

        rc = {}
        for i in all_caps.keys():
            if self._cap[i] == Capabilities.SUPPORTED:
                if i in all_caps:
                    rc[i] = all_caps[i]
        return rc

    def set(self, capability, value=SUPPORTED):
        self._cap[capability] = value

    def enable_all(self):
        for i in range(len(self._cap)):
            self._cap[i] = Capabilities.SUPPORTED


if __name__ == '__main__':
    #TODO Need some unit tests that encode/decode all the types with nested
    pass
