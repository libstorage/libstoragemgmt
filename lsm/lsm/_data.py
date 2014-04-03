# Copyright (C) 2011-2013 Red Hat, Inc.
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

from abc import ABCMeta as _ABCMeta

try:
    import simplejson as json
except ImportError:
    import json

from datetime import datetime
from json.decoder import WHITESPACE
from lsm import LsmError, ErrorNumber
from _common import get_class, sh, default_property


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

    _MAN_PROPERTIES_2_HEADER = dict()
    _OPT_PROPERTIES_2_HEADER = dict()
    _MAN_PROPERTIES_SEQUENCE = []
    _OPT_PROPERTIES_SEQUENCE = []

    def _value_convert(self, key_name, value, human, enum_as_number,
                      list_convert):
        return value

    def _str_of_key(self, key_name=None):
        """
        If key_name == None or not provided:
            Return a dictionary providing the mandatory properties key name to
            human friendly string mapping:
                {
                    'id': 'ID',
                    'member_type': 'Member Type',
                    .
                    .
                    .
                }
        else provide the human friendly string of certain key.
        """
        if key_name is None:
            return dict(list(self._MAN_PROPERTIES_2_HEADER.items()) +
                        list(self._OPT_PROPERTIES_2_HEADER.items()))

        man_pros_header = self._MAN_PROPERTIES_2_HEADER
        opt_pros_header = self._OPT_PROPERTIES_2_HEADER
        if key_name in man_pros_header.keys():
            return man_pros_header[key_name]
        elif key_name in opt_pros_header.keys():
            return opt_pros_header[key_name]
        else:
            raise LsmError(ErrorNumber.INVALID_VALUE,
                           "%s class does not provide %s property" %
                           (self.__name__, key_name))

    def _value_of_key(self, key_name=None, human=False, enum_as_number=False,
                     list_convert=False):
        """
        Return the value of certain key, allowing do humanize converting,
        list converting, or enumerate as number.
        For optional properties, if requesting key is not valid for current
        instance(but is valid for class definition), return None
        If key_name == None, we return a dictionary like this:
            {
                # key_name: converted_value
                id: 1232424abcef,
                raid_type: 'RAID6',
                    .
                    .
                    .
            }
        """
        man_pros_header = self._MAN_PROPERTIES_2_HEADER
        opt_pros_header = self._OPT_PROPERTIES_2_HEADER
        if key_name is None:
            all_value = {}
            for cur_key_name in man_pros_header.keys():
                all_value[cur_key_name] = self._value_of_key(
                    key_name=cur_key_name,
                    human=human,
                    enum_as_number=enum_as_number,
                    list_convert=list_convert)
            for cur_key_name in opt_pros_header.keys():
                cur_value = self._value_of_key(
                    key_name=cur_key_name,
                    human=human,
                    enum_as_number=enum_as_number,
                    list_convert=list_convert)
                if cur_value is None:
                    continue
                else:
                    all_value[cur_key_name] = cur_value
            return all_value

        if key_name in man_pros_header.keys():
            value = getattr(self, key_name)

            return self._value_convert(key_name, value, human, enum_as_number,
                                      list_convert)

        elif (hasattr(self, '_optional_data') and
              key_name in opt_pros_header.keys()):
            if key_name not in self._optional_data.list():
                return None

            value = self._optional_data.get(key_name)
            return self._value_convert(key_name, value, human, enum_as_number,
                                      list_convert)
        else:
            raise LsmError(ErrorNumber.INVALID_VALUE,
                           "%s class does not provide %s property" %
                           (self.__name__, key_name))

    def _key_display_sequence(self):
        """
        Return a List with suggested data displaying order of properties.
        """
        key = self._MAN_PROPERTIES_SEQUENCE
        key.extend(self._OPT_PROPERTIES_SEQUENCE)
        return key


@default_property('id', doc="Unique identifier")
@default_property('type', doc="Enumerated initiator type")
@default_property('name', doc="User supplied name")
class Initiator(IData):
    """
    Represents an initiator.
    """
    (TYPE_OTHER, TYPE_PORT_WWN, TYPE_NODE_WWN, TYPE_HOSTNAME, TYPE_ISCSI,
     TYPE_SAS) = (1, 2, 3, 4, 5, 7)

    _type_map = {1: 'Other', 2: 'Port WWN', 3: 'Node WWN', 4: 'Hostname',
                5: 'iSCSI', 7: "SAS"}

    _MAN_PROPERTIES_2_HEADER = {
        'id': 'ID',
        'name': 'Name',
        'type': 'Type',
    }

    _MAN_PROPERTIES_SEQUENCE = ['id', 'name', 'type']

    def _value_convert(self, key_name, value, human, enum_as_number,
                      list_convert):
        if not enum_as_number:
            if key_name == 'type':
                    value = Initiator._type_to_str(value)
        return value

    @staticmethod
    def _type_to_str(init_type):
        return Initiator._type_map[init_type]

    def __init__(self, _id, _type, _name):

        if not _name or not len(_name):
            name = "Unsupported"

        self._id = _id            # Identifier
        self._type = _type        # Initiator type id
        self._name = _name        # Initiator name


@default_property('id', doc="Unique identifier")
@default_property('name', doc="Disk name (aka. vendor)")
@default_property('disk_type', doc="Enumerated type of disk")
@default_property('block_size', doc="Size of each block")
@default_property('num_of_blocks', doc="Total number of blocks")
@default_property('status', doc="Enumerated status")
@default_property('system_id', doc="System identifier")
@default_property("optional_data", doc="Optional data")
class Disk(IData):
    """
    Represents a disk.
    """
    RETRIEVE_FULL_INFO = 2  # Used by _client.py for disks() call.

    # We use '-1' to indicate we failed to get the requested number.
    # For example, when block found is undetectable, we use '-1' instead of
    # confusing 0.
    BLOCK_COUNT_NOT_FOUND = -1
    BLOCK_SIZE_NOT_FOUND = -1

    # Disk Type, using DMTF 2.31.0+ CIM_DiskDrive['InterconnectType']
    DISK_TYPE_UNKNOWN = 0
    DISK_TYPE_OTHER = 1
    DISK_TYPE_NOT_APPLICABLE = 2
    DISK_TYPE_ATA = 3     # IDE disk is seldomly used.
    DISK_TYPE_SATA = 4
    DISK_TYPE_SAS = 5
    DISK_TYPE_FC = 6
    DISK_TYPE_SOP = 7     # SCSI over PCIe, often holding SSD
    DISK_TYPE_SCSI = 8

    # Due to complesity of disk types, we are defining these beside DMTF
    # standards:
    DISK_TYPE_NL_SAS = 51    # Near-Line SAS==SATA disk + SAS port.

    # in DMTF CIM 2.34.0+ CIM_DiskDrive['DiskType'], they also defined
    # SSD and HYBRID disk type. We use it as faillback.
    DISK_TYPE_HDD = 52    # Normal HDD
    DISK_TYPE_SSD = 53    # Solid State Drive
    DISK_TYPE_HYBRID = 54    # uses a combination of HDD and SSD

    _DISK_TYPE = {
        DISK_TYPE_UNKNOWN: 'UNKNOWN',
        DISK_TYPE_OTHER: 'OTHER',
        DISK_TYPE_NOT_APPLICABLE: 'NOT_APPLICABLE',
        DISK_TYPE_ATA: 'ATA',
        DISK_TYPE_SATA: 'SATA',
        DISK_TYPE_SAS: 'SAS',
        DISK_TYPE_FC: 'FC',
        DISK_TYPE_SOP: 'SOP',
        DISK_TYPE_NL_SAS: 'NL_SAS',
        DISK_TYPE_HDD: 'HDD',
        DISK_TYPE_SSD: 'SSD',
        DISK_TYPE_HYBRID: 'HYBRID',
    }

    MAX_DISK_STATUS_BITS = 64
    # Disk status could be any combination of these status.
    STATUS_UNKNOWN = 1 << 0
    # UNKNOWN:
    #   Failed to query out the status of Disk.
    STATUS_OK = 1 << 1
    # OK:
    #   Disk is accessible with no issue.
    STATUS_OTHER = 1 << 2
    # OTHER:
    #   Should explain in Disk.status_info for detail.
    STATUS_PREDICTIVE_FAILURE = 1 << 3
    # PREDICTIVE_FAILURE:
    #   Disk is in unstable state and will predictive fail.
    STATUS_ERROR = 1 << 4
    # ERROR:
    #   Disk data is not accessible due to hardware issue or connection error.
    STATUS_OFFLINE = 1 << 5
    # OFFLINE:
    #   Disk is connected but disabled by array for internal issue
    #   Should explain in Disk.status_info for reason.
    STATUS_STARTING = 1 << 6
    # STARTING:
    #   Disk is reviving from STOPPED status. Disk is not accessible.
    STATUS_STOPPING = 1 << 7
    # STOPPING:
    #   Disk is stopping by administrator. Disk is not accessible.
    STATUS_STOPPED = 1 << 8
    # STOPPING:
    #   Disk is stopped by administrator. Disk is not accessible.
    STATUS_INITIALIZING = 1 << 9
    # INITIALIZING:
    #   Disk is in initialing state.
    #   Mostly shown when new disk inserted or creating spare disk.
    STATUS_RECONSTRUCTING = 1 << 10
    # RECONSTRUCTING:
    #   Disk is in reconstructing date from other RAID member.
    #   Should explain progress in Disk.status_info

    _STATUS = {
        STATUS_UNKNOWN: 'UNKNOWN',
        STATUS_OK: 'OK',
        STATUS_OTHER: 'OTHER',
        STATUS_PREDICTIVE_FAILURE: 'PREDICTIVE_FAILURE',
        STATUS_ERROR: 'ERROR',
        STATUS_OFFLINE: 'OFFLINE',
        STATUS_STARTING: 'STARTING',
        STATUS_STOPPING: 'STOPPING',
        STATUS_STOPPED: 'STOPPED',
        STATUS_INITIALIZING: 'INITIALIZING',
        STATUS_RECONSTRUCTING: 'RECONSTRUCTING',
    }

    @staticmethod
    def status_to_str(status):
        """
        Convert status to a string
        When having multiple status, will use a comma between them
        """
        status_str = ''
        for x in Disk._STATUS.keys():
            if x & status:
                status_str = txt_a(status_str, Disk._STATUS[x])
        if status_str:
            return status_str
        raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                       "Invalid Disk.status: %d" % status)

    _MAN_PROPERTIES_2_HEADER = {
        'id': 'ID',
        'name': 'Name',
        'disk_type': 'Disk Type',
        'block_size': 'Block Size',
        'num_of_blocks': '#blocks',
        'size_bytes': 'Size',
        'status': 'Status',
        'system_id': 'System ID',
    }

    _MAN_PROPERTIES_SEQUENCE = ['id', 'name', 'disk_type', 'block_size',
                                'num_of_blocks', 'size_bytes', 'status',
                                'system_id']

    _OPT_PROPERTIES_2_HEADER = {
        'sn': 'SN',
        'part_num': 'Part Number',
        'vendor': 'Vendor',
        'model': 'Model',
        'status_info': 'Status Info',
        'owner_ctrler_id': 'Controller Owner',
    }

    _OPT_PROPERTIES_SEQUENCE = ['sn', 'part_num', 'vendor', 'model',
                                'status_info', 'owner_ctrler_id']

    def _value_convert(self, key_name, value, human, enum_as_number,
                      list_convert):
        if enum_as_number is False:
            if key_name == 'status':
                value = self.status_to_str(value)
            elif key_name == 'disk_type':
                value = self.disk_type_to_str(value)
        if human:
            if key_name == 'size_bytes':
                value = sh(value, human)
            elif key_name == 'block_size':
                value = sh(value, human)
        return value

    def __init__(self, _id, _name, _disk_type, _block_size, _num_of_blocks,
                 _status, _system_id, _optional_data=None):
        self._id = _id
        self._name = _name
        self._disk_type = _disk_type
        self._block_size = _block_size
        self._num_of_blocks = _num_of_blocks
        self._status = _status
        self._system_id = _system_id

        if _optional_data is None:
            self._optional_data = OptionalData()
        else:
            #Make sure the properties only contain ones we permit
            allowed = set(Disk._OPT_PROPERTIES_2_HEADER.keys())
            actual = set(_optional_data.list())

            if actual <= allowed:
                self._optional_data = _optional_data
            else:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               "Property keys are invalid: %s" %
                               "".join(actual - allowed))

    @property
    def size_bytes(self):
        """
        Disk size in bytes.
        """
        return self.block_size * self.num_of_blocks

    @staticmethod
    def disk_type_to_str(disk_type):
        if disk_type in Disk._DISK_TYPE.keys():
            return Disk._DISK_TYPE[disk_type]
        return Disk._DISK_TYPE[Disk.DISK_TYPE_UNKNOWN]

    @staticmethod
    def disk_type_str_to_type(disk_type_str):
        key = get_key(Disk._DISK_TYPE, disk_type_str)
        if key or key == 0:
            return key
        return Disk.DISK_TYPE_UNKNOWN

    def __str__(self):
        return self.name

    def _opt_column_headers(self):
        opt_headers = []
        opt_pros = self._optional_data.list()
        for opt_pro in opt_pros:
            opt_headers.extend([Disk._OPT_PROPERTIES_2_HEADER[opt_pro]])
        return opt_headers

    def _opt_column_data(self, human=False, enum_as_number=False):
        opt_data_values = []
        opt_pros = self._optional_data.list()
        for opt_pro in opt_pros:
            opt_pro_value = self._optional_data.get(opt_pro)
            if enum_as_number is False:
                pass

            opt_data_values.extend([opt_pro_value])
        return opt_data_values


@default_property('id', doc="Unique identifier")
@default_property('name', doc="User given name")
@default_property('vpd83', doc="Vital product page 0x83 identifier")
@default_property('block_size', doc="Volume block size")
@default_property('num_of_blocks', doc="Number of blocks")
@default_property('status', doc="Enumerated volume status")
@default_property('system_id', "System identifier")
@default_property('pool_id', "Pool identifier")
class Volume(IData):
    """
    Represents a volume.
    """

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

    @staticmethod
    def _prov_string_to_type(prov_type):
        if prov_type == 'DEFAULT':
            return Volume.PROVISION_DEFAULT
        elif prov_type == "FULL":
            return Volume.PROVISION_FULL
        elif prov_type == "THIN":
            return Volume.PROVISION_THIN
        else:
            return Volume.PROVISION_UNKNOWN

    @staticmethod
    def _rep_string_to_type(rt):
        if rt == "SNAPSHOT":
            return Volume.REPLICATE_SNAPSHOT
        elif rt == "CLONE":
            return Volume.REPLICATE_CLONE
        elif rt == "COPY":
            return Volume.REPLICATE_COPY
        elif rt == "MIRROR_SYNC":
            return Volume.REPLICATE_MIRROR_SYNC
        elif rt == "MIRROR_ASYNC":
            return Volume.REPLICATE_MIRROR_ASYNC
        else:
            return Volume.REPLICATE_UNKNOWN

    #Initiator access
    (ACCESS_READ_ONLY, ACCESS_READ_WRITE, ACCESS_NONE) = (1, 2, 3)

    @staticmethod
    def _status_to_str(status):
        if status == 1:
            return "OK"
        elif status == 0:
            return "Unknown"
        else:
            rc = ""
            if status & Volume.STATUS_OK:
                rc = txt_a(rc, "OK")
            if status & Volume.STATUS_DEGRADED:
                rc = txt_a(rc, "Degraded")
            if status & Volume.STATUS_DORMANT:
                rc = txt_a(rc, "Dormant")
            if status & Volume.STATUS_ERR:
                rc = txt_a(rc, "Error")
            if status & Volume.STATUS_STARTING:
                rc = txt_a(rc, "Starting")
            return rc

    @staticmethod
    def _access_string_to_type(access):
        if access == "RW":
            return Volume.ACCESS_READ_WRITE
        else:
            return Volume.ACCESS_READ_ONLY

    def __init__(self, _id, _name, _vpd83, _block_size, _num_of_blocks,
                 _status, _system_id, _pool_id):
        self._id = _id                        # Identifier
        self._name = _name                    # Human recognisable name
        self._vpd83 = _vpd83                  # SCSI page 83 unique ID
        self._block_size = _block_size        # Block size
        self._num_of_blocks = _num_of_blocks  # Number of blocks
        self._status = _status                # Status
        self._system_id = _system_id          # System id this volume belongs
        self._pool_id = _pool_id              # Pool id this volume belongs

    @property
    def size_bytes(self):
        """
        Volume size in bytes.
        """
        return self.block_size * self.num_of_blocks

    def __str__(self):
        return self.name

    _MAN_PROPERTIES_2_HEADER = {
        'id': 'ID',
        'name': 'Name',
        'vpd83': 'VPD83',
        'block_size': 'Block Size',
        'num_of_blocks': '#blocks',
        'size_bytes': 'Size',
        'status': 'Status',
        'system_id': 'System ID',
        'pool_id': 'Pool ID',
    }

    _MAN_PROPERTIES_SEQUENCE = ['id', 'name', 'vpd83', 'block_size',
                                'num_of_blocks', 'size_bytes', 'status',
                                'system_id', 'pool_id']

    def _value_convert(self, key_name, value, human, enum_as_number,
                      list_convert):

        if enum_as_number is False:
                if key_name == 'status':
                    value = self._status_to_str(value)
        if human:
            if key_name == 'size_bytes':
                value = sh(value, human)
            elif key_name == 'block_size':
                value = sh(value, human)
        return value


@default_property('id', doc="Unique identifier")
@default_property('name', doc="User defined system name")
@default_property('status', doc="Enumerated status of system")
class System(IData):
    (STATUS_UNKNOWN, STATUS_OK, STATUS_DEGRADED, STATUS_ERROR,
     STATUS_PREDICTIVE_FAILURE, STATUS_VENDOR_SPECIFIC) = \
        (0x0, 0x1, 0x2, 0x4, 0x8, 0x10)

    @staticmethod
    def _status_to_str(status):
        if status == 0:
            return "Unknown"
        elif status == 1:
            return "OK"
        else:
            rc = ""
            if status & System.STATUS_OK:
                rc = txt_a(rc, "OK")
            if status & System.STATUS_DEGRADED:
                rc = txt_a(rc, "Degraded")
            if status & System.STATUS_ERROR:
                rc = txt_a(rc, "Error")
            if status & System.STATUS_PREDICTIVE_FAILURE:
                rc = txt_a(rc, "Predictive failure")
            if status & System.STATUS_VENDOR_SPECIFIC:
                rc = txt_a(rc, "Vendor specific status")

            return rc

    def __init__(self, _id, _name, _status):
        self._id = _id        # For SMI-S this is the CIM_ComputerSystem->Name
        self._name = _name        # For SMI-S , CIM_ComputerSystem->ElementName
        self._status = _status    # OperationalStatus

    _MAN_PROPERTIES_2_HEADER = {
        'id': 'ID',
        'name': 'Name',
        'status': 'Status',
    }

    _MAN_PROPERTIES_SEQUENCE = ['id', 'name', 'status']

    def _value_convert(self, key_name, value, human, enum_as_number,
                      list_convert):

        if enum_as_number is False:
            if key_name == 'status':
                value = System._status_to_str(value)
        return value


@default_property('id', doc="Unique identifier")
@default_property('name', doc="User supplied name")
@default_property('total_space', doc="Total space in bytes")
@default_property('free_space', doc="Free space in bytes")
@default_property('status', doc="Enumerated status")
@default_property('system_id', doc="System identifier")
@default_property("optional_data", doc="Optional data")
class Pool(IData):
    """
    Pool specific information
    """
    RETRIEVE_FULL_INFO = 1  # Used by _client.py for pools() call.
                            # This might not be a good place, please
                            # suggest a better one.

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

    # The string of each RAID_TYPE is for CIM_StorageSetting['ElementName']
    _STD_RAID_TYPE = {
        RAID_TYPE_RAID0: 'RAID0',  # stripe
        RAID_TYPE_RAID1: 'RAID1',  # mirror
        RAID_TYPE_RAID3: 'RAID3',  # byte-level striping with dedicated
                                   # parity
        RAID_TYPE_RAID4: 'RAID4',  # block-level striping with dedicated
                                   # parity
        RAID_TYPE_RAID5: 'RAID5',  # block-level striping with distributed
                                   # parity
        RAID_TYPE_RAID6: 'RAID6',  # AKA, RAID-DP.
    }

    _NESTED_RAID_TYPE = {
        RAID_TYPE_RAID10: 'RAID10',  # stripe of mirrors
        RAID_TYPE_RAID15: 'RAID15',  # parity of mirrors
        RAID_TYPE_RAID16: 'RAID16',  # dual parity of mirrors
        RAID_TYPE_RAID50: 'RAID50',  # stripe of parities
        RAID_TYPE_RAID60: 'RAID60',  # stripe of dual parities
        RAID_TYPE_RAID51: 'RAID51',  # mirror of parities
        RAID_TYPE_RAID61: 'RAID61',  # mirror of dual parities
    }

    _MISC_RAID_TYPE = {
        RAID_TYPE_JBOD: 'JBOD',         # Just Bunch of Disks
        RAID_TYPE_UNKNOWN: 'UNKNOWN',
        RAID_TYPE_NOT_APPLICABLE: 'NOT_APPLICABLE',
        RAID_TYPE_MIXED: 'MIXED',  # a Pool are having 2+ RAID groups with
                                   # different RAID type
    }

    # Using 'dict(list(x.items()) + list(y.items()))' for python 3 prepare
    _RAID_TYPE = dict(list(_STD_RAID_TYPE.items()) +
                      list(_NESTED_RAID_TYPE.items()) +
                      list(_MISC_RAID_TYPE.items()))

    @staticmethod
    def _raid_type_to_num(raid_type):
        """
        Convert Pool.RAID_TYPE_RAID10 into int(10)
        Only check standard RAID and nested RAID, not including JBOD.
        The raid_type itself is a int number.
        If not a valid, we return None
        """
        if (raid_type in Pool._STD_RAID_TYPE.keys() or
           raid_type in Pool._NESTED_RAID_TYPE.keys()):
            return raid_type
        return None

    @staticmethod
    def raid_type_to_str(raid_type):
        if raid_type in Pool._RAID_TYPE.keys():
            return Pool._RAID_TYPE[raid_type]
        return Pool._RAID_TYPE[Pool.RAID_TYPE_UNKNOWN]

    @staticmethod
    def _raid_type_str_to_type(raid_type_str):
        key = get_key(Pool._RAID_TYPE, raid_type_str)
        if key or key == 0:
            return key
        return Pool.RAID_TYPE_UNKNOWN

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

    _MEMBER_TYPE = {
        MEMBER_TYPE_UNKNOWN: 'UNKNOWN',
        MEMBER_TYPE_DISK: 'DISK',       # Pool was created from Disk(s).
        MEMBER_TYPE_DISK_MIX: 'DISK_MIX',   # Has two or more types of disks.
        MEMBER_TYPE_DISK_ATA: 'DISK_ATA',
        MEMBER_TYPE_DISK_SATA: 'DISK_SATA',
        MEMBER_TYPE_DISK_SAS: 'DISK_SAS',
        MEMBER_TYPE_DISK_FC: 'DISK_FC',
        MEMBER_TYPE_DISK_SOP: 'DISK_SOP',
        MEMBER_TYPE_DISK_SCSI: 'DISK_SCSI',
        MEMBER_TYPE_DISK_NL_SAS: 'DISK_NL_SAS',
        MEMBER_TYPE_DISK_HDD: 'DISK_HDD',
        MEMBER_TYPE_DISK_SSD: 'DISK_SSD',
        MEMBER_TYPE_DISK_HYBRID: 'DISK_HYBRID',
        MEMBER_TYPE_POOL: 'POOL',       # Pool was created from other Pool(s).
        MEMBER_TYPE_VOLUME: 'VOLUME',   # Pool was created from Volume(s).
    }

    @staticmethod
    def _member_type_to_str(member_type):
        if member_type in Pool._MEMBER_TYPE.keys():
            return Pool._MEMBER_TYPE[member_type]
        return Pool._MEMBER_TYPE[Pool.MEMBER_TYPE_UNKNOWN]

    @staticmethod
    def _member_type_str_to_type(member_type_str):
        key = get_key(Pool._MEMBER_TYPE, member_type_str)
        if key or key == 0:
            return key
        return Pool.MEMBER_TYPE_UNKNOWN

    @staticmethod
    def _member_ids_to_str(member_ids):
        member_string = ''
        if isinstance(member_ids, list):
            for member_id in member_ids:
                member_string = txt_a(member_string, str(member_id))
        return member_string

    THINP_TYPE_UNKNOWN = 0
    THINP_TYPE_THIN = 1
    THINP_TYPE_THICK = 5
    THINP_TYPE_NOT_APPLICABLE = 6
    # NOT_APPLICABLE means current pool is not implementing Thin Provisioning,
    # but can create thin or thick pool from it.

    _THINP_TYPE = {
        THINP_TYPE_UNKNOWN: 'UNKNOWN',
        THINP_TYPE_THIN: 'THIN',
        THINP_TYPE_THICK: 'THICK',
        THINP_TYPE_NOT_APPLICABLE: 'NOT_APPLICABLE',
    }

    @staticmethod
    def thinp_type_to_str(thinp_type):
        if thinp_type in Pool._THINP_TYPE.keys():
            return Pool._THINP_TYPE[thinp_type]
        return Pool._THINP_TYPE[Pool.THINP_TYPE_UNKNOWN]

    @staticmethod
    def thinp_type_str_to_type(thinp_type_str):
        key = get_key(Pool._THINP_TYPE, thinp_type_str)
        if key or key == 0:
            return key
        return Pool.THINP_TYPE_UNKNOWN

    # Element Type indicate what kind of element could this pool create:
    #   * Another Pool
    #   * Volume (aka, LUN)
    #   * System Reserved Pool.
    ELEMENT_TYPE_UNKNOWN = 1 << 0
    ELEMENT_TYPE_POOL = 1 << 1
    ELEMENT_TYPE_VOLUME = 1 << 2
    ELEMENT_TYPE_FS = 1 << 3
    ELEMENT_TYPE_SYS_RESERVED = 1 << 10     # Reserved for system use

    _ELEMENT_TYPE = {
        ELEMENT_TYPE_UNKNOWN: 'UNKNOWN',
        ELEMENT_TYPE_POOL: 'POOL',
        ELEMENT_TYPE_VOLUME: 'VOLUME',
        ELEMENT_TYPE_FS: 'FILE_SYSTEM',
        ELEMENT_TYPE_SYS_RESERVED: 'SYSTEM_RESERVED',
    }

    @staticmethod
    def _element_type_to_str(element_type):
        element_str = ''
        for x in Pool._ELEMENT_TYPE.keys():
            if x & element_type:
                element_str = txt_a(element_str, Pool._ELEMENT_TYPE[x])
        if element_str:
            return element_str
        return Pool._ELEMENT_TYPE[Pool.ELEMENT_TYPE_UNKNOWN]

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
    # ERROR:
    #   Pool data is not accessible due to RAID members offline.
    #   Example:
    #    * RAID 5 pool lost access to 2 disks.
    #    * RAID 0 pool lost access to 1 disks.
    STATUS_OFFLINE = 1 << 6
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

    _STATUS = {
        STATUS_UNKNOWN: 'UNKNOWN',
        STATUS_OK: 'OK',
        STATUS_OTHER: 'OTHER',
        STATUS_STRESSED: 'STRESSED',
        STATUS_DEGRADED: 'DEGRADED',
        STATUS_ERROR: 'ERROR',
        STATUS_OFFLINE: 'OFFLINE',
        STATUS_STARTING: 'STARTING',
        STATUS_STOPPING: 'STOPPING',
        STATUS_STOPPED: 'STOPPED',
        STATUS_READ_ONLY: 'READ_ONLY',
        STATUS_DORMANT: 'DORMANT',
        STATUS_RECONSTRUCTING: 'RECONSTRUCTING',
        STATUS_VERIFYING: 'VERIFYING',
        STATUS_INITIALIZING: 'INITIALIZING',
        STATUS_GROWING: 'GROWING',
        STATUS_SHRINKING: 'SHRINKING',
        STATUS_DESTROYING: 'DESTROYING',
    }

    @staticmethod
    def _status_to_str(status):
        """
        Convert status to a string
        When having multiple status, will use a comma between them
        """
        status_str = ''
        for x in Pool._STATUS.keys():
            if x & status:
                status_str = txt_a(status_str, Pool._STATUS[x])
        if status_str:
            return status_str
        raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                       "Invalid Pool.status: %d" % status)

    _MAN_PROPERTIES_2_HEADER = {
        'id': 'ID',
        # id: Identifier of Pool.
        'name': 'Name',
        # name: Human readable name of Pool.
        'total_space': 'Total Space',
        # total_space: All spaces in bytes could be allocated to user.
        'free_space': 'Free Space',
        # free_space: Free spaces in bytes could be allocated to user.
        'status':   'Status',
        # status: Indicate the status of Pool.
        'system_id': 'System ID',
        # system_id: Identifier of belonging system.
    }
    _MAN_PROPERTIES_SEQUENCE = ['id', 'name', 'total_space', 'free_space',
                                'status', 'system_id']

    _OPT_PROPERTIES_2_HEADER = {
        'raid_type': 'RAID Type',
        # raid_type: RAID Type of this pool's RAID Group(s):
        #            RAID_TYPE_XXXX, check constants above.
        'member_type': 'Member Type',
        # member_type: What kind of items assembled this pool:
        #              MEMBER_TYPE_DISK/MEMBER_TYPE_POOL/MEMBER_TYPE_VOLUME
        'member_ids': 'Member IDs',
        # member_ids: The list of items' ID assembled this pool:
        #               [Pool.id, ] or [Disk.id, ] or [Volume.id, ]
        'thinp_type': 'Thin Provision Type',
        # thinp_type: Can this pool support Thin Provisioning or not:
        #             THINP_TYPE_THIN vs THINP_TYPE_THICK
        #             THINP_TYPE_NOT_APPLICABLE for those pool can create
        #             THICK sub_pool or THIN sub_pool. That means, ThinP is
        #             not implemented at current pool level.
        #             If we really need to identify the under algorithm some
        #             day, we will expand to THINP_TYPE_THIN_ALLOCATED and etc
        'status': 'Status',
        # status: The status of this pool, OK, Data Lose, or etc.
        'status_info': 'Status Info',
        # status_info: A string explaining the detail of current status.
        #              Check comments above about Pool.STATUS_XXX for
        #              what info you should save in it.
        'element_type': 'Element Type',
        # element_type: That kind of items can this pool create:
        #               ELEMENT_TYPE_VOLUME
        #               ELEMENT_TYPE_POOL
        #               ELEMENT_TYPE_FS
        #               For those system reserved pool, use
        #               ELEMENT_TYPE_SYS_RESERVED
        #               For example, pools for replication or spare.
        #               We will split them out once support spare and
        #               replication. Those system pool should be neither
        #               filtered or mark as ELEMENT_TYPE_SYS_RESERVED.
    }

    _OPT_PROPERTIES_SEQUENCE = ['raid_type', 'member_type', 'member_ids',
                                'element_type', 'thinp_type', 'status_info']

    def _value_convert(self, key_name, value, human, enum_as_number,
                      list_convert):

        if human:
            if key_name == 'total_space' or key_name == 'free_space':
                value = sh(value, human)
        if list_convert:
            if key_name == 'member_ids':
                value = self._member_ids_to_str(value)
        if enum_as_number is False:
            if key_name == 'raid_type':
                value = self.raid_type_to_str(value)
            elif key_name == 'member_type':
                value = self._member_type_to_str(value)
            elif key_name == 'thinp_type':
                value = self.thinp_type_to_str(value)
            elif key_name == 'status':
                value = self._status_to_str(value)
            elif key_name == 'element_type':
                value = self._element_type_to_str(value)
        return value

    def __init__(self, _id, _name, _total_space, _free_space, _status,
                 _system_id, _optional_data=None):
        self._id = _id                    # Identifier
        self._name = _name                # Human recognisable name
        self._total_space = _total_space  # Total size
        self._free_space = _free_space    # Free space available
        self._status = _status            # Status of pool.
        self._system_id = _system_id      # System id this pool belongs

        if _optional_data is None:
            self._optional_data = OptionalData()
        else:
            #Make sure the properties only contain ones we permit
            allowed = set(Pool._OPT_PROPERTIES_2_HEADER.keys())
            actual = set(_optional_data.list())

            if actual <= allowed:
                self._optional_data = _optional_data
            else:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               "Property keys are invalid: %s" %
                               "".join(actual - allowed))

    def _opt_column_headers(self):
        opt_headers = []
        opt_pros = self._optional_data.list()
        for opt_pro in opt_pros:
            opt_headers.extend([Pool._OPT_PROPERTIES_2_HEADER[opt_pro]])
        return opt_headers

    def _opt_column_data(self, human=False, enum_as_number=False):
        opt_data_values = []
        opt_pros = self._optional_data.list()
        for opt_pro in opt_pros:
            opt_pro_value = self._optional_data.get(opt_pro)
            if enum_as_number:
                pass    # no byte size needed to humanize
            else:
                if opt_pro == 'member_ids':
                    opt_pro_value = Pool._member_ids_to_str(opt_pro_value)
                elif opt_pro == 'raid_type':
                    opt_pro_value = Pool.raid_type_to_str(opt_pro_value)
                elif opt_pro == 'member_type':
                    opt_pro_value = Pool._member_type_to_str(opt_pro_value)
                elif opt_pro == 'thinp_type':
                    opt_pro_value = Pool.thinp_type_to_str(opt_pro_value)
                elif opt_pro == 'element_type':
                    opt_pro_value = Pool._element_type_to_str(opt_pro_value)

            opt_data_values.extend([opt_pro_value])
        return opt_data_values


@default_property('id', doc="Unique identifier")
@default_property('name', doc="File system name")
@default_property('total_space', doc="Total space in bytes")
@default_property('free_space', doc="Free space available")
@default_property('pool_id', doc="What pool the file system resides on")
@default_property('system_id', doc="System ID")
class FileSystem(IData):
    def __init__(self, _id, _name, _total_space, _free_space, _pool_id,
                 _system_id):
        self._id = _id
        self._name = _name
        self._total_space = _total_space
        self._free_space = _free_space
        self._pool_id = _pool_id
        self._system_id = _system_id

    _MAN_PROPERTIES_2_HEADER = {
        'id': 'ID',
        'name': 'Name',
        'total_space': 'Total Space',
        'free_space': 'Free Space',
        'pool_id': 'Pool ID',
    }

    _MAN_PROPERTIES_SEQUENCE = ['id', 'name', 'total_space', 'free_space',
                                'pool_id']

    def _value_convert(self, key_name, value, human, enum_as_number,
                      list_convert):
        if human:
            if key_name == 'total_space':
                value = sh(value, human)
            elif key_name == 'free_space':
                value = sh(value, human)
        return value


@default_property('id', doc="Unique identifier")
@default_property('name', doc="Snapshot name")
@default_property('ts', doc="Time stamp the snapshot was created")
class Snapshot(IData):
    def __init__(self, _id, _name, _ts):
        self._id = _id
        self._name = _name
        self._ts = int(_ts)

    def _value_convert(self, key_name, value, human, enum_as_number,
                      list_convert):
        if key_name == 'ts':
            value = datetime.fromtimestamp(value)
        return value

    _MAN_PROPERTIES_2_HEADER = {
        'id': 'ID',
        'name': 'Name',
        'ts': 'Created',
    }

    _MAN_PROPERTIES_SEQUENCE = ['id', 'name', 'ts']


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
class NfsExport(IData):
    ANON_UID_GID_NA = -1
    ANON_UID_GID_ERROR = (ANON_UID_GID_NA - 1)

    def __init__(self, _id, _fs_id, _export_path, _auth, _root, _rw, _ro,
                 _anonuid, _anongid, _options):
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

    _MAN_PROPERTIES_2_HEADER = {
        'id': 'ID',
        'fs_id': 'File system ID',
        'export_path': 'Export Path',
        'auth': 'Authentication',
        'root': 'Root',
        'rw': 'Read/Write',
        'ro': 'Read Only',
        'anonuid': 'Anon UID',
        'anongid': 'Anon GID',
        'options': 'Options'
    }

    _MAN_PROPERTIES_SEQUENCE = ['id', 'fs_id', 'export_path', 'auth', 'root',
                                'rw', 'ro', 'anonuid', 'anongid', 'options']


@default_property('src_block', doc="Source logical block address")
@default_property('dest_block', doc="Destination logical block address")
@default_property('block_count', doc="Number of blocks")
class BlockRange(IData):
    def __init__(self, _src_block, _dest_block, _block_count):
        self._src_block = _src_block
        self._dest_block = _dest_block
        self._block_count = _block_count

    def _str_of_key(self, key_name=None):
        raise NotImplementedError


@default_property('id', doc="Unique instance identifier")
@default_property('name', doc="Access group name")
@default_property('initiators', doc="List of initiators")
@default_property('system_id', doc="System identifier")
class AccessGroup(IData):
    def __init__(self, _id, _name, _initiators, _system_id='NA'):
        self._id = _id
        self._name = _name                # AccessGroup name
        self._initiators = _initiators    # List of initiators
        self._system_id = _system_id      # System id this group belongs

    _MAN_PROPERTIES_2_HEADER = {
        'id': 'ID',
        'name': 'Name',
        'initiators': 'Initiator IDs',
        'system_id': 'System ID',
    }

    _MAN_PROPERTIES_SEQUENCE = ['id', 'name', 'initiators', 'system_id']
    _OPT_PROPERTIES_SEQUENCE = []

    def _value_convert(self, key_name, value, human, enum_as_number,
                      list_convert):
        if list_convert:
            if key_name == 'initiators':
                value = ','.join(str(x) for x in value)
        return value


class OptionalData(IData):
    def _column_data(self, human=False, enum_as_number=False):
        return [sorted(self._values.iterkeys(),
                       key=lambda k: self._values[k][1])]

    def _str_of_key(self, key_name=None):
        raise NotImplementedError

    def __init__(self, _values=None):
        if _values is not None:
            self._values = _values
        else:
            self._values = {}

    def list(self):
        rc = self._values.keys()
        return rc

    def get(self, key):
        return self._values[key]

    def set(self, key, value):
        self._values[key] = value


class Capabilities(IData):
    (
        UNSUPPORTED,        # Not supported
        SUPPORTED,          # Supported
        SUPPORTED_OFFLINE,  # Supported, but only when item is in offline state
        NOT_IMPLEMENTED,    # Not implemented
        UNKNOWN             # Capability not known
    ) = (0, 1, 2, 3, 4)

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

    ACCESS_GROUP_GRANT = 36
    ACCESS_GROUP_REVOKE = 37
    ACCESS_GROUP_LIST = 38
    ACCESS_GROUP_CREATE = 39
    ACCESS_GROUP_DELETE = 40
    ACCESS_GROUP_ADD_INITIATOR = 41
    ACCESS_GROUP_DEL_INITIATOR = 42

    VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP = 43
    ACCESS_GROUPS_GRANTED_TO_VOLUME = 44

    VOLUME_CHILD_DEPENDENCY = 45
    VOLUME_CHILD_DEPENDENCY_RM = 46

    INITIATORS = 47
    INITIATORS_GRANTED_TO_VOLUME = 48

    VOLUME_INITIATOR_GRANT = 50
    VOLUME_INITIATOR_REVOKE = 51
    VOLUME_ACCESSIBLE_BY_INITIATOR = 52
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

    def _to_dict(self):
        rc = {'class': self.__class__.__name__,
              'cap': ''.join(['%02x' % b for b in self._cap])}
        return rc

    def __init__(self, _cap=None):
        if _cap is not None:
            self._cap = bytearray(_cap.decode('hex'))
        else:
            self._cap = bytearray(Capabilities._NUM)

    def get(self, capability):
        if capability > len(self._cap):
            return Capabilities.UNKNOWN
        return self._cap[capability]

    def set(self, capability, value=SUPPORTED):
        self._cap[capability] = value
        return None

    def enable_all(self):
        for i in range(len(self._cap)):
            self._cap[i] = Capabilities.SUPPORTED

    def _str_of_key(self, key_name=None):
        raise NotImplementedError


# This data is actually never serialized across the RPC, but is used only
# for displaying the data.
class PlugData(IData):
    _MAN_PROPERTIES_2_HEADER = {
        "desc": "Description",
        "version": "Version",
    }

    _MAN_PROPERTIES_SEQUENCE = ['desc', 'version']

    def __init__(self, description, plugin_version):
        self.desc = description
        self.version = plugin_version


if __name__ == '__main__':
    #TODO Need some unit tests that encode/decode all the types with nested
    pass
