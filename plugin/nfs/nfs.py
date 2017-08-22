#
# Copyright (C) 2017 Red Hat, Inc.
#
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
# Author: jumitche

import hashlib
import shlex
import os
import subprocess
import copy
import sys
import fcntl

from lsm import (Capabilities, ErrorNumber, FileSystem, INfs,
                 IStorageAreaNetwork, LsmError, NfsExport,
                 System, Pool, VERSION, search_property)

from lsm.plugin.nfs.nfs_clib import (get_fsid, list_mounts)


class NFSPlugin(INfs, IStorageAreaNetwork):
    """Main class"""
    _EXPORTS = '/etc/exports.d/libstoragemgmt.exports'
    _SYSID = 'nfs-localhost'
    _MOUNTS = '/proc/self/mounts'
    _AUTH_LIST = ["sys", "krb5", "krb5i", "krb5p"]

    @staticmethod
    def _run_cmd(cmd):
        try:
            if sys.version_info[0] > 3 or \
               (sys.version_info[0] == 3 and sys.version_info[1] >= 3):
                subprocess.check_call(cmd, timeout=3)
            else:
                subprocess.check_call(cmd)
        except:
            raise

    @staticmethod
    def _export_id(path, auth_type, anon_uid, anon_gid, options):
        """Calculate unique Export ID"""
        if auth_type is None:
            auth_type = 'sec'

        hsh = hashlib.md5()
        hsh.update(path.encode('utf-8'))
        hsh.update(auth_type.encode('utf-8'))
        if anon_uid is not None and anon_uid != NfsExport.ANON_UID_GID_NA:
            hsh.update(str(anon_uid).encode('utf-8'))
        if anon_gid is not None and anon_gid != NfsExport.ANON_UID_GID_NA:
            hsh.update(str(anon_gid).encode('utf-8'))
        if options is not None:
            hsh.update(options.encode('utf-8'))
        return hsh.hexdigest()

    @staticmethod
    def _parse_options(optionstring):
        """ Parse a comma separted option list into a dict """
        options = {}

        if optionstring is None:
            return options

        optionlist = optionstring.split(',')
        for opt in optionlist:
            if '=' in opt:
                key, val = opt.split('=')
            else:
                key = val = opt
            options[key] = val

        return options

    @staticmethod
    def _print_option(optionset):
        """Turn an options dict into a sorted string"""
        output = []

        if optionset is None:
            return None

        for key in sorted(optionset):
            if key == optionset[key]:
                output.append(key)
            else:
                output.append(key + '=' + optionset[key])

        if output:
            return ",".join(output)
        else:
            return None

    @staticmethod
    def _parse_export(parts=None):
        """Parse a line of export file"""
        if len(parts) < 1:
            return None

        path = parts[0]
        host = '*'
        optionstring = ""

        if len(parts) > 1:
            host = parts[1]
        if '(' and ')' in host:
            if host[0] != '(':
                host, optionstring = host[:-1].split('(')
            else:
                optionstring = host[1:-1]
                host = '*'

        options = NFSPlugin._parse_options(optionstring)

        root_list = []
        rw_list = []
        ro_list = []
        sec = None
        anonuid = None
        anongid = None

        if 'rw' in options:
            rw_list.append(host)
            del options['rw']

        if 'ro' in options:
            ro_list.append(host)
            del options['ro']

        if 'no_root_squash' in options:
            root_list.append(host)
            del options['no_root_squash']
        else:
            if 'root_squash' in options:
                del options['root_squash']

        if 'sec' in options:
            sec = options['sec']
            del options['sec']
        else:
            sec = None

        if 'anonuid' in options:
            anonuid = int(options['anonuid'])
            del options['anonuid']
        else:
            anonuid = NfsExport.ANON_UID_GID_NA

        if 'anongid' in options:
            anongid = int(options['anongid'])
            del options['anongid']
        else:
            anongid = NfsExport.ANON_UID_GID_NA

        try:
            fsid = get_fsid(path)
            optionstring = NFSPlugin._print_option(options)
            export_id = NFSPlugin._export_id(path, sec, anonuid, anongid,
                                             optionstring)

            result = NfsExport(export_id, fsid, path,
                               sec, root_list, rw_list, ro_list,
                               anonuid, anongid, optionstring)
            return result
        except:
            raise
        return None

    @staticmethod
    def _get_fsid_path(fs_id):
        """Return the mount point path for the give FSID"""
        parts = list_mounts()
        for prt in parts:
            try:
                fsid = get_fsid(prt)
                if fsid == fs_id:
                    return prt
            except:
                pass
        return None

    @staticmethod
    def _optionset(options):
        if options is None:
            return set()
        return set(options.split(','))

    @staticmethod
    def _match_path(expa, expb):
        if expa is None or expb is None:
            return False
        if expa._export_path != expb._export_path:
            return False
        return True

    @staticmethod
    def _match_export(expa, expb):
        fails = 0
        if expa is None or expb is None:
            return False
        if expa._export_path != expb._export_path:
            fails += 1
        if expa._auth != expb._auth:
            fails += 1
        if expa._anonuid != expb._anonuid:
            fails += 1
        if expa._anongid != expb._anongid:
            fails += 1
        opta = NFSPlugin._optionset(expa._options)
        optb = NFSPlugin._optionset(expb._options)
        if opta != optb:
            fails += 1
        if fails > 0:
            return False
        return True

    @staticmethod
    def _merge_exports(expa, expb):
        """Add new record expb's contents to existing expa's record"""

        # remove old mentions of hostname
        allhost = expb._rw + expb._root
        for host in allhost:
            if host in expa._root:
                expa._root.remove(host)
            if host in expa._ro:
                expa._ro.remove(host)
            if host in expa._rw:
                expa._rw.remove(host)

        # copy in additional hosts suppressing dupes
        for host in expb._root:
            if host not in expa._root:
                expa._root.append(host)
        for host in expb._rw:
            if host not in expa._rw:
                expa._rw.append(host)
        for host in expb._ro:
            if host not in expa._ro:
                expa._ro.append(host)
        return expa

    @staticmethod
    def _open_exports(readonly=True, filename=_EXPORTS):
        if readonly:
            mode = "r"
        else:
            mode = "r+"

        if not os.path.exists(filename):
            if readonly:
                raise IOError
            else:
                mode = "w+"

        try:
            efile = open(filename, mode)
            if not readonly:
                fcntl.flock(efile, fcntl.LOCK_EX)
        except:
            raise
        return efile

    @staticmethod
    def _close_exports(efile):
        """Close the exports file, unlocking the flock is implicit"""
        efile.close()

    @staticmethod
    def _read_exports(efile):
        """Load the exports file into a list"""
        exports = []
        for line in efile:
            newexp = NFSPlugin._parse_export(shlex.split(line, '#'))
            if newexp is None:
                continue
            for idx, oldexp in enumerate(exports):
                if NFSPlugin._match_export(newexp, oldexp):
                    exports[idx] = NFSPlugin._merge_exports(oldexp, newexp)
                    newexp = None
            if newexp is not None:
                exports.append(newexp)
        return exports

    @staticmethod
    def _load_exports(filename=_EXPORTS):
        exports = []
        try:
            efile = NFSPlugin._open_exports(True, filename)
            exports = NFSPlugin._read_exports(efile)
        except IOError:
            return []
        if efile:
            NFSPlugin._close_exports(efile)
        return exports

    @staticmethod
    def _filter_export_byid(exports, export_id):
        """Remove an entry by its export id"""
        output = []
        for exp in exports:
            if export_id != exp.id:
                output.append(exp)
        return output

    @staticmethod
    def _write_exports(efile, exports):
        """write out the exports list to a file"""
        try:
            efile.seek(0)
            efile.truncate()
            efile.write('# NFS exports managed by libstoragemgmt.'
                        ' do not edit.\n')

            for exp in exports:
                common_opts = {}
                if exp._options is not None:
                    common_opts = NFSPlugin._parse_options(exp._options)

                if exp._anonuid is not None and \
                   exp._anonuid != NfsExport.ANON_UID_GID_NA:
                    common_opts['anonuid'] = exp._anonuid

                if exp.anongid is not None and \
                   exp._anongid != NfsExport.ANON_UID_GID_NA:
                    common_opts['anongid'] = exp._anongid

                if exp._auth is not None:
                    common_opts['sec'] = str(exp._auth)

                if exp._export_path is not None:
                    if not os.path.isdir(exp._export_path):
                        raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                                       'Export path does not exist')

                if exp._fs_id is not None and exp._export_path is not None:
                    try:
                        export_id = get_fsid(exp._export_path)
                        if export_id != exp._fs_id:
                            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                                           'FS ID and Path\'s FS ID do not match')
                    except:
                        raise

                for host in exp._rw:
                    opts = copy.copy(common_opts)
                    if host in exp._root:
                        if 'root_squash' in opts:
                            del opts['root_squash']
                        opts['no_root_squash'] = 'no_root_squash'
                    if 'ro' in opts:
                        del opts['ro']
                    opts['rw'] = 'rw'

                    if " " in exp._export_path:
                        efile.write("\"%s\" %s(%s)\n" %
                                    (exp._export_path, host,
                                     NFSPlugin._print_option(opts)))
                    else:
                        efile.write("%s %s(%s)\n" %
                                    (exp._export_path, host,
                                     NFSPlugin._print_option(opts)))

                for host in exp._ro:
                    opts = copy.copy(common_opts)
                    if host in exp._root:
                        if 'root_squash' in opts:
                            del opts['root_squash']
                        opts['no_root_squash'] = 'no_root_squash'
                    if 'rw' in opts:
                        del opts['rw']
                    opts['ro'] = 'ro'
                    efile.write("%s %s(%s)\n" %
                                (exp._export_path,
                                 host, NFSPlugin._print_option(opts)))
        except IOError:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           'error writing exports')

    @staticmethod
    def _update_exports():
        """Update the system"""
        cmd = ["/usr/sbin/exportfs", "-ar"]
        try:
            NFSPlugin._run_cmd(cmd)
        except subprocess.CalledProcessError:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           'exportfs failed')
        except OSError:
            raise LsmError(ErrorNumber.PLUGIN_BUG,
                           'error calling exportfs')

    def plugin_info(self, flags=0):
        return "Local NFS Exports", VERSION

    def __init__(self):
        self.tmo = 0
        return

    def plugin_register(self, uri, password, timeout, flags=0):
        self.tmo = timeout
        if os.geteuid() != 0:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           "This plugin requires root privilege for both "
                           "daemon and client")

        return

    def time_out_set(self, ms, flags=0):
        self.tmo = ms

    def time_out_get(self, flags=0):
        return self.tmo

    @staticmethod
    def _get_fs_sizes(path):
        """Stat a filesystem and return total and available space"""
        try:
            sta = os.statvfs(path)
            total_size = sta.f_blocks * sta.f_frsize
            avail_size = sta.f_bavail * sta.f_frsize
        except OSError:
            raise

        return (total_size, avail_size)

    def pools(self, search_key=None, search_value=None, flags=0):
        pools = []
        parts = list_mounts()
        for prt in parts:
            try:
                (total_size, avail_size) = NFSPlugin._get_fs_sizes(prt)
                fsid = get_fsid(prt)
                pooltype = Pool.ELEMENT_TYPE_FS
                unsup_actions = 0
                status = System.STATUS_OK
                status_info = ''
                pools.append(Pool(fsid, prt, pooltype, unsup_actions,
                                  total_size, avail_size, status, status_info,
                                  self._SYSID))
            except OSError:
                pass
        return search_property(pools, search_key, search_value)

    def systems(self, flags=0):
        syslist = []
        hostname = os.uname()[1]
        syslist.append(System(NFSPlugin._SYSID, "NFS on %s" % hostname,
                              System.STATUS_UNKNOWN, ''))
        return syslist

    def fs(self, search_key=None, search_value=None, flags=0):
        """List filesystems, required for other operations"""
        fss = []
        parts = list_mounts()
        for prt in parts:
            try:
                (total_size, avail_size) = NFSPlugin._get_fs_sizes(prt)
                fsid = get_fsid(prt)
                fss.append(
                    FileSystem(fsid, prt, total_size,
                               avail_size, fsid, self._SYSID))
            except OSError:
                pass

        return search_property(fss, search_key, search_value)

    def exports(self, search_key=None, search_value=None, flags=0):
        """List existing exports"""
        exports = NFSPlugin._load_exports()
        return search_property(exports, search_key, search_value)

    def export_fs(self, fs_id, export_path, root_list, rw_list, ro_list,
                  anon_uid=NfsExport.ANON_UID_GID_NA,
                  anon_gid=NfsExport.ANON_UID_GID_NA,
                  auth_type=None, options=None, flags=None):
        """Add an export"""

        if fs_id is None and export_path is None:
            raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                           'Must provide fs_id or export_path')

        if fs_id is None:
            try:
                fs_id = get_fsid(export_path)
            except:
                raise LsmError(ErrorNumber.NOT_FOUND_FS,
                               'FileSystem not found')
        else:
            if NFSPlugin._get_fsid_path(fs_id) is None:
                raise LsmError(ErrorNumber.NOT_FOUND_FS,
                               'No FileSystem found with that ID')

        if export_path is None:
            export_path = NFSPlugin._get_fsid_path(fs_id)
            if export_path is None:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               'Could not locate filesystem')
        else:
            try:
                get_fsid(export_path)
            except:
                raise LsmError(ErrorNumber.NOT_FOUND_FS,
                               'Export path not found')

        if auth_type is not None:
            authlist = NFSPlugin._AUTH_LIST
            if auth_type not in authlist:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               'Unsupported auth type')

        for host in root_list:
            if host not in rw_list and host not in ro_list:
                raise LsmError(ErrorNumber.INVALID_ARGUMENT,
                               'Root hosts must also be in rw or ro lists')

        expid = NFSPlugin._export_id(export_path, auth_type, anon_uid,
                                     anon_gid, options)

        newexp = NfsExport(expid, fs_id, export_path,
                           auth_type, root_list, rw_list, ro_list,
                           anon_uid, anon_gid, options)

        efile = NFSPlugin._open_exports(readonly=False)
        exports = NFSPlugin._read_exports(efile)
        result = None

        for idx, oldexp in enumerate(exports):
            if NFSPlugin._match_path(newexp, oldexp):
                exports[idx] = newexp
                result = exports[idx]
                newexp = None
        if newexp is not None:
            exports.append(newexp)
            result = newexp

        NFSPlugin._write_exports(efile, exports)
        NFSPlugin._close_exports(efile)
        NFSPlugin._update_exports()
        return result

    def export_remove(self, export, flags=0):
        try:
            efile = NFSPlugin._open_exports(readonly=False)
            exports = NFSPlugin._read_exports(efile)
            filtered = NFSPlugin._filter_export_byid(exports, export.id)
            NFSPlugin._write_exports(efile, filtered)
            NFSPlugin._close_exports(efile)
            NFSPlugin._update_exports()
        except:
            raise

    def plugin_unregister(self, flags=0):
        return

    def job_status(self, job_id, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def job_free(self, job_id, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported")

    def capabilities(self, system, flags=0):
        """Define which Capabilities this plugin offers"""
        cap = Capabilities()

        # File system
        cap.set(Capabilities.FS)

        # NFS
        cap.set(Capabilities.EXPORT_AUTH)
        cap.set(Capabilities.EXPORTS)
        cap.set(Capabilities.EXPORT_FS)
        cap.set(Capabilities.EXPORT_REMOVE)
        cap.set(Capabilities.EXPORT_CUSTOM_PATH)

        return cap

    def export_auth(self, flags=0):
        """
        Returns the types of authentication that are available for NFS
        """
        return NFSPlugin._AUTH_LIST
