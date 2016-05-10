#!/usr/bin/env python2

# Copyright (C) 2011-2014 Red Hat, Inc.
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
# along with this program; If not, see <http://www.gnu.org/licenses/>.
# USA.
#
# Author: tasleson

# Description:   Query array capabilities and run very basic operational tests.
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

(OP_SYS, OP_POOL, OP_VOL, OP_FS, OP_EXPORTS, OP_SS) = \
    ('SYSTEMS', 'POOLS', 'VOLUMES', 'FS', 'EXPORTS',
     'SNAPSHOTS')

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
test_fs_pool_id = ''
test_disk_id = 'DISK_ID_00000'

CUR_SYS_ID = None

code_coverage = bool(os.getenv('LSM_PYTHON_COVERAGE', False))


def random_iqn():
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


def call(command, expected_rc=0, expected_rcs=None):
    """
    Call an executable and return a tuple of exitcode, stdout, stderr
    """

    if code_coverage:
        actual_command = ['coverage', 'run', '-o']
        actual_command.extend(command)
    else:
        actual_command = command

    expected_rcs_str = ""

    if expected_rcs is None:
        print actual_command, 'EXPECTED Exit [%d]' % expected_rc
    else:
        expected_rcs_str = " ".join(str(x) for x in expected_rcs)
        print(actual_command, 'EXPECTED Exit codes [%s]' % expected_rcs)

    process = Popen(actual_command, stdout=PIPE, stderr=PIPE)
    out = process.communicate()

    if process.returncode != expected_rc and \
       expected_rcs is not None and \
       process.returncode not in expected_rcs:
        if expected_rcs is None:
            raise RuntimeError("exit code != %s, actual= %s, stdout= %s, "
                               "stderr= %s" % (expected_rc, process.returncode,
                                               out[0], out[1]))
        raise RuntimeError("exit code not in one of '%s', "
                           "actual= %s, stdout= %s, stderr= %s" %
                           (expected_rcs_str, process.returncode, out[0],
                            out[1]))
    return process.returncode, out[0], out[1]


def parse(out):
    rc = []
    for line in out.split('\n'):
        elem = line.split(sep)
        cleaned_elem = []
        for e in elem:
            e = e.strip()
            cleaned_elem.append(e)

        if len(cleaned_elem) > 1:
            rc.append(cleaned_elem)
    return rc


def parse_key_value(out):
    rc = []
    for line in out.split('\n'):
        elem = line.split(sep)
        if len(elem) > 1:
            item = dict()

            for i in range(0, len(elem), 2):
                key = elem[i].strip()
                value = elem[i + 1].strip()
                item[key] = value

            rc.append(item)
    return rc


def parse_display(op):
    rc = []
    out = call([cmd, '-t' + sep, 'list', '--type', op])[1]
    for line in out.split('\n'):
        elem = line.split(sep)
        if len(elem) > 1:
            rc.append(list(d.strip() for d in elem))
    return rc


def name_to_id(op, name):
    out = parse_display(op)

    for i in out:
        if i[NAME] == name:
            return i[ID]
    return None


def create_volume(pool):
    out = call([cmd, '-t' + sep, 'volume-create', '--name', rs(12), '--size',
                '30M', '--pool', pool, '--provisioning', 'DEFAULT'])[1]
    r = parse(out)
    return r[0][ID]


def volume_delete(vol_id):
    call([cmd, '-t' + sep, '-f', 'volume-delete', '--vol', vol_id])


def fs_create(pool_id):
    out = call([cmd, '-t' + sep, 'fs-create', '--name', rs(12), '--size',
                '500M', '--pool', pool_id])[1]
    r = parse(out)
    return r[0][ID]


def export_fs(fs_id):
    out = call([cmd, '-t' + sep,
                'fs-export',
                '--fs', fs_id,
                '--rw-host', '192.168.0.1',
                '--root-host', '192.168.0.1', '--script'])[1]

    r = parse_key_value(out)
    return r[0]['ID']


def un_export_fs(export_id):
    call([cmd, 'fs-unexport', '--export', export_id])


def delete_fs(fs_id):
    call([cmd, '-t' + sep, '-f', 'fs-delete', '--fs', fs_id])


def access_group_create(init_id, system_id):
    out = call([cmd, '-t' + sep, 'access-group-create', '--name', rs(8),
                '--init', init_id, '--sys', system_id])[1]
    r = parse(out)
    return r[0][ID]


def access_group_initiator_add(group, initiator):
    call([cmd, 'access-group-add', '--ag', group, '--init', initiator])


def access_group_remove_init(group, initiator):
    call([cmd, 'access-group-remove', '--ag', group, '--init', initiator])


def access_group_delete(group_id):
    call([cmd, '-t' + sep, 'access-group-delete', '--ag', group_id])


def volume_mask(group, volume_id):
    call([cmd, 'volume-mask', '--ag', group, '--vol', volume_id])


def volume_unmask(group, volume_id):
    call([cmd, 'volume-unmask', '--ag', group, '--vol', volume_id])


def volumes_accessible_by_access_group(ag_id):
    call([cmd, 'list', '--type', 'volumes', '--ag', ag_id])


def access_groups_granted_to_volume(vol_id):
    call([cmd, 'list', '--type', 'access_groups', '--vol', vol_id])


def resize_vol(vol_id):
    call([cmd, '-t' + sep, '-f',
          'volume-resize',
          '--vol', vol_id,
          '--size', '60M'])
    call([cmd, '-t' + sep, '-f',
          'volume-resize',
          '--vol', vol_id,
          '--size', '100M'])
    # Some devices cannot re-size down...
    # call([cmd, '--volume-resize', id, '--size', '30M' , '-t'+sep ])


def resize_fs(fs_id):
    call([cmd, '-t' + sep, '-f',
          'fs-resize',
          '--fs', fs_id,
          '--size', '1G'])
    call([cmd, '-t' + sep, '-f',
          'fs-resize',
          '--fs', fs_id,
          '--size', '750M'])
    call([cmd, '-t' + sep, '-f',
          'fs-resize',
          '--fs', fs_id,
          '--size', '300M'])


def map_init(init, volume):
    call([cmd, '-t' + sep, 'access-grant', '--init', init, '--vol', volume,
          '--access', 'RW'])


def unmap(init, volume):
    call([cmd, 'access-revoke', '--init', init, '--vol', volume])


def clone_fs(fs_id):
    # TODO Change to --source_id instead of --source_name ?
    out = call([cmd, '-t' + sep, 'fs-clone', '--src-fs', fs_id,
                '--dst-name', 'cloned_' + rs(8)])[1]
    r = parse(out)
    return r[0][ID]


def fs_child_dependancy(fs_id):
    call([cmd, 'fs-dependants', '--fs', fs_id])


def fs_child_dependancy_rm(fs_id):
    call([cmd, 'fs-dependants-rm', '--fs', fs_id])


def clone_file(fs_id):
    # TODO Make this work outside of the simulator
    call([cmd, 'file-clone', '--fs', fs_id, '--src', 'foo', '--dst', 'bar'])


def create_ss(fs_id):
    out = call([cmd, '-t' + sep, 'fs-snap-create', '--name', rs(12), '--fs',
                fs_id])[1]
    r = parse(out)
    return r[0][ID]


def delete_ss(fs_id, ss_id):
    call([cmd, '-f', 'fs-snap-delete', '--snap', ss_id, '--fs', fs_id])


def restore_ss(snapshot_id, fs_id):
    call([cmd, '-f', 'fs-snap-restore', '--snap', snapshot_id, '--fs', fs_id])


def volume_replicate(source_id, vol_type, pool=None):
    out = call([cmd,
                '-t' + sep,
                'volume-replicate',
                '--vol', source_id,
                '--rep-type', vol_type,
                '--name', 'lun_' + vol_type + '_' + rs(12)])[1]
    r = parse(out)
    return r[0][ID]


def volume_replicate_range_bs(system_id):
    """
    Returns the replicated range block size.
    """
    out = call([cmd,
                'volume-replicate-range-block-size',
                '--sys', system_id])[1]
    return int(out)


def volume_replicate_range(vol_id, dest_vol_id, rep_type, src_start,
                           dest_start, count):
    out = call(
        [cmd, '-f', 'volume-replicate-range',
            '--src-vol', vol_id,
            '--rep-type', rep_type,
            '--dst-vol', dest_vol_id,
            '--src-start', str(src_start),
            '--dst-start', str(dest_start),
            '--count', str(count)])


def volume_child_dependency(vol_id):
    call([cmd, 'volume-dependants', '--vol', vol_id])


def volume_child_dependency_rm(vol_id):
    call([cmd, 'volume-dependants-rm', '--vol', vol_id])


def get_systems():
    out = call([cmd, '-t' + sep, 'list', '--type', 'SYSTEMS'])[1]
    system_list = parse(out)
    return system_list


def system_read_cache_pct_update_test(cap):
    if cap['SYS_READ_CACHE_PCT_UPDATE']:
        out = call([cmd, '-t' + sep, '--type', 'SYSTEMS'])[1]
        system_list = parse(out)
        for system in system_list:
            out = call([
                cmd, '-t' + sep, 'system-read-cache-pct-update', '--system',
                system[0], 50])[1]

    return


def initiator_chap(initiator):
    call([cmd, 'iscsi-chap',
          '--init', initiator])
    call([cmd, 'iscsi-chap',
          '--init', initiator,
          '--in-user', "foo",
          '--in-pass', "bar"])
    call([cmd, 'iscsi-chap',
          '--init', initiator, '--in-user', "foo",
          '--in-pass', "bar", '--out-user', "foo",
          '--out-pass', "bar"])


def capabilities(system_id):
    """
    Return a hash table of key:bool where key is supported operation
    """
    rc = {}
    out = call([cmd, '-t' + sep, 'capabilities', '--sys', system_id])[1]
    results = parse(out)

    for r in results:
        rc[r[0]] = True if r[1] == 'SUPPORTED' else False
    return rc


def get_existing_fs(system_id):
    out = call([cmd, '-t' + sep, 'list', '--type', 'FS', ])[1]
    results = parse(out)

    if len(results) > 0:
        return results[0][ID]
    return None


def numbers():
    vols = []
    test_pool_id = name_to_id(OP_POOL, test_pool_name)

    for i in range(10):
        vols.append(create_volume(test_pool_id))

    for i in vols:
        volume_delete(i)


def display_check(display_list, system_id):
    s = [x for x in display_list if x != 'SNAPSHOTS']
    for p in s:
        call([cmd, 'list', '--type', p])
        call([cmd, '-H', 'list', '--type', p, ])
        call([cmd, '-H', '-t' + sep, 'list', '--type', p])

    if 'SNAPSHOTS' in display_list:
        fs_id = get_existing_fs(system_id)
        if fs_id:
            call([cmd, 'list', '--type', 'SNAPSHOTS', '--fs', fs_id])

    if 'POOLS' in display_list:
        call([cmd, '-H', '-t' + sep, 'list', '--type', 'POOLS'])


def test_exit_code(cap, system_id):
    """
    Make sure we get the expected exit code when the command syntax is wrong
    """
    call([cmd, '-u'], 2)


def test_display(cap, system_id):
    """
    Crank through supported display operations making sure we get good
    status for each of them
    """
    to_test = ['SYSTEMS', 'POOLS']

    if cap['VOLUMES']:
        to_test.append('VOLUMES')

    if cap['FS']:
        to_test.append("FS")

    if cap['EXPORTS']:
        to_test.append("EXPORTS")

    if cap['ACCESS_GROUPS']:
        to_test.append("ACCESS_GROUPS")

    if cap['FS_SNAPSHOTS']:
        to_test.append('SNAPSHOTS')

    if cap['EXPORT_AUTH']:
        to_test.append('NFS_CLIENT_AUTH')

    if cap['BATTERIES']:
        to_test.append('BATTERIES')

    display_check(to_test, system_id)


def test_block_creation(cap, system_id):
    vol_src = None
    test_pool_id = name_to_id(OP_POOL, test_pool_name)

    # Fail early if no pool is available
    if test_pool_id is None:
        print 'Pool %s is not available!' % test_pool_name
        exit(10)

    if cap['VOLUME_CREATE']:
        vol_src = create_volume(test_pool_id)

    if cap['VOLUME_RESIZE']:
        resize_vol(vol_src)

    if cap['VOLUME_REPLICATE'] and cap['VOLUME_DELETE']:
        if cap['VOLUME_REPLICATE_CLONE']:
            clone = volume_replicate(vol_src, 'CLONE', test_pool_id)
            volume_delete(clone)

        if cap['VOLUME_REPLICATE_COPY']:
            copy = volume_replicate(vol_src, 'COPY', test_pool_id)
            volume_delete(copy)

        if cap['VOLUME_REPLICATE_MIRROR_ASYNC']:
            m = volume_replicate(vol_src, 'MIRROR_ASYNC', test_pool_id)
            volume_delete(m)

        if cap['VOLUME_REPLICATE_MIRROR_SYNC']:
            m = volume_replicate(vol_src, 'MIRROR_SYNC', test_pool_id)
            volume_delete(m)

        if cap['VOLUME_COPY_RANGE_BLOCK_SIZE']:
            size = volume_replicate_range_bs(system_id)
            print 'sub volume replication block size is=', size

        if cap['VOLUME_COPY_RANGE']:
            if cap['VOLUME_COPY_RANGE_CLONE']:
                volume_replicate_range(vol_src, vol_src, "CLONE",
                                       0, 10000, 100)

            if cap['VOLUME_COPY_RANGE_COPY']:
                volume_replicate_range(vol_src, vol_src, "COPY",
                                       0, 10000, 100)

    if cap['VOLUME_CHILD_DEPENDENCY']:
        volume_child_dependency(vol_src)

    if cap['VOLUME_CHILD_DEPENDENCY_RM']:
        volume_child_dependency_rm(vol_src)

    if cap['VOLUME_DELETE']:
        volume_delete(vol_src)


def test_fs_creation(cap, system_id):

    if test_fs_pool_id:
        pool_id = test_fs_pool_id
    else:
        pool_id = name_to_id(OP_POOL, test_pool_name)

    if cap['FS_CREATE']:
        fs_id = fs_create(pool_id)

        if cap['FS_RESIZE']:
            resize_fs(fs_id)

        if cap['FS_DELETE']:
            delete_fs(fs_id)

    if cap['FS_CLONE']:
        fs_id = fs_create(pool_id)
        clone = clone_fs(fs_id)
        test_display(cap, system_id)
        delete_fs(clone)
        delete_fs(fs_id)

    if cap['FILE_CLONE']:
        fs_id = fs_create(pool_id)
        clone_file(fs_id)
        test_display(cap, system_id)
        delete_fs(fs_id)

    if cap['FS_SNAPSHOT_CREATE'] and cap['FS_CREATE'] and cap['FS_DELETE'] \
            and cap['FS_SNAPSHOT_DELETE']:
        # Snapshot create/delete
        fs_id = fs_create(pool_id)
        ss = create_ss(fs_id)
        test_display(cap, system_id)
        restore_ss(ss, fs_id)
        delete_ss(fs_id, ss)
        delete_fs(fs_id)

    if cap['FS_CHILD_DEPENDENCY']:
        fs_id = fs_create(pool_id)
        fs_child_dependancy(fs_id)
        delete_fs(fs_id)

    if cap['FS_CHILD_DEPENDENCY_RM']:
        fs_id = fs_create(pool_id)
        clone_fs(fs_id)
        fs_child_dependancy_rm(fs_id)
        delete_fs(fs_id)


def test_nfs(cap, system_id):
    if test_fs_pool_id:
        pool_id = test_fs_pool_id
    else:
        pool_id = name_to_id(OP_POOL, test_pool_name)

    if cap['FS_CREATE'] and cap['EXPORT_FS'] and cap['EXPORT_REMOVE']:
        fs_id = fs_create(pool_id)
        export_id = export_fs(fs_id)
        test_display(cap, system_id)
        un_export_fs(export_id)
        delete_fs(fs_id)


def test_mapping(cap, system_id):
    pool_id = name_to_id(OP_POOL, test_pool_name)
    iqn1 = random_iqn()
    iqn2 = random_iqn()

    if cap['ACCESS_GROUP_CREATE_ISCSI_IQN']:
        ag_id = access_group_create(iqn1, system_id)

        if cap['VOLUME_ISCSI_CHAP_AUTHENTICATION']:
            initiator_chap(iqn1)

        if cap['ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN']:
            access_group_initiator_add(ag_id, iqn2)

        if cap['VOLUME_MASK'] and cap['VOLUME_UNMASK']:
            vol_id = create_volume(pool_id)
            volume_mask(ag_id, vol_id)

            test_display(cap, system_id)

            if cap['VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP']:
                volumes_accessible_by_access_group(ag_id)

            if cap['ACCESS_GROUPS_GRANTED_TO_VOLUME']:
                access_groups_granted_to_volume(vol_id)

            if cap['VOLUME_UNMASK']:
                volume_unmask(ag_id, vol_id)

            if cap['VOLUME_DELETE']:
                volume_delete(vol_id)

            if cap['ACCESS_GROUP_INITIATOR_DELETE']:
                access_group_remove_init(ag_id, iqn1)

            if cap['ACCESS_GROUP_DELETE']:
                access_group_delete(ag_id)


def test_nfs_operations(cap, system_id):
    pass


def test_plugin_info(cap, system_id):
    out = call([cmd, 'plugin-info', ])[1]
    out = call([cmd, '-t' + sep, 'plugin-info', ])[1]


def test_plugin_list(cap, system_id):
    out = call([cmd, 'list', '--type', 'PLUGINS'])[1]
    out = call([cmd, '-t' + sep, 'list', '--type', 'PLUGINS'])[1]


def test_error_paths(cap, system_id):

    # Generate bad argument exception
    call([cmd, 'list', '--type', 'SNAPSHOTS'], 2)
    call([cmd, 'list', '--type', 'SNAPSHOTS', '--fs', 'DOES_NOT_EXIST'], 2)


def create_all(cap, system_id):
    test_plugin_info(cap, system_id)
    test_block_creation(cap, system_id)
    test_fs_creation(cap, system_id)
    test_nfs(cap, system_id)


def search_test(cap, system_id):
    print "\nTesting query with search ID\n"
    sys_id_filter = "--sys='%s'" % system_id
    if test_fs_pool_id:
        pool_id = test_fs_pool_id
    else:
        pool_id = name_to_id(OP_POOL, test_pool_name)
    pool_id_filter = "--pool='%s'" % pool_id

    vol_id = create_volume(pool_id)
    vol_id_filter = "--vol='%s'" % vol_id

    disk_id_filter = "--disk='%s'" % test_disk_id

    ag_id = access_group_create(random_iqn(), system_id)
    ag_id_filter = "--ag='%s'" % ag_id

    fs_id = fs_create(pool_id)
    fs_id_filter = "--fs='%s'" % fs_id

    nfs_export_id = export_fs(fs_id)
    nfs_export_id_filter = "--nfs-export='%s'" % nfs_export_id

    all_filters = [sys_id_filter, pool_id_filter, vol_id_filter,
                   disk_id_filter, ag_id_filter, fs_id_filter,
                   nfs_export_id_filter]

    supported = {
        'pools': [sys_id_filter, pool_id_filter],
        'volumes': [sys_id_filter, pool_id_filter, vol_id_filter,
                    ag_id_filter],
        'disks': [sys_id_filter, disk_id_filter],
        'access_groups': [sys_id_filter, ag_id_filter, vol_id_filter],
        'fs': [sys_id_filter, pool_id_filter, fs_id_filter],
        'exports': [fs_id_filter, nfs_export_id_filter],
    }
    for resouce_type in supported.keys():
        for cur_filter in all_filters:
            if cur_filter in supported[resouce_type]:
                call([cmd, 'list', '--type', resouce_type, cur_filter])
            else:
                call([cmd, 'list', '--type', resouce_type, cur_filter], 2)

    un_export_fs(nfs_export_id)
    delete_fs(fs_id)
    access_group_delete(ag_id)
    volume_delete(vol_id)
    return


def volume_raid_info_test(cap, system_id):
    if cap['VOLUME_RAID_INFO'] and cap['VOLUME_CREATE']:
        test_pool_id = name_to_id(OP_POOL, test_pool_name)

        if test_pool_id is None:
            print 'Pool %s is not available!' % test_pool_name
            exit(10)

        vol_id = create_volume(test_pool_id)
        out = call([cmd, '-t' + sep, 'volume-raid-info', '--vol', vol_id])[1]
        r = parse(out)
        if len(r[0]) != 6:
            print "volume-raid-info got expected output: %s" % out
            exit(10)
        if r[0][0] != vol_id:
            print "volume-raid-info output volume ID is not requested " \
                  "volume ID %s" % out
            exit(10)
    return


def pool_member_info_test(cap, system_id):
    if cap['POOL_MEMBER_INFO']:
        out = call([cmd, '-t' + sep, 'list', '--type', 'POOLS'])[1]
        pool_list = parse(out)
        for pool in pool_list:
            out = call(
                [cmd, '-t' + sep, 'pool-member-info', '--pool', pool[0]])[1]
            r = parse(out)
            if len(r[0]) != 4:
                print "pool-member-info got expected output: %s" % out
                exit(10)
            if r[0][0] != pool[0]:
                print "pool-member-info output pool ID is not requested " \
                      "pool ID %s" % out
                exit(10)
    return


def volume_raid_create_test(cap, system_id):
    if cap['VOLUME_RAID_CREATE']:
        out = call(
            [cmd, '-t' + sep, 'volume-raid-create-cap', '--sys', system_id])[1]

        if 'RAID1' not in [r[1] for r in parse(out)]:
            return

        out = call([cmd, '-t' + sep, 'list', '--type', 'disks'])[1]
        free_disk_ids = []
        disk_list = parse(out)
        for disk in disk_list:
            if 'Free' in disk:
                if len(free_disk_ids) == 2:
                    break
                free_disk_ids.append(disk[0])

        if len(free_disk_ids) != 2:
            print "Require two free disks to test volume-create-raid"
            exit(10)

        out = call([
            cmd, '-t' + sep, 'volume-raid-create', '--disk', free_disk_ids[0],
            '--disk', free_disk_ids[1], '--name',
            'test_volume_raid_create_%s' % rs(4),
            '--raid-type', 'raid1'])[1]

        volume = parse(out)
        vol_id = volume[0][0]
        pool_id = volume[0][5]

        if cap['VOLUME_RAID_INFO']:
            out = call(
                [cmd, '-t' + sep, 'volume-raid-info', '--vol', vol_id])[1]
            if parse(out)[0][1] != 'RAID1':
                print "New volume is not RAID 1"
                exit(10)

        if cap['POOL_MEMBER_INFO']:
            out = call(
                [cmd, '-t' + sep, 'pool-member-info', '--pool', pool_id])[1]
            if parse(out)[0][1] != 'RAID1':
                print "New pool is not RAID 1"
                exit(10)
            for disk_id in free_disk_ids:
                if disk_id not in [p[3] for p in parse(out)]:
                    print "New pool does not contain requested disks"
                    exit(10)

        if cap['VOLUME_DELETE']:
            volume_delete(vol_id)

    return


def volume_ident_led_on_test(cap):
    if cap['VOLUME_LED']:
        out = call([cmd, '-t' + sep, 'list', '--type', 'volumes'])[1]
        volume_list = parse(out)
        for volume in volume_list:
            out = call([
                cmd, '-t' + sep, 'volume-ident-led-on', '--volume',
                volume[0]])[1]

    return


def volume_ident_led_off_test(cap):
    if cap['VOLUME_LED']:
        out = call([cmd, '-t' + sep, 'list', '--type', 'volumes'])[1]
        volume_list = parse(out)
        for volume in volume_list:
            out = call([
                cmd, '-t' + sep, 'volume-ident-led-off', '--volume',
                volume[0]])[1]

    return


def local_disk_list_test():
    # Only run this by root user.
    if os.geteuid() == 0:
        call([cmd, 'local-disk-list'])
    else:
        print("Skipping test of 'local-disk-list' command when not "
              "run by root user")


def test_volume_cache_info():
    # Since cmdtest is only designed to test against sim://, there is no
    # need to check capacity or preconditions.
    pool_id = name_to_id(OP_POOL, test_pool_name)
    vol_id = create_volume(pool_id)
    cache_info = parse(
        call([cmd, '-t' + sep, 'volume-cache-info', '--vol', vol_id])[1])
    if len(cache_info) != 1 or len(cache_info[0]) != 6:
        print("Invalid return from volume-cache-info, should has 6 items")
        exit(10)
    volume_delete(vol_id)


def test_volume_pdc_update():
    pool_id = name_to_id(OP_POOL, test_pool_name)
    vol_id = create_volume(pool_id)
    for policy, result in dict(ENABLE="Enabled", DISABLE="Disabled").items():
        cache_info = parse(
            call([cmd, '-t' + sep, 'volume-phy-disk-cache-update',
                 '--vol', vol_id, '--policy', policy])[1])
        if len(cache_info) != 1 or len(cache_info[0]) < 6:
            print("Invalid return from volume-phy-disk-cache-update, "
                  "should has 6 or more items")
            exit(10)
        if cache_info[0][5] != result:
            print("Got unexpected return from volume-phy-disk-cache-update, "
                  "should be %s, but got %s" % (result, cache_info[0][5]))
            exit(10)
    volume_delete(vol_id)


def test_volume_wcp_update():
    pool_id = name_to_id(OP_POOL, test_pool_name)
    vol_id = create_volume(pool_id)
    for policy, result in dict(WB="Write Back", AUTO="Auto",
                               WT="Write Through").items():
        cache_info = parse(
            call([cmd, '-t' + sep, 'volume-write-cache-policy-update',
                 '--vol', vol_id, '--policy', policy])[1])
        if len(cache_info) != 1 or len(cache_info[0]) < 2:
            print("Invalid return from volume-write-cache-policy-upate, "
                  "should has 6 or more items")
            exit(10)
        if cache_info[0][1] != result:
            print("Got unexpected return from volume-write-cache-policy-update"
                  "should be %s, but got %s" % (result, cache_info[0][1]))
            exit(10)
    volume_delete(vol_id)


def test_volume_rcp_update():
    pool_id = name_to_id(OP_POOL, test_pool_name)
    vol_id = create_volume(pool_id)
    for policy, result in dict(ENABLE="Enabled", DISABLE="Disabled").items():
        cache_info = parse(
            call([cmd, '-t' + sep, 'volume-read-cache-policy-update',
                 '--vol', vol_id, '--policy', policy])[1])
        if len(cache_info) != 1 or len(cache_info[0]) < 4:
            print("Invalid return from volume-read-cache-policy-update, "
                  "should has 6 or more items")
            exit(10)
        if cache_info[0][3] != result:
            print("Got unexpected return from volume-read-cache-policy-update"
                  "should be %s, but got %s" % (result, cache_info[0][4]))
            exit(10)
    volume_delete(vol_id)


def test_local_disk_led():
    expected_rcs = [0, 4]
    flag_disk_found = False

    if (os.geteuid() != 0):
        print("Skipping test of 'local-disk-ident-led-on' and etc commands "
              "when not run by root user")
        return

    disk_paths = list(x[0] for x in
                      parse(call([cmd, '-t' + sep, 'local-disk-list'])[1]))

    if len(disk_paths) == 0:
        print("Skipping test of 'local-disk-ident-led-on' and etc commands "
              "when no local disk found")
        return

    # Only test against maximum 4 disks
    for disk_path in disk_paths[:4]:
        if os.path.exists(disk_path):
            flag_disk_found = True
            call([cmd, 'local-disk-ident-led-on', '--path', disk_path],
                 expected_rcs=expected_rcs)
            call([cmd, 'local-disk-ident-led-off', '--path', disk_path],
                 expected_rcs=expected_rcs)
            call([cmd, 'local-disk-fault-led-on', '--path', disk_path],
                 expected_rcs=expected_rcs)
            call([cmd, 'local-disk-fault-led-off', '--path', disk_path],
                 expected_rcs=expected_rcs)

    if flag_disk_found is False:
        print("Skipping test of 'local-disk-ident-led-on' and etc command "
              "when none of these disks exists: %s" % ", ".join(disk_paths))


def run_all_tests(cap, system_id):
    test_exit_code(cap, system_id)
    test_display(cap, system_id)
    test_plugin_list(cap, system_id)

    test_error_paths(cap, system_id)
    create_all(cap, system_id)

    test_mapping(cap, system_id)

    search_test(cap, system_id)

    volume_raid_info_test(cap, system_id)

    pool_member_info_test(cap, system_id)

    volume_raid_create_test(cap, system_id)

    local_disk_list_test()
    test_volume_cache_info()
    test_volume_pdc_update()
    test_volume_wcp_update()
    test_volume_rcp_update()
    test_local_disk_led()

if __name__ == "__main__":
    parser = OptionParser()
    parser.add_option("-c", "--command", action="store", type="string",
                      dest="cmd", help="specific command line to test")
    parser.add_option("-p", "--pool", action="store", dest="pool_name",
                      default='lsm_test_aggr',
                      help="pool name to use for testing")

    parser.add_option("-f", "--fspool", action="store", dest="fs_pool_id",
                      default='',
                      help="fs pool id to use for testing")

    parser.description = "lsmcli command line test tool"

    (options, args) = parser.parse_args()

    if options.cmd is None:
        print 'Please specify which lsmcli to test using -c or --command'
        sys.exit(1)
    else:
        cmd = options.cmd
        test_pool_name = options.pool_name

        if options.fs_pool_id:
            test_fs_pool_id = options.fs_pool_id

    # Theory of testing.
    # For each system that is available to us:
    #   Query capabilities
    #       Query all supported query operations (should have more to query)
    #
    #       Create objects of every supported type
    #           Query all supported query operations
    #           (should have more to query),
    #           run though different options making sure nothing explodes!
    #
    #       Try calling un-supported operations and expect them to fail
    systems = get_systems()

    for system in systems:
        c = capabilities(system[ID])
        run_all_tests(c, system[ID])
