#!/usr/bin/env	python

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
from data import Volume, Initiator
from transport import Transport
import common

def del_self(d):
    """
    Used to remove the self key from the dict d.  Self is included when calling
    the function locals() in a class method.
    """
    del d['self']
    return d

# ** IMPORTANT **
# Theory of operation for methods in this class.
# We are using the name of the method and the name of the parameters and
# using python introspection abilities to translate them to the method and
# parameter names.  Makes the code compact, but you will break things if the
# IPlugin class does not match the method names and parameters here!
class Client(object):
    """
    Client side class used for managing storage.
    """
    def __start(self, uri, plain_text_password, timeout_ms):
        """
        Instruct the plug-in to get ready
        """
        
        self.tp.send_req('startup',
                {'uri': uri, 'password': plain_text_password,
                 'timeout': timeout_ms})
        self.tp.read_resp()

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

    def close(self):
        """
        Does an orderly shutdown of the plugin
        """
        self.tp.rpc('shutdown', None)
        self.tp.close()
        self.tp = None

    def set_time_out(self, ms):
        """
        Sets any time-outs for the plug-in (ms)

        Return None on success, else LsmError exception
        """
        return self.tp.rpc('set_time_out', del_self(locals()))

    def get_time_out(self):
        """
        Retrieves the current time-out

        Return time-out in ms, else raise LsmError
        """
        return self.tp.rpc('get_time_out', del_self(locals()))

    def job_status(self, job_number):
        """
        Returns the stats of the given job.

        Returns a tuple ( status (enumeration), percent_complete, completed item).
        else LsmError exception.
        """
        return self.tp.rpc('job_status', del_self(locals()))

    def job_free(self, job_number):
        """
        Frees resources for a given job number.

        Returns None on success, else raises an LsmError
        """
        return self.tp.rpc('job_free', del_self(locals()))

    def capabilities(self):
        raise NotImplemented()

    def pools(self):
        """
        Returns an array of pool objects.  Pools are used in both block and
        file system interfaces, thus the reason they are in the base class.
        """
        return self.tp.rpc('pools', del_self(locals()))

    def initiators(self):
        """
        Return an array of initiator objects
        """
        return self.tp.rpc('initiators', del_self(locals()))

    def volumes(self):
        """
        Returns an array of volume objects
        """
        return self.tp.rpc('volumes', del_self(locals()))

    def volume_create(self, pool, volume_name, size_bytes, provisioning):
        """
        Creates a volume, given a pool, volume name, size and provisioning

        returns a tuple (job_id, new volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('volume_create', del_self(locals()))

    def volume_resize(self, volume, new_size_bytes):
        """
        Re-sizes a volume.

        Returns a tuple (job_id, re-sized_volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('volume_resize', del_self(locals()))

    def volume_replicate(self, pool, rep_type, volume_src, name):
        """
        Replicates a volume from the specified pool.

        Returns a tuple (job_id, replicated volume)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('volume_replicate', del_self(locals()))

    def volume_delete(self, volume):
        """
        Deletes a volume.

        Returns None on success, else job id
        """
        return self.tp.rpc('volume_delete', del_self(locals()))

    def volume_online(self, volume):
        """
        Makes a volume available to the host

        returns None on success, else raises LsmError on errors.
        """
        return self.tp.rpc('volume_online', del_self(locals()))

    def volume_offline(self, volume):
        """
        Makes a volume unavailable to the host

        returns None on success, else raises LsmError on errors.
        """
        return self.tp.rpc('volume_offline', del_self(locals()))

    def initiator_create(self, name, id, id_type):
        """
        Creates an initiator to be used for granting access to volumes.

        Returns an initiator object, else raises an LsmError
        """
        return self.tp.rpc('initiator_create', del_self(locals()))

    def initiator_delete(self, initiator):
        """
        Deletes an initiator record.

        Returns none on success, else raises LsmError
        """
        return self.tp.rpc('initiator_delete', del_self(locals()))

    def access_grant(self, initiator, volume, access):
        """
        Access control for allowing an initiator to use a volume.

        Returns None on success else job id if in progress.
        Raises LsmError on errors.
        """
        return self.tp.rpc('access_grant', del_self(locals()))

    def access_revoke(self, initiator, volume):
        """
        Revokes privileges an initiator has to a volume

        Returns none on success, else raises a LsmError on error.
        """
        return self.tp.rpc('access_revoke', del_self(locals()))

    def access_group_list(self):
        raise NotImplemented()

    def access_group_create(self):
        raise NotImplemented()

    def access_group_del(self, group):
        assert group is not None
        raise NotImplemented()

    def access_group_add_initiator(self, group, initiator, access):
        assert group is not None
        assert initiator is not None
        assert access is not None
        raise NotImplemented()

    def access_group_del_initiator(self, group, initiator):
        assert group is not None
        assert initiator is not None
        raise NotImplemented()

    def fs(self):
        """
        Returns a list of file systems on the controller.
        """
        return self.tp.rpc('fs', del_self(locals()))

    def fs_delete(self, fs):
        """
        WARNING: Destructive

        Deletes a file system and everything it contains
        Returns None on success, else job id
        """
        return self.tp.rpc('fs_delete', del_self(locals()))

    def fs_resize(self, fs, new_size_bytes):
        """
        Re-size a file system

        Returns a tuple (job_id, re-sized file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('fs_resize', del_self(locals()))

    def fs_create(self, pool, name, size_bytes):
        """
        Creates a file system given a pool, name and size.
        Note: size is limited to 2**64 bytes

        Returns a tuple (job_id, file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('fs_create', del_self(locals()))

    def fs_clone(self, src_fs, dest_fs_name, snapshot=None):
        """
        Creates a thin, point in time read/writable copy of src to dest.
        Optionally uses snapshot as backing of src_fs

        Returns a tuple (job_id, file system)
        Note: Tuple return values are mutually exclusive, when one
        is None the other must be valid.
        """
        return self.tp.rpc('fs_clone', del_self(locals()))

    def file_clone(self, fs, src_file_name, dest_file_name, snapshot=None):
        """
        Creates a thinly provisioned clone of src to dest.
        Note: Source and Destination are required to be on same filesystem and
        all directories in destination path need to exist.

        Returns None on success, else job id
        """
        return self.tp.rpc('file_clone', del_self(locals()))

    def snapshots(self, fs):
        """
        Returns a list of snapshot names for the supplied file system
        """
        return self.tp.rpc('snapshots', del_self(locals()))

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
        return self.tp.rpc('snapshot_create', del_self(locals()))

    def snapshot_delete(self, fs, snapshot):
        """
        Frees the re-sources for the given snapshot on the supplied filesystem.

        Returns None on success else job id, LsmError exception on error
        """
        return self.tp.rpc('snapshot_delete', del_self(locals()))

    def snapshot_revert(self, fs, snapshot, files, all_files=False):
        """
        WARNING: Destructive!

        Reverts a file-system or just the specified files from the snapshot.  If
        a list of files is supplied but the array cannot restore just them then
        the operation will fail with an LsmError raised.  If files == None and
        all_files = True then all files on the file-system are reverted.

        Returns None on success, else job id, LsmError exception on error
        """
        assert(fs is not None)
        assert(snapshot is not None)
        assert(files is None or type(files) is list)
        assert(type(all_files) is bool)
        raise NotImplemented()

    def export_auth(self):
        """
        What types of NFS client authentication are supported.
        """
        return self.tp.rpc('export_auth', del_self(locals()))

    def exports(self):
        """
        Get a list of all exported file systems on the controller.
        """
        return self.tp.rpc('exports', del_self(locals()))

    def export_fs(self, export):
        """
        Exports a filesystem as specified in the export
        """
        return self.tp.rpc('export_fs', del_self(locals()))

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


    def test_initiators(self):
        init = self.c.initiators()
        self.assertTrue(len(init) == 0)

        pools = self.c.pools()
        vol = self.wait_to_finish( *(self.c.volume_create(pools[0],
            "TestMap", 1024*1024*10,
            Volume.PROVISION_DEFAULT)))


        i = self.c.initiator_create('Test initiator',
                                    '47E348D52647842ABB7897B36CA23A91',
                                    Initiator.TYPE_ISCSI)

        self.assertRaises(common.LsmError, self.c.initiator_create,
                                    'Test initiator',
                                    '47E348D52647842ABB7897B36CA23A91',
                                    Initiator.TYPE_ISCSI)

        init = self.c.initiators()
        self.assertTrue(len(init) == 1)

        self.c.access_grant(i, vol, Volume.ACCESS_READ_WRITE)
        self.c.access_revoke(i,vol)
        self.assertRaises(common.LsmError, self.c.access_revoke, i, vol)


    def tearDown(self):
        self.c.close()

if __name__ == "__main__":
    unittest.main()