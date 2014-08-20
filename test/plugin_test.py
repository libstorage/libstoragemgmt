#!/bin/env python2

# Copyright (C) 2013-2014 Red Hat, Inc.
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

import lsm
import functools
import time
import random
import string
import traceback
import unittest
import argparse
import collections
import atexit
import sys
import yaml
import re
from lsm import LsmError, ErrorNumber

results = {}
stats = {}


MIN_POOL_SIZE = 4096
MIN_OBJECT_SIZE = 512


def mb_in_bytes(mib):
    return 1024 * 1024 * mib


def record_result(method):
    def recorder(*args, **kwargs):
        try:
            result = method(*args, **kwargs)
            results[method.__name__] = dict(rc=True, msg=None)
            return result
        except Exception as e:
            results[method.__name__] = dict(rc=False,
                                            stack_trace=traceback.format_exc(),
                                            msg=str(e))
    return recorder


def update_stats(method_name, duration, number_results):
    if method_name in stats:
        stats[method_name]["count"] += 1
    else:
        stats[method_name] = dict(count=1, total_time=0, number_items=0)

    stats[method_name]["total_time"] += duration

    if number_results > 0:
        stats[method_name]["number_items"] += number_results


def rs(component, l=4):
    """
    Generate a random string
    """
    return 'lsm_%s_' % component + ''.join(
        random.choice(string.ascii_uppercase) for x in range(l))


def r_fcpn():
    """
    Generate a random 16 character hex number
    """
    rnd_fcpn = '%016x' % random.randrange(2 ** 64)
    return ':'.join(rnd_fcpn[i:i + 2] for i in range(0, len(rnd_fcpn), 2))


class Duration(object):
    def __init__(self):
        self.start = 0
        self.end = 0

    def __enter__(self):
        self.start = time.time()
        return self

    def __exit__(self, *ignore):
        self.end = time.time()

    def amount(self):
        return self.end - self.start


def supported(cap, capability):
    for c in capability:
        if not cap.supported(c):
            return False
    return True


class TestProxy(object):

    # Hash of all calls that can be async
    async_calls = {'volume_create': (unicode, lsm.Volume),
                   'volume_resize': (unicode, lsm.Volume),
                   'volume_replicate': (unicode, lsm.Volume),
                   'volume_replicate_range': (unicode,),
                   'volume_delete': (unicode,),
                   'volume_child_dependency_rm': (unicode),
                   'fs_delete': (unicode,),
                   'fs_resize': (unicode, lsm.FileSystem),
                   'fs_create': (unicode, lsm.FileSystem),
                   'fs_clone': (unicode, lsm.FileSystem),
                   'fs_file_clone': (unicode,),
                   'fs_snapshot_create': (unicode, lsm.FsSnapshot),
                   'fs_snapshot_delete': (unicode,),
                   'fs_snapshot_restore': (unicode,),
                   'fs_child_dependency_rm': (unicode,)}

    ## The constructor.
    # @param    self    The object self
    # @param    obj     The object instance to wrap
    def __init__(self, obj=None):
        """
        Constructor which takes an object to wrap.
        """
        self.o = obj

    ## Called each time an attribute is requested of the object
    # @param    self    The object self
    # @param    name    Name of the attribute being accessed
    # @return   The result of the method
    def __getattr__(self, name):
        """
        Called each time an attribute is requested of the object
        """
        if hasattr(self.o, name):
            return functools.partial(self.present, name)
        else:
            raise AttributeError("No such method %s" % name)

    @staticmethod
    def log_result(method, v):
        if not method in results:
            results[method] = []

        results[method].append(v)

    ## Method which is called to invoke the actual method of interest.
    #
    # The intentions of this method is this:
    # - Invoke the method just like it normally would without this
    #   so signature in & out is identical
    # - Collect results of the method call
    # - Collect stats on the execution time of call
    #
    # @param    self                The object self
    # @param    _proxy_method_name  Method to invoke
    # @param    args                Arguments
    # @param    kwargs              Keyword arguments
    # @return   The result of the method invocation
    def present(self, _proxy_method_name, *args, **kwargs):
        """
        Method which is called to invoke the actual method of interest.
        """
        rc = None
        job_possible = _proxy_method_name in TestProxy.async_calls

        # Timer block
        with Duration() as method_time:
            try:
                rc = getattr(self.o, _proxy_method_name)(*args, **kwargs)
                TestProxy.log_result(_proxy_method_name,
                                     dict(rc=True, stack_trace=None, msg=None))
            except lsm.LsmError as le:
                if le.code != lsm.ErrorNumber.NO_SUPPORT and \
                        le.code != lsm.ErrorNumber.NAME_CONFLICT:
                    TestProxy.log_result(
                        _proxy_method_name,
                        dict(rc=False,
                             stack_trace=traceback.format_exc(),
                             msg=str(le)))
                raise

            # If the job can do async, we will block looping on it.
            if job_possible and rc is not None:
                # Note: Some return a single unicode or None,
                #       others return a tuple (job, object)
                if type(rc) != tuple and type(rc) != list:
                    rc = (rc, None)
                rc = self.wait_for_it(_proxy_method_name, *rc)

        # Fix up return value to match what it would normally be
        if job_possible:
            if 2 == len(TestProxy.async_calls[_proxy_method_name]):
                rc = (None, rc)

        # We don't care about time per operation when there is only one
        # possible.
        if not job_possible and isinstance(rc, collections.Sequence) \
                and len(rc) > 2:
            num_results = len(rc)
        else:
            num_results = 0

        update_stats(_proxy_method_name, method_time.amount(), num_results)
        return rc

    def wait_for_it(self, msg, job, item):
        if not job:
            return item
        else:
            while True:
                (s, percent, i) = self.job_status(job)

                if s == lsm.JobStatus.INPROGRESS:
                    time.sleep(0.25)
                elif s == lsm.JobStatus.COMPLETE:
                    self.job_free(job)
                    return i
                else:
                    raise Exception(msg + " job error code= " + str(s))


def check_type(value, *expected):
    assert type(value) in expected, "type expected (%s), type actual (%s)" % \
                                    (str(type(value)), str(expected))


class TestPlugin(unittest.TestCase):
    """
    Anything that starts with test_ will be run as a separate unit test with
    the setUp and tearDown methods called before and after respectively
    """

    URI = 'sim://'
    PASSWORD = None

    def _object_size(self, pool):
        return mb_in_bytes(MIN_OBJECT_SIZE)

    def setUp(self):
        self.c = TestProxy(lsm.Client(TestPlugin.URI, TestPlugin.PASSWORD))

        self.systems = self.c.systems()
        self.pools = self.c.pools()

        self.pool_by_sys_id = {}

        for s in self.systems:
            self.pool_by_sys_id[s.id] = [p for p in self.pools if
                                         p.system_id == s.id]

        # TODO Store what exists, so that we don't remove it

    def _get_pool_by_usage(self, system_id, element_type):
        largest_free = 0
        rc = None

        for p in self.pool_by_sys_id[system_id]:
            # If the pool matches our criteria and min size we will consider
            # it, but we will select the one with the most free space for
            # testing.
            if p.element_type & element_type and \
                    p.free_space > mb_in_bytes(MIN_POOL_SIZE):
                if p.free_space > largest_free:
                    largest_free = p.free_space
                    rc = p
        return rc

    def tearDown(self):
        # TODO Walk the array looking for stuff we have created and remove it
        # What should we do if an array supports a create operation, but not
        # the corresponding remove?
        self.c.close()

    def test_plugin_info(self):
        (desc, version) = self.c.plugin_info()
        self.assertTrue(desc is not None and len(desc) > 0)
        self.assertTrue(version is not None and len(version) > 0)

    def test_timeout(self):
        tmo = 40000
        self.c.time_out_set(tmo)
        self.assertEquals(self.c.time_out_get(), tmo)

    def test_systems_list(self):
        arrays = self.c.systems()
        self.assertTrue(len(arrays) > 0, "We need at least one array for "
                                         "testing!")

    def test_pools_list(self):
        pools_list = self.c.pools()
        self.assertTrue(len(pools_list) > 0, "We need at least 1 pool to test")

    @staticmethod
    def _vpd_correct(vpd):
        p = re.compile('^[a-fA-F0-9]+$')

        if vpd is not None and len(vpd) > 0 and p.match(vpd) is not None:
            return True
        return False

    def test_volume_list(self):
        volumes = self.c.volumes()

        for v in volumes:
            self.assertTrue(TestPlugin._vpd_correct(v.vpd83),
                            "VPD is not as expected %s for volume id: %s" %
                            (v.vpd83, v.id))

        self.assertTrue(len(volumes) > 0, "We need at least 1 volume to test")

    def test_disks_list(self):
        disks = self.c.disks()
        self.assertTrue(len(disks) > 0, "We need at least 1 disk to test")

    def _volume_create(self, system_id):
        if system_id in self.pool_by_sys_id:
            p = self._get_pool_by_usage(system_id,
                                        lsm.Pool.ELEMENT_TYPE_VOLUME)

            self.assertTrue(p is not None, "Unable to find a suitable pool")

            if p:
                vol_size = self._object_size(p)

                vol = self.c.volume_create(p, rs('v'), vol_size,
                                           lsm.Volume.PROVISION_DEFAULT)[1]

                self.assertTrue(self._volume_exists(vol.id), p.id)
                self.assertTrue(vol.pool_id == p.id)
                return vol, p

    def _fs_create(self, system_id):
        if system_id in self.pool_by_sys_id:
            pool = self._get_pool_by_usage(system_id,
                                            lsm.Pool.ELEMENT_TYPE_FS)

            fs_size = self._object_size(pool)
            fs = self.c.fs_create(pool, rs('fs'), fs_size)[1]
            self.assertTrue(self._fs_exists(fs.id))

            self.assertTrue(fs is not None)
            self.assertTrue(pool is not None)
            return fs, pool

    def _volume_delete(self, volume):
        self.c.volume_delete(volume)
        self.assertFalse(self._volume_exists(volume.id))

    def _fs_delete(self, fs):
        self.c.fs_delete(fs)
        self.assertFalse(self._fs_exists(fs.id))

    def _fs_snapshot_delete(self, fs, ss):
        self.c.fs_snapshot_delete(fs, ss)
        self.assertFalse(self._fs_snapshot_exists(fs, ss.id))

    def _volume_exists(self, volume_id, pool_id=None):
        volumes = self.c.volumes()

        for v in volumes:
            if v.id == volume_id:
                if pool_id is not None:
                    if v.pool_id == pool_id:
                        return True
                    else:
                        return False

                return True

        return False

    def _fs_exists(self, fs_id):
        fs = self.c.fs()

        for f in fs:
            if f.id == fs_id:
                return True

        return False

    def _fs_snapshot_exists(self, fs, ss_id):
        snapshots = self.c.fs_snapshots(fs)

        for s in snapshots:
            if s.id == ss_id:
                return True

        return False

    def test_volume_create_delete(self):
        if self.pool_by_sys_id:
            for s in self.systems:
                vol = None
                cap = self.c.capabilities(s)
                if supported(cap, [lsm.Capabilities.VOLUME_CREATE]):
                    vol = self._volume_create(s.id)[0]
                    self.assertTrue(vol is not None)

                    if vol is not None and \
                            supported(cap, [lsm.Capabilities.VOLUME_DELETE]):
                        self._volume_delete(vol)

    def test_volume_resize(self):
        if self.pool_by_sys_id:
            for s in self.systems:
                cap = self.c.capabilities(s)

                if supported(cap, [lsm.Capabilities.VOLUME_CREATE,
                                   lsm.Capabilities.VOLUME_DELETE,
                                   lsm.Capabilities.VOLUME_RESIZE]):
                    vol = self._volume_create(s.id)[0]
                    vol_resize = self.c.volume_resize(
                        vol, int(vol.size_bytes * 1.10))[1]
                    self.assertTrue(vol.size_bytes < vol_resize.size_bytes)
                    self.assertTrue(vol.id == vol_resize.id,
                                    "Expecting re-sized volume to refer to "
                                    "same volume.  Expected %s, got %s" %
                                    (vol.id, vol_resize.id))
                    if vol.id == vol_resize.id:
                        self._volume_delete(vol_resize)
                    else:
                        # Delete the original
                        self._volume_delete(vol)

    def _replicate_test(self, capability, replication_type):
        if self.pool_by_sys_id:
            for s in self.systems:
                cap = self.c.capabilities(s)

                if supported(cap, [lsm.Capabilities.VOLUME_CREATE,
                                   lsm.Capabilities.VOLUME_DELETE]):
                    vol, pool = self._volume_create(s.id)

                    # For the moment lets allow the array to pick the pool
                    # to supply the backing store for the replicate
                    if supported(cap, [capability]):
                        volume_clone = self.c.volume_replicate(
                            None, replication_type, vol,
                            rs('v_c_'))[1]

                        self.assertTrue(volume_clone is not None)
                        self.assertTrue(self._volume_exists(volume_clone.id))

                        if volume_clone is not None:
                            # Lets test for creating a clone with an
                            # existing name
                            try:
                                volume_clone_dupe_name = \
                                    self.c.volume_replicate(
                                        None, replication_type, vol,
                                        volume_clone.name)[1]
                            except LsmError as le:
                                self.assertTrue(le.code ==
                                                ErrorNumber.NAME_CONFLICT)

                        self._volume_delete(volume_clone)

                    self._volume_delete(vol)

    def test_volume_replication(self):
        self._replicate_test(lsm.Capabilities.VOLUME_REPLICATE_CLONE,
                             lsm.Volume.REPLICATE_CLONE)

        self._replicate_test(lsm.Capabilities.VOLUME_REPLICATE_COPY,
                             lsm.Volume.REPLICATE_COPY)

        self._replicate_test(lsm.Capabilities.VOLUME_REPLICATE_MIRROR_ASYNC,
                             lsm.Volume.REPLICATE_MIRROR_ASYNC)

        self._replicate_test(lsm.Capabilities.VOLUME_REPLICATE_MIRROR_SYNC,
                             lsm.Volume.REPLICATE_MIRROR_SYNC)

    def test_volume_replicate_range_block_size(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [lsm.Capabilities.VOLUME_COPY_RANGE_BLOCK_SIZE]):
                size = self.c.volume_replicate_range_block_size(s)
                self.assertTrue(size > 0)
            else:
                self.assertRaises(lsm.LsmError,
                                  self.c.volume_replicate_range_block_size, s)

    def test_replication_range(self):
        if self.pool_by_sys_id:
            for s in self.systems:
                cap = self.c.capabilities(s)

                if supported(cap,
                             [lsm.Capabilities.VOLUME_COPY_RANGE_BLOCK_SIZE,
                             lsm.Capabilities.VOLUME_CREATE,
                             lsm.Capabilities.VOLUME_DELETE,
                             lsm.Capabilities.VOLUME_COPY_RANGE]):

                    size = self.c.volume_replicate_range_block_size(s)

                    vol, pool = self._volume_create(s.id)

                    br = lsm.BlockRange(0, size, size)

                    if supported(
                            cap, [lsm.Capabilities.VOLUME_COPY_RANGE_CLONE]):
                        self.c.volume_replicate_range(
                            lsm.Volume.REPLICATE_CLONE, vol, vol, [br])
                    else:
                        self.assertRaises(
                            lsm.LsmError,
                            self.c.volume_replicate_range,
                            lsm.Volume.REPLICATE_CLONE, vol, vol, [br])

                    br = lsm.BlockRange(size * 2, size, size)

                    if supported(
                            cap, [lsm.Capabilities.VOLUME_COPY_RANGE_COPY]):
                        self.c.volume_replicate_range(
                            lsm.Volume.REPLICATE_COPY, vol, vol, [br])
                    else:
                        self.assertRaises(
                            lsm.LsmError,
                            self.c.volume_replicate_range,
                            lsm.Volume.REPLICATE_COPY, vol, vol, [br])

                    self._volume_delete(vol)

    def test_fs_creation_deletion(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [lsm.Capabilities.FS_CREATE]):
                fs = self._fs_create(s.id)[0]

                if supported(cap, [lsm.Capabilities.FS_DELETE]):
                    self._fs_delete(fs)

    def test_fs_resize(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [lsm.Capabilities.FS_CREATE]):
                fs = self._fs_create(s.id)[0]

                if supported(cap, [lsm.Capabilities.FS_RESIZE]):
                    fs_size = fs.total_space * 1.10
                    fs_resized = self.c.fs_resize(fs, fs_size)[1]
                    self.assertTrue(fs_resized.total_space)

                if supported(cap, [lsm.Capabilities.FS_DELETE]):
                    self._fs_delete(fs)

    def test_fs_clone(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [lsm.Capabilities.FS_CREATE,
                               lsm.Capabilities.FS_CLONE]):
                fs = self._fs_create(s.id)[0]
                fs_clone = self.c.fs_clone(fs, rs('fs_c'))[1]

                if supported(cap, [lsm.Capabilities.FS_DELETE]):
                    self._fs_delete(fs_clone)
                    self._fs_delete(fs)

    def test_fs_snapshot(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [lsm.Capabilities.FS_CREATE,
                               lsm.Capabilities.FS_SNAPSHOT_CREATE]):

                fs = self._fs_create(s.id)[0]

                ss = self.c.fs_snapshot_create(fs, rs('ss'))[1]
                self.assertTrue(self._fs_snapshot_exists(fs, ss.id))

                # Delete snapshot
                if supported(cap, [lsm.Capabilities.FS_SNAPSHOT_DELETE]):
                    self._fs_snapshot_delete(fs, ss)

    def test_target_ports(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [lsm.Capabilities.TARGET_PORTS]):
                ports = self.c.target_ports()

                for p in ports:
                    self.assertTrue(p.id is not None)
                    self.assertTrue(p.port_type is not None)
                    self.assertTrue(p.service_address is not None)
                    self.assertTrue(p.network_address is not None)
                    self.assertTrue(p.physical_address is not None)
                    self.assertTrue(p.physical_name is not None)
                    self.assertTrue(p.system_id is not None)

    def _masking_state(self, cap, ag, vol, masked):
        if supported(cap,
                     [lsm.Capabilities.
                      VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP]):
            vol_masked = \
                self.c.volumes_accessible_by_access_group(ag)

            match = [x for x in vol_masked if x.id == vol.id]

            if masked:
                self.assertTrue(len(match) == 1)
            else:
                self.assertTrue(len(match) == 0)

        if supported(cap,
                     [lsm.Capabilities.
                      ACCESS_GROUPS_GRANTED_TO_VOLUME]):
            ag_masked = \
                self.c.access_groups_granted_to_volume(vol)

            match = [x for x in ag_masked if x.id == ag.id]

            if masked:
                self.assertTrue(len(match) == 1)
            else:
                self.assertTrue(len(match) == 0)

    def test_mask_unmask(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [lsm.Capabilities.ACCESS_GROUPS,
                               lsm.Capabilities.VOLUME_MASK,
                               lsm.Capabilities.VOLUME_UNMASK,
                               lsm.Capabilities.VOLUME_CREATE,
                               lsm.Capabilities.VOLUME_DELETE]):

                # Make sure we have an access group to test with, many
                # smi-s providers don't provide functionality to create them!
                ag_list = self.c.access_groups('system_id', s.id)
                if len(ag_list):
                    vol = self._volume_create(s.id)[0]
                    self.assertTrue(vol is not None)
                    chose_ag = None
                    for ag in ag_list:
                        if len(ag.init_ids) >= 1:
                            chose_ag = ag
                            break
                    if chose_ag is None:
                        raise Exception("No access group with 1+ member "
                                        "found, cannot do volume mask test")

                    if vol is not None and chose_ag is not None:
                        self.c.volume_mask(chose_ag, vol)
                        self._masking_state(cap, chose_ag, vol, True)
                        self.c.volume_unmask(chose_ag, vol)
                        self._masking_state(cap, chose_ag, vol, False)

                    if vol:
                        self._volume_delete(vol)

    def _create_access_group(self, cap, name, s, init_type):
        ag_created = None

        if init_type == lsm.AccessGroup.INIT_TYPE_ISCSI_IQN:
            ag_created = self.c.access_group_create(
                name,
                'iqn.1994-05.com.domain:01.89bd01',
                lsm.AccessGroup.INIT_TYPE_ISCSI_IQN, s)

        elif init_type == lsm.AccessGroup.INIT_TYPE_WWPN:
            ag_created = self.c.access_group_create(
                name,
                r_fcpn(),
                lsm.AccessGroup.INIT_TYPE_WWPN, s)

        self.assertTrue(ag_created is not None)

        if ag_created is not None:
            ag_list = self.c.access_groups()
            match = [x for x in ag_list if x.id == ag_created.id]
            self.assertTrue(len(match) == 1, "Newly created access group %s "
                                             "not in the access group listing"
                                             % (ag_created.name))

        return ag_created

    def _delete_access_group(self, ag):
        self.c.access_group_delete(ag)
        ag_list = self.c.access_groups()
        match = [x for x in ag_list if x.id == ag.id]
        self.assertTrue(len(match) == 0, "Expected access group that was "
                                         "deleted to not show up in the "
                                         "access group list!")

    def _test_ag_create_delete(self, cap, s):
        ag = None
        if supported(cap, [lsm.Capabilities.ACCESS_GROUPS,
                           lsm.Capabilities.ACCESS_GROUP_CREATE_ISCSI_IQN]):
            ag = self._create_access_group(
                cap, rs('ag'), s, lsm.AccessGroup.INIT_TYPE_ISCSI_IQN)
            if ag is not None and \
               supported(cap, [lsm.Capabilities.ACCESS_GROUP_DELETE]):
                self._delete_access_group(ag)

        if supported(cap, [lsm.Capabilities.ACCESS_GROUPS,
                            lsm.Capabilities.ACCESS_GROUP_CREATE_WWPN]):
            ag = self._create_access_group(
                cap, rs('ag'), s, lsm.AccessGroup.INIT_TYPE_WWPN)
            if ag is not None and \
               supported(cap, [lsm.Capabilities.ACCESS_GROUP_DELETE]):
                self._delete_access_group(ag)

    def test_access_group_create_delete(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            self._test_ag_create_delete(cap, s)

    def test_access_group_list(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [lsm.Capabilities.ACCESS_GROUPS]):
                ag_list = self.c.access_groups('system_id', s.id)
                if len(ag_list) == 0:
                    self._test_ag_create_delete(cap, s)
                else:
                    self.assertTrue(len(ag_list) > 0,
                                    "Need at least 1 access group for testing "
                                    "and no support exists for creation of "
                                    "access groups for this system")

    def _ag_init_add(self, ag):
        t = None
        t_id = ''

        if ag.init_type == lsm.AccessGroup.INIT_TYPE_ISCSI_IQN:
            t_id = 'iqn.1994-05.com.domain:01.89bd02'
            t = lsm.AccessGroup.INIT_TYPE_ISCSI_IQN
        else:
            # We will try FC PN
            t_id = r_fcpn()
            t = lsm.AccessGroup.INIT_TYPE_WWPN

        self.c.access_group_initiator_add(ag, t_id, t)

        ag_after = self.c.access_groups('id', ag.id)[0]
        match = [x for x in ag_after.init_ids if x == t_id]
        self.assertTrue(len(match) == 1)
        return t_id

    def _ag_init_delete(self, ag, init_id, init_type):
        self.c.access_group_initiator_delete(ag, init_id, init_type)
        ag_after = self.c.access_groups('id', ag.id)[0]
        match = [x for x in ag_after.init_ids if x == init_id]
        self.assertTrue(len(match) == 0)

    def test_access_group_initiator_add_delete(self):
        usable_ag_types = [lsm.AccessGroup.INIT_TYPE_WWPN,
                           lsm.AccessGroup.INIT_TYPE_ISCSI_IQN]

        for s in self.systems:
            ag_to_delete = None

            cap = self.c.capabilities(s)
            if supported(cap, [lsm.Capabilities.ACCESS_GROUPS]):
                ag_list = self.c.access_groups('system_id', s.id)

                if len(ag_list) == 0:
                    if supported(
                        cap, [lsm.Capabilities.ACCESS_GROUP_CREATE_ISCSI_IQN,
                              lsm.Capabilities.ACCESS_GROUP_DELETE]):
                        ag_to_delete = self._create_access_group(
                            cap, rs('ag'), s,
                            lsm.AccessGroup.INIT_TYPE_ISCSI_IQN)
                        ag_list = self.c.access_groups('system_id', s.id)
                    if supported(
                        cap, [lsm.Capabilities.ACCESS_GROUP_CREATE_WWPN,
                              lsm.Capabilities.ACCESS_GROUP_DELETE]):
                        ag_to_delete = self._create_access_group(
                            cap, rs('ag'), s, lsm.AccessGroup.INIT_TYPE_WWPN)
                        ag_list = self.c.access_groups('system_id', s.id)

                if len(ag_list):
                    # Try and find an initiator group that has a usable access
                    # group type instead of unknown or other...
                    ag = ag_list[0]
                    for a_tmp in ag_list:
                        if a_tmp.init_type in usable_ag_types:
                            ag = a_tmp
                            break

                    if supported(cap, [lsm.Capabilities.
                                       ACCESS_GROUP_INITIATOR_ADD_WWPN]):
                        init_id = self._ag_init_add(ag)
                        if supported(cap, [lsm.Capabilities.
                                            ACCESS_GROUP_INITIATOR_DELETE]):
                            self._ag_init_delete(
                                ag, init_id, lsm.AccessGroup.INIT_TYPE_WWPN)

                    if supported(cap, [lsm.Capabilities.
                                       ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN]):
                        init_id = self._ag_init_add(ag)
                        if supported(cap, [lsm.Capabilities.
                                            ACCESS_GROUP_INITIATOR_DELETE]):
                            self._ag_init_delete(
                                ag, init_id,
                                lsm.AccessGroup.INIT_TYPE_ISCSI_IQN)

                if ag_to_delete is not None:
                    self._delete_access_group(ag_to_delete)

    def test_duplicate_volume_name(self):
        if self.pool_by_sys_id:
            for s in self.systems:
                vol = None
                cap = self.c.capabilities(s)
                if supported(cap, [lsm.Capabilities.VOLUME_CREATE]):
                    vol, pool = self._volume_create(s.id)
                    self.assertTrue(vol is not None)

                    # Try to create another with same name
                    try:
                        vol_dupe = self.c.volume_create(
                            pool, vol.name, vol.size_bytes,
                            lsm.Volume.PROVISION_DEFAULT)[1]
                    except LsmError as le:
                        self.assertTrue(le.code == ErrorNumber.NAME_CONFLICT)

                    if vol is not None and \
                            supported(cap, [lsm.Capabilities.VOLUME_DELETE]):
                        self._volume_delete(vol)

    def test_duplicate_access_group_name(self):
        for s in self.systems:
            ag_to_delete = None

            ag_type = None
            ag_name = rs('ag_dupe')

            cap = self.c.capabilities(s)
            if supported(cap, [lsm.Capabilities.ACCESS_GROUPS]):
                ag_list = self.c.access_groups('system_id', s.id)

                if supported(
                    cap, [lsm.Capabilities.ACCESS_GROUP_CREATE_ISCSI_IQN,
                          lsm.Capabilities.ACCESS_GROUP_DELETE]):
                    ag_type = lsm.AccessGroup.INIT_TYPE_ISCSI_IQN

                elif supported(cap, [lsm.Capabilities.ACCESS_GROUP_CREATE_WWPN,
                                     lsm.Capabilities.ACCESS_GROUP_DELETE]):
                    ag_type = lsm.AccessGroup.INIT_TYPE_WWPN

                ag_created = self._create_access_group(
                    cap, ag_name, s, ag_type)

                # Try to create a duplicate
                got_exception = False
                try:
                    ag_dupe = self._create_access_group(
                        cap, ag_name, s, ag_type)
                except LsmError as le:
                    got_exception = True
                    self.assertTrue(le.code == ErrorNumber.NAME_CONFLICT)

                self.assertTrue(got_exception)

                self._delete_access_group(ag_created)


def dump_results():
    """
    unittest.main exits when done so we need to register this handler to
    get our results out.

    output details (yaml) results of what we called, how it finished and how
    long it took.
    """
    sys.stdout.write(yaml.dump(dict(methods_called=results, stats=stats)))


def add_our_params():
    """
    There are probably easier ways to extend unittest, but this seems
    easiest at the moment if we want to retain the default behavior and
    introduce a couple of parameters.
    """
    unittest.TestProgram.USAGE += """\

Options libStorageMgmt:
 --password  'Array password'
 --uri       'Array URI'
 """


if __name__ == "__main__":
    atexit.register(dump_results)
    add_our_params()

    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--password', default=None)
    parser.add_argument('--uri', default='sim://')
    options, other_args = parser.parse_known_args()

    if options.uri:
        TestPlugin.URI = options.uri

    if options.password:
        TestPlugin.PASSWORD = options.password

    unittest.main(argv=sys.argv[:1] + other_args)
