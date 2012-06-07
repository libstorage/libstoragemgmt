# Copyright (C) 2012 Red Hat, Inc.
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
from xml.etree import ElementTree
import time
from external.xmltodict import ConvertXmlToDict

#Set to an appropriate directory and file to dump the raw response.
from lsm.common import ErrorNumber, LsmError

xml_debug = None



def netapp_filer_parse_response(resp):
    if xml_debug:
        out = open(xml_debug, "wb")
        out.write(resp)
        out.close()

    return ConvertXmlToDict(ElementTree.fromstring(resp))

def param_value(val):
    """
    Given a parameter to pass to filer, convert to XML
    """
    rc = ""
    if type(val) is dict or isinstance(val, dict):
        for k,v in val.items():
            rc += "<%s>%s</%s>" % (k, param_value(v), k)
    elif type(val) is list or isinstance(val, list):
        for i in val:
            rc += param_value(i)
    else:
        rc = val
    return rc

def netapp_filer(host, username, password, command, parameters = None, ssl=False):
    """
    Issue a command to the NetApp filer.
    Note: Change to default ssl on before we ship a release version.
    """
    proto = 'http'
    if ssl:
        proto = 'https'

    url = "%s://%s/servlets/netapp.servlets.admin.XMLrequest_filer" % (proto, host)
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
        for k,v in parameters.items():
            p += "<%s>%s</%s>" % (k, param_value(v) ,k)

    payload = "<%s>\n%s\n</%s>" % (command,p,command)

    data = """<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE netapp SYSTEM "file:/etc/netapp_filer.dtd">
<netapp xmlns="http://www.netapp.com/filer/admin" version="1.1">
%s
</netapp>
""" % payload

    handler = urllib2.urlopen(req, data)

    rc = None
    if handler.getcode() == 200:
        rc = netapp_filer_parse_response(handler.read())

    handler.close()

    return rc

class FilerError(Exception):
    """
    Class represents a NetApp bad return code
    """
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
    ENOSPC = 28                     #Out of space
    ENO_SUCH_VOLUME = 9017          #lun not found
    ESIZE_TOO_LARGE = 9034          #Specified too large a size
    ENO_SUCH_FS = 9036              #FS not found
    EVOLUME_TOO_SMALL = 9041        #Specified too small a size
    EAPILICENSE = 13008             #Unlicensed API
    EFSDOESNOTEXIST = 13040         #FS does not exist
    EFSOFFLINE = 13042              #FS is offline.
    EFSNAMEINVALID = 13044          #FS Name invalid
    ESERVICENOTLICENSED = 13902     #Not licensed
    ECLONE_LICENSE_EXPIRED = 14955  #Not licensed
    ECLONE_NOT_LICENSED = 14956     #Not licensed


    def _invoke(self, command, parameters = None):

        rc = netapp_filer(self.host, self.username, self.password,
            command, parameters, self.ssl)

        t = rc['netapp']['results']['attrib']

        if t['status'] != 'passed':
            raise FilerError(t['errno'],t['reason'])

        return rc['netapp']['results']

    def __init__(self, host, username, password, ssl=True):
        self.host = host
        self.username = username
        self.password = password
        self.ssl = ssl

    def system_info(self):
        rc = self._invoke('system-get-info')
        return rc['system-info']

    def validate(self):
        #TODO: Validate that everything we need to function is available?
        self._invoke('system-api-list')
        return None

    def aggregates(self):
        """
        Return a list of aggregates
        """
        pools = self._invoke('aggr-list-info')
        tmp = pools['aggregates']['aggr-info']
        return [ p for p in to_list(tmp) if p['mount-state'] == 'online']

    def aggregate_volume_names(self, aggr_name):
        """
        Return a list of volume names that are on an aggregate
        """
        vol_names = []
        rc = self._invoke('aggr-list-info', { 'aggregate':aggr_name })

        aggr = rc['aggregates']['aggr-info']

        if  aggr is not None and aggr['volumes'] is not None:
            vols = aggr['volumes']['contained-volume-info']
            vol_names = [ e['name'] for e in to_list(vols) ]
        return vol_names

    def lun_build_name(self, volume_name, file_name):
        """
        Given a volume name and file return full path"
        """
        return '/vol/%s/%s' % (volume_name, file_name )

    def luns(self, na_lun_name=None, na_volume_name=None):
        """
        Return all logical units, or information about one or for all those
        on a volume name.
        """
        rc = []

        if na_lun_name is not None:
            luns = self._invoke('lun-list-info', {'path': na_lun_name} )
        elif na_volume_name is not None:
            luns = self._invoke('lun-list-info', {'volume-name':na_volume_name})
        else:
            luns = self._invoke('lun-list-info')

        tmp = luns['luns']

        if tmp is not None:
            rc = to_list(tmp['lun-info'])

        return rc

    def lun_create(self, full_path_name, size_bytes):
        """
        Creates a lun
        """
        params = { 'path': full_path_name ,
                   'size' : size_bytes}

        self._invoke('lun-create-by-size', params)

    def lun_delete(self, lun_path):
        """
        Deletes a lun given a lun path
        """
        self._invoke('lun-destroy', { 'path':lun_path })

    def lun_resize(self, lun_path, size_bytes):
        """
        Re-sizes a lun
        """
        self._invoke('lun-resize', {'path':lun_path, 'size':size_bytes,
                                    'force':'true'})

    def volume_resize(self,  na_vol_name, size_diff):
        """
        Given a NetApp volume name and a size change in bytes, re-size the
        NetApp volume.
        """
        params = { 'volume':na_vol_name }

        #Pad the increase for snapshot stuff
        size_diff = int((size_diff/1024) * 1.3)

        if size_diff > 0:
            params['new-size'] = '+' + str(size_diff)+'k'
        else:
            params['new-size'] = str(size_diff)+'k'

        self._invoke('volume-size', params)
        return None

    def volumes(self, volume_name = None):
        """
        Return a list of NetApp volumes
        """
        if not volume_name:
            v = self._invoke('volume-list-info')
        else:
            v = self._invoke('volume-list-info', { 'volume':volume_name} )

        t = v['volumes']['volume-info']
        rc = to_list(t)
        return rc

    def volume_create(self, aggr_name, vol_name, size_in_bytes):
        """
        Creates a volume given an aggr_name, volume name and size in bytes.
        """
        params = { 'containing-aggr-name': aggr_name,
                   'size':int(size_in_bytes * 1.30),  #There must be a better way to account for this
                   'volume': vol_name}

        self._invoke('volume-create', params)

        #Turn off scheduled snapshots
        self._invoke('volume-set-option', {'volume':vol_name,
                                           'option-name':'nosnap',
                                           'option-value':'on', } )

        #Turn off auto export!
        self.nfs_export_remove(['/vol/' + vol_name])

    def volume_clone(self, src_volume, dest_volume, snapshot = None):
        """
        Clones a volume given a source volume name, destination volume name
        and optional backing snapshot.
        """
        params = { 'parent-volume':src_volume, 'volume':dest_volume }
        if snapshot:
            params['parent-snapshot']= snapshot.name
        self._invoke('volume-clone-create', params)

    def volume_delete(self, vol_name):
        """
        Deletes a volume and everything on it.
        """
        online = False

        try:
            self._invoke('volume-offline', { 'name':vol_name })
            online = True
        except FilerError as fe:
            if fe.errno != Filer.EFSDOESNOTEXIST:
                raise fe

        try:
            self._invoke('volume-destroy', { 'name':vol_name })
        except FilerError as fe:
            #If the volume was online, we will return it to same status
            if online:
                try:
                    self._invoke('volume-online', { 'name':vol_name })
                except FilerError:
                    pass
            raise fe

    def volume_names(self):
        """
        Return a list of volume names
        """
        vols = self.volumes()
        return [ v['name'] for v in vols ]

    def clear_all_clone_errors(self):
        """
        Clears all the clone errors.
        """
        errors = self._invoke('clone-list-status')['status']

        if errors is not None:
            errors = to_list(errors['ops-info'])
            for e in errors:
                    self._invoke('clone-clear', { 'clone-id': e['clone-id'] })
        return None

    def clone(self, source_path, dest_path, backing_snapshot=None, ranges=None):
        """
        Creates a file clone
        """
        params = { 'source-path':source_path }

        #You can have source == dest, but if you do you can only specify source
        if source_path != dest_path:
            params['destination-path'] = dest_path

        if backing_snapshot:
            raise FilerError(LsmError.NOT_IMPLEMENTED,
                "Support for backing luns not implemented for this API version")
            #params['snapshot-name']= backing_snapshot

        if ranges:
            block_ranges = []
            for r in ranges:
                values = {'block-count': r.block_count,
                          'destination-block-number':r.dest_block,
                          'source-block-number':r.src_block}

                block_ranges.append( {'block-range':values } )

            params['block-ranges'] = block_ranges

        rc = self._invoke('clone-start', params)

        id = rc['clone-id']

        while True:
            progress = self._invoke('clone-list-status', { 'clone-id': id } )\
                                    ['status']['ops-info']

            if progress['clone-state'] == 'failed' :
                self._invoke('clone-clear', { 'clone-id': id })
                raise FilerError(progress['error'], progress['reason'])
            elif progress['clone-state'] == 'running' or \
                 progress['clone-state'] == 'fail exit':
                #State needs to transition to failed before we can clear it!
                time.sleep(0.2)     #Don't hog cpu
            elif progress['clone-state'] == 'completed':
                return progress['destination-file']
            else:
                raise FilerError(ErrorNumber.NOT_IMPLEMENTED, 'Unexpected state=' + progress['clone-state'])

    def lun_online(self, lun_path):
        self._invoke('lun-online', {'path': lun_path})

    def lun_offline(self, lun_path):
        self._invoke('lun-offline', {'path': lun_path})

    def igroups(self, group_name = None):
        rc = []

        if group_name:
            g = self._invoke('igroup-list-info',
                            {'initiator-group-name': group_name})
        else:
            g = self._invoke('igroup-list-info')

        if g['initiator-groups'] :
            rc = to_list(g['initiator-groups']['initiator-group-info'])
        return rc
    
    def igroup_exists(self, name):
        g = self.igroups()
        for ig in g:
            if ig['initiator-group-name'] == name:
                return True
        return False

    def igroup_create(self, name, type):
        params = { 'initiator-group-name': name, 'initiator-group-type':type}
        self._invoke('igroup-create', params)

    def igroup_delete(self, name):
        self._invoke('igroup-destroy', {'initiator-group-name':name})

    def igroup_add_initiator(self, ig, initiator):
        self._invoke('igroup-add', {'initiator-group-name':ig, 'initiator':initiator} )

    def igroup_del_initiator(self, ig, initiator):
        self._invoke('igroup-remove', {'initiator-group-name':ig, 'initiator':initiator} )

    def lun_map(self, igroup, lun_path):
        self._invoke('lun-map', {'initiator-group':igroup, 'path':lun_path})

    def lun_unmap(self, igroup, lun_path):
        self._invoke('lun-unmap', {'initiator-group':igroup, 'path':lun_path})

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
        Given an initiator_id and initiator group name, return a list of lun-info
        """
        luns = []

        rc = self._invoke('lun-initiator-list-map-info',
                            {'initiator': initiator_id})

        if rc['lun-maps']:
            lun_list = to_list(rc['lun-maps']['lun-map-info'])
            for l in lun_list:
                if l['initiator-group'] == initiator_group_name:
                    luns.append(self.luns(l['path'])[0])

        return luns

    def snapshots(self, volume_name):
        rc = []
        args = { 'target-type':'volume', 'target-name':volume_name }
        ss = self._invoke('snapshot-list-info', args)
        if ss['snapshots']:
            rc = to_list(ss['snapshots']['snapshot-info'])
        return rc

    def snapshot_create(self, volume_name, snapshot_name):
        self._invoke('snapshot-create', {'volume':volume_name,
                                        'snapshot':snapshot_name })
        return [ v for v in self.snapshots(volume_name)
                    if v['name'] == snapshot_name ][0]

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
        params = { 'snapshot':snapshot_name, 'volume':fs_name }
        self._invoke('snapshot-restore-volume', params)

    def snapshot_restore_file(self, snapshot_name, file, restore_file):
        """
        Restore a list of files
        """
        params = {'snapshot': snapshot_name, 'path': file }

        if restore_file:
            params['restore-path'] = restore_file

        self._invoke('snapshot-restore-file', params)

    def snapshot_delete(self, volume_name, snapshot_name):
        self._invoke('snapshot-delete', {'volume':volume_name, 'snapshot':snapshot_name})

    def export_auth_types(self):
        rc = self._invoke('nfs-get-supported-sec-flavors')
        return [ e['flavor'] for e in to_list(rc['sec-flavor']['sec-flavor-info'])]

    @staticmethod
    def _build_list(pylist, list_name, elem_name):
        """
        Given a python list, build the appropriate dict that contains the
        list items so that it can be converted to xml to be sent on the wire.
        """
        return [ {list_name:{elem_name:l}} for l in pylist ]

    @staticmethod
    def _build_export_fs_all():
        return  Filer._build_list( ['true'], 'exports-hostname-info', 'all-hosts' )

    @staticmethod
    def _build_export_fs_list( hosts ):
        if hosts[0] == '*':
            return Filer._build_export_fs_all()
        else:
            return  Filer._build_list( hosts, 'exports-hostname-info', 'name' )

    def nfs_export_fs(self, volume_path, export_path, ro_list, rw_list,
                      root_list, anonuid = None, sec_flavor = None):
        """
        Export a fs, deprecated (Will remove soon)
        """
        rule = {'pathname': volume_path}
        if volume_path != export_path:
            #Try creating the directory needed
            rule['actual-pathname'] = volume_path
            rule['pathname'] = export_path

        if len(ro_list):
            rule['read-only'] = Filer._build_export_fs_list(ro_list)

        if len(rw_list):
            rule['read-write'] = Filer._build_export_fs_list(rw_list)

        if len(root_list):
            rule['root'] = Filer._build_export_fs_list(root_list)

        if anonuid:
            rule['anon'] = anonuid

        if sec_flavor:
            rule['sec-flavor'] = Filer._build_list( [sec_flavor], 'sec-flavor-info', 'flavor')

        params = { 'persistent':'true', 'rules':{'exports-rule-info':[rule]}, 'verbose':'true' }
        self._invoke('nfs-exportfs-append-rules', params)

    def _build_export_rules(self, volume_path, export_path, ro_list, rw_list,
                            root_list, anonuid = None, sec_flavor = None):
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
            r['anon'] = anonuid

        if sec_flavor:
            r['sec-flavor'] = Filer._build_list( [sec_flavor], 'sec-flavor-info', 'flavor')

        return rule

    def nfs_export_fs2(self, volume_path, export_path, ro_list, rw_list,
                      root_list, anonuid = None, sec_flavor = None):
        """
        NFS export a volume.
        """

        rule = self._build_export_rules(volume_path, export_path, ro_list,
                    rw_list, root_list, anonuid, sec_flavor)

        params = { 'persistent':'true', 'rules':{'exports-rule-info-2':[rule]}, 'verbose':'true' }
        self._invoke('nfs-exportfs-append-rules-2', params)

    def nfs_export_fs_modify2(self, volume_path, export_path, ro_list, rw_list,
                             root_list, anonuid = None, sec_flavor = None):

        """
        Modifies an existing rule.
        """
        rule = self._build_export_rules(volume_path, export_path, ro_list,
            rw_list, root_list, anonuid, sec_flavor)

        params = { 'persistent':'true', 'rule':{'exports-rule-info-2':[rule]} }
        self._invoke('nfs-exportfs-modify-rule-2', params)

    def nfs_export_remove(self, export_paths):
        """
        Removes an existing export
        """
        assert(type(export_paths) is list)
        paths = Filer._build_list(export_paths, 'pathname-info', 'name')
        self._invoke('nfs-exportfs-delete-rules', {'pathnames':paths, 'persistent':'true'} )

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
        params = { 'volume': volume }

        rc = self._invoke('volume-list-info', params)

        if 'clone-children' in rc['volumes']['volume-info']:
            tmp = rc['volumes']['volume-info']['clone-children']['clone-child-info']
            rc = [ c['clone-child-name'] for c in to_list(tmp) ]
        else:
            rc = None

        return rc

    def volume_split_clone(self, volume):
        self._invoke('volume-clone-split-start', { 'volume': volume } )

    def volume_split_status(self):
        result = []

        rc = self._invoke('volume-clone-split-status')

        if 'clone-split-details' in rc:
            tmp = rc['clone-split-details']['clone-split-detail-info']
            result = [ r['name'] for r in to_list(tmp)]

        return result

if __name__ == '__main__':
    try:
        #TODO: Need some unit test code
        pass

    except FilerError as fe:
        print 'Errno=', fe.errno, 'reason=', fe.reason
