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
# License along with this library; If not, see <http://www.gnu.org/licenses/>.
#
# Author: tasleson

import socket
import traceback
import sys
from lsm import LsmError, error, ErrorNumber
from lsm.lsmcli import cmd_line_wrapper
import six

from lsm._common import SocketEOF as _SocketEOF
from lsm._transport import TransPort

def search_property(lsm_objs, search_key, search_value):
    """
    This method does not check whether lsm_obj contain requested property.
    The method caller should do the check.
    """
    if search_key is None:
        return lsm_objs
    return list(lsm_obj for lsm_obj in lsm_objs
                if getattr(lsm_obj, search_key) == search_value)


class PluginRunner(object):
    """
    Plug-in side common code which uses the passed in plugin to do meaningful
    work.
    """

    @staticmethod
    def _is_number(val):
        """
        Returns True if val is an integer.
        """
        try:
            int(val)
            return True
        except ValueError:
            return False

    def __init__(self, plugin, args):
        self.cmdline = False
        if len(args) == 2 and PluginRunner._is_number(args[1]):
            try:
                fd = int(args[1])
                self.tp = TransPort(
                    socket.fromfd(fd, socket.AF_UNIX, socket.SOCK_STREAM))

                # At this point we can return errors to the client, so we can
                # inform the client if the plug-in fails to create itself
                try:
                    self.plugin = plugin()
                except Exception as e:
                    ec_info = sys.exc_info()

                    self.tp.send_error(0, -32099,
                                       'Error instantiating plug-in ' + str(e))
                    raise six.reraise(*ec_info)

            except Exception:
                error(traceback.format_exc())
                error('Plug-in exiting.')
                sys.exit(2)

        else:
            self.cmdline = True
            cmd_line_wrapper(plugin)

    def run(self):
        # Don't need to invoke this when running stand alone as a cmdline
        if self.cmdline:
            return

        need_shutdown = False
        msg_id = 0

        try:
            while True:
                try:
                    # result = None

                    msg = self.tp.read_req()

                    method = msg['method']
                    msg_id = msg['id']
                    params = msg['params']

                    # Check to see if this plug-in implements this operation
                    # if not return the expected error.
                    if hasattr(self.plugin, method):
                        if params is None:
                            result = getattr(self.plugin, method)()
                        else:
                            result = getattr(self.plugin, method)(
                                **msg['params'])
                    else:
                        raise LsmError(ErrorNumber.NO_SUPPORT,
                                       "Unsupported operation")

                    self.tp.send_resp(result)

                    if method == 'plugin_register':
                        need_shutdown = True

                    if method == 'plugin_unregister':
                        # This is a graceful plugin_unregister
                        need_shutdown = False
                        self.tp.close()
                        break

                except ValueError as ve:
                    error(traceback.format_exc())
                    self.tp.send_error(msg_id, -32700, str(ve))
                except AttributeError as ae:
                    error(traceback.format_exc())
                    self.tp.send_error(msg_id, -32601, str(ae))
                except LsmError as lsm_err:
                    self.tp.send_error(msg_id, lsm_err.code, lsm_err.msg,
                                       lsm_err.data)
        except _SocketEOF:
            # Client went away and didn't meet our expectations for protocol,
            # this error message should not be seen as it shouldn't be
            # occurring.
            if need_shutdown:
                error('Client went away, exiting plug-in')
        except Exception:
            error("Unhandled exception in plug-in!\n" + traceback.format_exc())

            try:
                self.tp.send_error(msg_id, ErrorNumber.PLUGIN_BUG,
                                   "Unhandled exception in plug-in",
                                   str(traceback.format_exc()))
            except Exception:
                pass

        finally:
            if need_shutdown:
                # Client wasn't nice, we will allow plug-in to cleanup
                self.plugin.plugin_unregister()
                sys.exit(2)
