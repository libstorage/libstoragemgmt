#!/usr/bin/env python

# Copyright (c) 2011, Red Hat, Inc.
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions are met:
#
#    * Redistributions of source code must retain the above copyright
#      notice, this list of conditions and the following disclaimer.
#
#    * Redistributions in binary form must reproduce the above copyright
#      notice, this list of conditions and the following disclaimer in
#      the documentation and/or other materials provided with the
#      distribution.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS
# IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
# TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
# PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# HOLDER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED
# TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR
# PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF
# LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING
# NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
# SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

import socket
import traceback
import sys
from common import SocketEOF, LsmError, Error
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
            print   'At this time the only supported parameter'\
                    ' is a file descriptor.'
            sys.exit(1)
            #TODO Add command line parsing to allow stand alone plug-in
            #development.

    def run(self):
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

                    if params is None:
                        result = getattr(self.plugin, method)()
                    else:
                        result = getattr(self.plugin, method)(**msg['params'])

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
                except LsmError as lsm:
                    self.tp.send_error(id, lsm.code, lsm.msg, lsm.data)
        except SocketEOF:
            #Client went away
            Error('Client went away, exiting plug-in')
        except Exception as e:
            Error(traceback.format_exc())
        finally:
            if need_shutdown:
                #Client wasn't nice, we will allow plug-in to cleanup
                self.plugin.shutdown()
                sys.exit(2)
