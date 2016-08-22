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

import json
import socket
import string
import os
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

    def _read_all(self, l):
        """
        Reads l number of bytes before returning.  Will raise a SocketEOF
        if socket returns zero bytes (i.e. socket no longer connected)
        """

        if l < 1:
            raise ValueError("Trying to read less than 1 byte!")

        data = bytearray()
        while len(data) < l:
            r = self.s.recv(l - len(data))
            if not r:
                raise _SocketEOF()
            data += r

        return data.decode("utf-8")

    def _send_msg(self, msg):
        """
        Sends the json formatted message by pre-appending the length
        first.
        """

        if msg is None or len(msg) < 1:
            raise ValueError("Msg argument empty")

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
            l = self._read_all(self.HDR_LEN)
            msg = self._read_all(int(l))
            # common.Info("RECV: ", msg)
        except socket.error as e:
            raise LsmError(ErrorNumber.TRANSPORT_COMMUNICATION,
                           "Error while reading a message from the plug-in",
                           str(e))
        return msg

    def __init__(self, socket_descriptor):
        self.s = socket_descriptor

    @staticmethod
    def get_socket(path):
        """
        Returns a connected socket from the passed in path.
        """
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)

            if os.path.exists(path):
                if os.access(path, os.R_OK | os.W_OK):
                    s.connect(path)
                else:
                    raise LsmError(ErrorNumber.PLUGIN_SOCKET_PERMISSION,
                                   "Permissions are incorrect for IPC "
                                   "socket file")
            else:
                raise LsmError(ErrorNumber.PLUGIN_NOT_EXIST,
                               "Plug-in appears to not exist")
        except socket.error:
            # self, code, message, data=None, *args, **kwargs
            raise LsmError(ErrorNumber.PLUGIN_IPC_FAIL,
                           "Unable to connect to lsmd, daemon started?")
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
        e = {'id': msg_id, 'error': {'code': error_code, 'message': msg,
                                     'data': data}}
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
                srv.send_error(
                    msg['id'],
                    msg['params']['errorcode'],
                    msg['params']['errormsg'])
            else:
                srv.send_resp(msg['params'])
            msg = srv.read_req()
        srv.send_resp(msg['params'])
    finally:
        s.close()


class _TestTransport(unittest.TestCase):
    def setUp(self):
        (self.c, self.s) = socket.socketpair(
            socket.AF_UNIX, socket.SOCK_STREAM)

        self.client = TransPort(self.c)

        self.server = threading.Thread(target=_server, args=(self.s,))
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
            self.client.send_req('error', {'errorcode': e_code,
                                           'errormsg': e_msg})
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

            wire = string.zfill(len(data), TransPort.HDR_LEN) + data

            self.assertTrue(len(msg) >= 1)

            for i in wire:
                self.c.send(i)

            reply, msg_id = self.client.read_resp()
            self.assertTrue(payload == reply)

    def tearDown(self):
        self.client.send_req("done", None)
        resp, msg_id = self.client.read_resp()
        self.assertTrue(resp is None)
        self.server.join()


if __name__ == "__main__":
    unittest.main()
