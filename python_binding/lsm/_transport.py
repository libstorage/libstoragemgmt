# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2011-2023 Red Hat, Inc.
#
# Author: Tony Asleson <tasleson@redhat.com>

import json
import socket
import os
import time
import unittest
import threading

from lsm._common import LsmError, ErrorNumber
from lsm._common import SocketEOF as _SocketEOF
from lsm._data import DataDecoder as _DataDecoder
from lsm._data import DataEncoder as _DataEncoder



class TransPort(object):
    """
    Provides wire serialization by using json.  Loosely conforms to json-rpc,
    however a length header was added so that we would have the ability to use
    non sax like json parsers, which are more abundant.

    <Zero padded 10 digit number [1..2**32] for the length followed by
    valid json.

    Notes:
    id field (json-rpc) is present but currently not being used.
    This is available to be expanded on later.
    """

    HDR_LEN = 10

    # Matches the overflow guard in c_binding/lsm_ipc.cpp Transport::msg_recv
    MAX_MSG_LEN = 0x80000000

    def _read_all(self, l):
        """
        Reads l number of bytes before returning.  Will raise a SocketEOF
        if socket returns zero bytes (i.e. socket no longer connected), or
        socket.timeout if the deadline armed by set_io_deadline() passes
        before the read completes.
        """

        if l < 1:
            raise ValueError("Trying to read less than 1 byte!")

        data = bytearray()
        while len(data) < l:
            if self._io_deadline is not None:
                # settimeout() bounds a single recv() call, so each read gets
                # what is left of the deadline rather than a fresh copy of the
                # full timeout.  Note settimeout(0) puts the socket in
                # non-blocking mode, so an expired deadline has to be handled
                # here instead of being passed on.
                remaining = self._io_deadline - time.monotonic()
                if remaining <= 0:
                    raise socket.timeout("timed out")
                self.s.settimeout(remaining)
            r = self.s.recv(l - len(data))
            if not r:
                raise _SocketEOF()
            data += r

        return data.decode("utf-8")

    def _send_msg(self, msg):
        """
        Sends the json formatted message by pre-appending the length
        first.  Raises socket.timeout if the deadline armed by
        set_io_deadline() passes before the message is written; a peer that
        stops reading our replies fills the socket buffer and would otherwise
        block us here forever.
        """

        if msg is None or len(msg) < 1:
            raise ValueError("Msg argument empty")

        if self._io_deadline is not None:
            # Same handling as _read_all(): settimeout(0) would put the
            # socket in non-blocking mode, so an expired deadline is dealt
            # with here rather than being passed on.
            remaining = self._io_deadline - time.monotonic()
            if remaining <= 0:
                raise socket.timeout("timed out")
            self.s.settimeout(remaining)

        # Note: Don't catch io exceptions at this level!
        s = str.zfill(str(len(msg)), self.HDR_LEN) + msg
        # common.Info("SEND: ", msg)
        self.s.sendall(bytes(s.encode('utf-8')))

    def _recv_msg(self):
        """
        Reads header first to get the length and then the remaining
        bytes of the message.
        """
        try:
            num_bytes = self._read_all(self.HDR_LEN)
            length = int(num_bytes)
            if length >= self.MAX_MSG_LEN:
                raise LsmError(
                    ErrorNumber.TRANSPORT_COMMUNICATION,
                    "Message length of %d exceeds maximum allowed of %d" %
                    (length, self.MAX_MSG_LEN))
            msg = self._read_all(length)
            # common.Info("RECV: ", msg)
        except socket.timeout:
            # A receive timeout is distinct from a generic communication
            # error; let it propagate so the caller can act on it (e.g. a
            # plug-in giving up on an un-registered client).
            raise
        except socket.error as e:
            raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION,
                           "Error while reading a message from the plug-in",
                           str(e))
        except _SocketEOF:
            raise LsmError(
                ErrorNumber.TRANSPORT_COMMUNICATION,
                "Error while reading a message from the plug-in, EOF")
        return msg

    def __init__(self, socket_descriptor):
        self.s = socket_descriptor
        self._io_deadline = None

    def set_io_deadline(self, seconds):
        """
        Arms (or clears) an absolute I/O deadline.  The deadline expires the
        given number of seconds from now and covers every subsequent read and
        write, not just the next one: sending or receiving a message does not
        buy any more time.  Pass None to clear it and return to blocking
        behavior.

        Mirrors Transport::io_deadline() in c_binding/lsm_ipc.cpp, except
        that sendall() bounds the whole write against the deadline, where
        the C side can overshoot it by one send() call.
        """
        if seconds is not None:
            self._io_deadline = time.monotonic() + seconds
            # The socket timeout is left alone here; each read and write sets
            # it to whatever is left of the deadline.
            return

        self._io_deadline = None
        self.s.settimeout(None)

    @staticmethod
    def get_socket(path):
        """
        Returns a connected socket from the passed in path.
        """
        s = None
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

            if os.path.exists(path):
                if os.access(path, os.R_OK | os.W_OK):
                    s.connect(path)
                else:
                    raise LsmError(
                        ErrorNumber.PLUGIN_SOCKET_PERMISSION,
                        "Permissions are incorrect for IPC "
                        "socket file")
            else:
                raise LsmError(ErrorNumber.PLUGIN_NOT_EXIST,
                               "Plug-in appears to not exist")
        except socket.error:
            if s is not None:
                s.close()
            # self, code, message, data=None, *args, **kwargs
            raise LsmError(ErrorNumber.PLUGIN_IPC_FAIL,
                           "Unable to connect to lsmd, daemon started?")
        except LsmError:
            if s is not None:
                s.close()
            raise
        return s

    def close(self):
        """
        Closes the transport and the underlying socket
        """
        self.s.close()

    def send_req(self, method, args):
        """
        Sends a request given a method and arguments.
        Note: arguments must be in the form that can be automatically
        serialized to json
        """
        try:
            msg = {'method': method, 'id': 100, 'params': args}
            data = json.dumps(msg, cls=_DataEncoder)
            self._send_msg(data)
        except socket.timeout:
            # As in _recv_msg(), a deadline expiry is distinct from a generic
            # communication error; let the caller act on it.
            raise
        except socket.error as se:
            raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION,
                           "Error while sending a message to the plug-in",
                           str(se))

    def read_req(self):
        """
        Reads a message and returns the parsed version of it.
        """
        data = self._recv_msg()
        if len(data):
            # common.Info(str(data))
            return json.loads(data, cls=_DataDecoder)

    def rpc(self, method, args):
        """
        Sends a request and waits for a response.
        """
        self.send_req(method, args)
        (reply, msg_id) = self.read_resp()
        assert msg_id == 100
        return reply

    def send_error(self, msg_id, error_code, msg, data=None):
        """
        Used to transmit an error.
        """
        e = {
            'id': msg_id,
            'error': {
                'code': error_code,
                'message': msg,
                'data': data
            }
        }
        self._send_msg(json.dumps(e, cls=_DataEncoder))

    def send_resp(self, result, msg_id=100):
        """
        Used to transmit a response
        """
        r = {'id': msg_id, 'result': result}
        self._send_msg(json.dumps(r, cls=_DataEncoder))

    def read_resp(self):
        data = self._recv_msg()
        resp = json.loads(data, cls=_DataDecoder)

        if 'result' in resp:
            return resp['result'], resp['id']
        else:
            e = resp['error']
            raise LsmError(**e)


def _server(s):
    """
    Test echo server for test case.
    """
    srv = TransPort(s)

    msg = srv.read_req()

    try:
        while msg['method'] != 'done':

            if msg['method'] == 'error':
                srv.send_error(msg['id'], msg['params']['errorcode'],
                               msg['params']['errormsg'])
            else:
                srv.send_resp(msg['params'])
            msg = srv.read_req()
        srv.send_resp(msg['params'])
    finally:
        s.close()


class _TestTransport(unittest.TestCase):

    def setUp(self):
        (self.c, self.s) = socket.socketpair(socket.AF_UNIX,
                                             socket.SOCK_STREAM)

        self.client = TransPort(self.c)

        self.server = threading.Thread(target=_server, args=(self.s, ))
        self.server.start()

    def test_simple(self):
        tc = ['0', ' ', '   ', '{}:""', "Some text message", 'DEADBEEF']

        for t in tc:
            self.client.send_req('test', t)
            reply, msg_id = self.client.read_resp()
            self.assertTrue(msg_id == 100)
            self.assertTrue(reply == t)

    def test_exceptions(self):

        e_msg = 'Test error message'
        e_code = 100

        self.client.send_req('error', {'errorcode': e_code, 'errormsg': e_msg})
        self.assertRaises(LsmError, self.client.read_resp)

        try:
            self.client.send_req('error', {
                'errorcode': e_code,
                'errormsg': e_msg
            })
            self.client.read_resp()
        except LsmError as e:
            self.assertTrue(e.code == e_code)
            self.assertTrue(e.msg == e_msg)

    def test_slow(self):

        # Try to test the receiver getting small chunks to read
        # in a loop
        for l in range(1, 4096, 10):

            payload = "x" * l
            msg = {'method': 'drip', 'id': 100, 'params': payload}
            data = json.dumps(msg, cls=_DataEncoder)
            hdr = str(len(data))

            wire = hdr.zfill(TransPort.HDR_LEN) + data

            self.assertTrue(len(msg) >= 1)

            for i in wire:
                self.c.send(bytes(i, 'utf-8'))

            reply, msg_id = self.client.read_resp()
            self.assertTrue(payload == reply)

    def test_io_deadline(self):
        # With a receive deadline armed, an incomplete (or absent) frame must
        # surface as socket.timeout rather than being folded into a generic
        # LsmError, so callers (e.g. a plug-in waiting on an un-registered
        # client) can react to it.  b'' exercises a stalled header read;
        # b'00000' exercises a partial header.
        for partial in (b'', b'00000'):
            c, s = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
            try:
                client = TransPort(c)
                client.set_io_deadline(0.25)
                if partial:
                    s.sendall(partial)
                self.assertRaises(socket.timeout, client._recv_msg)
                # Clearing the deadline returns to blocking behavior.
                client.set_io_deadline(None)
                self.assertIsNone(c.gettimeout())
            finally:
                c.close()
                s.close()

    def _drip_server(self, s, header, count, interval, initial_delay=0):
        """
        Waits 'initial_delay', sends 'header', then sends 'count' single
        payload bytes 'interval' apart - never enough to complete the payload
        the header declares.  Returns the started thread.
        """

        def drip():
            try:
                time.sleep(initial_delay)
                if header:
                    s.sendall(header)
                for _ in range(count):
                    time.sleep(interval)
                    s.sendall(b'x')
            except OSError:
                return

        t = threading.Thread(target=drip)
        t.start()
        return t

    def test_io_deadline_bounds_whole_message(self):
        # A peer declares a 50 byte payload, then keeps sending a trickle of
        # payload bytes one at a time, each arriving well inside the
        # deadline, but never enough to complete the declared payload.  The
        # deadline must bound the whole read, not just each individual recv()
        # call, otherwise this drip defeats it and _recv_msg() never raises.
        c, s = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            client = TransPort(c)
            client.set_io_deadline(0.3)
            dripper = self._drip_server(s, b'0000000050', 8, 0.15)
            try:
                start = time.monotonic()
                self.assertRaises(socket.timeout, client._recv_msg)
                elapsed = time.monotonic() - start
                self.assertLess(elapsed, 1.0)
            finally:
                dripper.join()
        finally:
            c.close()
            s.close()

    def test_io_deadline_spans_header_and_payload(self):
        # The header can land just before the deadline; the payload read that
        # follows must inherit what is left of it rather than starting with a
        # fresh full timeout of its own, which would let a peer stretch a
        # single message to roughly twice the configured bound.
        c, s = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            client = TransPort(c)
            client.set_io_deadline(1.0)
            # A complete header at ~0.9s, then the peer stalls forever.
            late = self._drip_server(s, b'0000000050', 0, 0,
                                     initial_delay=0.9)
            try:
                start = time.monotonic()
                self.assertRaises(socket.timeout, client._recv_msg)
                elapsed = time.monotonic() - start
                self.assertLess(
                    elapsed, 1.5,
                    "payload read started a fresh timeout (elapsed %.2fs)" %
                    elapsed)
            finally:
                late.join()
        finally:
            c.close()
            s.close()

    def test_io_deadline_spans_messages(self):
        # A deadline that restarted on every message would let an
        # un-registered peer keep a plug-in alive forever by sending one
        # junk-but-parseable request just inside each window.  Reading a
        # complete message must not buy any more time.
        c, s = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            client = TransPort(c)
            peer = TransPort(s)
            client.set_io_deadline(1.0)

            # The request arrives late in the deadline, so a deadline that
            # restarted here would buy the peer a whole extra window.
            def late_request():
                time.sleep(0.8)
                try:
                    peer.send_req('systems', None)
                except OSError:
                    pass

            sender = threading.Thread(target=late_request)
            sender.start()
            try:
                start = time.monotonic()
                msg = client.read_req()
                self.assertEqual(msg['method'], 'systems')

                # Peer has gone quiet; the original deadline still applies.
                self.assertRaises(socket.timeout, client._recv_msg)
                elapsed = time.monotonic() - start
                self.assertLess(
                    elapsed, 1.5,
                    "deadline restarted after a successful read "
                    "(elapsed %.2fs)" % elapsed)
            finally:
                sender.join()
        finally:
            c.close()
            s.close()

    def test_io_deadline_bounds_send(self):
        # A peer that chatters requests and never reads the replies fills the
        # socket buffer; unless the deadline covers writes too, sendall()
        # blocks forever and pins the worker just as effectively as a stalled
        # read does.  Run in a thread so a regression fails rather than hangs.
        c, s = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        outcome = {}

        def sender():
            client = TransPort(c)
            client.set_io_deadline(0.3)
            start = time.monotonic()
            try:
                # Far more than the socket buffer holds; s never reads.
                for _ in range(64):
                    client.send_resp('x' * 65536)
                outcome['result'] = 'completed'
            except socket.timeout:
                outcome['result'] = 'timeout'
            except OSError as e:
                outcome['result'] = 'oserror: %s' % e
            outcome['elapsed'] = time.monotonic() - start

        sending = threading.Thread(target=sender)
        sending.daemon = True
        sending.start()
        try:
            sending.join(timeout=5)
            self.assertFalse(
                sending.is_alive(),
                "send to a peer that never reads was not bounded")
            self.assertEqual(outcome.get('result'), 'timeout')
            self.assertLess(
                outcome['elapsed'], 2.0,
                "deadline did not bound the send (elapsed %.2fs)" %
                outcome['elapsed'])
        finally:
            c.close()
            s.close()

    def test_oversized_message_rejected(self):
        # A header declaring a length >= MAX_MSG_LEN must be rejected before
        # ever attempting to read that many bytes, matching the overflow
        # guard in c_binding/lsm_ipc.cpp Transport::msg_recv.
        c, s = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)
        try:
            client = TransPort(c)
            hdr = str(TransPort.MAX_MSG_LEN).zfill(TransPort.HDR_LEN)
            s.sendall(hdr.encode('utf-8'))
            self.assertRaises(LsmError, client._recv_msg)
        finally:
            c.close()
            s.close()

    def tearDown(self):
        self.client.send_req("done", None)
        resp, msg_id = self.client.read_resp()
        self.assertTrue(resp is None)
        self.server.join()
        self.client.close()


if __name__ == "__main__":
    unittest.main()
