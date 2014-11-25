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
# Author: Andy Grover <agrover at redhat com>
#         Gris Ge <fge@redhat.com>

import copy

from lsm import (Pool, Volume, System, Capabilities,
                 IStorageAreaNetwork, INfs, FileSystem, FsSnapshot, NfsExport,
                 LsmError, ErrorNumber, uri_parse, md5, VERSION,
                 common_urllib2_error_handler, search_property,
                 AccessGroup)

import urllib2
import json
import time
import urlparse
import socket
import re

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

    def __init__(self, errno, reason, *args, **kwargs):
        Exception.__init__(self, *args, **kwargs)
        self.errno = int(errno)
        self.reason = reason


class TargetdStorage(IStorageAreaNetwork, INfs):

    def __init__(self):
        self.uri = None
        self.password = None
        self.tmo = 0
        self.rpc_id = 1
        self.host_with_port = None
        self.scheme = None
        self.url = None
        self.headers = None
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

    @handle_errors
    def access_groups(self, search_key=None, search_value=None, flags=0):
        rc = []
        for init_id in set(i['initiator_wwn']
                           for i in self._jsonrequest("export_list")):
            ag_id = md5(init_id)
            init_type = AccessGroup.INIT_TYPE_ISCSI_IQN
            ag_name = 'N/A'
            init_ids = [init_id]
            rc.extend(
                [AccessGroup(ag_id, ag_name, init_ids, init_type,
                             self.system.id)])
        return search_property(rc, search_key, search_value)

    def _mask_infos(self):
        """
        Return a list of tgt_mask:
            'vol_id': volume.id
            'ag_id': ag.id
            'lun_id': lun_id
        """
        tgt_masks = []
        tgt_exps = self._jsonrequest("export_list")
        for tgt_exp in tgt_exps:
            tgt_masks.extend([{
                'vol_id': tgt_exp['vol_uuid'],
                'ag_id': md5(tgt_exp['initiator_wwn']),
                'lun_id': tgt_exp['lun'],
            }])
        return tgt_masks

    def _is_masked(self, init_id, vol_id):
        """
        Check whether volume is masked to certain initiator.
        Return Tuple (True,_mask_infos)  or (False, _mask_infos)
        """
        ag_id = md5(init_id)
        tgt_mask_infos = self._mask_infos()
        for tgt_mask in tgt_mask_infos:
            if tgt_mask['vol_id'] == vol_id and tgt_mask['ag_id'] == ag_id:
                return True, tgt_mask_infos
        return False, tgt_mask_infos

    def volume_mask(self, access_group, volume, flags=0):
        if len(access_group.init_ids) == 0:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "No member belong to defined access group: %s"
                           % access_group.id)
        if len(access_group.init_ids) != 1:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Targetd does not allowing masking two or more "
                           "initiators to volume")

        if access_group.init_type != AccessGroup.INIT_TYPE_ISCSI_IQN:
            raise LsmError(ErrorNumber.NO_SUPPORT,
                           "Targetd only support ISCSI initiator group type")

        (is_masked, tgt_masks) = self._is_masked(
            access_group.init_ids[0], volume.id)

        if is_masked:
            raise LsmError(
                ErrorNumber.NO_STATE_CHANGE,
                "Volume is already masked to requested access group")

        # find lowest unused lun ID
        used_lun_ids = [x['lun_id'] for x in tgt_masks]
        lun_id = 0
        while True:
            if lun_id in used_lun_ids:
                lun_id += 1
            else:
                break

        self._jsonrequest("export_create",
                          dict(pool=volume.pool_id,
                               vol=volume.name,
                               initiator_wwn=access_group.init_ids[0],
                               lun=lun_id))
        return None

    @handle_errors
    def volume_unmask(self, volume, access_group, flags=0):
        # Pre-check if already unmasked
        if not self._is_masked(access_group.init_ids[0], volume.id)[0]:
            raise LsmError(ErrorNumber.NO_STATE_CHANGE,
                           "Volume is not masked to requested access group")
        else:
            self._jsonrequest("export_destroy",
                              dict(pool=volume.pool_id,
                                   vol=volume.name,
                                   initiator_wwn=access_group.init_ids[0]))
        return None

    @handle_errors
    def volumes_accessible_by_access_group(self, access_group, flags=0):
        tgt_masks = self._mask_infos()
        vol_ids = list(x['vol_id'] for x in tgt_masks
                       if x['ag_id'] == access_group.id)
        lsm_vols = self.volumes(flags=flags)
        return [x for x in lsm_vols if x.id in vol_ids]

    @handle_errors
    def access_groups_granted_to_volume(self, volume, flags=0):
        tgt_masks = self._mask_infos()
        ag_ids = list(x['ag_id'] for x in tgt_masks
                      if x['vol_id'] == volume.id)
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
        try:
            self._jsonrequest("vol_destroy",
                              dict(pool=volume.pool_id, name=volume.name))
        except TargetdError as te:
            if te.errno == TargetdError.VOLUME_MASKED:
                raise LsmError(ErrorNumber.IS_MASKED,
                               "Volume is masked to access group")
            raise

    @handle_errors
    def volume_replicate(self, pool, rep_type, volume_src, name, flags=0):
        if rep_type != Volume.REPLICATE_COPY:
            raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

        #pool id is optional, use volume src as default
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
            #self, id, name, total_space, free_space, pool_id, system_id
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
            #id, name, timestamp
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

        #Remove those that are not of FS origin
        fs_list = self._jsonrequest("fs_list")
        for f in fs_list:
            fs_full_paths[f['full_path']] = f

        for export in all_nfs_exports:
            if export['path'] in fs_full_paths:
                nfs_exports.append(export)

        #Collect like exports to minimize results
        for export in nfs_exports:
            key = export['path'] + \
                TargetdStorage._option_string(export['options'])
            if key in tmp_exports:
                tmp_exports[key].append(export)
            else:
                tmp_exports[key] = [export]

        #Walk through the options
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

        #Kind of a pain to determine which export was newly created as it
        #could get merged into an existing record, doh!
        #Make sure fs_id's match and that one of the hosts is in the
        #record.
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

    def _jsonrequest(self, method, params=None):
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
                #error_text = "%s:%s" % (str(response['error']['code']),
                #                     response['error'].get('message', ''))

                raise TargetdError(abs(int(response['error']['code'])),
                                   response['error'].get('message', ''))
            else:  # +code is async execution id
                #Async completion, polling for results
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
