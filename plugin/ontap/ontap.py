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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301
# USA
#
# Author: tasleson
#         Gris Ge <fge@redhat.com>

import os
import traceback
import urlparse
import sys
import copy

import na
from lsm import (Volume, FileSystem, FsSnapshot, NfsExport,
                 AccessGroup, System, Capabilities, Disk, Pool,
                 IStorageAreaNetwork, INfs, LsmError, ErrorNumber, JobStatus,
                 md5, error, VERSION, common_urllib2_error_handler,
                 search_property, TargetPort)

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
    na.Filer.EINVALID_ISCSI_NAME: ErrorNumber.INVALID_INIT,
    na.Filer.ETIMEOUT: ErrorNumber.PLUGIN_TIMEOUT,
    na.Filer.EUNKNOWN: ErrorNumber.PLUGIN_ERROR
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
        except LsmError:
            raise
        except na.FilerError as oe:
            error_code, error_msg = error_map(oe)
            raise LsmError(error_code, error_msg)
        except Exception as e:
            common_urllib2_error_handler(e)

    return na_wrapper


_INIT_TYPE_CONV = {
    'iscsi': AccessGroup.INIT_TYPE_ISCSI_IQN,
    'fcp': AccessGroup.INIT_TYPE_WWPN,
    'mixed': AccessGroup.INIT_TYPE_ISCSI_WWPN_MIXED,
}


def _na_init_type_to_lsm(na_ag):
    if 'initiator-group-type' in na_ag:
        if na_ag['initiator-group-type'] in _INIT_TYPE_CONV.keys():
            return _INIT_TYPE_CONV[na_ag['initiator-group-type']]
        else:
            return AccessGroup.INIT_TYPE_OTHER
    return AccessGroup.INIT_TYPE_UNKNOWN


class Ontap(IStorageAreaNetwork, INfs):
    TMO_CONV = 1000.0

    (LSM_VOL_PREFIX, LSM_INIT_PREFIX) = ('lsm_lun_container', 'lsm_init_')

    (SS_JOB, SPLIT_JOB) = ('ontap-ss-file-restore', 'ontap-clone-split')

    VOLUME_PREFIX = '/vol'

    NA_AGGR_STATUS_TO_LSM = {
        'creating': Pool.STATUS_STARTING,
        'destroying': Pool.STATUS_DESTROYING,
        'failed': Pool.STATUS_ERROR,
        'frozen': Pool.STATUS_OTHER,
        'inconsistent': Pool.STATUS_DEGRADED,
        'iron_restricted': Pool.STATUS_OTHER,
        'mounting': Pool.STATUS_STARTING,
        'offline': Pool.STATUS_STOPPED,
        'online': Pool.STATUS_OK,
        'partial': Pool.STATUS_ERROR,
        'quiesced': Pool.STATUS_DORMANT,
        'quiescing': Pool.STATUS_DORMANT,
        'restricted': Pool.STATUS_DORMANT,
        'reverted': Pool.STATUS_OTHER,
        'unknown': Pool.STATUS_UNKNOWN,
        'unmounted': Pool.STATUS_STOPPED,
        'unmounting': Pool.STATUS_STOPPING,
    }

    # Use information in https://communities.netapp.com/thread/9052
    # (Also found similar documents in ONTAP 7.3 command Manual Page
    # Reference, Volume 1, PDF Page 18.
    NA_AGGR_STATUS_TO_LSM_STATUS_INFO = {
        'frozen': 'aggregate is temporarily not accepting requests ' +
                  'and is queueing the requests.',
        'iron_restricted': 'aggregate access is restricted due to ' +
                           'running wafl_iron.',
        'partial': 'all the disks in the aggregate are not available.',
        'quiesced': 'aggregate is temporaily not accepting requests and ' +
                    'returns an error for the request.',
        'quiescing': 'aggregate is in the process on quiescing and not ' +
                     'accessible.',
        'restricted': 'aggregate is offline for WAFL',
        'reverted': 'final state of revert before shutting down ONTAP ' +
                    'and is not accessible.',
        'unmounted': 'aggregate is unmounted and is not accessible.',
        'unmounting': 'aggregate is in the process of unmounting and is' +
                      'not accessible.'
    }

    NA_VOL_STATUS_TO_LSM = {
        'offline': Pool.STATUS_STOPPED,
        'online': Pool.STATUS_OK,
        'restricted': Pool.STATUS_OTHER,
        'unknown': Pool.STATUS_UNKNOWN,
        'creating': Pool.STATUS_STOPPING,
        'failed': Pool.STATUS_ERROR,
        'partial': Pool.STATUS_ERROR,

    }

    NA_VOL_STATUS_TO_LSM_STATUS_INFO = {
        'partial': 'all the disks in the volume are not available.',
        'restricted': 'volume is restricted to protocol accesses',
    }

    def __init__(self):
        self.f = None
        self.sys_info = None

    @handle_ontap_errors
    def plugin_register(self, uri, password, timeout, flags=0):
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
                               System.STATUS_OK, '')
        return self.f.validate()

    def time_out_set(self, ms, flags=0):
        self.f.timeout = int(ms / Ontap.TMO_CONV)

    def time_out_get(self, flags=0):
        return int(self.f.timeout * Ontap.TMO_CONV)

    def plugin_unregister(self, flags=0):
        pass

    @staticmethod
    def _create_vpd(sn):
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
                      Ontap._create_vpd(l['serial-number']), block_size,
                      num_blocks, Volume.STATUS_OK, self.sys_info.id,
                      l['aggr'])

    def _vol(self, v, pools=None):
        pool_name = v['containing-aggregate']

        if pools is None:
            pools = self.pools()

        for p in pools:
            if p.name == pool_name:
                return FileSystem(v['uuid'], v['name'], int(v['size-total']),
                                  int(v['size-available']), p.id,
                                  self.sys_info.id)

    @staticmethod
    def _ss(s):
        #If we use the newer API we can use the uuid instead of this fake
        #md5 one
        return FsSnapshot(md5(s['name'] + s['access-time']), s['name'],
                          s['access-time'])

    @staticmethod
    def _disk_type(netapp_disk_type):
        conv = {'ATA': Disk.DISK_TYPE_ATA, 'BSAS': Disk.DISK_TYPE_SAS,
                'EATA': Disk.DISK_TYPE_ATA, 'FCAL': Disk.DISK_TYPE_FC,
                'FSAS': Disk.DISK_TYPE_SAS, 'LUN': Disk.DISK_TYPE_LUN,
                'SAS': Disk.DISK_TYPE_SAS, 'SATA': Disk.DISK_TYPE_SATA,
                'SCSI': Disk.DISK_TYPE_SCSI, 'SSD': Disk.DISK_TYPE_SSD,
                'XATA': Disk.DISK_TYPE_ATA, 'XSAS': Disk.DISK_TYPE_SAS,
                'unknown': Disk.DISK_TYPE_UNKNOWN}

        if netapp_disk_type in conv:
            return conv[netapp_disk_type]
        return Disk.DISK_TYPE_UNKNOWN

    @staticmethod
    def _disk_id(na_disk):
        """
        The md5sum of na_disk['disk-uid']
        """
        return md5(na_disk['disk-uid'])

    @staticmethod
    def _status_of_na_disk(na_disk):
        """
        Retrieve Disk.status from NetApp ONTAP disk-detail-info.
        TODO: API document does not provide enough explaination.
              Need lab test to verify.
        """
        status = Disk.STATUS_OK

        if 'raid-state' in na_disk:
            rs = na_disk['raid-state']
            if rs == "broken":
                status = Disk.STATUS_ERROR
            elif rs == "unknown":
                status = Disk.STATUS_UNKNOWN
            elif rs == 'zeroing':
                status = Disk.STATUS_INITIALIZING
            elif rs == 'reconstructing':
                # "reconstructing' should be a pool status, not disk status.
                # disk under reconstructing should be considered as OK.
                status = Disk.STATUS_OK

        if 'is-prefailed' in na_disk and na_disk['is-prefailed'] == 'true':
            status = Disk.STATUS_PREDICTIVE_FAILURE

        if 'is-offline' in na_disk and na_disk['is-offline'] == 'true':
            status = Disk.STATUS_ERROR
        return status

    @staticmethod
    def _status_info_of_na_disk(na_disk):
        """
        Provide more explainaion in Disk.status_info.
        TODO: API document does not provide enough explaination.
              Need lab test to verify.
        """
        status_info = ''
        if 'raid-state' in na_disk:
            rs = na_disk['raid-state']
            if rs == 'reconstructing':
                status_info = "Reconstruction progress: %s%%" %\
                    str(na_disk['reconstruction-percent'])
            if 'broken-details' in na_disk:
                status_info = na_disk['broken-details']
        return status_info

    def _disk(self, d, flag):
        status = Ontap._status_of_na_disk(d)
        return Disk(self._disk_id(d), d['name'],
                    Ontap._disk_type(d['disk-type']),
                    int(d['bytes-per-sector']), int(d['physical-blocks']),
                    status, self.sys_info.id)

    @handle_ontap_errors
    def volumes(self, search_key=None, search_value=None, flags=0):
        luns = self.f.luns_get_all()
        return search_property(
            [self._lun(l) for l in luns], search_key, search_value)

    @staticmethod
    def _pool_id(na_xxx):
        """
        Return na_aggr['uuid'] or na_vol['uuid']
        """
        return na_xxx['uuid']

    @staticmethod
    def _raid_type_of_na_aggr(na_aggr):
        na_raid_statuses = na_aggr['raid-status'].split(',')
        if 'raid0' in na_raid_statuses:
            return Pool.RAID_TYPE_RAID0
        if 'raid4' in na_raid_statuses:
            return Pool.RAID_TYPE_RAID4
        if 'raid_dp' in na_raid_statuses:
            return Pool.RAID_TYPE_RAID6
        if 'mixed_raid_type' in na_raid_statuses:
            return Pool.RAID_TYPE_MIXED
        return Pool.RAID_TYPE_UNKNOWN

    @staticmethod
    def _status_of_na_aggr(na_aggr):
        """
        Use aggr-info['state'] for Pool.status
        """
        na_aggr_state = na_aggr['state']
        if na_aggr_state in Ontap.NA_AGGR_STATUS_TO_LSM.keys():
            return Ontap.NA_AGGR_STATUS_TO_LSM[na_aggr_state]
        return Pool.STATUS_UNKNOWN

    @staticmethod
    def _status_info_of_na_aggr(na_aggr):
        """
        TODO: provides more information on disk failed.
        """
        na_aggr_state = na_aggr['state']
        if na_aggr_state in Ontap.NA_AGGR_STATUS_TO_LSM_STATUS_INFO.keys():
            return Ontap.NA_AGGR_STATUS_TO_LSM_STATUS_INFO[na_aggr_state]
        return ''

    def _pool_from_na_aggr(self, na_aggr, na_disks, flags):
        pool_id = self._pool_id(na_aggr)
        pool_name = na_aggr['name']
        total_space = int(na_aggr['size-total'])
        free_space = int(na_aggr['size-available'])
        system_id = self.sys_info.id
        status = self._status_of_na_aggr(na_aggr)

        element_type = (Pool.ELEMENT_TYPE_POOL | Pool.ELEMENT_TYPE_FS |
                        Pool.ELEMENT_TYPE_VOLUME)

        # The system aggregate can be used to create both FS and volumes, but
        # you can't take it offline or delete it.
        if pool_name == 'aggr0':
            element_type = element_type | Pool.ELEMENT_TYPE_SYS_RESERVED

        return Pool(pool_id, pool_name, element_type, total_space, free_space,
                    status, self._status_info_of_na_aggr(na_aggr), system_id)

    @staticmethod
    def _status_of_na_vol(na_vol):
        na_vol_state = na_vol['state']
        if na_vol_state in Ontap.NA_VOL_STATUS_TO_LSM.keys():
            return Ontap.NA_VOL_STATUS_TO_LSM[na_vol_state]
        return Pool.STATUS_UNKNOWN

    @staticmethod
    def _status_info_of_na_vol(na_vol):
        na_vol_state = na_vol['state']
        if na_vol_state in Ontap.NA_VOL_STATUS_TO_LSM_STATUS_INFO.keys():
            return Ontap.NA_VOL_STATUS_TO_LSM_STATUS_INFO[na_vol_state]
        return ''

    @staticmethod
    def _pool_name_of_na_vol(na_vol):
        return "%s/%s" % (Ontap.VOLUME_PREFIX, na_vol['name'])

    @staticmethod
    def _na_vol_name_of_pool(pool):
        return pool.name.split('/')[-1]

    def _pool_from_na_vol(self, na_vol, na_aggrs, flags):
        pool_id = self._pool_id(na_vol)
        pool_name = self._pool_name_of_na_vol(na_vol)
        total_space = int(na_vol['size-total'])
        free_space = int(na_vol['size-available'])
        system_id = self.sys_info.id
        status = self._status_of_na_vol(na_vol)

        return Pool(pool_id, pool_name, Pool.ELEMENT_TYPE_VOLUME,
                    total_space, free_space, status,
                    self._status_info_of_na_vol(na_vol), system_id)

    @handle_ontap_errors
    def capabilities(self, system, flags=0):
        cap = Capabilities()
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
        cap.set(Capabilities.VOLUME_MASK)
        cap.set(Capabilities.VOLUME_UNMASK)
        cap.set(Capabilities.ACCESS_GROUPS)
        cap.set(Capabilities.ACCESS_GROUP_CREATE_WWPN)
        cap.set(Capabilities.ACCESS_GROUP_CREATE_ISCSI_IQN)
        cap.set(Capabilities.ACCESS_GROUP_DELETE)
        cap.set(Capabilities.ACCESS_GROUP_INITIATOR_ADD_WWPN)
        cap.set(Capabilities.ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN)
        cap.set(Capabilities.ACCESS_GROUP_INITIATOR_DELETE)
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
        cap.set(Capabilities.FS_SNAPSHOT_RESTORE)
        cap.set(Capabilities.FS_CHILD_DEPENDENCY)
        cap.set(Capabilities.FS_CHILD_DEPENDENCY_RM)
        cap.set(Capabilities.EXPORT_AUTH)
        cap.set(Capabilities.EXPORTS)
        cap.set(Capabilities.EXPORT_FS)
        cap.set(Capabilities.EXPORT_REMOVE)
        cap.set(Capabilities.EXPORT_CUSTOM_PATH)
        cap.set(Capabilities.TARGET_PORTS)
        return cap

    @handle_ontap_errors
    def plugin_info(self, flags=0):
        return "NetApp Filer support", VERSION

    @handle_ontap_errors
    def disks(self, search_key=None, search_value=None, flags=0):
        disks = self.f.disks()
        return search_property(
            [self._disk(d, flags) for d in disks], search_key, search_value)

    @handle_ontap_errors
    def pools(self, search_key=None, search_value=None, flags=0):
        pools = []
        na_aggrs = self.f.aggregates()
        na_disks = []
        # We do extra flags check in order to save self.f.disks() calls
        # in case we have multiple aggregates.
        for na_aggr in na_aggrs:
            pools.extend([self._pool_from_na_aggr(na_aggr, na_disks, flags)])
        na_vols = self.f.volumes()
        for na_vol in na_vols:
            pools.extend([self._pool_from_na_vol(na_vol, na_aggrs, flags)])
        return search_property(pools, search_key, search_value)

    @handle_ontap_errors
    def systems(self, flags=0):
        return [self.sys_info]

    def _get_volume(self, vol_name, pool_id):
        return self._lun(self.f.luns_get_specific(pool_id, vol_name, None)[0])

    def _na_resize_recovery(self, vol, size_diff_kb):
        """
        We are have entered a failing state and have been asked to
        put a NetApp volume back.  To make sure this occurs we will
        interrogate the timeout value and increase if it is quite low or
        double if it's reasonable to try and get this operation to happen
        """

        current_tmo = self.time_out_get()
        try:
            #If the user selects a short timeout we may not be able
            #to restore the array back, so lets give the array
            #a chance to recover.
            if current_tmo < 30000:
                self.time_out_set(60000)
            else:
                self.time_out_set(current_tmo * 2)

            self.f.volume_resize(vol, size_diff_kb)
        except:
            #Anything happens here we are only going to report the
            #primary error through the API and log the secondary
            #error to syslog.
            error("Exception on trying to restore NA volume size:"
                  + traceback.format_exc())
        finally:
            #Restore timeout.
            self.time_out_set(current_tmo)

    def _na_volume_resize_restore(self, vol, size_diff):
        try:
            self.f.volume_resize(vol, size_diff)
        except na.FilerError as fe:
            if fe.errno == na.Filer.ETIMEOUT:
                exc_info = sys.exc_info()
                self._na_resize_recovery(vol, -size_diff)
                raise exc_info[0], exc_info[1], exc_info[2]

    @handle_ontap_errors
    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags=0):
        if provisioning != Volume.PROVISION_DEFAULT:
            raise LsmError(ErrorNumber.UNSUPPORTED_PROVISIONING,
                           "Unsupported provisioning")
        na_aggrs = self.f.aggregates()
        na_aggr_names = [x['name'] for x in na_aggrs]
        na_vol_name = ''
        if pool.name not in na_aggr_names:
            # user defined a NetApp volume.
            na_vol_name = self._na_vol_name_of_pool(pool)
        else:
            # user defined a NetApp Aggregate
            v = self.f.volume_names()
            na_vol_name = Ontap.LSM_VOL_PREFIX + '_' + pool.name
            if na_vol_name not in v:
                self.f.volume_create(pool.name, na_vol_name, size_bytes)

        #re-size volume to accommodate new logical unit
        flag_resize = False
        if size_bytes > pool.free_space:
            flag_resize = True
            self._na_volume_resize_restore(
                na_vol_name,
                Ontap._size_kb_padded(size_bytes))

        lun_name = self.f.lun_build_name(na_vol_name, volume_name)

        try:
            self.f.lun_create(lun_name, size_bytes)
        except Exception as e:
            exception_info = sys.exc_info()

            if flag_resize:
                self._na_resize_recovery(na_vol_name,
                                         -Ontap._size_kb_padded(size_bytes))
            raise exception_info[1], None, exception_info[2]

        #Get the information about the newly created LUN
        return None, self._get_volume(lun_name, pool.id)

    @staticmethod
    def _vol_to_na_volume_name(volume):
        return os.path.dirname(volume.name)[5:]

    @handle_ontap_errors
    def volume_delete(self, volume, flags=0):
        vol = Ontap._vol_to_na_volume_name(volume)

        luns = self.f.luns_get_specific(aggr=volume.pool_id,
                                        na_volume_name=vol)

        if len(luns) == 1 and Ontap.LSM_VOL_PREFIX in vol:
            self.f.volume_delete(vol)
        else:
            self.f.lun_delete(volume.name)

        return None

    @staticmethod
    def _size_kb_padded(size_bytes):
        return int((size_bytes / 1024) * 1.3)

    @handle_ontap_errors
    def volume_resize(self, volume, new_size_bytes, flags=0):
        na_vol = Ontap._vol_to_na_volume_name(volume)
        diff = new_size_bytes - volume.size_bytes

        #Convert to KB and pad for snapshots
        diff = Ontap._size_kb_padded(diff)

        #If the new size is > than old -> re-size volume then lun
        #If the new size is < than old -> re-size lun then volume
        if diff > 0:
            #if this raises an exception we are fine, except when we timeout
            #and the operation actually completes!  Anytime we get an
            #exception we will try to put the volume back to original size.
            self._na_volume_resize_restore(na_vol, diff)

            try:
                #if this raises an exception we need to restore the volume
                self.f.lun_resize(volume.name, new_size_bytes)
            except Exception as e:
                exception_info = sys.exc_info()

                #Put the volume back to previous size
                self._na_resize_recovery(na_vol, -diff)
                raise exception_info[1], None, exception_info[2]
        else:
            self.f.lun_resize(volume.name, new_size_bytes)
            self.f.volume_resize(na_vol, diff)

        return None, self._get_volume(volume.name, volume.pool_id)

    def _volume_on_aggr(self, pool, volume):
        search = Ontap._vol_to_na_volume_name(volume)
        contained_volumes = self.f.aggregate_volume_names(pool.name)
        return search in contained_volumes

    @handle_ontap_errors
    def volume_replicate(self, pool, rep_type, volume_src, name, flags=0):
        #At the moment we are only supporting space efficient writeable
        #logical units.  Add support for the others later.
        if rep_type != Volume.REPLICATE_CLONE:
            raise LsmError(ErrorNumber.NO_SUPPORT, "rep_type not supported")

        #Check to see if our volume is on a pool that was passed in or that
        #the pool itself is None
        if pool is None or self._volume_on_aggr(pool, volume_src):
            #re-size the NetApp volume to accommodate the new lun
            size = Ontap._size_kb_padded(volume_src.size_bytes)

            self._na_volume_resize_restore(
                Ontap._vol_to_na_volume_name(volume_src), size)

            #Thin provision copy the logical unit
            dest = os.path.dirname(volume_src.name) + '/' + name

            try:
                self.f.clone(volume_src.name, dest)
            except Exception as e:
                #Put volume back to previous size
                self._na_resize_recovery(
                    Ontap._vol_to_na_volume_name(volume_src), -size)
                raise
            return None, self._get_volume(dest, volume_src.pool_id)
        else:
            #TODO Need to get instructions on how to provide this
            #functionality
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
    def volume_mask(self, access_group, volume, flags=0):
        self.f.lun_map(access_group.name, volume.name)
        return None

    @handle_ontap_errors
    def volume_unmask(self, access_group, volume, flags=0):
        self.f.lun_unmap(access_group.name, volume.name)
        return None

    @staticmethod
    def _initiators_in_group(g):
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

        return AccessGroup(ag_id, name, Ontap._initiators_in_group(g),
                           _na_init_type_to_lsm(g), self.sys_info.id)

    @handle_ontap_errors
    def access_groups(self, search_key=None, search_value=None, flags=0):
        groups = self.f.igroups()
        return search_property(
            [self._access_group(g) for g in groups], search_key, search_value)

    @handle_ontap_errors
    def access_group_create(self, name, init_id, init_type, system,
                            flags=0):
        if self.sys_info.id != system.id:
            raise LsmError(ErrorNumber.NOT_FOUND_SYSTEM,
                           "System %s not found" % system.id)
        cur_groups = self.access_groups()
        for cg in cur_groups:
            if cg.name == name:
                raise LsmError(ErrorNumber.EXISTS_ACCESS_GROUP,
                               "Access group with the same name exists!")

        if init_type == AccessGroup.INIT_TYPE_ISCSI_IQN:
            self.f.igroup_create(name, 'iscsi')
        elif init_type == AccessGroup.INIT_TYPE_WWPN:
            self.f.igroup_create(name, 'fcp')
        else:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "ONTAP only support iSCSI and FC/FCoE, but got "
                           "init_type: %d" % init_type)

        self.f.igroup_add_initiator(name, init_id)

        groups = self.access_groups()
        for g in groups:
            if g.name == name:
                return g

        raise LsmError(ErrorNumber.LSM_PLUGIN_BUG,
                       "access_group_create(): Unable to find access group "
                       "%s just created!" % name)

    @handle_ontap_errors
    def access_group_delete(self, access_group, flags=0):
        return self.f.igroup_delete(access_group.name)

    @handle_ontap_errors
    def access_group_initiator_add(self, access_group, init_id, init_type,
                                   flags=0):
        try:
            self.f.igroup_add_initiator(access_group.name, init_id)
        except na.FilerError as oe:
            if oe.errno == na.FilerError.IGROUP_ALREADY_HAS_INIT:
                return copy.deepcopy(access_group)
            elif oe.errno == na.FilerError.NO_SUCH_IGROUP:
                raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                               "AccessGroup %s(%d) not found" %
                               (access_group.name, access_group.id))
            else:
                raise
        na_ags = self.f.igroups(access_group.name)
        if len(na_ags) != 1:
            raise LsmError(ErrorNumber.LSM_PLUGIN_BUG,
                           "access_group_initiator_add(): Got unexpected"
                           "(not 1) count of na_ag: %s" % na_ags)

        return self._access_group(na_ags[0])

    @handle_ontap_errors
    def access_group_initiator_delete(self, access_group, init_id, flags=0):
        try:
            self.f.igroup_del_initiator(access_group.name, init_id)
        except na.FilerError as oe:
            error_code, error_msg = error_map(oe)
            if oe.errno == na.FilerError.IGROUP_NOT_CONTAIN_GIVEN_INIT:
                return copy.deepcopy(access_group)
            elif oe.errno == na.FilerError.NO_SUCH_IGROUP:
                raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                               "AccessGroup %s(%d) not found" %
                               (access_group.name, access_group.id))
            else:
                raise
        na_ags = self.f.igroups(access_group.name)
        if len(na_ags) != 1:
            raise LsmError(ErrorNumber.LSM_PLUGIN_BUG,
                           "access_group_initiator_add(): Got unexpected"
                           "(not 1) count of na_ag: %s" % na_ags)

        return self._access_group(na_ags[0])

    @handle_ontap_errors
    def volumes_accessible_by_access_group(self, access_group, flags=0):
        rc = []

        if len(access_group.init_ids):
            luns = self.f.lun_initiator_list_map_info(access_group.init_ids[0],
                                                      access_group.name)
            rc = [self._lun(l) for l in luns]

        return rc

    @handle_ontap_errors
    def access_groups_granted_to_volume(self, volume, flags=0):
        groups = self.f.lun_map_list_info(volume.name)
        return [self._access_group(g) for g in groups]

    @handle_ontap_errors
    def iscsi_chap_auth(self, init_id, in_user, in_password, out_user,
                        out_password, flags=0):
        if out_user and out_password and \
                (in_user is None or in_password is None):
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "out_user and out_password only supported if "
                           "inbound is supplied")

        self.f.iscsi_initiator_add_auth(init_id, in_user, in_password,
                                        out_user, out_password)

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
    def fs(self, search_key=None, search_value=None, flags=0):
        volumes = self.f.volumes()
        pools = self.pools()
        return search_property(
            [self._vol(v, pools) for v in volumes], search_key, search_value)

    @handle_ontap_errors
    def fs_delete(self, fs, flags=0):
        self.f.volume_delete(fs.name)

    @handle_ontap_errors
    def fs_resize(self, fs, new_size_bytes, flags=0):
        diff = new_size_bytes - fs.total_space

        diff = Ontap._size_kb_padded(diff)
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
    def fs_file_clone(self, fs, src_file_name, dest_file_name, snapshot=None,
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
        return [Ontap._ss(s) for s in snapshots]

    @handle_ontap_errors
    def fs_snapshot_create(self, fs, snapshot_name, flags=0):
        #We can't do files, so we will do them all
        snap = self.f.snapshot_create(fs.name, snapshot_name)
        return None, Ontap._ss(snap)

    @handle_ontap_errors
    def fs_snapshot_delete(self, fs, snapshot, flags=0):
        self.f.snapshot_delete(fs.name, snapshot.name)

    def _ss_restore_files(self, volume_name, snapshot_name, files,
                          restore_files):
        for i in range(len(files)):
            src = Ontap.build_name(volume_name, files[i])
            dest = None
            if restore_files and len(restore_files):
                dest = Ontap.build_name(volume_name, restore_files[i])
            self.f.snapshot_restore_file(snapshot_name, src, dest)

    @handle_ontap_errors
    def fs_snapshot_restore(self, fs, snapshot, files, restore_files,
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

            self._ss_restore_files(fs.name, snapshot.name, files,
                                   restore_files)
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

    @staticmethod
    def _get_group(access_group, e):
        rc = []

        if access_group in e:
            for r in na.to_list(e[access_group]['exports-hostname-info']):
                if 'all-hosts' in r:
                    if r['all-hosts'] == 'true':
                        rc.append('*')
                else:
                    rc.append(r['name'])
        return rc

    @staticmethod
    def _get_value(key, e):
        if key in e:
            return e[key]
        else:
            return None

    @staticmethod
    def _get_volume_id(volumes, vol_name):
        for v in volumes:
            if v.name == vol_name:
                return v.id
        raise RuntimeError("Volume not found in volumes:" +
                           ":".join(volumes) + " " + vol_name)

    @staticmethod
    def _get_volume_from_path(path):
        #Volume paths have the form /vol/<volume name>/<rest of path>
        return path[5:].split('/')[0]

    @staticmethod
    def _export(volumes, e):
        if 'actual-pathname' in e:
            path = e['actual-pathname']
            export = e['pathname']
        else:
            path = e['pathname']
            export = e['pathname']

        vol_name = Ontap._get_volume_from_path(path)
        fs_id = Ontap._get_volume_id(volumes, vol_name)

        return NfsExport(md5(vol_name + fs_id), fs_id, export,
                         e['sec-flavor']['sec-flavor-info']['flavor'],
                         Ontap._get_group('root', e),
                         Ontap._get_group('read-write', e),
                         Ontap._get_group('read-only', e),
                         Ontap._get_value('anon', e), None, None)

    @handle_ontap_errors
    def exports(self, search_key=None, search_value=None, flags=0):
        #Get the file systems once and pass to _export which needs to lookup
        #the file system id by name.
        v = self.fs()
        return search_property(
            [Ontap._export(v, e) for e in self.f.nfs_exports()],
            search_key, search_value)

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

    @handle_ontap_errors
    def target_ports(self, search_key=None, search_value=None, flags=0):
        tp = []

        #Get all FC
        fcp = self.f.fcp_list()

        for f in fcp:
            a = f['addr']
            adapter = f['adapter']
            tp.append(TargetPort(md5(a), TargetPort.PORT_TYPE_FC, a, a, a,
                                 adapter, self.sys_info.id))

        node_name = self.f.iscsi_node_name()
        iscsi = self.f.iscsi_list()
        for i in iscsi:
            #Get all iSCSI
            service_address = node_name
            network_address = "%s:%s" % (i['ip'], i['port'])
            physical_address = i['mac']
            physical_name = i['interface']
            tid = md5(service_address + network_address + physical_address +
                      physical_name)
            tp.append(TargetPort(tid, TargetPort.PORT_TYPE_ISCSI,
                                 service_address,
                                 network_address,
                                 physical_address,
                                 physical_name,
                                 self.sys_info.id))

        return search_property(tp, search_key, search_value)
