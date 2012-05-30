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

from abc import ABCMeta, abstractmethod

class IPlugin(object):
    """
    Plug-in interface that all plug-ins must implement for basic
    operation.
    """
    __metaclass__ = ABCMeta

    @abstractmethod
    def startup(self, uri, password, timeout):
        """
        Method first called to setup the plug-in

        Return None on success, else LsmError exception
        """
        pass

    @abstractmethod
    def set_time_out(self, ms):
        """
        Sets any time-outs for the plug-in (ms)

        Return None on success, else LsmError exception
        """
        pass

    @abstractmethod
    def get_time_out(self):
        """
        Retrieves the current time-out

        Return time-out in ms, else raise LsmError
        """
        pass

    @abstractmethod
    def shutdown(self):
        """
        Called when the client wants to finish up or the socket goes eof.
        Plug-in should clean up all resources.  Note: In the case where
        the socket goes EOF and the shutdown runs into errors the exception(s)
        will not be delivered to the client!

        Return None on success, else LsmError exception
        """
        pass

    @abstractmethod
    def job_status(self, job_id):
        """
        Returns the stats of the given job.

        Returns a tuple ( status (enumeration), percent_complete, completed item).
        else LsmError exception.
        """
        pass

    @abstractmethod
    def job_free(self, job_id):
        """
        Frees resources for a given job.

        Returns None on success, else raises an LsmError
        """
        pass

    @abstractmethod
    def pools(self):
        """
        Returns an array of pool objects.  Pools are used in both block and
        file system interfaces, thus the reason they are in the base class.
        """
        pass

    @abstractmethod
    def systems(self):
        """
        Returns an array of system objects.  System information is used to
        distinguish resources from on storage array to another when the plug=in
        supports the ability to have more than one array managed by it
        """
        pass

class IStorageAreaNetwork(IPlugin):

    @abstractmethod
    def volumes(self):
        """
        Returns an array of volume objects

        """
        pass

    @abstractmethod
    def initiators(self):
        """
        Return an array of initiator objects
        """
        pass

    @abstractmethod
    def volume_create(self, pool, volume_name, size_bytes, provisioning):
        """
        Creates a volume, given a pool, volume name, size and provisioning

        returns a tuple (job_id, new volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        pass

    @abstractmethod
    def volume_delete(self, volume):
        """
        Deletes a volume.

        Returns None on success, else raises an LsmError
        """
        pass

    @abstractmethod
    def volume_resize(self, volume, new_size_bytes):
        """
        Re-sizes a volume.

        Returns a tuple (job_id, re-sized_volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """

        pass

    @abstractmethod
    def volume_replicate(self, pool, rep_type, volume_src, name):
        """
        Replicates a volume from the specified pool.

        Returns a tuple (job_id, replicated volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        pass

    @abstractmethod
    def volume_replicate_range_block_size(self):
        """
        Returns the number of bytes per block for volume_replicate_range
        call.  Callers of volume_replicate_range need to use this when
        calculating start and block lengths.

        Note: bytes per block may not match volume blocksize.

        Returns bytes per block.
        """
        pass

    @abstractmethod
    def volume_replicate_range(self, rep_type, volume_src, volume_dest, ranges):
        """
        Replicates a portion of a volume to itself or another volume.  The src,
        dest and number of blocks values change with vendor, call
        volume_replicate_range_block_size to get block unit size.

        Returns Job id or None if completed, else raises LsmError on errors.
        """
        pass

    @abstractmethod
    def volume_online(self, volume):
        """
        Makes a volume available to the host

        returns None on success, else raises LsmError on errors.
        """
        pass

    @abstractmethod
    def volume_offline(self, volume):
        """
        Makes a volume unavailable to the host

        returns None on success, else raises LsmError on errors.
        """
        pass

    def access_group_grant(self, group, volume, access):
        """
        Allows an access group to access a volume.
        """
        pass

    def access_group_revoke(self, group, volume):
        """
        Revokes access for an access group for a volume
        """
        pass

    @abstractmethod
    def access_group_list(self):
        """
        Returns a list of access groups
        """
        pass

    @abstractmethod
    def access_group_create(self, name, initiator_id, id_type, system_id):
        """
        Returns a list of access groups
        """
        pass

    @abstractmethod
    def access_group_del(self, group):
        """
        Deletes an access group
        """
        pass

    @abstractmethod
    def access_group_add_initiator(self, group, initiator_id, id_type):
        """
        Adds an initiator to an access group
        """
        pass

    @abstractmethod
    def access_group_del_initiator(self, group, initiator):
        """
        Deletes an initiator from an access group
        """
        pass

    @abstractmethod
    def volumes_accessible_by_access_group(self, group):
        """
        Returns the list of volumes that access group has access to.
        """
        pass

    @abstractmethod
    def access_groups_granted_to_volume(self, volume):
        """
        Returns the list of access groups that have access to the specified
        """
        pass

    @abstractmethod
    def volume_child_dependency(self, volume):
        """
        Returns True if this volume has other volumes which are dependant on it.
        Implies that this volume cannot be deleted or possibly modified because
        it would affect its children.
        """
        pass

    @abstractmethod
    def volume_child_dependency_rm(self, volume):
        """
        If this volume has child dependency, this method call will fully
        replicate the blocks removing the relationship between them.  This
        should return None (success) if volume_child_dependency would return
        False.

        Note:  This operation could take a very long time depending on the size
        of the volume and the number of child dependencies.

        Returns None if complete else job id, raises LsmError on errors.
        """
        pass

class INetworkAttachedStorage(IPlugin):
    """
    Class the represents Network attached storage (Common NFS/CIFS operations)
    """
    @abstractmethod
    def fs(self):
        """
        Returns a list of file systems on the controller.
        """
        pass

    @abstractmethod
    def fs_delete(self, fs):
        """
        WARNING: Destructive

        Deletes a file system and everything it contains
        Returns None on success, else job id
        """
        pass

    @abstractmethod
    def fs_resize(self, fs, new_size_bytes):
        """
        Re-size a file system

        Returns a tuple (job_id, re-sized file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        pass

    @abstractmethod
    def fs_create(self, pool, name, size_bytes):
        """
        Creates a file system given a pool, name and size.
        Note: size is limited to 2**64 bytes so max size of a single volume
        at this time is 16 Exabytes

        Returns a tuple (job_id, file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        pass

    @abstractmethod
    def fs_clone(self, src_fs, dest_fs_name, snapshot=None):
        """
        Creates a thin, point in time read/writable copy of src to dest.
        Optionally uses snapshot as backing of src_fs

        Returns a tuple (job_id, file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        pass

    @abstractmethod
    def file_clone(self, fs, src_file_name, dest_file_name, snapshot=None):
        """
        Creates a thinly provisioned clone of src to dest.
        Note: Source and Destination are required to be on same filesystem

        Returns None on success, else job id
        """
        pass

    @abstractmethod
    def snapshots(self, fs):
        """
        Returns a list of snapshots for the supplied file system
        """
        pass

    @abstractmethod
    def snapshot_create(self, fs, snapshot_name, files=None):
        """
        Snapshot is a point in time read-only copy

        Create a snapshot on the chosen file system with a supplied name for
        each of the files.  Passing None implies snapping all files on the file
        system.  When files is non-none it implies snap shoting those file.
        NOTE:  Some arrays only support snapshots at the file system level.  In
        this case it will not be considered an error if file names are passed.
        In these cases the file names are effectively discarded as all files
        are done.

        Returns Snapshot that was created.  Note:  Snapshot name may not match
        what was passed in (depends on array implementation)
        """
        pass

    @abstractmethod
    def snapshot_delete(self, fs, snapshot):
        """
        Frees the re-sources for the given snapshot on the supplied filesystem.

        Returns None on success else job id, LsmError exception on error
        """
        pass

    @abstractmethod
    def snapshot_revert(self, fs, snapshot, files, restore_files, all_files=False):
        """
        WARNING: Destructive!

        Reverts a file-system or just the specified files from the snapshot.  If
        a list of files is supplied but the array cannot restore just them then
        the operation will fail with an LsmError raised.  If files == None and
        all_files = True then all files on the file-system are reverted.

        Restore_file if not None must be the same length as files with each
        index in each list referring to the associated file.

        Returns None on success, else job id, LsmError exception on error
        """
        pass

    @abstractmethod
    def fs_child_dependency(self, fs, files=None):
        """
        Returns True if the specified filesystem or specified file on this
        file system has child dependencies.  This implies that this filesystem
        or specified file on this file system cannot be deleted or possibly
        modified because it would affect its children.
        """
        pass

    @abstractmethod
    def fs_child_dependency_rm(self, fs, files=None):
        """
        If this filesystem or specified file on this filesystem has child
        dependency this method will fully replicate the blocks removing the
        relationship between them.  This should return None(success) if
        fs_child_dependency would return False.

        Note:  This operation could take a very long time depending on the size
        of the filesystem and the number of child dependencies.

        Returns None if completed, else job id.  Raises LsmError on errors.
        """
        pass

class INfs(INetworkAttachedStorage):
    @abstractmethod
    def export_auth(self):
        """
        Returns the types of authentication that are available for NFS
        """
        pass

    @abstractmethod
    def exports(self):
        """
        Get a list of all exported file systems on the controller.
        """
        pass

    @abstractmethod
    def export_fs(self, export):
        """
        Exports a filesystem as specified in the export
        """
        pass

    @abstractmethod
    def export_remove(self, export):
        """
        Removes the specified export
        """
        pass

