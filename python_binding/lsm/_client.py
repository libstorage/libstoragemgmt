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
import os
import sys
from lsm import (Volume, NfsExport, Capabilities, Pool, System, Battery,
                 Disk, AccessGroup, FileSystem, FsSnapshot,
                 uri_parse, LsmError, ErrorNumber,
                 INetworkAttachedStorage, TargetPort)

from lsm._common import return_requires as _return_requires
from lsm._common import UDS_PATH as _UDS_PATH
from lsm._transport import TransPort as _TransPort
from lsm._data import IData as _IData

import six


# Removes self for the hash d
# @param    d   Hash to remove self from
# @returns d with hash removed.
def _del_self(d):
    """
    Used to remove the self key from the dict d.  Self is included when calling
    the function locals() in a class method.
    """
    del d['self']
    return d


def _check_search_key(search_key, supported_keys):
    if search_key and search_key not in supported_keys:
        raise LsmError(ErrorNumber.UNSUPPORTED_SEARCH_KEY,
                       "Unsupported search_key: '%s'" % search_key)
    return


# Descriptive exception about daemon not running.
def _raise_no_daemon():
    raise LsmError(ErrorNumber.DAEMON_NOT_RUNNING,
                   "The libStorageMgmt daemon is not running (process "
                   "name lsmd), try 'service libstoragemgmt start'")


# Main client class for library.
# ** IMPORTANT **
# Theory of operation for methods in this class.
# We are using the name of the method and the name of the parameters and
# using python introspection abilities to translate them to the method and
# parameter names.  Makes the code compact, but you will break things if the
# IPlugin class does not match the method names and parameters here!
class Client(INetworkAttachedStorage):

    #
    # Used for default flag value
    #
    FLAG_RSVD = 0

    FLAG_VOLUME_CREATE_USE_SYSTEM_CACHE = 1 << 0
    FLAG_VOLUME_CREATE_USE_IO_PASSTHROUGH = 1 << 1
    FLAG_VOLUME_CREATE_DISABLE_SYSTEM_CACHE = 1 << 2
    FLAG_VOLUME_CREATE_DISABLE_IO_PASSTHROUGH = 1 << 3

    """
    Client side class used for managing storage that utilises RPC mechanism.
    """
    # Method added so that the interface for the client RPC and the plug-in
    # itself match.
    def plugin_register(self, uri, plain_text_password, timeout_ms, flags=0):
        raise RuntimeError("Do not call directly!")

    # Called when we are ready to initialize the plug-in.
    # @param    self                    The this pointer
    # @param    uri                     The uniform resource identifier
    # @param    plain_text_password     Password as plain text
    # @param    timeout_ms              The timeout in ms
    # @param    flags                   Reserved for future use, must be zero.
    # @returns None
    def __start(self, uri, password, timeout, flags=0):
        """
        Instruct the plug-in to get ready
        """
        self._tp.rpc('plugin_register', _del_self(locals()))

    # Checks to see if any unix domain sockets exist in the base directory
    # and opens a socket to one to see if the server is actually there.
    # @param    self    The this pointer
    # @returns True if daemon appears to be present, else false.
    @staticmethod
    def _check_daemon_exists():
        uds_path = Client._plugin_uds_path()
        if os.path.exists(uds_path):
            for root, sub_folders, files in os.walk(uds_path):
                for filename in files:
                    uds = os.path.join(root, filename)

                    try:
                        # This operation will work if the daemon is available
                        s = _TransPort.get_socket(uds)
                        s.close()
                        return True
                    except LsmError:
                        pass
        else:
            # Base directory is not present?
            pass
        return False

    @staticmethod
    def _plugin_uds_path():
        rc = _UDS_PATH

        if 'LSM_UDS_PATH' in os.environ:
            rc = os.environ['LSM_UDS_PATH']

        return rc

    # Class constructor
    # @param    self                    The this pointer
    # @param    uri                     The uniform resource identifier
    # @param    plain_text_password     Password as plain text (Optional)
    # @param    timeout_ms              The timeout in ms
    # @param    flags                   Reserved for future use, must be zero.
    # @returns None
    def __init__(self, uri, plain_text_password=None, timeout_ms=30000,
                 flags=0):
        self._uri = uri
        self._password = plain_text_password
        self._timeout = timeout_ms
        self._uds_path = Client._plugin_uds_path()

        u = uri_parse(uri, ['scheme'])

        scheme = u['scheme']
        if "+" in scheme:
            (plug, proto) = scheme.split("+")
            scheme = plug

        self.plugin_path = os.path.join(self._uds_path, scheme)

        if os.path.exists(self.plugin_path):
            self._tp = _TransPort(_TransPort.get_socket(self.plugin_path))
        else:
            # At this point we don't know if the user specified an incorrect
            # plug-in in the URI or the daemon isn't started.  We will check
            # the directory for other unix domain sockets.
            if Client._check_daemon_exists():
                raise LsmError(ErrorNumber.PLUGIN_NOT_EXIST,
                               "Plug-in %s not found!" % self.plugin_path)
            else:
                _raise_no_daemon()

        self.__start(uri, plain_text_password, timeout_ms, flags)

    # Synonym for close.
    @_return_requires(None)
    def plugin_unregister(self, flags=FLAG_RSVD):
        """
        Synonym for close.
        """
        self.close(flags)

    # Does an orderly plugin_unregister of the plug-in
    # @param    self    The this pointer
    # @param    flags   Reserved for future use, must be zero.
    @_return_requires(None)
    def close(self, flags=FLAG_RSVD):
        """
        Does an orderly plugin_unregister of the plug-in
        """
        self._tp.rpc('plugin_unregister', _del_self(locals()))
        self._tp.close()
        self._tp = None

    # Retrieves all the available plug-ins
    # @param    field_sep   Field separator
    # @param    flags:      Reserved for future use
    @staticmethod
    @_return_requires([six.string_types[0]])
    def available_plugins(field_sep=':', flags=FLAG_RSVD):
        """
        Retrieves all the available plug-ins

        Return list of strings of available plug-ins with the
        "desc<sep>version"
        """
        rc = []

        if not Client._check_daemon_exists():
            _raise_no_daemon()

        uds_path = Client._plugin_uds_path()

        for root, sub_folders, files in os.walk(uds_path):
            for filename in files:
                uds = os.path.join(root, filename)
                tp = _TransPort(_TransPort.get_socket(uds))
                i, v = tp.rpc('plugin_info', dict(flags=flags))
                rc.append("%s%s%s" % (i, field_sep, v))
                tp.close()

        return rc

    # Sets the timeout for the plug-in
    # @param    self    The this pointer
    # @param    ms      Time-out in ms
    # @param    flags   Reserved for future use, must be zero.
    @_return_requires(None)
    def time_out_set(self, ms, flags=FLAG_RSVD):
        """
        Sets any time-outs for the plug-in (ms)

        Return None on success, else LsmError exception
        """
        return self._tp.rpc('time_out_set', _del_self(locals()))

    # Retrieves the current time-out value.
    # @param    self    The this pointer
    # @param    flags   Reserved for future use, must be zero.
    # @returns  Time-out value
    @_return_requires(int)
    def time_out_get(self, flags=FLAG_RSVD):
        """
        Retrieves the current time-out

        Return time-out in ms, else raise LsmError
        """
        return self._tp.rpc('time_out_get', _del_self(locals()))

    # Retrieves the status of the specified job id.
    # @param    self    The this pointer
    # @param    job_id  The job identifier
    # @param    flags   Reserved for future use, must be zero.
    # @returns A tuple ( status (enumeration), percent_complete,
    # completed item)
    @_return_requires(int, int, _IData)
    def job_status(self, job_id, flags=FLAG_RSVD):
        """
        Returns the stats of the given job.

        Returns a tuple ( status (enumeration), percent_complete,
                            completed item).
        else LsmError exception.
        """
        return self._tp.rpc('job_status', _del_self(locals()))

    # Frees the resources for the specified job id.
    # @param    self    The this pointer
    # @param    job_id  Job id in which to release resource for
    # @param    flags   Reserved for future use, must be zero.
    @_return_requires(None)
    def job_free(self, job_id, flags=FLAG_RSVD):
        """
        Frees resources for a given job number.

        Returns None on success, else raises an LsmError
        """
        return self._tp.rpc('job_free', _del_self(locals()))

    # Gets the capabilities of the array.
    # @param    self    The this pointer
    # @param    system  The system of interest
    # @param    flags   Reserved for future use, must be zero.
    # @returns  Capability object
    @_return_requires(Capabilities)
    def capabilities(self, system, flags=FLAG_RSVD):
        """
        Fetches the capabilities of the array

        Returns a capability object, see data,py for details.
        """
        return self._tp.rpc('capabilities', _del_self(locals()))

    # Gets information about the plug-in
    # @param    self    The this pointer
    # @param    flags   Reserved for future use
    # @returns  Tuple (description, version)
    @_return_requires(six.string_types[0], six.string_types[0])
    def plugin_info(self, flags=FLAG_RSVD):
        """
        Returns a description and version of plug-in
        """
        return self._tp.rpc('plugin_info', _del_self(locals()))

    # Returns an array of pool objects.
    # @param    self            The this pointer
    # @param    search_key      Search key
    # @param    search_value    Search value
    # @param    flags           Reserved for future use, must be zero.
    # @returns An array of pool objects.
    @_return_requires([Pool])
    def pools(self, search_key=None, search_value=None, flags=FLAG_RSVD):
        """
        Returns an array of pool objects.  Pools are used in both block and
        file system interfaces, thus the reason they are in the base class.
        """
        _check_search_key(search_key, Pool.SUPPORTED_SEARCH_KEYS)
        return self._tp.rpc('pools', _del_self(locals()))

    # Returns an array of system objects.
    # @param    self    The this pointer
    # @param    flags   Reserved for future use, must be zero.
    # @returns An array of system objects.
    @_return_requires([System])
    def systems(self, flags=FLAG_RSVD):
        """
        Returns an array of system objects.  System information is used to
        distinguish resources from on storage array to another when the plug=in
        supports the ability to have more than one array managed by it
        """
        return self._tp.rpc('systems', _del_self(locals()))

    # Changes the read cache percentage for a system.
    # @param    self            The this pointer
    # @param    system          System object to target
    # @param    read_pct        Desired read cache percentage
    # @param    flags           Flags
    # @returns None on success, else raises LsmError
    @_return_requires(None)
    def system_read_cache_pct_update(self, system, read_pct, flags=FLAG_RSVD):
        """
        lsm.Client.system_read_cache_pct_update(self, system, read_pct,
                                                flags=lsm.Client.FLAG_RSVD)

        Version:
            1.3
        Usage:
            This method updates the read cache percentage on a system if
            supported
        Parameters:
            system(lsm.System)
                an lsm.System object.
            read_pct(int)
                the desired read cache percentage.
            flags (int)
                Optional. Reserved for future use.
                Should be set as lsm.Client.FLAG_RSVD.
        Returns:
            returns None on success, else raises LsmError on errors.
        SpecialExceptions:
        """
        if sys.version_info[0] == 2:
            if not isinstance(read_pct, (int, long)):
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               "Invalid read_pct, should be an integer")
        else:
            if not isinstance(read_pct, int):
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               "Invalid read_pct, should be an integer")

        if read_pct > 100 or read_pct < 0:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Invalid read_pct, should be in range 0 - 100")
        return self._tp.rpc('system_read_cache_pct_update',
                            _del_self(locals()))

    # Register a user/password for the specified initiator for CHAP
    #  authentication.
    # Note: If you pass an empty user and password the expected behavior is to
    #       remove any authentication for the specified initiator.
    # @param    self            The this pointer
    # @param    init_id         The initiator ID
    # @param    in_user         User for inbound CHAP
    # @param    in_password     Password for inbound CHAP
    # @param    out_user        Outbound username
    # @param    out_password    Outbound password
    # @param    flags   Reserved for future use, must be zero.
    # @returns None on success, throws LsmError on errors.
    @_return_requires(None)
    def iscsi_chap_auth(self, init_id, in_user, in_password,
                        out_user, out_password, flags=FLAG_RSVD):
        """
        Register a user/password for the specified initiator for CHAP
        authentication.
        """
        AccessGroup.initiator_id_verify(init_id,
                                        AccessGroup.INIT_TYPE_ISCSI_IQN,
                                        raise_exception=True)
        return self._tp.rpc('iscsi_chap_auth', _del_self(locals()))

    # Returns an array of volume objects
    # @param    self            The this pointer
    # @param    search_key      Search key to use
    # @param    search_value    Search value
    # @param    flags           Reserved for future use, must be zero.
    # @returns An array of volume objects.
    @_return_requires([Volume])
    def volumes(self, search_key=None, search_value=None, flags=FLAG_RSVD):
        """
        Returns an array of volume objects
        """
        _check_search_key(search_key, Volume.SUPPORTED_SEARCH_KEYS)
        return self._tp.rpc('volumes', _del_self(locals()))

    # Creates a volume
    # @param    self            The this pointer
    # @param    pool            The pool object to allocate storage from
    # @param    volume_name     The human text name for the volume
    # @param    size_bytes      Size of the volume in bytes
    # @param    provisioning    How the volume is to be provisioned
    # @param    flags           Reserved for future use, must be zero.
    # @returns  A tuple (job_id, new volume), when one is None the other is
    #           valid.
    @_return_requires(six.string_types[0], Volume)
    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags=FLAG_RSVD):
        """
        Creates a volume, given a pool, volume name, size and provisioning

        returns a tuple (job_id, new volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self._tp.rpc('volume_create', _del_self(locals()))

    # Re-sizes a volume
    # @param    self    The this pointer
    # @param    volume  The volume object to re-size
    # @param    new_size_bytes  Size of the volume in bytes
    # @param    flags   Reserved for future use, must be zero.
    # @returns  A tuple (job_id, new re-sized volume), when one is
    #           None the other is valid.
    @_return_requires(six.string_types[0], Volume)
    def volume_resize(self, volume, new_size_bytes, flags=FLAG_RSVD):
        """
        Re-sizes a volume.

        Returns a tuple (job_id, re-sized_volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self._tp.rpc('volume_resize', _del_self(locals()))

    # Replicates a volume from the specified pool.
    # @param    self        The this pointer
    # @param    pool        The pool to re-size from
    # @param    rep_type    Replication type
    #                       (enumeration,see common.data.Volume)
    # @param    volume_src  The volume to replicate
    # @param    name        Human readable name of replicated volume
    # @param    flags       Reserved for future use, must be zero.
    # @returns  A tuple (job_id, new replicated volume), when one is
    #           None the other is valid.
    @_return_requires(six.string_types[0], Volume)
    def volume_replicate(self, pool, rep_type, volume_src, name,
                         flags=FLAG_RSVD):
        """
        Replicates a volume from the specified pool.

        Returns a tuple (job_id, replicated volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self._tp.rpc('volume_replicate', _del_self(locals()))

    # Size of a replicated block.
    # @param    self    The this pointer
    # @param    system  The system to request the rep. block range size from
    # @param    flags   Reserved for future use, must be zero
    # @returns  Size of the replicated block in bytes
    @_return_requires(int)
    def volume_replicate_range_block_size(self, system, flags=FLAG_RSVD):
        """
        Returns the size of a replicated block in bytes.
        """
        return self._tp.rpc('volume_replicate_range_block_size',
                            _del_self(locals()))

    # Replicates a portion of a volume to itself or another volume.
    # @param    self    The this pointer
    # @param    rep_type    Replication type
    #                       (enumeration, see common.data.Volume)
    # @param    volume_src  The volume src to replicate from
    # @param    volume_dest The volume dest to replicate to
    # @param    ranges      An array of Block range objects
    #                       @see lsm.common.data.BlockRange
    # @param    flags       Reserved for future use, must be zero.
    # @returns Job id or None when completed, else raises LsmError on errors.
    @_return_requires(six.string_types[0])
    def volume_replicate_range(self, rep_type, volume_src, volume_dest, ranges,
                               flags=FLAG_RSVD):
        """
        Replicates a portion of a volume to itself or another volume.  The src,
        dest and number of blocks values change with vendor, call
        volume_replicate_range_block_size to get block unit size.

        Returns Job id or None when completed, else raises LsmError on errors.
        """
        return self._tp.rpc('volume_replicate_range', _del_self(locals()))

    # Deletes a volume
    # @param    self    The this pointer
    # @param    volume  The volume object which represents the volume to delete
    # @param    flags   Reserved for future use, must be zero.
    # @returns None on success, else job id.  Raises LsmError on errors.
    @_return_requires(six.string_types[0])
    def volume_delete(self, volume, flags=FLAG_RSVD):
        """
        Deletes a volume.

        Returns None on success, else job id
        """
        return self._tp.rpc('volume_delete', _del_self(locals()))

    # Makes a volume online and available to the host.
    # @param    self    The this pointer
    # @param    volume  The volume to place online
    # @param    flags   Reserved for future use, must be zero.
    # @returns None on success, else raises LsmError
    @_return_requires(None)
    def volume_enable(self, volume, flags=FLAG_RSVD):
        """
        Makes a volume available to the host

        returns None on success, else raises LsmError on errors.
        """
        return self._tp.rpc('volume_enable', _del_self(locals()))

    # Takes a volume offline
    # @param    self    The this pointer
    # @param    volume  The volume object
    # @param    flags   Reserved for future use, must be zero.
    # @returns None on success, else raises LsmError on errors.
    @_return_requires(None)
    def volume_disable(self, volume, flags=FLAG_RSVD):
        """
        Makes a volume unavailable to the host

        returns None on success, else raises LsmError on errors.
        """
        return self._tp.rpc('volume_disable', _del_self(locals()))

    # Returns an array of disk objects
    # @param    self    The this pointer
    # @param    search_key      Search Key
    # @param    search_value    Search value
    # @param    flags   When equal to DISK.FLAG_RETRIEVE_FULL_INFO
    #                   returned objects will contain optional data.
    #                   If not defined, only the mandatory properties will
    #                   be returned.
    # @returns An array of disk objects.
    @_return_requires([Disk])
    def disks(self, search_key=None, search_value=None, flags=FLAG_RSVD):
        """
        Returns an array of disk objects
        """
        _check_search_key(search_key, Disk.SUPPORTED_SEARCH_KEYS)
        return self._tp.rpc('disks', _del_self(locals()))

    # Access control for allowing an access group to access a volume
    # @param    self            The this pointer
    # @param    access_group    The access group
    # @param    volume          The volume to grant access to
    # @param    flags           Reserved for future use, must be zero.
    # @returns None on success, throws LsmError on errors.
    @_return_requires(None)
    def volume_mask(self, access_group, volume, flags=FLAG_RSVD):
        """
        Allows an access group to access a volume.
        """
        return self._tp.rpc('volume_mask', _del_self(locals()))

    # Revokes access to a volume to initiators in an access group
    # @param    self            The this pointer
    # @param    access_group    The access group
    # @param    volume          The volume to grant access to
    # @param    flags           Reserved for future use, must be zero.
    # @returns None on success, throws LsmError on errors.
    @_return_requires(None)
    def volume_unmask(self, access_group, volume, flags=FLAG_RSVD):
        """
        Revokes access for an access group for a volume
        """
        return self._tp.rpc('volume_unmask', _del_self(locals()))

    # Returns a list of access group objects
    # @param    self    The this pointer
    # @param    search_key      Search Key
    # @param    search_value    Search value
    # @param    flags   Reserved for future use, must be zero.
    # @returns  List of access groups
    @_return_requires([AccessGroup])
    def access_groups(self, search_key=None, search_value=None,
                      flags=FLAG_RSVD):
        """
        Returns a list of access groups
        """
        _check_search_key(search_key, AccessGroup.SUPPORTED_SEARCH_KEYS)
        return self._tp.rpc('access_groups', _del_self(locals()))

    # Creates an access a group with the specified initiator in it.
    # @param    self                The this pointer
    # @param    name                The initiator group name
    # @param    init_id             Initiator id
    # @param    init_type           Type of initiator (Enumeration)
    # @param    system              Which system to create this group on
    # @param    flags               Reserved for future use, must be zero.
    # @returns AccessGroup on success, else raises LsmError
    @_return_requires(AccessGroup)
    def access_group_create(self, name, init_id, init_type, system,
                            flags=FLAG_RSVD):
        """
        Creates an access group and add the specified initiator id,
        init_type and desired access.
        """
        init_type, init_id = AccessGroup.initiator_id_verify(
            init_id, init_type, raise_exception=True)[1:]
        return self._tp.rpc('access_group_create', _del_self(locals()))

    # Deletes an access group.
    # @param    self            The this pointer
    # @param    access_group    The access group to delete
    # @param    flags           Reserved for future use, must be zero.
    # @returns None on success, throws LsmError on errors.
    @_return_requires(None)
    def access_group_delete(self, access_group, flags=FLAG_RSVD):
        """
        Deletes an access group
        """
        return self._tp.rpc('access_group_delete', _del_self(locals()))

    # Adds an initiator to an access group
    # @param    self            The this pointer
    # @param    access_group    Group to add initiator to
    # @param    init_id         Initiators id
    # @param    init_type       Initiator id type (enumeration)
    # @param    flags           Reserved for future use, must be zero.
    # @returns None on success, throws LsmError on errors.
    @_return_requires(AccessGroup)
    def access_group_initiator_add(self, access_group, init_id, init_type,
                                   flags=FLAG_RSVD):
        """
        Adds an initiator to an access group
        """
        init_type, init_id = AccessGroup.initiator_id_verify(
            init_id, init_type, raise_exception=True)[1:]
        return self._tp.rpc('access_group_initiator_add', _del_self(locals()))

    # Deletes an initiator from an access group
    # @param    self            The this pointer
    # @param    access_group    The access group to remove initiator from
    # @param    init_id         The initiator to remove from the group
    # @param    init_type       Initiator id type (enumeration)
    # @param    flags           Reserved for future use, must be zero.
    # @returns None on success, throws LsmError on errors.
    @_return_requires(AccessGroup)
    def access_group_initiator_delete(self, access_group, init_id, init_type,
                                      flags=FLAG_RSVD):
        """
        Deletes an initiator from an access group
        """
        init_id = AccessGroup.initiator_id_verify(init_id, None,
                                                  raise_exception=True)[2]
        return self._tp.rpc('access_group_initiator_delete',
                            _del_self(locals()))

    # Returns the list of volumes that access group has access to.
    # @param    self            The this pointer
    # @param    access_group    The access group to list volumes for
    # @param    flags           Reserved for future use, must be zero.
    # @returns list of volumes
    @_return_requires([Volume])
    def volumes_accessible_by_access_group(self, access_group,
                                           flags=FLAG_RSVD):
        """
        Returns the list of volumes that access group has access to.
        """
        return self._tp.rpc('volumes_accessible_by_access_group',
                            _del_self(locals()))

    # Returns the list of access groups that have access to the specified
    # volume.
    # @param    self        The this pointer
    # @param    volume      The volume to list access groups for
    # @param    flags       Reserved for future use, must be zero.
    # @returns  list of access groups
    @_return_requires([AccessGroup])
    def access_groups_granted_to_volume(self, volume, flags=FLAG_RSVD):
        """
        Returns the list of access groups that have access to the specified
        volume.
        """
        return self._tp.rpc('access_groups_granted_to_volume',
                            _del_self(locals()))

    # Checks to see if a volume has child dependencies.
    # @param    self    The this pointer
    # @param    volume  The volume to check
    # @param    flags   Reserved for future use, must be zero.
    # @returns True or False
    @_return_requires(bool)
    def volume_child_dependency(self, volume, flags=FLAG_RSVD):
        """
        Returns True if this volume has other volumes which are dependant on
        it. Implies that this volume cannot be deleted or possibly modified
        because it would affect its children.
        """
        return self._tp.rpc('volume_child_dependency', _del_self(locals()))

    # Removes any child dependency.
    # @param    self    The this pointer
    # @param    volume  The volume to remove dependencies for
    # @param    flags   Reserved for future use, must be zero.
    # @returns None if complete, else job id.
    @_return_requires(six.string_types[0])
    def volume_child_dependency_rm(self, volume, flags=FLAG_RSVD):
        """
        If this volume has child dependency, this method call will fully
        replicate the blocks removing the relationship between them.  This
        should return None (success) if volume_child_dependency would return
        False.

        Note:  This operation could take a very long time depending on the size
        of the volume and the number of child dependencies.

        Returns None if complete else job id, raises LsmError on errors.
        """
        return self._tp.rpc('volume_child_dependency_rm', _del_self(locals()))

    # Returns a list of file system objects.
    # @param    self            The this pointer
    # @param    search_key      Search Key
    # @param    search_value    Search value
    # @param    flags           Reserved for future use, must be zero.
    # @returns A list of FS objects.
    @_return_requires([FileSystem])
    def fs(self, search_key=None, search_value=None, flags=FLAG_RSVD):
        """
        Returns a list of file systems on the controller.
        """
        _check_search_key(search_key, FileSystem.SUPPORTED_SEARCH_KEYS)
        return self._tp.rpc('fs', _del_self(locals()))

    # Deletes a file system
    # @param    self    The this pointer
    # @param    fs      The file system to delete
    # @param    flags   Reserved for future use, must be zero.
    # @returns  None on success, else job id
    @_return_requires(six.string_types[0])
    def fs_delete(self, fs, flags=FLAG_RSVD):
        """
        WARNING: Destructive

        Deletes a file system and everything it contains
        Returns None on success, else job id
        """
        return self._tp.rpc('fs_delete', _del_self(locals()))

    # Re-sizes a file system
    # @param    self            The this pointer
    # @param    fs              The file system to re-size
    # @param    new_size_bytes  The new size of the file system in bytes
    # @param    flags           Reserved for future use, must be zero.
    # @returns tuple (job_id, re-sized file system),
    # When one is None the other is valid
    @_return_requires(six.string_types[0], FileSystem)
    def fs_resize(self, fs, new_size_bytes, flags=FLAG_RSVD):
        """
        Re-size a file system

        Returns a tuple (job_id, re-sized file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self._tp.rpc('fs_resize', _del_self(locals()))

    # Creates a file system.
    # @param    self        The this pointer
    # @param    pool        The pool object to allocate space from
    # @param    name        The human text name for the file system
    # @param    size_bytes  The size of the file system in bytes
    # @param    flags       Reserved for future use, must be zero.
    # @returns  tuple (job_id, file system),
    # When one is None the other is valid
    @_return_requires(six.string_types[0], FileSystem)
    def fs_create(self, pool, name, size_bytes, flags=FLAG_RSVD):
        """
        Creates a file system given a pool, name and size.
        Note: size is limited to 2**64 bytes

        Returns a tuple (job_id, file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self._tp.rpc('fs_create', _del_self(locals()))

    # Clones a file system
    # @param    self            The this pointer
    # @param    src_fs          The source file system to clone
    # @param    dest_fs_name    The destination file system clone name
    # @param    snapshot        Optional, create clone from previous snapshot
    # @param    flags           Reserved for future use, must be zero.
    # @returns tuple (job_id, file system)
    @_return_requires(six.string_types[0], FileSystem)
    def fs_clone(self, src_fs, dest_fs_name, snapshot=None, flags=FLAG_RSVD):
        """
        Creates a thin, point in time read/writable copy of src to dest.
        Optionally uses snapshot as backing of src_fs

        Returns a tuple (job_id, file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self._tp.rpc('fs_clone', _del_self(locals()))

    # Clones an individual file or files on the specified file system
    # @param    self            The this pointer
    # @param    fs              The file system the files are on
    # @param    src_file_name   The source file name
    # @param    dest_file_name  The dest. file name
    # @param    snapshot        Optional, the snapshot to base clone source
    #                                       file from
    # @param    flags           Reserved for future use, must be zero.
    # @returns  None on success, else job id
    @_return_requires(six.string_types[0])
    def fs_file_clone(self, fs, src_file_name, dest_file_name, snapshot=None,
                      flags=FLAG_RSVD):
        """
        Creates a thinly provisioned clone of src to dest.
        Note: Source and Destination are required to be on same filesystem and
        all directories in destination path need to exist.

        Returns None on success, else job id
        """
        return self._tp.rpc('fs_file_clone', _del_self(locals()))

    # Returns a list of snapshots
    # @param    self    The this pointer
    # @param    fs      The file system
    # @param    flags   Reserved for future use, must be zero.
    # @returns  a list of snapshot objects.
    @_return_requires([FsSnapshot])
    def fs_snapshots(self, fs, flags=FLAG_RSVD):
        """
        Returns a list of snapshot names for the supplied file system
        """
        return self._tp.rpc('fs_snapshots', _del_self(locals()))

    # Creates a snapshot (Point in time read only copy)
    # @param    self            The this pointer
    # @param    fs              The file system to snapshot
    # @param    snapshot_name   The human readable snapshot name
    # @param    flags           Reserved for future use, must be zero.
    # @returns tuple (job_id, snapshot)
    @_return_requires(six.string_types[0], FsSnapshot)
    def fs_snapshot_create(self, fs, snapshot_name, flags=FLAG_RSVD):
        """
        Snapshot is a point in time read-only copy

        Create a snapshot on the chosen file system.

        Returns a tuple (job_id, snapshot)
        Notes:
        - Snapshot name may not match what was passed in
          (depends on array implementation)
        - Tuple return values are mutually exclusive, when one
          is None the other must be valid.
        """
        return self._tp.rpc('fs_snapshot_create', _del_self(locals()))

    # Deletes a snapshot
    # @param    self        The this pointer
    # @param    fs          The filesystem the snapshot it for
    # @param    snapshot    The specific snap shot to delete
    # @param    flags       Reserved for future use, must be zero.
    # @returns  None on success, else job id
    @_return_requires(six.string_types[0])
    def fs_snapshot_delete(self, fs, snapshot, flags=FLAG_RSVD):
        """
        Frees the re-sources for the given snapshot on the supplied filesystem.

        Returns None on success else job id, LsmError exception on error
        """
        return self._tp.rpc('fs_snapshot_delete', _del_self(locals()))

    # Reverts a snapshot
    # @param    self            The this pointer
    # @param    fs              The file system object to restore snapshot for
    # @param    snapshot        The snapshot file to restore back too
    # @param    files           The specific files to restore
    # @param    restore_files   Individual files to restore
    # @param    all_files       Set to True if all files should be restored
    #                           back
    # @param    flags           Reserved for future use, must be zero.
    # @return None on success, else job id
    @_return_requires(six.string_types[0])
    def fs_snapshot_restore(self, fs, snapshot, files, restore_files,
                            all_files=False, flags=FLAG_RSVD):
        """
        WARNING: Destructive!

        Reverts a file-system or just the specified files from the snapshot.
        If a list of files is supplied but the array cannot restore just them
        then the operation will fail with an LsmError raised.  If files == None
        and all_files = True then all files on the file-system are restored.

        Restore_file if None none must be the same length as files with each
        index in each list referring to the associated file.

        Returns None on success, else job id, LsmError exception on error
        """
        return self._tp.rpc('fs_snapshot_restore', _del_self(locals()))

    # Checks to see if a file system has child dependencies.
    # @param    fs      The file system to check
    # @param    files   The files to check (optional)
    # @param    flags   Reserved for future use, must be zero.
    # @returns True or False
    @_return_requires(bool)
    def fs_child_dependency(self, fs, files, flags=FLAG_RSVD):
        """
        Returns True if the specified filesystem or specified file on this
        file system has child dependencies.  This implies that this filesystem
        or specified file on this file system cannot be deleted or possibly
        modified because it would affect its children.
        """
        return self._tp.rpc('fs_child_dependency', _del_self(locals()))

    # Removes child dependencies from a FS or specific file.
    # @param    self    The this pointer
    # @param    fs      The file system to remove child dependencies for
    # @param    files   The list of files to remove child dependencies (opt.)
    # @param    flags   Reserved for future use, must be zero.
    # @returns None if complete, else job id.
    @_return_requires(six.string_types[0])
    def fs_child_dependency_rm(self, fs, files, flags=FLAG_RSVD):
        """
        If this filesystem or specified file on this filesystem has child
        dependency this method will fully replicate the blocks removing the
        relationship between them.  This should return None(success) if
        fs_child_dependency would return False.

        Note:  This operation could take a very long time depending on the size
        of the filesystem and the number of child dependencies.

        Returns None if completed, else job id.  Raises LsmError on errors.
        """
        return self._tp.rpc('fs_child_dependency_rm', _del_self(locals()))

    # Returns a list of all the NFS client authentication types.
    # @param    self    The this pointer
    # @param    flags   Reserved for future use, must be zero.
    # @returns  An array of client authentication types.
    @_return_requires([six.string_types[0]])
    def export_auth(self, flags=FLAG_RSVD):
        """
        What types of NFS client authentication are supported.
        """
        return self._tp.rpc('export_auth', _del_self(locals()))

    # Returns a list of all the exported file systems
    # @param    self    The this pointer
    # @param    search_key      Search Key
    # @param    search_value    Search value
    # @param    flags   Reserved for future use, must be zero.
    # @returns An array of export objects
    @_return_requires([NfsExport])
    def exports(self, search_key=None, search_value=None, flags=FLAG_RSVD):
        """
        Get a list of all exported file systems on the controller.
        """
        _check_search_key(search_key, NfsExport.SUPPORTED_SEARCH_KEYS)
        return self._tp.rpc('exports', _del_self(locals()))

    # Exports a FS as specified in the export.
    # @param    self            The this pointer
    # @param    fs_id           The FS ID to export
    # @param    export_path     The export path (Set to None for array to pick)
    # @param    root_list       List of hosts with root access
    # @param    rw_list         List of hosts with read/write access
    # @param    ro_list         List of hosts with read only access
    # @param    anon_uid        UID to map to anonymous
    # @param    anon_gid        GID to map to anonymous
    # @param    auth_type       NFS client authentication type
    # @param    options         Options to pass to plug-in
    # @param    flags           Reserved for future use, must be zero.
    # @returns NfsExport on success, else raises LsmError
    @_return_requires(NfsExport)
    def export_fs(self, fs_id, export_path, root_list, rw_list, ro_list,
                  anon_uid=NfsExport.ANON_UID_GID_NA,
                  anon_gid=NfsExport.ANON_UID_GID_NA,
                  auth_type=None, options=None, flags=FLAG_RSVD):
        """
        Exports a filesystem as specified in the arguments
        """
        if set(rw_list) - (set(rw_list) - set(ro_list)):
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Host cannot both in rw_list and ro_list.")
        return self._tp.rpc('export_fs', _del_self(locals()))

    # Removes the specified export
    # @param    self    The this pointer
    # @param    export  The export to remove
    # @param    flags   Reserved for future use, must be zero.
    # @returns None on success, else raises LsmError
    @_return_requires(None)
    def export_remove(self, export, flags=FLAG_RSVD):
        """
        Removes the specified export
        """
        return self._tp.rpc('export_remove', _del_self(locals()))

    # Returns a list of target ports
    # @param    self    The this pointer
    # @param    search_key      The key to search against
    # @param    search_value    The value to search for
    # @param    flags           Reserved for future use, must be zero
    # @returns List of target ports, else raises LsmError
    @_return_requires([TargetPort])
    def target_ports(self, search_key=None, search_value=None,
                     flags=FLAG_RSVD):
        """
        Returns a list of target ports
        """
        _check_search_key(search_key, TargetPort.SUPPORTED_SEARCH_KEYS)
        return self._tp.rpc('target_ports', _del_self(locals()))

    # Returns the RAID information of certain volume
    # @param    self    The this pointer
    # @param    volume  The volume to retrieve RAID information for
    # @param    flags   Reserved for future use, must be zero
    # @returns [raid_type, strip_size, disk_count, min_io_size, opt_io_size],
    #           else raises LsmError
    @_return_requires([int, int, int, int, int])
    def volume_raid_info(self, volume, flags=FLAG_RSVD):
        """Query the RAID information of certain volume.

        New in version 1.2.

        Query the RAID type, strip size, extents count, minimum I/O size,
        optimal I/O size of given volume.

        This method requires this capability:
            lsm.Capabilities.VOLUME_RAID_INFO

        Args:
            volume (Volume object): Volume to query
            flags (int): Reserved for future use. Should be set as
                lsm.Client.FLAG_RSVD
        Returns:
            [raid_type, strip_size, disk_count, min_io_size, opt_io_size]

            raid_type (int): RAID Type of requested volume.
                Could be one of these values:
                    Volume.RAID_TYPE_RAID0
                        Stripe
                    Volume.RAID_TYPE_RAID1
                        Two disks Mirror
                    Volume.RAID_TYPE_RAID3
                        Byte-level striping with dedicated parity
                    Volume.RAID_TYPE_RAID4
                        Block-level striping with dedicated parity
                    Volume.RAID_TYPE_RAID5
                        Block-level striping with distributed parity
                    Volume.RAID_TYPE_RAID6
                        Block-level striping with two distributed parities,
                        aka, RAID-DP
                    Volume.RAID_TYPE_RAID10
                        Stripe of mirrors
                    Volume.RAID_TYPE_RAID15
                        Parity of mirrors
                    Volume.RAID_TYPE_RAID16
                        Dual parity of mirrors
                    Volume.RAID_TYPE_RAID50
                        Stripe of parities
                    Volume.RAID_TYPE_RAID60
                        Stripe of dual parities
                    Volume.RAID_TYPE_RAID51
                        Mirror of parities
                    Volume.RAID_TYPE_RAID61
                        Mirror of dual parities
                    Volume.RAID_TYPE_JBOD
                        Just bunch of disks, no parity, no striping.
                    Volume.RAID_TYPE_UNKNOWN
                        The plugin failed to detect the volume's RAID type.
                    Volume.RAID_TYPE_MIXED
                        This volume contains multiple RAID settings.
                    Volume.RAID_TYPE_OTHER
                        Vendor specific RAID type
            strip_size(int): The size of strip on each disk or other storage
                extent.
                For RAID1/JBOD, it should be set as sector size.
                If plugin failed to detect strip size, it should be set
                as Volume.STRIP_SIZE_UNKNOWN(0).
            disk_count(int): The count of disks used for assembling the RAID
                group(s) where this volume allocated from.
                For any RAID system using the slice of disk, this value
                indicate how many disk slices are used for the RAID.
                For exmaple, on LVM RAID, the 'disk_count' here indicate the
                count of PVs used for certain volume.
                Another example, on EMC VMAX, the 'disk_count' here indicate
                how many hyper volumes are used for this volume.
                For any RAID system using remote LUN for data storing, each
                remote LUN should be count as a disk.
                If the plugin failed to detect disk_count, it should be set
                as Volume.DISK_COUNT_UNKNOWN(0).
            min_io_size(int): The minimum I/O size, device preferred I/O
                size for random I/O. Any I/O size not equal to a multiple
                of this value may get significant speed penalty.
                Normally it refers to strip size of each disk(extent).
                If plugin failed to detect min_io_size, it should try these
                values in the sequence of:
                logical sector size -> physical sector size ->
                Volume.MIN_IO_SIZE_UNKNOWN(0).
            opt_io_size(int): The optimal I/O size, device preferred I/O
                size for sequential I/O. Normally it refers to RAID group
                stripe size.
                If plugin failed to detect opt_io_size, it should be set
                to Volume.OPT_IO_SIZE_UNKNOWN(0).
        Raises:
            LsmError:
                ErrorNumber.NO_SUPPORT
                    No support.
        """
        return self._tp.rpc('volume_raid_info', _del_self(locals()))

    # Query the membership information of specified pool
    # @param    self    The this pointer
    # @param    pool    The pool to query
    # @param    flags   Reserved for future use, must be zero
    # @returns  [raid_type, member_type, [member_ids]], lsmError on error
    @_return_requires([int, int, [six.string_types[0]]])
    def pool_member_info(self, pool, flags=FLAG_RSVD):
        """
        lsm.Client.pool_member_info(self, pool, flags=lsm.Client.FLAG_RSVD)

        Version:
            1.2
        Usage:
            Query the membership information of certain pool:
                RAID type, member type and member ids.
            Currently, LibStorageMgmt supports two types of pool:
                * Sub-pool -- Pool.MEMBER_TYPE_POOL
                    Pool space is allocated from parent pool.
                    Example:
                        * NetApp ONTAP volume

                * Disk RAID pool -- Pool.MEMBER_TYPE_DISK
                    Pool is a RAID group assembled by disks.
                    Example:
                        * LSI MegaRAID disk group
                        * EMC VNX pool
                        * NetApp ONTAP aggregate
        Parameters:
            pool (lsm.Pool object)
                Pool to query
            flags (int)
                Optional. Reserved for future use.
                Should be set as lsm.Client.FLAG_RSVD.
        Returns:
            [raid_type, member_type, member_ids]

            raid_type (int)
                RAID Type of requested pool.
                    Could be one of these values:
                        Volume.RAID_TYPE_RAID0
                            Stripe
                        Volume.RAID_TYPE_RAID1
                            Two disks Mirror
                        Volume.RAID_TYPE_RAID3
                            Byte-level striping with dedicated parity
                        Volume.RAID_TYPE_RAID4
                            Block-level striping with dedicated parity
                        Volume.RAID_TYPE_RAID5
                            Block-level striping with distributed parity
                        Volume.RAID_TYPE_RAID6
                            Block-level striping with two distributed parities,
                            aka, RAID-DP
                        Volume.RAID_TYPE_RAID10
                            Stripe of mirrors
                        Volume.RAID_TYPE_RAID15
                            Parity of mirrors
                        Volume.RAID_TYPE_RAID16
                            Dual parity of mirrors
                        Volume.RAID_TYPE_RAID50
                            Stripe of parities Volume.RAID_TYPE_RAID60
                            Stripe of dual parities
                        Volume.RAID_TYPE_RAID51
                            Mirror of parities
                        Volume.RAID_TYPE_RAID61
                            Mirror of dual parities
                        Volume.RAID_TYPE_JBOD
                            Just bunch of disks, no parity, no striping.
                        Volume.RAID_TYPE_UNKNOWN
                            The plugin failed to detect the volume's RAID type.
                        Volume.RAID_TYPE_MIXED
                            This pool contains multiple RAID settings.
                        Volume.RAID_TYPE_OTHER
                            Vendor specific RAID type
            member_type (int)
                Could be one of these values:
                    Pool.MEMBER_TYPE_POOL
                        Current pool(also known as sub-pool) is allocated from
                        other pool(parent pool). The 'raid_type' will set to
                        RAID_TYPE_OTHER unless certain RAID system support RAID
                        using space of parent pools.
                    Pool.MEMBER_TYPE_DISK
                        Pool is created from RAID group using whole disks.
                    Pool.MEMBER_TYPE_OTHER
                        Vendor specific RAID member type.
                    Pool.MEMBER_TYPE_UNKNOWN
                        Plugin failed to detect the RAID member type.
            member_ids (list of strings)
                When 'member_type' is Pool.MEMBER_TYPE_POOL,
                the 'member_ids' will contain a list of parent Pool IDs.
                When 'member_type' is Pool.MEMBER_TYPE_DISK,
                the 'member_ids' will contain a list of disk IDs.
                When 'member_type' is Pool.MEMBER_TYPE_OTHER or
                Pool.MEMBER_TYPE_UNKNOWN, the member_ids should be an empty
                list.
        SpecialExceptions:
            LsmError
                ErrorNumber.NO_SUPPORT
                ErrorNumber.NOT_FOUND_POOL
        Capability:
            lsm.Capabilities.POOL_MEMBER_INFO
        """
        return self._tp.rpc('pool_member_info', _del_self(locals()))

    # Queries all the supported RAID types and stripe sizes which could be
    #  used for input into volume_raid_create
    # @param    self    The this pointer
    # @param    system  System (raid) card to query
    # @param    flags   Reserved for future use, must be zero
    # @returns  [raid_types, strip_sizes], lsmError on error
    @_return_requires([[int], [int]])
    def volume_raid_create_cap_get(self, system, flags=FLAG_RSVD):
        """
        lsm.Client.volume_raid_create_cap_get(
            self, system, flags=lsm.Client.FLAG_RSVD)

        Version:
            1.2
        Usage:
            This method is dedicated to local hardware RAID cards.
            Query out all supported RAID types and strip sizes which could
            be used by lsm.Client.volume_raid_create() method.
        Parameters:
            system (lsm.System)
                Instance of lsm.System
            flags (int)
                Optional. Reserved for future use.
                Should be set as lsm.Client.FLAG_RSVD.
        Returns:
            [raid_types, strip_sizes]
                raid_types ([int])
                    List of integer, possible values are:
                        Volume.RAID_TYPE_RAID0
                        Volume.RAID_TYPE_RAID1
                        Volume.RAID_TYPE_RAID5
                        Volume.RAID_TYPE_RAID6
                        Volume.RAID_TYPE_RAID10
                        Volume.RAID_TYPE_RAID50
                        Volume.RAID_TYPE_RAID60
                strip_sizes ([int])
                    List of integer. Stripe size in bytes.
        SpecialExceptions:
            LsmError
                lsm.ErrorNumber.NO_SUPPORT
                    Method not supported.
        Sample:
            lsm_client = lsm.Client('sim://')
            lsm_sys = lsm_client.systems()[0]
            disks = lsm_client.disks(
                search_key='system_id', search_value=lsm_sys.id)

            free_disks = [d for d in disks if d.status == Disk.STATUS_FREE]
            supported_raid_types, supported_strip_sizes = \
                lsm_client.volume_raid_create_cap_get(lsm_sys)
            new_vol = lsm_client.volume_raid_create(
                'test_volume_raid_create', supported_raid_types[0],
                free_disks, supported_strip_sizes[0])

        Capability:
            lsm.Capabilities.VOLUME_CREATE_RAID
                This method is mandatory when volume_raid_create() is
                supported.
        """
        return self._tp.rpc('volume_raid_create_cap_get', _del_self(locals()))

    # Create a disk RAID pool and allocate entire storage space to new volume
    # @param    self            The this pointer
    # @param    name            Name of the raid to be created
    # @param    raid_type       The raid type (enumerated) of the raid group
    # @param    disks           List of disk objects to use
    # @param    strip_size      Strip size in bytes
    # @param    flags           Flags
    # @returns  the newly created volume, lsmError on errors
    @_return_requires(Volume)
    def volume_raid_create(self, name, raid_type, disks, strip_size,
                           flags=FLAG_RSVD):
        """
        lsm.Client.volume_raid_create(self, name, raid_type, disks,
                                      strip_size, flags=lsm.Client.FLAG_RSVD)

        Version:
            1.2
        Usage:
            This method is dedicated to local hardware RAID cards.
            Create a disk RAID pool and allocate entire storage space to
            new volume using requested volume name.
            When dealing with RAID10, 50 or 60, the first half part of
            'disks' will be located in one bottom layer RAID group.
            The new volume and new pool will created within the same system
            of provided disks.
            This method does not allow duplicate call, when duplicate call
            was issued, LsmError with ErrorNumber.DISK_NOT_FREE will be raise.
            User should check disk.status for Disk.STATUS_FREE before
            invoking this method.
        Parameters:
            name (string)
                The name for new volume.
                The requested volume name might be ignored due to restriction
                of hardware RAID vendors.
                The pool name will be automatically choosed by plugin.
            raid_type (int)
                The RAID type for the RAID group, possible values are:
                    Volume.RAID_TYPE_RAID0
                    Volume.RAID_TYPE_RAID1
                    Volume.RAID_TYPE_RAID5
                    Volume.RAID_TYPE_RAID6
                    Volume.RAID_TYPE_RAID10
                    Volume.RAID_TYPE_RAID15
                    Volume.RAID_TYPE_RAID16
                    Volume.RAID_TYPE_RAID50
                Please check volume_raid_create_cap_get() returns to get
                supported all raid types of current hardware RAID card.
            disks ([lsm.Disks,])
                A list of lsm.Disk objects. Free disks used for new RAID group.
            strip_size (int)
                The size in bytes of strip.
                When setting strip_size to Volume.VCR_STRIP_SIZE_DEFAULT, it
                allow hardware RAID cards to choose their default value.
                Please use volume_raid_create_cap_get() method to get all
                supported strip size of current hardware RAID card.
                The Volume.VCR_STRIP_SIZE_DEFAULT is always supported when
                lsm.Capabilities.VOLUME_CREATE_RAID is supported.
            flags (int)
                Optional. Reserved for future use.
                Should be set as lsm.Client.FLAG_RSVD.
        Returns:
            lsm.Volume
                The lsm.Volume object for newly created volume.
        SpecialExceptions:
            LsmError
                lsm.ErrorNumber.NO_SUPPORT
                    Method not supported or RAID type not supported.
                lsm.ErrorNumber.DISK_NOT_FREE
                    Disk is not in Disk.STATUS_FREE status.
                lsm.ErrorNumber.NOT_FOUND_DISK
                    Disk not found
                lsm.ErrorNumber.INVALID_ARGUMENT
                    1. Invalid input argument data.
                    2. Disks are not from the same system.
                    3. Disks are not from the same enclosure.
                    4. Invalid strip_size.
                    5. Disk count are meet the minimum requirement:
                        RAID1: len(disks) == 2
                        RAID5: len(disks) >= 3
                        RAID6: len(disks) >= 4
                        RAID10: len(disks) % 2 == 0 and len(disks) >= 4
                        RAID50: len(disks) % 2 == 0 and len(disks) >= 6
                        RAID60: len(disks) % 2 == 0 and len(disks) >= 8
                lsm.ErrorNumber.NAME_CONFLICT
                    Requested name is already be used by other volume.
        Sample:
            lsm_client = lsm.Client('sim://')
            disks = lsm_client.disks()
            free_disks = [d for d in disks if d.status == Disk.STATUS_FREE]
            new_vol = lsm_client.volume_raid_create(
                'raid0_vol1', Volume.RAID_TYPE_RAID0, free_disks)
        Capability:
            lsm.Capabilities.VOLUME_CREATE_RAID
                Indicate current system support volume_raid_create() method.
                At least one RAID type should be supported.
                The strip_size == Volume.VCR_STRIP_SIZE_DEFAULT is supported.
        """
        if len(disks) == 0:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input disks argument: no disk included")

        if raid_type == Volume.RAID_TYPE_RAID1 and len(disks) != 2:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input disks argument: RAID 1 only allow 2 disks")

        if raid_type == Volume.RAID_TYPE_RAID5 and len(disks) < 3:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input disks argument: RAID 5 require 3 or more disks")

        if raid_type == Volume.RAID_TYPE_RAID6 and len(disks) < 4:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "Illegal input disks argument: RAID 6 require 4 or more disks")

        if raid_type == Volume.RAID_TYPE_RAID10:
            if len(disks) % 2 or len(disks) < 4:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: "
                    "RAID 10 require even disks count and 4 or more disks")

        if raid_type == Volume.RAID_TYPE_RAID50:
            if len(disks) % 2 or len(disks) < 6:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: "
                    "RAID 50 require even disks count and 6 or more disks")

        if raid_type == Volume.RAID_TYPE_RAID60:
            if len(disks) % 2 or len(disks) < 8:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "Illegal input disks argument: "
                    "RAID 60 require even disks count and 8 or more disks")

        return self._tp.rpc('volume_raid_create', _del_self(locals()))

    # Enable the IDENT LED for a volume.
    # @param    self            The this pointer
    # @param    volume          Volume object to target
    # @param    flags           Flags
    # @returns None on success, else raises LsmError
    @_return_requires(None)
    def volume_ident_led_on(self, volume, flags=FLAG_RSVD):
        """
        lsm.Client.volume_ident_led_on(self, volume,
                                       flags=lsm.Client.FLAG_RSVD)

        Version:
            1.3
        Usage:
            This method enables the IDENT LED on a volume if supported
        Parameters:
            volume (lsm.Volume)
                An lsm.Volume object.
            flags (int)
                Optional. Reserved for future use.
                Should be set as lsm.Client.FLAG_RSVD.
        Returns:
            returns None on success, else raises LsmError on errors.
        SpecialExceptions:
        """
        return self._tp.rpc('volume_ident_led_on', _del_self(locals()))

    # Disable the IDENT LED for a volume.
    # @param    self            The this pointer
    # @param    volume          Volume object to target
    # @param    flags           Flags
    # @returns None on success, else raises LsmError
    @_return_requires(None)
    def volume_ident_led_off(self, volume, flags=FLAG_RSVD):
        """
        lsm.Client.volume_ident_led_off(self, volume,
                                          flags=lsm.Client.FLAG_RSVD)

        Version:
            1.3
        Usage:
            This method disables the IDENT LED on a volume if supported
        Parameters:
            volume (lsm.Volume)
                An lsm.Volume object.
            flags (int)
                Optional. Reserved for future use.
                Should be set as lsm.Client.FLAG_RSVD.
        Returns:
            returns None on success, else raises LsmError on errors.
        SpecialExceptions:
        """
        return self._tp.rpc('volume_ident_led_off', _del_self(locals()))

    @_return_requires([Battery])
    def batteries(self, search_key=None, search_value=None, flags=FLAG_RSVD):
        """
        lsm.Client.batteries(self, search_key=None, search_value=None,
                             flags=lsm.Client.FLAG_RSVD)
        Version:
            1.3
        Usage:
            Query batteries. When present, super capacitors will also be
            included.
        Parameters:
            search_key (string, optional)
                The key name for the search. Valid search keys are stored in
                lsm.Battery.SUPPORTED_SEARCH_KEYS
            search_value (string, optional)
                The value of search_key to match.
            flags (int, optional):
                Reserved for future use. Should be set as lsm.Client.FLAG_RSVD
        Returns:
            [lsm.Battery]

            lsm.Battery (object)
                lsm.Battery.id (string)
                    Unique ID for this cache hardware.
                lsm.Battery.name (string)
                    Human friendly name, might include physical location, model
                    name and etc.
                lsm.Battery.type (int)
                    The hardware type of cache. Could be one of these values:
                        lsm.Battery.TYPE_CHEMICAL
                            Chemical battery.
                        lsm.Battery.TYPE_CAPACITOR
                            Supper capacitor.
                        lsm.Battery.TYPE_OTHER
                            Vendor specific battery type.
                        lsm.Battery.TYPE_UNKNOWN
                            Unknown type.
                lsm.Battery.status (int, bitmap)
                    Could be any combination of these values:
                        lsm.Battery.STATUS_OK
                            Battery is fully charged, health and not in use
                            currently.
                        lsm.Battery.STATUS_DISCHARGING
                            Battery is in use.
                        lsm.Battery.STATUS_CHARGING
                            Battery is charging.
                        lsm.Battery.STATUS_LEARNING
                            Battery is calibrating itself by discharging
                            battery and recharging again.
                        lsm.Battery.STATUS_DEGRADED
                            Battery is in degraded mode, need attention.
                            For example, battery is near end of life.
                        lsm.Battery.STATUS_ERROR
                            Battery is having hardware error or end of life.
                        lsm.Battery.STATUS_OTHER
                            Vendor specific status.
                        lsm.Battery.STATUS_UNKNOWN
                            Unknown.
                lsm.Battery.system_id (string)
                    The id of system which current battery belong to.
        SpecialExceptions:
            LsmError
                ErrorNumber.NO_SUPPORT
        Capability:
            lsm.Capabilities.BATTERIES
        """
        _check_search_key(search_key, Battery.SUPPORTED_SEARCH_KEYS)
        return self._tp.rpc('batteries', _del_self(locals()))

    @_return_requires([int, int, int, int, int])
    def volume_cache_info(self, volume, flags=FLAG_RSVD):
        """
        lsm.Client.volume_cache_info(self, volume,
                                     flags=lsm.Client.FLAG_RSVD)

        Version:
            1.3
        Usage:
            Query RAM cache setting and status of specified volume on read
            and write I/O.
        Parameters:
            volume (Lsm.Volume)
                The lsm.Volume instance.
            flags (int, optional):
                Reserved for future use. Should be set as lsm.Client.FLAG_RSVD
        Returns:
            [int, int, int, int, int]
            write_cache_policy (int)
                The write cache policy. Valid values are:
                    * lsm.Volume.WRITE_CACHE_POLICY_WRITE_BACK
                        The storage system will use write back mode if cache
                        hardware found.
                    * lsm.Volume.WRITE_CACHE_POLICY_AUTO
                        The controller will use write back mode when
                        battery/capacitor is in good health, otherwise,
                        write through mode.
                    * lsm.Volume.WRITE_CACHE_POLICY_WRITE_THROUGH
                        The storage system will use write through mode.
                    * lsm.Volume.WRITE_CACHE_POLICY_UNKNOWN
                        Plugin failed to detect this setting.

            write_cache_status (int)
                The status of write cache. Valid values are:
                    * lsm.Volume.WRITE_CACHE_STATUS_WRITE_THROUGH
                    * lsm.Volume.WRITE_CACHE_STATUS_WRITE_BACK
                    * lsm.Volume.WRITE_CACHE_STATUS_UNKNOWN

            read_cache_policy (int)
                The policy for read cache. Valid values are:
                    * lsm.Volume.READ_CACHE_POLICY_ENABLED
                        Read cache is enabled, when reading I/O on previous
                        unchanged written I/O or read I/O in cache will be
                        returned to I/O initiator immediately without checking
                        backing store(normally disk).
                    * lsm.Volume.READ_CACHE_POLICY_DISABLED
                        Read cache is disabled.
                    * lsm.Volume.READ_CACHE_POLICY_UNKNOWN
                        Plugin failed to detect the read cache policy.
            read_cache_status (int)
                The status of read cache. Valid values are:
                    * lsm.Volume.READ_CACHE_STATUS_ENABLED
                    * lsm.Volume.READ_CACHE_STATUS_DISABLED
                    * lsm.Volume.READ_CACHE_STATUS_UNKNOWN
            physical_disk_cache (int)
                Whether physical disk's cache is enabled or not.
                Please be advised, HDD's physical disk ram cache might be not
                protected by storage system's battery or capacitor on sudden
                power loss, you could lose data if a power failure occurs
                during a write process.
                For SSD's physical disk cache, please check with the vendor of
                your hardware RAID card and SSD disk.
                Valid values are:
                    * lsm.Volume.PHYSICAL_DISK_CACHE_ENABLED
                        Physical disk cache enabled.
                    * lsm.Volume.PHYSICAL_DISK_CACHE_DISABLED
                        Physical disk cache disabled.
                    * lsm.Volume.PHYSICAL_DISK_CACHE_USE_DISK_SETTING
                        Physical disk cache is determined by the disk vendor
                        via physical disks' SCSI caching mode page(0x08 page).
                        It is strongly suggested to change this value to
                        lsm.Volume.PHYSICAL_DISK_CACHE_ENABLED or
                        lsm.Volume.PHYSICAL_DISK_CACHE_DISABLED
                    * lsm.Volume.PHYSICAL_DISK_CACHE_UNKNOWN
                        Plugin failed to detect the physical disk status.
        SpecialExceptions:
            LsmError
                ErrorNumber.NO_SUPPORT
                ErrorNumber.NOT_FOUND_VOLUME
        Capability:
            lsm.Capabilities.VOLUME_CACHE_INFO
        """
        return self._tp.rpc('volume_cache_info', _del_self(locals()))

    @_return_requires(None)
    def volume_physical_disk_cache_update(self, volume, pdc, flags=FLAG_RSVD):
        """
        lsm.Client.volume_physical_disk_cache_update(self, volume, pdc,
                                                     flags=lsm.Client.FLAG_RSVD)

        Version:
            1.3
        Usage:
            Change the setting of RAM physical disk cache of specified volume.
            On some product(like HPE SmartArray), this action will be effective
            at system level which means that even you are requesting a change
            on a specified volume, this change will apply to all other volumes
            on the same controller(system).
        Parameters:
            volume (Lsm.Volume)
                The lsm.Volume instance.
            pdc (int)
                lsm.Volume.PHYSICAL_DISK_CACHE_ENABLED
                    Enable physical disk cache.
                lsm.Volume.PHYSICAL_DISK_CACHE_DISABLED
                    Disable physical disk cache
            flags (int, optional):
                Reserved for future use. Should be set as lsm.Client.FLAG_RSVD
        Returns:
            N/A
        SpecialExceptions:
            LsmError
                ErrorNumber.NO_SUPPORT
                    You might also get NO_SUPPORT error when trying
                    to change SSD physical disk cache on MegaRAID.
                ErrorNumber.NOT_FOUND_VOLUME
        Capability:
            lsm.Capabilities.VOLUME_PHYSICAL_DISK_CACHE_SET
                Allow changing physical disk cache.
            lsm.Capabilities.VOLUME_PHYSICAL_DISK_CACHE_SET_SYSTEM_LEVEL
                Indicate that this action will change system settings which
                are effective on all volumes in this storage system.
                For example, on HPE SmartArray, the physical disk cache
                setting is a controller level setting.
        """
        if (pdc != Volume.PHYSICAL_DISK_CACHE_ENABLED) and \
           (pdc != Volume.PHYSICAL_DISK_CACHE_DISABLED):
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Argument pdc should be "
                           "Volume.PHYSICAL_DISK_CACHE_ENABLED or "
                           "Volume.PHYSICAL_DISK_CACHE_DISABLED")

        return self._tp.rpc('volume_physical_disk_cache_update',
                            _del_self(locals()))

    @_return_requires(None)
    def volume_write_cache_policy_update(self, volume, wcp, flags=FLAG_RSVD):
        """
        lsm.Client.volume_write_cache_policy_update(self, volume, wcp,
                                                    flags=lsm.Client.FLAG_RSVD)

        Version:
            1.3
        Usage:
            Change the RAM write cache policy on specified volume.
            If lsm.Capabilities.VOLUME_WRITE_CACHE_POLICY_SET_IMPACT_READ
            is supported(e.g. HPE SmartArray), the changes on write cache
            policy might also impact read cache policy.
            If lsm.Capabilities.VOLUME_WRITE_CACHE_POLICY_SET_WB_IMPACT_OTHER
            is supported(e.g. HPE SmartArray), changing write cache policy
            to write back mode might impact other volumes in the same system.
        Parameters:
            volume (Lsm.Volume)
                The lsm.Volume instance.
            wcp (int)
                Could be one of these value:
                    * lsm.Volume.WRITE_CACHE_POLICY_WRITE_BACK
                        Change to write back mode.
                    * lsm.Volume.WRITE_CACHE_POLICY_AUTO
                        Change to auto mode: use write back mode when
                        battery/capacitor is healthy, otherwise use write
                        through.
                    * lsm.Volume.WRITE_CACHE_POLICY_WRITE_THROUGH
                        Change to write through mode.
            flags (int, optional):
                Reserved for future use. Should be set as lsm.Client.FLAG_RSVD
        Returns:
            N/A
        SpecialExceptions:
            LsmError
                ErrorNumber.NO_SUPPORT
                ErrorNumber.NOT_FOUND_VOLUME
        Capability:
            lsm.Capabilities.VOLUME_WRITE_CACHE_POLICY_SET_WRITE_BACK
                Allow changing to always mode.
            lsm.Capabilities.VOLUME_WRITE_CACHE_POLICY_SET_AUTO
                Allow changing to auto mode.
            lsm.Capabilities.VOLUME_WRITE_CACHE_POLICY_SET_WRITE_THROUGH
                Allow changing to disable mode.
            lsm.Capabilities.VOLUME_WRITE_CACHE_POLICY_SET_IMPACT_READ
                Indicate this action might impact read cache policy.
            lsm.Capabilities.VOLUME_WRITE_CACHE_POLICY_SET_WB_IMPACT_OTHER
                Indicate that changing to write back mode might impact other
                volumes. For example, on HPE SmartArray, changing to write back
                mode will change all other volumes with auto write cache policy
                to write back mode.
        """
        if wcp != Volume.WRITE_CACHE_POLICY_WRITE_BACK and \
           wcp != Volume.WRITE_CACHE_POLICY_AUTO and \
           wcp != Volume.WRITE_CACHE_POLICY_WRITE_THROUGH:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Argument wcp should be "
                           "Volume.WRITE_CACHE_POLICY_WRITE_BACK or "
                           "Volume.WRITE_CACHE_POLICY_AUTO or "
                           "Volume.WRITE_CACHE_POLICY_WRITE_THROUGH")
        return self._tp.rpc('volume_write_cache_policy_update',
                            _del_self(locals()))

    @_return_requires(None)
    def volume_read_cache_policy_update(self, volume, rcp, flags=FLAG_RSVD):
        """
        lsm.Client.volume_read_cache_policy_update(self, volume, rcp,
                                                   flags=lsm.Client.FLAG_RSVD)

        Version:
            1.3
        Usage:
            Change the RAM read cache policy of specified volume.
            If lsm.Capabilities.VOLUME_READ_CACHE_POLICY_SET_IMPACT_WRITE
            is supported(like HPE SmartArray), the change on write cache policy
            might also impact read cache policy.
        Parameters:
            volume (Lsm.Volume)
                The lsm.Volume instance.
            rcp (int)
                Could be one of these value:
                    * lsm.Volume.READ_CACHE_POLICY_ENABLED
                        Enable read cache.
                    * lsm.Volume.READ_CACHE_POLICY_DISABLED
                        Disable read cache.
            flags (int, optional):
                Reserved for future use. Should be set as lsm.Client.FLAG_RSVD
        Returns:
            N/A
        SpecialExceptions:
            LsmError
                ErrorNumber.NO_SUPPORT
                ErrorNumber.NOT_FOUND_VOLUME
        Capability:
            lsm.Capabilities.VOLUME_READ_CACHE_POLICY_SET
                Allow enabling or disabling read cache policy.
            lsm.Capabilities.VOLUME_READ_CACHE_POLICY_SET_IMPACT_WRITE
                Indicate that changing read cache policy might impact write
                cache policy. For example, on HPE SmartArray, disabling read
                cache will also change write cache policy to write through.
        """
        if rcp != Volume.READ_CACHE_POLICY_ENABLED and \
           rcp != Volume.READ_CACHE_POLICY_DISABLED:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Argument rcp should be "
                           "Volume.READ_CACHE_POLICY_ENABLED or "
                           "Volume.READ_CACHE_POLICY_DISABLED")
        return self._tp.rpc('volume_read_cache_policy_update',
                            _del_self(locals()))
