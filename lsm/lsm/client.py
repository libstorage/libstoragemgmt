#!/usr/bin/env	python

# Copyright (C) 2011 Red Hat, Inc.
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
import sys
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

        self.plugin_path = self.uds_path + '/' + u.scheme

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
        Returns the stats of the given job number.

        Returns a tuple ( status (enumeration), percent_complete, volume).
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
        Returns an array of pool objects
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

        Returns None on success, else raises an LsmError
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


        self.c.access_grant(i, vol, Volume.ACCESS_READ_WRITE)
        self.c.access_revoke(i,vol)
        self.assertRaises(common.LsmError, self.c.access_revoke, i, vol)


    def tearDown(self):
        self.c.close()

if __name__ == "__main__":
    unittest.main()