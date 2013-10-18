#!/usr/bin/env python

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
# Author: Andy Grover <agrover at redhat com>

import copy

from lsm.iplugin import IStorageAreaNetwork, INfs
from lsm.data import (Pool, Volume, System, Capabilities, Initiator,
                      FileSystem, Snapshot, NfsExport)
from lsm.common import (LsmError, ErrorNumber, uri_parse, md5, Error)

import traceback
import urllib2
import json
import time
import urlparse
import socket
from lsm.version import VERSION

DEFAULT_USER = "admin"
DEFAULT_PORT = 18700
PATH = "/targetrpc"


def handle_errors(method):
    def target_wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except urllib2.HTTPError as he:
            raise LsmError(ErrorNumber.PLUGIN_AUTH_FAILED, str(he))
        except urllib2.URLError as ue:
            Error("Unexpected exception:\n" + traceback.format_exc())
            raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION, str(ue))
        except Exception as e:
            Error("Unexpected exception:\n" + traceback.format_exc())
            raise e
    return target_wrapper


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
                             System.STATUS_UNKNOWN)

    @handle_errors
    def startup(self, uri, password, timeout, flags=0):
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
    def set_time_out(self, ms, flags=0):
        self.tmo = ms

    @handle_errors
    def get_time_out(self, flags=0):
        return self.tmo

    @handle_errors
    def shutdown(self, flags=0):
        pass

    @handle_errors
    def capabilities(self, system, flags=0):
        cap = Capabilities()
        cap.set(Capabilities.BLOCK_SUPPORT)
        cap.set(Capabilities.FS_SUPPORT)
        cap.set(Capabilities.VOLUMES)
        cap.set(Capabilities.VOLUME_CREATE)
        cap.set(Capabilities.VOLUME_REPLICATE)
        cap.set(Capabilities.VOLUME_REPLICATE_COPY)
        cap.set(Capabilities.VOLUME_DELETE)
        cap.set(Capabilities.VOLUME_OFFLINE)
        cap.set(Capabilities.VOLUME_ONLINE)
        cap.set(Capabilities.INITIATORS)
        cap.set(Capabilities.VOLUME_INITIATOR_GRANT)
        cap.set(Capabilities.VOLUME_INITIATOR_REVOKE)
        cap.set(Capabilities.VOLUME_ACCESSIBLE_BY_INITIATOR)
        cap.set(Capabilities.INITIATORS_GRANTED_TO_VOLUME)
        cap.set(Capabilities.FS)
        cap.set(Capabilities.FS_CREATE)
        cap.set(Capabilities.FS_DELETE)
        cap.set(Capabilities.FS_CLONE)
        cap.set(Capabilities.FS_SNAPSHOT_CREATE)
        cap.set(Capabilities.FS_SNAPSHOT_DELETE)
        cap.set(Capabilities.FS_SNAPSHOTS)
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

    @handle_errors
    def volumes(self, flags=0):
        volumes = []
        for p_name in (p['name'] for p in self._jsonrequest("pool_list") if
                       p['type'] == 'block'):
            for vol in self._jsonrequest("vol_list", dict(pool=p_name)):
                volumes.append(
                    Volume(vol['uuid'], vol['name'], vol['uuid'],
                           512, vol['size'] / 512,
                           Volume.STATUS_OK,
                           self.system.id, p_name))
        return volumes

    @handle_errors
    def pools(self, flags=0):
        pools = []
        for pool in self._jsonrequest("pool_list"):
            pools.append(Pool(pool['name'], pool['name'], pool['size'],
                              pool['free_size'], 'targetd'))
        return pools

    @handle_errors
    def initiators(self, flags=0):
        inits = []
        for init in set(i['initiator_wwn']
                        for i in self._jsonrequest("export_list")):
            inits.append(Initiator(init, Initiator.TYPE_ISCSI, init))

        return inits

    def _get_volume(self, pool_id, volume_name):
        vol = [v for v in self._jsonrequest("vol_list", dict(pool=pool_id))
               if v['name'] == volume_name][0]

        return Volume(vol['uuid'], vol['name'], vol['uuid'],
                      512, vol['size'] / 512, Volume.STATUS_OK,
                      self.system.id, pool_id)

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

        #pool id is optional, use volume src as default
        pool_id = volume_src.pool_id
        if pool:
            pool_id = pool.id

        self._jsonrequest("vol_copy",
                          dict(pool=pool_id, vol_orig=volume_src.name,
                               vol_new=name))

        return None, self._get_volume(pool_id, name)

    @handle_errors
    def volume_replicate_range_block_size(self, system, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def volume_replicate_range(self, rep_type, volume_src, volume_dest,
                               ranges, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def volume_online(self, volume, flags=0):
        vol_list = self._jsonrequest("vol_list", dict(pool=volume.pool_id))

        return volume.name in [vol['name'] for vol in vol_list]

    @handle_errors
    def volume_offline(self, volume, flags=0):
        return not self.volume_online(volume)

    @handle_errors
    def volume_resize(self, volume, new_size_bytes, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def access_group_grant(self, group, volume, access, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def access_group_revoke(self, group, volume, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def access_group_list(self, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def access_group_create(self, name, initiator_id, id_type, system_id,
                            flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def access_group_del(self, group, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def access_group_add_initiator(self, group, initiator_id, id_type,
                                   flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def access_group_del_initiator(self, group, initiator, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def volumes_accessible_by_access_group(self, group, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def access_groups_granted_to_volume(self, volume, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def iscsi_chap_auth(self, initiator, in_user, in_password, out_user,
                        out_password, flags=0):
        self._jsonrequest("initiator_set_auth",
                          dict(initiator_wwn=initiator.id,
                               in_user=in_user,
                               in_pass=in_password,
                               out_user=out_user,
                               out_pass=out_password))

        return None

    @handle_errors
    def initiator_grant(self, initiator_id, initiator_type, volume, access,
                        flags=0):
        if initiator_type != Initiator.TYPE_ISCSI:
            raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

        # find lowest unused lun and use that
        used_luns = [x['lun'] for x in self._jsonrequest("export_list")]
        lun = 0
        while True:
            if lun in used_luns:
                lun += 1
            else:
                break

        self._jsonrequest("export_create",
                          dict(pool=volume.pool_id,
                               vol=volume.name,
                               initiator_wwn=initiator_id, lun=lun))

    @handle_errors
    def initiator_revoke(self, initiator, volume, flags=0):
        self._jsonrequest("export_destroy",
                          dict(pool=volume.pool_id,
                               vol=volume.name,
                               initiator_wwn=initiator.id))

    @handle_errors
    def volumes_accessible_by_initiator(self, initiator, flags=0):
        exports = [x for x in self._jsonrequest("export_list")
                   if initiator.id == x['initiator_wwn']]

        vols = []
        for export in exports:
            vols.append(Volume(export['vol_uuid'], export['vol_name'],
                               export['vol_uuid'], 512,
                               export['vol_size'] / 512,
                               Volume.STATUS_OK, self.system.id,
                               export['pool']))

        return vols

    @handle_errors
    def initiators_granted_to_volume(self, volume, flags=0):
        exports = [x for x in self._jsonrequest("export_list")
                   if volume.id == x['vol_uuid']]

        inits = []
        for export in exports:
            name = export['initiator_wwn']
            inits.append(Initiator(name, Initiator.TYPE_ISCSI, name))

        return inits

    @handle_errors
    def volume_child_dependency(self, volume, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def volume_child_dependency_rm(self, volume, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def fs(self, flags=0):
        rc = []
        for fs in self._jsonrequest("fs_list"):
            #self, id, name, total_space, free_space, pool_id, system_id
            rc.append(FileSystem(fs['uuid'], fs['name'], fs['total_space'],
                                 fs['free_space'], fs['pool'],
                                 self.system.id))
        return rc

    @handle_errors
    def fs_delete(self, fs, flags=0):
        self._jsonrequest("fs_destroy", dict(uuid=fs.id))

    @handle_errors
    def fs_resize(self, fs, new_size_bytes, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

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
    def file_clone(self, fs, src_file_name, dest_file_name, snapshot=None,
                   flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def fs_snapshots(self, fs, flags=0):
        rc = []
        for ss in self._jsonrequest("ss_list", dict(fs_uuid=fs.id)):
            #id, name, timestamp
            rc.append(Snapshot(ss['uuid'], ss['name'], ss['timestamp']))
        return rc

    @handle_errors
    def fs_snapshot_create(self, fs, snapshot_name, files, flags=0):

        self._jsonrequest("fs_snapshot", dict(fs_uuid=fs.id,
                                              dest_ss_name=snapshot_name))

        return None, self._get_ss(fs, snapshot_name)

    @handle_errors
    def fs_snapshot_delete(self, fs, snapshot, flags=0):
        self._jsonrequest("fs_snapshot_delete", dict(fs_uuid=fs.id,
                                                     ss_uuid=snapshot.id))

    @handle_errors
    def fs_snapshot_revert(self, fs, snapshot, files, restore_files,
                           all_files=False, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def fs_child_dependency(self, fs, files, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    @handle_errors
    def fs_child_dependency_rm(self, fs, files, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

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
    def exports(self, flags=0):
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

            exports.append(NfsExport(
                TargetdStorage._calculate_export_md5(export['path'],
                                                     options),
                fs_full_paths[export['path']]['uuid'],
                export['path'], sec, root, rw, ro, anonuid,
                anongid, TargetdStorage._option_string(options)))

        return exports

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
            raise LsmError(ErrorNumber.INVALID_FS, "File system not found")

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
                for host in h:
                    if host in l:
                        return host

        raise LsmError(ErrorNumber.PLUGIN_ERROR, "Failed to create export")

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
            raise LsmError(ErrorNumber.NO_CONNECT,
                           "Unable to connect to targetd, uri right?")

        response_data = response_obj.read()
        response = json.loads(response_data)
        if response.get('error', None) is None:
            return response.get('result')
        else:
            if response['error']['code'] <= 0:
                #error_text = "%s:%s" % (str(response['error']['code']),
                #                     response['error'].get('message', ''))

                raise LsmError(abs(int(response['error']['code'])),
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
                                ErrorNumber.INTERNAL_ERROR,
                                "%d has error %d" % (async_code, status[0]))
