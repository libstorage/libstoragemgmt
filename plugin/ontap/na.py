# Copyright (C) 2012-2014 Red Hat, Inc.
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

import urllib2
import socket
import sys
from xml.etree import ElementTree
import time
from binascii import hexlify
from _ssl import SSLError

from M2Crypto import RC4

from lsm.external.xmltodict import convert_xml_to_dict
from lsm import (ErrorNumber)


#Set to an appropriate directory and file to dump the raw response.
xml_debug = None


def netapp_filer_parse_response(resp):
    if xml_debug:
        out = open(xml_debug, "wb")
        out.write(resp)
        out.close()

    return convert_xml_to_dict(ElementTree.fromstring(resp))


def param_value(val):
    """
    Given a parameter to pass to filer, convert to XML
    """
    rc = ""
    if type(val) is dict or isinstance(val, dict):
        for k, v in val.items():
            rc += "<%s>%s</%s>" % (k, param_value(v), k)
    elif type(val) is list or isinstance(val, list):
        for i in val:
            rc += param_value(i)
    else:
        rc = val
    return rc


def netapp_filer(host, username, password, timeout, command, parameters=None,
                 ssl=False):
    """
    Issue a command to the NetApp filer.
    Note: Change to default ssl on before we ship a release version.
    """
    proto = 'http'
    if ssl:
        proto = 'https'

    url = "%s://%s/servlets/netapp.servlets.admin.XMLrequest_filer" % \
          (proto, host)

    req = urllib2.Request(url)
    req.add_header('Content-Type', 'text/xml')

    password_manager = urllib2.HTTPPasswordMgrWithDefaultRealm()
    password_manager.add_password(None, url, username, password)
    auth_manager = urllib2.HTTPBasicAuthHandler(password_manager)

    opener = urllib2.build_opener(auth_manager)
    urllib2.install_opener(opener)

    #build the command and the arguments for it
    p = ""

    if parameters:
        for k, v in parameters.items():
            p += "<%s>%s</%s>" % (k, param_value(v), k)

    payload = "<%s>\n%s\n</%s>" % (command, p, command)

    data = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE netapp SYSTEM "file:/etc/netapp_filer.dtd">
<netapp xmlns="http://www.netapp.com/filer/admin" version="1.1">
%s
</netapp>
""" % payload

    handler = None
    rc = None
    try:
        handler = urllib2.urlopen(req, data, float(timeout))

        if handler.getcode() == 200:
            rc = netapp_filer_parse_response(handler.read())
    except urllib2.HTTPError as he:
        raise
    except urllib2.URLError as ue:
        if isinstance(ue.reason, socket.timeout):
            raise FilerError(Filer.ETIMEOUT, "Connection timeout")
        else:
            raise
    except socket.timeout:
        raise FilerError(Filer.ETIMEOUT, "Connection timeout")
    except SSLError as sse:
        # The ssl library doesn't give a good way to find specific reason.
        # We are doing a string contains which is not ideal, but other than
        # throwing a generic error in this case there isn't much we can do
        # to be more specific.
        if "timed out" in str(sse).lower():
            raise FilerError(Filer.ETIMEOUT, "Connection timeout (SSL)")
        else:
            raise FilerError(Filer.EUNKNOWN,
                             "SSL error occurred (%s)", str(sse))
    finally:
        if handler:
            handler.close()

    return rc


class FilerError(Exception):
    """
    Class represents a NetApp bad return code
    """
    IGROUP_NOT_CONTAIN_GIVEN_INIT = 9007
    IGROUP_ALREADY_HAS_INIT = 9008
    NO_SUCH_IGROUP = 9003

    # Using the name from NetApp SDK netapp_errno.h
    EVDISK_ERROR_VDISK_EXISTS = 9012        # LUN name already in use
    EVDISK_ERROR_VDISK_EXPORTED = 9013  # LUN is currently mapped
    EVDISK_ERROR_VDISK_NOT_ENABLED = 9014   # LUN is not online
    EVDISK_ERROR_VDISK_NOT_DISABLED = 9015  # LUN is not offline
    EVDISK_ERROR_NO_SUCH_LUNMAP = 9016      # LUN is already unmapped
    EVDISK_ERROR_INITGROUP_MAPS_EXIST = 9029    # LUN maps for this initiator
                                                # group exist
    EVDISK_ERROR_SIZE_TOO_LARGE = 9034      # LUN size too large.
    EVDISK_ERROR_NO_SUCH_VOLUME = 9036      # NetApp Volume not exists.
    EVDISK_ERROR_SIZE_TOO_SMALL = 9041      # Specified too small a size
    EVDISK_ERROR_SIZE_UNCHANGED = 9042      # requested size is the same.
    EVDISK_ERROR_INITGROUP_HAS_VDISK = 9023     # Already masked

    def __init__(self, errno, reason, *args, **kwargs):
        Exception.__init__(self, *args, **kwargs)
        self.errno = int(errno)
        self.reason = reason


def to_list(v):
    """
    The return values in hash form can either be a single hash item or a list
    of hash items, this code handles both to make callers always get a list.
    """
    rc = []
    if v is not None:
        if isinstance(v, list):
            rc = v
        else:
            rc.append(v)
    return rc


class Filer(object):
    """
    Class to handle NetApp API calls.
    Note: These are using lsm terminology.
    """
    EUNKNOWN = 10                   # Non-specific error
    ENAVOL_NAME_DUPE = 17           # Volume name collision
    ENOSPC = 28                     # Out of space
    ETIMEOUT = 60                   # Time-out
    EINVALID_ISCSI_NAME = 9006      # Invalid ISCSI IQN
    EDUPE_VOLUME_PATH = 9012        # Duplicate volume name
    ENO_SUCH_VOLUME = 9017          # lun not found
    ESIZE_TOO_LARGE = 9034          # Specified too large a size
    ENO_SUCH_FS = 9036              # FS not found
    EVOLUME_TOO_SMALL = 9041        # Specified too small a size
    EAPILICENSE = 13008             # Unlicensed API
    EFSDOESNOTEXIST = 13040         # FS does not exist
    EFSOFFLINE = 13042              # FS is offline.
    EFSNAMEINVALID = 13044          # FS Name invalid
    ENOSPACE = 13062                # Not enough space
    ESERVICENOTLICENSED = 13902     # Not licensed
    ECLONE_NAME_EXISTS = 14952      # Clone with same name exists
    ECLONE_LICENSE_EXPIRED = 14955  # Not licensed
    ECLONE_NOT_LICENSED = 14956     # Not licensed

    (LSM_VOL_PREFIX, LSM_INIT_PREFIX) = ('lsm_lun_container', 'lsm_init_')

    def _invoke(self, command, parameters=None):

        rc = netapp_filer(self.host, self.username, self.password,
                          self.timeout, command, parameters, self.ssl)

        t = rc['netapp']['results']['attrib']

        if t['status'] != 'passed':
            raise FilerError(t['errno'], t['reason'])

        return rc['netapp']['results']

    def __init__(self, host, username, password, timeout, ssl=True):
        self.host = host
        self.username = username
        self.password = password
        self.timeout = timeout
        self.ssl = ssl

    def system_info(self):
        rc = self._invoke('system-get-info')
        return rc['system-info']

    def validate(self):
        #TODO: Validate that everything we need to function is available?
        self._invoke('system-api-list')
        return None

    def disks(self):
        disks = self._invoke('disk-list-info')
        return disks['disk-details']['disk-detail-info']

    def aggregates(self):
        """
        Return a list of aggregates
        """
        pools = self._invoke('aggr-list-info')
        tmp = pools['aggregates']['aggr-info']
        return to_list(tmp)

    def aggregate_volume_names(self, aggr_name):
        """
        Return a list of volume names that are on an aggregate
        """
        vol_names = []
        rc = self._invoke('aggr-list-info', {'aggregate': aggr_name})

        aggr = rc['aggregates']['aggr-info']

        if aggr is not None and aggr['volumes'] is not None:
            vols = aggr['volumes']['contained-volume-info']
            vol_names = [e['name'] for e in to_list(vols)]
        return vol_names

    def lun_build_name(self, volume_name, file_name):
        """
        Given a volume name and file return full path"
        """
        return '/vol/%s/%s' % (volume_name, file_name)

    def luns_get_specific(self, aggr, na_lun_name=None, na_volume_name=None):
        """
        Return all logical units, or information about one or for all those
        on a volume name.
        """
        rc = []

        if na_lun_name is not None:
            luns = self._invoke('lun-list-info', {'path': na_lun_name})
        elif na_volume_name is not None:
            luns = self._invoke('lun-list-info',
                                {'volume-name': na_volume_name})
        else:
            luns = self._invoke('lun-list-info')

        return to_list(luns['luns']['lun-info'])

    def _get_aggr_info(self):
        aggrs = self._invoke('aggr-list-info')
        tmp = to_list(aggrs['aggregates']['aggr-info'])
        return [x for x in tmp if x['volumes'] is not None]

    def luns_get_all(self):
        """
        Return all lun-info
        """
        try:
            return to_list(self._invoke('lun-list-info')['luns']['lun-info'])
        except TypeError:
            # No LUN found.
            return []

    def lun_min_size(self):
        return self._invoke('lun-get-minsize', {'type': 'image'})['min-size']

    def lun_create(self, full_path_name, size_bytes, flag_thin=False):
        """
        Creates a lun
        If flag_thin set to True, will set 'space-reservation-enabled' as
        'false' which means "create a LUN without any space being reserved".
        """
        params = {'path': full_path_name,
                  'size': size_bytes}
        if flag_thin is True:
            params['space-reservation-enabled'] = 'false'

        self._invoke('lun-create-by-size', params)

    def lun_delete(self, lun_path):
        """
        Deletes a lun given a lun path
        """
        self._invoke('lun-destroy', {'path': lun_path})

    def lun_resize(self, lun_path, size_bytes):
        """
        Re-sizes a lun
        """
        self._invoke('lun-resize', {'path': lun_path, 'size': size_bytes,
                                    'force': 'true'})

    def volume_resize(self, na_vol_name, size_diff_kb):
        """
        Given a NetApp volume name and a size change in kb, re-size the
        NetApp volume.
        """
        params = {'volume': na_vol_name}

        if size_diff_kb > 0:
            params['new-size'] = '+' + str(size_diff_kb) + 'k'
        else:
            params['new-size'] = str(size_diff_kb) + 'k'

        self._invoke('volume-size', params)
        return None

    def volumes(self, volume_name=None):
        """
        Return a list of NetApp volumes
        """
        if not volume_name:
            v = self._invoke('volume-list-info')
        else:
            v = self._invoke('volume-list-info', {'volume': volume_name})

        t = v['volumes']['volume-info']
        rc = to_list(t)
        return rc

    def volume_create(self, aggr_name, vol_name, size_in_bytes):
        """
        Creates a volume given an aggr_name, volume name and size in bytes.
        """
        params = {'containing-aggr-name': aggr_name,
                  'size': int(size_in_bytes * 1.30),
                  #There must be a better way to account for this
                  'volume': vol_name}

        self._invoke('volume-create', params)

        #Turn off scheduled snapshots
        self._invoke('volume-set-option', {'volume': vol_name,
                                           'option-name': 'nosnap',
                                           'option-value': 'on', })

        #Turn off auto export!
        self.nfs_export_remove(['/vol/' + vol_name])

    def volume_clone(self, src_volume, dest_volume, snapshot=None):
        """
        Clones a volume given a source volume name, destination volume name
        and optional backing snapshot.
        """
        params = {'parent-volume': src_volume, 'volume': dest_volume}
        if snapshot:
            params['parent-snapshot'] = snapshot.name
        self._invoke('volume-clone-create', params)

    def volume_delete(self, vol_name):
        """
        Deletes a volume and everything on it.
        """
        online = False

        try:
            self._invoke('volume-offline', {'name': vol_name})
            online = True
        except FilerError as f_error:
            if f_error.errno != Filer.EFSDOESNOTEXIST:
                raise

        try:
            self._invoke('volume-destroy', {'name': vol_name})
        except FilerError as f_error:
            #If the volume was online, we will return it to same status
            # Store the original exception information
            exception_info = sys.exc_info()

            if online:
                try:
                    self._invoke('volume-online', {'name': vol_name})
                except FilerError:
                    pass
            raise exception_info[1], None, exception_info[2]

    def volume_names(self):
        """
        Return a list of volume names
        """
        vols = self.volumes()
        return [v['name'] for v in vols]

    def clone(self, source_path, dest_path, backing_snapshot=None,
              ranges=None):
        """
        Creates a file clone
        """
        params = {'source-path': source_path}

        #You can have source == dest, but if you do you can only specify source
        if source_path != dest_path:
            params['destination-path'] = dest_path

        if backing_snapshot:
            raise FilerError(ErrorNumber.NO_SUPPORT,
                             "Support for backing luns not implemented "
                             "for this API version")
            #params['snapshot-name']= backing_snapshot

        if ranges:
            block_ranges = []
            for r in ranges:
                values = {'block-count': r.block_count,
                          'destination-block-number': r.dest_block,
                          'source-block-number': r.src_block}

                block_ranges.append({'block-range': values})

            params['block-ranges'] = block_ranges

        rc = self._invoke('clone-start', params)

        c_id = rc['clone-id']

        while True:
            progress = self._invoke('clone-list-status',
                                    {'clone-id': c_id})

            # According to the spec the output is optional, if not present
            # then we are done and good
            if 'status' in progress:
                progress = progress['status']['ops-info']

                if progress['clone-state'] == 'failed':
                    self._invoke('clone-clear', {'clone-id': c_id})
                    raise FilerError(progress['error'], progress['reason'])
                elif progress['clone-state'] == 'running' \
                        or progress['clone-state'] == 'fail exit':
                    # State needs to transition to failed before we can
                    # clear it!
                    time.sleep(0.2)     # Don't hog cpu
                elif progress['clone-state'] == 'completed':
                    return
                else:
                    raise FilerError(ErrorNumber.NO_SUPPORT,
                                     'Unexpected state=' +
                                     progress['clone-state'])
            else:
                return

    def lun_online(self, lun_path):
        self._invoke('lun-online', {'path': lun_path})

    def lun_offline(self, lun_path):
        self._invoke('lun-offline', {'path': lun_path})

    def igroups(self, group_name=None):
        rc = []

        if group_name:
            g = self._invoke('igroup-list-info',
                             {'initiator-group-name': group_name})
        else:
            g = self._invoke('igroup-list-info')

        if g['initiator-groups']:
            rc = to_list(g['initiator-groups']['initiator-group-info'])
        return rc

    def igroup_create(self, name, igroup_type):
        params = {'initiator-group-name': name,
                  'initiator-group-type': igroup_type}
        self._invoke('igroup-create', params)

    def igroup_delete(self, name):
        self._invoke('igroup-destroy', {'initiator-group-name': name})

    @staticmethod
    def encode(password):
        rc4 = RC4.RC4()
        rc4.set_key("#u82fyi8S5\017pPemw")
        return hexlify(rc4.update(password))

    def iscsi_initiator_add_auth(self, initiator, user_name, password,
                                 out_user, out_password):
        pw = self.encode(password)

        args = {'initiator': initiator}

        if user_name and len(user_name) and password and len(password):
            args.update({'user-name': user_name,
                         'password': pw, 'auth-type': "CHAP"})

            if out_user and len(out_user) and \
                    out_password and len(out_password):
                args.update({'outbound-user-name': out_user,
                             'outbound-password': out_password})
        else:
            args.update({'initiator': initiator, 'auth-type': "none"})

        self._invoke('iscsi-initiator-add-auth', args)

    def igroup_add_initiator(self, ig, initiator):
        self._invoke('igroup-add',
                     {'initiator-group-name': ig, 'initiator': initiator})

    def igroup_del_initiator(self, ig, initiator):
        self._invoke('igroup-remove',
                     {'initiator-group-name': ig,
                      'initiator': initiator,
                      'force': 'true'})

    def lun_map(self, igroup, lun_path):
        self._invoke('lun-map', {'initiator-group': igroup, 'path': lun_path})

    def lun_unmap(self, igroup, lun_path):
        self._invoke(
            'lun-unmap', {'initiator-group': igroup, 'path': lun_path})

    def lun_map_list_info(self, lun_path):
        initiator_groups = []
        rc = self._invoke('lun-map-list-info', {'path': lun_path})
        if rc['initiator-groups'] is not None:
            igi = to_list(rc['initiator-groups'])
            for i in igi:
                group_name = i['initiator-group-info']['initiator-group-name']
                initiator_groups.append(self.igroups(group_name)[0])

        return initiator_groups

    def lun_initiator_list_map_info(self, initiator_id, initiator_group_name):
        """
        Given an initiator_id and initiator group name, return a list of
        lun-info
        """
        luns = []

        rc = self._invoke('lun-initiator-list-map-info',
                          {'initiator': initiator_id})

        if rc['lun-maps']:

            lun_name_list = to_list(rc['lun-maps']['lun-map-info'])

            #Get all the lun with information about aggr
            all_luns = self.luns_get_all()

            for l in lun_name_list:
                if l['initiator-group'] == initiator_group_name:
                    for al in all_luns:
                        if al['path'] == l['path']:
                            luns.append(al)
        return luns

    def snapshots(self, volume_name):
        rc = []
        args = {'target-type': 'volume', 'target-name': volume_name}
        ss = self._invoke('snapshot-list-info', args)
        if ss['snapshots']:
            rc = to_list(ss['snapshots']['snapshot-info'])
        return rc

    def snapshot_create(self, volume_name, snapshot_name):
        self._invoke('snapshot-create', {'volume': volume_name,
                                         'snapshot': snapshot_name})
        return [v for v in self.snapshots(volume_name)
                if v['name'] == snapshot_name][0]

    def snapshot_file_restore_num(self):
        """
        Returns the number of executing file restore snapshots.
        """
        rc = self._invoke('snapshot-restore-file-info')
        if 'sfsr-in-progress' in rc:
            return int(rc['sfsr-in-progress'])

        return 0

    def snapshot_restore_volume(self, fs_name, snapshot_name):
        """
        Restores all files on a volume
        """
        params = {'snapshot': snapshot_name, 'volume': fs_name}
        self._invoke('snapshot-restore-volume', params)

    def snapshot_restore_file(self, snapshot_name, restore_path, restore_file):
        """
        Restore a list of files
        """
        params = {'snapshot': snapshot_name, 'path': restore_path}

        if restore_file:
            params['restore-path'] = restore_file

        self._invoke('snapshot-restore-file', params)

    def snapshot_delete(self, volume_name, snapshot_name):
        self._invoke('snapshot-delete',
                     {'volume': volume_name, 'snapshot': snapshot_name})

    def export_auth_types(self):
        rc = self._invoke('nfs-get-supported-sec-flavors')
        return [e['flavor'] for e in
                to_list(rc['sec-flavor']['sec-flavor-info'])]

    @staticmethod
    def _build_list(pylist, list_name, elem_name):
        """
        Given a python list, build the appropriate dict that contains the
        list items so that it can be converted to xml to be sent on the wire.
        """
        return [{list_name: {elem_name: l}} for l in pylist]

    @staticmethod
    def _build_export_fs_all():
        return Filer._build_list(
            ['true'], 'exports-hostname-info', 'all-hosts')

    @staticmethod
    def _build_export_fs_list(hosts):
        if hosts[0] == '*':
            return Filer._build_export_fs_all()
        else:
            return Filer._build_list(hosts, 'exports-hostname-info', 'name')

    def _build_export_rules(self, volume_path, export_path, ro_list, rw_list,
                            root_list, anonuid=None, sec_flavor=None):
        """
        Common logic to build up the rules for nfs
        """

        #One of the more complicated data structures to push down to the
        #controller
        rule = {'pathname': volume_path}
        if volume_path != export_path:
            rule['actual-pathname'] = volume_path
            rule['pathname'] = export_path

        rule['security-rules'] = {}
        rule['security-rules']['security-rule-info'] = {}

        r = rule['security-rules']['security-rule-info']

        if len(ro_list):
            r['read-only'] = Filer._build_export_fs_list(ro_list)

        if len(rw_list):
            r['read-write'] = Filer._build_export_fs_list(rw_list)

        if len(root_list):
            r['root'] = Filer._build_export_fs_list(root_list)

        if anonuid:
            uid = long(anonuid)
            if uid != -1 and uid != 0xFFFFFFFFFFFFFFFF:
                r['anon'] = str(uid)

        if sec_flavor:
            r['sec-flavor'] = Filer._build_list(
                [sec_flavor], 'sec-flavor-info', 'flavor')

        return rule

    def nfs_export_fs2(self, volume_path, export_path, ro_list, rw_list,
                       root_list, anonuid=None, sec_flavor=None):
        """
        NFS export a volume.
        """

        rule = self._build_export_rules(
            volume_path, export_path, ro_list, rw_list, root_list, anonuid,
            sec_flavor)

        params = {'persistent': 'true',
                  'rules': {'exports-rule-info-2': [rule]}, 'verbose': 'true'}
        self._invoke('nfs-exportfs-append-rules-2', params)

    def nfs_export_fs_modify2(self, volume_path, export_path, ro_list, rw_list,
                              root_list, anonuid=None, sec_flavor=None):

        """
        Modifies an existing rule.
        """
        rule = self._build_export_rules(
            volume_path, export_path, ro_list, rw_list, root_list, anonuid,
            sec_flavor)

        params = {
            'persistent': 'true', 'rule': {'exports-rule-info-2': [rule]}}
        self._invoke('nfs-exportfs-modify-rule-2', params)

    def nfs_export_remove(self, export_paths):
        """
        Removes an existing export
        """
        assert (type(export_paths) is list)
        paths = Filer._build_list(export_paths, 'pathname-info', 'name')
        self._invoke('nfs-exportfs-delete-rules',
                     {'pathnames': paths, 'persistent': 'true'})

    def nfs_exports(self):
        """
        Returns a list of exports (in hash form)
        """
        rc = []
        exports = self._invoke('nfs-exportfs-list-rules')
        if 'rules' in exports and exports['rules']:
            rc = to_list(exports['rules']['exports-rule-info'])
        return rc

    def volume_children(self, volume):
        params = {'volume': volume}

        rc = self._invoke('volume-list-info', params)

        if 'clone-children' in rc['volumes']['volume-info']:
            tmp = rc['volumes']['volume-info']['clone-children'][
                'clone-child-info']
            rc = [c['clone-child-name'] for c in to_list(tmp)]
        else:
            rc = None

        return rc

    def volume_split_clone(self, volume):
        self._invoke('volume-clone-split-start', {'volume': volume})

    def volume_split_status(self):
        result = []

        rc = self._invoke('volume-clone-split-status')

        if 'clone-split-details' in rc:
            tmp = rc['clone-split-details']['clone-split-detail-info']
            result = [r['name'] for r in to_list(tmp)]

        return result

    def fcp_list(self):
        fcp_list = []

        try:

            rc = self._invoke('fcp-adapter-list-info')

            if 'fcp-config-adapters' in rc:
                if 'fcp-config-adapter-info' in rc['fcp-config-adapters']:
                    fc_config = rc['fcp-config-adapters']
                    adapters = fc_config['fcp-config-adapter-info']
                    for f in adapters:
                        fcp_list.append(dict(addr=f['port-name'],
                                             adapter=f['adapter']))
        except FilerError as na:
            if na.errno != Filer.EAPILICENSE:
                raise

        return fcp_list

    def iscsi_node_name(self):
        try:
            rc = self._invoke('iscsi-node-get-name')
            if 'node-name' in rc:
                return rc['node-name']
        except FilerError as na:
            if na.errno != Filer.EAPILICENSE:
                raise
        return None

    def interface_get_infos(self):
        i_info = {}

        rc = self._invoke('net-ifconfig-get')

        if 'interface-config-info' in rc:
            i_config = rc['interface-config-info']
            if 'interface-config-info' in i_config:
                tmp = to_list(i_config['interface-config-info'])
                for i in tmp:
                    i_info[i['interface-name']] = i

        return i_info

    def iscsi_list(self):
        i_list = []

        # Get interface information
        i_info = self.interface_get_infos()

        try:
            rc = self._invoke('iscsi-portal-list-info')

            if 'iscsi-portal-list-entries' in rc:
                portal_entries = rc['iscsi-portal-list-entries']
                if 'iscsi-portal-list-entry-info' in portal_entries:
                    tmp = portal_entries['iscsi-portal-list-entry-info']
                    portals = to_list(tmp)
                    for p in portals:
                        mac = i_info[p['interface-name']]['mac-address']
                        i_list.append(dict(interface=p['interface-name'],
                                           ip=p['ip-address'],
                                           port=p['ip-port'],
                                           mac=mac))
        except FilerError as na:
            if na.errno != Filer.EAPILICENSE:
                raise

        return i_list

if __name__ == '__main__':
    try:
        #TODO: Need some unit test code
        pass

    except FilerError as fe:
        print 'Errno=', fe.errno, 'reason=', fe.reason
