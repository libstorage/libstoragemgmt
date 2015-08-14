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
# Author: Andy Grover <agrover at redhat com>
#         Gris Ge <fge@redhat.com>

import copy
import urllib2
import json
import time
import urlparse
import socket
import re

from lsm import (Pool, Volume, System, Capabilities,
                 IStorageAreaNetwork, INfs, FileSystem, FsSnapshot, NfsExport,
                 LsmError, ErrorNumber, uri_parse, md5, VERSION,
                 common_urllib2_error_handler, search_property,
                 AccessGroup)

DEFAULT_USER = "admin"
DEFAULT_PORT = 18700
PATH = "/targetrpc"

# Current sector size in liblvm
_LVM_SECTOR_SIZE = 512


def handle_errors(method):
    def target_wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except TargetdError as te:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           "Got error %d from targetd: %s"
                           % (te.errno, te.reason))
        except LsmError:
            raise
        except Exception as e:
            common_urllib2_error_handler(e)

    return target_wrapper


class TargetdError(Exception):
    VOLUME_MASKED = 303
    INVALID_METHOD = 32601
    INVALID_ARGUMENT = 32602
    NAME_CONFLICT = 50
    EXISTS_INITIATOR = 52
    NO_FREE_HOST_LUN_ID = 1000
    EMPTY_ACCESS_GROUP = 511

    def __init__(self, errno, reason, *args, **kwargs):
        Exception.__init__(self, *args, **kwargs)
        self.errno = int(errno)
        self.reason = reason


class TargetdStorage(IStorageAreaNetwork, INfs):
    _FAKE_AG_PREFIX = 'init.'
    _MAX_H_LUN_ID = 255

    _ERROR_MAPPING = {
        TargetdError.VOLUME_MASKED:
        dict(ec=ErrorNumber.IS_MASKED,
             msg="Volume is masked to access group"),

        TargetdError.EXISTS_INITIATOR:
        dict(ec=ErrorNumber.EXISTS_INITIATOR,
             msg="Initiator is already used by other access group"),

        TargetdError.NAME_CONFLICT:
        dict(ec=ErrorNumber.NAME_CONFLICT,
             msg=None),

        TargetdError.INVALID_ARGUMENT:
        dict(ec=ErrorNumber.INVALID_ARGUMENT,
             msg=None),

        TargetdError.NO_FREE_HOST_LUN_ID:
        # TODO(Gris Ge): Add SYSTEM_LIMIT error into API
        dict(ec=ErrorNumber.PLUGIN_BUG,
             msg="System limit: targetd only allows %s LUN masked" %
             _MAX_H_LUN_ID),

        TargetdError.EMPTY_ACCESS_GROUP:
        dict(ec=ErrorNumber.NOT_FOUND_ACCESS_GROUP,
             msg="Access group not found"),
    }

    def __init__(self):
        self.uri = None
        self.password = None
        self.tmo = 0
        self.rpc_id = 1
        self.host_with_port = None
        self.scheme = None
        self.url = None
        self.headers = None
        self._flag_ag_support = True
        self.system = System("targetd", "targetd storage appliance",
                             System.STATUS_UNKNOWN, '')

    @handle_errors
    def plugin_register(self, uri, password, timeout, flags=0):
        self.uri = uri_parse(uri)
        self.password = password
        self.tmo = timeout

        user = self.uri.get('username', DEFAULT_USER)
        port = self.uri.get('port', DEFAULT_PORT)

        self.host_with_port = "%s:%s" % (self.uri['host'], port)
        if self.uri['scheme'].lower() == 'targetd+ssl':
            self.scheme = 'https'
        else:
            self.scheme = 'http'

        self.url = urlparse.urlunsplit(
            (self.scheme, self.host_with_port, PATH, None, None))

        auth = ('%s:%s' % (user, self.password)).encode('base64')[:-1]
        self.headers = {'Content-Type': 'application/json',
                        'Authorization': 'Basic %s' % (auth,)}

        try:
            self._jsonrequest('access_group_list', default_error_handler=False)
        except TargetdError as te:
            if te.errno == TargetdError.INVALID_METHOD:
                self._flag_ag_support = False
            else:
                raise

    @handle_errors
    def time_out_set(self, ms, flags=0):
        self.tmo = ms

    @handle_errors
    def time_out_get(self, flags=0):
        return self.tmo

    @handle_errors
    def plugin_unregister(self, flags=0):
        pass

    @handle_errors
    def capabilities(self, system, flags=0):
        cap = Capabilities()
        cap.set(Capabilities.VOLUMES)
        cap.set(Capabilities.VOLUME_CREATE)
        cap.set(Capabilities.VOLUME_REPLICATE)
        cap.set(Capabilities.VOLUME_REPLICATE_COPY)
        cap.set(Capabilities.VOLUME_DELETE)
        cap.set(Capabilities.VOLUME_MASK)
        cap.set(Capabilities.VOLUME_UNMASK)
        cap.set(Capabilities.FS)
        cap.set(Capabilities.FS_CREATE)
        cap.set(Capabilities.FS_DELETE)
        cap.set(Capabilities.FS_CLONE)
        cap.set(Capabilities.FS_SNAPSHOT_CREATE)
        cap.set(Capabilities.FS_SNAPSHOT_DELETE)
        cap.set(Capabilities.FS_SNAPSHOTS)
        cap.set(Capabilities.EXPORT_AUTH)
        cap.set(Capabilities.EXPORTS)
        cap.set(Capabilities.EXPORT_FS)
        cap.set(Capabilities.EXPORT_REMOVE)
        cap.set(Capabilities.ACCESS_GROUPS)
        cap.set(Capabilities.ACCESS_GROUPS_GRANTED_TO_VOLUME)
        cap.set(Capabilities.VOLUMES_ACCESSIBLE_BY_ACCESS_GROUP)
        cap.set(Capabilities.VOLUME_ISCSI_CHAP_AUTHENTICATION)

        if self._flag_ag_support:
            cap.set(Capabilities.ACCESS_GROUP_CREATE_ISCSI_IQN)
            cap.set(Capabilities.ACCESS_GROUP_INITIATOR_ADD_ISCSI_IQN)
            cap.set(Capabilities.ACCESS_GROUP_INITIATOR_DELETE)
            cap.set(Capabilities.ACCESS_GROUP_DELETE)

        return cap

    @handle_errors
    def plugin_info(self, flags=0):
        return "Linux LIO target support", VERSION

    @handle_errors
    def systems(self, flags=0):
        # verify we're online
        self._jsonrequest("pool_list")

        return [self.system]

    @handle_errors
    def job_status(self, job_id, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def job_free(self, job_id, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @staticmethod
    def _uuid_to_vpd83(uuid):
        """
        Convert LVM UUID to VPD 83 Device ID.
        LIO kernel module(target_core_mod.ko) does not expose VPD83 via
        ConfigFs.
        Targetd does not expose VPD83 via its API.
        Hence we have to do the convention here base on kernel code.
        """
        # NAA IEEE Registered Extended Identifier/Designator.
        # SPC-4 rev37a 7.8.6.6.5
        vpd83 = '6'
        # Use OpenFabrics IEEE Company ID: 00 14 05
        # https://standards.ieee.org/develop/regauth/oui/oui.txt
        vpd83 += '001405'

        # Take all [a-f0-9] digits from UUID for VENDOR_SPECIFIC_IDENTIFIER
        # and VENDOR_SPECIFIC_IDENTIFIER_EXTENTION
        vpd83 += re.sub('[^a-f0-9]', '', uuid.lower())[:25]

        # Fill up with zero.
        vpd83 += '0' * (32 - len(vpd83))

        return vpd83

    @handle_errors
    def volumes(self, search_key=None, search_value=None, flags=0):
        volumes = []
        for p_name in (p['name'] for p in self._jsonrequest("pool_list") if
                       p['type'] == 'block'):
            for vol in self._jsonrequest("vol_list", dict(pool=p_name)):
                vpd83 = TargetdStorage._uuid_to_vpd83(vol['uuid'])
                volumes.append(
                    Volume(vol['uuid'], vol['name'], vpd83, 512,
                           long(vol['size'] / 512),
                           Volume.ADMIN_STATE_ENABLED,
                           self.system.id, p_name))
        return search_property(volumes, search_key, search_value)

    @handle_errors
    def pools(self, search_key=None, search_value=None, flags=0):
        pools = []
        for pool in self._jsonrequest("pool_list"):
            if pool['name'].startswith('/'):
                et = Pool.ELEMENT_TYPE_FS
            else:
                et = Pool.ELEMENT_TYPE_VOLUME

            pools.append(Pool(pool['name'],
                              pool['name'], et, 0, pool['size'],
                              pool['free_size'], Pool.STATUS_UNKNOWN, '',
                              'targetd'))
        return search_property(pools, search_key, search_value)

    @staticmethod
    def _tgt_ag_to_lsm(tgt_ag, sys_id):
        return AccessGroup(
            tgt_ag['name'], tgt_ag['name'], tgt_ag['init_ids'],
            AccessGroup.INIT_TYPE_ISCSI_IQN, sys_id)

    @staticmethod
    def _tgt_init_to_lsm(tgt_init, sys_id):
        return AccessGroup(
            "%s%s" % (
                TargetdStorage._FAKE_AG_PREFIX, md5(tgt_init['init_id'])),
            'N/A', [tgt_init['init_id']], AccessGroup.INIT_TYPE_ISCSI_IQN,
            sys_id)

    @handle_errors
    def access_groups(self, search_key=None, search_value=None, flags=0):
        rc_lsm_ags = []

        # For backward compatibility
        if self._flag_ag_support is True:
            tgt_inits = self._jsonrequest(
                'initiator_list', {'standalone_only': True})
        else:
            tgt_inits = list(
                {'init_id': x}
                for x in set(
                    i['initiator_wwn']
                    for i in self._jsonrequest("export_list")))

        rc_lsm_ags.extend(
            list(
                TargetdStorage._tgt_init_to_lsm(i, self.system.id)
                for i in tgt_inits))

        if self._flag_ag_support is True:
            for tgt_ag in self._jsonrequest('access_group_list'):
                rc_lsm_ags.append(
                    TargetdStorage._tgt_ag_to_lsm(
                        tgt_ag, self.system.id))

        return search_property(rc_lsm_ags, search_key, search_value)

    def _lsm_ag_of_id(self, ag_id, lsm_error_obj=None):
        """
        Raise provided error if defined when not found.
        Return lsm.AccessGroup if found.
        """
        lsm_ags = self.access_groups()
        for lsm_ag in lsm_ags:
            if lsm_ag.id == ag_id:
                return lsm_ag

        if lsm_error_obj:
            raise lsm_error_obj

    @handle_errors
    def access_group_create(self, name, init_id, init_type, system, flags=0):
        if system.id != self.system.id:
            raise LsmError(
                ErrorNumber.NOT_FOUND_SYSTEM,
                "System %s not found" % system.id)
        if self._flag_ag_support is False:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "Please upgrade your targetd package to support "
                "access_group_create()")

        if init_type != AccessGroup.INIT_TYPE_ISCSI_IQN:
            raise LsmError(ErrorNumber.NO_SUPPORT, "Only iSCSI yet")

        self._jsonrequest(
            "access_group_create",
            dict(ag_name=name, init_id=init_id, init_type='iscsi'))

        return self._lsm_ag_of_id(
            name,
            LsmError(
                ErrorNumber.PLUGIN_BUG,
                "access_group_create(): Failed to find the newly created "
                "access group"))

    @handle_errors
    def access_group_initiator_add(self, access_group, init_id, init_type,
                                   flags=0):
        if init_type != AccessGroup.INIT_TYPE_ISCSI_IQN:
            raise LsmError(
                ErrorNumber.NO_SUPPORT, "Targetd only support iscsi")

        lsm_ag = self._lsm_ag_of_id(
            access_group.name,
            LsmError(
                ErrorNumber.NOT_FOUND_ACCESS_GROUP, "Access group not found"))

        # Pre-check for NO_STATE_CHANGE error as targetd silently pass
        # if initiator is already in requested access group.
        if init_id in lsm_ag.init_ids:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Requested init_id is already in defined access group")

        self._jsonrequest(
            "access_group_init_add",
            dict(
                ag_name=access_group.name, init_id=init_id,
                init_type='iscsi'))

        return self._lsm_ag_of_id(
            access_group.name,
            LsmError(
                ErrorNumber.PLUGIN_BUG,
                "access_group_initiator_add(): "
                "Failed to find the updated access group"))

    @handle_errors
    def access_group_initiator_delete(self, access_group, init_id, init_type,
                                      flags=0):
        if init_type != AccessGroup.INIT_TYPE_ISCSI_IQN:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "Targetd only support iscsi")

        # Pre-check for NO_STATE_CHANGE as targetd sliently return
        # when init_id not in requested access_group.
        lsm_ag = self._lsm_ag_of_id(
            access_group.name,
            LsmError(
                ErrorNumber.NOT_FOUND_ACCESS_GROUP, "Access group not found"))

        if init_id not in lsm_ag.init_ids:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Requested initiator is not in defined access group")

        if len(lsm_ag.init_ids) == 1:
            raise LsmError(
                ErrorNumber.LAST_INIT_IN_ACCESS_GROUP,
                "Refused to remove the last initiator from access group")

        self._jsonrequest(
            "access_group_init_del",
            dict(
                ag_name=access_group.name,
                init_id=init_id,
                init_type='iscsi'))

        return self._lsm_ag_of_id(
            access_group.name,
            LsmError(
                ErrorNumber.PLUGIN_BUG,
                "access_group_initiator_delete(): "
                "Failed to find the updated access group"))

    @handle_errors
    def access_group_delete(self, access_group, flags=0):
        if access_group.id.startswith(TargetdStorage._FAKE_AG_PREFIX):
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "Cannot delete old initiator simulated access group, "
                "they will be automatically deleted when no volume masked to")

        if self._flag_ag_support is False:
            raise LsmError(
                ErrorNumber.NO_SUPPORT,
                "Please upgrade your targetd package to support "
                "access_group_delete()")

        self._lsm_ag_of_id(
            access_group.id,
            LsmError(
                ErrorNumber.NOT_FOUND_ACCESS_GROUP,
                "Access group not found"))

        if list(m for m in self._tgt_masks() if m['ag_id'] == access_group.id):
            raise LsmError(
                ErrorNumber.IS_MASKED,
                "Cannot delete access group which has volume masked to")

        self._jsonrequest(
            "access_group_destroy", {'ag_name': access_group.name})
        return None

    def _tgt_masks(self):
        """
        Return a list of tgt_mask:
            {
                'pool_name': pool_name,
                'vol_name': vol_name,
                'ag_id': lsm_ag.id,
                'h_lun_id': h_lun_id,
            }
        """
        tgt_masks = []
        for tgt_exp in self._jsonrequest("export_list"):
            tgt_masks.append({
                'ag_id': "%s%s" % (
                    TargetdStorage._FAKE_AG_PREFIX,
                    md5(tgt_exp['initiator_wwn'])),
                'vol_name': tgt_exp['vol_name'],
                'pool_name': tgt_exp['pool'],
                'h_lun_id': tgt_exp['lun'],
            })
        if self._flag_ag_support:
            for tgt_ag_map in self._jsonrequest("access_group_map_list"):
                tgt_masks.append({
                    'ag_id': tgt_ag_map['ag_name'],
                    'vol_name': tgt_ag_map['vol_name'],
                    'pool_name': tgt_ag_map['pool_name'],
                    'h_lun_id': tgt_ag_map['h_lun_id'],
                })

        return tgt_masks

    def _is_masked(self, ag_id, pool_name, vol_name, tgt_masks=None):
        """
        Check whether volume is masked to certain access group.
        Return True or False
        """
        if tgt_masks is None:
            tgt_masks = self._tgt_masks()
        return list(
            m for m in tgt_masks
            if (m['vol_name'] == vol_name and
                m['pool_name'] == pool_name and
                m['ag_id'] == ag_id)) != []

    def _lsm_vol_of_id(self, vol_id, error=None):
        try:
            return list(v for v in self.volumes() if v.id == vol_id)[0]
        except IndexError:
            if error:
                raise error
            else:
                return None

    @handle_errors
    def volume_mask(self, access_group, volume, flags=0):
        self._lsm_ag_of_id(
            access_group.id,
            LsmError(
                ErrorNumber.NOT_FOUND_ACCESS_GROUP, "Access group not found"))

        self._lsm_vol_of_id(
            volume.id,
            LsmError(
                ErrorNumber.NOT_FOUND_VOLUME, "Volume not found"))

        tgt_masks = self._tgt_masks()
        if self._is_masked(
                access_group.id, volume.pool_id, volume.name, tgt_masks):
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Volume is already masked to requested access group")

        if access_group.id.startswith(TargetdStorage._FAKE_AG_PREFIX):
            free_h_lun_ids = (
                set(range(TargetdStorage._MAX_H_LUN_ID + 1)) -
                set([m['h_lun_id'] for m in tgt_masks]))

            if len(free_h_lun_ids) == 0:
                # TODO(Gris Ge): Add SYSTEM_LIMIT error into API
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "System limit: targetd only allows %s LUN masked" %
                    TargetdStorage._MAX_H_LUN_ID)

            h_lun_id = free_h_lun_ids.pop()

            self._jsonrequest(
                "export_create",
                {
                    'pool': volume.pool_id,
                    'vol': volume.name,
                    'initiator_wwn': access_group.init_ids[0],
                    'lun': h_lun_id
                })
        else:

            self._jsonrequest(
                'access_group_map_create',
                {
                    'pool_name': volume.pool_id,
                    'vol_name': volume.name,
                    'ag_name': access_group.id,
                })

        return None

    @handle_errors
    def volume_unmask(self, access_group, volume, flags=0):
        self._lsm_ag_of_id(
            access_group.id,
            LsmError(
                ErrorNumber.NOT_FOUND_ACCESS_GROUP, "Access group not found"))

        self._lsm_vol_of_id(
            volume.id,
            LsmError(
                ErrorNumber.NOT_FOUND_VOLUME, "Volume not found"))

        # Pre-check if already unmasked
        if not self._is_masked(access_group.id, volume.pool_id, volume.name):
            raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                           "Volume is not masked to requested access group")

        if access_group.id.startswith(TargetdStorage._FAKE_AG_PREFIX):
            self._jsonrequest("export_destroy",
                              dict(pool=volume.pool_id,
                                   vol=volume.name,
                                   initiator_wwn=access_group.init_ids[0]))
        else:
            self._jsonrequest(
                "access_group_map_destroy",
                {
                    'pool_name': volume.pool_id,
                    'vol_name': volume.name,
                    'ag_name': access_group.id,
                })

        return None

    @handle_errors
    def volumes_accessible_by_access_group(self, access_group, flags=0):
        tgt_masks = self._tgt_masks()

        vol_infos = list(
            [m['vol_name'], m['pool_name']]
            for m in tgt_masks
            if m['ag_id'] == access_group.id)

        if len(vol_infos) == 0:
            return []

        rc_lsm_vols = []
        return list(
            lsm_vol
            for lsm_vol in self.volumes(flags=flags)
            if [lsm_vol.name, lsm_vol.pool_id] in vol_infos)

    @handle_errors
    def access_groups_granted_to_volume(self, volume, flags=0):
        tgt_masks = self._tgt_masks()
        ag_ids = list(
            m['ag_id']
            for m in tgt_masks
            if (m['vol_name'] == volume.name and
                m['pool_name'] == volume.pool_id))

        lsm_ags = self.access_groups(flags=flags)
        return [x for x in lsm_ags if x.id in ag_ids]

    def _get_volume(self, pool_id, volume_name):
        vol = [v for v in self._jsonrequest("vol_list", dict(pool=pool_id))
               if v['name'] == volume_name][0]

        vpd83 = TargetdStorage._uuid_to_vpd83(vol['uuid'])
        return Volume(vol['uuid'], vol['name'], vpd83, 512,
                      vol['size'] / 512,
                      Volume.ADMIN_STATE_ENABLED,
                      self.system.id,
                      pool_id)

    def _get_fs(self, pool_id, fs_name):
        fs = self.fs()
        for f in fs:
            if f.name == fs_name and f.pool_id == pool_id:
                return f
        return None

    def _get_ss(self, fs, ss_name):
        ss = self.fs_snapshots(fs)
        for s in ss:
            if s.name == ss_name:
                return s
        return None

    @handle_errors
    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags=0):
        if provisioning != Volume.PROVISION_DEFAULT:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "Unsupported provisioning")

        # Make sure size_bytes round up with _LVM_SECTOR_SIZE
        if size_bytes:
            remainder = size_bytes % _LVM_SECTOR_SIZE
            if remainder:
                size_bytes = size_bytes + _LVM_SECTOR_SIZE - remainder
        else:
            size_bytes = _LVM_SECTOR_SIZE

        self._jsonrequest("vol_create", dict(pool=pool.id,
                                             name=volume_name,
                                             size=size_bytes))

        return None, self._get_volume(pool.id, volume_name)

    @handle_errors
    def volume_delete(self, volume, flags=0):
        self._jsonrequest("vol_destroy",
                          dict(pool=volume.pool_id, name=volume.name))

    @handle_errors
    def volume_replicate(self, pool, rep_type, volume_src, name, flags=0):
        if rep_type != Volume.REPLICATE_COPY:
            raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

        # pool id is optional, use volume src as default
        pool_id = volume_src.pool_id
        if pool:
            pool_id = pool.id

        self._jsonrequest("vol_copy",
                          dict(pool=pool_id, vol_orig=volume_src.name,
                               vol_new=name))

        return None, self._get_volume(pool_id, name)

    @handle_errors
    def iscsi_chap_auth(self, init_id, in_user, in_password, out_user,
                        out_password, flags=0):
        self._jsonrequest("initiator_set_auth",
                          dict(initiator_wwn=init_id,
                               in_user=in_user,
                               in_pass=in_password,
                               out_user=out_user,
                               out_pass=out_password))

        return None

    @handle_errors
    def fs(self, search_key=None, search_value=None, flags=0):
        rc = []
        for fs in self._jsonrequest("fs_list"):
            # self, id, name, total_space, free_space, pool_id, system_id
            rc.append(FileSystem(fs['uuid'], fs['name'], fs['total_space'],
                                 fs['free_space'], fs['pool'], self.system.id))
        return search_property(rc, search_key, search_value)

    @handle_errors
    def fs_delete(self, fs, flags=0):
        self._jsonrequest("fs_destroy", dict(uuid=fs.id))

    @handle_errors
    def fs_create(self, pool, name, size_bytes, flags=0):
        self._jsonrequest("fs_create", dict(pool_name=pool.id, name=name,
                                            size_bytes=size_bytes))

        return None, self._get_fs(pool.name, name)

    @handle_errors
    def fs_clone(self, src_fs, dest_fs_name, snapshot=None, flags=0):

        ss_id = None
        if snapshot:
            ss_id = snapshot.id

        self._jsonrequest("fs_clone", dict(fs_uuid=src_fs.id,
                                           dest_fs_name=dest_fs_name,
                                           snapshot_id=ss_id))

        return None, self._get_fs(src_fs.pool_id, dest_fs_name)

    @handle_errors
    def fs_snapshots(self, fs, flags=0):
        rc = []
        for ss in self._jsonrequest("ss_list", dict(fs_uuid=fs.id)):
            # id, name, timestamp
            rc.append(FsSnapshot(ss['uuid'], ss['name'], ss['timestamp']))
        return rc

    @handle_errors
    def fs_snapshot_create(self, fs, snapshot_name, flags=0):

        self._jsonrequest("fs_snapshot", dict(fs_uuid=fs.id,
                                              dest_ss_name=snapshot_name))

        return None, self._get_ss(fs, snapshot_name)

    @handle_errors
    def fs_snapshot_delete(self, fs, snapshot, flags=0):
        self._jsonrequest("fs_snapshot_delete", dict(fs_uuid=fs.id,
                                                     ss_uuid=snapshot.id))

    @handle_errors
    def export_auth(self, flags=0):
        exports = self._jsonrequest("nfs_export_auth_list")
        return exports

    @staticmethod
    def _get_value(options, key):
        for o in options:
            if '=' in o:
                k, v = o.split('=')
                if k == key:
                    return v
        return None

    @staticmethod
    def _option_string(nfs_options):
        cpy = copy.copy(nfs_options)
        if 'ro' in cpy:
            cpy.remove('ro')
        if 'rw' in cpy:
            cpy.remove('rw')
        if 'no_root_squash' in cpy:
            cpy.remove('no_root_squash')
        if 'root_squash' in cpy:
            cpy.remove('root_squash')

        cpy.sort()
        s = ','.join(cpy)
        return s

    @staticmethod
    def _calculate_export_md5(export_path, options):
        opts = TargetdStorage._option_string(options)
        return md5(export_path + opts)

    @handle_errors
    def exports(self, search_key=None, search_value=None, flags=0):
        tmp_exports = {}
        exports = []
        fs_full_paths = {}
        all_nfs_exports = self._jsonrequest("nfs_export_list")
        nfs_exports = []

        # Remove those that are not of FS origin
        fs_list = self._jsonrequest("fs_list")
        for f in fs_list:
            fs_full_paths[f['full_path']] = f

        for export in all_nfs_exports:
            if export['path'] in fs_full_paths:
                nfs_exports.append(export)

        # Collect like exports to minimize results
        for export in nfs_exports:
            key = export['path'] + \
                TargetdStorage._option_string(export['options'])
            if key in tmp_exports:
                tmp_exports[key].append(export)
            else:
                tmp_exports[key] = [export]

        # Walk through the options
        for le in tmp_exports.values():
            export_id = ""
            root = []
            rw = []
            ro = []
            sec = None
            anonuid = NfsExport.ANON_UID_GID_NA
            anongid = NfsExport.ANON_UID_GID_NA

            options = None

            for export in le:

                host = export['host']
                export_id += host
                export_id += export['path']
                export_id += fs_full_paths[export['path']]['uuid']

                options = export['options']

                if 'rw' in options:
                    rw.append(host)

                if 'ro' in options:
                    ro.append(host)

                sec = TargetdStorage._get_value(options, 'sec')
                if sec is None:
                    sec = 'sys'

                if 'no_root_squash' in options:
                    root.append(host)

                uid = TargetdStorage._get_value(options, 'anonuid')
                if uid is not None:
                    anonuid = uid
                gid = TargetdStorage._get_value(options, 'anongid')
                if gid is not None:
                    anongid = gid

            exports.append(
                NfsExport(TargetdStorage._calculate_export_md5(export['path'],
                                                               options),
                          fs_full_paths[export['path']]['uuid'],
                          export['path'], sec, root, rw, ro, anonuid, anongid,
                          TargetdStorage._option_string(options)))

        return search_property(exports, search_key, search_value)

    def _get_fs_path(self, fs_id):
        for fs in self._jsonrequest("fs_list"):
            if fs_id == fs['uuid']:
                return fs['full_path']
        return None

    @handle_errors
    def export_fs(
            self, fs_id, export_path, root_list, rw_list, ro_list,
            anon_uid=NfsExport.ANON_UID_GID_NA,
            anon_gid=NfsExport.ANON_UID_GID_NA,
            auth_type=None, options=None, flags=0):

        if export_path is not None:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           'export_path required to be None')

        base_opts = []

        if anon_uid is not None:
            base_opts.append('anonuid=%s' % str(anon_uid))

        if anon_gid is not None:
            base_opts.append('anongid=%s' % str(anon_gid))

        if auth_type is not None:
            base_opts.append('sec=%s' % str(auth_type))

        fs_path = self._get_fs_path(fs_id)
        if fs_path is None:
            raise LsmError(ErrorNumber.NOT_FOUND_FS, "File system not found")

        for host in rw_list:
            tmp_opts = copy.copy(base_opts)
            if host in root_list:
                tmp_opts.append('no_root_squash')

            tmp_opts.append('rw')

            self._jsonrequest("nfs_export_add",
                              dict(host=host, path=fs_path,
                                   export_path=None, options=tmp_opts))

        for host in ro_list:
            tmp_opts = copy.copy(base_opts)
            if host in root_list:
                tmp_opts.append('no_root_squash')

            tmp_opts.append('ro')

            self._jsonrequest("nfs_export_add",
                              dict(host=host, path=fs_path,
                                   export_path=None, options=tmp_opts))

        # Kind of a pain to determine which export was newly created as it
        # could get merged into an existing record, doh!
        # Make sure fs_id's match and that one of the hosts is in the
        # record.
        exports = self.exports()
        h = []
        h.extend(rw_list)
        h.extend(ro_list)
        for host in exports:
            if host.fs_id == fs_id:
                l = []
                l.extend(host.ro)
                l.extend(host.rw)
                for host_entry in h:
                    if host_entry in l:
                        return host

        raise LsmError(ErrorNumber.PLUGIN_BUG, "Failed to create export")

    @handle_errors
    def export_remove(self, export, flags=0):

        for host in export.rw:
            params = dict(host=host, path=export.export_path)
            self._jsonrequest("nfs_export_remove", params)

        for host in export.ro:
            params = dict(host=host, path=export.export_path)
            self._jsonrequest("nfs_export_remove", params)

    @staticmethod
    def _default_error_handler(error_code, msg):
        if error_code in TargetdStorage._ERROR_MAPPING:
            ec = TargetdStorage._ERROR_MAPPING[error_code]['ec']
            msg_d = TargetdStorage._ERROR_MAPPING[error_code]['msg']
            if not msg_d:
                msg_d = msg
            raise LsmError(ec, msg_d)

    def _jsonrequest(self, method, params=None, default_error_handler=True):
        data = json.dumps(dict(id=self.rpc_id, method=method,
                               params=params, jsonrpc="2.0"))
        self.rpc_id += 1

        try:
            request = urllib2.Request(self.url, data, self.headers)
            response_obj = urllib2.urlopen(request)
        except socket.error:
            raise LsmError(ErrorNumber.NETWORK_ERROR,
                           "Unable to connect to targetd, uri right?")

        response_data = response_obj.read()
        response = json.loads(response_data)
        if response.get('error', None) is None:
            return response.get('result')
        else:
            if response['error']['code'] <= 0:
                # error_text = "%s:%s" % (str(response['error']['code']),
                #                     response['error'].get('message', ''))

                rc = abs(int(response['error']['code']))
                msg = response['error'].get('message', '')

                # If the caller wants the standard error handling mapping
                if default_error_handler:
                    self._default_error_handler(rc, msg)

                raise TargetdError(rc, msg)
            else:  # +code is async execution id
                # Async completion, polling for results
                async_code = response['error']['code']
                while True:
                    time.sleep(1)
                    results = self._jsonrequest('async_list')
                    status = results.get(str(async_code), None)
                    if status:
                        if status[0]:
                            raise LsmError(
                                ErrorNumber.PLUGIN_BUG,
                                "%d has error %d" % (async_code, status[0]))
