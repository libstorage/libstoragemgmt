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

from iplugin import IStorageAreaNetwork
from common import LsmError, ErrorNumber, JobStatus, md5, uri_parse
import random
from data import Pool, Initiator, Volume, BlockRange, System, AccessGroup
import time
import pickle

SIM_DATA_FILE = '/tmp/lsm_sim_data'
duration = os.getenv("LSM_SIM_TIME", 8)

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

    def __init__(self, volume):
        self.status = JobStatus.INPROGRESS
        self.percent = 0
        self.__volume = volume
        self.start = time.time()
        self.duration = float(random.randint(0, int(duration)))

    def progress(self):
        """
        Returns a tuple (status, percent, volume)
        """
        self.__calc_progress()
        return self.status, self.percent, self.volume

    @property
    def volume(self):
        if self.percent >= 100:
            return self.__volume
        return None

    @volume.setter
    def volume(self, value):
        self.__volume = value


class SimState(object):

    def __init__(self):
        self.sys_info = System('sim-01', 'LSM simulated storage plug-in')
        p1 = Pool('POO1', 'Pool 1', 2 ** 64, 2 ** 64, self.sys_info.id)
        p2 = Pool('POO2', 'Pool 2', 2 ** 64, 2 ** 64, self.sys_info.id)
        p3 = Pool('POO3', 'lsm_test_aggr', 2 ** 64, 2 ** 64, self.sys_info.id)

        pm1 = {'pool': p1, 'volumes': {}}
        pm2 = {'pool': p2, 'volumes': {}}
        pm3 = {'pool': p3, 'volumes': {}}

        self.pools = {p1.id: pm1, p2.id: pm2, p3.id: pm3}
        self.volumes = {}
        self.vol_num = 1
        self.access_groups = {}

        self.tmo = 30000
        self.jobs = {}
        self.job_num = 1

        #These express relationships between initiators and volumes.  This
        #is done because if you delete either part of the relationship
        #you need to delete the association between them.  Holding this stuff
        #in a db would be easier :-)
        self.group_grants = {}      # {access group id : {volume id: access }}


class StorageSimulator(IStorageAreaNetwork):
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

    def __create_job(self, volume):
        if True:
        #if random.randint(0,5) == 1:
            self.s.job_num += 1
            job = "JOB_" + str(self.s.job_num)
            self.s.jobs[job] = SimJob(volume)
            return job, None
        else:
            return None, volume

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

    def __init__(self):

        self.file = SIM_DATA_FILE
        self._load_state()

    def _new_access_group(self, name, h):
        return AccessGroup(md5(name), name,
            [i.id for i in h['initiators']], self.s.sys_info.id)

    def _create_vol(self, pool, name, size_bytes):
        p = self.s.pools[pool.id]['pool']

        rounded_size = self.__block_rounding(size_bytes)

        if p.free_space >= rounded_size:
            nv = Volume('Vol' + str(self.s.vol_num), name,
                StorageSimulator.__randomVpd(), 512,
                (rounded_size / 512), Volume.STATUS_OK, self.s.sys_info.id)
            self.s.volumes[nv.id] = {'pool': pool, 'volume': nv}
            p.free_space -= rounded_size
            self.s.vol_num += 1
            return self.__create_job(nv)
        else:
            raise LsmError(ErrorNumber.INSUFFICIENT_SPACE,
                'Insufficient space in pool')

    def startup(self, uri, password, timeout):
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

    def set_time_out(self, ms):
        self.tmo = ms
        return None

    def get_time_out(self):
        return self.tmo

    def shutdown(self):
        self._save()

    def systems(self):
        return [ self.s.sys_info ]

    def job_status(self, job_id):
        if job_id in self.s.jobs:
            return self.s.jobs[job_id].progress()
        raise LsmError(ErrorNumber.INVALID_JOB, 'Non-existent job')

    def job_free(self, job_id):
        if job_id in self.s.jobs:
            del self.s.jobs[job_id]
            return None
        raise LsmError(ErrorNumber.INVALID_JOB, 'Non-existent job')

    def volumes(self):
        return [e['volume'] for e in self.s.volumes.itervalues()]

    def _get_volume(self, volume_id):
        for v in self.s.volumes.itervalues():
            if v['volume'].id == volume_id:
                return v['volume']
        return None

    def pools(self):
        return [e['pool'] for e in self.s.pools.itervalues()]

    def initiators(self):
        rc = []
        if len(self.s.access_groups):
            for v in self.s.access_groups.values():
                rc.extend(v['initiators'])

        return rc

    def volume_create(self, pool, volume_name, size_bytes, provisioning):
        assert provisioning is not None
        return self._create_vol(pool, volume_name, size_bytes)

    def volume_delete(self, volume):
        if volume.id in self.s.volumes:
            v = self.s.volumes[volume.id]['volume']
            p = self.s.volumes[volume.id]['pool']
            p.free_space += v.size_bytes
            del self.s.volumes[volume.id]

            for (k, v) in self.s.group_grants.items():
                if volume.id in v:
                    del self.s.group_grants[k][volume.id]

            #We only return null or job id.
            return self.__create_job(None)[0]
        else:
            raise LsmError(ErrorNumber.INVALID_VOL, 'Volume not found')

    def volume_replicate(self, pool, rep_type, volume_src, name):
        assert rep_type is not None

        if pool.id in self.s.pools and volume_src.id in self.s.volumes:
            p = self.s.pools[pool.id]['pool']
            v = self.s.volumes[volume_src.id]['volume']

            return self._create_vol(p, name, v.size_bytes)
        else:
            if pool.id not in self.s.pools:
                raise LsmError(ErrorNumber.INVALID_POOL, 'Incorrect pool')

            if volume_src.id not in self.s.volumes:
                raise LsmError(ErrorNumber.INVALID_VOL, 'Volume not present')
        return None

    def volume_replicate_range_block_size(self):
        return 512

    def volume_replicate_range(self, rep_type, volume_src, volume_dest,
                               ranges):

        if rep_type not in (Volume.REPLICATE_SNAPSHOT,
                            Volume.REPLICATE_CLONE,
                            Volume.REPLICATE_COPY, Volume.REPLICATE_MIRROR):
            raise LsmError(ErrorNumber.INVALID_ARGUMENT, "rep_type invalid")

        if ranges:
            if isinstance(ranges, list):
                for r in ranges:
                    if isinstance(r, BlockRange):
                        #We could do some overlap range testing etc. here.
                        pass
                    else:
                        raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                            "range element not BlockRange")

            else:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                    "ranges not a list")

        #Make sure all the arguments are validated
        if volume_src.id in self.s.volumes and\
           volume_dest.id in self.s.volumes:
            return None
        else:
            if volume_src.id not in self.s.volumes:
                raise LsmError(ErrorNumber.INVALID_VOLUME,
                    "volume_src not found")
            if volume_dest.id not in self.s.volumes:
                raise LsmError(ErrorNumber.INVALID_VOLUME,
                    "volume_dest not found")

    def volume_online(self, volume):
        if volume.id in self.s.volumes:
            return None
        else:
            raise LsmError(ErrorNumber.INVALID_VOL, 'Volume not present')

    def volume_offline(self, volume):
        if volume.id in self.s.volumes:
            return None
        else:
            raise LsmError(ErrorNumber.INVALID_VOL, 'Volume not present')

    def volume_resize(self, volume, new_size_bytes):
        if volume.id in self.s.volumes:
            v = self.s.volumes[volume.id]['volume']
            p = self.s.volumes[volume.id]['pool']

            current_size = v.size_bytes
            new_size = self.__block_rounding(new_size_bytes)

            if new_size == current_size:
                raise LsmError(ErrorNumber.VOLUME_SAME_SIZE,
                    'Volume same size')

            if new_size < current_size or\
               p.free_space >= (new_size - current_size):
                p.free_space -= (new_size - current_size)
                v.num_of_blocks = new_size / 512
            else:
                raise LsmError(ErrorNumber.INSUFFICIENT_SPACE,
                    'Insufficient space in pool')
        else:
            raise LsmError(ErrorNumber.INVALID_VOL, 'Volume not found')

        return self.__create_job(v)

    def access_group_grant(self, group, volume, access):
        if group.name not in self.s.access_groups:
            raise LsmError(ErrorNumber.ACCESS_GROUP_NOT_FOUND,
                "access group not present")

        if volume.id not in self.s.volumes:
            raise LsmError(ErrorNumber.INVALID_VOL, 'Volume not found')

        if group.id not in self.s.group_grants:
            self.s.group_grants[group.id] = {volume.id: access}
        elif volume.id not in self.s.group_grants[group.id]:
            self.s.group_grants[group.id][volume.id] = access
        else:
            raise LsmError(ErrorNumber.IS_MAPPED, 'Existing access present')

    def access_group_revoke(self, group, volume):
        if group.name not in self.s.access_groups:
            raise LsmError(ErrorNumber.ACCESS_GROUP_NOT_FOUND,
                "access group not present")

        if volume.id not in self.s.volumes:
            raise LsmError(ErrorNumber.INVALID_VOL, 'Volume not found')

        if group.id in self.s.group_grants and\
           volume.id in self.s.group_grants[group.id]:
            del self.s.group_grants[group.id][volume.id]
        else:
            raise LsmError(ErrorNumber.NO_MAPPING,
                'No volume access to revoke')

    def access_group_list(self):
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

    def access_group_create(self, name, initiator_id, id_type, system_id):
        if name not in self.s.access_groups:
            self.s.access_groups[name] = {'initiators':
                                              [Initiator(initiator_id, id_type, 'UNA')],
                                          'access': {}}
            return self._new_access_group(name, self.s.access_groups[name])
        else:
            raise LsmError(ErrorNumber.ACCESS_GROUP_EXISTS,
                "Access group with name exists")

    def access_group_del(self, group):
        if group.name in self.s.access_groups:
            del self.s.access_groups[group.name]

            if group.id in self.s.group_grants:
                del self.s.group_grants[group.id]

            return None
        else:
            raise LsmError(ErrorNumber.ACCESS_GROUP_NOT_FOUND,
                "access group not found")

    def access_group_add_initiator(self, group, initiator_id, id_type):
        if group.name in self.s.access_groups:
            self.s.access_groups[group.name]['initiators'].\
            append(Initiator(initiator_id, id_type, 'UNA'))
            return None
        else:
            raise LsmError(ErrorNumber.ACCESS_GROUP_NOT_FOUND,
                "access group not found")

    def access_group_del_initiator(self, group, initiator):
        if group.name in self.s.access_groups:
            for i in self.s.access_groups[group.name]['initiators']:
                if i.id == initiator.id:
                    self.s.access_groups[group.name]['initiators'].\
                    remove(i)
                    return None

            raise LsmError(ErrorNumber.INITIATOR_NOT_IN_ACCESS_GROUP,
                "initiator not found")
        else:
            raise LsmError(ErrorNumber.ACCESS_GROUP_NOT_FOUND,
                "access group not found")

    def volumes_accessible_by_access_group(self, group):
        rc = []
        if group.name in self.s.access_groups:
            if group.id in self.s.group_grants:
                for (k, v) in self.s.group_grants[group.id].items():
                    rc.append(self._get_volume(k))

            return rc
        else:
            raise LsmError(ErrorNumber.ACCESS_GROUP_NOT_FOUND,
                "access group not found")

    def access_groups_granted_to_volume(self, volume):
        rc = []

        for (k, v) in self.s.group_grants.items():
            if volume.id in self.s.group_grants[k]:
                rc.append(self._get_access_group(k))
        return rc

    def volume_child_dependency(self, volume):
        return False

    def volume_child_dependency_rm(self, volume):
        return None