#!/bin/env python

# Copyright (C) 2013 Red Hat, Inc.
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
# Author: tasleson AT redhat DOT com

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

results = {}
stats = {}


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


def rs(component, l=8):
    """
    Generate a random string
    """
    return 'lsm_%s_' % component + ''.join(
        random.choice(string.ascii_uppercase) for x in range(l))


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


class TestProxy(object):

    # Hash of all calls that can be async
    async_calls = {'pool_create': (unicode, lsm.Pool),
                   'pool_create_from_disks': (unicode, lsm.Pool),
                   'pool_create_from_volumes': (unicode, lsm.Pool),
                   'pool_create_from_pool': (unicode, lsm.Pool),
                   'pool_delete': (unicode,),
                   'volume_create': (unicode, lsm.Volume),
                   'volume_resize': (unicode, lsm.Volume),
                   'volume_replicate': (unicode, lsm.Volume),
                   'volume_replicate_range': (unicode,),
                   'volume_delete': (unicode,),
                   'volume_child_dependency_rm': (unicode),
                   'fs_delete': (unicode,),
                   'fs_resize': (unicode,),
                   'fs_create': (unicode,),
                   'fs_clone': (unicode,),
                   'file_clone': (unicode,),
                   'fs_snapshot_create': (unicode, lsm.Snapshot),
                   'fs_snapshot_delete': (unicode,),
                   'fs_snapshot_revert': (unicode,),
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

        # Check to see if we have a place to store result
        if not _proxy_method_name in results:
            results[_proxy_method_name] = []

        # Timer block
        with Duration() as method_time:
            try:
                rc = getattr(self.o, _proxy_method_name)(*args, **kwargs)
                results[_proxy_method_name].append(dict(rc=True,
                                                        stack_trace=None,
                                                        msg=None))
            except Exception as e:
                results[_proxy_method_name].append(
                    dict(rc=False,
                         stack_trace=traceback.format_exc(),
                         msg=str(e)))
                raise e

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

    def setUp(self):
        self.c = TestProxy(lsm.Client(TestPlugin.URI, TestPlugin.PASSWORD))
        # TODO Store what exists, so that we don't remove it

    def tearDown(self):
        # TODO Walk the array looking for stuff we have created and remove it
        # What should we do if an array supports a create operation, but not
        # the corresponding remove?
        self.c.close()

    def test_timeout(self):
        tmo = 40000
        self.c.set_time_out(tmo)
        self.assertEquals(self.c.get_time_out(), tmo)

    def test_systems_list(self):
        arrays = self.c.systems()
        self.assertTrue(len(arrays) > 0)

    def test_pools_list(self):
        pools_list = self.c.pools()

    def test_disks_list(self):
        disks = self.c.disks()

    def test_pool_create(self):
        pass

    def test_volume_create_delete(self):
        systems = self.c.systems()
        pools = self.c.pools()

        p_dict = dict((p.system_id, p) for p in pools)

        for s in systems:
            vol = None
            cap = self.c.capabilities(s)
            if cap.get(lsm.Capabilities.VOLUME_CREATE):

                if s.id in p_dict:
                    pool = p_dict[s.id]

                    vol_size = min(pool.free_space / 10, mb_in_bytes(512))

                    vol = self.c.volume_create(pool, rs('volume'), vol_size,
                                               lsm.Volume.PROVISION_DEFAULT)[1]

            if cap.get(lsm.Capabilities.VOLUME_DELETE):
                self.c.volume_delete(vol)


def dump_results():
    """
    unittest.main exits when done so we need to register this handler to
    get our results out.

    output details (yaml) results of what we called, how it finished and how
    long it tool.
    """
    sys.stderr.write(yaml.dump(dict(methods_called=results, stats=stats)))


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
