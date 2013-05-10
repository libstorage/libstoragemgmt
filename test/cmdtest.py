#!/usr/bin/env python

# Copyright (C) 2011-2013 Red Hat, Inc.
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301,
# USA.
#
# Author: tasleson

#Description:   Query array capabilities and run very basic operational tests.
#
# Note: This file is GPL copyright and not LGPL because:
# 1. It is used to test the library, not provide functionality for it.
# 2. It uses a function copied from anaconda library which is GPLv2 or later,
#    thus this code must be GPL as well.

import random
import string
import sys
import hashlib
import os
from subprocess import Popen, PIPE
from optparse import OptionParser


(OP_SYS, OP_POOL, OP_VOL, OP_INIT, OP_FS, OP_EXPORTS, OP_SS) = \
    ('SYSTEMS', 'POOLS', 'VOLUMES', 'INITIATORS', 'FS', 'EXPORTS', 'SNAPSHOTS')

(ID, NAME) = (0, 1)
(POOL_TOTAL, POOL_FREE, POOL_SYSTEM) = (2, 3, 4)
(VOL_VPD, VOL_BS, VOL_BLOCKS, VOL_STATUS, VOL_SIZE) = (2, 3, 4, 5, 6)
(INIT_TYPE) = 2
(FS_TOTAL, FS_FREE, FS_POOL_ID) = (2, 3, 4)

(SYS_STATUS,) = (2,)

iqn = ['iqn.1994-05.com.domain:01.89bd01', 'iqn.1994-05.com.domain:01.89bd02']

cmd = "lsmcli"

sep = ","
test_pool_name = 'lsm_test_aggr'
test_pool_id = None

CUR_SYS_ID = None


def randomIQN():
    """Logic taken from anaconda library"""

    s = "iqn.1994-05.com.domain:01."
    m = hashlib.md5()
    u = os.uname()
    for i in u:
        m.update(i)
    dig = m.hexdigest()

    for i in range(0, 6):
        s += dig[random.randrange(0, 32)]
    return s


def rs(l):
    """
    Generate a random string
    """
    return 'lsm_' + ''.join(
        random.choice(string.ascii_uppercase) for x in range(l))


def call(cmd, expected_rc=0):
    """
    Call an executable and return a tuple of exitcode, stdout, stderr
    """
    print cmd

    c = Popen(cmd, stdout=PIPE, stderr=PIPE)
    out = c.communicate()

    if c.returncode != expected_rc:
        raise RuntimeError("exit code != %s, actual= %s, stdout= %s, stderr= %s"
                           % (expected_rc, c.returncode, out[0], out[1]))

    return c.returncode, out[0], out[1]


def parse(out):
    rc = []
    for line in out.split('\n'):
        elem = line.split(sep)
        if len(elem) > 1:
            rc.append(elem)
    return rc


def parse_display(op):
    rc = []
    out = call([cmd, '-l', op, '-t' + sep])[1]
    for line in out.split('\n'):
        elem = line.split(sep)
        if len(elem) > 1:
            rc.append(elem)
    return rc


def name_to_id(op, name):
    out = parse_display(op)

    for i in out:
        if i[NAME] == name:
            return i[ID]
    return None


def delete_init(id):
    call([cmd, '--delete-initiator', id])


def create_volume(pool):
    out = call([cmd, '--create-volume', rs(12), '--size', '30M', '--pool',
                pool, '-t' + sep, '--provisioning', 'DEFAULT'])[1]
    r = parse(out)
    return r[0][ID]


def delete_volume(id):
    call([cmd, '--delete-volume', id, '-t' + sep, '-f'])


def create_fs(pool_id):
    out = call([cmd, '--create-fs', rs(12), '--size', '500M', '--pool',
                pool_id, '-t' + sep])[1]
    r = parse(out)
    return r[0][ID]


def delete_fs(id):
    call([cmd, '--delete-fs', id, '-t' + sep, '-f'])


def create_access_group(iqn, system_id):
    out = call([cmd, '--create-access-group', rs(8), '--id', iqn,
                '--type', 'ISCSI', '-t' + sep, '--system', system_id])[1]
    r = parse(out)
    return r[0][ID]


def access_group_add_init(group, initiator):
    call([cmd, '--access-group-add', group, '--id', initiator, '--type',
          'ISCSI'])


def access_group_remove_init(group, initiator):
    call([cmd, '--access-group-remove', group, '--id', initiator])


def delete_access_group(id):
    call([cmd, '--delete-access-group', id, '-t' + sep])


def access_group_grant(group, volume_id):
    call([cmd, '--access-grant-group', group, '--volume', volume_id, '--access',
          'RW'])


def access_group_revoke(group, volume_id):
    call([cmd, '--access-revoke-group', group, '--volume', volume_id])


def resize_vol(id):
    call([cmd, '--resize-volume', id, '--size', '60M', '-t' + sep, '-f'])
    call([cmd, '--resize-volume', id, '--size', '100M', '-t' + sep, '-f'])
    #Some devices cannot re-size down...
    #call([cmd, '--resize-volume', id, '--size', '30M' , '-t'+sep ])


def resize_fs(id):
    call([cmd, '--resize-fs', id, '--size', '1G', '-t' + sep, '-f'])
    call([cmd, '--resize-fs', id, '--size', '750M', '-t' + sep, '-f'])
    call([cmd, '--resize-fs', id, '--size', '300M', '-t' + sep, '-f'])


def map(init, volume):
    call([cmd, '--access-grant', init, '--volume', volume, '--access',
          'RW', '-t' + sep])


def unmap(init, volume):
    call([cmd, '--access-revoke', init, '--volume', volume])


def clone_fs(fs_id):
    out = call([cmd, '--clone-fs', fs_id, '--name', 'cloned_' + rs(8),
                '-t' + sep])[1]
    r = parse(out)
    return r[0][ID]


def create_ss(fs_id):
    out = call([cmd, '--create-ss', rs(12), '--fs', fs_id, '-t' + sep])[1]
    r = parse(out)
    return r[0][ID]


def delete_ss(fs_id, ss_id):
    call([cmd, '--delete-ss', ss_id, '--fs', fs_id, '-f'])


def replicate_volume(source_id, type, pool):
    out = call([cmd, '-r', source_id, '--type', type,
                '--name', 'lun_' + type + '_' + rs(12), '-t' + sep])[1]
    r = parse(out)
    return r[0][ID]


def replicate_volume_range_bs(system_id):
    """
    Returns the replicated range block size.
    """
    out = call([cmd, '--replicate-volume-range-block-size', system_id])[1]
    return int(out)


def replicate_volume_range(vol_id, dest_vol_id, rep_type, src_start, dest_start,
                           count):
    out = call(
        [cmd, '--replicate-volume-range', vol_id, '--type', rep_type, '--dest',
         dest_vol_id, '--src_start', str(src_start), '--dest_start',
         str(dest_start),
         '--count', str(count), '-f'])


def get_systems():
    out = call([cmd, '-l', 'SYSTEMS', '-t' + sep])[1]
    systems = parse(out)
    return systems


def initiator_grant(iqn, vol_id):
#initiator_grant(self, initiator_id, initiator_type, volume, access,
#   flags = 0):
    call([cmd, '--access-grant', iqn, '--type', 'ISCSI', '--volume', vol_id,
          '--access', 'RW'])


def initiator_chap(initiator):
    call([cmd, '--iscsi-chap', initiator])
    call([cmd, '--iscsi-chap', initiator, '--in-user', "foo", '--in-password', "bar"])
    call([cmd, '--iscsi-chap', initiator, '--in-user', "foo", '--in-password', "bar",
          '--out-user', "foo", '--out-password', "bar"])


def initiator_revoke(iqn, vol_id):
    call([cmd, '--access-revoke', iqn, '--volume', vol_id])


def capabilities(system_id):
    """
    Return a hash table of key:bool where key is supported operation
    """
    rc = {}
    out = call([cmd, '--capabilities', system_id, '-t' + sep])[1]
    results = parse(out)

    for r in results:
        rc[r[0]] = True if r[1] == 'SUPPORTED' else False
    return rc


def get_existing_fs(system_id):
    out = ( [cmd, '-l', 'FS', '-t' + sep])[1]
    results = parse(out)
    print 'fs results=', results


def numbers():
    vols = []
    for i in range(10):
        vols.append(create_volume(test_pool_id))

    for i in vols:
        delete_volume(i)


def display_check(display_list, system_id):
    s = [x for x in display_list if x != 'SNAPSHOTS']
    for p in s:
        call([cmd, '-l', p])
        call([cmd, '-l', p, '-H'])
        call([cmd, '-l', p, '-H', '-t' + sep])

    if 'SNAPSHOTS' in display_list:
        fs_id = get_existing_fs(system_id)


def test_display(cap, system_id):
    """
    Crank through supported display operations making sure we get good
    status for each of them
    """
    to_test = ['SYSTEMS']

    if cap['BLOCK_SUPPORT']:
        to_test.append('POOLS')
        to_test.append('VOLUMES')

    if cap['FS_SUPPORT'] and cap['FS']:
        to_test.append("FS")

    if cap['INITIATORS']:
        to_test.append("INITIATORS")

    if cap['EXPORTS']:
        to_test.append("EXPORTS")

    if cap['ACCESS_GROUP_LIST']:
        to_test.append("ACCESS_GROUPS")

    if cap['FS_SNAPSHOTS']:
        to_test.append('SNAPSHOTS')

    if cap['EXPORT_AUTH']:
        to_test.append('NFS_CLIENT_AUTH')

    display_check(to_test, system_id)


def test_block_creation(cap, system_id):
    vol_src = None
    test_pool_id = name_to_id(OP_POOL, test_pool_name)

    if cap['VOLUME_CREATE']:
        vol_src = create_volume(test_pool_id)

    if cap['VOLUME_RESIZE']:
        resize_vol(vol_src)

    if cap['VOLUME_REPLICATE'] and cap['VOLUME_DELETE']:
        if cap['VOLUME_REPLICATE_CLONE']:
            clone = replicate_volume(vol_src, 'CLONE', test_pool_id)
            delete_volume(clone)

        if cap['VOLUME_REPLICATE_COPY']:
            copy = replicate_volume(vol_src, 'COPY', test_pool_id)
            delete_volume(copy)

        if cap['VOLUME_REPLICATE_MIRROR_ASYNC']:
            m = replicate_volume(vol_src, 'MIRROR_ASYNC', test_pool_id)
            delete_volume(m)

        if cap['VOLUME_REPLICATE_MIRROR_SYNC']:
            m = replicate_volume(vol_src, 'MIRROR_SYNC', test_pool_id)
            delete_volume(m)

        if cap['VOLUME_COPY_RANGE_BLOCK_SIZE']:
            size = replicate_volume_range_bs(system_id)
            print 'sub volume replication block size is=', size

        if cap['VOLUME_COPY_RANGE']:
            if cap['VOLUME_COPY_RANGE_CLONE']:
                replicate_volume_range(vol_src, vol_src, "CLONE", 0, 10000, 100)

            if cap['VOLUME_COPY_RANGE_COPY']:
                replicate_volume_range(vol_src, vol_src, "COPY", 0, 10000, 100)

    if cap['VOLUME_DELETE']:
        delete_volume(vol_src)


def test_fs_creation(cap, system_id):
    pool_id = name_to_id(OP_POOL, test_pool_name)

    if cap['FS_CREATE']:
        fs_id = create_fs(pool_id)

        if cap['FS_RESIZE']:
            resize_fs(fs_id)

        if cap['FS_DELETE']:
            delete_fs(fs_id)

    if cap['FS_CLONE']:
        fs_id = create_fs(pool_id)
        clone = clone_fs(fs_id)
        test_display(cap, system_id)
        delete_fs(clone)
        delete_fs(fs_id)

    if cap['FS_SNAPSHOT_CREATE'] and cap['FS_CREATE'] and cap['FS_DELETE'] \
        and cap['FS_SNAPSHOT_DELETE']:
        #Snapshot create/delete
        id = create_fs(pool_id)
        ss = create_ss(id)
        test_display(cap, system_id)
        delete_ss(id, ss)
        delete_fs(id)


def test_mapping(cap, system_id):
    pool_id = name_to_id(OP_POOL, test_pool_name)
    iqn1 = randomIQN()
    iqn2 = randomIQN()

    if cap['ACCESS_GROUP_CREATE']:
        id = create_access_group(iqn1, system_id)

        if cap['ACCESS_GROUP_ADD_INITIATOR']:
            access_group_add_init(id, iqn2)

        if cap['ACCESS_GROUP_GRANT'] and cap['VOLUME_CREATE']:
            vol = create_volume(pool_id)
            access_group_grant(id, vol)

            test_display(cap, system_id)

            if cap['ACCESS_GROUP_REVOKE']:
                access_group_revoke(id, vol)

            if cap['VOLUME_DELETE']:
                delete_volume(vol)

            if cap['ACCESS_GROUP_DEL_INITIATOR']:
                access_group_remove_init(id, iqn1)
                access_group_remove_init(id, iqn2)

            if cap['ACCESS_GROUP_DELETE']:
                delete_access_group(id)

    if cap['VOLUME_INITIATOR_GRANT']:
        vol_id = create_volume(pool_id)
        initiator_grant(iqn1, vol_id)

        test_display(cap, system_id)

        if cap['VOLUME_ISCSI_CHAP_AUTHENTICATION']:
            initiator_chap(iqn1)


        if cap['VOLUME_INITIATOR_REVOKE']:
            initiator_revoke(iqn1, vol_id)

        if cap['VOLUME_DELETE']:
            delete_volume(vol_id)


def create_all(cap, system_id):
    test_block_creation(cap, system_id)
    test_fs_creation(cap, system_id)


def run_all_tests(cap, system_id):
    test_display(cap, system_id)

    create_all(cap, system_id)
    test_mapping(cap, system_id)


if __name__ == "__main__":
    parser = OptionParser()
    parser.add_option("-c", "--command", action="store", type="string",
                      dest="cmd", help="specific command line to test")
    parser.add_option("-p", "--pool", action="store", dest="pool_name",
                      default='lsm_test_aggr',
                      help="pool name to use for testing")

    parser.description = "lsmcli command line test tool"

    (options, args) = parser.parse_args()

    if options.cmd is None:
        print 'Please specify which lsmcli to test using -c or --command'
        sys.exit(1)
    else:
        cmd = options.cmd
        test_pool_name = options.pool_name

    #Theory of testing.
    # For each system that is available to us:
    #   Query capabilities
    #       Query all supported query operations (should have more to query)
    #
    #       Create objects of every supported type
    #           Query all supported query operations (should have more to query),
    #           run though different options making sure nothing explodes!
    #
    #       Try calling un-supported operations and expect them to fail
    systems = get_systems()

    for s in systems:
        cap = capabilities(s[ID])
        run_all_tests(cap, s[ID])
