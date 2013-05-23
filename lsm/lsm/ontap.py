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
import urllib2
import urlparse

import na
from data import Volume, Initiator, FileSystem, Snapshot, NfsExport, \
    AccessGroup, System, Capabilities
from iplugin import IStorageAreaNetwork, INfs
from common import LsmError, ErrorNumber, JobStatus, md5
from lsm.version import VERSION
from data import Pool

#Maps na to lsm, this is expected to expand over time.
e_map = {
    na.Filer.ENOSPC: ErrorNumber.SIZE_INSUFFICIENT_SPACE,
    na.Filer.ENO_SUCH_VOLUME: ErrorNumber.NOT_FOUND_VOLUME,
    na.Filer.ESIZE_TOO_LARGE: ErrorNumber.SIZE_TOO_LARGE,
    na.Filer.ENO_SUCH_FS: ErrorNumber.NOT_FOUND_FS,
    na.Filer.EVOLUME_TOO_SMALL: ErrorNumber.SIZE_TOO_SMALL,
    na.Filer.EAPILICENSE: ErrorNumber.NOT_LICENSED,
    na.Filer.EFSDOESNOTEXIST: ErrorNumber.NOT_FOUND_FS,
    na.Filer.EFSOFFLINE: ErrorNumber.OFF_LINE,
    na.Filer.EFSNAMEINVALID: ErrorNumber.INVALID_NAME,
    na.Filer.ESERVICENOTLICENSED: ErrorNumber.NOT_LICENSED,
    na.Filer.ECLONE_LICENSE_EXPIRED: ErrorNumber.NOT_LICENSED,
    na.Filer.ECLONE_NOT_LICENSED: ErrorNumber.NOT_LICENSED,
    na.Filer.EINVALID_ISCSI_NAME: ErrorNumber.INVALID_IQN,
    na.Filer.ETIMEOUT: ErrorNumber.PLUGIN_TIMEOUT
}


def error_map(oe):
    """
    Maps a ontap error code to a lsm error code.
    Returns a tuple containing error code and text.
    """
    if oe.errno in e_map:
        return e_map[oe.errno], oe.reason
    else:
        return ErrorNumber.PLUGIN_ERROR, \
            oe.reason + " (vendor error code= " + str(oe.errno) + ")"


def handle_ontap_errors(method):
    def na_wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except na.FilerError as oe:
            error, error_msg = error_map(oe)
            raise LsmError(error, error_msg)
        except urllib2.HTTPError as he:
            if he.code == 401:
                raise LsmError(ErrorNumber.PLUGIN_AUTH_FAILED, he.msg)
            else:
                raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION, str(he))
        except urllib2.URLError as ce:
            raise LsmError(ErrorNumber.PLUGIN_UNKNOWN_HOST, str(ce))

    return na_wrapper


class Ontap(IStorageAreaNetwork, INfs):
    TMO_CONV = 1000.0

    (LSM_VOL_PREFIX, LSM_INIT_PREFIX) = ('lsm_lun_container', 'lsm_init_')

    (SS_JOB, SPLIT_JOB) = ('ontap-ss-file-restore', 'ontap-clone-split')

    def __init__(self):
        self.f = None
        self.sys_info = None

    @handle_ontap_errors
    def startup(self, uri, password, timeout, flags=0):
        ssl = False
        u = urlparse.urlparse(uri)

        if u.scheme.lower() == 'ontap+ssl':
            ssl = True

        self.f = na.Filer(u.hostname, u.username, password,
                          timeout / Ontap.TMO_CONV, ssl)
        #Smoke test
        i = self.f.system_info()
        #TODO Get real filer status
        self.sys_info = System(i['system-id'], i['system-name'],
                               System.STATUS_OK)
        return self.f.validate()

    def set_time_out(self, ms, flags=0):
        self.f.timeout = ms / Ontap.TMO_CONV

    def get_time_out(self, flags=0):
        return self.f.timeout * Ontap.TMO_CONV

    def shutdown(self, flags=0):
        self._jobs = None

    def _create_vpd(self, sn):
        """
        Construct the vpd83 for this lun
        """
        return "60a98000" + ''.join(["%02x" % ord(x) for x in sn])

    def _lun(self, l):
        #Info('_lun=' + str(l))
        block_size = int(l['block-size'])
        num_blocks = int(l['size']) / block_size
        #TODO: Need to retrieve actual volume status
        return Volume(l['serial-number'], l['path'],
                      self._create_vpd(l['serial-number']),
                      block_size, num_blocks, Volume.STATUS_OK,
                      self.sys_info.id, l['aggr'])

    def _vol(self, v):
        pool_name = v['containing-aggregate']
        pools = self.pools()

        for p in pools:
            if p.name == pool_name:
                return FileSystem(v['uuid'], v['name'], int(v['size-total']),
                                  int(v['size-available']), p.id,
                                  self.sys_info.id)

    def _ss(self, s):
        #If we use the newer API we can use the uuid instead of this fake
        #md5 one
        return Snapshot(md5(s['name'] + s['access-time']), s['name'],
                        s['access-time'])

    @handle_ontap_errors
    def volumes(self, flags=0):
        luns = self.f.luns_get_all()
        return [self._lun(l) for l in luns]

    @handle_ontap_errors
    def _pool(self, p):
        total = int(p['size-total'])
        used = int(p['size-used'])
        return Pool(p['uuid'], p['name'], total, total - used,
                    self.sys_info.id)

    @handle_ontap_errors
    def capabilities(self, system, flags=0):
        cap = Capabilities()
        cap.set(Capabilities.BLOCK_SUPPORT)
        cap.set(Capabilities.FS_SUPPORT)
        cap.set(Capabilities.INITIATORS)
        cap.set(Capabilities.VOLUMES)
        cap.set(Capabilities.VOLUME_CREATE)
        cap.set(Capabilities.VOLUME_RESIZE)
        cap.set(Capabilities.VOLUME_REPLICATE)
        cap.set(Capabilities.VOLUME_REPLICATE_CLONE)
        cap.set(Capabilities.VOLUME_COPY_RANGE_BLOCK_SIZE)
        cap.set(Capabilities.VOLUME_COPY_RANGE)
        cap.set(Capabilities.VOLUME_COPY_RANGE_CLONE)
        cap.set(Capabilities.VOLUME_DELETE)
        cap.set(Capabilities.VOLUME_ONLINE)
        cap.set(Capabilities.VOLUME_OFFLINE)
        cap.set(Capabilities.VOLUME_ISCSI_CHAP_AUTHENTICATION)
        cap.set(Capabilities.ACCESS_GROUP_GRANT)
        cap.set(Capabilities.ACCESS_GROUP_REVOKE)
        cap.set(Capabilities.ACCESS_GROUP_LIST)
        cap.set(Capabilities.ACCESS_GROUP_CREATE)
        cap.set(Capabilities.ACCESS_GROUP_DELETE)
        cap.set(Capabilities.ACCESS_GROUP_ADD_INITIATOR)
        cap.set(Capabilities.ACCESS_GROUP_DEL_INITIATOR)
        cap.set(Capabilities.VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP)
        cap.set(Capabilities.ACCESS_GROUPS_GRANTED_TO_VOLUME)
        cap.set(Capabilities.VOLUME_CHILD_DEPENDENCY)
        cap.set(Capabilities.VOLUME_CHILD_DEPENDENCY_RM)
        cap.set(Capabilities.FS)
        cap.set(Capabilities.FS_DELETE)
        cap.set(Capabilities.FS_RESIZE)
        cap.set(Capabilities.FS_CREATE)
        cap.set(Capabilities.FS_CLONE)
        cap.set(Capabilities.FILE_CLONE)
        cap.set(Capabilities.FS_SNAPSHOTS)
        cap.set(Capabilities.FS_SNAPSHOT_CREATE)
        cap.set(Capabilities.FS_SNAPSHOT_DELETE)
        cap.set(Capabilities.FS_SNAPSHOT_REVERT)
        cap.set(Capabilities.FS_CHILD_DEPENDENCY)
        cap.set(Capabilities.FS_CHILD_DEPENDENCY_RM)
        cap.set(Capabilities.EXPORT_AUTH)
        cap.set(Capabilities.EXPORTS)
        cap.set(Capabilities.EXPORT_FS)
        cap.set(Capabilities.EXPORT_REMOVE)
        cap.set(Capabilities.EXPORT_CUSTOM_PATH)
        return cap

    @handle_ontap_errors
    def plugin_info(self, flags=0):
        return "NetApp Filer support", VERSION

    @handle_ontap_errors
    def pools(self, flags=0):
        aggr = self.f.aggregates()
        return [self._pool(p) for p in aggr]

    @handle_ontap_errors
    def systems(self, flags=0):
        return [self.sys_info]

    @handle_ontap_errors
    def initiators(self, flags=0):
        """
        We will list all the initiators that are in all the groups.
        """
        rc = []
        groups = self.f.igroups()

        for g in groups:
            #Get the initiator in the group
            if g['initiators']:
                inits = na.to_list(g['initiators']['initiator-info'])

                for i in inits:
                    init = i['initiator-name']

                    if g['initiator-group-type'] == 'iscsi':
                        init_type = Initiator.TYPE_ISCSI
                    else:
                        init_type = Initiator.TYPE_PORT_WWN

                    name = init
                    rc.append(Initiator(init, init_type, name))

        return rc

    def _get_volume(self, vol_name, pool_id):
        return self._lun(self.f.luns_get_specific(pool_id, vol_name, None)[0])

    @handle_ontap_errors
    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags=0):

        v = self.f.volume_names()

        vol_prefix = Ontap.LSM_VOL_PREFIX + '_' + pool.name

        if vol_prefix not in v:
            self.f.volume_create(pool.name, vol_prefix, size_bytes)
        else:
            #re-size volume to accommodate new logical unit
            self.f.volume_resize(vol_prefix, self._size_kb_padded(size_bytes))

        lun_name = self.f.lun_build_name(vol_prefix, volume_name)

        try:
            self.f.lun_create(lun_name, size_bytes)
        except Exception as e:
            self.f.volume_resize(vol_prefix, -self._size_kb_padded(size_bytes))
            raise e

        #Get the information about the newly created LUN
        return None, self._get_volume(lun_name, pool.id)

    def _vol_to_na_volume_name(self, volume):
        return os.path.dirname(volume.name)[5:]

    @handle_ontap_errors
    def volume_delete(self, volume, flags=0):
        vol = self._vol_to_na_volume_name(volume)

        luns = self.f.luns_get_specific(aggr=volume.pool_id, na_volume_name=vol)

        if len(luns) == 1:
            self.f.volume_delete(vol)
        else:
            self.f.lun_delete(volume.name)

        return None

    @staticmethod
    def _size_kb_padded(size_bytes):
        return int((size_bytes / 1024) * 1.3)

    @handle_ontap_errors
    def volume_resize(self, volume, new_size_bytes, flags=0):
        na_vol = self._vol_to_na_volume_name(volume)
        diff = new_size_bytes - volume.size_bytes

        #Convert to KB and pad for snapshots
        diff = self._size_kb_padded(diff)

        #If the new size is > than old -> re-size volume then lun
        #If the new size is < than old -> re-size lun then volume
        if diff > 0:
            #if this raises an exception we are fine
            self.f.volume_resize(na_vol, diff)

            try:
                #if this raises an exception we need to revert the volume
                self.f.lun_resize(volume.name, new_size_bytes)
            except Exception as e:
                #Put the volume back to previous size
                self.f.volume_resize(na_vol, -diff)
                raise e
        else:
            self.f.lun_resize(volume.name, new_size_bytes)
            self.f.volume_resize(na_vol, diff)

        return None, self._get_volume(volume.name, volume.pool_id)

    def _volume_on_aggr(self, pool, volume):
        search = self._vol_to_na_volume_name(volume)
        contained_volumes = self.f.aggregate_volume_names(pool.name)
        return search in contained_volumes

    @handle_ontap_errors
    def volume_replicate(self, pool, rep_type, volume_src, name, flags=0):
        #At the moment we are only supporting space efficient writeable logical
        #units.  Add support for the others later.
        if rep_type != Volume.REPLICATE_CLONE:
            raise LsmError(ErrorNumber.NO_SUPPORT, "rep_type not supported")

        #Check to see if our volume is on a pool that was passed in or that
        #the pool itself is None
        if pool is None or self._volume_on_aggr(pool, volume_src):
            #re-size the NetApp volume to accommodate the new lun
            size = self._size_kb_padded(volume_src.size_bytes)

            self.f.volume_resize(self._vol_to_na_volume_name(volume_src), size)

            #Thin provision copy the logical unit
            dest = os.path.dirname(volume_src.name) + '/' + name

            try:
                self.f.clone(volume_src.name, dest)
            except Exception as e:
                #Put volume back to previous size
                self.f.volume_resize(self._vol_to_na_volume_name(volume_src),
                                     -size)
                raise e
            return None, self._get_volume(dest, volume_src.pool_id)
        else:
            #TODO Need to get instructions on how to provide this functionality
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Unable to replicate volume to different pool")

    @handle_ontap_errors
    def volume_replicate_range_block_size(self, system, flags=0):
        return 4096

    @handle_ontap_errors
    def volume_replicate_range(self, rep_type, volume_src, volume_dest, ranges,
                               flags=0):
        if rep_type != Volume.REPLICATE_CLONE:
            raise LsmError(ErrorNumber.NO_SUPPORT, "rep_type not supported")
        self.f.clone(volume_src.name, volume_dest.name, None, ranges)

    @handle_ontap_errors
    def volume_online(self, volume, flags=0):
        return self.f.lun_online(volume.name)

    @handle_ontap_errors
    def volume_offline(self, volume, flags=0):
        return self.f.lun_offline(volume.name)

    @handle_ontap_errors
    def access_group_grant(self, group, volume, access, flags=0):
        self.f.lun_map(group.name, volume.name)
        return None

    @handle_ontap_errors
    def access_group_revoke(self, group, volume, flags=0):
        self.f.lun_unmap(group.name, volume.name)
        return None

    def _initiators_in_group(self, g):
        rc = []
        if g:
            if 'initiators' in g and g['initiators'] is not None:
                initiators = na.to_list(g['initiators']['initiator-info'])
                for i in initiators:
                    rc.append(i['initiator-name'])
        return rc

    def _access_group(self, g):
        name = g['initiator-group-name']

        if 'initiator-group-uuid' in g:
            ag_id = g['initiator-group-uuid']
        else:
            ag_id = md5(name)

        return AccessGroup(ag_id, name, self._initiators_in_group(g),
                           self.sys_info.id)

    @handle_ontap_errors
    def access_group_list(self, flags=0):
        groups = self.f.igroups()
        return [self._access_group(g) for g in groups]

    @handle_ontap_errors
    def access_group_create(self, name, initiator_id, id_type, system_id,
                            flags=0):
        cur_groups = self.access_group_list()
        for cg in cur_groups:
            if cg.name == name:
                raise LsmError(ErrorNumber.EXISTS_ACCESS_GROUP,
                               "Access group exists!")

        if id_type == Initiator.TYPE_ISCSI:
            self.f.igroup_create(name, 'iscsi')
        else:
            self.f.igroup_create(name, 'fcp')

        self.f.igroup_add_initiator(name, initiator_id)

        groups = self.access_group_list()
        for g in groups:
            if g.name == name:
                return g

        raise LsmError(ErrorNumber.INTERNAL_ERROR,
                       "Unable to find group just created!")

    @handle_ontap_errors
    def access_group_del(self, group, flags=0):
        return self.f.igroup_delete(group.name)

    @handle_ontap_errors
    def access_group_add_initiator(self, group, initiator_id, id_type, flags=0):
        return self.f.igroup_add_initiator(group.name, initiator_id)

    @handle_ontap_errors
    def access_group_del_initiator(self, group, initiator_id, flags=0):
        return self.f.igroup_del_initiator(group.name, initiator_id)

    @handle_ontap_errors
    def volumes_accessible_by_access_group(self, group, flags=0):
        rc = []

        if len(group.initiators):
            luns = self.f.lun_initiator_list_map_info(group.initiators[0],
                                                      group.name)
            rc = [self._lun(l) for l in luns]

        return rc

    @handle_ontap_errors
    def access_groups_granted_to_volume(self, volume, flags=0):
        groups = self.f.lun_map_list_info(volume.name)
        return [self._access_group(g) for g in groups]

    @handle_ontap_errors
    def iscsi_chap_auth(self, initiator, in_user, in_password, out_user,
                        out_password, flags=0):
        if out_user and out_password and \
                (in_user is None or in_password is None):
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "out_user and out_password only supported if "
                           "inbound is supplied")

        self.f.iscsi_initiator_add_auth(initiator.id, in_user, in_password,
                                        out_user, out_password)

    @handle_ontap_errors
    def initiator_grant(self, initiator_id, initiator_type, volume, access,
                        flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_ontap_errors
    def initiator_revoke(self, initiator, volume, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_ontap_errors
    def volumes_accessible_by_initiator(self, initiator, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_ontap_errors
    def initiators_granted_to_volume(self, volume, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @staticmethod
    def _rpercent(total, current):
        p = 1 - (current / float(total))
        p = min(int(100 * p), 100)
        return p

    def _restore_file_status(self, num):
        running = self.f.snapshot_file_restore_num()

        if running:
            running = min(num, running)
            return JobStatus.INPROGRESS, Ontap._rpercent(num, running), None

        return JobStatus.COMPLETE, 100, None

    def _clone_split_status(self, volumes):
        vols = volumes.split(',')
        current = len(vols)

        #It doesn't appear that we have a good percentage
        #indicator from the clone split status...
        running = self.f.volume_split_status()

        for v in vols:
            if v not in running:
                current -= 1

        if not running:
            return JobStatus.COMPLETE, 100, None
        else:
            return JobStatus.INPROGRESS, \
                Ontap._rpercent(len(vols), current), None

    @handle_ontap_errors
    def job_status(self, job_id, flags=0):
        if job_id is None and '@' not in job_id:
            raise LsmError(ErrorNumber.INVALID_JOB, "Invalid job, missing @")

        job = job_id.split('@', 2)

        if job[0] == Ontap.SS_JOB:
            return self._restore_file_status(int(job[1]))
        elif job[0] == Ontap.SPLIT_JOB:
            return self._clone_split_status(job[1])

        raise LsmError(ErrorNumber.INVALID_JOB, "Invalid job")

    @handle_ontap_errors
    def job_free(self, job_id, flags=0):
        return None

    @handle_ontap_errors
    def fs(self, flags=0):
        volumes = self.f.volumes()
        return [self._vol(v) for v in volumes]

    @handle_ontap_errors
    def fs_delete(self, fs, flags=0):
        self.f.volume_delete(fs.name)

    @handle_ontap_errors
    def fs_resize(self, fs, new_size_bytes, flags=0):
        diff = new_size_bytes - fs.total_space

        diff = self._size_kb_padded(diff)
        self.f.volume_resize(fs.name, diff)
        return None, self._vol(self.f.volumes(fs.name)[0])

    @handle_ontap_errors
    def fs_create(self, pool, name, size_bytes, flags=0):
        self.f.volume_create(pool.name, name, size_bytes)
        return None, self._vol(self.f.volumes(name)[0])

    @handle_ontap_errors
    def fs_clone(self, src_fs, dest_fs_name, snapshot=None, flags=0):
        self.f.volume_clone(src_fs.name, dest_fs_name, snapshot)
        return None, self._vol(self.f.volumes(dest_fs_name)[0])

    @staticmethod
    def build_name(volume_name, relative_name):
        return "/vol/%s/%s" % (volume_name, relative_name)

    @handle_ontap_errors
    def file_clone(self, fs, src_file_name, dest_file_name, snapshot=None,
                   flags=0):
        full_src = Ontap.build_name(fs.name, src_file_name)
        full_dest = Ontap.build_name(fs.name, dest_file_name)

        ss = None
        if snapshot:
            ss = snapshot.name

        self.f.clone(full_src, full_dest, ss)
        return None

    @handle_ontap_errors
    def fs_snapshots(self, fs, flags=0):
        snapshots = self.f.snapshots(fs.name)
        return [self._ss(s) for s in snapshots]

    @handle_ontap_errors
    def fs_snapshot_create(self, fs, snapshot_name, files=None, flags=0):
        #We can't do files, so we will do them all
        snap = self.f.snapshot_create(fs.name, snapshot_name)
        return None, self._ss(snap)

    @handle_ontap_errors
    def fs_snapshot_delete(self, fs, snapshot, flags=0):
        self.f.snapshot_delete(fs.name, snapshot.name)

    def _ss_revert_files(self, volume_name, snapshot_name, files,
                         restore_files):
        for i in range(len(files)):
            src = Ontap.build_name(volume_name, files[i])
            dest = None
            if restore_files and len(restore_files):
                dest = Ontap.build_name(volume_name, restore_files[i])
            self.f.snapshot_restore_file(snapshot_name, src, dest)

    @handle_ontap_errors
    def fs_snapshot_revert(self, fs, snapshot, files, restore_files,
                           all_files=False, flags=0):
        """
        Restores a FS or files on a FS.
        Note: Restoring an individual file is a O(n) operation, i.e. time it
        takes to restore a file depends on the file size.  Reverting an entire
        FS is O(1).  Try to avoid restoring individual files from a snapshot.
        """
        if files is None and all_files:
            self.f.snapshot_restore_volume(fs.name, snapshot.name)
            return None
        elif files:
            if restore_files and len(files) != len(restore_files):
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               "num files != num restore_files")

            self._ss_revert_files(fs.name, snapshot.name, files, restore_files)
            return "%s@%d" % (Ontap.SS_JOB, len(files))
        else:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Invalid parameter combination")

    @handle_ontap_errors
    def export_auth(self, flags=0):
        """
        Returns the types of authentication that are available for NFS
        """
        return self.f.export_auth_types()

    def _get_root(self, e):
        if 'root' in e:
            return [r['name']
                    for r in na.to_list(e['root']['exports-hostname-info'])]
        else:
            return []

    def _get_group(self, access_group, e):
        rc = []

        if access_group in e:
            for r in na.to_list(e[access_group]['exports-hostname-info']):
                if 'all-hosts' in r:
                    if r['all-hosts'] == 'true':
                        rc.append('*')
                else:
                    rc.append(r['name'])
        return rc

    def _get_value(self, key, e):
        if key in e:
            return e[key]
        else:
            return None

    def _get_volume_id(self, volumes, vol_name):
        for v in volumes:
            if v.name == vol_name:
                return v.id
        raise RuntimeError("Volume not found in volumes:" +
                           ":".join(volumes) + " " + vol_name)

    @staticmethod
    def _get_volume_from_path(path):
        #Volume paths have the form /vol/<volume name>/<rest of path>
        return path[5:].split('/')[0]

    def _export(self, volumes, e):
        if 'actual-pathname' in e:
            path = e['actual-pathname']
            export = e['pathname']
        else:
            path = e['pathname']
            export = e['pathname']

        vol_name = Ontap._get_volume_from_path(path)
        fs_id = self._get_volume_id(volumes, vol_name)

        return NfsExport(md5(vol_name + fs_id),
                         fs_id,
                         export,
                         e['sec-flavor']['sec-flavor-info']['flavor'],
                         self._get_group('root', e),
                         self._get_group('read-write', e),
                         self._get_group('read-only', e),
                         self._get_value('anon', e),
                         None,
                         None)

    @handle_ontap_errors
    def exports(self, flags=0):
        #Get the file systems once and pass to _export which needs to lookup
        #the file system id by name.
        v = self.fs()
        return [self._export(v, e) for e in self.f.nfs_exports()]

    def _get_volume_from_id(self, fs_id):
        fs = self.fs()
        for i in fs:
            if i.id == fs_id:
                return i
        raise RuntimeError("fs id not found in fs:" + fs_id)

    def _current_export(self, export_path):
        """
        Checks to see if we already have this export.
        """
        cur_exports = self.exports()
        for ce in cur_exports:
            if ce.export_path == export_path:
                return True

        return False

    @handle_ontap_errors
    def export_fs(self, fs_id, export_path, root_list, rw_list, ro_list,
                  anon_uid, anon_gid, auth_type, options, flags=0):
        """
        Creates or modifies the specified export
        """

        #Get the volume info from the fs_id
        vol = self._get_volume_from_id(fs_id)

        #If the export already exists we need to update the existing export
        #not create a new one.
        if self._current_export(export_path):
            method = self.f.nfs_export_fs_modify2
        else:
            method = self.f.nfs_export_fs2

        method('/vol/' + vol.name,
               export_path,
               ro_list,
               rw_list,
               root_list,
               anon_uid,
               auth_type)

        current_exports = self.exports()
        for e in current_exports:
            if e.fs_id == fs_id and e.export_path == export_path:
                return e

        raise LsmError(ErrorNumber.PLUGIN_ERROR,
                       "export not created successfully!")

    @handle_ontap_errors
    def export_remove(self, export, flags=0):
        self.f.nfs_export_remove([export.export_path])

    @handle_ontap_errors
    def volume_child_dependency(self, volume, flags=0):
        return False

    @handle_ontap_errors
    def volume_child_dependency_rm(self, volume, flags=0):
        return None

    @handle_ontap_errors
    def fs_child_dependency(self, fs, files=None, flags=0):
        rc = False

        #TODO: Make sure file actually exists if specified

        if not files:
            children = self.f.volume_children(fs.name)
            if children:
                rc = True
        return rc

    @handle_ontap_errors
    def fs_child_dependency_rm(self, fs, files=None, flags=0):
        if files:
            return None
        else:
            children = self.f.volume_children(fs.name)
            if children:
                for c in children:
                    self.f.volume_split_clone(c)
                return "%s@%s" % (Ontap.SPLIT_JOB, ",".join(children))
        return None
