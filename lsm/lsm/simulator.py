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

from common import LsmError, ErrorNumber, JobStatus, md5, uri_parse
import random
from data import Pool, Initiator, Volume, BlockRange, System, AccessGroup, Snapshot, NfsExport
import time
import pickle
from data import FileSystem
from iplugin import INfs, IStorageAreaNetwork
from lsm.data import Capabilities

SIM_DATA_FILE = '/tmp/lsm_sim_data'
duration = os.getenv("LSM_SIM_TIME", 1)

class SimJob(object):
    """
    Simulates a longer running job, uses actual wall time.  If test cases
    take too long we can reduce time by shortening time duration.
    """
    def __calc_progress(self):
        if self.percent < 100:
            end = self.start + self.duration
            now = time.time()
            if now > end:
                self.percent = 100
                self.status = JobStatus.COMPLETE
            else:
                diff = now - self.start
                self.percent = int(100 * (diff / self.duration))

    def __init__(self, item_to_return):
        self.status = JobStatus.INPROGRESS
        self.percent = 0
        self.__item = item_to_return
        self.start = time.time()
        self.duration = float(random.randint(0, int(duration)))

    def progress(self):
        """
        Returns a tuple (status, percent, volume)
        """
        self.__calc_progress()
        return self.status, self.percent, self.item

    @property
    def item(self):
        if self.percent >= 100:
            return self.__item
        return None

    @item.setter
    def item(self, value):
        self.__item = value


class SimState(object):

    def __init__(self):
        self.sys_info = System('sim-01', 'LSM simulated storage plug-in',
                                System.STATUS_OK)
        p1 = Pool('POO1', 'Pool 1', 2 ** 64, 2 ** 64, self.sys_info.id)
        p2 = Pool('POO2', 'Pool 2', 2 ** 64, 2 ** 64, self.sys_info.id)
        p3 = Pool('POO3', 'Pool 3', 2 ** 64, 2 ** 64, self.sys_info.id)
        p4 = Pool('POO4', 'lsm_test_aggr', 2 ** 64, 2 ** 64, self.sys_info.id)

        pm1 = {'pool': p1, 'volumes': {}}
        pm2 = {'pool': p2, 'volumes': {}}
        pm3 = {'pool': p3, 'volumes': {}}
        pm4 = {'pool': p4, 'volumes': {}}

        self.pools = {p1.id: pm1, p2.id: pm2, p3.id: pm3, p4.id: pm4}
        self.volumes = {}
        self.vol_num = 1
        self.access_groups = {}

        self.fs = {}
        self.fs_num = 1

        self.tmo = 30000
        self.jobs = {}
        self.job_num = 1

        #These express relationships between initiators and volumes.  This
        #is done because if you delete either part of the relationship
        #you need to delete the association between them.  Holding this stuff
        #in a db would be easier :-)
        self.group_grants = {}      # {access group id : {volume id: access }}


class StorageSimulator(INfs,IStorageAreaNetwork):
    """
    Simple class that implements enough to allow the framework to be exercised.
    """
    @staticmethod
    def __randomVpd():
        """
        Generate a random 16 digit number as hex
        """
        vpd = []
        for i in range(0, 16):
            vpd.append(str('%02X' % (random.randint(0, 255))))
        return "".join(vpd)

    def __block_rounding(self, size_bytes):
        """
        Round the requested size to block size.
        """
        return (size_bytes / 512) * 512

    def __create_job(self, returned_item):
        if True:
        #if random.randint(0,5) == 1:
            self.s.job_num += 1
            job = "JOB_" + str(self.s.job_num)
            self.s.jobs[job] = SimJob(returned_item)
            return job, None
        else:
            return None, returned_item

    def _load(self):
        tmp = None
        if os.path.exists(self.file):
            f = open(self.file, 'rb')
            tmp = pickle.load(f)
            f.close()
        return tmp

    def _save(self):
        f = open(self.file, 'wb')
        pickle.dump(self.s, f)
        f.close()

        #If we run via the daemon the file will be owned by libstoragemgmt
        #and if we run sim_lsmplugin stand alone we will be unable to
        #change the permissions.
        try:
            os.chmod(self.file, 0666)
        except OSError:
            pass

    def _load_state(self):
        prev = self._load()
        if prev:
            self.s = prev
        else:
            self.s = SimState()

    @staticmethod
    def _check_sl(string_list):
        """
        String list should be an empty list or a list with items
        """
        if string_list is not None and isinstance(string_list, list):
            pass
        else:
            raise LsmError(ErrorNumber.INVALID_SL, 'Invalid string list')

    def __init__(self):

        self.file = SIM_DATA_FILE
        self._load_state()

    def _allocate_from_pool(self, pool_id, size_bytes):
        p = self.s.pools[pool_id]['pool']

        rounded_size = self.__block_rounding(size_bytes)

        if p.free_space >= rounded_size:
            p.free_space -= rounded_size
        else:
            raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                'Insufficient space in pool')
        return rounded_size

    def _deallocate_from_pool(self, pool_id, size_bytes):
        p = self.s.pools[pool_id]['pool']
        p.free_space += size_bytes

    def _new_access_group(self, name, h):
        return AccessGroup(md5(name), name,
            [i.id for i in h['initiators']], self.s.sys_info.id)

    def _create_vol(self, pool, name, size_bytes):
        actual_size = self._allocate_from_pool(pool.id, size_bytes)

        nv = Volume('Vol' + str(self.s.vol_num), name,
            StorageSimulator.__randomVpd(), 512,
            (actual_size / 512), Volume.STATUS_OK, self.s.sys_info.id)
        self.s.volumes[nv.id] = {'pool': pool, 'volume': nv}
        self.s.vol_num += 1
        return self.__create_job(nv)

    def _create_fs(self, pool, name, size_bytes):
        if pool.id in self.s.pools:
            p = self.s.pools[pool.id]['pool']
            actual_size = self._allocate_from_pool(p.id, size_bytes)

            new_fs = FileSystem('FS' + str(self.s.fs_num), name, actual_size,
                        actual_size, p.id, self.s.sys_info.id)

            self.s.fs[new_fs.id] = { 'pool': p, 'fs':new_fs, 'ss': {},
                                     'exports' : {} }
            self.s.fs_num += 1
            return self.__create_job(new_fs)
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_POOL, 'Pool not found')

    def startup(self, uri, password, timeout, flags = 0):
        self.uri = uri
        self.password = password

        #The caller may want to start clean, so we allow the caller to specify
        #a file to store and retrieve individual state.
        qp = uri_parse(uri)
        if 'parameters' in qp and 'statefile' in qp['parameters'] and \
            qp['parameters']['statefile'] is not None:
            self.file = qp['parameters']['statefile']
            self._load_state()

        return None

    def set_time_out(self, ms, flags = 0):
        self.tmo = ms
        return None

    def get_time_out(self, flags = 0):
        return self.tmo

    def capabilities(self, system, flags = 0):
        rc = Capabilities()
        rc.enable_all()
        return rc

    def shutdown(self, flags = 0):
        self._save()

    def systems(self, flags = 0):
        return [ self.s.sys_info ]

    def job_status(self, job_id, flags = 0):
        if job_id in self.s.jobs:
            return self.s.jobs[job_id].progress()
        raise LsmError(ErrorNumber.NOT_FOUND_JOB, 'Non-existent job')

    def job_free(self, job_id, flags = 0):
        if job_id in self.s.jobs:
            del self.s.jobs[job_id]
            return None
        raise LsmError(ErrorNumber.NOT_FOUND_JOB, 'Non-existent job')

    def volumes(self, flags = 0):
        return [e['volume'] for e in self.s.volumes.itervalues()]

    def _get_volume(self, volume_id):
        for v in self.s.volumes.itervalues():
            if v['volume'].id == volume_id:
                return v['volume']
        return None

    def pools(self, flags = 0):
        return [e['pool'] for e in self.s.pools.itervalues()]

    def _volume_accessible(self, access_group_id, volume):
        ag = self.s.group_grants[access_group_id]

        if volume.id in ag:
            return True
        return False

    def _initiators(self, volume_filter = None):
        rc = []
        if len(self.s.access_groups):
            for k, v in self.s.access_groups.items():
                if volume_filter:
                    ag = self._new_access_group(k,v)
                    if self._volume_accessible(ag.id,volume_filter):
                        rc.extend(v['initiators'])
                else:
                    rc.extend(v['initiators'])

        #We can have multiples as the same initiator can be in multiple access
        #groups
        remove_dupes = {}
        for x in rc:
            remove_dupes[x.id] = x

        return list(remove_dupes.values())

    def initiators(self, flags = 0):
        return self._initiators()

    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags = 0):
        assert provisioning is not None
        return self._create_vol(pool, volume_name, size_bytes)

    def volume_delete(self, volume, flags = 0):
        if volume.id in self.s.volumes:
            v = self.s.volumes[volume.id]['volume']
            p = self.s.volumes[volume.id]['pool']
            self._deallocate_from_pool(p.id,v.size_bytes)
            del self.s.volumes[volume.id]

            for (k, v) in self.s.group_grants.items():
                if volume.id in v:
                    del self.s.group_grants[k][volume.id]

            #We only return null or job id.
            return self.__create_job(None)[0]
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME, 'Volume not found')

    def volume_replicate(self, pool, rep_type, volume_src, name, flags = 0):
        assert rep_type is not None

        if pool.id in self.s.pools and volume_src.id in self.s.volumes:
            p = self.s.pools[pool.id]['pool']
            v = self.s.volumes[volume_src.id]['volume']

            return self._create_vol(p, name, v.size_bytes)
        else:
            if pool.id not in self.s.pools:
                raise LsmError(ErrorNumber.NOT_FOUND_POOL, 'Incorrect pool')

            if volume_src.id not in self.s.volumes:
                raise LsmError(ErrorNumber.NOT_FOUND_VOLUME, 'Volume not present')
        return None

    def volume_replicate_range_block_size(self, flags = 0):
        return 512

    def volume_replicate_range(self, rep_type, volume_src, volume_dest,
                               ranges, flags = 0):

        if rep_type not in (Volume.REPLICATE_SNAPSHOT,
                            Volume.REPLICATE_CLONE,
                            Volume.REPLICATE_COPY, Volume.REPLICATE_MIRROR_SYNC):
            raise LsmError(ErrorNumber.UNSUPPORTED_REPLICATION_TYPE,
                            "rep_type invalid")

        if ranges:
            if isinstance(ranges, list):
                for r in ranges:
                    if isinstance(r, BlockRange):
                        #We could do some overlap range testing etc. here.
                        pass
                    else:
                        raise LsmError(ErrorNumber.INVALID_VALUE,
                            "range element not BlockRange")

            else:
                raise LsmError(ErrorNumber.INVALID_VALUE,
                    "ranges not a list")

        #Make sure all the arguments are validated
        if volume_src.id in self.s.volumes and\
           volume_dest.id in self.s.volumes:
            return None
        else:
            if volume_src.id not in self.s.volumes:
                raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                    "volume_src not found")
            if volume_dest.id not in self.s.volumes:
                raise LsmError(ErrorNumber.NOT_FOUND_VOLUME,
                    "volume_dest not found")

    def volume_online(self, volume, flags = 0):
        if volume.id in self.s.volumes:
            return None
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME, 'Volume not present')

    def volume_offline(self, volume, flags = 0):
        if volume.id in self.s.volumes:
            return None
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME, 'Volume not present')

    def volume_resize(self, volume, new_size_bytes, flags = 0):
        if volume.id in self.s.volumes:
            v = self.s.volumes[volume.id]['volume']
            p = self.s.volumes[volume.id]['pool']

            current_size = v.size_bytes
            new_size = self.__block_rounding(new_size_bytes)

            if new_size == current_size:
                raise LsmError(ErrorNumber.SIZE_SAME,
                    'Volume same size')

            if new_size < current_size or\
               p.free_space >= (new_size - current_size):
                p.free_space -= (new_size - current_size)
                v.num_of_blocks = new_size / 512
            else:
                raise LsmError(ErrorNumber.SIZE_INSUFFICIENT_SPACE,
                    'Insufficient space in pool')
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME, 'Volume not found')

        return self.__create_job(v)

    def access_group_grant(self, group, volume, access, flags = 0):
        if group.name not in self.s.access_groups:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                "access group not present")

        if volume.id not in self.s.volumes:
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME, 'Volume not found')

        if group.id not in self.s.group_grants:
            self.s.group_grants[group.id] = {volume.id: access}
        elif volume.id not in self.s.group_grants[group.id]:
            self.s.group_grants[group.id][volume.id] = access
        else:
            raise LsmError(ErrorNumber.IS_MAPPED, 'Existing access present')

    def access_group_revoke(self, group, volume, flags = 0):
        if group.name not in self.s.access_groups:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                "access group not present")

        if volume.id not in self.s.volumes:
            raise LsmError(ErrorNumber.NOT_FOUND_VOLUME, 'Volume not found')

        if group.id in self.s.group_grants and\
           volume.id in self.s.group_grants[group.id]:
            del self.s.group_grants[group.id][volume.id]
        else:
            raise LsmError(ErrorNumber.NO_MAPPING,
                'No volume access to revoke')

    def access_group_list(self, flags = 0):
        rc = []
        for (k, v) in self.s.access_groups.items():
            rc.append(self._new_access_group(k, v))
        return rc

    def _get_access_group(self, id):
        groups = self.access_group_list()
        for g in groups:
            if g.id == id:
                return g
        return None

    def access_group_create(self, name, initiator_id, id_type, system_id,
                            flags = 0):
        if name not in self.s.access_groups:
            self.s.access_groups[name] = {'initiators':
                                              [Initiator(initiator_id, id_type, 'UNA')],
                                          'access': {}}
            return self._new_access_group(name, self.s.access_groups[name])
        else:
            raise LsmError(ErrorNumber.EXISTS_ACCESS_GROUP,
                "Access group with name exists")

    def access_group_del(self, group, flags = 0):
        if group.name in self.s.access_groups:
            del self.s.access_groups[group.name]

            if group.id in self.s.group_grants:
                del self.s.group_grants[group.id]

            return None
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                "access group not found")

    def access_group_add_initiator(self, group, initiator_id, id_type,
                                   flags = 0):
        if group.name in self.s.access_groups:
            self.s.access_groups[group.name]['initiators'].\
            append(Initiator(initiator_id, id_type, 'UNA'))
            return None
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                "access group not found")

    def access_group_del_initiator(self, group, initiator_id, flags = 0):
        if group.name in self.s.access_groups:
            for i in self.s.access_groups[group.name]['initiators']:
                if i.id == initiator_id:
                    self.s.access_groups[group.name]['initiators'].\
                    remove(i)
                    return None

            raise LsmError(ErrorNumber.INITIATOR_NOT_IN_ACCESS_GROUP,
                "initiator not found")
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                "access group not found")

    def volumes_accessible_by_access_group(self, group, flags = 0):
        rc = []
        if group.name in self.s.access_groups:
            if group.id in self.s.group_grants:
                for (k, v) in self.s.group_grants[group.id].items():
                    rc.append(self._get_volume(k))

            return rc
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                "access group not found")

    def access_groups_granted_to_volume(self, volume, flags = 0):
        rc = []

        for (k, v) in self.s.group_grants.items():
            if volume.id in self.s.group_grants[k]:
                rc.append(self._get_access_group(k))
        return rc

    def iscsi_chap_auth_inbound( self, initiator, user, password, flags = 0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def initiator_grant(self, initiator_id, initiator_type, volume, access, flags = 0):
        name = initiator_id + volume.id
        group = None

        try:
            group = self.access_group_create(name, initiator_id, initiator_type,
                                                volume.system_id)
            result = self.access_group_grant(group,volume,access)

        except Exception as e:
            if group:
                self.access_group_del(group)
            raise e

        return result

    def initiator_revoke(self, initiator, volume, flags = 0):
        name = initiator.id + volume.id

        if any(x.id for x in self.initiators()):
            if volume.id in self.s.volumes:
                ag = self._new_access_group(name, self.s.access_groups[name])

                if ag:
                    self.access_group_del(ag)
                else:
                    raise LsmError(ErrorNumber.NO_MAPPING, "No mapping of initiator "
                                                           "and volume")
            else:
                raise LsmError(ErrorNumber.NOT_FOUND_VOLUME, "volume not found")
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_INITIATOR, "Initiator not found")

        return None

    def volumes_accessible_by_initiator(self, initiator, flags = 0):
        rc = []
        volumes = {}
        for k, v in self.s.access_groups.items():
            initiators = v['initiators']
            if any(x.id for x in initiators):
                for (ag_id, volume_mappings) in self.s.group_grants.items():
                    for volume_id in volume_mappings.keys():
                        volumes[volume_id] = None

        for vol_id in volumes.keys():
            rc.append(self._get_volume(vol_id))

        return rc

    def initiators_granted_to_volume(self, volume, flags = 0):
        return self._initiators(volume)

    def volume_child_dependency(self, volume, flags = 0):
        return False

    def volume_child_dependency_rm(self, volume, flags = 0):
        return None

    def fs(self, flags = 0):
        return [e['fs'] for e in self.s.fs.itervalues()]

    def fs_delete(self, fs, flags = 0):
        if fs.id in self.s.fs:
            f = self.s.fs[fs.id]['fs']
            p = self.s.fs[fs.id]['pool']

            self._deallocate_from_pool(p.id, f.total_space)
            del self.s.fs[fs.id]

            #TODO: Check for exports and remove them.

            return self.__create_job(None)[0]
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')

    def fs_resize(self, fs, new_size_bytes, flags = 0):
        if fs.id in self.s.fs:
            f = self.s.fs[fs.id]['fs']
            p = self.s.fs[fs.id]['pool']

            #TODO Check to make sure we have enough space before proceeding
            self._deallocate_from_pool(p.id, f.total_space)
            f.total_space = self._allocate_from_pool(p.id, new_size_bytes)
            f.free_space = f.total_space
            return self.__create_job(f)
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')

    def fs_create(self, pool, name, size_bytes, flags = 0):
        return self._create_fs(pool, name, size_bytes)

    def fs_clone(self, src_fs, dest_fs_name, snapshot=None, flags= 0):
        #TODO If snapshot is not None, then check for existence.

        if src_fs.id in self.s.fs:
            f = self.s.fs[src_fs.id]['fs']
            p = self.s.fs[src_fs.id]['pool']
            return self._create_fs(p, dest_fs_name, f.total_space)
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')

    def file_clone(self, fs, src_file_name, dest_file_name, snapshot=None, flags=0):
        #TODO If snapshot is not None, then check for existence.
        if fs.id in self.s.fs:
            if src_file_name is not None and dest_file_name is not None:
                return self.__create_job(None)[0]
            else:
                raise LsmError(ErrorNumber.INVALID_VALUE,
                                "Invalid src/destination file names")
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')

    def fs_snapshots(self, fs, flags = 0):
        if fs.id in self.s.fs:
            rc =  [e for e in self.s.fs[fs.id]['ss'].itervalues()]
            return rc
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')

    def fs_snapshot_create(self, fs, snapshot_name, files, flags = 0):
        self._check_sl(files)
        if fs.id in self.s.fs:
            for e in self.s.fs[fs.id]['ss'].itervalues():
                if e.name == snapshot_name:
                    raise LsmError(ErrorNumber.EXISTS_NAME,
                                    'Snapshot name exists')

            s = Snapshot(md5(snapshot_name), snapshot_name, time.time())
            self.s.fs[fs.id]['ss'][s.id] = s
            return self.__create_job(s)
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')

    def fs_snapshot_delete(self, fs, snapshot, flags = 0):
        if fs.id in self.s.fs:
            if snapshot.id in self.s.fs[fs.id]['ss']:
                del self.s.fs[fs.id]['ss'][snapshot.id]
                return self.__create_job(None)[0]
            else:
                raise LsmError(ErrorNumber.NOT_FOUND_SS, "Snapshot not found")
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')

    def fs_snapshot_revert(self, fs, snapshot, files, restore_files,
                           all_files=False, flags = 0):

        self._check_sl(files)
        self._check_sl(files)

        if fs.id in self.s.fs:
            if snapshot.id in self.s.fs[fs.id]['ss']:
                return self.__create_job(None)[0]
            else:
                raise LsmError(ErrorNumber.NOT_FOUND_SS, "Snapshot not found")
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')

    def fs_child_dependency(self, fs, files, flags = 0):
        self._check_sl(files)
        if fs.id in self.s.fs:
            return False
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')

    def fs_child_dependency_rm(self, fs, files, flags = 0):
        self._check_sl(files)
        if fs.id in self.s.fs:
            return self.__create_job(None)[0]
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')

    def export_auth(self, flags = 0):
        return ["simple"]

    def exports(self, flags = 0):
        rc = []
        for fs in self.s.fs.itervalues():
            for exp in fs['exports'].values():
                rc.append(exp)
        return rc

    def export_fs(self, fs_id, export_path, root_list, rw_list, ro_list,
                  anon_uid, anon_gid, auth_type,options, flags = 0):

        if fs_id in self.s.fs:
            export_id = md5(export_path)

            export = NfsExport(export_id, fs_id, export_path, auth_type,
                            root_list, rw_list, ro_list, anon_uid, anon_gid,
                            options)

            self.s.fs[fs_id]['exports'][export_id] = export
            return export
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')

    def export_remove(self, export, flags = 0):
        fs_id = export.fs_id

        if fs_id in self.s.fs:
            if export.id in self.s.fs[fs_id]['exports']:
                del self.s.fs[fs_id]['exports'][export.id]
            else:
                raise LsmError(ErrorNumber.FS_NOT_EXPORTED, "FS not exported")
        else:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, 'Filesystem not found')