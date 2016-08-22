# Copyright (C) 2011-2016 Red Hat, Inc.
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
# Author: tasleson
#         Gris Ge <fge@redhat.com>

from lsm import (uri_parse, VERSION, Capabilities, INfs,
                 IStorageAreaNetwork, search_property, Client)

from lsm.plugin.sim.simarray import SimArray


class SimPlugin(INfs, IStorageAreaNetwork):
    """
    Simple class that implements enough to allow the framework to be exercised.
    """
    def __init__(self):
        self.uri = None
        self.password = None
        self.sim_array = None

    def plugin_register(self, uri, password, timeout, flags=0):
        self.uri = uri
        self.password = password

        # The caller may want to start clean, so we allow the caller to specify
        # a file to store and retrieve individual state.
        qp = uri_parse(uri)
        if 'parameters' in qp and 'statefile' in qp['parameters'] \
                and qp['parameters']['statefile'] is not None:
            self.sim_array = SimArray(qp['parameters']['statefile'], timeout)
        else:
            self.sim_array = SimArray(None, timeout)

        return None

    def plugin_unregister(self, flags=0):
        pass

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

    def time_out_set(self, ms, flags=0):
        self.sim_array.time_out_set(ms, flags)
        return None

    def time_out_get(self, flags=0):
        return self.sim_array.time_out_get(flags)

    def capabilities(self, system, flags=0):
        rc = Capabilities()
        rc.enable_all()
        rc.set(Capabilities.POOLS_QUICK_SEARCH, Capabilities.UNSUPPORTED)
        rc.set(Capabilities.VOLUMES_QUICK_SEARCH, Capabilities.UNSUPPORTED)
        rc.set(Capabilities.DISKS_QUICK_SEARCH, Capabilities.UNSUPPORTED)
        rc.set(Capabilities.FS_QUICK_SEARCH, Capabilities.UNSUPPORTED)
        rc.set(Capabilities.ACCESS_GROUPS_QUICK_SEARCH,
               Capabilities.UNSUPPORTED)
        rc.set(Capabilities.NFS_EXPORTS_QUICK_SEARCH, Capabilities.UNSUPPORTED)
        rc.set(Capabilities.TARGET_PORTS_QUICK_SEARCH,
               Capabilities.UNSUPPORTED)
        rc.set(Capabilities.VOLUME_PHYSICAL_DISK_CACHE_UPDATE_SYSTEM_LEVEL,
               Capabilities.UNSUPPORTED)
        rc.set(Capabilities.VOLUME_WRITE_CACHE_POLICY_UPDATE_IMPACT_READ,
               Capabilities.UNSUPPORTED)
        rc.set(Capabilities.VOLUME_WRITE_CACHE_POLICY_UPDATE_WB_IMPACT_OTHER,
               Capabilities.UNSUPPORTED)
        rc.set(Capabilities.VOLUME_READ_CACHE_POLICY_UPDATE_IMPACT_WRITE,
               Capabilities.UNSUPPORTED)
        return rc

    def plugin_info(self, flags=0):
        return "Storage simulator", VERSION

    def systems(self, flags=0):
        sim_syss = self.sim_array.systems()
        return [SimPlugin._sim_data_2_lsm(s) for s in sim_syss]

    def system_read_cache_pct_update(self, system, read_pct, flags=0):
        return self.sim_array.system_read_cache_pct_update(system, read_pct)

    def pools(self, search_key=None, search_value=None, flags=0):
        sim_pools = self.sim_array.pools(flags)
        return search_property(
            [SimPlugin._sim_data_2_lsm(p) for p in sim_pools],
            search_key, search_value)

    def volumes(self, search_key=None, search_value=None, flags=0):
        sim_vols = self.sim_array.volumes()
        return search_property(
            [SimPlugin._sim_data_2_lsm(v) for v in sim_vols],
            search_key, search_value)

    def disks(self, search_key=None, search_value=None, flags=0):
        sim_disks = self.sim_array.disks()
        return search_property(
            [SimPlugin._sim_data_2_lsm(d) for d in sim_disks],
            search_key, search_value)

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

    def volume_enable(self, volume, flags=0):
        return self.sim_array.volume_enable(volume.id, flags)

    def volume_disable(self, volume, flags=0):
        return self.sim_array.volume_disable(volume.id, flags)

    def access_groups(self, search_key=None, search_value=None, flags=0):
        sim_ags = self.sim_array.ags()
        return search_property(
            [SimPlugin._sim_data_2_lsm(a) for a in sim_ags],
            search_key, search_value)

    def access_group_create(self, name, init_id, init_type, system,
                            flags=0):
        sim_ag = self.sim_array.access_group_create(
            name, init_id, init_type, system.id, flags)
        return SimPlugin._sim_data_2_lsm(sim_ag)

    def access_group_delete(self, access_group, flags=0):
        return self.sim_array.access_group_delete(access_group.id, flags)

    def access_group_initiator_add(self, access_group, init_id, init_type,
                                   flags=0):
        sim_ag = self.sim_array.access_group_initiator_add(
            access_group.id, init_id, init_type, flags)
        return SimPlugin._sim_data_2_lsm(sim_ag)

    def access_group_initiator_delete(self, access_group, init_id, init_type,
                                      flags=0):
        sim_ag = self.sim_array.access_group_initiator_delete(
            access_group.id, init_id, init_type, flags)
        return SimPlugin._sim_data_2_lsm(sim_ag)

    def volume_mask(self, access_group, volume, flags=0):
        return self.sim_array.volume_mask(
            access_group.id, volume.id, flags)

    def volume_unmask(self, access_group, volume, flags=0):
        return self.sim_array.volume_unmask(
            access_group.id, volume.id, flags)

    def volumes_accessible_by_access_group(self, access_group, flags=0):
        sim_vols = self.sim_array.volumes_accessible_by_access_group(
            access_group.id, flags)
        return [SimPlugin._sim_data_2_lsm(v) for v in sim_vols]

    def access_groups_granted_to_volume(self, volume, flags=0):
        sim_vols = self.sim_array.access_groups_granted_to_volume(
            volume.id, flags)
        return [SimPlugin._sim_data_2_lsm(v) for v in sim_vols]

    def iscsi_chap_auth(self, init_id, in_user, in_password,
                        out_user, out_password, flags=0):
        return self.sim_array.iscsi_chap_auth(
            init_id, in_user, in_password, out_user, out_password, flags)

    def volume_child_dependency(self, volume, flags=0):
        return self.sim_array.volume_child_dependency(volume.id, flags)

    def volume_child_dependency_rm(self, volume, flags=0):
        return self.sim_array.volume_child_dependency_rm(volume.id, flags)

    def fs(self, search_key=None, search_value=None, flags=0):
        sim_fss = self.sim_array.fs()
        return search_property(
            [SimPlugin._sim_data_2_lsm(f) for f in sim_fss],
            search_key, search_value)

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

    def fs_file_clone(self, fs, src_file_name, dest_file_name, snapshot=None,
                      flags=0):
        if snapshot is None:
            return self.sim_array.fs_file_clone(
                fs.id, src_file_name, dest_file_name, None, flags)

        return self.sim_array.fs_file_clone(
            fs.id, src_file_name, dest_file_name, snapshot.id, flags)

    def fs_snapshots(self, fs, flags=0):
        sim_snaps = self.sim_array.fs_snapshots(fs.id, flags)
        return [SimPlugin._sim_data_2_lsm(s) for s in sim_snaps]

    def fs_snapshot_create(self, fs, snapshot_name, flags=0):
        return self.sim_array.fs_snapshot_create(
            fs.id, snapshot_name, flags)

    def fs_snapshot_delete(self, fs, snapshot, flags=0):
        return self.sim_array.fs_snapshot_delete(
            fs.id, snapshot.id, flags)

    def fs_snapshot_restore(self, fs, snapshot, files, restore_files,
                            all_files=False, flags=0):
        return self.sim_array.fs_snapshot_restore(
            fs.id, snapshot.id, files, restore_files, all_files, flags)

    def fs_child_dependency(self, fs, files, flags=0):
        return self.sim_array.fs_child_dependency(fs.id, files, flags)

    def fs_child_dependency_rm(self, fs, files, flags=0):
        return self.sim_array.fs_child_dependency_rm(fs.id, files, flags)

    def export_auth(self, flags=0):
        # The API should change some day
        return ["standard"]

    def exports(self, search_key=None, search_value=None, flags=0):
        sim_exps = self.sim_array.exports(flags)
        return search_property(
            [SimPlugin._sim_data_2_lsm(e) for e in sim_exps],
            search_key, search_value)

    def export_fs(self, fs_id, export_path, root_list, rw_list, ro_list,
                  anon_uid, anon_gid, auth_type, options, flags=0):
        sim_exp = self.sim_array.fs_export(
            fs_id, export_path, root_list, rw_list, ro_list,
            anon_uid, anon_gid, auth_type, options, flags=0)
        return SimPlugin._sim_data_2_lsm(sim_exp)

    def export_remove(self, export, flags=0):
        return self.sim_array.fs_unexport(export.id, flags)

    def target_ports(self, search_key=None, search_value=None, flags=0):
        sim_tgts = self.sim_array.target_ports()
        return search_property(
            [SimPlugin._sim_data_2_lsm(t) for t in sim_tgts],
            search_key, search_value)

    def volume_raid_info(self, volume, flags=0):
        return self.sim_array.volume_raid_info(volume)

    def pool_member_info(self, pool, flags=0):
        return self.sim_array.pool_member_info(pool)

    def volume_raid_create_cap_get(self, system, flags=0):
        return self.sim_array.volume_raid_create_cap_get(system)

    def volume_raid_create(self, name, raid_type, disks, strip_size,
                           flags=0):
        return self.sim_array.volume_raid_create(
            name, raid_type, disks, strip_size)

    def volume_ident_led_on(self, volume, flags=0):
        return self.sim_array.volume_ident_led_on(volume)

    def volume_ident_led_off(self, volume, flags=0):
        return self.sim_array.volume_ident_led_off(volume)

    def batteries(self, search_key=None, search_value=None,
                  flags=Client.FLAG_RSVD):
        sim_batteries = self.sim_array.batteries()
        return search_property(
            [SimPlugin._sim_data_2_lsm(b) for b in sim_batteries],
            search_key, search_value)

    def volume_cache_info(self, volume, flags=Client.FLAG_RSVD):
        return self.sim_array.volume_cache_info(volume)

    def volume_physical_disk_cache_update(self, volume, pdc,
                                          flags=Client.FLAG_RSVD):
        return self.sim_array.volume_physical_disk_cache_update(volume, pdc)

    def volume_read_cache_policy_update(self, volume, rcp,
                                        flags=Client.FLAG_RSVD):
        return self.sim_array.volume_read_cache_policy_update(volume, rcp)

    def volume_write_cache_policy_update(self, volume, wcp,
                                         flags=Client.FLAG_RSVD):
        return self.sim_array.volume_write_cache_policy_update(volume, wcp)
