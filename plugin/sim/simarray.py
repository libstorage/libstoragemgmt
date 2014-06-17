# Copyright (C) 2011-2014 Red Hat, Inc.
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
#         Gris Ge <fge@redhat.com>

# TODO: 1. Introduce constant check by using state_to_str() converting.
#       2. Snapshot should consume space in pool.

import random
import pickle
import tempfile
import os
import time

from lsm import (size_human_2_size_bytes, size_bytes_2_size_human)
from lsm import (System, Volume, Disk, Pool, FileSystem, AccessGroup,
                 FsSnapshot, NfsExport, OptionalData, md5, LsmError,
                 ErrorNumber, JobStatus)

# Used for format width for disks
D_FMT = 5


class SimJob(object):
    """
    Simulates a longer running job, uses actual wall time.  If test cases
    take too long we can reduce time by shortening time duration.
    """

    def _calc_progress(self):
        if self.percent < 100:
            end = self.start + self.duration
            now = time.time()
            if now >= end:
                self.percent = 100
                self.status = JobStatus.COMPLETE
            else:
                diff = now - self.start
                self.percent = int(100 * (diff / self.duration))

    def __init__(self, item_to_return):
        duration = os.getenv("LSM_SIM_TIME", 1)
        self.status = JobStatus.INPROGRESS
        self.percent = 0
        self.__item = item_to_return
        self.start = time.time()
        self.duration = float(random.randint(0, int(duration)))

    def progress(self):
        """
        Returns a tuple (status, percent, data)
        """
        self._calc_progress()
        return self.status, self.percent, self.item

    @property
    def item(self):
        if self.percent >= 100:
            return self.__item
        return None

    @item.setter
    def item(self, value):
        self.__item = value


class SimArray(object):
    SIM_DATA_FILE = os.getenv("LSM_SIM_DATA",
                              tempfile.gettempdir() + '/lsm_sim_data')

    @staticmethod
    def _version_error(dump_file):
        raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                       "Stored simulator state incompatible with "
                       "simulator, please move or delete %s" %
                       dump_file)

    @staticmethod
    def _sort_by_id(resources):
        # isupper() just make sure the 'lsm_test_aggr' come as first one.
        return sorted(resources, key=lambda k: (k.id.isupper(), k.id))

    def __init__(self, dump_file=None):
        if dump_file is None:
            self.dump_file = SimArray.SIM_DATA_FILE
        else:
            self.dump_file = dump_file

        if os.path.exists(self.dump_file):
            try:
                with open(self.dump_file, 'rb') as f:
                    self.data = pickle.load(f)

            # Going forward we could get smarter about handling this for
            # changes that aren't invasive, but we at least need to check
            # to make sure that the data will work and not cause any
            # undo confusion.
                if self.data.version != SimData.SIM_DATA_VERSION or \
                   self.data.signature != SimData.state_signature():
                    SimArray._version_error(self.dump_file)
            except AttributeError:
                SimArray._version_error(self.dump_file)

        else:
            self.data = SimData()

    def save_state(self):
        fh_dump_file = open(self.dump_file, 'wb')
        pickle.dump(self.data, fh_dump_file)
        fh_dump_file.close()

    def job_status(self, job_id, flags=0):
        return self.data.job_status(job_id, flags=0)

    def job_free(self, job_id, flags=0):
        return self.data.job_free(job_id, flags=0)

    def time_out_set(self, ms, flags=0):
        return self.data.set_time_out(ms, flags)

    def time_out_get(self, flags=0):
        return self.data.get_time_out(flags)

    def systems(self):
        return self.data.systems()

    @staticmethod
    def _sim_vol_2_lsm(sim_vol):
        return Volume(sim_vol['vol_id'], sim_vol['name'], sim_vol['vpd83'],
                      SimData.SIM_DATA_BLK_SIZE,
                      int(sim_vol['total_space'] / SimData.SIM_DATA_BLK_SIZE),
                      Volume.STATUS_OK, sim_vol['sys_id'],
                      sim_vol['pool_id'])

    def volumes(self):
        sim_vols = self.data.volumes()
        return SimArray._sort_by_id(
            [SimArray._sim_vol_2_lsm(v) for v in sim_vols])

    def _sim_pool_2_lsm(self, sim_pool, flags=0):
        pool_id = sim_pool['pool_id']
        name = sim_pool['name']
        total_space = self.data.pool_total_space(pool_id)
        free_space = self.data.pool_free_space(pool_id)
        status = sim_pool['status']
        status_info = sim_pool['status_info']
        sys_id = sim_pool['sys_id']
        opt_data = OptionalData()
        if flags & Pool.FLAG_RETRIEVE_FULL_INFO:
            opt_data.set('raid_type', sim_pool['raid_type'])
            opt_data.set('member_type', sim_pool['member_type'])
            opt_data.set('member_ids', sim_pool['member_ids'])
            opt_data.set('thinp_type', Pool.THINP_TYPE_THIN)
            opt_data.set('element_type', sim_pool['element_type'])

        return Pool(pool_id, name, total_space, free_space, status,
                    status_info, sys_id, opt_data)

    def pools(self, flags=0):
        rc = []
        sim_pools = self.data.pools()
        for sim_pool in sim_pools:
            rc.extend([self._sim_pool_2_lsm(sim_pool, flags)])
        return SimArray._sort_by_id(rc)

    def pool_create(self, sys_id, pool_name, size_bytes,
                    raid_type=Pool.RAID_TYPE_UNKNOWN,
                    member_type=Pool.MEMBER_TYPE_UNKNOWN, flags=0):
        sim_pool = self.data.pool_create(
            sys_id, pool_name, size_bytes, raid_type, member_type, flags)
        return self.data.job_create(
            self._sim_pool_2_lsm(sim_pool, Pool.FLAG_RETRIEVE_FULL_INFO))

    def pool_create_from_disks(self, sys_id, pool_name, disks_ids, raid_type,
                               flags=0):
        sim_pool = self.data.pool_create_from_disks(
            sys_id, pool_name, disks_ids, raid_type, flags)
        return self.data.job_create(
            self._sim_pool_2_lsm(sim_pool, Pool.FLAG_RETRIEVE_FULL_INFO))

    def pool_create_from_volumes(self, sys_id, pool_name, member_ids,
                                 raid_type, flags=0):
        sim_pool = self.data.pool_create_from_volumes(
            sys_id, pool_name, member_ids, raid_type, flags)
        return self.data.job_create(
            self._sim_pool_2_lsm(sim_pool, Pool.FLAG_RETRIEVE_FULL_INFO))

    def pool_create_from_pool(self, sys_id, pool_name, member_id, size_bytes,
                              flags=0):
        sim_pool = self.data.pool_create_from_pool(
            sys_id, pool_name, member_id, size_bytes, flags)
        return self.data.job_create(
            self._sim_pool_2_lsm(sim_pool, Pool.FLAG_RETRIEVE_FULL_INFO))

    def pool_delete(self, pool_id, flags=0):
        return self.data.job_create(self.data.pool_delete(pool_id, flags))[0]

    def disks(self):
        rc = []
        sim_disks = self.data.disks()
        for sim_disk in sim_disks:
            disk = Disk(sim_disk['disk_id'], sim_disk['name'],
                        sim_disk['disk_type'], SimData.SIM_DATA_BLK_SIZE,
                        int(sim_disk['total_space'] /
                            SimData.SIM_DATA_BLK_SIZE),
                        Disk.STATUS_OK, sim_disk['sys_id'])
            rc.extend([disk])
        return SimArray._sort_by_id(rc)

    def volume_create(self, pool_id, vol_name, size_bytes, thinp, flags=0):
        sim_vol = self.data.volume_create(
            pool_id, vol_name, size_bytes, thinp, flags)
        return self.data.job_create(SimArray._sim_vol_2_lsm(sim_vol))

    def volume_delete(self, vol_id, flags=0):
        self.data.volume_delete(vol_id, flags=0)
        return self.data.job_create(None)[0]

    def volume_resize(self, vol_id, new_size_bytes, flags=0):
        sim_vol = self.data.volume_resize(vol_id, new_size_bytes, flags)
        return self.data.job_create(SimArray._sim_vol_2_lsm(sim_vol))

    def volume_replicate(self, dst_pool_id, rep_type, src_vol_id, new_vol_name,
                         flags=0):
        sim_vol = self.data.volume_replicate(
            dst_pool_id, rep_type, src_vol_id, new_vol_name, flags)
        return self.data.job_create(SimArray._sim_vol_2_lsm(sim_vol))

    def volume_replicate_range_block_size(self, sys_id, flags=0):
        return self.data.volume_replicate_range_block_size(sys_id, flags)

    def volume_replicate_range(self, rep_type, src_vol_id, dst_vol_id, ranges,
                               flags=0):
        return self.data.job_create(
            self.data.volume_replicate_range(
                rep_type, src_vol_id, dst_vol_id, ranges, flags))[0]

    def volume_online(self, vol_id, flags=0):
        return self.data.volume_online(vol_id, flags)

    def volume_offline(self, vol_id, flags=0):
        return self.data.volume_offline(vol_id, flags)

    def volume_child_dependency(self, vol_id, flags=0):
        return self.data.volume_child_dependency(vol_id, flags)

    def volume_child_dependency_rm(self, vol_id, flags=0):
        return self.data.job_create(
            self.data.volume_child_dependency_rm(vol_id, flags))[0]

    @staticmethod
    def _sim_fs_2_lsm(sim_fs):
        return FileSystem(sim_fs['fs_id'], sim_fs['name'],
                          sim_fs['total_space'], sim_fs['free_space'],
                          sim_fs['pool_id'], sim_fs['sys_id'])

    def fs(self):
        sim_fss = self.data.fs()
        return SimArray._sort_by_id(
            [SimArray._sim_fs_2_lsm(f) for f in sim_fss])

    def fs_create(self, pool_id, fs_name, size_bytes, flags=0):
        sim_fs = self.data.fs_create(pool_id, fs_name, size_bytes, flags)
        return self.data.job_create(SimArray._sim_fs_2_lsm(sim_fs))

    def fs_delete(self, fs_id, flags=0):
        self.data.fs_delete(fs_id, flags=0)
        return self.data.job_create(None)[0]

    def fs_resize(self, fs_id, new_size_bytes, flags=0):
        sim_fs = self.data.fs_resize(fs_id, new_size_bytes, flags)
        return self.data.job_create(SimArray._sim_fs_2_lsm(sim_fs))

    def fs_clone(self, src_fs_id, dst_fs_name, snap_id, flags=0):
        sim_fs = self.data.fs_clone(src_fs_id, dst_fs_name, snap_id, flags)
        return self.data.job_create(SimArray._sim_fs_2_lsm(sim_fs))

    def fs_file_clone(self, fs_id, src_fs_name, dst_fs_name, snap_id, flags=0):
        return self.data.job_create(
            self.data.fs_file_clone(
                fs_id, src_fs_name, dst_fs_name, snap_id, flags))[0]

    @staticmethod
    def _sim_snap_2_lsm(sim_snap):
        return FsSnapshot(sim_snap['snap_id'], sim_snap['name'],
                        sim_snap['timestamp'])

    def fs_snapshots(self, fs_id, flags=0):
        sim_snaps = self.data.fs_snapshots(fs_id, flags)
        return [SimArray._sim_snap_2_lsm(s) for s in sim_snaps]

    def fs_snapshot_create(self, fs_id, snap_name, files, flags=0):
        sim_snap = self.data.fs_snapshot_create(fs_id, snap_name, files,
                                                flags)
        return self.data.job_create(SimArray._sim_snap_2_lsm(sim_snap))

    def fs_snapshot_delete(self, fs_id, snap_id, flags=0):
        return self.data.job_create(
            self.data.fs_snapshot_delete(fs_id, snap_id, flags))[0]

    def fs_snapshot_restore(self, fs_id, snap_id, files, restore_files,
                            flag_all_files, flags):
        return self.data.job_create(
            self.data.fs_snapshot_restore(
                fs_id, snap_id, files, restore_files,
                flag_all_files, flags))[0]

    def fs_child_dependency(self, fs_id, files, flags=0):
        return self.data.fs_child_dependency(fs_id, files, flags)

    def fs_child_dependency_rm(self, fs_id, files, flags=0):
        return self.data.job_create(
            self.data.fs_child_dependency_rm(fs_id, files, flags))[0]

    @staticmethod
    def _sim_exp_2_lsm(sim_exp):
        return NfsExport(
            sim_exp['exp_id'], sim_exp['fs_id'], sim_exp['exp_path'],
            sim_exp['auth_type'], sim_exp['root_hosts'], sim_exp['rw_hosts'],
            sim_exp['ro_hosts'], sim_exp['anon_uid'], sim_exp['anon_gid'],
            sim_exp['options'])

    def exports(self, flags=0):
        sim_exps = self.data.exports(flags)
        return SimArray._sort_by_id(
            [SimArray._sim_exp_2_lsm(e) for e in sim_exps])

    def fs_export(self, fs_id, exp_path, root_hosts, rw_hosts, ro_hosts,
                  anon_uid, anon_gid, auth_type, options, flags=0):
        sim_exp = self.data.fs_export(
            fs_id, exp_path, root_hosts, rw_hosts, ro_hosts,
            anon_uid, anon_gid, auth_type, options, flags)
        return SimArray._sim_exp_2_lsm(sim_exp)

    def fs_unexport(self, exp_id, flags=0):
        return self.data.fs_unexport(exp_id, flags)

    @staticmethod
    def _sim_ag_2_lsm(sim_ag):
        return AccessGroup(sim_ag['ag_id'], sim_ag['name'],
                           sim_ag['init_ids'], sim_ag['init_type'],
                           sim_ag['sys_id'])

    def ags(self):
        sim_ags = self.data.ags()
        return [SimArray._sim_ag_2_lsm(a) for a in sim_ags]

    def access_group_create(self, name, init_id, init_type, sys_id, flags=0):
        sim_ag = self.data.access_group_create(
            name, init_id, init_type, sys_id, flags)
        return SimArray._sim_ag_2_lsm(sim_ag)

    def access_group_delete(self, ag_id, flags=0):
        return self.data.access_group_delete(ag_id, flags)

    def access_group_initiator_add(self, ag_id, init_id, init_type, flags=0):
        return self.data.access_group_initiator_add(
            ag_id, init_id, init_type, flags)

    def access_group_initiator_delete(self, ag_id, init_id, flags=0):
        return self.data.access_group_initiator_delete(ag_id, init_id, flags)

    def volume_mask(self, ag_id, vol_id, flags=0):
        return self.data.volume_mask(ag_id, vol_id, flags)

    def volume_unmask(self, ag_id, vol_id, flags=0):
        return self.data.volume_unmask(ag_id, vol_id, flags)

    def volumes_accessible_by_access_group(self, ag_id, flags=0):
        sim_vols = self.data.volumes_accessible_by_access_group(ag_id, flags)
        return [SimArray._sim_vol_2_lsm(v) for v in sim_vols]

    def access_groups_granted_to_volume(self, vol_id, flags=0):
        sim_ags = self.data.access_groups_granted_to_volume(vol_id, flags)
        return [SimArray._sim_ag_2_lsm(a) for a in sim_ags]

    def iscsi_chap_auth(self, init_id, in_user, in_pass, out_user, out_pass,
                        flags=0):
        return self.data.iscsi_chap_auth(init_id, in_user, in_pass, out_user,
                                         out_pass, flags)


class SimData(object):
    """
        Rules here are:
            * we don't store one data twice
            * we don't srore data which could be caculated out

        self.vol_dict = {
            Volume.id = sim_vol,
        }

        sim_vol = {
            'vol_id': self._next_vol_id()
            'vpd83': SimData._random_vpd(),
            'name': vol_name,
            'total_space': size_bytes,
            'sys_id': SimData.SIM_DATA_SYS_ID,
            'pool_id': owner_pool_id,
            'consume_size': size_bytes,
            'replicate': {
                dst_vol_id = [
                    {
                        'src_start_blk': src_start_blk,
                        'dst_start_blk': dst_start_blk,
                        'blk_count': blk_count,
                        'rep_type': Volume.REPLICATE_XXXX,
                    },
                ],
            },
            'mask': {
                ag_id = Volume.ACCESS_READ_WRITE|Volume.ACCESS_READ_ONLY,
            },
            'mask_init': {
                init_id = Volume.ACCESS_READ_WRITE|Volume.ACCESS_READ_ONLY,
            }
        }

        self.ag_dict ={
            AccessGroup.id = sim_ag,
        }
        sim_ag = {
            'init_ids': [init_id,],
            'init_type': AccessGroup.init_type,
            'sys_id': SimData.SIM_DATA_SYS_ID,
            'name': name,
            'ag_id': self._next_ag_id()
        }

        self.fs_dict = {
            FileSystem.id = sim_fs,
        }
        sim_fs = {
            'fs_id': self._next_fs_id(),
            'name': fs_name,
            'total_space': size_bytes,
            'free_space': size_bytes,
            'sys_id': SimData.SIM_DATA_SYS_ID,
            'pool_id': pool_id,
            'consume_size': size_bytes,
            'clone': {
                dst_fs_id: {
                    'snap_id': snap_id,     # None if no snapshot
                    'files': [ file_path, ] # [] if all files cloned.
                },
            },
            'snaps' = [snap_id, ],
        }
        self.snap_dict = {
            Snapshot.id: sim_snap,
        }
        sim_snap = {
            'snap_id': self._next_snap_id(),
            'name': snap_name,
            'fs_id': fs_id,
            'files': [file_path, ],
            'timestamp': time.time(),
        }
        self.exp_dict = {
            Export.id: sim_exp,
        }
        sim_exp = {
            'exp_id': self._next_exp_id(),
            'fs_id': fs_id,
            'exp_path': exp_path,
            'auth_type': auth_type,
            'root_hosts': [root_host, ],
            'rw_hosts': [rw_host, ],
            'ro_hosts': [ro_host, ],
            'anon_uid': anon_uid,
            'anon_gid': anon_gid,
            'options': [option, ],
        }

        self.pool_dict = {
            Pool.id: sim_pool,
        }
        sim_pool = {
            'name': pool_name,
            'pool_id': Pool.id,
            'raid_type': Pool.RAID_TYPE_XXXX,
            'member_ids': [ disk_id or pool_id or volume_id ],
            'member_type': Pool.MEMBER_TYPE_XXXX,
            'member_size': size_bytes  # space allocated from each member pool.
                                       # only for MEMBER_TYPE_POOL
            'status': SIM_DATA_POOL_STATUS,
            'sys_id': SimData.SIM_DATA_SYS_ID,
            'element_type': SimData.SIM_DATA_POOL_ELEMENT_TYPE,
        }
    """
    SIM_DATA_BLK_SIZE = 512
    SIM_DATA_VERSION = "2.4"
    SIM_DATA_SYS_ID = 'sim-01'
    SIM_DATA_INIT_NAME = 'NULL'
    SIM_DATA_TMO = 30000    # ms
    SIM_DATA_POOL_STATUS = Pool.STATUS_OK
    SIM_DATA_POOL_STATUS_INFO = ''
    SIM_DATA_DISK_DEFAULT_RAID = Pool.RAID_TYPE_RAID0
    SIM_DATA_VOLUME_DEFAULT_RAID = Pool.RAID_TYPE_RAID0
    SIM_DATA_POOL_ELEMENT_TYPE = Pool.ELEMENT_TYPE_FS \
        | Pool.ELEMENT_TYPE_POOL \
        | Pool.ELEMENT_TYPE_VOLUME

    SIM_DATA_SYS_POOL_ELEMENT_TYPE = SIM_DATA_POOL_ELEMENT_TYPE \
        | Pool.ELEMENT_TYPE_SYS_RESERVED

    SIM_DATA_CUR_VOL_ID = 0
    SIM_DATA_CUR_POOL_ID = 0
    SIM_DATA_CUR_FS_ID = 0
    SIM_DATA_CUR_AG_ID = 0
    SIM_DATA_CUR_SNAP_ID = 0
    SIM_DATA_CUR_EXP_ID = 0

    def _next_pool_id(self):
        self.SIM_DATA_CUR_POOL_ID += 1
        return "POOL_ID_%08d" % self.SIM_DATA_CUR_POOL_ID

    def _next_vol_id(self):
        self.SIM_DATA_CUR_VOL_ID += 1
        return "VOL_ID_%08d" % self.SIM_DATA_CUR_VOL_ID

    def _next_fs_id(self):
        self.SIM_DATA_CUR_FS_ID += 1
        return "FS_ID_%08d" % self.SIM_DATA_CUR_FS_ID

    def _next_ag_id(self):
        self.SIM_DATA_CUR_AG_ID += 1
        return "AG_ID_%08d" % self.SIM_DATA_CUR_AG_ID

    def _next_snap_id(self):
        self.SIM_DATA_CUR_SNAP_ID += 1
        return "SNAP_ID_%08d" % self.SIM_DATA_CUR_SNAP_ID

    def _next_exp_id(self):
        self.SIM_DATA_CUR_EXP_ID += 1
        return "EXP_ID_%08d" % self.SIM_DATA_CUR_EXP_ID

    @staticmethod
    def state_signature():
        return 'LSM_SIMULATOR_DATA_%s' % md5(SimData.SIM_DATA_VERSION)

    @staticmethod
    def _disk_id(num):
        return "DISK_ID_%0*d" % (D_FMT, num)

    def __init__(self):
        self.tmo = SimData.SIM_DATA_TMO
        self.version = SimData.SIM_DATA_VERSION
        self.signature = SimData.state_signature()
        self.job_num = 0
        self.job_dict = {
            # id: SimJob
        }
        self.syss = [System(SimData.SIM_DATA_SYS_ID,
                            'LSM simulated storage plug-in',
                            System.STATUS_OK, '')]
        pool_size_200g = size_human_2_size_bytes('200GiB')
        self.pool_dict = {
            'POO1': {
                'pool_id': 'POO1',
                'name': 'Pool 1',
                'member_type': Pool.MEMBER_TYPE_DISK_SATA,
                'member_ids': [SimData._disk_id(0), SimData._disk_id(1)],
                'raid_type': Pool.RAID_TYPE_RAID1,
                'status': SimData.SIM_DATA_POOL_STATUS,
                'status_info': SimData.SIM_DATA_POOL_STATUS_INFO,
                'sys_id': SimData.SIM_DATA_SYS_ID,
                'element_type': SimData.SIM_DATA_SYS_POOL_ELEMENT_TYPE,
            },
            'POO2': {
                'pool_id': 'POO2',
                'name': 'Pool 2',
                'member_type': Pool.MEMBER_TYPE_POOL,
                'member_ids': ['POO1'],
                'member_size': pool_size_200g,
                'raid_type': Pool.RAID_TYPE_NOT_APPLICABLE,
                'status': Pool.STATUS_OK,
                'status_info': SimData.SIM_DATA_POOL_STATUS_INFO,
                'sys_id': SimData.SIM_DATA_SYS_ID,
                'element_type': SimData.SIM_DATA_POOL_ELEMENT_TYPE,
            },
            # lsm_test_aggr pool is required by test/runtest.sh
            'lsm_test_aggr': {
                'pool_id': 'lsm_test_aggr',
                'name': 'lsm_test_aggr',
                'member_type': Pool.MEMBER_TYPE_DISK_SAS,
                'member_ids': [SimData._disk_id(2), SimData._disk_id(3)],
                'raid_type': Pool.RAID_TYPE_RAID0,
                'status': Pool.STATUS_OK,
                'status_info': SimData.SIM_DATA_POOL_STATUS_INFO,
                'sys_id': SimData.SIM_DATA_SYS_ID,
                'element_type': SimData.SIM_DATA_POOL_ELEMENT_TYPE,
            },
        }
        self.vol_dict = {
        }
        self.fs_dict = {
        }
        self.snap_dict = {
        }
        self.exp_dict = {
        }
        disk_size_2t = size_human_2_size_bytes('2TiB')
        disk_size_512g = size_human_2_size_bytes('512GiB')

        # Make disks in a loop so that we can create many disks easily
        # if we wish
        self.disk_dict = {}

        for i in range(0, 21):
            d_id = SimData._disk_id(i)
            d_size = disk_size_2t
            d_name = "SATA Disk %0*d" % (D_FMT, i)
            d_type = Disk.DISK_TYPE_SATA

            if 2 <= i <= 8:
                d_name = "SAS  Disk %0*d" % (D_FMT, i)
                d_type = Disk.DISK_TYPE_SAS
            elif 9 <= i:
                d_name = "SSD  Disk %0*d" % (D_FMT, i)
                d_type = Disk.DISK_TYPE_SSD
                if i <= 13:
                    d_size = disk_size_512g

            self.disk_dict[d_id] = dict(disk_id=d_id, name=d_name,
                                        total_space=d_size,
                                        disk_type=d_type,
                                        sys_id=SimData.SIM_DATA_SYS_ID)

        self.ag_dict = {
        }
        self.init_dict = {
        }
        # Create some volumes, fs and etc
        self.volume_create(
            'POO1', 'Volume 000', size_human_2_size_bytes('200GiB'),
            Volume.PROVISION_DEFAULT)
        self.volume_create(
            'POO1', 'Volume 001', size_human_2_size_bytes('200GiB'),
            Volume.PROVISION_DEFAULT)

        self.pool_dict['POO3'] = {
            'pool_id': 'POO3',
            'name': 'Pool 3',
            'member_type': Pool.MEMBER_TYPE_VOLUME,
            'member_ids': [
                self.vol_dict.values()[0]['vol_id'],
                self.vol_dict.values()[1]['vol_id'],
            ],
            'raid_type': Pool.RAID_TYPE_RAID1,
            'status': Pool.STATUS_OK,
            'status_info': SimData.SIM_DATA_POOL_STATUS_INFO,
            'sys_id': SimData.SIM_DATA_SYS_ID,
            'element_type': SimData.SIM_DATA_POOL_ELEMENT_TYPE,
        }

        return

    def pool_free_space(self, pool_id):
        """
        Calculate out the free size of certain pool.
        """
        free_space = self.pool_total_space(pool_id)
        for sim_vol in self.vol_dict.values():
            if sim_vol['pool_id'] != pool_id:
                continue
            if free_space <= sim_vol['consume_size']:
                return 0
            free_space -= sim_vol['consume_size']
        for sim_fs in self.fs_dict.values():
            if sim_fs['pool_id'] != pool_id:
                continue
            if free_space <= sim_fs['consume_size']:
                return 0
            free_space -= sim_fs['consume_size']
        for sim_pool in self.pool_dict.values():
            if sim_pool['member_type'] != Pool.MEMBER_TYPE_POOL:
                continue
            if pool_id in sim_pool['member_ids']:
                free_space -= sim_pool['member_size']
        return free_space

    @staticmethod
    def _random_vpd(l=16):
        """
        Generate a random 16 digit number as hex
        """
        vpd = []
        for i in range(0, l):
            vpd.append(str('%02X' % (random.randint(0, 255))))
        return "".join(vpd)

    def _size_of_raid(self, member_type, member_ids, raid_type,
                      pool_each_size=0):
        member_sizes = []
        if Pool.member_type_is_disk(member_type):
            for member_id in member_ids:
                member_sizes.extend([self.disk_dict[member_id]['total_space']])

        elif member_type == Pool.MEMBER_TYPE_VOLUME:
            for member_id in member_ids:
                member_sizes.extend([self.vol_dict[member_id]['total_space']])

        elif member_type == Pool.MEMBER_TYPE_POOL:
            for member_id in member_ids:
                member_sizes.extend([pool_each_size])

        else:
            raise LsmError(ErrorNumber.INTERNAL_ERROR,
                           "Got unsupported member_type in _size_of_raid()" +
                           ": %d" % member_type)
        all_size = 0
        member_size = 0
        member_count = len(member_ids)
        for member_size in member_sizes:
            all_size += member_size

        if raid_type == Pool.RAID_TYPE_JBOD or \
           raid_type == Pool.RAID_TYPE_NOT_APPLICABLE or \
           raid_type == Pool.RAID_TYPE_RAID0:
            return int(all_size)
        elif (raid_type == Pool.RAID_TYPE_RAID1 or
              raid_type == Pool.RAID_TYPE_RAID10):
            if member_count % 2 == 1:
                return 0
            return int(all_size / 2)
        elif (raid_type == Pool.RAID_TYPE_RAID3 or
              raid_type == Pool.RAID_TYPE_RAID4 or
              raid_type == Pool.RAID_TYPE_RAID5):
            if member_count < 3:
                return 0
            return int(all_size - member_size)
        elif raid_type == Pool.RAID_TYPE_RAID50:
            if member_count < 6 or member_count % 2 == 1:
                return 0
            return int(all_size - member_size * 2)
        elif raid_type == Pool.RAID_TYPE_RAID6:
            if member_count < 4:
                return 0
            return int(all_size - member_size * 2)
        elif raid_type == Pool.RAID_TYPE_RAID60:
            if member_count < 8 or member_count % 2 == 1:
                return 0
            return int(all_size - member_size * 4)
        elif raid_type == Pool.RAID_TYPE_RAID51:
            if member_count < 6 or member_count % 2 == 1:
                return 0
            return int(all_size / 2 - member_size)
        elif raid_type == Pool.RAID_TYPE_RAID61:
            if member_count < 8 or member_count % 2 == 1:
                return 0
            print "%s" % size_bytes_2_size_human(all_size)
            print "%s" % size_bytes_2_size_human(member_size)
            return int(all_size / 2 - member_size * 2)
        raise LsmError(ErrorNumber.INTERNAL_ERROR,
                       "_size_of_raid() got invalid raid type: " +
                       "%s(%d)" % (Pool.raid_type_to_str(raid_type),
                                   raid_type))

    def pool_total_space(self, pool_id):
        """
        Find out the correct size of RAID pool
        """
        sim_pool = self.pool_dict[pool_id]
        each_pool_size_bytes = 0
        member_type = sim_pool['member_type']
        if sim_pool['member_type'] == Pool.MEMBER_TYPE_POOL:
            each_pool_size_bytes = sim_pool['member_size']

        return self._size_of_raid(
            member_type, sim_pool['member_ids'], sim_pool['raid_type'],
            each_pool_size_bytes)

    @staticmethod
    def _block_rounding(size_bytes):
        return (size_bytes + SimData.SIM_DATA_BLK_SIZE - 1) / \
            SimData.SIM_DATA_BLK_SIZE * \
            SimData.SIM_DATA_BLK_SIZE

    def job_create(self, returned_item):
        if random.randint(0, 3) > 0:
            self.job_num += 1
            job_id = "JOB_%s" % self.job_num
            self.job_dict[job_id] = SimJob(returned_item)
            return job_id, None
        else:
            return None, returned_item

    def job_status(self, job_id, flags=0):
        if job_id in self.job_dict.keys():
            return self.job_dict[job_id].progress()
        raise LsmError(ErrorNumber.NOT_FOUND_JOB,
                       'Non-existent job: %s' % job_id)

    def job_free(self, job_id, flags=0):
        if job_id in self.job_dict.keys():
            del(self.job_dict[job_id])
            return
        raise LsmError(ErrorNumber.NOT_FOUND_JOB,
                       'Non-existent job: %s' % job_id)

    def set_time_out(self, ms, flags=0):
        self.tmo = ms
        return None

    def get_time_out(self, flags=0):
        return self.tmo

    def systems(self):
        return self.syss

    def pools(self):
        return self.pool_dict.values()

    def volumes(self):
        return self.vol_dict.values()

    def disks(self):
        return self.disk_dict.values()

    def access_groups(self):
        return self.ag_dict.values()

    def volume_create(self, pool_id, vol_name, size_bytes, thinp, flags=0):
        self._check_dup_name(
            self.vol_dict.values(), vol_name, ErrorNumber.EXISTS_VOLUME)
        size_bytes = SimData._block_rounding(size_bytes)
        # check free size
        free_space = self.pool_free_space(pool_id)
        if (free_space < size_bytes):
            raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                           "Insufficient space in pool")
        sim_vol = dict()
        sim_vol['vol_id'] = self._next_vol_id()
        sim_vol['vpd83'] = SimData._random_vpd()
        sim_vol['name'] = vol_name
        sim_vol['total_space'] = size_bytes
        sim_vol['thinp'] = thinp
        sim_vol['sys_id'] = SimData.SIM_DATA_SYS_ID
        sim_vol['pool_id'] = pool_id
        sim_vol['consume_size'] = size_bytes
        self.vol_dict[sim_vol['vol_id']] = sim_vol
        return sim_vol

    def volume_delete(self, vol_id, flags=0):
        if vol_id in self.vol_dict.keys():
            del(self.vol_dict[vol_id])
            return
        raise LsmError(ErrorNumber.INVALID_VOLUME,
                       "No such volume: %s" % vol_id)

    def volume_resize(self, vol_id, new_size_bytes, flags=0):
        new_size_bytes = SimData._block_rounding(new_size_bytes)
        if vol_id in self.vol_dict.keys():
            pool_id = self.vol_dict[vol_id]['pool_id']
            free_space = self.pool_free_space(pool_id)
            if (free_space < new_size_bytes):
                raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                               "Insufficient space in pool")

            self.vol_dict[vol_id]['total_space'] = new_size_bytes
            self.vol_dict[vol_id]['consume_size'] = new_size_bytes
            return self.vol_dict[vol_id]
        raise LsmError(ErrorNumber.INVALID_VOLUME,
                       "No such volume: %s" % vol_id)

    def volume_replicate(self, dst_pool_id, rep_type, src_vol_id, new_vol_name,
                         flags=0):
        if src_vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such volume: %s" % src_vol_id)
        size_bytes = self.vol_dict[src_vol_id]['total_space']
        size_bytes = SimData._block_rounding(size_bytes)
        # check free size
        free_space = self.pool_free_space(dst_pool_id)
        if (free_space < size_bytes):
            raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                           "Insufficient space in pool")
        sim_vol = dict()
        sim_vol['vol_id'] = self._next_vol_id()
        sim_vol['vpd83'] = SimData._random_vpd()
        sim_vol['name'] = new_vol_name
        sim_vol['total_space'] = size_bytes
        sim_vol['sys_id'] = SimData.SIM_DATA_SYS_ID
        sim_vol['pool_id'] = dst_pool_id
        sim_vol['consume_size'] = size_bytes
        self.vol_dict[sim_vol['vol_id']] = sim_vol

        dst_vol_id = sim_vol['vol_id']
        if 'replicate' not in self.vol_dict[src_vol_id].keys():
            self.vol_dict[src_vol_id]['replicate'] = dict()

        if dst_vol_id not in self.vol_dict[src_vol_id]['replicate'].keys():
            self.vol_dict[src_vol_id]['replicate'][dst_vol_id] = list()

        sim_rep = {
            'rep_type': rep_type,
            'src_start_blk': 0,
            'dst_start_blk': 0,
            'blk_count': size_bytes,
        }
        self.vol_dict[src_vol_id]['replicate'][dst_vol_id].extend(
            [sim_rep])

        return sim_vol

    def volume_replicate_range_block_size(self, sys_id, flags=0):
        return SimData.SIM_DATA_BLK_SIZE

    def volume_replicate_range(self, rep_type, src_vol_id, dst_vol_id, ranges,
                               flags=0):
        if src_vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % src_vol_id)

        if dst_vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % dst_vol_id)

        sim_reps = []
        for rep_range in ranges:
            sim_rep = dict()
            sim_rep['rep_type'] = rep_type
            sim_rep['src_start_blk'] = rep_range.src_block
            sim_rep['dst_start_blk'] = rep_range.dest_block
            sim_rep['blk_count'] = rep_range.block_count
            sim_reps.extend([sim_rep])

        if 'replicate' not in self.vol_dict[src_vol_id].keys():
            self.vol_dict[src_vol_id]['replicate'] = dict()

        if dst_vol_id not in self.vol_dict[src_vol_id]['replicate'].keys():
            self.vol_dict[src_vol_id]['replicate'][dst_vol_id] = list()

        self.vol_dict[src_vol_id]['replicate'][dst_vol_id].extend(
            [sim_reps])

        return None

    def volume_online(self, vol_id, flags=0):
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % vol_id)
        # TODO: Volume.STATUS_XXX does have indication about volume offline
        #       or online, meanwhile, cmdline does not support volume_online()
        #       yet
        return None

    def volume_offline(self, vol_id, flags=0):
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % vol_id)
        # TODO: Volume.STATUS_XXX does have indication about volume offline
        #       or online, meanwhile, cmdline does not support volume_online()
        #       yet
        return None

    def volume_child_dependency(self, vol_id, flags=0):
        """
        If volume is a src or dst of a replication, we return True.
        """
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % vol_id)
        if 'replicate' in self.vol_dict[vol_id].keys() and \
           self.vol_dict[vol_id]['replicate']:
            return True
        for sim_vol in self.vol_dict.values():
            if 'replicate' in sim_vol.keys():
                if vol_id in sim_vol['replicate'].keys():
                    return True
        return False

    def volume_child_dependency_rm(self, vol_id, flags=0):
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % vol_id)
        if 'replicate' in self.vol_dict[vol_id].keys() and \
           self.vol_dict[vol_id]['replicate']:
            del self.vol_dict[vol_id]['replicate']

        for sim_vol in self.vol_dict.values():
            if 'replicate' in sim_vol.keys():
                if vol_id in sim_vol['replicate'].keys():
                    del sim_vol['replicate'][vol_id]
        return None

    def ags(self, flags=0):
        return self.ag_dict.values()

    def _check_dup_init(self, init_id):
        for sim_ag in self.ag_dict.values():
            if init_id in sim_ag['init_ids']:
                raise LsmError(ErrorNumber.EXISTS_INITIATOR,
                               "init_id %s already exist in other "
                               % init_id +
                               "access group %s" % sim_ag['ag_id'])
    def _check_dup_name(self, sim_list, name, error_num):
        used_names = [x['name'] for x in sim_list]
        if name in used_names:
            raise LsmError(error_num, "Name '%s' already in use" % name)

    def access_group_create(self, name, init_id, init_type, sys_id, flags=0):
        self._check_dup_name(
            self.ag_dict.values(), name, ErrorNumber.EXISTS_ACCESS_GROUP)
        self._check_dup_init(init_id)
        sim_ag = dict()
        sim_ag['init_ids'] = [init_id]
        sim_ag['init_type'] = init_type
        sim_ag['sys_id'] = SimData.SIM_DATA_SYS_ID
        sim_ag['name'] = name
        sim_ag['ag_id'] = self._next_ag_id()
        self.ag_dict[sim_ag['ag_id']] = sim_ag
        return sim_ag

    def access_group_delete(self, ag_id, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found")
        del(self.ag_dict[ag_id])
        return None

    def access_group_initiator_add(self, ag_id, init_id, init_type, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found")
        if init_id in self.ag_dict[ag_id]['init_ids']:
            return None

        self._check_dup_init(init_id)

        self.ag_dict[ag_id]['init_ids'].extend([init_id])
        return None

    def access_group_initiator_delete(self, ag_id, init_id, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found: %s" % ag_id)

        if init_id in self.ag_dict[ag_id]['init_ids']:
            new_init_ids = []
            for cur_init_id in self.ag_dict[ag_id]['init_ids']:
                if cur_init_id != init_id:
                    new_init_ids.extend([cur_init_id])
            del(self.ag_dict[ag_id]['init_ids'])
            self.ag_dict[ag_id]['init_ids'] = new_init_ids
        return None

    def volume_mask(self, ag_id, vol_id, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found: %s" % ag_id)
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % vol_id)
        if 'mask' not in self.vol_dict[vol_id].keys():
            self.vol_dict[vol_id]['mask'] = dict()

        self.vol_dict[vol_id]['mask'][ag_id] = 2
        return None

    def volume_unmask(self, ag_id, vol_id, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found: %s" % ag_id)
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % vol_id)
        if 'mask' not in self.vol_dict[vol_id].keys():
            return None

        if ag_id not in self.vol_dict[vol_id]['mask'].keys():
            return None

        del(self.vol_dict[vol_id]['mask'][ag_id])
        return None

    def volumes_accessible_by_access_group(self, ag_id, flags=0):
        # We don't check wether ag_id is valid
        rc = []
        for sim_vol in self.vol_dict.values():
            if 'mask' not in sim_vol:
                continue
            if ag_id in sim_vol['mask'].keys():
                rc.extend([sim_vol])
        return rc

    def access_groups_granted_to_volume(self, vol_id, flags=0):
        # We don't check wether vold_id is valid
        sim_ags = []
        if 'mask' in self.vol_dict[vol_id].keys():
            ag_ids = self.vol_dict[vol_id]['mask'].keys()
            for ag_id in ag_ids:
                sim_ags.extend([self.ag_dict[ag_id]])
        return sim_ags

    def _ag_ids_of_init(self, init_id):
        """
        Find out the access groups defined initiator belong to.
        Will return a list of access group id or []
        """
        rc = []
        for sim_ag in self.ag_dict.values():
            if init_id in sim_ag['init_ids']:
                rc.extend([sim_ag['ag_id']])
        return rc

    def iscsi_chap_auth(self, init_id, in_user, in_pass, out_user, out_pass,
                        flags=0):
        # to_code
        if self.init_dict[init_id]['init_type'] != Initiator.TYPE_ISCSI:
            raise LsmError(ErrorNumber.UNSUPPORTED_INITIATOR_TYPE,
                           "Initiator %s is not an iSCSI IQN" % init_id)
        # No iscsi chap query API yet
        return None

    def fs(self):
        return self.fs_dict.values()

    def fs_create(self, pool_id, fs_name, size_bytes, flags=0):
        size_bytes = SimData._block_rounding(size_bytes)
        # check free size
        free_space = self.pool_free_space(pool_id)
        if (free_space < size_bytes):
            raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                           "Insufficient space in pool")
        sim_fs = dict()
        fs_id = self._next_fs_id()
        sim_fs['fs_id'] = fs_id
        sim_fs['name'] = fs_name
        sim_fs['total_space'] = size_bytes
        sim_fs['free_space'] = size_bytes
        sim_fs['sys_id'] = SimData.SIM_DATA_SYS_ID
        sim_fs['pool_id'] = pool_id
        sim_fs['consume_size'] = size_bytes
        self.fs_dict[fs_id] = sim_fs
        return sim_fs

    def fs_delete(self, fs_id, flags=0):
        if fs_id in self.fs_dict.keys():
            del(self.fs_dict[fs_id])
            return
        raise LsmError(ErrorNumber.INVALID_FS,
                       "No such File System: %s" % fs_id)

    def fs_resize(self, fs_id, new_size_bytes, flags=0):
        new_size_bytes = SimData._block_rounding(new_size_bytes)
        if fs_id in self.fs_dict.keys():
            pool_id = self.fs_dict[fs_id]['pool_id']
            free_space = self.pool_free_space(pool_id)
            if (free_space < new_size_bytes):
                raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                               "Insufficient space in pool")

            self.fs_dict[fs_id]['total_space'] = new_size_bytes
            self.fs_dict[fs_id]['free_space'] = new_size_bytes
            self.fs_dict[fs_id]['consume_size'] = new_size_bytes
            return self.fs_dict[fs_id]
        raise LsmError(ErrorNumber.INVALID_VOLUME,
                       "No such File System: %s" % fs_id)

    def fs_clone(self, src_fs_id, dst_fs_name, snap_id, flags=0):
        if src_fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % src_fs_id)
        if snap_id and snap_id not in self.snap_dict.keys():
            raise LsmError(ErrorNumber.INVALID_SS,
                           "No such Snapshot: %s" % snap_id)
        src_sim_fs = self.fs_dict[src_fs_id]
        dst_sim_fs = self.fs_create(
            src_sim_fs['pool_id'], dst_fs_name, src_sim_fs['total_space'], 0)
        if 'clone' not in src_sim_fs.keys():
            src_sim_fs['clone'] = dict()
        src_sim_fs['clone'][dst_sim_fs['fs_id']] = {
            'snap_id': snap_id,
        }
        return dst_sim_fs

    def fs_file_clone(self, fs_id, src_fs_name, dst_fs_name, snap_id, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % fs_id)
        if snap_id and snap_id not in self.snap_dict.keys():
            raise LsmError(ErrorNumber.INVALID_SS,
                           "No such Snapshot: %s" % snap_id)
        # TODO: No file clone query API yet, no need to do anything internally
        return None

    def fs_snapshots(self, fs_id, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % fs_id)
        rc = []
        if 'snaps' in self.fs_dict[fs_id].keys():
            for snap_id in self.fs_dict[fs_id]['snaps']:
                rc.extend([self.snap_dict[snap_id]])
        return rc

    def fs_snapshot_create(self, fs_id, snap_name, files, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % fs_id)
        if 'snaps' not in self.fs_dict[fs_id].keys():
            self.fs_dict[fs_id]['snaps'] = []

        snap_id = self._next_snap_id()
        sim_snap = dict()
        sim_snap['snap_id'] = snap_id
        sim_snap['name'] = snap_name
        if files is None:
            sim_snap['files'] = []
        else:
            sim_snap['files'] = files
        sim_snap['timestamp'] = time.time()
        self.snap_dict[snap_id] = sim_snap
        self.fs_dict[fs_id]['snaps'].extend([snap_id])
        return sim_snap

    def fs_snapshot_delete(self, fs_id, snap_id, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % fs_id)
        if snap_id not in self.snap_dict.keys():
            raise LsmError(ErrorNumber.INVALID_SS,
                           "No such Snapshot: %s" % snap_id)
        del self.snap_dict[snap_id]
        new_snap_ids = []
        for old_snap_id in self.fs_dict[fs_id]['snaps']:
            if old_snap_id != snap_id:
                new_snap_ids.extend([old_snap_id])
        self.fs_dict[fs_id]['snaps'] = new_snap_ids
        return None

    def fs_snapshot_restore(self, fs_id, snap_id, files, restore_files,
                           flag_all_files, flags):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % fs_id)
        if snap_id not in self.snap_dict.keys():
            raise LsmError(ErrorNumber.INVALID_SS,
                           "No such Snapshot: %s" % snap_id)
        # Nothing need to done internally for restore.
        return None

    def fs_child_dependency(self, fs_id, files, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % fs_id)
        if 'snaps' not in self.fs_dict[fs_id].keys():
            return False
        if files is None or len(files) == 0:
            if len(self.fs_dict[fs_id]['snaps']) >= 0:
                return True
        else:
            for req_file in files:
                for snap_id in self.fs_dict[fs_id]['snaps']:
                    if len(self.snap_dict[snap_id]['files']) == 0:
                        # We are snapshoting all files
                        return True
                    if req_file in self.snap_dict[snap_id]['files']:
                        return True
        return False

    def fs_child_dependency_rm(self, fs_id, files, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % fs_id)
        if 'snaps' not in self.fs_dict[fs_id].keys():
            return None
        if files is None or len(files) == 0:
            if len(self.fs_dict[fs_id]['snaps']) >= 0:
                snap_ids = self.fs_dict[fs_id]['snaps']
                for snap_id in snap_ids:
                    del self.snap_dict[snap_id]
                del self.fs_dict[fs_id]['snaps']
        else:
            for req_file in files:
                snap_ids_to_rm = []
                for snap_id in self.fs_dict[fs_id]['snaps']:
                    if len(self.snap_dict[snap_id]['files']) == 0:
                        # BUG: if certain snapshot is againsting all files,
                        #      what should we do if user request remove
                        #      dependency on certain files.
                        #      Currently, we do nothing
                        return None
                    if req_file in self.snap_dict[snap_id]['files']:
                        new_files = []
                        for old_file in self.snap_dict[snap_id]['files']:
                            if old_file != req_file:
                                new_files.extend([old_file])
                        if len(new_files) == 0:
                        # all files has been removed from snapshot list.
                            snap_ids_to_rm.extend([snap_id])
                        else:
                            self.snap_dict[snap_id]['files'] = new_files
                for snap_id in snap_ids_to_rm:
                    del self.snap_dict[snap_id]

                new_snap_ids = []
                for cur_snap_id in self.fs_dict[fs_id]['snaps']:
                    if cur_snap_id not in snap_ids_to_rm:
                        new_snap_ids.extend([cur_snap_id])
                if len(new_snap_ids) == 0:
                    del self.fs_dict[fs_id]['snaps']
                else:
                    self.fs_dict[fs_id]['snaps'] = new_snap_ids
        return None

    def exports(self, flags=0):
        return self.exp_dict.values()

    def fs_export(self, fs_id, exp_path, root_hosts, rw_hosts, ro_hosts,
                  anon_uid, anon_gid, auth_type, options, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % fs_id)
        sim_exp = dict()
        sim_exp['exp_id'] = self._next_exp_id()
        sim_exp['fs_id'] = fs_id
        if exp_path is None:
            sim_exp['exp_path'] = "/%s" % sim_exp['exp_id']
        else:
            sim_exp['exp_path'] = exp_path
        sim_exp['auth_type'] = auth_type
        sim_exp['root_hosts'] = root_hosts
        sim_exp['rw_hosts'] = rw_hosts
        sim_exp['ro_hosts'] = ro_hosts
        sim_exp['anon_uid'] = anon_uid
        sim_exp['anon_gid'] = anon_gid
        sim_exp['options'] = options
        self.exp_dict[sim_exp['exp_id']] = sim_exp
        return sim_exp

    def fs_unexport(self, exp_id, flags=0):
        if exp_id not in self.exp_dict.keys():
            raise LsmError(ErrorNumber.INVALID_NFS,
                           "No such NFS Export: %s" % exp_id)
        del self.exp_dict[exp_id]
        return None

    def _free_disks_list(self, disk_type=Disk.DISK_TYPE_UNKNOWN):
        """
        Return a list of free sim_disk.
        Return [] if no free disk found.
        """
        free_sim_disks = []
        for sim_disk in self.disk_dict.values():
            if disk_type != Disk.DISK_TYPE_UNKNOWN and \
               sim_disk['disk_type'] != disk_type:
                continue
            flag_free = True
            for sim_pool in self.pool_dict.values():
                if Pool.member_type_is_disk(sim_pool['member_type']) and \
                   sim_disk['disk_id'] in sim_pool['member_ids']:
                    flag_free = False
                    break
            if flag_free is True:
                free_sim_disks.extend([sim_disk])
        return sorted(free_sim_disks, key=lambda k: (k['disk_id']))

    def _free_disks(self, disk_type=Disk.DISK_TYPE_UNKNOWN):
        """
        Return a dictionary like this:
            {
                Disk.DISK_TYPE_XXX: {
                    Disk.total_space:   [sim_disk, ]
                }
            }
        Return None if not free.
        """
        free_sim_disks = self._free_disks_list()
        rc = dict()
        for sim_disk in free_sim_disks:
            if disk_type != Disk.DISK_TYPE_UNKNOWN and \
               sim_disk['disk_type'] != disk_type:
                continue

            cur_type = sim_disk['disk_type']
            cur_size = sim_disk['total_space']

            if cur_type not in rc.keys():
                rc[cur_type] = dict()

            if cur_size not in rc[cur_type]:
                rc[cur_type][cur_size] = []

            rc[cur_type][cur_size].extend([sim_disk])

        return rc

    def _free_volumes_list(self):
        rc = []
        for sim_vol in self.vol_dict.values():
            flag_free = True
            for sim_pool in self.pool_dict.values():
                if sim_pool['member_type'] == Pool.MEMBER_TYPE_VOLUME and \
                   sim_vol['vol_id'] in sim_pool['member_ids']:
                    flag_free = False
                    break
            if flag_free:
                rc.extend([sim_vol])
        return sorted(rc, key=lambda k: (k['vol_id']))

    def _free_volumes(self, size_bytes=0):
        """
        We group sim_vol based on theri total_space because RAID (except
        JBOD) require member in the same spaces.
        Return a dictionary like this:
            {
                sim_vol['total_space']: [sim_vol, ]
            }
        """
        free_sim_vols = self._free_volumes_list()
        if len(free_sim_vols) <= 0:
            return dict()
        rc = dict()
        for sim_vol in free_sim_vols:
            if size_bytes != 0 and sim_vol['total_space'] != size_bytes:
                continue
            # TODO: one day we will introduce free_size of Volume.
            #       in that case we will check whether
            #           total_space == vol_free_size(sim_vol['vol_id'])

            if sim_vol['total_space'] not in rc.keys():
                rc[sim_vol['total_space']] = []
            rc[sim_vol['total_space']].extend([sim_vol])
        return rc

    def _free_pools_list(self):
        """
        Return a list of sim_pool or []
        """
        free_sim_pools = []
        for sim_pool in self.pool_dict.values():
            # TODO: one day we will introduce free_size of Volume.
            #       in that case we will check whether
            #           total_space == pool_free_size(sim_pool['pool_id'])
            pool_id = sim_pool['pool_id']
            if self.pool_free_space(pool_id) > 0:
                free_sim_pools.extend([sim_pool])
        return sorted(
            free_sim_pools,
            key=lambda k: (k['pool_id'].isupper(), k['pool_id']))

    def _pool_create_from_disks(self, pool_name, member_ids, raid_type,
                                raise_error=False):
        # Check:
        #   1. The disk_id is valid
        #   2. All disks are the same disk type.
        #   3. All disks are free.
        #   4. All disks' total space is the same.
        if len(member_ids) <= 0:
            if raise_error:
                raise LsmError(ErrorNumber.INVALID_DISK,
                               "No disk ID defined")
            else:
                return None

        if raid_type == Pool.RAID_TYPE_NOT_APPLICABLE and \
           len(member_ids) >= 2:
            if raise_error:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               "Pool.RAID_TYPE_NOT_APPLICABLE means only 1 " +
                               "member, but got 2 or more: %s" %
                               ', '.join(member_ids))
            else:
                return None

        current_disk_type = None
        current_total_space = None
        for disk_id in member_ids:
            if disk_id not in self.disk_dict.keys():
                if raise_error:
                    raise LsmError(ErrorNumber.INVALID_DISK,
                                   "The disk ID %s does not exist" % disk_id)
                else:
                    return None
            sim_disk = self.disk_dict[disk_id]
            if current_disk_type is None:
                current_disk_type = sim_disk['disk_type']
            elif current_disk_type != sim_disk['disk_type']:
                if raise_error:
                    raise LsmError(
                        ErrorNumber.NO_SUPPORT,
                        "Mixing disk types in one pool " +
                        "is not supported: %s and %s" %
                        (Disk.disk_type_to_str(current_disk_type),
                         Disk.disk_type_to_str(sim_disk['disk_type'])))
                else:
                    return None
            if current_total_space is None:
                current_total_space = sim_disk['total_space']
            elif current_total_space != sim_disk['total_space']:
                if raise_error:
                    raise LsmError(ErrorNumber.NO_SUPPORT,
                                   "Mixing different size of disks is not " +
                                   "supported")
                else:
                    return None

        all_free_disks = self._free_disks_list()
        if all_free_disks is None:
            if raise_error:
                raise LsmError(ErrorNumber.DISK_BUSY,
                               "No free disk to create new pool")
            else:
                return None
        all_free_disk_ids = [d['disk_id'] for d in all_free_disks]
        for disk_id in member_ids:
            if disk_id not in all_free_disk_ids:
                if raise_error:
                    raise LsmError(ErrorNumber.DISK_BUSY,
                                   "Disk %s is used by other pool" % disk_id)
                else:
                    return None

        if raid_type == Pool.RAID_TYPE_UNKNOWN or \
           raid_type == Pool.RAID_TYPE_MIXED:
            if raise_error:
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               "RAID type %s(%d) is not supported" %
                               (Pool.raid_type_to_str(raid_type), raid_type))
            else:
                return None

        pool_id = self._next_pool_id()
        if pool_name == '':
            pool_name = 'POOL %s' % SimData._random_vpd(4)

        sim_pool = dict()
        sim_pool['name'] = pool_name
        sim_pool['pool_id'] = pool_id
        if len(member_ids) == 1:
            sim_pool['raid_type'] = Pool.RAID_TYPE_NOT_APPLICABLE
        else:
            sim_pool['raid_type'] = raid_type
        sim_pool['member_ids'] = member_ids
        sim_pool['member_type'] = \
            Pool.disk_type_to_member_type(current_disk_type)
        sim_pool['sys_id'] = SimData.SIM_DATA_SYS_ID
        sim_pool['element_type'] = SimData.SIM_DATA_POOL_ELEMENT_TYPE
        sim_pool['status'] = SimData.SIM_DATA_POOL_STATUS
        sim_pool['status_info'] = SimData.SIM_DATA_POOL_STATUS_INFO
        self.pool_dict[pool_id] = sim_pool
        return sim_pool

    def pool_create_from_disks(self, sys_id, pool_name, member_ids, raid_type,
                               flags=0):
        """
        return newly create sim_pool or None.
        """
        if sys_id != SimData.SIM_DATA_SYS_ID:
            raise LsmError(ErrorNumber.INVALID_SYSTEM,
                           "No such system: %s" % sys_id)

        return self._pool_create_from_disks(pool_name, member_ids, raid_type,
                                            raise_error=True)

    def _pool_create_from_volumes(self, pool_name, member_ids, raid_type,
                                  raise_error=False):
        # Check:
        #   1. The vol_id is valid
        #   3. All volumes are free.
        if len(member_ids) == 0:
            if raise_error:
                raise LsmError(ErrorNumber.INVALID_VOLUME,
                               "No volume ID defined")
            else:
                return None

        if raid_type == Pool.RAID_TYPE_NOT_APPLICABLE and \
           len(member_ids) >= 2:
            if raise_error:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               "Pool.RAID_TYPE_NOT_APPLICABLE means only 1 " +
                               "member, but got 2 or more: %s" %
                               ', '.join(member_ids))
            else:
                return None

        current_vol_size = None
        for vol_id in member_ids:
            if vol_id not in self.vol_dict.keys() and raise_error:
                if raise_error:
                    raise LsmError(ErrorNumber.INVALID_DISK,
                                   "The vol ID %s does not exist" % vol_id)
                else:
                    return None
            sim_vol = self.vol_dict[vol_id]
            if current_vol_size is None:
                current_vol_size = sim_vol['total_space']
            elif current_vol_size != sim_vol['total_space']:
                if raise_error:
                    raise LsmError(ErrorNumber.NO_SUPPORT,
                                   "Mixing volume size in one pool " +
                                   "is not supported: %d and %d" %
                                   (current_vol_size, sim_vol['total_space']))
                else:
                    return None

        all_free_vols = self._free_volumes_list()
        if all_free_vols is None:
            if raise_error:
                raise LsmError(ErrorNumber.VOLUME_BUSY,
                               "No free volume to create new pool")
            else:
                return None
        all_free_vol_ids = [v['vol_id'] for v in all_free_vols]
        for vol_id in member_ids:
            if vol_id not in all_free_vol_ids:
                if raise_error:
                    raise LsmError(ErrorNumber.VOLUME_BUSY,
                                   "Volume %s is used by other pool" % vol_id)
                else:
                    return None

        if raid_type == Pool.RAID_TYPE_UNKNOWN or \
           raid_type == Pool.RAID_TYPE_MIXED:
            if raise_error:
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               "RAID type %s(%d) is not supported" %
                               (Pool.raid_type_to_str(raid_type), raid_type))
            else:
                return None

        pool_id = self._next_pool_id()
        if pool_name == '':
            pool_name = 'POOL %s' % SimData._random_vpd(4)
        sim_pool = dict()
        sim_pool['name'] = pool_name
        sim_pool['pool_id'] = pool_id
        if len(member_ids) == 1:
            sim_pool['raid_type'] = Pool.RAID_TYPE_NOT_APPLICABLE
        else:
            sim_pool['raid_type'] = raid_type
        sim_pool['member_ids'] = member_ids
        sim_pool['member_type'] = Pool.MEMBER_TYPE_VOLUME
        sim_pool['sys_id'] = SimData.SIM_DATA_SYS_ID
        sim_pool['element_type'] = SimData.SIM_DATA_POOL_ELEMENT_TYPE
        sim_pool['status'] = SimData.SIM_DATA_POOL_STATUS
        sim_pool['status_info'] = SimData.SIM_DATA_POOL_STATUS_INFO
        self.pool_dict[pool_id] = sim_pool
        return sim_pool

    def pool_create_from_volumes(self, sys_id, pool_name, member_ids,
                                 raid_type, flags=0):
        if sys_id != SimData.SIM_DATA_SYS_ID:
            raise LsmError(ErrorNumber.INVALID_SYSTEM,
                           "No such system: %s" % sys_id)
        return self._pool_create_from_volumes(pool_name, member_ids, raid_type,
                                              raise_error=True)

    def _pool_create_from_pool(self, pool_name, member_id,
                               size_bytes, raise_error=False):

        size_bytes = SimData._block_rounding(size_bytes)
        free_sim_pools = self._free_pools_list()
        free_sim_pool_ids = [p['pool_id'] for p in free_sim_pools]
        if len(free_sim_pool_ids) == 0 or \
           member_id not in free_sim_pool_ids:
            if raise_error:
                raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                               "Pool %s " % member_id +
                               "is full, no space to create new pool")
            else:
                return None

        free_size = self.pool_free_space(member_id)
        if free_size < size_bytes:
            raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                           "Pool %s does not have requested free" %
                           member_id + "to create new pool")

        pool_id = self._next_pool_id()
        if pool_name == '':
            pool_name = 'POOL %s' % SimData._random_vpd(4)
        sim_pool = dict()
        sim_pool['name'] = pool_name
        sim_pool['pool_id'] = pool_id
        sim_pool['raid_type'] = Pool.RAID_TYPE_NOT_APPLICABLE
        sim_pool['member_ids'] = [member_id]
        sim_pool['member_type'] = Pool.MEMBER_TYPE_POOL
        sim_pool['member_size'] = size_bytes
        sim_pool['sys_id'] = SimData.SIM_DATA_SYS_ID
        sim_pool['element_type'] = SimData.SIM_DATA_POOL_ELEMENT_TYPE
        sim_pool['status'] = SimData.SIM_DATA_POOL_STATUS
        sim_pool['status_info'] = SimData.SIM_DATA_POOL_STATUS_INFO
        self.pool_dict[pool_id] = sim_pool
        return sim_pool

    def pool_create_from_pool(self, sys_id, pool_name, member_id, size_bytes,
                              flags=0):
        if sys_id != SimData.SIM_DATA_SYS_ID:
            raise LsmError(ErrorNumber.INVALID_SYSTEM,
                           "No such system: %s" % sys_id)
        return self._pool_create_from_pool(pool_name, member_id, size_bytes,
                                           raise_error=True)

    def _auto_choose_disk(self, size_bytes, raid_type, disk_type,
                          raise_error=False):
        """
        Return a list of member ids suitable for creating RAID pool with
        required size_bytes.
        Return [] if nothing found.
        if raise_error is True, raise error if not found
        """
        disk_type_str = "disk"
        if disk_type != Disk.DISK_TYPE_UNKNOWN:
            disk_type_str = "disk(type: %s)" % Disk.disk_type_to_str(disk_type)

        if raid_type == Pool.RAID_TYPE_NOT_APPLICABLE:
        # NOT_APPLICABLE means pool will only contain one disk.
            sim_disks = self._free_disks_list(disk_type)
            if len(sim_disks) == 0:
                if raise_error:
                    raise LsmError(ErrorNumber.DISK_BUSY,
                                   "No free %s found" % disk_type_str)
                else:
                    return []

            for sim_disk in sim_disks:
                if sim_disk['total_space'] >= size_bytes:
                    return [sim_disk]
            if raise_error:
                raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                               "No %s is bigger than " % disk_type_str +
                               "expected size: %s(%d)" %
                               (size_bytes_2_size_human(size_bytes),
                                size_bytes))
            else:
                return []

        if raid_type == Pool.RAID_TYPE_JBOD:
        # JBOD does not require all disks in the same size or the same type.
            sim_disks = self._free_disks_list(disk_type)
            if len(sim_disks) == 0:
                if raise_error:
                    raise LsmError(ErrorNumber.DISK_BUSY,
                                   "No free %s found" % disk_type_str)
                else:
                    return []

            chose_sim_disks = []
            all_free_size = 0
            for sim_disk in sim_disks:
                chose_sim_disks.extend([sim_disk])
                all_free_size += sim_disk['total_space']
                if all_free_size >= size_bytes:
                    return chose_sim_disks
            if raise_error:
                raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                               "No enough %s to provide size %s(%d)" %
                               (disk_type_str,
                                size_bytes_2_size_human(size_bytes),
                                size_bytes))
            else:
                return []

        # All rest RAID type require member are in the same size and same
        # type.
        sim_disks_struct = self._free_disks(disk_type)
        for cur_disk_type in sim_disks_struct.keys():
            for cur_disk_size in sim_disks_struct[cur_disk_type].keys():
                cur_sim_disks = sim_disks_struct[cur_disk_type][cur_disk_size]
                if len(cur_sim_disks) == 0:
                    continue
                chose_sim_disks = []
                for member_count in range(1, len(cur_sim_disks) + 1):
                    partial_sim_disks = cur_sim_disks[0:member_count]
                    member_ids = [x['disk_id'] for x in partial_sim_disks]
                    raid_actual_size = self._size_of_raid(
                        Pool.MEMBER_TYPE_DISK, member_ids, raid_type)
                    if size_bytes <= raid_actual_size:
                        return cur_sim_disks[0:member_count]

        if raise_error:
            raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                           "No enough %s " % disk_type_str +
                           "to create %s providing size: %s(%d)" %
                           (Pool.raid_type_to_str(raid_type),
                            size_bytes_2_size_human(size_bytes),
                            size_bytes))
        else:
            return []

    def _auto_choose_vol(self, size_bytes, raid_type, raise_error=False):
        """
        Return a list of member ids suitable for creating RAID pool with
        required size_bytes.
        Return [] if nothing found.
        Raise LsmError if raise_error is True
        """
        if raid_type == Pool.RAID_TYPE_NOT_APPLICABLE:
        # NOT_APPLICABLE means pool will only contain one volume.
            sim_vols = self._free_volumes_list()
            if len(sim_vols) == 0:
                if raise_error:
                    raise LsmError(ErrorNumber.VOLUME_BUSY,
                                   "No free volume found")
                else:
                    return []
            for sim_vol in sim_vols:
                if sim_vol['total_space'] >= size_bytes:
                    return [sim_vol]

            if raise_error:
                raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                               "No volume is bigger than expected size: " +
                               "%s(%d)" %
                               (size_bytes_2_size_human(size_bytes),
                                size_bytes))
            else:
                return []

        if raid_type == Pool.RAID_TYPE_JBOD:
        # JBOD does not require all vols in the same size.
            sim_vols = self._free_volumes_list()
            if len(sim_vols) == 0:
                if raise_error:
                    raise LsmError(ErrorNumber.VOLUME_BUSY,
                                   "No free volume found")
                else:
                    return []

            chose_sim_vols = []
            all_free_size = 0
            for sim_vol in sim_vols:
                chose_sim_vols.extend([sim_vol])
                all_free_size += sim_vol['total_space']
                if all_free_size >= size_bytes:
                    return chose_sim_vols

            if raise_error:
                raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                               "No enough volumes to provide size %s(%d)" %
                               (size_bytes_2_size_human(size_bytes),
                                size_bytes))
            else:
                return []

        # Rest RAID types require volume to be the same size.
        sim_vols_dict = self._free_volumes()
        for vol_size in sim_vols_dict.keys():
            sim_vols = sim_vols_dict[vol_size]
            if len(sim_vols) == 0:
                continue
            for member_count in range(1, len(sim_vols) + 1):
                partial_sim_vols = sim_vols[0:member_count]
                member_ids = [v['vol_id'] for v in partial_sim_vols]
                raid_actual_size = self._size_of_raid(
                    Pool.MEMBER_TYPE_VOLUME, member_ids, raid_type)
                if size_bytes <= raid_actual_size:
                    return sim_vols[0:member_count]

        if raise_error:
            raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                           "No enough volumes to create "
                           "%s providing size: %s(%d)" %
                           (Pool.raid_type_to_str(raid_type),
                            size_bytes_2_size_human(size_bytes),
                            size_bytes))
        else:
            return []

    def _auto_choose_pool(self, size_bytes, raise_error=False):
        """
        Return a sim_pool.
        Return None if not found.
        """
        sim_pools = self._free_pools_list()
        if len(sim_pools) >= 1:
            for sim_pool in sim_pools:
                pool_id = sim_pool['pool_id']
                free_size = self.pool_free_space(pool_id)
                if free_size >= size_bytes:
                    return sim_pool

        if raise_error:
            raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                           "No pool is bigger than expected size: " +
                           "%s(%d)" %
                           (size_bytes_2_size_human(size_bytes),
                            size_bytes))
        else:
            return None

    def pool_create(self, sys_id, pool_name, size_bytes,
                    raid_type=Pool.RAID_TYPE_UNKNOWN,
                    member_type=Pool.MEMBER_TYPE_UNKNOWN, flags=0):
        if sys_id != SimData.SIM_DATA_SYS_ID:
            raise LsmError(ErrorNumber.INVALID_SYSTEM,
                           "No such system: %s" % sys_id)

        size_bytes = SimData._block_rounding(size_bytes)

        raise_error = False
        if member_type != Pool.MEMBER_TYPE_UNKNOWN:
            raise_error = True

        if member_type == Pool.MEMBER_TYPE_UNKNOWN or \
           Pool.member_type_is_disk(member_type):
            disk_raid_type = raid_type
            if raid_type == Pool.RAID_TYPE_UNKNOWN:
                disk_raid_type = SimData.SIM_DATA_DISK_DEFAULT_RAID
            if member_type == Pool.MEMBER_TYPE_UNKNOWN:
                disk_type = Disk.DISK_TYPE_UNKNOWN
            else:
                disk_type = Pool.member_type_to_disk_type(member_type)
            sim_disks = self._auto_choose_disk(
                size_bytes, disk_raid_type, disk_type, raise_error)
            if len(sim_disks) >= 1:
                member_ids = [d['disk_id'] for d in sim_disks]
                sim_pool = self._pool_create_from_disks(
                    pool_name, member_ids, disk_raid_type, raise_error)
                if sim_pool:
                    return sim_pool

        if member_type == Pool.MEMBER_TYPE_UNKNOWN or \
           member_type == Pool.MEMBER_TYPE_VOLUME:
            vol_raid_type = raid_type
            if raid_type == Pool.RAID_TYPE_UNKNOWN:
                vol_raid_type = SimData.SIM_DATA_VOLUME_DEFAULT_RAID
            sim_vols = self._auto_choose_vol(
                size_bytes, vol_raid_type, raise_error)
            if len(sim_vols) >= 1:
                member_ids = [v['vol_id'] for v in sim_vols]
                sim_pool = self._pool_create_from_volumes(
                    pool_name, member_ids, vol_raid_type, raise_error)
                if sim_pool:
                    return sim_pool

        if member_type == Pool.MEMBER_TYPE_POOL:
            if raid_type != Pool.RAID_TYPE_UNKNOWN and \
               raid_type != Pool.RAID_TYPE_NOT_APPLICABLE:
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               "Pool based pool does not support " +
                               "raid_type: %s(%d)" %
                               (Pool.raid_type_to_str(raid_type),
                                raid_type))

        if member_type == Pool.MEMBER_TYPE_UNKNOWN:
            if raid_type != Pool.RAID_TYPE_UNKNOWN and \
               raid_type != Pool.RAID_TYPE_NOT_APPLICABLE:
                raise LsmError(ErrorNumber.NO_SUPPORT,
                               "No enough free disk or volume spaces " +
                               "to create new pool. And pool based " +
                               "pool does not support raid_type: %s" %
                               Pool.raid_type_to_str(raid_type))

        member_sim_pool = self._auto_choose_pool(size_bytes, raise_error)
        if member_sim_pool:
            member_id = member_sim_pool['pool_id']
            sim_pool = self._pool_create_from_pool(
                pool_name, member_id, size_bytes, raise_error)
            if sim_pool:
                return sim_pool

        # only member_type == Pool.MEMBER_TYPE_UNKNOWN can reach here.
        raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                       "No enough free spaces to create new pool")

    def pool_delete(self, pool_id, flags=0):
        if pool_id not in self.pool_dict.keys():
            raise LsmError(ErrorNumber.INVALID_POOL,
                           "Pool not found: %s" % pool_id)

        volumes = self.volumes()
        for v in volumes:
            if v['pool_id'] == pool_id:
                raise LsmError(ErrorNumber.EXISTS_VOLUME,
                               "Volumes exist on pool")

        del(self.pool_dict[pool_id])
        return None
