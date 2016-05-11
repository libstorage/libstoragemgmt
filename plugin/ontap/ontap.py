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
# License along with this library; If not, see <http://www.gnu.org/licenses/>.
#
# Author: tasleson
#         Gris Ge <fge@redhat.com>

import os
import urlparse
import copy

import na
from lsm import (Volume, FileSystem, FsSnapshot, NfsExport,
                 AccessGroup, System, Capabilities, Disk, Pool,
                 IStorageAreaNetwork, INfs, LsmError, ErrorNumber, JobStatus,
                 md5, VERSION, common_urllib2_error_handler,
                 search_property, TargetPort)

# Maps na to lsm, this is expected to expand over time.
e_map = {
    na.Filer.ENOSPC: ErrorNumber.NOT_ENOUGH_SPACE,
    na.Filer.ENO_SUCH_VOLUME: ErrorNumber.NOT_FOUND_VOLUME,
    na.Filer.ESIZE_TOO_LARGE: ErrorNumber.NOT_ENOUGH_SPACE,
    na.Filer.ENOSPACE: ErrorNumber.NOT_ENOUGH_SPACE,
    na.Filer.ENO_SUCH_FS: ErrorNumber.NOT_FOUND_FS,
    na.Filer.EAPILICENSE: ErrorNumber.NOT_LICENSED,
    na.Filer.EFSDOESNOTEXIST: ErrorNumber.NOT_FOUND_FS,
    na.Filer.EFSOFFLINE: ErrorNumber.NO_SUPPORT_ONLINE_CHANGE,
    na.Filer.EFSNAMEINVALID: ErrorNumber.INVALID_ARGUMENT,
    na.Filer.ESERVICENOTLICENSED: ErrorNumber.NOT_LICENSED,
    na.Filer.ECLONE_LICENSE_EXPIRED: ErrorNumber.NOT_LICENSED,
    na.Filer.ECLONE_NOT_LICENSED: ErrorNumber.NOT_LICENSED,
    na.Filer.EINVALID_ISCSI_NAME: ErrorNumber.INVALID_ARGUMENT,
    na.Filer.ETIMEOUT: ErrorNumber.TIMEOUT,
    na.Filer.EUNKNOWN: ErrorNumber.PLUGIN_BUG,
    na.Filer.EDUPE_VOLUME_PATH: ErrorNumber.NAME_CONFLICT,
    na.Filer.ENAVOL_NAME_DUPE: ErrorNumber.NAME_CONFLICT,
    na.Filer.ECLONE_NAME_EXISTS: ErrorNumber.NAME_CONFLICT
}


def error_map(oe):
    """
    Maps a ontap error code to a lsm error code.
    Returns a tuple containing error code and text.
    """
    if oe.errno in e_map:
        return e_map[oe.errno], oe.reason
    else:
        return ErrorNumber.PLUGIN_BUG, \
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


def _lsm_vol_to_na_vol_path(vol):
    return vol.id


class Ontap(IStorageAreaNetwork, INfs):
    TMO_CONV = 1000.0

    (SS_JOB, SPLIT_JOB) = ('ontap-ss-file-restore', 'ontap-clone-split')

    VOLUME_PREFIX = '/vol'

    NA_VOL_STATUS_TO_LSM = {
        'offline': Pool.STATUS_STOPPED,
        'online': Pool.STATUS_OK,
        'restricted': Pool.STATUS_OTHER,
        'unknown': Pool.STATUS_UNKNOWN,
        'creating': Pool.STATUS_INITIALIZING,
        'failed': Pool.STATUS_ERROR,
        'partial': Pool.STATUS_ERROR,

    }

    NA_VOL_STATUS_TO_LSM_STATUS_INFO = {
        'partial': 'all the disks in the volume are not available.',
        'restricted': 'volume is restricted to protocol accesses',
    }

    # strip size: http://www.netapp.com/us/media/tr-3001.pdf
    _STRIP_SIZE = 4096
    _OPT_IO_SIZE = 65536

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
        # Smoke test
        i = self.f.system_info()
        # TODO Get real filer status
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

    @staticmethod
    def _lsm_lun_name(path_name):
        return os.path.basename(path_name)

    def _lun(self, l):
        block_size = int(l['block-size'])
        num_blocks = int(l['size']) / block_size
        pool_id = "/".join(l['path'].split('/')[0:3])
        vol_id = l['path']
        vol_name = os.path.basename(vol_id)
        admin_state = Volume.ADMIN_STATE_ENABLED
        if l['online'] == 'false':
            admin_state = Volume.ADMIN_STATE_DISABLED

        return Volume(vol_id, vol_name,
                      Ontap._create_vpd(l['serial-number']), block_size,
                      num_blocks, admin_state, self.sys_info.id, pool_id)

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
        # If we use the newer API we can use the uuid instead of this fake
        # md5 one
        return FsSnapshot(md5(s['name'] + s['access-time']), s['name'],
                          s['access-time'])

    _NA_DISK_TYPE_TO_LSM = {
        'ATA': Disk.TYPE_ATA,
        'BSAS': Disk.TYPE_SATA,
        'EATA': Disk.TYPE_ATA,
        'FCAL': Disk.TYPE_FC,
        'FSAS': Disk.TYPE_NL_SAS,
        'LUN': Disk.TYPE_OTHER,
        'MSATA': Disk.TYPE_SATA,
        'SAS': Disk.TYPE_SAS,
        'SATA': Disk.TYPE_SATA,
        'SCSI': Disk.TYPE_SCSI,
        'SSD': Disk.TYPE_SSD,
        'XATA': Disk.TYPE_ATA,
        'XSAS': Disk.TYPE_SAS,
        'unknown': Disk.TYPE_UNKNOWN,
    }

    @staticmethod
    def _disk_type_of(na_disk):
        """
        Convert na_disk['effective-disk-type'] to LSM disk type.
        """
        na_disk_type = na_disk['effective-disk-type']
        if na_disk_type in Ontap._NA_DISK_TYPE_TO_LSM.keys():
            return Ontap._NA_DISK_TYPE_TO_LSM[na_disk_type]
        return Disk.TYPE_UNKNOWN

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
        status = 0

        if 'raid-state' in na_disk:
            rs = na_disk['raid-state']
            if rs == "broken":
                if na_disk['broken-details'] == 'admin removed' or \
                   na_disk['broken-details'] == 'admin failed':
                    status |= Disk.STATUS_REMOVED
                elif na_disk['broken-details'] == 'admin testing':
                    status |= Disk.STATUS_STOPPED | \
                        Disk.STATUS_MAINTENANCE_MODE
                else:
                    status |= Disk.STATUS_ERROR
            elif rs == "unknown":
                status |= Disk.STATUS_UNKNOWN
            elif rs == 'zeroing':
                status |= Disk.STATUS_INITIALIZING | Disk.STATUS_SPARE_DISK
            elif rs == 'reconstructing' or rs == 'copy':
                # "reconstructing' should be a pool status, not disk status.
                # disk under reconstructing should be considered as OK.
                status |= Disk.STATUS_OK | Disk.STATUS_RECONSTRUCT
            elif rs == 'spare':
                if 'is-zeroed' in na_disk and na_disk['is-zeroed'] == 'true':
                    status |= Disk.STATUS_OK | Disk.STATUS_SPARE_DISK
                else:
                    # If spare disk is not zerored, it will be automaticlly
                    # zeroed before assigned to aggregate.
                    # Hence we consider non-zeroed spare disks as stopped
                    # spare disks.
                    status |= Disk.STATUS_STOPPED | Disk.STATUS_SPARE_DISK
            elif rs == 'present':
                status |= Disk.STATUS_OK
            elif rs == 'partner':
                # Before we have good way to connect two controller,
                # we have to mark partner disk as OTHER
                return Disk.STATUS_OTHER

        if 'is-prefailed' in na_disk and na_disk['is-prefailed'] == 'true':
            status |= Disk.STATUS_STOPPING

        if 'is-offline' in na_disk and na_disk['is-offline'] == 'true':
            status |= Disk.STATUS_ERROR

        if 'aggregate' not in na_disk:
            # All free disks are automatically marked as spare disks. They
            # could easily convert to data or parity disk without any
            # explicit command.
            status |= Disk.STATUS_FREE

        if status == 0:
            status = Disk.STATUS_UNKNOWN

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
                    Ontap._disk_type_of(d),
                    int(d['bytes-per-sector']), int(d['physical-blocks']),
                    status, self.sys_info.id)

    @handle_ontap_errors
    def volumes(self, search_key=None, search_value=None, flags=0):
        luns = self.f.luns_get_all()
        return search_property(
            [self._lun(l) for l in luns], search_key, search_value)

    # This is based on NetApp ONTAP Manual pages:
    # https://library.netapp.com/ecmdocs/ECMP1196890/html/man1/na_aggr.1.html
    _AGGR_RAID_STATUS_CONV = {
        'normal': Pool.STATUS_OK,
        'verifying': Pool.STATUS_OK | Pool.STATUS_VERIFYING,
        'copying': Pool.STATUS_INITIALIZING,
        'ironing': Pool.STATUS_OK | Pool.STATUS_VERIFYING,
        'resyncing': Pool.STATUS_OK | Pool.STATUS_DEGRADED |
        Pool.STATUS_RECONSTRUCTING,
        'mirror degraded': Pool.STATUS_OK | Pool.STATUS_DEGRADED,
        'needs check': Pool.STATUS_ERROR,
        'initializing': Pool.STATUS_INITIALIZING,
        'growing': Pool.STATUS_OK | Pool.STATUS_GROWING,
        'partial': Pool.STATUS_ERROR,
        'noparity': Pool.STATUS_OTHER,
        'degraded': Pool.STATUS_OK | Pool.STATUS_DEGRADED,
        'reconstruct': Pool.STATUS_OK | Pool.STATUS_DEGRADED |
        Pool.STATUS_RECONSTRUCTING,
        'out-of-date': Pool.STATUS_OTHER,
        'foreign': Pool.STATUS_OTHER,
    }

    _AGGR_RAID_ST_INFO_CONV = {
        'copying': 'The aggregate is currently the target aggregate of an'
                   'active aggr copy operation. ',
        'invalid': 'The aggregate does not contain any volume and no volume'
                   'can be added to it. Typically this happens after an '
                   'aborted aggregate copy operation. ',
        'needs check': 'A WAFL consistency check needs to be performed on '
                       'the aggregate. ',
        'partial': 'Two or more disks are missing.',
        # noparity, no document found.
        'noparity': 'NetApp ONTAP mark this aggregate as "noparity". ',
        # out-of-data: no document found.
        'out-of-date': 'NetApp ONTAP mark this aggregate as "out-of-date". ',
        'foreign': "The disks that the aggregate contains were moved to the"
                   "current node from another node. "
    }

    @staticmethod
    def _status_of_na_aggr(na_aggr):
        """
        Use aggr-info['state'] and ['raid-status'] for Pool.status and
        status_info.
        Return (status, status_info)
        """
        status = 0
        status_info = ''
        na_aggr_raid_status_list = list(
            x.strip() for x in na_aggr['raid-status'].split(','))
        for na_aggr_raid_status in na_aggr_raid_status_list:
            if na_aggr_raid_status in Ontap._AGGR_RAID_STATUS_CONV.keys():
                status |= Ontap._AGGR_RAID_STATUS_CONV[na_aggr_raid_status]
            if na_aggr_raid_status in Ontap._AGGR_RAID_ST_INFO_CONV.keys():
                status_info += \
                    Ontap._AGGR_RAID_ST_INFO_CONV[na_aggr_raid_status]

        # Now check na_aggr['state']
        na_aggr_state = na_aggr['state'].strip()

        if na_aggr_state == 'online' or na_aggr_state == 'creating':
            pass
        elif na_aggr_state == 'offline':
            # When aggr is marked as offline, the restruction is stoped.
            if status & Pool.STATUS_RECONSTRUCTING:
                status -= Pool.STATUS_RECONSTRUCTING
                status |= Pool.STATUS_DEGRADED
            status |= Pool.STATUS_STOPPED
        else:
            status_info += "%s " % na_aggr_state

        if status == 0:
            status = Pool.STATUS_OK

        return status, status_info

    def _pool_from_na_aggr(self, na_aggr, flags):
        pool_id = na_aggr['name']
        pool_name = na_aggr['name']
        total_space = int(na_aggr['size-total'])
        free_space = int(na_aggr['size-available'])
        system_id = self.sys_info.id
        (status, status_info) = self._status_of_na_aggr(na_aggr)

        element_type = (Pool.ELEMENT_TYPE_POOL | Pool.ELEMENT_TYPE_FS)

        # The system aggregate can be used to create both FS and volumes, but
        # you can't take it offline or delete it.
        if pool_name == 'aggr0':
            element_type = element_type | Pool.ELEMENT_TYPE_SYS_RESERVED

        return Pool(pool_id, pool_name, element_type, 0, total_space,
                    free_space, status, status_info, system_id)

    @staticmethod
    def _status_info_of_na_vol(na_vol):
        na_vol_state = na_vol['state']
        if na_vol_state in Ontap.NA_VOL_STATUS_TO_LSM_STATUS_INFO.keys():
            return Ontap.NA_VOL_STATUS_TO_LSM_STATUS_INFO[na_vol_state]
        return ''

    @staticmethod
    def _pool_id_of_na_vol_name(na_vol_name):
        return "%s/%s" % (Ontap.VOLUME_PREFIX, na_vol_name)

    def _pool_from_na_vol(self, na_vol, na_aggrs, flags):
        element_type = Pool.ELEMENT_TYPE_VOLUME
        # Thin provisioning is controled by:
        #   1. NetApp Volume level:
        #      'guarantee' option and 'fractional_reserve' option.
        #      If 'guarantee' is 'file', 'fractional_reserve' is forced to
        #      be 100, we can create Thin LUN and full allocated LUN.
        #      If 'guarantee' is 'volume' and 'fractional_reserve' is 100, we
        #      can create full LUN.
        #      If 'guarantee' is 'volume' and 'fractional_reserve' is less
        #      than 100, we can only create thin LUN.
        #      If 'guarantee' is 'none', we can only create thin LUN.
        #   2. NetApp LUN level:
        #      If option 'reservation' is enabled, it's a full allocated LUN
        #      when parent NetApp volume allowed.
        #      If option 'reservation' is disabled, it's a thin LUN if
        #      parent NetApp volume allowed.

        if 'space-reserve' in na_vol and \
           'space-reserve-enabled' in na_vol and \
           'reserve' in na_vol and \
           na_vol['space-reserve-enabled'] == 'true':
            # 'space-reserve' and  'space-reserve-enabled' might not appear if
            # the flexible volume is restricted or offline.
            if na_vol['space-reserve'] == 'file':
                # space-reserve: 'file' means only LUN or file marked as
                # 'Space Reservation: enabled' will be reserve all space.
                element_type |= Pool.ELEMENT_TYPE_VOLUME_THIN
                element_type |= Pool.ELEMENT_TYPE_VOLUME_FULL
            elif na_vol['space-reserve'] == 'volume':
                # space-reserve: 'volume' means only LUN or file marked as
                # 'Space Reservation: enabled' will be reserve all space.
                if na_vol['reserve'] == na_vol['reserve-required']:
                    # When 'reserve' == 'reserve-required' it means option
                    # 'fractional_reserve' is set to 100, only with that we
                    # can create full alocated LUN.
                    element_type |= Pool.ELEMENT_TYPE_VOLUME_FULL
                else:
                    element_type |= Pool.ELEMENT_TYPE_VOLUME_THIN
            elif na_vol['space-reserve'] == 'none':
                element_type |= Pool.ELEMENT_TYPE_VOLUME_THIN

        pool_name = na_vol['name']
        pool_id = self._pool_id_of_na_vol_name(na_vol['name'])
        total_space = int(na_vol['size-total'])
        free_space = int(na_vol['size-available'])
        system_id = self.sys_info.id
        status = Pool.STATUS_UNKNOWN
        status_info = ''
        if 'containing-aggregate' in na_vol:
            for na_aggr in na_aggrs:
                if na_aggr['name'] == na_vol['containing-aggregate']:
                    status = self._status_of_na_aggr(na_aggr)[0]
                    if not (status & Pool.STATUS_OK):
                        status_info = "Parrent pool '%s'" \
                                      % na_aggr['name']
                    break

        if status & Pool.STATUS_OK and na_vol['state'] == 'offline':
            status = Pool.STATUS_STOPPED
            status_info = 'Disabled by admin'

        # This volume should be noted that it is reserved for system
        # and thus cannot be removed.
        if pool_name == '/vol/vol0':
            element_type |= Pool.ELEMENT_TYPE_SYS_RESERVED

        return Pool(pool_id, pool_name, element_type, 0, total_space,
                    free_space, status, status_info, system_id)

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
        cap.set(Capabilities.VOLUME_ENABLE)
        cap.set(Capabilities.VOLUME_DISABLE)
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
        cap.set(Capabilities.DISKS)
        cap.set(Capabilities.VOLUME_RAID_INFO)
        cap.set(Capabilities.POOL_MEMBER_INFO)
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
        for na_aggr in na_aggrs:
            pools.extend([self._pool_from_na_aggr(na_aggr, flags)])
        na_vols = self.f.volumes()
        for na_vol in na_vols:
            pools.extend([self._pool_from_na_vol(na_vol, na_aggrs, flags)])
        return search_property(pools, search_key, search_value)

    @handle_ontap_errors
    def systems(self, flags=0):
        return [self.sys_info]

    def _get_volume(self, vol_name, pool_id):
        return self._lun(self.f.luns_get_specific(pool_id, vol_name, None)[0])

    @handle_ontap_errors
    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags=0):

        if not pool.element_type & Pool.ELEMENT_TYPE_VOLUME:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Pool not suitable for creating volumes")

        # Even pool is full or thin only pool, we still allow user to
        # create full or thin LUN in case that's what they intend to do so.
        # TODO: allow user to query provising status of certain LUN. We can
        #       use THIN(not effective) or FULL(not effective) to indicate
        #       pool setting not allow thin/full LUN yet, user can change pool
        #       setting.
        # Wise user can check pool.element_type before creating full or thin
        # volume.
        flag_thin = False
        if provisioning == Volume.PROVISION_THIN:
            flag_thin = True

        na_vol_name = pool.name

        lun_name = self.f.lun_build_name(na_vol_name, volume_name)

        try:
            self.f.lun_create(lun_name, size_bytes, flag_thin)
        except na.FilerError as fe:
            if fe.errno == na.FilerError.EVDISK_ERROR_SIZE_TOO_LARGE:
                raise LsmError(
                    ErrorNumber.NOT_ENOUGH_SPACE,
                    "No enough requested free size in pool")
            elif fe.errno == na.FilerError.EVDISK_ERROR_VDISK_EXISTS:
                raise LsmError(
                    ErrorNumber.NAME_CONFLICT,
                    "Requested volume name is already used by other volume")
            elif fe.errno == na.FilerError.EVDISK_ERROR_SIZE_TOO_SMALL:
                # Size too small should not be raised. By API defination,
                # we should create a LUN with mimun size.
                min_size = self.f.lun_min_size()
                return self.volume_create(
                    pool, volume_name, min_size, provisioning, flags)
            elif fe.errno == na.FilerError.EVDISK_ERROR_NO_SUCH_VOLUME:
                # When NetApp volume is offline, we will get this error also.
                self._check_na_volume(na_vol_name)
            else:
                raise

        # Get the information about the newly created LUN
        return None, self._get_volume(lun_name, pool.id)

    @staticmethod
    def _vol_to_na_volume_name(volume):
        return os.path.dirname(_lsm_vol_to_na_vol_path(volume))[5:]

    @handle_ontap_errors
    def volume_delete(self, volume, flags=0):
        try:
            self.f.lun_delete(_lsm_vol_to_na_vol_path(volume))
        except na.FilerError as f_error:
            # We don't use handle_ontap_errors which use netapp
            # error message which is not suitable for LSM user.
            if f_error.errno == na.FilerError.EVDISK_ERROR_VDISK_EXPORTED:
                raise LsmError(ErrorNumber.IS_MASKED,
                               "Volume is masked to access group")
            raise
        return None

    @staticmethod
    def _size_kb_padded(size_bytes):
        return int((size_bytes / 1024) * 1.3)

    @handle_ontap_errors
    def volume_resize(self, volume, new_size_bytes, flags=0):
        try:
            self.f.lun_resize(_lsm_vol_to_na_vol_path(volume), new_size_bytes)
        except na.FilerError as fe:
            if fe.errno == na.FilerError.EVDISK_ERROR_SIZE_TOO_SMALL:
                min_size = self.f.lun_min_size()
                try:
                    self.f.lun_resize(_lsm_vol_to_na_vol_path(volume),
                                      min_size)
                except na.FilerError as fe:
                    if fe.errno == na.FilerError.EVDISK_ERROR_SIZE_UNCHANGED:
                        # As requested size is not the one we are send to
                        # self.f.lun_resize(), we should silently pass.
                        pass
                    else:
                        raise
            elif fe.errno == na.FilerError.EVDISK_ERROR_SIZE_UNCHANGED:
                raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                               "Requested size is the same as current "
                               "volume size")
            else:
                raise
        return None, self._get_volume(_lsm_vol_to_na_vol_path(volume),
                                      volume.pool_id)

    def _check_na_volume(self, na_vol_name):
        na_vols = self.f.volumes(volume_name=na_vol_name)
        if len(na_vols) == 0:
            raise LsmError(ErrorNumber.NOT_FOUND_POOL,
                           "Pool not found")
        elif len(na_vols) == 1:
            # NetApp Volume is disabled.
            if na_vols[0]['state'] == 'offline':
                raise LsmError(ErrorNumber.POOL_NOT_READY,
                               "Pool not ready for volume creation")
        else:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "volume_create(): "
                           "Got 2 or more na_vols: %s" % na_vols)

    def _volume_on_aggr(self, pool, volume):
        search = Ontap._vol_to_na_volume_name(volume)
        contained_volumes = self.f.aggregate_volume_names(pool.name)
        return search in contained_volumes

    @handle_ontap_errors
    def volume_replicate(self, pool, rep_type, volume_src, name, flags=0):
        # At the moment we are only supporting space efficient writeable
        # logical units.  Add support for the others later.
        if rep_type != Volume.REPLICATE_CLONE:
            raise LsmError(ErrorNumber.NO_SUPPORT, "rep_type not supported")

        # Check to see if our volume is on a pool that was passed in or that
        # the pool itself is None
        if pool is None or self._volume_on_aggr(pool, volume_src):
            # Thin provision copy the logical unit
            dest = os.path.dirname(_lsm_vol_to_na_vol_path(volume_src)) + '/' \
                + name
            self.f.clone(_lsm_vol_to_na_vol_path(volume_src), dest)
            return None, self._get_volume(dest, volume_src.pool_id)
        else:
            # TODO Need to get instructions on how to provide this
            # functionality
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
        self.f.clone(_lsm_vol_to_na_vol_path(volume_src),
                     _lsm_vol_to_na_vol_path(volume_dest), None, ranges)

    @handle_ontap_errors
    def volume_enable(self, volume, flags=0):
        try:
            return self.f.lun_online(_lsm_vol_to_na_vol_path(volume))
        except na.FilerError as fe:
            if fe.errno == na.FilerError.EVDISK_ERROR_VDISK_NOT_DISABLED:
                raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                               "Volume is already enabled")
            raise

    @handle_ontap_errors
    def volume_disable(self, volume, flags=0):
        try:
            return self.f.lun_offline(_lsm_vol_to_na_vol_path(volume))
        except na.FilerError as fe:
            if fe.errno == na.FilerError.EVDISK_ERROR_VDISK_NOT_ENABLED:
                raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                               "Volume is already disabled")
            raise

    @handle_ontap_errors
    def volume_mask(self, access_group, volume, flags=0):
        igroups = self.f.igroups(group_name=access_group.name)
        if len(igroups) != 1:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "AccessGroup %s(%d) not found" %
                           (access_group.name, access_group.id))

        cur_init_ids = Ontap._initiators_in_group(igroups[0])
        if len(cur_init_ids) == 0:
            raise LsmError(
                ErrorNumber.EMPTY_ACCESS_GROUP,
                "Refuse to do volume masking against empty access group")
        try:
            self.f.lun_map(access_group.name, _lsm_vol_to_na_vol_path(volume))
        except na.FilerError as fe:
            if fe.errno == na.FilerError.EVDISK_ERROR_INITGROUP_HAS_VDISK:
                raise LsmError(
                    ErrorNumber.NO_STATE_CHANGE,
                    "Volume is already masked to requested access group")
            else:
                raise
        return None

    @handle_ontap_errors
    def volume_unmask(self, access_group, volume, flags=0):
        try:
            self.f.lun_unmap(
                access_group.name, _lsm_vol_to_na_vol_path(volume))
        except na.FilerError as filer_error:
            if filer_error.errno == na.FilerError.EVDISK_ERROR_NO_SUCH_LUNMAP:
                raise LsmError(
                    ErrorNumber.NO_STATE_CHANGE,
                    "Volume is not masked to requested access group")
            else:
                raise
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

        # NetApp sometimes(real hardware 8.0.2 and simulator 8.1.1) does not
        # raise error for initiator conflict.
        #
        # Precheck for initiator conflict
        cur_lsm_groups = self.access_groups()
        for cur_lsm_group in cur_lsm_groups:
            if cur_lsm_group.name == name:
                raise LsmError(
                    ErrorNumber.NAME_CONFLICT,
                    "Requested access group name is already used by other "
                    "access group")
            if init_id in cur_lsm_group.init_ids:
                raise LsmError(
                    ErrorNumber.EXISTS_INITIATOR,
                    "Requested initiator is already used by other "
                    "access group")

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

        raise LsmError(ErrorNumber.PLUGIN_BUG,
                       "access_group_create(): Unable to find access group "
                       "%s just created!" % name)

    @handle_ontap_errors
    def access_group_delete(self, access_group, flags=0):
        try:
            return self.f.igroup_delete(access_group.name)
        except na.FilerError as f_error:
            if f_error.errno == \
               na.FilerError.EVDISK_ERROR_INITGROUP_MAPS_EXIST:
                raise LsmError(ErrorNumber.IS_MASKED,
                               "Access Group has volume masked")
            raise

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
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "access_group_initiator_add(): Got unexpected"
                           "(not 1) count of na_ag: %s" % na_ags)

        return self._access_group(na_ags[0])

    @handle_ontap_errors
    def access_group_initiator_delete(self, access_group, init_id, init_type,
                                      flags=0):
        igroups = self.f.igroups(group_name=access_group.name)
        if len(igroups) != 1:
            raise LsmError(ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                           "AccessGroup %s(%d) not found" %
                           (access_group.name, access_group.id))

        cur_init_ids = Ontap._initiators_in_group(igroups[0])
        if init_id not in cur_init_ids:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Initiator %s does not exist in access group %s" %
                (init_id, access_group.name))

        if len(cur_init_ids) == 1:
            raise LsmError(
                ErrorNumber.LAST_INIT_IN_ACCESS_GROUP,
                "Refuse to remove last initiator from access group")

        self.f.igroup_del_initiator(access_group.name, init_id)

        na_ags = self.f.igroups(access_group.name)
        if len(na_ags) != 1:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
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
        groups = self.f.lun_map_list_info(_lsm_vol_to_na_vol_path(volume))
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

        # It doesn't appear that we have a good percentage
        # indicator from the clone split status...
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
        if '@' not in job_id:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Invalid job, missing @")

        job = job_id.split('@', 2)

        if job[0] == Ontap.SS_JOB:
            return self._restore_file_status(int(job[1]))
        elif job[0] == Ontap.SPLIT_JOB:
            return self._clone_split_status(job[1])

        raise LsmError(ErrorNumber.INVALID_ARGUMENT, "Invalid job")

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
        # We can't do files, so we will do them all
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
        # Volume paths have the form /vol/<volume name>/<rest of path>
        return path[5:].split('/')[0]

    @staticmethod
    def _export(volumes, e):
        if 'actual-pathname' in e:
            path = e['actual-pathname']
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
                         NfsExport.ANON_UID_GID_NA, NfsExport.ANON_UID_GID_NA,
                         None)

    @handle_ontap_errors
    def exports(self, search_key=None, search_value=None, flags=0):
        # Get the file systems once and pass to _export which needs to lookup
        # the file system id by name.
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
        # NetApp does not support anon_gid setting.
        if not (anon_gid == -1 or anon_gid == 0xFFFFFFFFFFFFFFFF):
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "ontap plugin does not support "
                           "anon_gid setting")

        # Get the volume info from the fs_id
        vol = self._get_volume_from_id(fs_id)

        # API states that if export path is None the plug-in will select
        # export path
        if export_path is None:
            export_path = '/vol/' + vol.name

        # If the export already exists we need to update the existing export
        # not create a new one.
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

        raise LsmError(ErrorNumber.PLUGIN_BUG,
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

        # TODO: Make sure file actually exists if specified

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

        # Get all FC
        fcp = self.f.fcp_list()

        for f in fcp:
            a = f['addr']
            adapter = f['adapter']
            tp.append(TargetPort(md5(a), TargetPort.TYPE_FC, a, a, a,
                                 adapter, self.sys_info.id))

        node_name = self.f.iscsi_node_name()
        iscsi = self.f.iscsi_list()
        for i in iscsi:
            # Get all iSCSI
            service_address = node_name
            network_address = "%s:%s" % (i['ip'], i['port'])
            physical_address = i['mac']
            physical_name = i['interface']
            tid = md5(service_address + network_address + physical_address +
                      physical_name)
            tp.append(TargetPort(tid, TargetPort.TYPE_ISCSI,
                                 service_address,
                                 network_address,
                                 physical_address,
                                 physical_name,
                                 self.sys_info.id))

        return search_property(tp, search_key, search_value)

    @staticmethod
    def _raid_type_of_na_aggr(na_aggr):
        na_raid_statuses = na_aggr['raid-status'].split(',')
        if 'mixed_raid_type' in na_raid_statuses:
            return Volume.RAID_TYPE_MIXED
        elif 'raid0' in na_raid_statuses:
            return Volume.RAID_TYPE_RAID0
        elif 'raid4' in na_raid_statuses:
            return Volume.RAID_TYPE_RAID4
        elif 'raid_dp' in na_raid_statuses:
            return Volume.RAID_TYPE_RAID6
        return Volume.RAID_TYPE_UNKNOWN

    @handle_ontap_errors
    def volume_raid_info(self, volume, flags=0):
        # Check existance of LUN
        self.f.luns_get_specific(None, na_lun_name=volume.id)

        na_vol_name = Ontap._get_volume_from_path(volume.pool_id)
        na_vol = self.f.volumes(volume_name=na_vol_name)
        if len(na_vol) == 0:
            # If parent pool not found, then this LSM volume should not exist.
            raise LsmError(
                ErrorNumber.NOT_FOUND_VOLUME,
                "Volume not found")
        if len(na_vol) != 1:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "volume_raid_info(): Got 2+ na_vols from self.f.volumes() "
                "%s" % na_vol)

        na_vol = na_vol[0]
        na_aggr_name = na_vol['containing-aggregate']
        na_aggr = self.f.aggregates(aggr_name=na_aggr_name)[0]
        raid_type = Ontap._raid_type_of_na_aggr(na_aggr)
        disk_count = int(na_aggr['disk-count'])

        return [
            raid_type, Ontap._STRIP_SIZE, disk_count, Ontap._STRIP_SIZE,
            Ontap._OPT_IO_SIZE]

    @handle_ontap_errors
    def pool_member_info(self, pool, flags=0):
        if pool.element_type & Pool.ELEMENT_TYPE_VOLUME:
            # We got a NetApp volume
            raid_type = Volume.RAID_TYPE_OTHER
            member_type = Pool.MEMBER_TYPE_POOL
            na_vol = self.f.volumes(volume_name=pool.name)[0]
            disk_ids = [na_vol['containing-aggregate']]
        else:
            # We got a NetApp aggregate
            member_type = Pool.MEMBER_TYPE_DISK
            na_aggr = self.f.aggregates(aggr_name=pool.name)[0]
            raid_type = Ontap._raid_type_of_na_aggr(na_aggr)
            disk_ids = list(
                Ontap._disk_id(d)
                for d in self.f.disks()
                if 'aggregate' in d and d['aggregate'] == pool.name)

        return raid_type, member_type, disk_ids
