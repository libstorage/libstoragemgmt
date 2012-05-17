# Copyright (C) 2011-2012 Red Hat, Inc.
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

import socket
import traceback
import sys
from common import SocketEOF, LsmError, Error, ErrorNumber
import cmdline
import transport

class PluginRunner(object):
    """
    Plug-in side common code which uses the passed in plugin to do meaningful
    work.
    """

    def _is_number(self, val):
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
        if len(args) == 2 and self._is_number(args[1]):
            try:
                fd = int(args[1])
                self.tp = transport.Transport(
                    socket.fromfd(fd, socket.AF_UNIX, socket.SOCK_STREAM))

                #At this point we can return errors to the client, so we can
                #inform the client if the plug-in fails to create itself
                try:
                    self.plugin = plugin()
                except Exception as e:
                    self.tp.send_error(0, -32099,
                        'Error instantiating plug-in ' + str(e))
                    raise e

            except Exception:
                Error(traceback.format_exc())
                Error('Plug-in exiting.')
                sys.exit(2)

        else:
            self.cmdline = True
            cmdline.cmd_line_wrapper(plugin)

    def run(self):
        #Don't need to invoke this when running stand alone as a cmdline
        if self.cmdline:
            return

        need_shutdown = False
        id = 0

        try:
            while True:
                try:
                    #result = None

                    msg = self.tp.read_req()

                    method = msg['method']
                    id = msg['id']
                    params = msg['params']

                    #Check to see if this plug-in implements this operation
                    #if not return the expected error.
                    if hasattr(self.plugin, method):
                        if params is None:
                            result = getattr(self.plugin, method)()
                        else:
                            result = getattr(self.plugin, method)(**msg['params'])
                    else:
                        raise LsmError(ErrorNumber.NO_SUPPORT, "Unsupported operation")

                    self.tp.send_resp(result)

                    if method == 'startup':
                        need_shutdown = True

                    if method == 'shutdown':
                        #This is a graceful shutdown
                        need_shutdown = False
                        self.tp.close()
                        break

                except ValueError as ve:
                    Error(traceback.format_exc())
                    self.tp.send_error(id, -32700, str(ve))
                except AttributeError as ae:
                    Error(traceback.format_exc())
                    self.tp.send_error(id, -32601, str(ae))
                except LsmError as lsmerr:
                    self.tp.send_error(id, lsmerr.code, lsmerr.msg, lsmerr.data)
        except SocketEOF:
            #Client went away
            Error('Client went away, exiting plug-in')
        except Exception:
            Error(traceback.format_exc())
        finally:
            if need_shutdown:
                #Client wasn't nice, we will allow plug-in to cleanup
                self.plugin.shutdown()
                sys.exit(2)
