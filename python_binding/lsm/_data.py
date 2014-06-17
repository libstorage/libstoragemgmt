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
# License along with this library; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
#
# Author: tasleson
#         Gris Ge <fge@redhat.com>

from abc import ABCMeta as _ABCMeta

try:
    import simplejson as json
except ImportError:
    import json

from json.decoder import WHITESPACE
from lsm import LsmError, ErrorNumber
from _common import get_class, default_property


def txt_a(txt, append):
    if len(txt):
        return txt + ',' + append
    else:
        return append


def get_key(dictionary, value):
    keys = [k for k, v in dictionary.items() if v == value]
    if len(keys) > 0:
        return keys[0]
    return None


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
        decoded = json.loads(json_string)
        decoded = DataDecoder.__decode(decoded)
        return decoded


class IData(object):
    """
    Base class functionality of serializable
    classes.
    """
    __metaclass__ = _ABCMeta

    OPT_PROPERTIES = []

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

            i = c(**d)
            return i

    def __str__(self):
        """
        Used for human string representation.
        """
        return str(self._to_dict())

    def _check_opt_data(self, optional_data):
        if optional_data is None:
            return OptionalData()
        else:
            #Make sure the properties only contain ones we permit
            allowed = set(self.OPT_PROPERTIES)
            actual = set(optional_data.keys())

            if actual <= allowed:
                return optional_data
            else:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               "Property keys are not supported: %s" %
                               "".join(actual - allowed))


@default_property('id', doc="Unique identifier")
@default_property('name', doc="Disk name (aka. vendor)")
@default_property('disk_type', doc="Enumerated type of disk")
@default_property('block_size', doc="Size of each block")
@default_property('num_of_blocks', doc="Total number of blocks")
@default_property('status', doc="Enumerated status")
@default_property('system_id', doc="System identifier")
@default_property("optional_data", doc="Optional data")
@default_property("plugin_data", doc="Private plugin data")
class Disk(IData):
    """
    Represents a disk.
    """
    SUPPORTED_SEARCH_KEYS = ['id', 'system_id']
    FLAG_RETRIEVE_FULL_INFO = 1 << 0

    # We use '-1' to indicate we failed to get the requested number.
    # For example, when block found is undetectable, we use '-1' instead of
    # confusing 0.
    BLOCK_COUNT_NOT_FOUND = -1
    BLOCK_SIZE_NOT_FOUND = -1

    DISK_TYPE_UNKNOWN = 0
    DISK_TYPE_OTHER = 1
    DISK_TYPE_NOT_APPLICABLE = 2
    DISK_TYPE_ATA = 3     # IDE disk which is seldomly used.
    DISK_TYPE_SATA = 4
    DISK_TYPE_SAS = 5
    DISK_TYPE_FC = 6
    DISK_TYPE_SOP = 7     # SCSI over PCIe(SSD)
    DISK_TYPE_SCSI = 8
    DISK_TYPE_LUN = 9   # Remote LUN was treated as a disk.

    # Due to complesity of disk types, we are defining these beside DMTF
    # standards:
    DISK_TYPE_NL_SAS = 51    # Near-Line SAS==SATA disk + SAS port.

    # in DMTF CIM 2.34.0+ CIM_DiskDrive['DiskType'], they also defined
    # SSD and HYBRID disk type. We use it as faillback.
    DISK_TYPE_HDD = 52    # Normal HDD
    DISK_TYPE_SSD = 53    # Solid State Drive
    DISK_TYPE_HYBRID = 54    # uses a combination of HDD and SSD

    STATUS_UNKNOWN = 1 << 0
    STATUS_OK = 1 << 1
    STATUS_OTHER = 1 << 2
    STATUS_PREDICTIVE_FAILURE = 1 << 3
    STATUS_ERROR = 1 << 4
    STATUS_OFFLINE = 1 << 5
    STATUS_STARTING = 1 << 6
    STATUS_STOPPING = 1 << 7
    STATUS_STOPPED = 1 << 8
    STATUS_INITIALIZING = 1 << 9

    OPT_PROPERTIES = ['sn', 'part_num', 'vendor', 'model']

    def __init__(self, _id, _name, _disk_type, _block_size, _num_of_blocks,
                 _status, _system_id, _optional_data=None, _plugin_data=None):
        self._id = _id
        self._name = _name
        self._disk_type = _disk_type
        self._block_size = _block_size
        self._num_of_blocks = _num_of_blocks
        self._status = _status
        self._system_id = _system_id
        self._optional_data = self._check_opt_data(_optional_data)
        self._plugin_data = _plugin_data

    @property
    def size_bytes(self):
        """
        Disk size in bytes.
        """
        return self.block_size * self.num_of_blocks

    def __str__(self):
        return self.name


@default_property('id', doc="Unique identifier")
@default_property('name', doc="User given name")
@default_property('vpd83', doc="Vital product page 0x83 identifier")
@default_property('block_size', doc="Volume block size")
@default_property('num_of_blocks', doc="Number of blocks")
@default_property('status', doc="Enumerated volume status")
@default_property('system_id', doc="System identifier")
@default_property('pool_id', doc="Pool identifier")
@default_property("optional_data", doc="Optional data")
@default_property("plugin_data", doc="Private plugin data")
class Volume(IData):
    """
    Represents a volume.
    """
    FLAG_RETRIEVE_FULL_INFO = 1 << 0

    SUPPORTED_SEARCH_KEYS = ['id', 'system_id', 'pool_id']
    # Volume status Note: Volumes can have multiple status bits set at same
    # time.
    (STATUS_UNKNOWN, STATUS_OK, STATUS_DEGRADED, STATUS_ERR, STATUS_STARTING,
     STATUS_DORMANT) = (0x0, 0x1, 0x2, 0x4, 0x8, 0x10)

    #Replication types
    (REPLICATE_UNKNOWN, REPLICATE_SNAPSHOT, REPLICATE_CLONE, REPLICATE_COPY,
     REPLICATE_MIRROR_SYNC, REPLICATE_MIRROR_ASYNC) = \
        (-1, 1, 2, 3, 4, 5)

    #Provisioning types
    (PROVISION_UNKNOWN, PROVISION_THIN, PROVISION_FULL, PROVISION_DEFAULT) = \
        (-1, 1, 2, 3)

    def __init__(self, _id, _name, _vpd83, _block_size, _num_of_blocks,
                 _status, _system_id, _pool_id, _optional_data=None,
                 _plugin_data=None):
        self._id = _id                        # Identifier
        self._name = _name                    # Human recognisable name
        self._vpd83 = _vpd83                  # SCSI page 83 unique ID
        self._block_size = _block_size        # Block size
        self._num_of_blocks = _num_of_blocks  # Number of blocks
        self._status = _status                # Status
        self._system_id = _system_id          # System id this volume belongs
        self._pool_id = _pool_id              # Pool id this volume belongs
        self._optional_data = self._check_opt_data(_optional_data)
        self._plugin_data = _plugin_data

    @property
    def size_bytes(self):
        """
        Volume size in bytes.
        """
        return self.block_size * self.num_of_blocks

    def __str__(self):
        return self.name


@default_property('id', doc="Unique identifier")
@default_property('name', doc="User defined system name")
@default_property('status', doc="Enumerated status of system")
@default_property('status_info', doc="Detail status information of system")
@default_property("optional_data", doc="Optional data")
@default_property("plugin_data", doc="Private plugin data")
class System(IData):
    """
### 11.3 System -- lsm.System

#### 11.3.1 System Properties
 * id
   String. Free form string used to identify certain system at plugin level.
   Plugin can use this property for performance improvement
   when concerting between LSM object to internal object. When displaying this
   property to user, use the ID hashed string(like md5) is suggested.
 * name
   String. Human friendly name for this system.
 * status
   Integer. Byte Map(Check Appendix.D). The health status of system.
   Could be any combination of these values:
    * **lsm.System.STATUS_UNKNOWN**
      Plugin failed to determine the status.
    * **lsm.System.STATUS_OK**
      Everything is OK.
    * **lsm.System.STATUS_ERROR**
      System is having errors which causing 'Data Unavailable' or 'Data Lose'.
      Example:
        * A RAID5 pool lose two disks.
        * All controllers down.
        * Internal hardware(like, memory) down and no redundant part.
      The 'status_info' property will explain the detail.
    * **lsm.System.STATUS_DEGRADED**
      System is still functional but lose protection of redundant parts,
      Example:
        * One or more controller offline, but existing controller is taking
          over all works.
        * A RAID 5 pool lose 1 disk, no spare disk or spare disk is rebuilding.
        * One or more battery changed from online to offline.
      The 'status_info' property will explain the detail.
    * **lsm.System.STATUS_PREDICTIVE_FAILURE**
      System is still functional and protected by redundant parts, but
      certain parts will soon be unfunctional.
        * One or more battery voltage low.
        * SMART information indicate some disk is dieing.
      The 'status_info' property will explain the detail.
    * **lsm.System.STATUS_STRESSED**
      System is having too much I/O in progress or temperature exceeded the
      limit. The 'status_info' property will explain the detail.
    * **lsm.System.STATUS_STARTING**
      System is booting up.
    * **lsm.System.STATUS_STOPPING**
      System is shutting down.
    * **lsm.System.STATUS_STOPPED**
      System is stopped by administrator.
    * **lsm.System.STATUS_OTHER**
      Vendor specifice status. The 'status_info' property will explain the
      detail.
 * status_info
   String. Free form string used for explaining system status. For example:
   "Disk <disk_id> is in Offline state. Battery X is near end of life"

##### 11.3.2 System Optional Properties

The lsm.System class does not have any optional properties yet.

##### 11.3.3 System Extra Constants

The lsm.System class does not have any extra constants.

##### 11.3.4 System Class Methods

The lsm.System class does not have class methods.
    """
    FLAG_RETRIEVE_FULL_INFO = 1 << 0

    STATUS_UNKNOWN = 1 << 0
    STATUS_OK = 1 << 1
    STATUS_ERROR = 1 << 2
    STATUS_DEGRADED = 1 << 3
    STATUS_PREDICTIVE_FAILURE = 1 << 4
    STATUS_STRESSED = 1 << 5
    STATUS_STARTING = 1 << 6
    STATUS_STOPPING = 1 << 7
    STATUS_STOPPED = 1 << 8
    STATUS_OTHER = 1 << 9

    def __init__(self, _id, _name, _status, _status_info, _optional_data=None,
                 _plugin_data=None):
        self._id = _id
        self._name = _name
        self._status = _status
        self._status_info = _status_info
        self._optional_data = self._check_opt_data(_optional_data)
        self._plugin_data = _plugin_data


@default_property('id', doc="Unique identifier")
@default_property('name', doc="User supplied name")
@default_property('total_space', doc="Total space in bytes")
@default_property('free_space', doc="Free space in bytes")
@default_property('status', doc="Enumerated status")
@default_property('status_info', doc="Text explaining status")
@default_property('system_id', doc="System identifier")
@default_property("optional_data", doc="Optional data")
@default_property("plugin_data", doc="Plug-in private data")
class Pool(IData):
    """
    Pool specific information
    """
    FLAG_RETRIEVE_FULL_INFO = 1 << 0
    SUPPORTED_SEARCH_KEYS = ['id', 'system_id']

    TOTAL_SPACE_NOT_FOUND = -1
    FREE_SPACE_NOT_FOUND = -1
    STRIPE_SIZE_NOT_FOUND = -1

    # RAID_xx name was following SNIA SMI-S 1.4 rev6 Block Book,
    # section '14.1.5.3', Table 255 - Supported Common RAID Levels
    RAID_TYPE_RAID0 = 0
    RAID_TYPE_RAID1 = 1
    RAID_TYPE_RAID3 = 3
    RAID_TYPE_RAID4 = 4
    RAID_TYPE_RAID5 = 5
    RAID_TYPE_RAID6 = 6
    RAID_TYPE_RAID10 = 10
    RAID_TYPE_RAID15 = 15
    RAID_TYPE_RAID16 = 16
    RAID_TYPE_RAID50 = 50
    RAID_TYPE_RAID60 = 60
    RAID_TYPE_RAID51 = 51
    RAID_TYPE_RAID61 = 61
    # number 2x is reserved for non-numbered RAID.
    RAID_TYPE_JBOD = 20
    RAID_TYPE_UNKNOWN = 21
    RAID_TYPE_NOT_APPLICABLE = 22
    # NOT_APPLICABLE indicate current pool only has one member.
    RAID_TYPE_MIXED = 23

    MEMBER_TYPE_UNKNOWN = 0
    MEMBER_TYPE_DISK = 1
    MEMBER_TYPE_DISK_MIX = 10
    MEMBER_TYPE_DISK_ATA = 11
    MEMBER_TYPE_DISK_SATA = 12
    MEMBER_TYPE_DISK_SAS = 13
    MEMBER_TYPE_DISK_FC = 14
    MEMBER_TYPE_DISK_SOP = 15
    MEMBER_TYPE_DISK_SCSI = 16
    MEMBER_TYPE_DISK_NL_SAS = 17
    MEMBER_TYPE_DISK_HDD = 18
    MEMBER_TYPE_DISK_SSD = 19
    MEMBER_TYPE_DISK_HYBRID = 110
    MEMBER_TYPE_DISK_LUN = 111

    MEMBER_TYPE_POOL = 2
    MEMBER_TYPE_VOLUME = 3

    _MEMBER_TYPE_2_DISK_TYPE = {
        MEMBER_TYPE_DISK: Disk.DISK_TYPE_UNKNOWN,
        MEMBER_TYPE_DISK_MIX: Disk.DISK_TYPE_UNKNOWN,
        MEMBER_TYPE_DISK_ATA: Disk.DISK_TYPE_ATA,
        MEMBER_TYPE_DISK_SATA: Disk.DISK_TYPE_SATA,
        MEMBER_TYPE_DISK_SAS: Disk.DISK_TYPE_SAS,
        MEMBER_TYPE_DISK_FC: Disk.DISK_TYPE_FC,
        MEMBER_TYPE_DISK_SOP: Disk.DISK_TYPE_SOP,
        MEMBER_TYPE_DISK_SCSI: Disk.DISK_TYPE_SCSI,
        MEMBER_TYPE_DISK_NL_SAS: Disk.DISK_TYPE_NL_SAS,
        MEMBER_TYPE_DISK_HDD: Disk.DISK_TYPE_HDD,
        MEMBER_TYPE_DISK_SSD: Disk.DISK_TYPE_SSD,
        MEMBER_TYPE_DISK_HYBRID: Disk.DISK_TYPE_HYBRID,
        MEMBER_TYPE_DISK_LUN: Disk.DISK_TYPE_LUN,
    }

    @staticmethod
    def member_type_is_disk(member_type):
        """
        Returns True if defined 'member_type' is disk.
        False when else.
        """
        if member_type in Pool._MEMBER_TYPE_2_DISK_TYPE.keys():
            return True
        return False

    @staticmethod
    def member_type_to_disk_type(member_type):
        """
        Convert member_type to disk_type.
        For non-disk member, we return Disk.DISK_TYPE_NOT_APPLICABLE
        """
        if member_type in Pool._MEMBER_TYPE_2_DISK_TYPE.keys():
            return Pool._MEMBER_TYPE_2_DISK_TYPE[member_type]
        return Disk.DISK_TYPE_NOT_APPLICABLE

    @staticmethod
    def disk_type_to_member_type(disk_type):
        """
        Convert disk_type to Pool.MEMBER_TYPE_DISK_XXXX
        Will return Pool.MEMBER_TYPE_DISK as failback.
        """
        key = get_key(Pool._MEMBER_TYPE_2_DISK_TYPE, disk_type)
        if key or key == 0:
            return key
        return Pool.MEMBER_TYPE_DISK

    THINP_TYPE_UNKNOWN = 0
    THINP_TYPE_THIN = 1
    THINP_TYPE_THICK = 5
    THINP_TYPE_NOT_APPLICABLE = 6
    # NOT_APPLICABLE means current pool is not implementing Thin Provisioning,
    # but can create thin or thick pool from it.

    # Element Type indicate what kind of element could this pool create:
    #   * Another Pool
    #   * Volume (aka, LUN)
    #   * System Reserved Pool.
    ELEMENT_TYPE_UNKNOWN = 1 << 0
    ELEMENT_TYPE_POOL = 1 << 1
    ELEMENT_TYPE_VOLUME = 1 << 2
    ELEMENT_TYPE_FS = 1 << 3
    ELEMENT_TYPE_SYS_RESERVED = 1 << 10     # Reserved for system use

    MAX_POOL_STATUS_BITS = 64
    # Pool status could be any combination of these status.
    STATUS_UNKNOWN = 1 << 0
    # UNKNOWN:
    #   Failed to query out the status of Pool.
    STATUS_OK = 1 << 1
    # OK:
    #   Pool is accessible with no issue.
    STATUS_OTHER = 1 << 2
    # OTHER:
    #   Should explain in Pool.status_info for detail.
    STATUS_STRESSED = 1 < 3
    # STRESSED:
    #   Pool is under heavy workload which cause bad I/O performance.
    STATUS_DEGRADED = 1 << 4
    # DEGRADED:
    #   Pool is accessible but lost full RAID protection due to
    #   I/O error or offline of one or more RAID member.
    #   Example:
    #    * RAID 6 pool lost access to 1 disk or 2 disks.
    #    * RAID 5 pool lost access to 1 disk.
    #   May explain detail in Pool.status_info.
    #   Example:
    #    * Pool.status = 'Disk 0_0_1 offline'
    STATUS_ERROR = 1 << 5
    # OFFLINE:
    #   Pool is not accessible for internal issue.
    #   Should explain in Pool.status_info for reason.
    STATUS_STARTING = 1 << 7
    # STARTING:
    #   Pool is reviving from STOPPED status. Pool is not accessible.
    STATUS_STOPPING = 1 << 8
    # STOPPING:
    #   Pool is stopping by administrator. Pool is not accessible.
    STATUS_STOPPED = 1 << 9
    # STOPPING:
    #   Pool is stopped by administrator. Pool is not accessible.
    STATUS_READ_ONLY = 1 << 10
    # READ_ONLY:
    #   Pool is read only.
    #   Pool.status_info should explain why.
    STATUS_DORMANT = 1 << 11
    # DORMANT:
    #   Pool is not accessible.
    #   It's not stopped by administrator, but stopped for some mechanism.
    #   For example, The DR pool acting as the SYNC replication target will be
    #   in DORMANT state, As long as the PR(production) pool alive.
    #   Another example could relocating.
    STATUS_RECONSTRUCTING = 1 << 12
    # RECONSTRUCTING:
    #   Pool is reconstructing the hash data or mirror data.
    #   Mostly happen when disk revive from offline or disk replaced.
    #   Pool.status_info can contain progress of this reconstruction job.
    STATUS_VERIFYING = 1 << 13
    # VERIFYING:
    #   Array is running integrity check on data of current pool.
    #   It might be started by administrator or array itself.
    #   Pool.status_info can contain progress of this verification job.
    STATUS_INITIALIZING = 1 << 14
    # INITIALIZING:
    #   Pool is in initialing state.
    #   Mostly shown when new pool created or array boot up.
    STATUS_GROWING = 1 << 15
    # GROWING:
    #   Pool is growing its size and doing internal jobs.
    #   Pool.status_info can contain progress of this growing job.
    STATUS_SHRINKING = 1 << 16
    # SHRINKING:
    #   Pool is shrinking its size and doing internal jobs.
    #   Pool.status_info can contain progress of this shrinking job.
    STATUS_DESTROYING = 1 << 17
    # DESTROYING:
    #   Array is removing current pool.

    OPT_PROPERTIES = ['raid_type', 'member_type', 'member_ids',
                      'element_type', 'thinp_type']

    def __init__(self, _id, _name, _total_space, _free_space, _status,
                 _status_info, _system_id, _optional_data=None,
                 _plugin_data=None):
        self._id = _id                    # Identifier
        self._name = _name                # Human recognisable name
        self._total_space = _total_space  # Total size
        self._free_space = _free_space    # Free space available
        self._status = _status            # Status of pool.
        self._status_info = _status_info  # Additional status text of pool
        self._system_id = _system_id      # System id this pool belongs
        self._plugin_data = _plugin_data  # Plugin private data
        self._optional_data = self._check_opt_data(_optional_data)


@default_property('id', doc="Unique identifier")
@default_property('name', doc="File system name")
@default_property('total_space', doc="Total space in bytes")
@default_property('free_space', doc="Free space available")
@default_property('pool_id', doc="What pool the file system resides on")
@default_property('system_id', doc="System ID")
@default_property("optional_data", "Optional data")
@default_property("plugin_data", "Private plugin data")
class FileSystem(IData):
    FLAG_RETRIEVE_FULL_INFO = 1 << 0
    SUPPORTED_SEARCH_KEYS = ['id', 'system_id', 'pool_id']

    def __init__(self, _id, _name, _total_space, _free_space, _pool_id,
                 _system_id, _optional_data=None, _plugin_data=None):
        self._id = _id
        self._name = _name
        self._total_space = _total_space
        self._free_space = _free_space
        self._pool_id = _pool_id
        self._system_id = _system_id
        self._optional_data = self._check_opt_data(_optional_data)
        self._plugin_data = _plugin_data


@default_property('id', doc="Unique identifier")
@default_property('name', doc="Snapshot name")
@default_property('ts', doc="Time stamp the snapshot was created")
@default_property("optional_data", "Optional data")
@default_property("plugin_data", "Private plugin data")
class FsSnapshot(IData):
    FLAG_RETRIEVE_FULL_INFO = 1 << 0

    def __init__(self, _id, _name, _ts, _optional_data=None,
                 _plugin_data=None):
        self._id = _id
        self._name = _name
        self._ts = int(_ts)
        self._optional_data = self._check_opt_data(_optional_data)
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
@default_property('optional_data', doc="Optional data")
@default_property('plugin_data', doc="Plugin private data")
class NfsExport(IData):
    FLAG_RETRIEVE_FULL_INFO = 1 << 0
    SUPPORTED_SEARCH_KEYS = ['id', 'fs_id']
    ANON_UID_GID_NA = -1
    ANON_UID_GID_ERROR = (ANON_UID_GID_NA - 1)

    def __init__(self, _id, _fs_id, _export_path, _auth, _root, _rw, _ro,
                 _anonuid, _anongid, _options, _optional_data=None,
                 _plugin_data=None):
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
        self._optional_data = self._check_opt_data(_optional_data)
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
@default_property('optional_data', doc="Optional data")
@default_property('plugin_data', doc="Plugin private data")
class AccessGroup(IData):
    FLAG_RETRIEVE_FULL_INFO = 1 << 0
    SUPPORTED_SEARCH_KEYS = ['id', 'system_id']

    INIT_TYPE_UNKNOWN = 0
    INIT_TYPE_OTHER = 1
    INIT_TYPE_WWPN = 2
    INIT_TYPE_WWNN = 3
    INIT_TYPE_HOSTNAME = 4
    INIT_TYPE_ISCSI_IQN = 5
    INIT_TYPE_SAS = 6
    INIT_TYPE_ISCSI_WWPN_MIXED = 7

    def __init__(self, _id, _name, _init_ids, _init_type,
                 _system_id, _optional_data=None, _plugin_data=None):
        self._id = _id
        self._name = _name                # AccessGroup name
        self._init_ids = _init_ids        # List of initiator IDs
        self._init_type = _init_type
        self._system_id = _system_id      # System id this group belongs
        self._plugin_data = _plugin_data
        self._optional_data = self._check_opt_data(_optional_data)


class OptionalData(IData):
    def _column_data(self, human=False, enum_as_number=False):
        return [sorted(self._values.iterkeys(),
                       key=lambda k: self._values[k][1])]

    def __init__(self, _values=None):
        if _values is not None:
            self._values = _values
        else:
            self._values = {}

    def keys(self):
        rc = self._values.keys()
        return rc

    def get(self, key):
        return self._values[key]

    def set(self, key, value):
        self._values[str(key)] = value


class Capabilities(IData):
    (
        UNSUPPORTED,        # Not supported
        SUPPORTED           # Supported
    ) = (0, 1)

    _NUM = 512

    #Array wide
    BLOCK_SUPPORT = 0       # Array handles block operations
    FS_SUPPORT = 1          # Array handles file system

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

    VOLUME_ONLINE = 34
    VOLUME_OFFLINE = 35

    VOLUME_MASK = 36
    VOLUME_UNMASK = 37
    ACCESS_GROUPS = 38
    ACCESS_GROUP_CREATE = 39
    ACCESS_GROUP_DELETE = 40
    ACCESS_GROUP_ADD_INITIATOR = 41
    ACCESS_GROUP_DEL_INITIATOR = 42

    VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP = 43
    ACCESS_GROUPS_GRANTED_TO_VOLUME = 44

    VOLUME_CHILD_DEPENDENCY = 45
    VOLUME_CHILD_DEPENDENCY_RM = 46

    VOLUME_ISCSI_CHAP_AUTHENTICATION = 53

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
    FS_SNAPSHOT_CREATE_SPECIFIC_FILES = 108
    FS_SNAPSHOT_DELETE = 109
    FS_SNAPSHOT_REVERT = 110
    FS_SNAPSHOT_REVERT_SPECIFIC_FILES = 111
    FS_CHILD_DEPENDENCY = 112
    FS_CHILD_DEPENDENCY_RM = 113
    FS_CHILD_DEPENDENCY_RM_SPECIFIC_FILES = 114

    #NFS
    EXPORT_AUTH = 120
    EXPORTS = 121
    EXPORT_FS = 122
    EXPORT_REMOVE = 123
    EXPORT_CUSTOM_PATH = 124

    #Pool
    POOL_CREATE = 130
    POOL_CREATE_FROM_DISKS = 131
    POOL_CREATE_FROM_VOLUMES = 132
    POOL_CREATE_FROM_POOL = 133

    POOL_CREATE_DISK_RAID_0 = 140
    POOL_CREATE_DISK_RAID_1 = 141
    POOL_CREATE_DISK_RAID_JBOD = 142
    POOL_CREATE_DISK_RAID_3 = 143
    POOL_CREATE_DISK_RAID_4 = 144
    POOL_CREATE_DISK_RAID_5 = 145
    POOL_CREATE_DISK_RAID_6 = 146
    POOL_CREATE_DISK_RAID_10 = 147
    POOL_CREATE_DISK_RAID_50 = 148
    POOL_CREATE_DISK_RAID_51 = 149
    POOL_CREATE_DISK_RAID_60 = 150
    POOL_CREATE_DISK_RAID_61 = 151
    POOL_CREATE_DISK_RAID_15 = 152
    POOL_CREATE_DISK_RAID_16 = 153
    POOL_CREATE_DISK_RAID_NOT_APPLICABLE = 154

    POOL_CREATE_VOLUME_RAID_0 = 160
    POOL_CREATE_VOLUME_RAID_1 = 161
    POOL_CREATE_VOLUME_RAID_JBOD = 162
    POOL_CREATE_VOLUME_RAID_3 = 163
    POOL_CREATE_VOLUME_RAID_4 = 164
    POOL_CREATE_VOLUME_RAID_5 = 165
    POOL_CREATE_VOLUME_RAID_6 = 166
    POOL_CREATE_VOLUME_RAID_10 = 167
    POOL_CREATE_VOLUME_RAID_50 = 168
    POOL_CREATE_VOLUME_RAID_51 = 169
    POOL_CREATE_VOLUME_RAID_60 = 170
    POOL_CREATE_VOLUME_RAID_61 = 171
    POOL_CREATE_VOLUME_RAID_15 = 172
    POOL_CREATE_VOLUME_RAID_16 = 173
    POOL_CREATE_VOLUME_RAID_NOT_APPLICABLE = 174

    POOL_DELETE = 200

    POOLS_QUICK_SEARCH = 210
    VOLUMES_QUICK_SEARCH = 211
    DISKS_QUICK_SEARCH = 212
    ACCESS_GROUPS_QUICK_SEARCH = 213
    FS_QUICK_SEARCH = 214
    NFS_EXPORTS_QUICK_SEARCH = 215

    def _to_dict(self):
        rc = {'class': self.__class__.__name__,
              'cap': ''.join(['%02x' % b for b in self._cap])}
        return rc

    def __init__(self, _cap=None):
        if _cap is not None:
            self._cap = bytearray(_cap.decode('hex'))
        else:
            self._cap = bytearray(Capabilities._NUM)

    def supported(self, capability):
        if self.get(capability) == Capabilities.SUPPORTED:
            return True
        return False

    def get(self, capability):
        if capability > len(self._cap):
            return Capabilities.UNSUPPORTED
        return self._cap[capability]

    def set(self, capability, value=SUPPORTED):
        self._cap[capability] = value
        return None

    def enable_all(self):
        for i in range(len(self._cap)):
            self._cap[i] = Capabilities.SUPPORTED


if __name__ == '__main__':
    #TODO Need some unit tests that encode/decode all the types with nested
    pass
