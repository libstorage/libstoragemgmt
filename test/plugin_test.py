#!/usr/bin/env python2
# Copyright (C) 2013-2016 Red Hat, Inc.
# (C) Copyright 2015-2016 Hewlett Packard Enterprise Development LP
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
# Author:   tasleson
#           Joe Handzik <joseph.t.handzik@hpe.com>
#           Gris Ge <fge@redhat.com>

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
import os
import tempfile
from lsm import LsmError, ErrorNumber
from lsm import Capabilities as Cap
import six

results = {}
stats = {}


MIN_POOL_SIZE = 4096
MIN_OBJECT_SIZE = 512


# If you print anything during execution make sure it's to stderr as the
# automated tests are making the assumption that test messages go to stderr and
# execution information gets written to stdout as yaml
def print_stderr(msg):
    sys.stderr.write(msg)
    sys.stderr.flush()


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
    rp = ''.join(random.choice(string.ascii_uppercase) for x in range(l))

    if component is not None:
        return 'lsm_%s_%s' % (component, rp)
    return rp


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

    # Errors that we are forcing to occur
    not_logging = [lsm.ErrorNumber.NO_SUPPORT,
                   lsm.ErrorNumber.NAME_CONFLICT,
                   lsm.ErrorNumber.NO_STATE_CHANGE,
                   lsm.ErrorNumber.IS_MASKED,
                   lsm.ErrorNumber.EXISTS_INITIATOR]

    # Hash of all calls that can be async
    async_calls = {'volume_create': (str, lsm.Volume),
                   'volume_resize': (str, lsm.Volume),
                   'volume_replicate': (str, lsm.Volume),
                   'volume_replicate_range': (str,),
                   'volume_delete': (str,),
                   'volume_child_dependency_rm': (str,),
                   'fs_delete': (str,),
                   'fs_resize': (str, lsm.FileSystem),
                   'fs_create': (str, lsm.FileSystem),
                   'fs_clone': (str, lsm.FileSystem),
                   'fs_file_clone': (str,),
                   'fs_snapshot_create': (str, lsm.FsSnapshot),
                   'fs_snapshot_delete': (str,),
                   'fs_snapshot_restore': (str,),
                   'fs_child_dependency_rm': (str,)}

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
        if method not in results:
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
                # We are forcing some types of error, for these we won't log
                # but will allow the test case asserts to check to make sure
                # we actually got them.
                if le.code not in self.not_logging:
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
        for skip_test_case in TestPlugin.SKIP_TEST_CASES:
            if self.id().endswith(skip_test_case):
                self.skipTest("Tested has been skiped as requested")

        self.c = TestProxy(lsm.Client(TestPlugin.URI, TestPlugin.PASSWORD))

        self.systems = self.c.systems()
        self.pools = self.c.pools()

        self.pool_by_sys_id = {}

        for s in self.systems:
            self.pool_by_sys_id[s.id] = [p for p in self.pools if
                                         p.system_id == s.id]

        # TODO Store what exists, so that we don't remove it

    def _get_pool_by_usage(self, system_id, element_type,
                           unsupported_features=0):
        largest_free = 0
        rc = None

        for p in self.pool_by_sys_id[system_id]:
            # If the pool matches our criteria and min size we will consider
            # it, but we will select the one with the most free space for
            # testing and one that support volume expansion
            if p.element_type & element_type and \
                    p.free_space > mb_in_bytes(MIN_POOL_SIZE) and \
                    (not p.unsupported_actions & unsupported_features):
                if p.free_space > largest_free:
                    largest_free = p.free_space
                    rc = p
        return rc

    def _clean_up(self):

        # Note: We make best effort to clean things up, thus we are ignoring
        # exceptions in this code, depending on what's failing in the plugin
        # we may not be able to remove something we created
        try:
            for s in self.systems:
                cap = self.c.capabilities(s)
                # Remove any access groups we created, removing any mappings
                # first.
                if supported(cap, [Cap.ACCESS_GROUP_DELETE,
                                   Cap.VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP]):
                    for ag in self.c.access_groups():
                        if 'lsm_' in ag.name and s.id == ag.system_id:
                            try:
                                # Make sure it doesn't have any mappings
                                mapped_volume = self.c.\
                                    volumes_accessible_by_access_group(ag)

                                # Remove any mappings
                                for vol in mapped_volume:
                                    self.c.volume_unmask(ag, vol)

                                # Lastly, remove the access group
                                self.c.access_group_delete(ag)
                            except LsmError as le:
                                print_stderr("[WARNING] error when "
                                             "removing ag %s\n" % str(le))
                                pass

                # Remove any volumes we created
                if supported(cap, [Cap.VOLUME_DELETE]):
                    for v in self.c.volumes():
                        if 'lsm_' in v.name and s.id == v.system_id:
                            # Check to see if this volume is participating in
                            # an access group, if it is we will remove the
                            # volume from it, but we will not delete the access
                            # group as we likely didn't create it
                            access_groups = self.c.\
                                access_groups_granted_to_volume(v)
                            for ag in access_groups:
                                try:
                                    self.c.volume_unmask(ag, v)
                                except LsmError as le:
                                    print_stderr(
                                        "[WARNING] error when unmasking "
                                        "volume %s\n" % str(le))
                                    pass
                            try:
                                self.c.volume_delete(v)
                            except LsmError as le:
                                print_stderr("[WARNING] error when removing "
                                             "volume %s\n" % str(le))
                                pass

                # Remove any fs exports we created
                if supported(cap, [Cap.FS, Cap.EXPORTS, Cap.EXPORT_REMOVE]):

                    # Get all the FS
                    fs_list = self.c.fs()

                    for e_fs in self.c.exports():
                        # Get the fs object that is exported
                        fs = [x for x in fs_list if x.id == e_fs.fs_id][0]

                        # Make sure we are un-exporting a FS we created
                        if 'lsm_' in fs.name and s.id == fs.system_id:
                            try:
                                self.c.export_remove(e_fs)
                            except LsmError as le:
                                print_stderr("[WARNING] error when removing "
                                             "export %s\n" % str(le))
                                pass

                # Remove any fs we created
                if supported(cap, [Cap.FS, Cap.FS_DELETE]):
                    for f in self.c.fs():
                        if self.c.fs_child_dependency(f, None):
                            self.c.fs_child_dependency_rm(f, None)
                        if 'lsm_' in f.name and s.id == f.system_id:
                            try:
                                self.c.fs_delete(f)
                            except LsmError as le:
                                print_stderr("[WARNING] error when removing "
                                             "fs %s\n" % str(le))
                                pass

        except Exception as e:
            print_stderr("[WARNING] exception in _clean_up %s\n" % str(e))
            pass

    def tearDown(self):
        self._clean_up()
        self.c.close()

    def test_plugin_info(self):
        (desc, version) = self.c.plugin_info()
        self.assertTrue(desc is not None and len(desc) > 0)
        self.assertTrue(version is not None and len(version) > 0)

    def test_fw_version_get(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.SYS_FW_VERSION_GET]):
                fw_ver = s.fw_version
                self.assertTrue(fw_ver is not None and len(fw_ver) > 0,
                                "Firmware version retrieval failed")

    def test_sys_mode_get(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.SYS_MODE_GET]):
                sys_mode = s.mode
                self.assertTrue(sys_mode is not None,
                                "System mode retrieval failed")

    def test_sys_read_cache_pct_get(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.SYS_READ_CACHE_PCT_GET]):
                read_pct = s.read_cache_pct
                self.assertTrue(read_pct is not None,
                                "Read cache percentage retrieval failed")

    def test_system_read_cache_pct_update(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.SYS_READ_CACHE_PCT_UPDATE]):
                cache_status = self.c.system_read_cache_pct_update(s, 100)
                self.assertTrue(cache_status is None,
                                "system_read_cache_pct_update failed")

    def test_timeout(self):
        tmo = 40000
        self.c.time_out_set(tmo)
        self.assertEqual(self.c.time_out_get(), tmo)

    def test_systems_list(self):
        arrays = self.c.systems()
        self.assertTrue(len(arrays) > 0, "We need at least one array for "
                                         "testing!")

    def test_pools_list(self):
        pools_list = self.c.pools()
        self.assertTrue(len(pools_list) > 0, "We need at least 1 pool to test")

    def _find_or_create_volumes(self):
        """
        Find existing volumes, if not found, try to create one.
        Return (volumes, flag_created)
        If 'flag_created' is True, then returned volumes is newly created.
        """
        volumes = self.c.volumes()
        flag_created = False
        if len(self.c.volumes()) == 0:
            for s in self.systems:
                cap = self.c.capabilities(s)
                if supported(cap, [Cap.VOLUME_CREATE, Cap.VOLUME_DELETE]):
                    self._volume_create(s.id)
                    flag_created = True
                    break
            volumes = self.c.volumes()

        return volumes, flag_created

    def test_volume_list(self):
        (volumes, flag_created) = self._find_or_create_volumes()
        self.assertTrue(len(volumes) > 0, "We need at least 1 volume to test")

        if flag_created:
            self._volume_delete(volumes[0])

    def test_volume_vpd83(self):
        (volumes, flag_created) = self._find_or_create_volumes()
        self.assertTrue(len(volumes) > 0, "We need at least 1 volume to test")
        for v in volumes:
            self.assertTrue(lsm.Volume.vpd83_verify(v.vpd83),
                            "VPD is not as expected '%s' for volume id: '%s'" %
                            (v.vpd83, v.id))
        if flag_created:
            self._volume_delete(volumes[0])

    def test_disks_list(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.DISKS]):
                disks = self.c.disks()
                self.assertTrue(len(disks) > 0,
                                "We need at least 1 disk to test")
                if supported(cap, [Cap.DISK_VPD83_GET]):
                    try:
                        list(disk.vpd83 for disk in disks)
                        list(disk.rpm for disk in disks)
                        list(disk.link_type for disk in disks)
                    except LsmError as lsm_err:
                        if lsm_err.code != ErrorNumber.NO_SUPPORT:
                            raise

    def test_disk_location_get(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.DISK_LOCATION]):
                for disk in self.c.disks():
                    disk_location = disk.location
                    self.assertTrue(disk_location is not None and
                                    len(disk_location) > 0,
                                    "Disk location retrieval failed")

    def _volume_create(self, system_id,
                       element_type=lsm.Pool.ELEMENT_TYPE_VOLUME,
                       unsupported_features=0):
        if system_id in self.pool_by_sys_id:
            p = self._get_pool_by_usage(system_id, element_type,
                                        unsupported_features)

            self.assertTrue(p is not None, "Unable to find a suitable pool")

            if p:
                vol_size = self._object_size(p)

                vol = self.c.volume_create(p, rs('v'), vol_size,
                                           lsm.Volume.PROVISION_DEFAULT)[1]

                self.assertTrue(self._volume_exists(vol.id), p.id)
                self.assertTrue(vol.pool_id == p.id)
                self.assertTrue(vol.system_id == p.system_id)

                return vol, p

    def _fs_create(self, system_id):
        if system_id in self.pool_by_sys_id:
            fs = None
            pool = self._get_pool_by_usage(system_id,
                                           lsm.Pool.ELEMENT_TYPE_FS)

            self.assertTrue(pool is not None, "Unable to find a suitable pool "
                                              "for fs creation")

            if pool is not None:
                fs_size = self._object_size(pool)
                fs = self.c.fs_create(pool, rs('fs'), fs_size)[1]

                self.assertTrue(fs is not None)

                if fs:
                    self.assertTrue(self._fs_exists(fs.id))
                    self.assertTrue(self._system_exists(fs.system_id))
                    self.assertTrue(pool.system_id == fs.system_id)

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

    def _system_exists(self, system_id):
        systems = self.c.systems()

        for s in systems:
            if s.id == system_id:
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
                if supported(cap, [Cap.VOLUME_CREATE]):
                    vol = self._volume_create(s.id)[0]
                    self.assertTrue(vol is not None)

                    if vol is not None and \
                            supported(cap, [Cap.VOLUME_DELETE]):
                        self._volume_delete(vol)

    def test_volume_resize(self):
        if self.pool_by_sys_id:
            for s in self.systems:
                cap = self.c.capabilities(s)

                # We need to make sure that the pool supports volume grow.
                unsupported = lsm.Pool.UNSUPPORTED_VOLUME_GROW

                if supported(cap, [Cap.VOLUME_CREATE,
                                   Cap.VOLUME_DELETE,
                                   Cap.VOLUME_RESIZE]):
                    vol = self._volume_create(
                        s.id,
                        unsupported_features=unsupported)[0]
                    vol_resize = self.c.volume_resize(
                        vol, vol.size_bytes + mb_in_bytes(16))[1]
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

                if supported(cap, [Cap.VOLUME_CREATE,
                                   Cap.VOLUME_DELETE]):
                    vol, pool = self._volume_create(s.id)

                    # For the moment lets allow the array to pick the pool
                    # to supply the backing store for the replicate
                    if supported(cap, [capability]):
                        volume_clone = self.c.volume_replicate(
                            None, replication_type, vol,
                            rs('v_c_'))[1]

                        self.assertTrue(volume_clone is not None)

                        if volume_clone:
                            self.assertTrue(
                                self._volume_exists(volume_clone.id))

                            # Lets test for creating a clone with an
                            # existing name
                            error_num = None
                            try:
                                volume_clone_dupe_name = \
                                    self.c.volume_replicate(
                                        None, replication_type, vol,
                                        volume_clone.name)[1]
                            except LsmError as le:
                                error_num = le.code

                            self.assertTrue(error_num ==
                                            ErrorNumber.NAME_CONFLICT)

                        self._volume_delete(volume_clone)

                    self._volume_delete(vol)

    def test_volume_replication(self):
        self._replicate_test(Cap.VOLUME_REPLICATE_CLONE,
                             lsm.Volume.REPLICATE_CLONE)

        self._replicate_test(Cap.VOLUME_REPLICATE_COPY,
                             lsm.Volume.REPLICATE_COPY)

        self._replicate_test(Cap.VOLUME_REPLICATE_MIRROR_ASYNC,
                             lsm.Volume.REPLICATE_MIRROR_ASYNC)

        self._replicate_test(Cap.VOLUME_REPLICATE_MIRROR_SYNC,
                             lsm.Volume.REPLICATE_MIRROR_SYNC)

    def test_volume_replicate_range_block_size(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.VOLUME_COPY_RANGE_BLOCK_SIZE]):
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
                             [Cap.VOLUME_COPY_RANGE_BLOCK_SIZE,
                              Cap.VOLUME_CREATE,
                              Cap.VOLUME_DELETE,
                              Cap.VOLUME_COPY_RANGE]):

                    size = self.c.volume_replicate_range_block_size(s)

                    vol, pool = self._volume_create(s.id)

                    br = lsm.BlockRange(0, size, size)

                    if supported(
                            cap, [Cap.VOLUME_COPY_RANGE_CLONE]):
                        self.c.volume_replicate_range(
                            lsm.Volume.REPLICATE_CLONE, vol, vol, [br])
                    else:
                        self.assertRaises(
                            lsm.LsmError,
                            self.c.volume_replicate_range,
                            lsm.Volume.REPLICATE_CLONE, vol, vol, [br])

                    br = lsm.BlockRange(size * 2, size, size)

                    if supported(
                            cap, [Cap.VOLUME_COPY_RANGE_COPY]):
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

            if supported(cap, [Cap.FS_CREATE]):
                fs, pool = self._fs_create(s.id)

                if fs is not None:
                    if supported(cap, [Cap.FS_DELETE]):
                        self._fs_delete(fs)

    def test_fs_resize(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.FS_CREATE]):
                fs, pool = self._fs_create(s.id)

                if fs is not None:
                    if supported(cap, [Cap.FS_RESIZE]):
                        fs_size = fs.total_space + mb_in_bytes(16)
                        fs_resized = self.c.fs_resize(fs, fs_size)[1]
                        self.assertTrue(fs_resized.total_space)

                    if supported(cap, [Cap.FS_DELETE]):
                        self._fs_delete(fs)

    def test_fs_clone(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.FS_CREATE,
                               Cap.FS_CLONE]):
                fs, pool = self._fs_create(s.id)

                if fs is not None:
                    fs_clone = self.c.fs_clone(fs, rs('fs_c'))[1]

                    if supported(cap, [Cap.FS_DELETE]):
                        self._fs_delete(fs_clone)
                        self._fs_delete(fs)

    def test_fs_snapshot(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.FS_CREATE,
                               Cap.FS_SNAPSHOT_CREATE]):

                fs, pool = self._fs_create(s.id)

                if fs is not None:
                    ss = self.c.fs_snapshot_create(fs, rs('ss'))[1]
                    self.assertTrue(self._fs_snapshot_exists(fs, ss.id))

                    if supported(cap, [Cap.FS_SNAPSHOT_RESTORE]):
                        self.c.fs_snapshot_restore(fs, ss, None, None, True)

                    # Delete snapshot
                    if supported(cap, [Cap.FS_SNAPSHOT_DELETE]):
                        self._fs_snapshot_delete(fs, ss)

    def test_target_ports(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.TARGET_PORTS]):
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
                     [Cap.
                      VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP]):
            vol_masked = \
                self.c.volumes_accessible_by_access_group(ag)

            match = [x for x in vol_masked if x.id == vol.id]

            if masked:
                self.assertTrue(len(match) == 1, "len = %d" % len(match))
            else:
                self.assertTrue(len(match) == 0, "len = %d" % len(match))

        if supported(cap,
                     [Cap.
                      ACCESS_GROUPS_GRANTED_TO_VOLUME]):
            ag_masked = \
                self.c.access_groups_granted_to_volume(vol)

            match = [x for x in ag_masked if x.id == ag.id]

            if masked:
                self.assertTrue(len(match) == 1, "len = %d" % len(match))
            else:
                self.assertTrue(len(match) == 0, "len = %d" % len(match))

    def test_mask_unmask(self):
        for s in self.systems:
            ag_created = None
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.ACCESS_GROUPS,
                               Cap.VOLUME_MASK,
                               Cap.VOLUME_UNMASK,
                               Cap.VOLUME_CREATE,
                               Cap.VOLUME_DELETE]):

                if supported(cap, [Cap.ACCESS_GROUP_CREATE_ISCSI_IQN]):
                    ag_name = rs("ag")
                    ag_iqn = 'iqn.1994-05.com.domain:01.' + rs(None, 6)

                    ag_created = self.c.access_group_create(
                        ag_name, ag_iqn, lsm.AccessGroup.INIT_TYPE_ISCSI_IQN,
                        s)

                # Make sure we have an access group to test with, many
                # smi-s providers don't provide functionality to create them!
                ag_list = self.c.access_groups('system_id', s.id)
                if len(ag_list):
                    vol = self._volume_create(s.id)[0]
                    self.assertTrue(vol is not None)

                    chose_ag = ag_created

                    if chose_ag is None:
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

                        # Test duplicate call for NO_STATE_CHANGE error
                        flag_dup_error_found = False
                        try:
                            self.c.volume_mask(chose_ag, vol)
                        except LsmError as lsm_error:
                            self.assertTrue(
                                lsm_error.code == ErrorNumber.NO_STATE_CHANGE)
                            flag_dup_error_found = True
                        self.assertTrue(flag_dup_error_found)

                        self.c.volume_unmask(chose_ag, vol)
                        self._masking_state(cap, chose_ag, vol, False)

                        # Test duplicate call for NO_STATE_CHANGE error
                        flag_dup_error_found = False
                        try:
                            self.c.volume_unmask(chose_ag, vol)
                        except LsmError as lsm_error:
                            self.assertTrue(
                                lsm_error.code == ErrorNumber.NO_STATE_CHANGE)
                            flag_dup_error_found = True
                        self.assertTrue(flag_dup_error_found)

                    if vol:
                        self._volume_delete(vol)

                if ag_created:
                    self.assertTrue(s.id == ag_created.system_id)

                    self.c.access_group_delete(ag_created)
                    ag_created = None

    def _create_access_group(self, cap, name, s, init_type):
        ag_created = None

        if init_type == lsm.AccessGroup.INIT_TYPE_ISCSI_IQN:
            ag_created = self.c.access_group_create(
                name,
                'iqn.1994-05.com.domain:01.' + rs(None, 6),
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
            self.assertTrue(
                len(match) == 1,
                "Newly created access group %s not in the access group listing"
                % ag_created.name)

            self.assertTrue(s.id == ag_created.system_id)

        return ag_created

    def _delete_access_group(self, ag):
        self.c.access_group_delete(ag)
        ag_list = self.c.access_groups()
        match = [x for x in ag_list if x.id == ag.id]
        self.assertTrue(len(match) == 0, "Expected access group that was "
                                         "deleted to not show up in the "
                                         "access group list!")

    def _test_ag_create_dup(self, lsm_ag, lsm_system):
        """
        Test NAME_CONFLICT and EXISTS_INITIATOR of access_group_create().
        """
        flag_got_expected_error = False
        new_init_id = None
        if lsm_ag.init_type == lsm.AccessGroup.INIT_TYPE_ISCSI_IQN:
            new_init_id = 'iqn.1994-05.com.domain:01.' + rs(None, 6)
        else:
            new_init_id = r_fcpn()
        try:
            self.c.access_group_create(
                lsm_ag.name, new_init_id, lsm_ag.init_type, lsm_system)
        except LsmError as lsm_error:
            self.assertTrue(lsm_error.code == ErrorNumber.NAME_CONFLICT)
            flag_got_expected_error = True

        self.assertTrue(flag_got_expected_error)

        flag_got_expected_error = False
        try:
            self.c.access_group_create(
                rs('ag'), lsm_ag.init_ids[0], lsm_ag.init_type, lsm_system)
        except LsmError as lsm_error:
            self.assertTrue(lsm_error.code == ErrorNumber.EXISTS_INITIATOR)
            flag_got_expected_error = True

        self.assertTrue(flag_got_expected_error)

    def _test_ag_create_delete(self, cap, s):
        ag = None
        if supported(cap, [Cap.ACCESS_GROUPS,
                           Cap.ACCESS_GROUP_CREATE_ISCSI_IQN]):
            ag = self._create_access_group(
                cap, rs('ag'), s, lsm.AccessGroup.INIT_TYPE_ISCSI_IQN)
            if ag is not None and \
               supported(cap, [Cap.ACCESS_GROUP_DELETE]):
                self._test_ag_create_dup(ag, s)
                self._delete_access_group(ag)

        if supported(cap, [Cap.ACCESS_GROUPS, Cap.ACCESS_GROUP_CREATE_WWPN]):
            ag = self._create_access_group(
                cap, rs('ag'), s, lsm.AccessGroup.INIT_TYPE_WWPN)
            if ag is not None and \
               supported(cap, [Cap.ACCESS_GROUP_DELETE]):
                self._test_ag_create_dup(ag, s)
                self._delete_access_group(ag)

    def test_iscsi_chap(self):
        ag = None

        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.ACCESS_GROUPS,
                               Cap.ACCESS_GROUP_CREATE_ISCSI_IQN,
                               Cap.VOLUME_ISCSI_CHAP_AUTHENTICATION]):
                ag = self._create_access_group(
                    cap, rs('ag'), s, lsm.AccessGroup.INIT_TYPE_ISCSI_IQN)

                self.assertTrue(ag is not None)

                if ag:
                    self.c.iscsi_chap_auth(ag.init_ids[0], 'foo', rs(None, 12),
                                           None, None)

                    if supported(cap, [Cap.ACCESS_GROUP_DELETE]):
                        self._test_ag_create_dup(ag, s)
                        self._delete_access_group(ag)

    def test_access_group_create_delete(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            self._test_ag_create_delete(cap, s)

    def test_access_group_list(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.ACCESS_GROUPS]):
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

        ag_add = self.c.access_group_initiator_add(ag, t_id, t)

        self.assertTrue(ag_add.system_id == ag.system_id)

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
            if supported(cap, [Cap.ACCESS_GROUPS]):
                ag_list = self.c.access_groups('system_id', s.id)

                if supported(cap, [Cap.ACCESS_GROUP_CREATE_WWPN])\
                        or supported(cap, [Cap.ACCESS_GROUP_CREATE_ISCSI_IQN]):

                    if supported(
                        cap, [Cap.ACCESS_GROUP_CREATE_ISCSI_IQN,
                              Cap.ACCESS_GROUP_DELETE]):
                        ag_to_delete = self._create_access_group(
                            cap, rs('ag'), s,
                            lsm.AccessGroup.INIT_TYPE_ISCSI_IQN)
                        ag_list = self.c.access_groups('system_id', s.id)

                        if supported(cap,
                                     [Cap.
                                      ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN]):

                            init_id = self._ag_init_add(ag_to_delete)

                            if supported(
                                    cap, [Cap.ACCESS_GROUP_INITIATOR_DELETE]):
                                self._ag_init_delete(
                                    ag_to_delete, init_id,
                                    lsm.AccessGroup.INIT_TYPE_ISCSI_IQN)

                    if supported(
                        cap, [Cap.ACCESS_GROUP_CREATE_WWPN,
                              Cap.ACCESS_GROUP_DELETE]):
                        ag_to_delete = self._create_access_group(
                            cap, rs('ag'), s, lsm.AccessGroup.INIT_TYPE_WWPN)
                        ag_list = self.c.access_groups('system_id', s.id)

                    if ag_to_delete is not None:
                        self._delete_access_group(ag_to_delete)
                else:
                    if len(ag_list):
                        # Try and find an initiator group that has a usable
                        # access group type instead of unknown or other...
                        ag = ag_list[0]
                        for a_tmp in ag_list:
                            if a_tmp.init_type in usable_ag_types:
                                ag = a_tmp
                                break

                        if supported(cap, [Cap.
                                           ACCESS_GROUP_INITIATOR_ADD_WWPN]):
                            init_id = self._ag_init_add(ag)
                            if supported(
                                    cap, [Cap.ACCESS_GROUP_INITIATOR_DELETE]):
                                self._ag_init_delete(
                                    ag, init_id,
                                    lsm.AccessGroup.INIT_TYPE_WWPN)

                        if supported(
                                cap,
                                [Cap.ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN]):
                            init_id = self._ag_init_add(ag)
                            if supported(cap,
                                         [Cap.ACCESS_GROUP_INITIATOR_DELETE]):
                                self._ag_init_delete(
                                    ag, init_id,
                                    lsm.AccessGroup.INIT_TYPE_ISCSI_IQN)

    def test_duplicate_volume_name(self):
        if self.pool_by_sys_id:
            for s in self.systems:
                vol = None
                cap = self.c.capabilities(s)
                if supported(cap, [Cap.VOLUME_CREATE]):
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
                            supported(cap, [Cap.VOLUME_DELETE]):
                        self._volume_delete(vol)

    def test_duplicate_access_group_name(self):
        for s in self.systems:
            ag_to_delete = None

            ag_type = None
            ag_name = rs('ag_dupe')

            cap = self.c.capabilities(s)
            if supported(cap, [Cap.ACCESS_GROUPS,
                               Cap.ACCESS_GROUP_DELETE]):
                ag_list = self.c.access_groups('system_id', s.id)

                if supported(
                        cap, [Cap.ACCESS_GROUP_CREATE_ISCSI_IQN]):
                    ag_type = lsm.AccessGroup.INIT_TYPE_ISCSI_IQN

                elif supported(cap,
                               [Cap.ACCESS_GROUP_CREATE_WWPN]):
                    ag_type = lsm.AccessGroup.INIT_TYPE_WWPN

                else:
                    return

                ag_created = self._create_access_group(
                    cap, ag_name, s, ag_type)

                if ag_created is not None:
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

    def test_ag_vol_delete_with_vol_masked(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.ACCESS_GROUPS,
                               Cap.ACCESS_GROUP_CREATE_ISCSI_IQN,
                               Cap.ACCESS_GROUP_DELETE,
                               Cap.VOLUME_UNMASK,
                               Cap.VOLUME_CREATE,
                               Cap.VOLUME_DELETE,
                               Cap.VOLUME_MASK,
                               Cap.VOLUME_UNMASK]):

                ag_name = rs("ag")
                ag_iqn = 'iqn.1994-05.com.domain:01.' + rs(None, 6)

                ag = self.c.access_group_create(
                    ag_name, ag_iqn, lsm.AccessGroup.INIT_TYPE_ISCSI_IQN, s)

                pool = self._get_pool_by_usage(s.id,
                                               lsm.Pool.ELEMENT_TYPE_VOLUME)
                if ag and pool:
                    vol_size = self._object_size(pool)

                    vol = self.c.volume_create(
                        pool, rs('v'), vol_size,
                        lsm.Volume.PROVISION_DEFAULT)[1]

                    if vol:
                        got_exception = False
                        self.c.volume_mask(ag, vol)

                        # Try to delete the access group

                        try:
                            self.c.access_group_delete(ag)
                        except LsmError as le:
                            if le.code == lsm.ErrorNumber.IS_MASKED:
                                got_exception = True
                            self.assertTrue(le.code ==
                                            lsm.ErrorNumber.IS_MASKED)

                        self.assertTrue(got_exception)

                        # Try to delete the volume
                        got_exception = False
                        try:
                            self.c.volume_delete(vol)
                        except LsmError as le:
                            if le.code == lsm.ErrorNumber.IS_MASKED:
                                got_exception = True
                            self.assertTrue(le.code ==
                                            lsm.ErrorNumber.IS_MASKED)

                        self.assertTrue(got_exception)

                        # Clean up
                        self.c.volume_unmask(ag, vol)
                        self.c.volume_delete(vol)

                    self.c.access_group_delete(ag)

    def test_volume_vpd83_verify(self):

        failing = [None,
                   "012345678901234567890123456789AB",
                   "012345678901234567890123456789ax",
                   "012345678901234567890123456789ag",
                   "1234567890123456789012345abcdef",
                   "01234567890123456789012345abcdefa",
                   "55cd2e404beec32e0", "55cd2e404beec32ex",
                   "35cd2e404beec32A"]

        for f in failing:
            self.assertFalse(lsm.Volume.vpd83_verify(f))

        self.assertTrue(
            lsm.Volume.vpd83_verify("61234567890123456789012345abcdef"))
        self.assertTrue(
            lsm.Volume.vpd83_verify("55cd2e404beec32e"))
        self.assertTrue(
            lsm.Volume.vpd83_verify("35cd2e404beec32e"))
        self.assertTrue(
            lsm.Volume.vpd83_verify("25cd2e404beec32e"))

    def test_available_plugins(self):
        plugins = self.c.available_plugins(':')
        self.assertTrue(plugins is not None)
        self.assertTrue(len(plugins) > 0)
        self.assertTrue(':' in plugins[0])

    def test_volume_enable_disable(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.VOLUME_CREATE, Cap.VOLUME_DELETE,
                               Cap.VOLUME_ENABLE, Cap.VOLUME_DISABLE]):
                vol, pool = self._volume_create(s.id)

                self.c.volume_disable(vol)
                self.c.volume_enable(vol)

                self._volume_delete(vol)

    def test_daemon_not_running(self):
        current = None
        got_exception = False
        # Force a ErrorNumber.DAEMON_NOT_RUNNING
        if 'LSM_UDS_PATH' in os.environ:
            current = os.environ['LSM_UDS_PATH']

        tmp_dir = tempfile.mkdtemp()
        os.environ['LSM_UDS_PATH'] = tmp_dir

        try:
            tmp_c = lsm.Client(TestPlugin.URI, TestPlugin.PASSWORD)
        except LsmError as expected_error:
            got_exception = True
            self.assertTrue(expected_error.code ==
                            ErrorNumber.DAEMON_NOT_RUNNING,
                            'Actual error %d' % (expected_error.code))

        self.assertTrue(got_exception)

        os.rmdir(tmp_dir)

        if current:
            os.environ['LSM_UDS_PATH'] = current
        else:
            del os.environ['LSM_UDS_PATH']

    def test_non_existent_plugin(self):
        got_exception = False
        try:
            uri = "%s://user@host" % rs(None, 6)

            tmp_c = lsm.Client(uri, TestPlugin.PASSWORD)
        except LsmError as expected_error:
            got_exception = True
            self.assertTrue(expected_error.code ==
                            ErrorNumber.PLUGIN_NOT_EXIST,
                            'Actual error %d' % (expected_error.code))

        self.assertTrue(got_exception)

    def test_volume_depends(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.VOLUME_CREATE, Cap.VOLUME_DELETE,
                               Cap.VOLUME_CHILD_DEPENDENCY,
                               Cap.VOLUME_CHILD_DEPENDENCY_RM]) and \
                    (supported(cap, [Cap.VOLUME_REPLICATE_COPY]) or
                     supported(cap, [Cap.VOLUME_REPLICATE_CLONE])):
                vol, pol = self._volume_create(s.id)

                if supported(cap, [Cap.VOLUME_REPLICATE_CLONE]):
                    vol_child = self.c.volume_replicate(
                        None,
                        lsm.Volume.REPLICATE_CLONE,
                        vol,
                        rs('v_tc_'))[1]
                else:
                    vol_child = self.c.volume_replicate(
                        None,
                        lsm.Volume.REPLICATE_COPY,
                        vol,
                        rs('v_fc_'))[1]

                self.assertTrue(vol_child is not None)

                if self.c.volume_child_dependency(vol):
                    self.c.volume_child_dependency_rm(vol)
                else:
                    self.assertTrue(self.c.volume_child_dependency_rm(vol)
                                    is None)

                self._volume_delete(vol)

                if vol_child:
                    self._volume_delete(vol_child)

    def test_fs_depends(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.FS_CREATE, Cap.FS_DELETE,
                               Cap.FS_CHILD_DEPENDENCY,
                               Cap.FS_CHILD_DEPENDENCY_RM,
                               Cap.FS_CLONE]):
                fs, pol = self._fs_create(s.id)
                fs_child = self.c.fs_clone(fs, rs('fs_c_'))[1]
                self.assertTrue(fs_child is not None)

                if self.c.fs_child_dependency(fs, None):
                    self.c.fs_child_dependency_rm(fs, None)
                else:
                    self.assertTrue(self.c.fs_child_dependency_rm(fs, None)
                                    is None)

                self._fs_delete(fs)
                if fs_child:
                    self._fs_delete(fs_child)

    def test_nfs_auth_types(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.EXPORT_AUTH]):
                auth_types = self.c.export_auth()
                self.assertTrue(auth_types is not None)

    def test_export_list(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.EXPORTS]):
                exports = self.c.exports()
                # TODO verify export values

    def test_create_delete_exports(self):
        for s in self.systems:
            cap = self.c.capabilities(s)

            if supported(cap, [Cap.FS_CREATE, Cap.EXPORTS, Cap.EXPORT_FS,
                               Cap.EXPORT_REMOVE]):
                fs, pool = self._fs_create(s.id)

                if supported(cap, [Cap.EXPORT_CUSTOM_PATH]):
                    path = "/mnt/%s" % rs(None, 6)
                    exp = self.c.export_fs(fs.id, path, [], [],
                                           ['192.168.2.1'])
                else:
                    exp = self.c.export_fs(fs.id, None, [], [],
                                           ['192.168.2.1'])
                self.c.export_remove(exp)
                self._fs_delete(fs)

    def test_pool_member_info(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.POOL_MEMBER_INFO]):
                for pool in self.c.pools():
                    (raid_type, member_type, member_ids) = \
                        self.c.pool_member_info(pool)
                    self.assertTrue(type(raid_type) is int)
                    self.assertTrue(type(member_type) is int)
                    self.assertTrue(type(member_ids) is list)

    def _skip_current_test(self, messsage):
        """
        If skipTest is supported, skip this test with provided message.
        Sliently return if not supported.
        """
        if hasattr(unittest.TestCase, 'skipTest') is True:
            self.skipTest(messsage)
        return

    def test_volume_raid_create(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            # TODO(Gris Ge): Add test code for other RAID type and strip size
            if supported(cap, [Cap.VOLUME_RAID_CREATE]):
                supported_raid_types, supported_strip_sizes = \
                    self.c.volume_raid_create_cap_get(s)
                if lsm.Volume.RAID_TYPE_RAID1 not in supported_raid_types:
                    self._skip_current_test(
                        "Skip test: current system does not support "
                        "creating RAID1 volume")

                # Find two free disks
                free_disks = []
                for disk in self.c.disks():
                    if len(free_disks) == 2:
                        break
                    if disk.status & lsm.Disk.STATUS_FREE:
                        free_disks.append(disk)

                if len(free_disks) != 2:
                    self._skip_current_test(
                        "Skip test: Failed to find two free disks for RAID 1")
                    return

                new_vol = self.c.volume_raid_create(
                    rs('v'), lsm.Volume.RAID_TYPE_RAID1, free_disks,
                    lsm.Volume.VCR_STRIP_SIZE_DEFAULT)

                self.assertTrue(new_vol is not None)

                # TODO(Gris Ge): Use volume_raid_info() and pool_member_info()
                # to verify size, raid_type, member type, member ids.

                if supported(cap, [Cap.VOLUME_DELETE]):
                    self._volume_delete(new_vol)

            else:
                self._skip_current_test(
                    "Skip test: not support of VOLUME_RAID_CREATE")

    def test_volume_raid_info(self):
        flag_supported = False
        pool_id_to_lsm_vols = dict()
        created_lsm_vol = None

        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.VOLUME_RAID_INFO]):
                flag_supported = True
        if flag_supported is False:
            self._skip_current_test(
                "Skip test: current system does not support "
                "query volume raid info(lsm.Capabilities.VOLUME_RAID_INFO)")

        # Try to find a volume per pool.
        lsm_vols = self.c.volumes()
        for lsm_vol in lsm_vols:
            pool_id = lsm_vol.pool_id
            if len(pool_id) != 0 and \
               pool_id_to_lsm_vols.get(pool_id) is None:
                pool_id_to_lsm_vols[pool_id] = lsm_vol

        lsm_vols = list(pool_id_to_lsm_vols.values())
        created_lsm_vol = self._volume_create(s.id)[0]
        lsm_vols.append(created_lsm_vol)

        for lsm_vol in lsm_vols:
            [raid_type, strip_size, disk_count, min_io_size, opt_io_size] = \
                self.c.volume_raid_info(lsm_vol)

        # Test NOT_FOUND_VOLUME error.
        self._volume_delete(created_lsm_vol)
        try:
            self.c.volume_raid_info(lsm_vol)
        except lsm.LsmError as le:
            if le.code != ErrorNumber.NOT_FOUND_VOLUME:
                raise

    def test_volume_ident_led_on(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.VOLUME_LED]):
                for volume in self.c.volumes():
                    volume_led_status = self.c.volume_ident_led_on(volume)
                    self.assertTrue(volume_led_status is None,
                                    "Volume ident_led_on"
                                    "failed")

    def test_volume_ident_led_off(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.VOLUME_LED]):
                for volume in self.c.volumes():
                    volume_led_status = self.c.volume_ident_led_off(volume)
                    self.assertTrue(volume_led_status is None,
                                    "Volume ident_led_off"
                                    "failed")

    def test_invalid_uri(self):

        # Make sure we are getting an exception
        self.assertRaises(lsm.LsmError, lsm.Client,
                          "ontap//root@na-sim", "some_password")

        # Make sure exception has the correct error code
        try:
            lsm.Client("ontap//root@na-sim", "some_password")
        except lsm.LsmError as le:
            self.assertTrue(le.code == lsm.ErrorNumber.INVALID_ARGUMENT)

    def test_battery_list(self):
        for s in self.systems:
            cap = self.c.capabilities(s)
            if supported(cap, [Cap.BATTERIES]):
                self.c.batteries()

    def test_volume_cache_info(self):
        flag_tested = False
        flag_created = False
        for s in self.systems:
            cap = self.c.capabilities(s)
            if not supported(cap, [Cap.VOLUME_CACHE_INFO]):
                continue

            lsm_vols = self.c.volumes(search_key='system_id',
                                      search_value=s.id)
            if len(lsm_vols) == 0:
                if not supported(cap, [Cap.VOLUME_CREATE, Cap.VOLUME_DELETE]):
                    continue
                lsm_vol = self._volume_create(s.id)[0]
                flag_created = True
            else:
                lsm_vol = lsm_vols[0]
            cache_info = self.c.volume_cache_info(lsm_vol)
            self.assertTrue(len(cache_info) == 5)
            if flag_created:
                self._volume_delete(lsm_vol)
            flag_tested = True

        if flag_tested is False:
            self._skip_current_test(
                "Skip test: no storage system support volume cache info query")

    def test_volume_cache_pdc_update(self):
        flag_tested = False
        for s in self.systems:
            cap = self.c.capabilities(s)
            if not supported(cap,
                             [Cap.VOLUME_CACHE_INFO, Cap.VOLUME_CREATE,
                              Cap.VOLUME_DELETE,
                              Cap.VOLUME_PHYSICAL_DISK_CACHE_UPDATE]):
                continue
            lsm_vol = self._volume_create(s.id)[0]
            self.c.volume_physical_disk_cache_update(
                lsm_vol, lsm.Volume.PHYSICAL_DISK_CACHE_ENABLED)
            cache_info = self.c.volume_cache_info(lsm_vol)
            self.assertTrue(cache_info[4] ==
                            lsm.Volume.PHYSICAL_DISK_CACHE_ENABLED)
            self.c.volume_physical_disk_cache_update(
                lsm_vol, lsm.Volume.PHYSICAL_DISK_CACHE_DISABLED)
            cache_info = self.c.volume_cache_info(lsm_vol)
            self.assertTrue(cache_info[4] ==
                            lsm.Volume.PHYSICAL_DISK_CACHE_DISABLED)

            self._volume_delete(lsm_vol)
            flag_tested = True
        if flag_tested is False:
            self._skip_current_test(
                "Skip test: current system does not support required "
                "capabilities for testing volume_physical_disk_cache_update()")

    def test_volume_cache_wcp_update(self):
        flag_tested = False
        for s in self.systems:
            cap = self.c.capabilities(s)
            if not supported(cap,
                             [Cap.VOLUME_CACHE_INFO, Cap.VOLUME_CREATE,
                              Cap.VOLUME_DELETE]):
                continue
            lsm_vol = self._volume_create(s.id)[0]

            if supported(cap,
                         [Cap.VOLUME_WRITE_CACHE_POLICY_UPDATE_WRITE_BACK]):
                self.c.volume_write_cache_policy_update(
                    lsm_vol, lsm.Volume.WRITE_CACHE_POLICY_WRITE_BACK)
                cache_info = self.c.volume_cache_info(lsm_vol)
                self.assertTrue(cache_info[0] ==
                                lsm.Volume.WRITE_CACHE_POLICY_WRITE_BACK)
                flag_tested = True

            if supported(cap, [Cap.VOLUME_WRITE_CACHE_POLICY_UPDATE_AUTO]):
                self.c.volume_write_cache_policy_update(
                    lsm_vol, lsm.Volume.WRITE_CACHE_POLICY_AUTO)
                cache_info = self.c.volume_cache_info(lsm_vol)
                self.assertTrue(cache_info[0] ==
                                lsm.Volume.WRITE_CACHE_POLICY_AUTO)
                flag_tested = True

            if supported(cap,
                         [Cap.VOLUME_WRITE_CACHE_POLICY_UPDATE_WRITE_THROUGH]):
                self.c.volume_write_cache_policy_update(
                    lsm_vol, lsm.Volume.WRITE_CACHE_POLICY_WRITE_THROUGH)
                cache_info = self.c.volume_cache_info(lsm_vol)
                self.assertTrue(cache_info[0] ==
                                lsm.Volume.WRITE_CACHE_POLICY_WRITE_THROUGH)
                flag_tested = True

            self._volume_delete(lsm_vol)

        if flag_tested is False:
            self._skip_current_test(
                "Skip test: current system does not support required "
                "capabilities for testing volume_write_cache_policy_update()")

    def test_volume_cache_rcp_update(self):
        flag_tested = False
        for s in self.systems:
            cap = self.c.capabilities(s)
            if not supported(cap,
                             [Cap.VOLUME_CACHE_INFO, Cap.VOLUME_CREATE,
                              Cap.VOLUME_DELETE,
                              Cap.VOLUME_READ_CACHE_POLICY_UPDATE]):
                continue
            lsm_vol = self._volume_create(s.id)[0]
            self.c.volume_read_cache_policy_update(
                lsm_vol, lsm.Volume.READ_CACHE_POLICY_ENABLED)
            cache_info = self.c.volume_cache_info(lsm_vol)
            self.assertTrue(cache_info[2] ==
                            lsm.Volume.READ_CACHE_POLICY_ENABLED)
            self.c.volume_read_cache_policy_update(
                lsm_vol, lsm.Volume.READ_CACHE_POLICY_DISABLED)
            cache_info = self.c.volume_cache_info(lsm_vol)
            self.assertTrue(cache_info[2] ==
                            lsm.Volume.READ_CACHE_POLICY_DISABLED)

            self._volume_delete(lsm_vol)
            flag_tested = True
        if flag_tested is False:
            self._skip_current_test(
                "Skip test: current system does not support required "
                "capabilities for testing volume_read_cache_policy_update()")


def dump_results():
    """
    unittest.main exits when done so we need to register this handler to
    get our results out.

    If PyYAML is available we will output detailed results, else we will
    output nothing.  The detailed output results of what we called,
    how it finished and how long it took.
    """
    try:
        import yaml
        sys.stdout.write(yaml.dump(dict(methods_called=results, stats=stats)))
    except ImportError:
        sys.stdout.write("NOTICE: Install PyYAML for detailed test results\n")


def add_our_params():
    """
    There are probably easier ways to extend unittest, but this seems
    easiest at the moment if we want to retain the default behavior and
    introduce a couple of parameters.
    """
    additional = """\

Options libStorageMgmt:
 --password  'Array password'
 --uri       'Array URI'
 --skip      'Test case to skip. Repeatable argument'
 """

    if sys.version_info[0] == 2:
        unittest.TestProgram.USAGE += additional

if __name__ == "__main__":
    atexit.register(dump_results)
    add_our_params()

    parser = argparse.ArgumentParser(add_help=False)
    parser.add_argument('--password', default=None)
    parser.add_argument('--uri')
    parser.add_argument('--skip', action='append')
    options, other_args = parser.parse_known_args()

    if options.uri:
        TestPlugin.URI = options.uri
    elif os.getenv('LSM_TEST_URI'):
        TestPlugin.URI = os.getenv('LSM_TEST_URI')
    else:
        TestPlugin.URI = 'sim://'

    if options.password:
        TestPlugin.PASSWORD = options.password
    elif os.getenv('LSM_TEST_PASSWORD'):
        TestPlugin.PASSWORD = os.getenv('LSM_TEST_PASSWORD')

    if options.skip:
        if hasattr(unittest.TestCase, 'skipTest') is False:
            raise Exception(
                "Current python version is too old to support 'skipTest'")
        TestPlugin.SKIP_TEST_CASES = options.skip
    else:
        TestPlugin.SKIP_TEST_CASES = []

    unittest.main(argv=sys.argv[:1] + other_args)
