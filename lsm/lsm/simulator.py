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

import os
import random
import time
import pickle
import tempfile

from common import LsmError, ErrorNumber, JobStatus, md5, uri_parse
from data import Pool, Initiator, Volume, BlockRange, System, AccessGroup, \
    Snapshot, NfsExport, FileSystem, Capabilities, Disk, OptionalData
from iplugin import INfs, IStorageAreaNetwork
from version import VERSION
from simarray import SimArray, SimJob

class SimPlugin(INfs, IStorageAreaNetwork):
    """
    Simple class that implements enough to allow the framework to be exercised.
    """
    def __init__(self):
        self.uri = None
        self.password = None
        self.tmo = 0

    def startup(self, uri, password, timeout, flags=0):
        self.uri = uri
        self.password = password

        #The caller may want to start clean, so we allow the caller to specify
        #a file to store and retrieve individual state.
        qp = uri_parse(uri)
        if 'parameters' in qp and 'statefile' in qp['parameters'] \
                and qp['parameters']['statefile'] is not None:
            self.sim_array = SimArray(qp['parameters']['statefile'])
        else:
            self.sim_array = SimArray()

        return None

    def shutdown(self, flags=0):
        self.sim_array.save_state()

    def job_status(self, job_id, flags=0):
        return self.sim_array.job_status(job_id, flags)

    def job_free(self, job_id, flags=0):
        return self.sim_array.job_free(job_id, flags)

    @staticmethod
    def _sim_data_2_lsm(sim_data):
        """
        Fake converter. SimArray already do SimData to LSM data convert.
        We move data convert to SimArray to make this sample plugin looks
        clean.
        But in real world, data converting is often handled by plugin itself
        rather than array.
        """
        return sim_data

    def set_time_out(self, ms, flags=0):
        self.sim_array.set_time_out(ms, flags)
        return None

    def get_time_out(self, flags=0):
        return self.sim_array.get_time_out(flags)

    def capabilities(self, system, flags=0):
        rc = Capabilities()
        rc.enable_all()
        return rc

    def plugin_info(self, flags=0):
        return "Storage simulator", VERSION

    def systems(self, flags=0):
        sim_syss = self.sim_array.systems()
        return [SimPlugin._sim_data_2_lsm(s) for s in sim_syss]

    def pools(self, flags=0):
        sim_pools = self.sim_array.pools()
        return [SimPlugin._sim_data_2_lsm(p) for p in sim_pools]

    def volumes(self, flags=0):
        sim_vols = self.sim_array.volumes()
        return [SimPlugin._sim_data_2_lsm(v) for v in sim_vols]

    def disks(self, flags=0):
        sim_disks = self.sim_array.disks()
        return [SimPlugin._sim_data_2_lsm(d) for d in sim_disks]

    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags=0):
        sim_vol = self.sim_array.volume_create(
            pool.id, volume_name, size_bytes, provisioning, flags)
        return SimPlugin._sim_data_2_lsm(sim_vol)

    def volume_delete(self, volume, flags=0):
        return self.sim_array.volume_delete(volume.id, flags)

    def volume_resize(self, volume, new_size_bytes, flags=0):
        sim_vol = self.sim_array.volume_resize(
            volume.id, new_size_bytes, flags)
        return SimPlugin._sim_data_2_lsm(sim_vol)

    def volume_replicate(self, pool, rep_type, volume_src, name, flags=0):
        dst_pool_id = None

        if pool is not None:
            dst_pool_id = pool.id
        else:
            dst_pool_id = volume_src.pool_id
        return self.sim_array.volume_replicate(
            dst_pool_id, rep_type, volume_src.id, name, flags)

    def volume_replicate_range_block_size(self, system, flags=0):
        return self.sim_array.volume_replicate_range_block_size(
            system.id, flags)

    def volume_replicate_range(self, rep_type, volume_src, volume_dest,
                               ranges, flags=0):
        return self.sim_array.volume_replicate_range(
            rep_type, volume_src.id, volume_dest.id, ranges, flags)

    def volume_online(self, volume, flags=0):
        return self.sim_array.volume_online(volume.id, flags)

    def volume_offline(self, volume, flags=0):
        return self.sim_array.volume_online(volume.id, flags)

    def access_group_list(self, flags=0):
        sim_ags = self.sim_array.ags()
        return [SimPlugin._sim_data_2_lsm(a) for a in sim_ags]

    def access_group_create(self, name, initiator_id, id_type, system_id,
                            flags=0):
        sim_ag = self.sim_array.access_group_create(name, initiator_id,
            id_type, system_id, flags)
        return SimPlugin._sim_data_2_lsm(sim_ag)

    def access_group_del(self, group, flags=0):
        return self.sim_array.access_group_del(group.id, flags)

    def access_group_add_initiator(self, group, initiator_id, id_type,
                                   flags=0):
        sim_ag = self.sim_array.access_group_add_initiator(
            group.id, initiator_id, id_type, flags)
        return SimPlugin._sim_data_2_lsm(sim_ag)

    def access_group_del_initiator(self, group, initiator_id, flags=0):
        return self.sim_array.access_group_del_initiator(
            group.id, initiator_id, flags)

    def access_group_grant(self, group, volume, access, flags=0):
        return self.sim_array.access_group_grant(
            group.id, volume.id, access, flags)

    def access_group_revoke(self, group, volume, flags=0):
        return self.sim_array.access_group_revoke(
            group.id, volume.id, flags)

    def volumes_accessible_by_access_group(self, group, flags=0):
        sim_vols = self.sim_array.volumes_accessible_by_access_group(
            group.id, flags)
        return [SimPlugin._sim_data_2_lsm(v) for v in sim_vols]

    def access_groups_granted_to_volume(self, volume, flags=0):
        sim_vols = self.sim_array.access_groups_granted_to_volume(
            volume.id, flags)
        return [SimPlugin._sim_data_2_lsm(v) for v in sim_vols]

    def initiators(self, flags=0):
        return self.sim_array.inits(flags)

    def initiator_grant(self, initiator_id, initiator_type, volume, access,
                        flags=0):
        return self.sim_array.initiator_grant(
            initiator_id, initiator_type, volume.id, access, flags)

    def initiator_revoke(self, initiator, volume, flags=0):
        return self.sim_array.initiator_revoke(initiator.id, volume.id, flags)

    def volumes_accessible_by_initiator(self, initiator, flags=0):
        sim_vols = self.sim_array.volumes_accessible_by_initiator(
            initiator.id, flags)
        return [SimPlugin._sim_data_2_lsm(v) for v in sim_vols]

    def initiators_granted_to_volume(self, volume, flags=0):
        sim_inits = self.sim_array.initiators_granted_to_volume(
            volume.id, flags)
        return [SimPlugin._sim_data_2_lsm(i) for i in sim_inits]

    def iscsi_chap_auth(self, initiator, in_user, in_password,
                        out_user, out_password, flags=0):
        return self.sim_array.iscsi_chap_auth(
            initiator.id, in_user, in_password, out_user, out_password, flags)

    def volume_child_dependency(self, volume, flags=0):
        return self.sim_array.volume_child_dependency(volume.id, flags)

    def volume_child_dependency_rm(self, volume, flags=0):
        return self.sim_array.volume_child_dependency_rm(volume.id, flags)

    def fs(self, flags=0):
        sim_fss = self.sim_array.fs()
        return [SimPlugin._sim_data_2_lsm(f) for f in sim_fss]

    def fs_create(self, pool, name, size_bytes, flags=0):
        sim_fs = self.sim_array.fs_create(pool.id, name, size_bytes)
        return SimPlugin._sim_data_2_lsm(sim_fs)

    def fs_delete(self, fs, flags=0):
        return self.sim_array.fs_delete(fs.id, flags)

    def fs_resize(self, fs, new_size_bytes, flags=0):
        sim_fs = self.sim_array.fs_resize(
            fs.id, new_size_bytes, flags)
        return SimPlugin._sim_data_2_lsm(sim_fs)

    def fs_clone(self, src_fs, dest_fs_name, snapshot=None, flags=0):
        if snapshot is None:
            return self.sim_array.fs_clone(
                src_fs.id, dest_fs_name, None, flags)
        return self.sim_array.fs_clone(
            src_fs.id, dest_fs_name, snapshot.id, flags)

    def file_clone(self, fs, src_file_name, dest_file_name, snapshot=None,
                   flags=0):
        if snapshot is None:
            return self.sim_array.file_clone(
                fs.id, src_file_name, dest_file_name, None, flags)

        return self.sim_array.file_clone(
            fs.id, src_file_name, dest_file_name, snapshot.id, flags)

    def fs_snapshots(self, fs, flags=0):
        sim_snaps = self.sim_array.fs_snapshots(fs.id, flags)
        return [SimPlugin._sim_data_2_lsm(s) for s in sim_snaps]

    def fs_snapshot_create(self, fs, snapshot_name, files, flags=0):
        return self.sim_array.fs_snapshot_create(
            fs.id, snapshot_name, files, flags)

    def fs_snapshot_delete(self, fs, snapshot, flags=0):
        return self.sim_array.fs_snapshot_delete(
            fs.id, snapshot.id, flags)

    def fs_snapshot_revert(self, fs, snapshot, files, restore_files,
                           all_files=False, flags=0):
        return self.sim_array.fs_snapshot_revert(
            fs.id, snapshot.id, files, restore_files, all_files, flags)

    def fs_child_dependency(self, fs, files, flags=0):
        return self.sim_array.fs_child_dependency(fs.id, files, flags)

    def fs_child_dependency_rm(self, fs, files, flags=0):
        return self.sim_array.fs_child_dependency_rm(fs.id, files, flags)

    def export_auth(self, flags=0):
        # The API should change some day
        return ["simple"]

    def exports(self, flags=0):
        sim_exps = self.sim_array.exports(flags)
        return [SimPlugin._sim_data_2_lsm(e) for e in sim_exps]

    def export_fs(self, fs_id, export_path, root_list, rw_list, ro_list,
                  anon_uid, anon_gid, auth_type, options, flags=0):
        sim_exp = self.sim_array.fs_export(
            fs_id, export_path, root_list, rw_list, ro_list,
            anon_uid, anon_gid, auth_type, options, flags=0)
        return SimPlugin._sim_data_2_lsm(sim_exp)

    def export_remove(self, export, flags=0):
        return self.sim_array.fs_unexport(export.id, flags)

