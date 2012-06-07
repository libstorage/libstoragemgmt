# Copyright (C) 2011-2012 Red Hat, Inc.
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
    AccessGroup, System
from iplugin import IStorageAreaNetwork, INfs
from common import  LsmError, ErrorNumber, JobStatus, md5
from data import Pool

#Maps na to lsm, this is expected to expand over time.
e_map = {
    na.Filer.ENOSPC:                    ErrorNumber.SIZE_INSUFFICIENT_SPACE,
    na.Filer.ENO_SUCH_VOLUME:           ErrorNumber.NOT_FOUND_VOLUME,
    na.Filer.ESIZE_TOO_LARGE:           ErrorNumber.SIZE_TOO_LARGE,
    na.Filer.ENO_SUCH_FS:               ErrorNumber.NOT_FOUND_FS,
    na.Filer.EVOLUME_TOO_SMALL:         ErrorNumber.SIZE_TOO_SMALL,
    na.Filer.EAPILICENSE:               ErrorNumber.NOT_LICENSED,
    na.Filer.EFSDOESNOTEXIST:           ErrorNumber.NOT_FOUND_FS,
    na.Filer.EFSOFFLINE:                ErrorNumber.OFF_LINE,
    na.Filer.EFSNAMEINVALID:            ErrorNumber.INVALID_NAME,
    na.Filer.ESERVICENOTLICENSED:       ErrorNumber.NOT_LICENSED,
    na.Filer.ECLONE_LICENSE_EXPIRED:    ErrorNumber.NOT_LICENSED,
    na.Filer.ECLONE_NOT_LICENSED:       ErrorNumber.NOT_LICENSED
}


def error_map(oe):
    """
    Maps a ontap error code to a lsm error code.
    Returns a tuple containing error code and text.
    """
    if oe.errno in e_map:
        return e_map[oe.errno], oe.reason
    else:
        return ErrorNumber.PLUGIN_ERROR, oe.reason + \
                                  " (vendor error code= " + oe.errno + ")"


def handle_ontap_errors(method):
    def na_wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except na.FilerError as oe:
            error, error_msg = error_map(oe)
            raise LsmError(error, error_msg + ":" + str(oe.errno))
        except urllib2.HTTPError as he:
            if he.code == 401:
                raise LsmError(ErrorNumber.PLUGIN_AUTH_FAILED, he.msg)
            else:
                raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION, str(he))
        except urllib2.URLError as ce:
            raise LsmError(ErrorNumber.PLUGIN_UNKNOWN_HOST, str(ce))
    return na_wrapper


class Ontap(IStorageAreaNetwork, INfs):

    (LSM_VOL_PREFIX, LSM_INIT_PREFIX) = ('lsm_lun_container', 'lsm_init_')

    (SS_JOB, SPLIT_JOB) = ('ontap-ss-file-restore', 'ontap-clone-split')

    def __init__(self):
        self.f = None
        self.sys_info = None

    @handle_ontap_errors
    def startup(self, uri, password, timeout):
        ssl = False
        u = urlparse.urlparse(uri)
        self.tmo = timeout

        if u.scheme.lower() == 'ontap+ssl':
            ssl = True

        self.f = na.Filer(u.hostname, u.username, password, ssl)
        #Smoke test
        i = self.f.system_info()
        self.sys_info = System(i['system-id'], i['system-name'])
        return self.f.validate()

    def set_time_out(self, ms):
        self.tmo = ms

    def get_time_out(self):
        return self.tmo

    def shutdown(self):
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
            self.sys_info.id)

    def _vol(self, v):
        pool_name = v['containing-aggregate']
        pools = self.pools()

        for p in pools:
            if p.name == pool_name:
                return FileSystem(v['uuid'], v['name'], int(v['size-total']),
                    int(v['size-available']), p.id, self.sys_info.id)

    def _ss(self, s):
        #If we use the newer API we can use the uuid instead of this fake
        #md5 one
        return Snapshot(md5(s['name'] + s['access-time']), s['name'],
                            s['access-time'])

    @handle_ontap_errors
    def volumes(self):
        luns = self.f.luns()
        return [self._lun(l) for l in luns]

    @handle_ontap_errors
    def _pool(self, p):
        total = int(p['size-total'])
        used = int(p['size-used'])
        return Pool(p['uuid'], p['name'], total, total - used,
                        self.sys_info.id)

    @handle_ontap_errors
    def pools(self):
        aggr = self.f.aggregates()
        return [self._pool(p) for p in aggr]

    @handle_ontap_errors
    def systems(self):
        return [self.sys_info]

    @handle_ontap_errors
    def initiators(self):
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
                    id = i['initiator-name']

                    if g['initiator-group-type'] == 'iscsi':
                        type = Initiator.TYPE_ISCSI
                    else:
                        type = Initiator.TYPE_PORT_WWN

                    name = id
                    rc.append(Initiator(id, type, name))

        return rc

    def _get_volume(self, vol_name):
        return self._lun(self.f.luns(vol_name)[0])

    @handle_ontap_errors
    def volume_create(self, pool, volume_name, size_bytes, provisioning):

        v = self.f.volume_names()

        if Ontap.LSM_VOL_PREFIX not in v:
            self.f.volume_create(pool.name, Ontap.LSM_VOL_PREFIX, size_bytes)
        else:
            #re-size volume to accommodate new logical unit
            self.f.volume_resize(Ontap.LSM_VOL_PREFIX, size_bytes)

        lun_name = self.f.lun_build_name(Ontap.LSM_VOL_PREFIX, volume_name)
        self.f.lun_create(lun_name, size_bytes)

        #Get the information about the newly created LUN
        return None, self._get_volume(lun_name)

    def _vol_to_na_volume_name(self, volume):
        return os.path.dirname(volume.name)[5:]

    @handle_ontap_errors
    def volume_delete(self, volume):
        vol = self._vol_to_na_volume_name(volume)

        luns = self.f.luns(na_volume_name=vol)

        if len(luns) == 1:
            self.f.volume_delete(vol)
        else:
            self.f.lun_delete(volume.name)

        return None

    @handle_ontap_errors
    def volume_resize(self, volume, new_size_bytes):
        na_vol = self._vol_to_na_volume_name(volume)
        diff = new_size_bytes - volume.size_bytes

        #If the new size is > than old -> re-size volume then lun
        #If the new size is < than old -> re-size lun then volume
        if diff > 0:
            self.f.volume_resize(na_vol, diff)
            self.f.lun_resize(volume.name, new_size_bytes)
        else:
            self.f.lun_resize(volume.name, new_size_bytes)
            self.f.volume_resize(na_vol, diff)

        return None, self._get_volume(volume.name)

    def _volume_on_aggr(self, pool, volume):
        search = self._vol_to_na_volume_name(volume)
        contained_volumes = self.f.aggregate_volume_names(pool.name)
        return search in contained_volumes

    @handle_ontap_errors
    def volume_replicate(self, pool, rep_type, volume_src, name):
        #At the moment we are only supporting space efficient writeable logical
        #units.  Add support for the others later.
        if rep_type != Volume.REPLICATE_CLONE:
            raise LsmError(ErrorNumber.NO_SUPPORT, "rep_type not supported")

        #Check to see if our volume is on a pool that was passed in
        if self._volume_on_aggr(pool, volume_src):
            #re-size the NetApp volume to accommodate the new lun
            self.f.volume_resize(self._vol_to_na_volume_name(volume_src),
                volume_src.size_bytes)

            #Thin provision copy the logical unit
            dest = os.path.dirname(volume_src.name) + '/' + name
            self.f.clone(volume_src.name, dest)
            return None, self._get_volume(dest)
        else:
            #TODO Need to get instructions on how to provide this functionality
            raise LsmError(ErrorNumber.NO_SUPPORT,
                        "Unable to replicate volume to different pool")

    @handle_ontap_errors
    def volume_replicate_range_block_size(self):
        return 4096

    @handle_ontap_errors
    def volume_replicate_range(self, rep_type, vol_src, vol_dest, ranges):
        if rep_type != Volume.REPLICATE_CLONE:
            raise LsmError(ErrorNumber.NO_SUPPORT, "rep_type not supported")
        self.f.clone(vol_src.name, vol_dest.name, None, ranges)

    @handle_ontap_errors
    def volume_online(self, volume):
        return self.f.lun_online(volume.name)

    @handle_ontap_errors
    def volume_offline(self, volume):
        return self.f.lun_offline(volume.name)

    @handle_ontap_errors
    def access_group_grant(self, group, volume, access):
        self.f.lun_map(group.name, volume.name)
        return None

    @handle_ontap_errors
    def access_group_revoke(self, group, volume):
        self.f.lun_unmap(group.name, volume.name)
        return None

    def _initiators_in_group(self, g):
        rc = []
        if g:
            if 'initiators' in g and g['initiators'] is not None:
                initiators = na.to_list(g['initiators']['initiator-info'])
                for i in initiators:
                    if g['initiator-group-type'] == 'iscsi':
                        type = Initiator.TYPE_ISCSI
                    else:
                        type = Initiator.TYPE_PORT_WWN

                    rc.append(i['initiator-name'])
        return rc

    def _access_group(self, g):
        name = g['initiator-group-name']

        if 'initiator-group-uuid' in g:
            id = g['initiator-group-uuid']
        else:
            id = md5(name)

        return AccessGroup(id, name, self._initiators_in_group(g),
            self.sys_info.id)

    @handle_ontap_errors
    def access_group_list(self):
        groups = self.f.igroups()
        return [self._access_group(g) for g in groups]

    @handle_ontap_errors
    def access_group_create(self, name, initiator_id, id_type, system_id):
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
    def access_group_del(self, group):
        return self.f.igroup_delete(group.name)

    @handle_ontap_errors
    def access_group_add_initiator(self, group, initiator_id, id_type):
        return self.f.igroup_add_initiator(group.name, initiator_id)

    @handle_ontap_errors
    def access_group_del_initiator(self, group, initiator):
        return self.f.igroup_del_initiator(group.name, initiator.name)

    @handle_ontap_errors
    def volumes_accessible_by_access_group(self, group):
        rc = []

        if len(group.initiators):
            luns = self.f.lun_initiator_list_map_info(group.initiators[0],
                group.name)
            rc = [self._lun(l) for l in luns]

        return rc

    @handle_ontap_errors
    def access_groups_granted_to_volume(self, volume):
        groups = self.f.lun_map_list_info(volume.name)
        return [self._access_group(g) for g in groups]

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
            return JobStatus.INPROGRESS, Ontap._rpercent(len(vols), current), \
                   None

    @handle_ontap_errors
    def job_status(self, job_id):
        if job_id is None and '@' not in job_id:
            raise LsmError(ErrorNumber.INVALID_JOB, "Invalid job, missing @")

        job = job_id.split('@', 2)

        if job[0] == Ontap.SS_JOB:
            return self._restore_file_status(int(job[1]))
        elif job[0] == Ontap.SPLIT_JOB:
            return self._clone_split_status(job[1])

        raise LsmError(ErrorNumber.INVALID_JOB, "Invalid job")

    @handle_ontap_errors
    def job_free(self, job_id):
        return None

    @handle_ontap_errors
    def fs(self):
        volumes = self.f.volumes()
        return [self._vol(v) for v in volumes]

    @handle_ontap_errors
    def fs_delete(self, fs):
        self.f.volume_delete(fs.name)

    @handle_ontap_errors
    def fs_resize(self, fs, new_size_bytes):
        diff = new_size_bytes - fs.total_space
        self.f.volume_resize(fs.name, diff)
        return None, self._vol(self.f.volumes(fs.name)[0])

    @handle_ontap_errors
    def fs_create(self, pool, name, size_bytes):
        self.f.volume_create(pool.name, name, size_bytes)
        return None, self._vol(self.f.volumes(name)[0])

    @handle_ontap_errors
    def fs_clone(self, src_fs, dest_fs_name, snapshot=None):
        self.f.volume_clone(src_fs.name, dest_fs_name, snapshot)
        return None, self._vol(self.f.volumes(dest_fs_name)[0])

    @staticmethod
    def build_name(volume_name, relative_name):
        return "/vol/%s/%s" % (volume_name, relative_name)

    @handle_ontap_errors
    def file_clone(self, fs, src_file_name, dest_file_name, snapshot=None):
        full_src = Ontap.build_name(fs.name, src_file_name)
        full_dest = Ontap.build_name(fs.name, dest_file_name)

        ss = None
        if snapshot:
            ss = snapshot.name

        self.f.clone(full_src, full_dest, ss)
        return None

    @handle_ontap_errors
    def snapshots(self, fs):
        snapshots = self.f.snapshots(fs.name)
        return [self._ss(s) for s in snapshots]

    @handle_ontap_errors
    def snapshot_create(self, fs, snapshot_name, files=None):
        #We can't do files, so we will do them all
        snap = self.f.snapshot_create(fs.name, snapshot_name)
        return None, self._ss(snap)

    @handle_ontap_errors
    def snapshot_delete(self, fs, snapshot):
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
    def snapshot_revert(self, fs, snapshot, files, restore_files,
                        all_files=False):
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
            return ("%s@%d") % (Ontap.SS_JOB, len(files))
        else:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                            "Invalid parameter combination")

    @handle_ontap_errors
    def export_auth(self):
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
            None
        )

    @handle_ontap_errors
    def exports(self):
        #Get the file systems once and pass to _export which needs to lookup
        #the file system id by name.
        v = self.fs()
        return [self._export(v, e) for e in self.f.nfs_exports()]

    def _get_volume_from_id(self, id):
        fs = self.fs()
        for i in fs:
            if i.id == id:
                return i
        raise RuntimeError("fs id not found in fs:" +
                           ":".join(fs) + " " + id)

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
    def export_fs(self, export):
        """
        Creates or modifies the specified export
        """

        #Get the volume info from the fs_id
        vol = self._get_volume_from_id(export.fs_id)

        #If the export already exists we need to update the existing export
        #not create a new one.
        if self._current_export(export.export_path):
            method = self.f.nfs_export_fs_modify2
        else:
            method = self.f.nfs_export_fs2

        method('/vol/' + vol.name,
            export.export_path,
            export.ro,
            export.rw,
            export.root,
            export.anonuid,
            export.auth)

        current_exports = self.exports()
        for e in current_exports:
            if e.fs_id == export.fs_id and e.export_path == export.export_path:
                return e

        raise LsmError(ErrorNumber.PLUGIN_ERROR,
                        "export not created successfully!")

    @handle_ontap_errors
    def export_remove(self, export):
        self.f.nfs_export_remove([export.export_path])

    @handle_ontap_errors
    def volume_child_dependency(self, volume):
        return False

    @handle_ontap_errors
    def volume_child_dependency_rm(self, volume):
        return None

    @handle_ontap_errors
    def fs_child_dependency(self, fs, file=None):
        rc = False

        #TODO: Make sure file actually exists if specified

        if not file:
            children = self.f.volume_children(fs.name)
            if children:
                rc = True
        return rc

    @handle_ontap_errors
    def fs_child_dependency_rm(self, fs, file=None):
        if file:
            return None
        else:
            children = self.f.volume_children(fs.name)
            if children:
                for c in children:
                    self.f.volume_split_clone(c)
                return "%s@%s" % (Ontap.SPLIT_JOB, ",".join(children))
        return None
