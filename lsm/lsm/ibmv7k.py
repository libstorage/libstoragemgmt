# Copyright (C) 2013 IBM Corporation
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
# Author: Deepak C Shetty (deepakcs@linux.vnet.ibm.com)

import paramiko
from iplugin import IStorageAreaNetwork
from common import LsmError, ErrorNumber, uri_parse
from version import VERSION
from data import Volume, Initiator, FileSystem, Snapshot, \
    AccessGroup, System, Capabilities, Pool


def handle_ssh_errors(method):
    def ssh_wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except paramiko.SSHException as sshe:
            raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION, str(sshe))

    return ssh_wrapper


class V7kError(Exception):
    """
    Class represents a v7k cli bad return code
    """

    def __init__(self, errno, reason, *args, **kwargs):
        Exception.__init__(self, *args, **kwargs)
        self.errno = errno
        self.reason = reason


# NOTE: v7k cli doc doesn't list the possible error codes per cli.
# Thus do very generic and basic error handling for now.
# Just pass the error msg & code returned by the array back to lsm.
def handle_v7k_errors(method):
    def v7k_wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except V7kError as ve:
            msg = ve.reason + " (vendor error code= " + str(ve.errno) + ")"
            raise LsmError(ErrorNumber.PLUGIN_ERROR, msg)

    return v7k_wrapper


# A lite weight sshclient
class SSHClient():

    @handle_ssh_errors
    def __init__(self, hostname, login, passwd, conn_timeout=30):
        self.ssh = paramiko.SSHClient()
        self.ssh.set_missing_host_key_policy(paramiko.AutoAddPolicy())

        try:
            self.ssh.connect(hostname, username=login, password=passwd,
                             timeout=conn_timeout)
        except Exception as e:
            msg = ("Error while connecting via ssh to host %s : %s") % (hostname, e)
            raise paramiko.SSHException(msg)

    @handle_v7k_errors
    def execute(self, command, check_exit_code=True):
        stdin_strm, stdout_strm, stderr_strm = self.ssh.exec_command(command)

        stdin_strm.close()

        stdout_channel = stdout_strm.channel
        exit_code = stdout_channel.recv_exit_status()
        stdout_channel.close()

        stdout = stdout_strm.read()
        stdout_strm.close()

        stderr = stderr_strm.read()
        stderr_strm.close()

        if check_exit_code and exit_code != 0:
            errList = stderr.split(' ', 1)

            if exit_code == 127:
                # command not found error
                errno = exit_code
            else:
                # other errors
                errno = errList[0]

            reason = errList[1]

            raise V7kError(errno, reason)

        return (exit_code, stdout, stderr)

    @handle_ssh_errors
    def close(self):
        self.ssh.close()


# IBM V7000 storage lsm plugin

# *** lsm -- IBM V7000 terminology mapping ***
# volume -- v7k volume (aka vdisk)
# initiator -- host
# access group --  NA
# NA -- I/O group
# pool -- mdisk group
# volume-child dep -- ~fcmap and other maps if any
# job -- cli that can be invoked in async mode

class IbmV7k(IStorageAreaNetwork):

    def __init__(self):
        self.sys_info = None
        self.ssh = None

    def _execute_command_and_parse_detailed(self, ssh_cmd):
        exit_code, stdout, stderr = self.ssh.execute(ssh_cmd)

        cmd_output = {}
        for line in stdout.split('\n'):
            name, foo, value = line.partition('!')
            if name is not None and len(name.strip()):
                cmd_output[name] = value

        return cmd_output

    def _execute_command_and_parse_concise(self, ssh_cmd):
        # This assume -nohdr is *not* present in ssh_cmd
        exit_code, stdout, stderr = self.ssh.execute(ssh_cmd)

        output_lines = stdout.split('\n')
        header_line = output_lines[0]
        keylist = header_line.split('!')

        # For some reason, concise output gives one extra blank line at the end
        attrib_lines = output_lines[1:-1]
        cmd_output = {}
        lineindex = 0

        for attrib_line in attrib_lines:
            valuelist = attrib_line.split('!')
            attributes = {}
            valueindex = 0
            for key in keylist:
                attributes[key] = valuelist[valueindex]
                valueindex += 1

            cmd_output[lineindex] = attributes
            lineindex += 1

        return cmd_output

    def _get_system_info(self):
        ssh_cmd = 'lssystem -delim !'
        return self._execute_command_and_parse_detailed(ssh_cmd)

    def _get_pools(self):
        ssh_cmd = 'lsmdiskgrp -bytes -delim !'
        return self._execute_command_and_parse_concise(ssh_cmd)

    def _pool(self, p):
        return Pool(p['id'], p['name'], p['capacity'], p['free_capacity'],
                    self.sys_info.id)

    def _get_volumes(self):
        ssh_cmd = 'lsvdisk -bytes -delim !'
        return self._execute_command_and_parse_concise(ssh_cmd)

    def _get_volume(self, volume):
        ssh_cmd = 'lsvdisk -bytes -delim ! %s' % volume
        return self._execute_command_and_parse_detailed(ssh_cmd)

    def _volume(self, v):
        # v7k support 512 bytes/sec only, as the phy drive bs.
        # Its a bit complicated to reverse map v7k volume to
        # phy drive using the cli, so for now hardcode it as
        # thats the only supported bs at the drive level.
        bs = 512

        if v['status'] == 'online':
            vol_status = Volume.STATUS_OK
        elif v['status'] == 'offline':
            vol_status = Volume.STATUS_DORMANT
        else:
            vol_status = Volume.STATUS_ERR

        return Volume(v['id'], v['name'], v['vdisk_UID'], bs,
                      (float(v['capacity']) / bs), vol_status, self.sys_info.id,
                      v['mdisk_grp_id'])

    def _create_volume(self, pool, volname, size_bytes, prov):
        ssh_cmd = ('mkvdisk -name %s -mdiskgrp %s -iogrp 0 -size %s'
                  ' -unit b') % (volname, pool, size_bytes)

        if prov == Volume.PROVISION_THIN:
            # Defaults for thinp
            rsize = 5
            warning = 0
            autoex = '-autoexpand'

            ssh_cmd += (' -rsize %d%% -warning %d %s') % (rsize, warning,
                        autoex)

        ec, so, se = self.ssh.execute(ssh_cmd)

    def _delete_volume(self, volume, force):
        # NOTE: volume can be id or name
        ssh_cmd = ('rmvdisk %s %s') % ('-force' if force else '', volume)
        ec, so, se = self.ssh.execute(ssh_cmd)

    def startup(self, uri, password, timeout, flags=0):
        self.uri = uri
        self.password = password

        up = uri_parse(uri)
        self.ssh = SSHClient(up['host'], up['username'], password, timeout)

        si = self._get_system_info()
        self.sys_info = System(si['id'], si['name'], System.STATUS_OK)

    def set_time_out(self, ms, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def get_time_out(self, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def shutdown(self, flags=0):
        self.ssh.close()
        return

    def job_status(self, job_id, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def job_free(self, job_id, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    # NOTE: Add more capabilities as more cli's are supported
    def capabilities(self, system, flags=0):
        cap = Capabilities()
        cap.set(Capabilities.BLOCK_SUPPORT)
        cap.set(Capabilities.VOLUMES)
        cap.set(Capabilities.VOLUME_CREATE)
        cap.set(Capabilities.VOLUME_DELETE)
        cap.set(Capabilities.VOLUME_THIN)
        return cap

    def plugin_info(self, flags=0):
        return "IBM V7000 lsm plugin", VERSION

    def pools(self, flags=0):
        gp = self._get_pools()
        return [self._pool(p) for p in gp.itervalues()]

    def systems(self, flags=0):
        return [self.sys_info]

    def volumes(self, flags=0):
        gv = self._get_volumes()
        return [self._volume(v) for v in gv.itervalues()]

    def initiators(self, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def volume_create(self, pool, volume_name, size_bytes, provisioning,
                      flags=0):
        self._create_volume(pool.id, volume_name, size_bytes, provisioning)
        newvol = self._get_volume(volume_name)
        return None, self._volume(newvol)

    def volume_delete(self, volume, flags=0):
        # TODO: How to pass -force param ? For now, assume -force
        self._delete_volume(volume.id, force=True)
        return None

    def volume_resize(self, volume, new_size_bytes, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def volume_replicate(self, pool, rep_type, volume_src, name, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def volume_replicate_range_block_size(self, system, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def volume_replicate_range(self, rep_type, volume_src, volume_dest, ranges,
                               flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def volume_online(self, volume, flags=0):
        # NOTE: Closest cli is mkvdiskhostmap, but that needs host id/name
        #       as well... hence doesn't fit under this API
        raise LsmError(ErrorNumber.NO_SUPPORT, "Feature not supported")

    def volume_offline(self, volume, flags=0):
        # NOTE: Closest cli is rmvdiskhostmap, but that needs host id/name
        #       as well... hence doesn't fit under this API
        raise LsmError(ErrorNumber.NO_SUPPORT, "Feature not supported")

    def iscsi_chap_auth(self, initiator, in_user, in_password, out_user,
                        out_password, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def initiator_grant(self, initiator_id, initiator_type, volume, access,
                        flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def initiator_revoke(self, initiator, volume, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def access_group_list(self, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Feature not supported")

    def access_group_create(self, name, initiator_id, id_type, system_id,
                            flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Feature not supported")

    def access_group_del(self, group, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Feature not supported")

    def access_group_add_initiator(self, group, initiator_id, id_type,
                                   flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Feature not supported")

    def access_group_del_initiator(self, group, initiator_id, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Feature not supported")

    def volumes_accessible_by_access_group(self, group, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Feature not supported")

    def access_groups_granted_to_volume(self, volume, flags=0):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Feature not supported")

    def volume_child_dependency(self, volume, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def volume_child_dependency_rm(self, volume, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def volumes_accessible_by_initiator(self, initiator, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")

    def initiators_granted_to_volume(self, volume, flags=0):
        raise LsmError(ErrorNumber.NOT_IMPLEMENTED,
                       "API not implemented at this time")
