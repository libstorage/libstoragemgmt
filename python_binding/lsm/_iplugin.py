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
# License along with this library; If not, see <http://www.gnu.org/licenses/>.
#
# Author: tasleson

from abc import ABCMeta as _ABCMeta
from abc import abstractmethod as _abstractmethod
from lsm import LsmError, ErrorNumber
from six import with_metaclass


class IPlugin(with_metaclass(_ABCMeta, object)):
    """
    Plug-in interface that all plug-ins must implement for basic
    operation.
    """

    @_abstractmethod
    def plugin_register(self, uri, password, timeout, flags=0):
        """
        Method first called to setup the plug-in (except for plugin_info)

        This would be the place to make a connection to the array.

        Returns None on success, else LsmError exception
        """
        pass

    @_abstractmethod
    def time_out_set(self, ms, flags=0):
        """
        Sets any time-outs for the plug-in (ms)

        Returns None on success, else LsmError exception
        """
        pass

    @_abstractmethod
    def time_out_get(self, flags=0):
        """
        Retrieves the current time-out

        Returns time-out in ms, else raise LsmError
        """
        pass

    @_abstractmethod
    def plugin_unregister(self, flags=0):
        """
        Called when the client wants to finish up or the socket goes eof.
        Plug-in should clean up all resources.  Note: In the case where
        the socket goes EOF and the plugin_unregister runs into errors the
        exception(s) will not be delivered to the client!

        Returns None on success, else LsmError exception
        """
        pass

    @_abstractmethod
    def job_status(self, job_id, flags=0):
        """
        Returns the stats of the given job.

        Returns a tuple ( status (enumeration), percent_complete,
                            completed item).
        else LsmError exception.
        """
        pass

    @_abstractmethod
    def job_free(self, job_id, flags=0):
        """
        Frees resources for a given job.

        Returns None on success, else raises an LsmError
        """
        pass

    @_abstractmethod
    def capabilities(self, system, flags=0):
        """
        Returns the capabilities for the selected system, raises LsmError
        """
        pass

    @_abstractmethod
    def plugin_info(self, flags=0):
        """
        Returns the description and version for plug-in, raises LsmError

        Note: Make sure plugin can handle this call before plugin_register is
        called.
        """
        pass

    @_abstractmethod
    def pools(self, search_key=None, search_value=None, flags=0):
        """
        Returns an array of pool objects.  Pools are used in both block and
        file system interfaces, thus the reason they are in the base class.

        Raises LsmError on error
        """
        pass

    @_abstractmethod
    def systems(self, flags=0):
        """
        Returns an array of system objects.  System information is used to
        distinguish resources from on storage array to another when the plug=in
        supports the ability to have more than one array managed by it

        Raises LsmError on error
        """
        pass


class IStorageAreaNetwork(IPlugin):

    def volumes(self, search_key=None, search_value=None, flags=0):
        """
        Returns an array of volume objects

        Raises LsmError on error
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags=0):
        """
        Creates a volume, given a pool, volume name, size and provisioning

        Returns a tuple (job_id, new volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_delete(self, volume, flags=0):
        """
        Deletes a volume.

        Returns Job id or None if completed, else raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_resize(self, volume, new_size_bytes, flags=0):
        """
        Re-sizes a volume.

        Returns a tuple (job_id, re-sized_volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_replicate(self, pool, rep_type, volume_src, name, flags=0):
        """
        Replicates a volume from the specified pool.  In this library, to
        replicate means to create a new volume which is a copy of the source.

        Returns a tuple (job_id, replicated volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_replicate_range_block_size(self, system, flags=0):
        """
        Returns the number of bytes per block for volume_replicate_range
        call.  Callers of volume_replicate_range need to use this when
        calculating start and block lengths.

        Note: bytes per block may not match volume blocksize.

        Returns bytes per block, Raises LsmError on error
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_replicate_range(self, rep_type, volume_src, volume_dest, ranges,
                               flags=0):
        """
        Replicates a portion of a volume to itself or another volume.  The src,
        dest and number of blocks values change with vendor, call
        volume_replicate_range_block_size to get block unit size.

        Returns Job id or None if completed, else raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_enable(self, volume, flags=0):
        """
        Makes a volume available to the host

        Returns None on success, else raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_disable(self, volume, flags=0):
        """
        Makes a volume unavailable to the host

        Returns None on success, else raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def iscsi_chap_auth(self, init_id, in_user, in_password, out_user,
                        out_password, flags):
        """
        Register a user/password for the specified initiator for CHAP
        authentication.  in_user & in_password are for inbound CHAP, out_user &
        out_password are for outbound CHAP.

        Note: Setting in_user, in_password or out_user, out_password to None
        will disable authentication.

        Raises LsmError on error
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_mask(self, access_group, volume, flags=0):
        """
        Allows an access group to access a volume.

        Returns None on success, else raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_unmask(self, access_group, volume, flags=0):
        """
        Revokes access for an access group for a volume

        Returns None on success, else raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def access_groups(self, search_key=None, search_value=None, flags=0):
        """
        Returns a list of access groups, raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def access_group_create(self, name, init_id, init_type, system,
                            flags=0):
        """
        Returns a list of access groups, raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def access_group_delete(self, access_group, flags=0):
        """
        Deletes an access group, Raises LsmError on error
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def access_group_initiator_add(self, access_group, init_id, init_type,
                                   flags=0):
        """
        Adds an initiator to an access group, Raises LsmError on error
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def access_group_initiator_delete(self, access_group, init_id, init_type,
                                      flags=0):
        """
        Deletes an initiator from an access group, Raises LsmError on error
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volumes_accessible_by_access_group(self, access_group, flags=0):
        """
        Returns the list of volumes that access group has access to.
        Raises LsmError on error
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def access_groups_granted_to_volume(self, volume, flags=0):
        """
        Returns the list of access groups that have access to the specified,
        Raises LsmError on error
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_child_dependency(self, volume, flags=0):
        """
        Returns True if this volume has other volumes which are dependant on
        it.  Implies that this volume cannot be deleted or possibly modified
        because it would affect its children.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def volume_child_dependency_rm(self, volume, flags=0):
        """
        If this volume has child dependency, this method call will fully
        replicate the blocks removing the relationship between them.  This
        should return None (success) if volume_child_dependency would return
        False.

        Note:  This operation could take a very long time depending on the size
        of the volume and the number of child dependencies.

        Returns None if complete else job id, raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def target_ports(self, search_key=None, search_value=None, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")


class INetworkAttachedStorage(IPlugin):
    """
    Class the represents Network attached storage (Common NFS/CIFS operations)
    """
    def fs(self, search_key=None, search_value=None, flags=0):
        """
        Returns a list of file systems on the controller. Raises LsmError on
        errors.
        """
        pass

    def fs_delete(self, fs, flags=0):
        """
        WARNING: Destructive

        Deletes a file system and everything it contains
        Returns None on success, else job id
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def fs_resize(self, fs, new_size_bytes, flags=0):
        """
        Re-size a file system

        Returns a tuple (job_id, re-sized file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def fs_create(self, pool, name, size_bytes, flags=0):
        """
        Creates a file system given a pool, name and size.
        Note: size is limited to 2**64 bytes so max size of a single volume
        at this time is 16 Exabytes

        Returns a tuple (job_id, file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def fs_clone(self, src_fs, dest_fs_name, snapshot=None, flags=0):
        """
        Creates a thin, point in time read/writable copy of src to dest.
        Optionally uses snapshot as backing of src_fs

        Returns a tuple (job_id, file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def fs_file_clone(self, fs, src_file_name, dest_file_name, snapshot=None,
                      flags=0):
        """
        Creates a thinly provisioned clone of src to dest.
        Note: Source and Destination are required to be on same filesystem

        Returns Job id or None if completed, else raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def fs_snapshots(self, fs, flags=0):
        """
        Returns a list of snapshots for the supplied file system,
        Raises LsmError on error
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def fs_snapshot_create(self, fs, snapshot_name, flags=0):
        """
        Snapshot is a point in time read-only copy

        Create a snapshot on the chosen file system

        Returns a tuple (job_id, snap shot created)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.

        Note:  Snapshot name may not match
        what was passed in (depends on array implementation)
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def fs_snapshot_delete(self, fs, snapshot, flags=0):
        """
        Frees the re-sources for the given snapshot on the supplied filesystem.

        Returns Job id or None if completed, else raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def fs_snapshot_restore(self, fs, snapshot, files, restore_files,
                            all_files=False, flags=0):
        """
        WARNING: Destructive!

        Reverts a file-system or just the specified files from the snapshot.
        If a list of files is supplied but the array cannot restore just them
        then the operation will fail with an LsmError raised.
        If files == None and all_files = True then all files on the
        file-system are restored.

        Restore_file if not None must be the same length as files with each
        index in each list referring to the associated file.

        Returns None on success, else job id, LsmError exception on error
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def fs_child_dependency(self, fs, files, flags=0):
        """
        Returns True if the specified filesystem or specified file on this
        file system has child dependencies.  This implies that this filesystem
        or specified file on this file system cannot be deleted or possibly
        modified because it would affect its children.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def fs_child_dependency_rm(self, fs, files, flags=0):
        """
        If this filesystem or specified file on this filesystem has child
        dependency this method will fully replicate the blocks removing the
        relationship between them.  This should return None(success) if
        fs_child_dependency would return False.

        Note:  This operation could take a very long time depending on the size
        of the filesystem and the number of child dependencies.

        Returns Job id or None if completed, else raises LsmError on errors.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")


class INfs(INetworkAttachedStorage):
    def export_auth(self, flags=0):
        """
        Returns the types of authentication that are available for NFS
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def exports(self, search_key=None, search_value=None, flags=0):
        """
        Get a list of all exported file systems on the controller.
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def export_fs(self, fs_id, export_path, root_list, rw_list, ro_list,
                  anon_uid, anon_gid, auth_type, options, flags=0):
        """
        Exports a filesystem as specified in the export
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def export_remove(self, export, flags=0):
        """
        Removes the specified export
        """
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")
