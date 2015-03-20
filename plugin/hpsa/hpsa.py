# Copyright (C) 2015 Red Hat, Inc.
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
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA
#
# Author: Gris Ge <fge@redhat.com>

import os
import errno

from lsm import (
    IPlugin, Client, Capabilities, VERSION, LsmError, ErrorNumber, uri_parse,
    System)

from lsm.plugin.hpsa.utils import cmd_exec, ExecError


def _handle_errors(method):
    def _wrapper(*args, **kwargs):
        try:
            return method(*args, **kwargs)
        except LsmError:
            raise
        except KeyError as key_error:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Expected key missing from SmartArray hpssacli output:%s" %
                key_error)
        except ExecError as exec_error:
            if 'No controllers detected' in exec_error.stdout:
                raise LsmError(
                    ErrorNumber.NOT_FOUND_SYSTEM,
                    "No HP SmartArray deteceted by hpssacli.")
            else:
                raise LsmError(ErrorNumber.PLUGIN_BUG, str(exec_error))
        except Exception as common_error:
            raise LsmError(
                ErrorNumber.PLUGIN_BUG,
                "Got unexpected error %s" % common_error)

    return _wrapper


def _sys_status_of(hp_ctrl_status):
    """
    Base on data of "hpssacli ctrl all show status"
    """
    status_info = ''
    status = System.STATUS_UNKNOWN
    check_list = [
        'Controller Status', 'Cache Status', 'Battery/Capacitor Status']
    for key_name in check_list:
        if key_name in hp_ctrl_status and hp_ctrl_status[key_name] != 'OK':
            # TODO(Gris Ge): Beg HP for possible values
            status = System.STATUS_OTHER
            status_info += hp_ctrl_status[key_name]

    if status != System.STATUS_OTHER:
        status = System.STATUS_OK

    return status, status_info


def _parse_hpssacli_output(output):
    """
    Got a output string of hpssacli to dictionary(nested).
    """
    output_lines = [
        l for l in output.split("\n") if l and not l.startswith('Note:')]

    data = {}

    # Detemine indention level
    top_indention_level = sorted(
        set(
            len(line) - len(line.lstrip())
            for line in output_lines))[0]

    indent_2_data = {
        top_indention_level: data
    }

    for line_num in range(len(output_lines)):
        cur_line = output_lines[line_num]
        if cur_line.strip() == 'None attached':
            continue
        if line_num + 1 == len(output_lines):
            nxt_line = ''
        else:
            nxt_line = output_lines[line_num + 1]

        cur_indent_count = len(cur_line) - len(cur_line.lstrip())
        nxt_indent_count = len(nxt_line) - len(nxt_line.lstrip())

        cur_line_splitted = cur_line.split(": ")
        cur_data_pointer = indent_2_data[cur_indent_count]

        if nxt_indent_count > cur_indent_count:
            nxt_line_splitted = nxt_line.split(": ")
            if len(nxt_line_splitted) == 1:
                new_data = []
            else:
                new_data = {}

            if cur_line.lstrip() not in cur_data_pointer:
                cur_data_pointer[cur_line.lstrip()] = new_data
                indent_2_data[nxt_indent_count] = new_data
            elif type(cur_data_pointer[cur_line.lstrip()]) != type(new_data):
                raise LsmError(
                    ErrorNumber.PLUGIN_BUG,
                    "_parse_hpssacli_output(): Unexpected line '%s%s\n'" %
                    (cur_line, nxt_line))
        else:
            if len(cur_line_splitted) == 1:
                if type(indent_2_data[cur_indent_count]) != list:
                    raise Exception("not a list: '%s'" % cur_line)
                cur_data_pointer.append(cur_line.lstrip())
            else:
                cur_data_pointer[cur_line_splitted[0].lstrip()] = \
                    ": ".join(cur_line_splitted[1:]).strip()
    return data


class SmartArray(IPlugin):
    _DEFAULT_BIN_PATHS = [
        "/usr/sbin/hpssacli", "/opt/hp/hpssacli/bld/hpssacli"]

    def __init__(self):
        self._sacli_bin = None

    def _find_sacli(self):
        """
        Try _DEFAULT_MDADM_BIN_PATHS
        """
        for cur_path in SmartArray._DEFAULT_BIN_PATHS:
            if os.path.lexists(cur_path):
                self._sacli_bin = cur_path

        if not self._sacli_bin:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "SmartArray sacli is not installed correctly")

    @_handle_errors
    def plugin_register(self, uri, password, timeout, flags=Client.FLAG_RSVD):
        if os.geteuid() != 0:
            raise LsmError(
                ErrorNumber.INVALID_ARGUMENT,
                "This plugin requires root privilege both daemon and client")
        uri_parsed = uri_parse(uri)
        self._sacli_bin = uri_parsed.get('parameters', {}).get('hpssacli')
        if not self._sacli_bin:
            self._find_sacli()

        self._sacli_exec(['version'], flag_convert=False)

    @_handle_errors
    def plugin_unregister(self, flags=Client.FLAG_RSVD):
        pass

    @_handle_errors
    def job_status(self, job_id, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    @_handle_errors
    def job_free(self, job_id, flags=Client.FLAG_RSVD):
        pass

    @_handle_errors
    def plugin_info(self, flags=Client.FLAG_RSVD):
        return "HP SmartArray Plugin", VERSION

    @_handle_errors
    def time_out_set(self, ms, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    @_handle_errors
    def time_out_get(self, flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")

    @_handle_errors
    def capabilities(self, system, flags=Client.FLAG_RSVD):
        return Capabilities()

    def _sacli_exec(self, sacli_cmds, flag_convert=True):
        """
        If flag_convert is True, convert data into dict.
        """
        sacli_cmds.insert(0, self._sacli_bin)
        try:
            output = cmd_exec(sacli_cmds)
        except OSError as os_error:
            if os_error.errno == errno.ENOENT:
                raise LsmError(
                    ErrorNumber.INVALID_ARGUMENT,
                    "hpssacli binary '%s' is not exist or executable." %
                    self._sacli_bin)
            else:
                raise

        if flag_convert:
            return _parse_hpssacli_output(output)
        else:
            return output

    @_handle_errors
    def systems(self, flags=0):
        """
        Depend on command:
            hpssacli ctrl all show detail
            hpssacli ctrl all show status
        """
        rc_lsm_syss = []
        ctrl_all_show = self._sacli_exec(
            ["ctrl", "all", "show", "detail"])
        ctrl_all_status = self._sacli_exec(
            ["ctrl", "all", "show", "status"])

        for ctrl_name in ctrl_all_show.keys():
            ctrl_data = ctrl_all_show[ctrl_name]
            sys_id = ctrl_data['Serial Number']
            (status, status_info) = _sys_status_of(ctrl_all_status[ctrl_name])

            plugin_data = "%s" % ctrl_data['Slot']

            rc_lsm_syss.append(
                System(sys_id, ctrl_name, status, status_info, plugin_data))

        return rc_lsm_syss

    @_handle_errors
    def pools(self, search_key=None, search_value=None,
              flags=Client.FLAG_RSVD):
        raise LsmError(ErrorNumber.NO_SUPPORT, "Not supported yet")
