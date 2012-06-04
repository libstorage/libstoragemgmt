# Copyright (C) 2011-2012 Red Hat, Inc.
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

from abc import ABCMeta
import json
from json.decoder import WHITESPACE
from common import get_class

class DataEncoder(json.JSONEncoder):
    """
    Custom json encoder for objects derived form ILsmData
    """

    def default(self, my_class):
        if not isinstance(my_class, IData):
            raise ValueError('incorrect class type:' + str(type(my_class)))
        else:
            return my_class.toDict()

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
            rc = IData.factory(d)
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
    __metaclass__ = ABCMeta

    def toDict(self):
        """
        Represent the class as a dictionary
        """
        rc = {'class': self.__class__.__name__}

        #If one of the attributes is another IData we will
        #process that too, is there a better way to handle this?
        for (k,v) in self.__dict__.items():
            if isinstance(v, IData):
                rc[k] = v.toDict()
            else:
                rc[k] = v

        return rc

    @staticmethod
    def factory(d):
        """
        Factory for creating the appropriate class given a dictionary.
        This only works for objects that inherit from IData
        """
        if 'class' in d:
            class_name = d['class']
            del d['class']
            c = get_class(__name__ + '.' + class_name)
            i = c(**d)
            return i

    def __str__(self):
        """
        Used for human string representation.
        """
        return str(self.toDict())


class Initiator(IData):
    """
    Represents an initiator.
    """
    (TYPE_OTHER, TYPE_PORT_WWN, TYPE_NODE_WWN, TYPE_HOSTNAME, TYPE_ISCSI) = \
    (1, 2, 3, 4, 5)

    def __init__(self, id, type, name):

        if not name or not len(name):
            name = "Unsupported"

        self.id = id
        self.type = type
        self.name = name


class Volume(IData):
    """
    Represents a volume.
    """

    #Volume status Note: Volumes can have multiple status bits set at same time.
    (STATUS_UNKNOWN, STATUS_OK, STATUS_DEGRADED, STATUS_ERR, STATUS_STARTING,
     STATUS_DORMANT) = (0x0, 0x1, 0x2, 0x4, 0x8, 0x10)

    #Replication types
    (REPLICATE_UNKNOWN, REPLICATE_SNAPSHOT, REPLICATE_CLONE, REPLICATE_COPY, REPLICATE_MIRROR) = \
    (-1, 1, 2, 3, 4)

    #Provisioning types
    (PROVISION_UNKNOWN, PROVISION_THIN, PROVISION_FULL, PROVISION_DEFAULT) = \
    ( -1, 1, 2, 3)

    @staticmethod
    def prov_string_to_type(type):
        if type == 'DEFAULT':
            return Volume.PROVISION_DEFAULT
        elif type == "FULL":
            return Volume.PROVISION_FULL
        elif type == "THIN":
            return Volume.PROVISION_THIN
        else:
            return Volume.PROVISION_UNKNOWN

    @staticmethod
    def rep_String_to_type(rt):
        if rt == "SNAPSHOT":
            return Volume.REPLICATE_SNAPSHOT
        elif rt == "CLONE":
            return Volume.REPLICATE_CLONE
        elif rt == "COPY":
            return Volume.REPLICATE_COPY
        elif rt == "MIRROR":
            return Volume.REPLICATE_MIRROR
        else:
            return Volume.REPLICATE_UNKNOWN

    #Initiator access
    (ACCESS_READ_ONLY, ACCESS_READ_WRITE, ACCESS_NONE) = (1,2,3)

    @staticmethod
    def access_string_to_type(access):
        if access == "RW":
            return Volume.ACCESS_READ_WRITE
        else:
            return Volume.ACCESS_READ_ONLY

    def __init__(self, id, name, vpd83, block_size, num_of_blocks, status,
                 system_id):
        self.id = id
        self.name = name
        self.vpd83 = vpd83
        self.block_size = block_size
        self.num_of_blocks = num_of_blocks
        self.status = status
        self.system_id = system_id

    @property
    def size_bytes(self):
        """
        Volume size in bytes.
        """
        return self.block_size * self.num_of_blocks

    def __str__(self):
        return self.name

class System(IData):
    def __init__(self, id, name):
        self.id = id                # For SMI-S this is the CIM_ComputerSystem->Name
        self.name = name            # For SMI-S this is the CIM_ComputerSystem->ElementName

class Pool(IData):
    """
    Pool specific information
    """
    def __init__(self, id, name, total_space, free_space, system_id):
        self.id = id
        self.name = name
        self.total_space = total_space
        self.free_space = free_space
        self.system_id = system_id

class FileSystem(IData):
    def __init__(self, id, name, total_space, free_space, pool_id, system_id):
        self.id = id
        self.name = name
        self.total_space = total_space
        self.free_space = free_space
        self.pool_id = pool_id
        self.system_id = system_id

class Snapshot(IData):
    def __init__(self, id, name, ts):
        self.id = id
        self.name = name
        self.ts = int(ts)

class NfsExport(IData):
    def __init__(self, id, fs_id, export_path, auth, root, rw, ro, anonuid,
                 anongid, options):
        assert(fs_id is not None)
        assert(export_path is not None)

        self.id = id
        self.fs_id = fs_id          #File system exported
        self.export_path = export_path     #Export path
        self.auth = auth            #Authentication type
        self.root = root            #List of hosts with no_root_squash
        self.rw = rw                #List of hosts with read/write
        self.ro = ro                #List of hosts with read/only
        self.anonuid = anonuid      #uid for anonymous user id
        self.anongid = anongid      #gid for anonymous group id
        self.options = options      #NFS options


class BlockRange(IData):
    def __init__(self, source_start, dest_start, block_count):
        self.src_block = source_start
        self.dest_block = dest_start
        self.block_count = block_count

class AccessGroup(IData):
    def __init__(self, id, name, initiators, system_id = 'NA'):
        self.id = id
        self.name = name
        self.initiators = initiators
        self.system_id = system_id

if __name__ == '__main__':
    #TODO Need some unit tests that encode/decode all the types with nested
    pass