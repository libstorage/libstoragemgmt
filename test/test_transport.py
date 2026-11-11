# SPDX-License-Identifier: LGPL-2.1-or-later
#
# Copyright (C) 2011-2023 Red Hat, Inc.

import importlib.util
import json
import os
import socket
import sys
import threading
import types
import unittest

# Import python_binding/lsm/_transport.py (and the modules it needs) directly,
# bypassing lsm/__init__.py which pulls in the compiled _clib C extension.
_lsm_src_dir = os.path.join(os.path.dirname(__file__), '..', 'python_binding',
                            'lsm')


def _load(name, filename):
    path = os.path.join(_lsm_src_dir, filename)
    spec = importlib.util.spec_from_file_location(name, path)
    mod = importlib.util.module_from_spec(spec)
    sys.modules[name] = mod
    spec.loader.exec_module(mod)
    return mod


_lsm_pkg = types.ModuleType("lsm")
_lsm_pkg.__path__ = [_lsm_src_dir]
sys.modules.setdefault("lsm", _lsm_pkg)

_load("lsm._common", "_common.py")
_load("lsm._data", "_data.py")
_transport = _load("lsm._transport", "_transport.py")

TransPort = _transport.TransPort
LsmError = _transport.LsmError
_DataEncoder = _transport._DataEncoder


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


class TestTransport(unittest.TestCase):

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
