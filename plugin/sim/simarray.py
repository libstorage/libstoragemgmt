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
import fcntl

from lsm import (size_human_2_size_bytes, size_bytes_2_size_human)
from lsm import (System, Volume, Disk, Pool, FileSystem, AccessGroup,
                 FsSnapshot, NfsExport, md5, LsmError, TargetPort,
                 ErrorNumber, JobStatus)

# Used for format width for disks
D_FMT = 5


class PoolRAID(object):
    RAID_TYPE_RAID0 = 0
    RAID_TYPE_RAID1 = 1
    RAID_TYPE_RAID3 = 3
    RAID_TYPE_RAID4 = 4
    RAID_TYPE_RAID5 = 5
    RAID_TYPE_RAID6 = 6
    RAID_TYPE_RAID10 = 10
    RAID_TYPE_RAID15 = 15
    RAID_TYPE_RAID16 = 16
    RAID_TYPE_RAID50 = 50
    RAID_TYPE_RAID60 = 60
    RAID_TYPE_RAID51 = 51
    RAID_TYPE_RAID61 = 61
    # number 2x is reserved for non-numbered RAID.
    RAID_TYPE_JBOD = 20
    RAID_TYPE_UNKNOWN = 21
    RAID_TYPE_NOT_APPLICABLE = 22
    # NOT_APPLICABLE indicate current pool only has one member.
    RAID_TYPE_MIXED = 23

    MEMBER_TYPE_UNKNOWN = 0
    MEMBER_TYPE_DISK = 1
    MEMBER_TYPE_DISK_MIX = 10
    MEMBER_TYPE_DISK_ATA = 11
    MEMBER_TYPE_DISK_SATA = 12
    MEMBER_TYPE_DISK_SAS = 13
    MEMBER_TYPE_DISK_FC = 14
    MEMBER_TYPE_DISK_SOP = 15
    MEMBER_TYPE_DISK_SCSI = 16
    MEMBER_TYPE_DISK_NL_SAS = 17
    MEMBER_TYPE_DISK_HDD = 18
    MEMBER_TYPE_DISK_SSD = 19
    MEMBER_TYPE_DISK_HYBRID = 110
    MEMBER_TYPE_DISK_LUN = 111

    MEMBER_TYPE_POOL = 2

    _MEMBER_TYPE_2_DISK_TYPE = {
        MEMBER_TYPE_DISK: Disk.TYPE_UNKNOWN,
        MEMBER_TYPE_DISK_MIX: Disk.TYPE_UNKNOWN,
        MEMBER_TYPE_DISK_ATA: Disk.TYPE_ATA,
        MEMBER_TYPE_DISK_SATA: Disk.TYPE_SATA,
        MEMBER_TYPE_DISK_SAS: Disk.TYPE_SAS,
        MEMBER_TYPE_DISK_FC: Disk.TYPE_FC,
        MEMBER_TYPE_DISK_SOP: Disk.TYPE_SOP,
        MEMBER_TYPE_DISK_SCSI: Disk.TYPE_SCSI,
        MEMBER_TYPE_DISK_NL_SAS: Disk.TYPE_NL_SAS,
        MEMBER_TYPE_DISK_HDD: Disk.TYPE_HDD,
        MEMBER_TYPE_DISK_SSD: Disk.TYPE_SSD,
        MEMBER_TYPE_DISK_HYBRID: Disk.TYPE_HYBRID,
        MEMBER_TYPE_DISK_LUN: Disk.TYPE_LUN,
    }

    @staticmethod
    def member_type_is_disk(member_type):
        """
        Returns True if defined 'member_type' is disk.
        False when else.
        """
        return member_type in PoolRAID._MEMBER_TYPE_2_DISK_TYPE


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

        self.state_fd = os.open(self.dump_file, os.O_RDWR | os.O_CREAT)
        fcntl.lockf(self.state_fd, fcntl.LOCK_EX)
        self.state_fo = os.fdopen(self.state_fd, "r+b")

        current = self.state_fo.read()

        if current:
            try:
                self.data = pickle.loads(current)

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
        # Make sure we are at the beginning of the stream
        self.state_fo.seek(0)
        pickle.dump(self.data, self.state_fo)
        self.state_fo.flush()
        self.state_fo.close()
        self.state_fo = None

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
                      sim_vol['admin_state'], sim_vol['sys_id'],
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
        unsupported_actions = sim_pool['unsupported_actions']
        return Pool(pool_id, name,
                    Pool.ELEMENT_TYPE_VOLUME | Pool.ELEMENT_TYPE_FS,
                    unsupported_actions, total_space, free_space, status,
                    status_info, sys_id)

    def pools(self, flags=0):
        rc = []
        sim_pools = self.data.pools()
        for sim_pool in sim_pools:
            rc.extend([self._sim_pool_2_lsm(sim_pool, flags)])
        return SimArray._sort_by_id(rc)

    def disks(self):
        rc = []
        sim_disks = self.data.disks()
        for sim_disk in sim_disks:
            disk = Disk(sim_disk['disk_id'], sim_disk['name'],
                        sim_disk['disk_type'], SimData.SIM_DATA_BLK_SIZE,
                        int(sim_disk['total_space'] /
                            SimData.SIM_DATA_BLK_SIZE), Disk.STATUS_OK,
                        sim_disk['sys_id'])
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

    def volume_enable(self, vol_id, flags=0):
        return self.data.volume_enable(vol_id, flags)

    def volume_disable(self, vol_id, flags=0):
        return self.data.volume_disable(vol_id, flags)

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

    def fs_snapshot_create(self, fs_id, snap_name, flags=0):
        sim_snap = self.data.fs_snapshot_create(fs_id, snap_name, flags)
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
        return NfsExport(sim_exp['exp_id'], sim_exp['fs_id'],
                         sim_exp['exp_path'], sim_exp['auth_type'],
                         sim_exp['root_hosts'], sim_exp['rw_hosts'],
                         sim_exp['ro_hosts'], sim_exp['anon_uid'],
                         sim_exp['anon_gid'], sim_exp['options'])

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
        return AccessGroup(sim_ag['ag_id'], sim_ag['name'], sim_ag['init_ids'],
                           sim_ag['init_type'], sim_ag['sys_id'])

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
        sim_ag = self.data.access_group_initiator_add(
            ag_id, init_id, init_type, flags)
        return SimArray._sim_ag_2_lsm(sim_ag)

    def access_group_initiator_delete(self, ag_id, init_id, init_type,
                                      flags=0):
        sim_ag = self.data.access_group_initiator_delete(ag_id, init_id,
                                                         init_type, flags)
        return SimArray._sim_ag_2_lsm(sim_ag)

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

    @staticmethod
    def _sim_tgt_2_lsm(sim_tgt):
        return TargetPort(
            sim_tgt['tgt_id'], sim_tgt['port_type'],
            sim_tgt['service_address'], sim_tgt['network_address'],
            sim_tgt['physical_address'], sim_tgt['physical_name'],
            sim_tgt['sys_id'])

    def target_ports(self):
        sim_tgts = self.data.target_ports()
        return [SimArray._sim_tgt_2_lsm(t) for t in sim_tgts]


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
            'admin_state': Volume.ADMIN_STATE_ENABLED,
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
            'raid_type': PoolRAID.RAID_TYPE_XXXX,
            'member_ids': [ disk_id or pool_id or volume_id ],
            'member_type': PoolRAID.MEMBER_TYPE_XXXX,
            'member_size': size_bytes  # space allocated from each member pool.
                                       # only for MEMBER_TYPE_POOL
            'status': SIM_DATA_POOL_STATUS,
            'sys_id': SimData.SIM_DATA_SYS_ID,
            'element_type': SimData.SIM_DATA_POOL_ELEMENT_TYPE,
        }
    """
    SIM_DATA_BLK_SIZE = 512
    SIM_DATA_VERSION = "2.9"
    SIM_DATA_SYS_ID = 'sim-01'
    SIM_DATA_INIT_NAME = 'NULL'
    SIM_DATA_TMO = 30000    # ms
    SIM_DATA_POOL_STATUS = Pool.STATUS_OK
    SIM_DATA_POOL_STATUS_INFO = ''
    SIM_DATA_POOL_ELEMENT_TYPE = Pool.ELEMENT_TYPE_FS \
        | Pool.ELEMENT_TYPE_POOL \
        | Pool.ELEMENT_TYPE_VOLUME

    SIM_DATA_POOL_UNSUPPORTED_ACTIONS = 0

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
        self.syss = [
            System(SimData.SIM_DATA_SYS_ID, 'LSM simulated storage plug-in',
                   System.STATUS_OK, '')]
        pool_size_200g = size_human_2_size_bytes('200GiB')
        self.pool_dict = {
            'POO1': dict(
                pool_id='POO1', name='Pool 1',
                member_type=PoolRAID.MEMBER_TYPE_DISK_SATA,
                member_ids=[SimData._disk_id(0), SimData._disk_id(1)],
                raid_type=PoolRAID.RAID_TYPE_RAID1,
                status=SimData.SIM_DATA_POOL_STATUS,
                status_info=SimData.SIM_DATA_POOL_STATUS_INFO,
                sys_id=SimData.SIM_DATA_SYS_ID,
                element_type=SimData.SIM_DATA_SYS_POOL_ELEMENT_TYPE,
                unsupported_actions=Pool.UNSUPPORTED_VOLUME_GROW |
                                 Pool.UNSUPPORTED_VOLUME_SHRINK),
            'POO2': dict(
                pool_id='POO2', name='Pool 2',
                member_type=PoolRAID.MEMBER_TYPE_POOL,
                member_ids=['POO1'], member_size=pool_size_200g,
                raid_type=PoolRAID.RAID_TYPE_NOT_APPLICABLE,
                status=Pool.STATUS_OK,
                status_info=SimData.SIM_DATA_POOL_STATUS_INFO,
                sys_id=SimData.SIM_DATA_SYS_ID,
                element_type=SimData.SIM_DATA_POOL_ELEMENT_TYPE,
                unsupported_actions=SimData.SIM_DATA_POOL_UNSUPPORTED_ACTIONS),
            # lsm_test_aggr pool is required by test/runtest.sh
            'lsm_test_aggr': dict(
                pool_id='lsm_test_aggr',
                name='lsm_test_aggr',
                member_type=PoolRAID.MEMBER_TYPE_DISK_SAS,
                member_ids=[SimData._disk_id(2), SimData._disk_id(3)],
                raid_type=PoolRAID.RAID_TYPE_RAID0,
                status=Pool.STATUS_OK,
                status_info=SimData.SIM_DATA_POOL_STATUS_INFO,
                sys_id=SimData.SIM_DATA_SYS_ID,
                element_type=SimData.SIM_DATA_POOL_ELEMENT_TYPE,
                unsupported_actions=SimData.SIM_DATA_POOL_UNSUPPORTED_ACTIONS),
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
            d_type = Disk.TYPE_SATA

            if 2 <= i <= 8:
                d_name = "SAS  Disk %0*d" % (D_FMT, i)
                d_type = Disk.TYPE_SAS
            elif 9 <= i:
                d_name = "SSD  Disk %0*d" % (D_FMT, i)
                d_type = Disk.TYPE_SSD
                if i <= 13:
                    d_size = disk_size_512g

            self.disk_dict[d_id] = dict(disk_id=d_id, name=d_name,
                                        total_space=d_size,
                                        disk_type=d_type,
                                        sys_id=SimData.SIM_DATA_SYS_ID)

        self.ag_dict = {
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
            'member_type': PoolRAID.MEMBER_TYPE_DISK_SSD,
            'member_ids': [
                self.disk_dict[SimData._disk_id(9)]['disk_id'],
                self.disk_dict[SimData._disk_id(10)]['disk_id'],
            ],
            'raid_type': PoolRAID.RAID_TYPE_RAID1,
            'status': Pool.STATUS_OK,
            'status_info': SimData.SIM_DATA_POOL_STATUS_INFO,
            'sys_id': SimData.SIM_DATA_SYS_ID,
            'element_type': SimData.SIM_DATA_POOL_ELEMENT_TYPE,
            'unsupported_actions': SimData.SIM_DATA_POOL_UNSUPPORTED_ACTIONS
        }

        self.tgt_dict = {
            'TGT_PORT_ID_01': {
                'tgt_id': 'TGT_PORT_ID_01',
                'port_type': TargetPort.TYPE_FC,
                'service_address': '50:0a:09:86:99:4b:8d:c5',
                'network_address': '50:0a:09:86:99:4b:8d:c5',
                'physical_address': '50:0a:09:86:99:4b:8d:c5',
                'physical_name': 'FC_a_0b',
                'sys_id': SimData.SIM_DATA_SYS_ID,
            },
            'TGT_PORT_ID_02': {
                'tgt_id': 'TGT_PORT_ID_02',
                'port_type': TargetPort.TYPE_FCOE,
                'service_address': '50:0a:09:86:99:4b:8d:c6',
                'network_address': '50:0a:09:86:99:4b:8d:c6',
                'physical_address': '50:0a:09:86:99:4b:8d:c6',
                'physical_name': 'FCoE_b_0c',
                'sys_id': SimData.SIM_DATA_SYS_ID,
            },
            'TGT_PORT_ID_03': {
                'tgt_id': 'TGT_PORT_ID_03',
                'port_type': TargetPort.TYPE_ISCSI,
                'service_address': 'iqn.1986-05.com.example:sim-tgt-03',
                'network_address': 'sim-iscsi-tgt-3.example.com:3260',
                'physical_address': 'a4:4e:31:47:f4:e0',
                'physical_name': 'iSCSI_c_0d',
                'sys_id': SimData.SIM_DATA_SYS_ID,
            },
            'TGT_PORT_ID_04': {
                'tgt_id': 'TGT_PORT_ID_04',
                'port_type': TargetPort.TYPE_ISCSI,
                'service_address': 'iqn.1986-05.com.example:sim-tgt-03',
                'network_address': '10.0.0.1:3260',
                'physical_address': 'a4:4e:31:47:f4:e1',
                'physical_name': 'iSCSI_c_0e',
                'sys_id': SimData.SIM_DATA_SYS_ID,
            },
            'TGT_PORT_ID_05': {
                'tgt_id': 'TGT_PORT_ID_05',
                'port_type': TargetPort.TYPE_ISCSI,
                'service_address': 'iqn.1986-05.com.example:sim-tgt-03',
                'network_address': '[2001:470:1f09:efe:a64e:31ff::1]:3260',
                'physical_address': 'a4:4e:31:47:f4:e1',
                'physical_name': 'iSCSI_c_0e',
                'sys_id': SimData.SIM_DATA_SYS_ID,
            },
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
            if sim_pool['member_type'] != PoolRAID.MEMBER_TYPE_POOL:
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
            vpd.append(str('%02x' % (random.randint(0, 255))))
        return "".join(vpd)

    def _size_of_raid(self, member_type, member_ids, raid_type,
                      pool_each_size=0):
        member_sizes = []
        if PoolRAID.member_type_is_disk(member_type):
            for member_id in member_ids:
                member_sizes.extend([self.disk_dict[member_id]['total_space']])

        elif member_type == PoolRAID.MEMBER_TYPE_POOL:
            for member_id in member_ids:
                member_sizes.extend([pool_each_size])

        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Got unsupported member_type in _size_of_raid()" +
                           ": %d" % member_type)
        all_size = 0
        member_size = 0
        member_count = len(member_ids)
        for member_size in member_sizes:
            all_size += member_size

        if raid_type == PoolRAID.RAID_TYPE_JBOD or \
           raid_type == PoolRAID.RAID_TYPE_NOT_APPLICABLE or \
           raid_type == PoolRAID.RAID_TYPE_RAID0:
            return int(all_size)
        elif (raid_type == PoolRAID.RAID_TYPE_RAID1 or
              raid_type == PoolRAID.RAID_TYPE_RAID10):
            if member_count % 2 == 1:
                return 0
            return int(all_size / 2)
        elif (raid_type == PoolRAID.RAID_TYPE_RAID3 or
              raid_type == PoolRAID.RAID_TYPE_RAID4 or
              raid_type == PoolRAID.RAID_TYPE_RAID5):
            if member_count < 3:
                return 0
            return int(all_size - member_size)
        elif raid_type == PoolRAID.RAID_TYPE_RAID50:
            if member_count < 6 or member_count % 2 == 1:
                return 0
            return int(all_size - member_size * 2)
        elif raid_type == PoolRAID.RAID_TYPE_RAID6:
            if member_count < 4:
                return 0
            return int(all_size - member_size * 2)
        elif raid_type == PoolRAID.RAID_TYPE_RAID60:
            if member_count < 8 or member_count % 2 == 1:
                return 0
            return int(all_size - member_size * 4)
        elif raid_type == PoolRAID.RAID_TYPE_RAID51:
            if member_count < 6 or member_count % 2 == 1:
                return 0
            return int(all_size / 2 - member_size)
        elif raid_type == PoolRAID.RAID_TYPE_RAID61:
            if member_count < 8 or member_count % 2 == 1:
                return 0
            print "%s" % size_bytes_2_size_human(all_size)
            print "%s" % size_bytes_2_size_human(member_size)
            return int(all_size / 2 - member_size * 2)
        raise LsmError(ErrorNumber.PLUGIN_BUG,
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
        if sim_pool['member_type'] == PoolRAID.MEMBER_TYPE_POOL:
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
            self.vol_dict.values(), vol_name, ErrorNumber.NAME_CONFLICT)
        size_bytes = SimData._block_rounding(size_bytes)
        # check free size
        free_space = self.pool_free_space(pool_id)
        if (free_space < size_bytes):
            raise LsmError(ErrorNumber.NOT_ENOUGH_SPACE,
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
        sim_vol['admin_state'] = Volume.ADMIN_STATE_ENABLED
        self.vol_dict[sim_vol['vol_id']] = sim_vol
        return sim_vol

    def volume_delete(self, vol_id, flags=0):
        if vol_id in self.vol_dict.keys():
            if 'mask' in self.vol_dict[vol_id].keys() and \
               self.vol_dict[vol_id]['mask']:
                raise LsmError(ErrorNumber.IS_MASKED,
                               "Volume is masked to access group")
            del(self.vol_dict[vol_id])
            return None
        raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                       "No such volume: %s" % vol_id)

    def volume_resize(self, vol_id, new_size_bytes, flags=0):
        new_size_bytes = SimData._block_rounding(new_size_bytes)
        if vol_id in self.vol_dict.keys():
            pool_id = self.vol_dict[vol_id]['pool_id']
            free_space = self.pool_free_space(pool_id)
            if (free_space < new_size_bytes):
                raise LsmError(ErrorNumber.NOT_ENOUGH_SPACE,
                               "Insufficient space in pool")

            if self.vol_dict[vol_id]['total_space'] == new_size_bytes:
                raise LsmError(ErrorNumber.NO_STATE_CHANGE, "New size same "
                                                            "as current")

            self.vol_dict[vol_id]['total_space'] = new_size_bytes
            self.vol_dict[vol_id]['consume_size'] = new_size_bytes
            return self.vol_dict[vol_id]
        raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                       "No such volume: %s" % vol_id)

    def volume_replicate(self, dst_pool_id, rep_type, src_vol_id, new_vol_name,
                         flags=0):
        if src_vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                           "No such volume: %s" % src_vol_id)

        self._check_dup_name(self.vol_dict.values(), new_vol_name,
                             ErrorNumber.NAME_CONFLICT)

        size_bytes = self.vol_dict[src_vol_id]['total_space']
        size_bytes = SimData._block_rounding(size_bytes)
        # check free size
        free_space = self.pool_free_space(dst_pool_id)
        if (free_space < size_bytes):
            raise LsmError(ErrorNumber.NOT_ENOUGH_SPACE,
                           "Insufficient space in pool")
        sim_vol = dict()
        sim_vol['vol_id'] = self._next_vol_id()
        sim_vol['vpd83'] = SimData._random_vpd()
        sim_vol['name'] = new_vol_name
        sim_vol['total_space'] = size_bytes
        sim_vol['sys_id'] = SimData.SIM_DATA_SYS_ID
        sim_vol['pool_id'] = dst_pool_id
        sim_vol['consume_size'] = size_bytes
        sim_vol['admin_state'] = Volume.ADMIN_STATE_ENABLED
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
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                           "No such Volume: %s" % src_vol_id)

        if dst_vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
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

    def volume_enable(self, vol_id, flags=0):
        try:
            if self.vol_dict[vol_id]['admin_state'] == \
               Volume.ADMIN_STATE_ENABLED:
                raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                               "Volume is already enabled")
        except KeyError:
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                           "No such Volume: %s" % vol_id)

        self.vol_dict[vol_id]['admin_state'] = Volume.ADMIN_STATE_ENABLED
        return None

    def volume_disable(self, vol_id, flags=0):
        try:
            if self.vol_dict[vol_id]['admin_state'] == \
               Volume.ADMIN_STATE_DISABLED:
                raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                               "Volume is already disabled")
        except KeyError:
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                           "No such Volume: %s" % vol_id)

        self.vol_dict[vol_id]['admin_state'] = Volume.ADMIN_STATE_DISABLED
        return None

    def volume_child_dependency(self, vol_id, flags=0):
        """
        If volume is a src or dst of a replication, we return True.
        """
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
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
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
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

    def _sim_ag_of_init(self, init_id):
        """
        Return sim_ag which containing this init_id.
        If not found, return None
        """
        for sim_ag in self.ag_dict.values():
            if init_id in sim_ag['init_ids']:
                return sim_ag
        return None

    def _sim_ag_of_name(self, ag_name):
        for sim_ag in self.ag_dict.values():
            if ag_name == sim_ag['name']:
                return sim_ag
        return None

    def _check_dup_name(self, sim_list, name, error_num):
        used_names = [x['name'] for x in sim_list]
        if name in used_names:
            raise LsmError(error_num, "Name '%s' already in use" % name)

    def access_group_create(self, name, init_id, init_type, sys_id, flags=0):

        # Check to see if we have an access group with this name
        self._check_dup_name(self.ag_dict.values(), name,
                             ErrorNumber.NAME_CONFLICT)

        exist_sim_ag = self._sim_ag_of_init(init_id)
        if exist_sim_ag:
            if exist_sim_ag['name'] == name:
                return exist_sim_ag
            else:
                raise LsmError(ErrorNumber.EXISTS_INITIATOR,
                               "Initiator %s already exist in other " %
                               init_id + "access group %s(%s)" %
                               (exist_sim_ag['name'], exist_sim_ag['ag_id']))

        exist_sim_ag = self._sim_ag_of_name(name)
        if exist_sim_ag:
            if init_id in exist_sim_ag['init_ids']:
                return exist_sim_ag
            else:
                raise LsmError(ErrorNumber.NAME_CONFLICT,
                               "Another access group %s(%s) is using " %
                               (exist_sim_ag['name'], exist_sim_ag['ag_id']) +
                               "requested name %s but not contain init_id %s" %
                               (exist_sim_ag['name'], init_id))

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
        # Check whether any volume masked to.
        for sim_vol in self.vol_dict.values():
            if 'mask' in sim_vol.keys() and ag_id in sim_vol['mask']:
                raise LsmError(ErrorNumber.IS_MASKED,
                               "Access group is masked to volume")
        del(self.ag_dict[ag_id])
        return None

    def access_group_initiator_add(self, ag_id, init_id, init_type, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found")
        if init_id in self.ag_dict[ag_id]['init_ids']:
            raise LsmError(ErrorNumber.NO_STATE_CHANGE, "Initiator already "
                                                        "in access group")

        self._sim_ag_of_init(init_id)

        self.ag_dict[ag_id]['init_ids'].extend([init_id])
        return self.ag_dict[ag_id]

    def access_group_initiator_delete(self, ag_id, init_id, init_type,
                                      flags=0):
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
        else:
            raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                           "Initiator %s type %s not in access group %s"
                           % (init_id, str(type(init_id)),
                              str(self.ag_dict[ag_id]['init_ids'])))
        return self.ag_dict[ag_id]

    def volume_mask(self, ag_id, vol_id, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found: %s" % ag_id)
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                           "No such Volume: %s" % vol_id)
        if 'mask' not in self.vol_dict[vol_id].keys():
            self.vol_dict[vol_id]['mask'] = dict()

        if ag_id in self.vol_dict[vol_id]['mask']:
            raise LsmError(ErrorNumber.NO_STATE_CHANGE, "Volume already "
                                                        "masked to access "
                                                        "group")

        self.vol_dict[vol_id]['mask'][ag_id] = 2
        return None

    def volume_unmask(self, ag_id, vol_id, flags=0):
        if ag_id not in self.ag_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "Access group not found: %s" % ag_id)
        if vol_id not in self.vol_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                           "No such Volume: %s" % vol_id)
        if 'mask' not in self.vol_dict[vol_id].keys():
            raise LsmError(ErrorNumber.NO_STATE_CHANGE, "Volume not "
                                                        "masked to access "
                                                        "group")

        if ag_id not in self.vol_dict[vol_id]['mask'].keys():
            raise LsmError(ErrorNumber.NO_STATE_CHANGE, "Volume not "
                                                        "masked to access "
                                                        "group")

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
        # No iscsi chap query API yet, not need to setup anything
        return None

    def fs(self):
        return self.fs_dict.values()

    def fs_create(self, pool_id, fs_name, size_bytes, flags=0):

        self._check_dup_name(self.fs_dict.values(), fs_name,
                             ErrorNumber.NAME_CONFLICT)

        size_bytes = SimData._block_rounding(size_bytes)
        # check free size
        free_space = self.pool_free_space(pool_id)
        if (free_space < size_bytes):
            raise LsmError(ErrorNumber.NOT_ENOUGH_SPACE,
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
        raise LsmError(ErrorNumber.NOT_FOUND_FS,
                       "No such File System: %s" % fs_id)

    def fs_resize(self, fs_id, new_size_bytes, flags=0):
        new_size_bytes = SimData._block_rounding(new_size_bytes)
        if fs_id in self.fs_dict.keys():
            pool_id = self.fs_dict[fs_id]['pool_id']
            free_space = self.pool_free_space(pool_id)
            if (free_space < new_size_bytes):
                raise LsmError(ErrorNumber.NOT_ENOUGH_SPACE,
                               "Insufficient space in pool")

            if self.fs_dict[fs_id]['total_space'] == new_size_bytes:
                raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                               "New size same as current")

            self.fs_dict[fs_id]['total_space'] = new_size_bytes
            self.fs_dict[fs_id]['free_space'] = new_size_bytes
            self.fs_dict[fs_id]['consume_size'] = new_size_bytes
            return self.fs_dict[fs_id]
        raise LsmError(ErrorNumber.NOT_FOUND_FS,
                       "No such File System: %s" % fs_id)

    def fs_clone(self, src_fs_id, dst_fs_name, snap_id, flags=0):
        if src_fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % src_fs_id)
        if snap_id and snap_id not in self.snap_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS_SS,
                           "No such Snapshot: %s" % snap_id)

        src_sim_fs = self.fs_dict[src_fs_id]
        if 'clone' not in src_sim_fs.keys():
            src_sim_fs['clone'] = dict()

        # Make sure we don't have a duplicate name
        self._check_dup_name(self.fs_dict.values(), dst_fs_name,
                             ErrorNumber.NAME_CONFLICT)

        dst_sim_fs = self.fs_create(
            src_sim_fs['pool_id'], dst_fs_name, src_sim_fs['total_space'], 0)

        src_sim_fs['clone'][dst_sim_fs['fs_id']] = {
            'snap_id': snap_id,
        }
        return dst_sim_fs

    def fs_file_clone(self, fs_id, src_fs_name, dst_fs_name, snap_id, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % fs_id)
        if snap_id and snap_id not in self.snap_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS_SS,
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

    def fs_snapshot_create(self, fs_id, snap_name, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % fs_id)
        if 'snaps' not in self.fs_dict[fs_id].keys():
            self.fs_dict[fs_id]['snaps'] = []
        else:
            self._check_dup_name(self.fs_dict[fs_id]['snaps'], snap_name,
                                 ErrorNumber.NAME_CONFLICT)

        snap_id = self._next_snap_id()
        sim_snap = dict()
        sim_snap['snap_id'] = snap_id
        sim_snap['name'] = snap_name
        sim_snap['files'] = []

        sim_snap['timestamp'] = time.time()
        self.snap_dict[snap_id] = sim_snap
        self.fs_dict[fs_id]['snaps'].extend([snap_id])
        return sim_snap

    def fs_snapshot_delete(self, fs_id, snap_id, flags=0):
        if fs_id not in self.fs_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS,
                           "File System: %s not found" % fs_id)
        if snap_id not in self.snap_dict.keys():
            raise LsmError(ErrorNumber.NOT_FOUND_FS_SS,
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
            raise LsmError(ErrorNumber.NOT_FOUND_FS_SS,
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
            raise LsmError(ErrorNumber.NOT_FOUND_NFS_EXPORT,
                           "No such NFS Export: %s" % exp_id)
        del self.exp_dict[exp_id]
        return None

    def target_ports(self):
        return self.tgt_dict.values()
