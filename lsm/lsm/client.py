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

import time
import os
import unittest
import urlparse
from data import Volume
from iplugin import INetworkAttachedStorage
from transport import Transport
import common

## Removes self for the hash d
# @param    d   Hash to remove self from
# @returns d with hash removed.
def del_self(d):
    """
    Used to remove the self key from the dict d.  Self is included when calling
    the function locals() in a class method.
    """
    del d['self']
    return d

## Main client class for library.
# ** IMPORTANT **
# Theory of operation for methods in this class.
# We are using the name of the method and the name of the parameters and
# using python introspection abilities to translate them to the method and
# parameter names.  Makes the code compact, but you will break things if the
# IPlugin class does not match the method names and parameters here!
class Client(INetworkAttachedStorage):
    """
    Client side class used for managing storage that utilises RPC mechanism.
    """
    ## Method added so shat the interface for the client RPC and the plug-in
    ## itself match.
    def startup(self, uri, plain_text_password, timeout_ms):
        raise RuntimeError("Do not call directly!")

    ## Called when we are ready to initialize the plug-in.
    # @param    self    The this pointer
    # @param    uri     The uniform resource identifier
    # @param    plain_text_password     Password as plain text
    # @param    timeout_ms  The timeout in ms
    # @returns None
    def __start(self, uri, plain_text_password, timeout_ms):
        """
        Instruct the plug-in to get ready
        """
        
        self.tp.send_req('startup',
                {'uri': uri, 'password': plain_text_password,
                 'timeout': timeout_ms})
        self.tp.read_resp()

    ## Class constructor
    # @param    self    The this pointer
    # @param    uri     The uniform resource identifier
    # @param    plain_text_password     Password as plain text (Optional)
    # @param    timeout_ms  The timeout in ms
    # @returns None
    def __init__(self, uri, plain_text_password=None, timeout_ms=30000):
        self.uri = uri
        self.password = plain_text_password
        self.timeout = timeout_ms
        self.uds_path = common.UDS_PATH

        u = urlparse.urlparse(uri)

        #Figure out which path to use
        if 'LSM_UDS_PATH' in os.environ:
            self.uds_path = os.environ['LSM_UDS_PATH']

        scheme = u.scheme
        if "+" in u.scheme:
            (plug,proto) = scheme.split("+")
            scheme = plug

        self.plugin_path = self.uds_path + '/' + scheme

        if os.path.exists(self.plugin_path):
            self.tp = Transport(Transport.getSocket(self.plugin_path))
        else:
            raise ValueError("Plug-in " + u.scheme + " not found!")

        self.__start(uri, plain_text_password, timeout_ms)

    ## Synonym for close.
    def shutdown(self):
        """
        Synonym for close.
        """
        self.close()

    ## Does an orderly shutdown of the plug-in
    # @param    self    The this pointer
    def close(self):
        """
        Does an orderly shutdown of the plug-in
        """
        self.tp.rpc('shutdown', None)
        self.tp.close()
        self.tp = None

    ## Sets the timeout for the plug-in
    # @param    self    The this pointer
    # @param    ms      Time-out in ms
    def set_time_out(self, ms):
        """
        Sets any time-outs for the plug-in (ms)

        Return None on success, else LsmError exception
        """
        return self.tp.rpc('set_time_out', del_self(locals()))

    ## Retrieves the current time-out value.
    # @param    self    The this pointer
    # @returns  Time-out value
    def get_time_out(self):
        """
        Retrieves the current time-out

        Return time-out in ms, else raise LsmError
        """
        return self.tp.rpc('get_time_out', del_self(locals()))

    ## Retrieves the status of the specified job id.
    # @param    self    The this pointer
    # @param    job_id  The job identifier
    # @returns A tuple ( status (enumeration), percent_complete, completed item)
    def job_status(self, job_id):
        """
        Returns the stats of the given job.

        Returns a tuple ( status (enumeration), percent_complete, completed item).
        else LsmError exception.
        """
        return self.tp.rpc('job_status', del_self(locals()))

    ## Frees the resources for the specfied job id.
    # @param    self    The this pointer
    # @param    job_id  Job id in which to release resource for
    def job_free(self, job_id):
        """
        Frees resources for a given job number.

        Returns None on success, else raises an LsmError
        """
        return self.tp.rpc('job_free', del_self(locals()))

    def capabilities(self):
        raise NotImplemented()

    ## Returns an array of pool objects.
    # @param    self    The this pointer
    # @returns An array of pool objects.
    def pools(self):
        """
        Returns an array of pool objects.  Pools are used in both block and
        file system interfaces, thus the reason they are in the base class.
        """
        return self.tp.rpc('pools', del_self(locals()))

    ## Returns an array of system objects.
    # @param    self    The this pointer
    # @returns An array of system objects.
    def systems(self):
        """
        Returns an array of system objects.  System information is used to
        distinguish resources from on storage array to another when the plug=in
        supports the ability to have more than one array managed by it
        """
        return self.tp.rpc('systems', del_self(locals()))

    ## Returns an array of initiator objects
    # @param    self    The this pointer
    # @returns An array of initiator objects.
    def initiators(self):
        """
        Return an array of initiator objects
        """
        return self.tp.rpc('initiators', del_self(locals()))

    ## Returns an array of volume objects
    # @param    self    The this pointer
    # @returns An array of volume objects.
    def volumes(self):
        """
        Returns an array of volume objects
        """
        return self.tp.rpc('volumes', del_self(locals()))

    ## Creates a volume
    # @param    self    The this pointer
    # @param    pool    The pool object to allocate storage from
    # @param    volume_name The human text name for the volume
    # @param    size_bytes  Size of the volume in bytes
    # @param    provisioning    How the volume is to be provisioned
    # @returns  A tuple (job_id, new volume), when one is None the other is valid.
    def volume_create(self, pool, volume_name, size_bytes, provisioning):
        """
        Creates a volume, given a pool, volume name, size and provisioning

        returns a tuple (job_id, new volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('volume_create', del_self(locals()))

    ## Re-sizes a volume
    # @param    self    The this pointer
    # @param    volume  The volume object to re-size
    # @param    new_size_bytes  Size of the volume in bytes
    # @returns  A tuple (job_id, new re-sized volume), when one is None the other is valid.
    def volume_resize(self, volume, new_size_bytes):
        """
        Re-sizes a volume.

        Returns a tuple (job_id, re-sized_volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('volume_resize', del_self(locals()))

    ## Replicates a volume from the specified pool.
    # @param    self    The this pointer
    # @param    pool    The pool to re-size from
    # @param    rep_type    Replication type (enumeration,see common.data.Volume)
    # @param    volume_src  The volume to replicate
    # @param    name    Human readable name of replicated volume
    # @returns  A tuple (job_id, new replicated volume), when one is None the other is valid.
    def volume_replicate(self, pool, rep_type, volume_src, name):
        """
        Replicates a volume from the specified pool.

        Returns a tuple (job_id, replicated volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('volume_replicate', del_self(locals()))

    ## Size of a replicated block.
    # @param    self    The this pointer
    # @returns  Size of the replicated block in bytes
    def volume_replicate_range_block_size(self):
        """
        Returns the size of a replicated block in bytes.
        """
        return self.tp.rpc('volume_replicate_range_block_size', del_self(locals()))

    ## Replicates a portion of a volume to itself or another volume.
    # @param    self    The this pointer
    # @param    rep_type    Replication type (enumeration, see common.data.Volume)
    # @param    volume_src  The volume src to replicate from
    # @param    volume_dest The volume dest to replicate to
    # @param    ranges      An array of Block range objects @see lsm.common.data.BlockRange
    def volume_replicate_range(self, rep_type, volume_src, volume_dest, ranges):
        """
        Replicates a portion of a volume to itself or another volume.  The src,
        dest and number of blocks values change with vendor, call
        volume_replicate_range_block_size to get block unit size.

        Returns Job id or None when completed, else raises LsmError on errors.
        """
        return self.tp.rpc('volume_replicate_range', del_self(locals()))

    ## Deletes a volume
    # @param    self    The this pointer
    # @param    volume  The volume object which represents the volume to delete
    # @returns None on success, else job id.  Raises LsmError on errors.
    def volume_delete(self, volume):
        """
        Deletes a volume.

        Returns None on success, else job id
        """
        return self.tp.rpc('volume_delete', del_self(locals()))

    ## Makes a volume online and available to the host.
    # @param    self    The this pointer
    # @param    volume  The volume to place online
    # @returns None on success, else raises LsmError
    def volume_online(self, volume):
        """
        Makes a volume available to the host

        returns None on success, else raises LsmError on errors.
        """
        return self.tp.rpc('volume_online', del_self(locals()))

    ## Takes a volume offline
    # @param    self    The this pointer
    # @param    volume  The volume object
    # @returns None on success, else raises LsmError on errors.
    def volume_offline(self, volume):
        """
        Makes a volume unavailable to the host

        returns None on success, else raises LsmError on errors.
        """
        return self.tp.rpc('volume_offline', del_self(locals()))

    ## Access control for allowing an access group to access a volume
    # @param    self    The this pointer
    # @param    group   The access group
    # @param    volume  The volume to grant access to
    # @param    access  The desired access
    # @returns  None on success, else job id
    def access_group_grant(self, group, volume, access):
        """
        Allows an access group to access a volume.
        """
        return self.tp.rpc('access_group_grant', del_self(locals()))

    ## Revokes privileges an initiator has to a volume
    # @param    self    The this pointer
    # @param    initiator   The initiator object
    # @param    volume  The volume object
    # @returns  None on success, else job id
    def access_revoke(self, initiator, volume):
        """
        Revokes privileges an initiator has to a volume

        Returns none on success, else raises a LsmError on error.
        """
        return self.tp.rpc('access_revoke', del_self(locals()))

    ## Revokes access to a volume to initiators in an access group
    # @param    self    The this pointer
    # @param    group   The access group
    # @param    volume  The volume to grant access to
    # @returns  None on success, else job id
    def access_group_revoke(self, group, volume):
        """
        Revokes access for an access group for a volume
        """
        return self.tp.rpc('access_group_revoke', del_self(locals()))

    ## Returns a list of access group objects
    # @param    self    The this pointer
    # @returns  List of access groups
    def access_group_list(self):
        """
        Returns a list of access groups
        """
        return self.tp.rpc('access_group_list', del_self(locals()))

    ## Creates an access a group with the specified initiator in it.
    # @param    self                The this pointer
    # @param    name                The initiator group name
    # @param    initiator_id        Initiator id
    # @param    id_type             Type of initiator (Enumeration)
    # @param    system_id           Which system to create this group on
    # @returns AccessGroup on success, else raises LsmError
    def access_group_create(self, name, initiator_id, id_type, system_id):
        """
        Creates an access group and add the specified initiator id, id_type and
        desired access.
        """
        return self.tp.rpc('access_group_create', del_self(locals()))

    ## Deletes an access group.
    # @param    self    The this pointer
    # @param    group   The access group to delete
    # @returns  None on success, else job id
    def access_group_del(self, group):
        """
        Deletes an access group
        """
        return self.tp.rpc('access_group_del', del_self(locals()))

    ## Adds an initiator to an access group
    # @param    self            The this pointer
    # @param    group           Group to add initiator to
    # @param    initiator_id    Initiators id
    # @param    id_type         Initiator id type (enumeration)
    # @returns  None on success, else job id
    def access_group_add_initiator(self, group, initiator_id, id_type):
        """
        Adds an initiator to an access group
        """
        return self.tp.rpc('access_group_add_initiator', del_self(locals()))

    ## Deletes an initiator from an access group
    # @param    self        The this pointer
    # @param    group       The access group to remove initiator from
    # @param    initiator   The initiator to remove from the group
    # @returns  None on success, else job id
    def access_group_del_initiator(self, group, initiator):
        """
        Deletes an initiator from an access group
        """
        return self.tp.rpc('access_group_del_initiator', del_self(locals()))

    ## Returns the list of volumes that access group has access to.
    # @param    self        The this pointer
    # @param    group       The access group to list volumes for
    # @returns list of volumes
    def volumes_accessible_by_access_group(self, group):
        """
        Returns the list of volumes that access group has access to.
        """
        return self.tp.rpc('volumes_accessible_by_access_group',
                            del_self(locals()))

    ##Returns the list of access groups that have access to the specified
    #volume.
    # @param    self        The this pointer
    # @param    volume      The volume to list access groups for
    # @returns  list of access groups
    def access_groups_granted_to_volume(self, volume):
        """
        Returns the list of access groups that have access to the specified
        volume.
        """
        return self.tp.rpc('access_groups_granted_to_volume',
            del_self(locals()))

    ## Checks to see if a volume has child dependencies.
    # @param    self    The this pointer
    # @param    volume  The volume to check
    # @returns True or False
    def volume_child_dependency(self, volume):
        """
        Returns True if this volume has other volumes which are dependant on it.
        Implies that this volume cannot be deleted or possibly modified because
        it would affect its children.
        """
        return self.tp.rpc('volume_child_dependency', del_self(locals()))

    ## Removes any child dependency.
    # @param    self    The this pointer
    # @param    volume  The volume to remove dependencies for
    # @returns None if complete, else job id.
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
        return self.tp.rpc('volume_child_dependency_rm', del_self(locals()))

    ## Returns a list of file system objects.
    # @param    self    The this pointer
    # @returns A list of FS objects.
    def fs(self):
        """
        Returns a list of file systems on the controller.
        """
        return self.tp.rpc('fs', del_self(locals()))

    ## Deletes a file system
    # @param    self    The this pointer
    # @param    fs      The file system to delete
    # @returns  None on success, else job id
    def fs_delete(self, fs):
        """
        WARNING: Destructive

        Deletes a file system and everything it contains
        Returns None on success, else job id
        """
        return self.tp.rpc('fs_delete', del_self(locals()))

    ## Re-sizes a file system
    # @param    self    The this pointer
    # @param    fs      The file system to re-size
    # @param    new_size_bytes  The new size of the file system in bytes
    # @returns tuple (job_id, re-sized file system),
    # When one is None the other is valid
    def fs_resize(self, fs, new_size_bytes):
        """
        Re-size a file system

        Returns a tuple (job_id, re-sized file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('fs_resize', del_self(locals()))

    ## Creates a file system.
    # @param    self    The this pointer
    # @param    pool    The pool object to allocate space from
    # @param    name    The human text name for the file system
    # @param    size_bytes  The size of the file system in bytes
    # @returns  tuple (job_id, file system),
    # When one is None the other is valid
    def fs_create(self, pool, name, size_bytes):
        """
        Creates a file system given a pool, name and size.
        Note: size is limited to 2**64 bytes

        Returns a tuple (job_id, file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('fs_create', del_self(locals()))

    ## Clones a file system
    # @param    self    The this pointer
    # @param    src_fs  The source file system to clone
    # @param    dest_fs_name    The destination file system clone name
    # @param    snapshot    Optional, create clone from previous snapshot
    # @returns tuple (job_id, file system)
    def fs_clone(self, src_fs, dest_fs_name, snapshot=None):
        """
        Creates a thin, point in time read/writable copy of src to dest.
        Optionally uses snapshot as backing of src_fs

        Returns a tuple (job_id, file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('fs_clone', del_self(locals()))

    ## Clones an individial file or files on the specified file system
    # @param    self    The this pointer
    # @param    fs      The file system the files are on
    # @param    src_file_name   The source file name
    # @param    dest_file_name  The dest. file name
    # @param    snapshot    Optional, the snapshot to base clone source file from
    # @returns  None on success, else job id
    def file_clone(self, fs, src_file_name, dest_file_name, snapshot=None):
        """
        Creates a thinly provisioned clone of src to dest.
        Note: Source and Destination are required to be on same filesystem and
        all directories in destination path need to exist.

        Returns None on success, else job id
        """
        return self.tp.rpc('file_clone', del_self(locals()))

    ## Returns a list of snapshots
    # @param    self    The this pointer
    # @param    fs      The file system
    # @returns  a list of snapshot objects.
    def snapshots(self, fs):
        """
        Returns a list of snapshot names for the supplied file system
        """
        return self.tp.rpc('snapshots', del_self(locals()))

    ## Creates a snapshot (Point in time read only copy)
    # @param    self    The this pointer
    # @param    fs      The file system to snapshot
    # @param    snapshot_name   The human readable snapshot name
    # @param    files   The list of specific files to snapshot.
    # @returns Snapshot that was created.
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

        Returns a tuple (job_id, snapshot)
        Notes:
        - Snapshot name may not match what was passed in
          (depends on array implementation)
        - Tuple return values are mutually exclusive, when one
          is None the other must be valid.
        """
        return self.tp.rpc('snapshot_create', del_self(locals()))

    ## Deletes a snapshot
    # @param    self    The this pointer
    # @param    fs      The filesystem the snapshot it for
    # @param    snapshot    The specific snap shot to delete
    # @returns  None on success, else job id
    def snapshot_delete(self, fs, snapshot):
        """
        Frees the re-sources for the given snapshot on the supplied filesystem.

        Returns None on success else job id, LsmError exception on error
        """
        return self.tp.rpc('snapshot_delete', del_self(locals()))

    ## Reverts a snapshot
    # @param    self        The this pointer
    # @param    fs          The file system object to revert snapthot for
    # @param    snapshot    The snapshot file to revert back too
    # @param    files       The specific files to revert.
    # @param    all_files   Set to True if all files should be reverted back.
    def snapshot_revert(self, fs, snapshot, files, restore_files, all_files=False):
        """
        WARNING: Destructive!

        Reverts a file-system or just the specified files from the snapshot.  If
        a list of files is supplied but the array cannot restore just them then
        the operation will fail with an LsmError raised.  If files == None and
        all_files = True then all files on the file-system are reverted.

        Restore_file if None none must be the same length as files with each
        index in each list referring to the associated file.

        Returns None on success, else job id, LsmError exception on error
        """
        return self.tp.rpc('snapshot_revert', del_self(locals()))

    ## Checks to see if a file system has child dependencies.
    # @param    fs      The file system to check
    # @param    file    The files to check (optional)
    # @returns True or False
    def fs_child_dependency(self, fs, files=None):
        """
        Returns True if the specified filesystem or specified file on this
        file system has child dependencies.  This implies that this filesystem
        or specified file on this file system cannot be deleted or possibly
        modified because it would affect its children.
        """
        return self.tp.rpc('fs_child_dependency', del_self(locals()))

    ## Removes child dependencies from a FS or specific file.
    # @param    self    The this pointer
    # @param    fs      The file system to remove child dependencies for
    # @param    file    The files to remove child dependencies for (optional)
    # @returns None if complete, else job id.
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
        return self.tp.rpc('fs_child_dependency_rm', del_self(locals()))

    ## Returns a list of all the NFS client authentication types.
    # @param    self    The this pointer
    # @returns  An array of client authentication types.
    def export_auth(self):
        """
        What types of NFS client authentication are supported.
        """
        return self.tp.rpc('export_auth', del_self(locals()))

    ## Returns a list of all the exported file systems
    # @param    self    The this pointer
    # @returns An array of export objects
    def exports(self):
        """
        Get a list of all exported file systems on the controller.
        """
        return self.tp.rpc('exports', del_self(locals()))

    ## Exports a FS as specified in the export.
    # @param    self    The this pointer
    # @param    export  The export
    # @returns NfsExport on success, else raises LsmError
    def export_fs(self, export):
        """
        Exports a filesystem as specified in the export
        """
        return self.tp.rpc('export_fs', del_self(locals()))

    ## Removes the specified export
    # @param    self    The this pointer
    # @param    export  The export to remove
    # @returns None on success, else raises LsmError
    def export_remove(self, export):
        """
        Removes the specified export
        """
        return self.tp.rpc('export_remove', del_self(locals()))


class TestClient(unittest.TestCase):
    def wait_to_finish(self, job, vol):

        if vol is not None:
            return vol
        else:
            (status, percent, volume) = self.c.job_status(job)
            print 'Job status:', status, ' percent complete=', percent

            while status == common.JobStatus.INPROGRESS:
                time.sleep(1)
                (status, percent, volume) = self.c.job_status(job)
                print 'Job status:', status, ' percent complete=', percent

            self.c.job_free(job)

            if status == common.JobStatus.COMPLETE:
                self.assertTrue(volume is not None)

        return volume


    def setUp(self):
        #Most of the uri is not needed for the simulator
        #Remember that the setup and teardown methods are run for each test
        #case!
        self.c = Client('sim://username@host:5988/?namespace=root/foo')

    def test_tmo(self):
        expected = 40000

        self.c.set_time_out(expected)
        tmo = self.c.get_time_out()
        self.assertTrue(tmo == expected)

    def test_job_errors(self):
        self.assertRaises(common.LsmError, self.c.job_free, 0)
        self.assertRaises(common.LsmError, self.c.job_status, 0)

    def test_pools(self):
        self.pools = self.c.pools()

        self.assertTrue(len(self.pools) == 2)

        for p in self.pools:
            print p

    def test_volumes(self):
        volumes = self.c.volumes()
        self.assertTrue(len(volumes) == 0)

        pools = self.c.pools()

        #create a volume
        p = pools[0]

        #Create volumes
        num_volumes = 10
        for i in range(num_volumes):
            vol = self.wait_to_finish( *(self.c.volume_create(p,
                                        "TestVol" + str(i), 1024*1024*10,
                                        Volume.PROVISION_DEFAULT)))
            print str(vol)

        volumes = self.c.volumes()
        self.assertTrue(len(volumes) == num_volumes)

        #delete volumes
        for i in volumes:
            self.c.volume_delete(i)

        volumes = self.c.volumes()
        self.assertTrue(len(volumes) == 0)

        #Create a volume and replicate it
        vol = self.wait_to_finish( *(self.c.volume_create(p,
                                    "To be replicated", 1024*1024*10,
                                    Volume.PROVISION_DEFAULT)))
        rep = self.wait_to_finish(*(self.c.volume_replicate(p,
                                    Volume.REPLICATE_CLONE, vol,
                                    'Replicated')))

        volumes = self.c.volumes()
        self.assertTrue(len(volumes) == 2)

        self.c.volume_delete(rep)

        re_sized = self.wait_to_finish(*(self.c.volume_resize(vol, vol.size_bytes * 2)))

        self.assertTrue(vol.size_bytes == re_sized.size_bytes/2)

        self.c.volume_offline(re_sized)
        self.c.volume_online(re_sized)


    def tearDown(self):
        self.c.close()

if __name__ == "__main__":
    unittest.main()
