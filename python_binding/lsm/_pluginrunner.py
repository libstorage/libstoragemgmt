# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2011-2023 Red Hat, Inc.
#
# Author: Tony Asleson <tasleson@redhat.com>

import socket
import traceback
import sys
from lsm import LsmError, error, ErrorNumber
from lsm.lsmcli import cmd_line_wrapper
import errno

from lsm._common import SocketEOF as _SocketEOF
from lsm._transport import TransPort
from lsm._transport import IoDeadlineExpired as _IoDeadlineExpired

# How long (seconds) a client has to complete plugin_register.  Turned into a
# single absolute deadline covering every read until registration, so a peer
# cannot renew it by sending requests.  Kept consistent with the C binding
# (LSM_PLUGIN_INITIAL_RECV_TIMEOUT_SECONDS in c_binding/lsm_ipc_timeout.h).
# Cleared once the client registers so slow operations are never interrupted.
REGISTRATION_TIMEOUT = 30


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
                    self.tp.send_error(0, -32099,
                                       'Error instantiating plug-in ' + str(e))
                    raise

            except Exception:
                error(traceback.format_exc())
                error('Plug-in exiting.')
                sys.exit(2)

        else:
            self.cmdline = True
            cmd_line_wrapper(plugin)

    def _report_unhandled(self, msg_id):
        """
        Logs the exception being handled and makes a best effort to tell the
        client the plug-in hit a bug.
        """
        error("Unhandled exception in plug-in!\n" + traceback.format_exc())

        try:
            self.tp.send_error(msg_id, ErrorNumber.PLUGIN_BUG,
                               "Unhandled exception in plug-in",
                               str(traceback.format_exc()))
        except Exception:
            pass

    def run(self):
        # Don't need to invoke this when running stand alone as a cmdline
        if self.cmdline:
            return

        need_shutdown = False
        msg_id = 0

        # One absolute deadline for the whole registration handshake, armed
        # before the first read: an un-registered client cannot stretch it by
        # keeping the conversation alive with requests we reject, the way a
        # per-message timeout let it.  It bounds our replies too, so a peer
        # that chatters but never reads them cannot pin us inside sendall()
        # either; both expiries arrive as IoDeadlineExpired.
        self.tp.set_io_deadline(REGISTRATION_TIMEOUT)

        try:
            while True:
                try:
                    # result = None

                    msg = self.tp.read_req()

                    method = msg['method']
                    msg_id = msg['id']
                    params = msg['params']

                    if not isinstance(method, str) \
                            or method.startswith('_'):
                        raise LsmError(ErrorNumber.NO_SUPPORT,
                                       "Unsupported operation")

                    target = getattr(self.plugin, method, None)
                    if target is None or not callable(target):
                        raise LsmError(ErrorNumber.NO_SUPPORT,
                                       "Unsupported operation")

                    if params is None:
                        result = target()
                    else:
                        result = target(**msg['params'])

                    if method == 'plugin_register':
                        # Set before the reply goes out: the plug-in is
                        # registered from here on, so it is owed a
                        # plugin_unregister even if the client is gone by
                        # the time we answer.
                        need_shutdown = True

                    self.tp.send_resp(result)

                    if method == 'plugin_register':
                        # Client has registered; drop the deadline so slow
                        # operations are never interrupted.
                        self.tp.set_io_deadline(None)

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
        except _IoDeadlineExpired:
            # Client connected but did not complete plugin_register within the
            # allotted time; give up rather than block this worker.  This is
            # the transport's own exception rather than socket.timeout, which
            # a registered plug-in's backend calls raise too (it is the
            # builtin TimeoutError); those are plug-in bugs and belong to the
            # handlers below.  Note this must precede the socket.error
            # handler as it is a subclass of it.
            error('Client failed to register in time, exiting plug-in')
        except _SocketEOF:
            # Client went away and didn't meet our expectations for protocol,
            # this error message should not be seen as it shouldn't be
            # occurring.
            if need_shutdown:
                error('Client went away, exiting plug-in')
        except socket.error as se:
            if se.errno == errno.EPIPE:
                error('Client went away, exiting plug-in')
            else:
                # Not necessarily the client socket: an OSError out of the
                # plug-in's own code lands here as well, and the client is
                # still there to be told.
                self._report_unhandled(msg_id)
        except Exception:
            self._report_unhandled(msg_id)

        finally:
            if need_shutdown:
                # Client wasn't nice, we will allow plug-in to cleanup
                self.plugin.plugin_unregister()
                sys.exit(2)
