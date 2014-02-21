# Copyright (C) 2011-2013 Red Hat, Inc.
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

from common import md5, LsmError, ErrorNumber, size_human_2_size_bytes, \
    JobStatus
from data import System, Volume, Disk, Pool, FileSystem, AccessGroup, \
    Initiator, Snapshot, NfsExport

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
                   self.data.signature != SimData._state_signature():
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

    def set_time_out(self, ms, flags=0):
        return self.data.set_time_out(ms, flags)

    def get_time_out(self, flags=0):
        return self.data.get_time_out(flags)

    def systems(self):
        return self.data.systems()

    @staticmethod
    def _sim_vol_2_lsm(sim_vol):
        return Volume(sim_vol['vol_id'], sim_vol['name'], sim_vol['vpd83'],
                      SimData.SIM_DATA_BLK_SIZE,
                      int(sim_vol['total_space']/SimData.SIM_DATA_BLK_SIZE),
                      Volume.STATUS_OK, sim_vol['sys_id'],
                      sim_vol['pool_id'])

    def volumes(self):
        sim_vols = self.data.volumes()
        return [SimArray._sim_vol_2_lsm(v) for v in sim_vols]

    def pools(self):
        rc = []
        sim_pools = self.data.pools()
        for sim_pool in sim_pools:
            pool = Pool(sim_pool['pool_id'], sim_pool['name'],
                        sim_pool['total_space'], sim_pool['free_space'],
                        sim_pool['status'], sim_pool['sys_id'])
            rc.extend([pool])
        return rc

    def disks(self):
        rc = []
        sim_disks = self.data.disks()
        for sim_disk in sim_disks:
            disk = Disk(sim_disk['disk_id'], sim_disk['name'],
                        sim_disk['disk_type'], SimData.SIM_DATA_BLK_SIZE,
                        int(sim_disk['total_space']/SimData.SIM_DATA_BLK_SIZE),
                        Disk.STATUS_OK, sim_disk['sys_id'])
            rc.extend([disk])
        return rc

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
        return [SimArray._sim_fs_2_lsm(f) for f in sim_fss]

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

    def file_clone(self, fs_id, src_fs_name, dst_fs_name, snap_id, flags=0):
        return self.data.job_create(
            self.data.file_clone(
                fs_id, src_fs_name, dst_fs_name, snap_id, flags))[0]

    @staticmethod
    def _sim_snap_2_lsm(sim_snap):
        return Snapshot(sim_snap['snap_id'], sim_snap['name'],
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

    def fs_snapshot_revert(self, fs_id, snap_id, files, restore_files,
                           flag_all_files, flags):
        return self.data.job_create(
            self.data.fs_snapshot_revert(
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
        return [SimArray._sim_exp_2_lsm(e) for e in sim_exps]

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
                           sim_ag['init_ids'], sim_ag['sys_id'])

    def ags(self):
        sim_ags = self.data.ags()
        return [SimArray._sim_ag_2_lsm(a) for a in sim_ags]

    def access_group_create(self, name, init_id, init_type, sys_id, flags=0):
        sim_ag = self.data.access_group_create(
            name, init_id, init_type, sys_id, flags)
        return SimArray._sim_ag_2_lsm(sim_ag)

    def access_group_del(self, ag_id, flags=0):
        return self.data.access_group_del(ag_id, flags)

    def access_group_add_initiator(self, ag_id, init_id, init_type, flags=0):
        return self.data.access_group_add_initiator(
            ag_id, init_id, init_type, flags)

    def access_group_del_initiator(self, ag_id, init_id, flags=0):
        return self.data.access_group_del_initiator(ag_id, init_id, flags)

    def access_group_grant(self, ag_id, vol_id, access, flags=0):
        return self.data.access_group_grant(ag_id, vol_id, access, flags)

    def access_group_revoke(self, ag_id, vol_id, flags=0):
        return self.data.access_group_revoke(ag_id, vol_id, flags)

    def volumes_accessible_by_access_group(self, ag_id, flags=0):
        sim_vols = self.data.volumes_accessible_by_access_group(ag_id, flags)
        return [SimArray._sim_vol_2_lsm(v) for v in sim_vols]

    def access_groups_granted_to_volume(self, vol_id, flags=0):
        sim_ags = self.data.access_groups_granted_to_volume(vol_id, flags)
        return [SimArray._sim_ag_2_lsm(a) for a in sim_ags]

    @staticmethod
    def _sim_init_2_lsm(sim_init):
        return Initiator(sim_init['init_id'], sim_init['init_type'],
                         sim_init['name'])

    def inits(self, flags=0):
        sim_inits = self.data.inits()
        return [SimArray._sim_init_2_lsm(a) for a in sim_inits]

    def initiator_grant(self, init_id, init_type, vol_id, access, flags=0):
        return self.data.initiator_grant(
            init_id, init_type, vol_id, access, flags)

    def initiator_revoke(self, init_id, vol_id, flags=0):
        return self.data.initiator_revoke(init_id, vol_id, flags)

    def volumes_accessible_by_initiator(self, init_id, flags=0):
        sim_vols = self.data.volumes_accessible_by_initiator(init_id, flags)
        return [SimArray._sim_vol_2_lsm(v) for v in sim_vols]

    def initiators_granted_to_volume(self, vol_id, flags=0):
        sim_inits = self.data.initiators_granted_to_volume(vol_id, flags)
        return [SimArray._sim_init_2_lsm(i) for i in sim_inits]

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
            'vol_id': "VOL_ID_%s" % SimData._random_vpd(4),
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

        self.init_dict = {
            Initiator.id = sim_init,
        }
        sim_init = {
            'init_id': Initiator.id,
            'init_type': Initiator.TYPE_XXXX,
            'name': SimData.SIM_DATA_INIT_NAME,
            'sys_id': SimData.SIM_DATA_SYS_ID,
        }

        self.ag_dict ={
            AccessGroup.id = sim_ag,
        }
        sim_ag = {
            'init_ids': [init_id,],
            'sys_id': SimData.SIM_DATA_SYS_ID,
            'name': name,
            'ag_id': "AG_ID_%s" % SimData._random_vpd(4),
        }

        self.fs_dict = {
            FileSystem.id = sim_fs,
        }
        sim_fs = {
            'fs_id': "FS_ID_%s" % SimData._random_vpd(4),
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
            'snap_id': "SNAP_ID_%s" % SimData._random_vpd(4),
            'name': snap_name,
            'fs_id': fs_id,
            'files': [file_path, ],
            'timestamp': time.time(),
        }
        self.exp_dict = {
            Export.id: sim_exp,
        }
        sim_exp = {
            'exp_id': "EXP_ID_%s" % SimData._random_vpd(4),
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
    """
    SIM_DATA_BLK_SIZE = 512
    SIM_DATA_VERSION = "2.0"
    SIM_DATA_SYS_ID = 'sim-01'
    SIM_DATA_INIT_NAME = 'NULL'
    SIM_DATA_TMO = 30000 # ms

    @staticmethod
    def _state_signature():
        return 'LSM_SIMULATOR_DATA_%s' % md5(SimData.SIM_DATA_VERSION)

    def __init__(self):
        self.tmo = SimData.SIM_DATA_TMO
        self.version = SimData.SIM_DATA_VERSION
        self.signature = SimData._state_signature()
        self.job_num = 0
        self.job_dict = {
            # id: SimJob
        }
        self.syss = [System(SimData.SIM_DATA_SYS_ID,
                            'LSM simulated storage plug-in',
                            System.STATUS_OK)]
        pool_size_200g = size_human_2_size_bytes('200GiB')
        self.pool_dict = {
                'POO1': {
                    'pool_id': 'POO1',
                    'name': 'Pool 1',
                    'member_type': Pool.MEMBER_TYPE_DISK,
                    'member_ids': ['DISK_ID_000', 'DISK_ID_001'],
                    'raid_type': Pool.RAID_TYPE_RAID1,
                    'status': Pool.STATUS_OK,
                    'sys_id': SimData.SIM_DATA_SYS_ID,
                },
                'POO2': {
                    'pool_id': 'POO2',
                    'name': 'Pool 2',
                    'total_space': pool_size_200g,
                    'member_type': Pool.MEMBER_TYPE_POOL,
                    'member_ids': ['POO1'],
                    'raid_type': Pool.RAID_TYPE_NOT_APPLICABLE,
                    'status': Pool.STATUS_OK,
                    'sys_id': SimData.SIM_DATA_SYS_ID,
                },
                # lsm_test_aggr pool is requred by test/runtest.sh
                'lsm_test_aggr': {
                    'pool_id': 'lsm_test_aggr',
                    'name': 'lsm_test_aggr',
                    'member_type': Pool.MEMBER_TYPE_DISK,
                    'member_ids': ['DISK_ID_002', 'DISK_ID_003'],
                    'raid_type': Pool.RAID_TYPE_RAID0,
                    'status': Pool.STATUS_OK,
                    'sys_id': SimData.SIM_DATA_SYS_ID,
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
        self.disk_dict = {
            'DISK_ID_000': {
                'disk_id': 'DISK_ID_000',
                'name': 'SATA Disk 000',
                'total_space': disk_size_2t,
                'disk_type': Disk.DISK_TYPE_SATA,
                'sys_id': SimData.SIM_DATA_SYS_ID,
            },
            'DISK_ID_001': {
                'disk_id': 'DISK_ID_001',
                'name': 'SATA Disk 001',
                'total_space': disk_size_2t,
                'disk_type': Disk.DISK_TYPE_SATA,
                'sys_id': SimData.SIM_DATA_SYS_ID,
            },
            'DISK_ID_002': {
                'disk_id': 'DISK_ID_002',
                'name': 'SAS Disk 002',
                'total_space': disk_size_2t,
                'disk_type': Disk.DISK_TYPE_SAS,
                'sys_id': SimData.SIM_DATA_SYS_ID,
            },
            'DISK_ID_003': {
                'disk_id': 'DISK_ID_003',
                'name': 'SAS Disk 003',
                'total_space': disk_size_2t,
                'disk_type': Disk.DISK_TYPE_SAS,
                'sys_id': SimData.SIM_DATA_SYS_ID,
            },
        }
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

        self.pool_dict['POO3']= {
            'pool_id': 'POO3',
            'name': 'Pool 3',
            'member_type': Pool.MEMBER_TYPE_VOLUME,
            'member_ids': [
                self.vol_dict.values()[0]['vol_id'],
                self.vol_dict.values()[1]['vol_id'],
            ],
            'raid_type': Pool.RAID_TYPE_RAID1,
            'status': Pool.STATUS_OK,
            'sys_id': SimData.SIM_DATA_SYS_ID,
        }

        return

    def _pool_free_space(self, pool_id):
        """
        Calculate out the free size of certain pool.
        """
        free_space = self._pool_total_space(pool_id)
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

    def _pool_total_space(self, pool_id):
        """
        Find out the correct size of RAID pool
        """
        member_type = self.pool_dict[pool_id]['member_type']
        if member_type == Pool.MEMBER_TYPE_POOL:
            return self.pool_dict[pool_id]['total_space']

        all_size = 0
        item_size = 0   # disk size, used by RAID 3/4/5/6
        member_ids = self.pool_dict[pool_id]['member_ids']
        raid_type = self.pool_dict[pool_id]['raid_type']
        member_count = len(member_ids)

        if member_type == Pool.MEMBER_TYPE_DISK:
            for member_id in member_ids:
                all_size += self.disk_dict[member_id]['total_space']
                item_size = self.disk_dict[member_id]['total_space']

        elif member_type == Pool.MEMBER_TYPE_VOLUME:
            for member_id in member_ids:
                all_size += self.vol_dict[member_id]['total_space']
                item_size = self.vol_dict[member_id]['total_space']

        if raid_type == Pool.RAID_TYPE_JBOD:
            return int(all_size)
        elif raid_type == Pool.RAID_TYPE_RAID0:
            return int(all_size)
        elif raid_type == Pool.RAID_TYPE_RAID1 or \
             raid_type == Pool.RAID_TYPE_RAID10:
            return int(all_size/2)
        elif raid_type == Pool.RAID_TYPE_RAID3 or \
             raid_type == Pool.RAID_TYPE_RAID4 or \
             raid_type == Pool.RAID_TYPE_RAID5 or \
             raid_type == Pool.RAID_TYPE_RAID50:
            return int(all_size - item_size)
        elif raid_type == Pool.RAID_TYPE_RAID6 or \
             raid_type == Pool.RAID_TYPE_RAID60:
            return int(all_size - item_size - item_size)
        elif raid_type == Pool.RAID_TYPE_RAID51:
            return int((all_size - item_size)/2)
        elif raid_type == Pool.RAID_TYPE_RAID61:
            return int((all_size - item_size - item_size)/2)
        return 0

    @staticmethod
    def _block_rounding(size_bytes):
        return (size_bytes / SimData.SIM_DATA_BLK_SIZE + 1) * \
            SimData.SIM_DATA_BLK_SIZE

    def job_create(self, returned_item):
        if True:
        #if random.randint(0,5) == 1:
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
        rc = []
        for sim_pool in self.pool_dict.values():
            sim_pool['total_space'] = \
                self._pool_total_space(sim_pool['pool_id'])
            sim_pool['free_space'] = \
                self._pool_free_space(sim_pool['pool_id'])
            rc.extend([sim_pool])
        return rc

    def volumes(self):
        return self.vol_dict.values()

    def disks(self):
        return self.disk_dict.values()

    def access_group_list(self):
        return self.ag_dict.values()

    def volume_create(self, pool_id, vol_name, size_bytes, thinp, flags=0):
        size_bytes = SimData._block_rounding(size_bytes)
        # check free size
        free_space = self._pool_free_space(pool_id)
        if (free_space < size_bytes):
            raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                           "Insufficient space in pool")
        sim_vol = dict()
        vol_id = "VOL_ID_%s" % SimData._random_vpd(4)
        sim_vol['vol_id'] = vol_id
        sim_vol['vpd83'] = SimData._random_vpd()
        sim_vol['name'] = vol_name
        sim_vol['total_space'] = size_bytes
        sim_vol['thinp'] = thinp
        sim_vol['sys_id'] = SimData.SIM_DATA_SYS_ID
        sim_vol['pool_id'] = pool_id
        sim_vol['consume_size'] = size_bytes
        self.vol_dict[vol_id] = sim_vol
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
            free_space = self._pool_free_space(pool_id)
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
        free_space = self._pool_free_space(dst_pool_id)
        if (free_space < size_bytes):
            raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                           "Insufficient space in pool")
        sim_vol = dict()
        vol_id = "VOL_ID_%s" % SimData._random_vpd(4)
        sim_vol['vol_id'] = vol_id
        sim_vol['vpd83'] = SimData._random_vpd()
        sim_vol['name'] = new_vol_name
        sim_vol['total_space'] = size_bytes
        sim_vol['sys_id'] = SimData.SIM_DATA_SYS_ID
        sim_vol['pool_id'] = dst_pool_id
        sim_vol['consume_size'] = size_bytes
        self.vol_dict[vol_id] = sim_vol

        dst_vol_id = vol_id
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

    def access_group_create(self, name, init_id, init_type, sys_id, flags=0):
        sim_ag = dict()
        if init_id not in self.init_dict.keys():
            sim_init = dict()
            sim_init['init_id'] = init_id
            sim_init['init_type'] = init_type
            sim_init['name'] = SimData.SIM_DATA_INIT_NAME
            sim_init['sys_id'] = SimData.SIM_DATA_SYS_ID
            self.init_dict[init_id] = sim_init

        sim_ag['init_ids'] = [init_id]
        sim_ag['sys_id'] = SimData.SIM_DATA_SYS_ID
        sim_ag['name'] = name
        sim_ag['ag_id'] = "AG_ID_%s" % SimData._random_vpd(4)
        self.ag_dict[sim_ag['ag_id']] = sim_ag
        return sim_ag

    def access_group_del(self, ag_id, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found")
        del(self.ag_dict[ag_id])
        return None

    def access_group_add_initiator(self, ag_id, init_id, init_type, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found")
        if init_id not in self.init_dict.keys():
            sim_init = dict()
            sim_init['init_id'] = init_id
            sim_init['init_type'] = init_type
            sim_init['name'] = SimData.SIM_DATA_INIT_NAME
            sim_init['sys_id'] = SimData.SIM_DATA_SYS_ID
            self.init_dict[init_id] = sim_init
        if init_id in self.ag_dict[ag_id]['init_ids']:
            return self.ag_dict[ag_id]

        self.ag_dict[ag_id]['init_ids'].extend([init_id])

        return None

    def access_group_del_initiator(self, ag_id, init_id, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found: %s" % ag_id)
        if init_id not in self.init_dict.keys():
            return None

        if init_id in self.ag_dict[ag_id]['init_ids']:
            new_init_ids = []
            for cur_init_id in self.ag_dict[ag_id]['init_ids']:
                if cur_init_id != init_id:
                    new_init_ids.extend([cur_init_id])
            del(self.ag_dict[ag_id]['init_ids'])
            self.ag_dict[ag_id]['init_ids'] = new_init_ids
        return None

    def access_group_grant(self, ag_id, vol_id, access, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found: %s" % ag_id)
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % vol_id)
        if 'mask' not in self.vol_dict[vol_id].keys():
            self.vol_dict[vol_id]['mask'] = dict()

        self.vol_dict[vol_id]['mask'][ag_id] = access
        return None

    def access_group_revoke(self, ag_id, vol_id, flags=0):
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
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found: %s" % ag_id)
        rc = []
        for sim_vol in self.vol_dict.values():
            if 'mask' not in sim_vol:
                continue
            if ag_id in sim_vol['mask'].keys():
                rc.extend([sim_vol])
        return rc

    def access_groups_granted_to_volume(self, vol_id, flags=0):
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % vol_id)
        sim_ags = []
        if 'mask' in self.vol_dict[vol_id].keys():
            ag_ids = self.vol_dict[vol_id]['mask'].keys()
            for ag_id in ag_ids:
                sim_ags.extend([self.ag_dict[ag_id]])
        return sim_ags

    def inits(self, flags=0):
        return self.init_dict.values()

    def initiator_grant(self, init_id, init_type, vol_id, access, flags):
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % vol_id)
        if init_id not in self.init_dict.keys():
            sim_init = dict()
            sim_init['init_id'] = init_id
            sim_init['init_type'] = init_type
            sim_init['name'] = SimData.SIM_DATA_INIT_NAME
            sim_init['sys_id'] = SimData.SIM_DATA_SYS_ID
            self.init_dict[init_id] = sim_init
        if 'mask_init' not in self.vol_dict[vol_id].keys():
            self.vol_dict[vol_id]['mask_init'] = dict()

        self.vol_dict[vol_id]['mask_init'][init_id] = access
        return None

    def initiator_revoke(self, init_id, vol_id, flags=0):
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % vol_id)
        if init_id not in self.init_dict.keys():
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such Initiator: %s" % init_id)

        if 'mask_init' in self.vol_dict[vol_id].keys():
            if init_id in self.vol_dict[vol_id]['mask_init'].keys():
                del self.vol_dict[vol_id]['mask_init'][init_id]

        return None

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

    def volumes_accessible_by_initiator(self, init_id, flags=0):
        if init_id not in self.init_dict.keys():
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such Initiator: %s" % init_id)
        rc_dedup_dict = dict()
        ag_ids = self._ag_ids_of_init(init_id)
        for ag_id in ag_ids:
            sim_vols = self.volumes_accessible_by_access_group(ag_id)
            for sim_vol in sim_vols:
                rc_dedup_dict[sim_vol['vol_id']] = sim_vol

        for sim_vol in self.vol_dict.values():
            if 'mask_init' in sim_vol:
                if init_id in sim_vol['mask_init'].keys():
                    rc_dedup_dict[sim_vol['vol_id']] = sim_vol
        return rc_dedup_dict.values()

    def initiators_granted_to_volume(self, vol_id, flags=0):
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.INVALID_VOLUME,
                           "No such Volume: %s" % vol_id)
        rc_dedup_dict = dict()
        sim_ags = self.access_groups_granted_to_volume(vol_id, flags)
        for sim_ag in sim_ags:
            for init_id in sim_ag['init_ids']:
                rc_dedup_dict[init_id] = self.init_dict[init_id]

        if 'mask_init' in self.vol_dict[vol_id].keys():
            for init_id in self.vol_dict[vol_id]['mask_init']:
                rc_dedup_dict[init_id] = self.init_dict[init_id]

        return rc_dedup_dict.values()

    def iscsi_chap_auth(self, init_id, in_user, in_pass, out_user, out_pass,
                        flags=0):
        if init_id not in self.init_dict.keys():
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such Initiator: %s" % init_id)
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
        free_space = self._pool_free_space(pool_id)
        if (free_space < size_bytes):
            raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                           "Insufficient space in pool")
        sim_fs = dict()
        fs_id = "FS_ID_%s" % SimData._random_vpd(4)
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
            free_space = self._pool_free_space(pool_id)
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
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such File System: %s" % src_fs_id)
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

    def file_clone(self, fs_id, src_fs_name, dst_fs_name, snap_id, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such File System: %s" % fs_id)
        if snap_id and snap_id not in self.snap_dict.keys():
            raise LsmError(ErrorNumber.INVALID_SS,
                           "No such Snapshot: %s" % snap_id)
        # TODO: No file clone query API yet, no need to do anything internally
        return None

    def fs_snapshots(self, fs_id, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such File System: %s" % fs_id)
        rc = []
        if 'snaps' in self.fs_dict[fs_id].keys():
            for snap_id in self.fs_dict[fs_id]['snaps']:
                rc.extend([self.snap_dict[snap_id]])
        return rc

    def fs_snapshot_create(self, fs_id, snap_name, files, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such File System: %s" % fs_id)
        if 'snaps' not in self.fs_dict[fs_id].keys():
            self.fs_dict[fs_id]['snaps'] = []

        snap_id = "SNAP_ID_%s" % SimData._random_vpd(4)
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
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such File System: %s" % fs_id)
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

    def fs_snapshot_revert(self, fs_id, snap_id, files, restore_files,
                           flag_all_files, flags):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such File System: %s" % fs_id)
        if snap_id not in self.snap_dict.keys():
            raise LsmError(ErrorNumber.INVALID_SS,
                           "No such Snapshot: %s" % snap_id)
        # Nothing need to done internally for revert.
        return None

    def fs_child_dependency(self, fs_id, files, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such File System: %s" % fs_id)
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
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such File System: %s" % fs_id)
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
            raise LsmError(ErrorNumber.INVALID_INIT,
                           "No such File System: %s" % fs_id)
        sim_exp = dict()
        sim_exp['exp_id'] = "EXP_ID_%s" % SimData._random_vpd(4)
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

    def pool_create(self,
                    system_id,
                    pool_name='',
                    raid_type=Pool.RAID_TYPE_UNKNOWN,
                    member_type=Pool.MEMBER_TYPE_UNKNOWN,
                    member_ids=None,
                    member_count=0,
                    size_bytes=0,
                    thinp_type=Pool.THINP_TYPE_UNKNOWN,
                    flags=0):
        if pool_name == '':
            pool_name = 'POOL %s' % SimData._random_vpd(4)

        ## Coding
        return
