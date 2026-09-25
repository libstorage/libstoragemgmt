# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2011-2023 Red Hat, Inc.
#
# Author: Tony Asleson <tasleson@redhat.com>

import os
import socket
import traceback
import sys
from lsm import LsmError, error, ErrorNumber
from lsm.lsmcli import cmd_line_wrapper
import errno

from lsm._common import SocketEOF as _SocketEOF
from lsm._transport import TransPort

# Name of the environment variable lsmd sets (from "plugin-registration-timeout"
# in lsmd.conf) before exec'ing a plug-in.  Kept consistent with
# LSM_PLUGIN_REGISTRATION_TIMEOUT_ENV in c_binding/lsm_ipc_timeout.h.
_REGISTRATION_TIMEOUT_ENV_NAME = 'LSM_PLUGIN_REGISTRATION_TIMEOUT'

# Default, in seconds.  Kept consistent with
# LSM_PLUGIN_INITIAL_RECV_TIMEOUT_SECONDS in c_binding/lsm_ipc_timeout.h.
_REGISTRATION_TIMEOUT_DEFAULT = 30

# Upper bound, matching the "value <= INT_MAX" guard in the C mirror,
# registration_timeout_seconds() in c_binding/lsm_plugin_ipc.cpp.  Without
# this, an absurdly large (but syntactically valid) config value would sail
# through here as an arbitrary-precision int and only fail later, inside
# set_io_deadline()'s time.monotonic() + seconds, as an uncaught
# OverflowError on every single connection - the opposite of the safe
# fallback this function exists to provide.
_REGISTRATION_TIMEOUT_MAX = 2 ** 31 - 1


def _registration_timeout_from_env():
    """
    How long (seconds) a client has to complete plugin_register, so a
    deployment with e.g. an array that is slow to authenticate can raise it
    without a rebuild.  Falls back to the default if the environment
    variable is absent, empty, not a plain integer, or not a positive
    integer no greater than _REGISTRATION_TIMEOUT_MAX - which also covers
    running a plug-in by hand, outside of lsmd.  Note int() strips leading
    and trailing whitespace, so e.g. "30 " parses as 30 here while the C
    mirror's strtol()-based registration_timeout_seconds() (in
    c_binding/lsm_plugin_ipc.cpp) rejects it; lsmd itself never sets a value
    with either, so this only matters for a hand-set variable.
    """
    env = os.environ.get(_REGISTRATION_TIMEOUT_ENV_NAME)
    if env:
        try:
            value = int(env)
            if 0 < value <= _REGISTRATION_TIMEOUT_MAX:
                return value
        except ValueError:
            pass
    return _REGISTRATION_TIMEOUT_DEFAULT


# Turned into a single absolute deadline covering every read until
# registration, so a peer cannot renew it by sending requests.  Cleared once
# the client registers so slow post-registration operations are never
# interrupted.
REGISTRATION_TIMEOUT = _registration_timeout_from_env()


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
        # either; both expiries arrive as socket.timeout.
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

                    self.tp.send_resp(result)

                    if method == 'plugin_register':
                        need_shutdown = True
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
        except socket.timeout:
            # Client connected but did not complete plugin_register within the
            # allotted time; give up rather than block this worker.  Note this
            # must precede the socket.error handler as socket.timeout is a
            # subclass of it.
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
                error("Unhandled exception in plug-in!\n" +
                      traceback.format_exc())
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
