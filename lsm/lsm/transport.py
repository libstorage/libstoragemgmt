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

import json
import socket
import string
from common import SocketEOF, LsmError
from data import DataDecoder, DataEncoder
import unittest
import threading
import common

class Transport(object):
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

    def __readAll( self, l ):
        """
        Reads l number of bytes before returning.  Will raise a SocketEOF
        if socket returns zero bytes (i.e. socket no longer connected)
        """

        if l < 1:
            raise ValueError("Trying to read less than 1 byte!")

        data = ""
        while len(data) < l:
            r = self.s.recv(l - len(data))
            if not r:
                raise SocketEOF()
            data += r

        return data

    def __sendMsg(self, msg):
        """
        Sends the json formatted message by pre-appending the length
        first.
        """

        if msg is None or len(msg) < 1:
            raise ValueError("Msg argument empty")

        #Note: Don't catch io exceptions at this level!
        s = string.zfill(len(msg), self.HDR_LEN) + msg
        #common.Info("SEND: ", msg)
        self.s.sendall(s)

    def __recvMsg(self):
        """
        Reads header first to get the length and then the remaining
        bytes of the message.
        """
        try:
            l = self.__readAll(self.HDR_LEN)
            msg = self.__readAll(int(l))
            #common.Info("RECV: ", msg)
        except socket.error as e:
            raise LsmError(common.ErrorNumber.TRANSPORT_COMMUNICATION,
                "Error while reading a message from the plug-in", str(e))
        return msg

    def __init__(self, socket):
        self.s = socket

    @staticmethod
    def getSocket(path):
        """
        Returns a connected socket from the passed in path.
        """
        try:
            s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            s.connect(path)
        except socket.error:
            #self, code, message, data=None, *args, **kwargs
            raise LsmError(common.ErrorNumber.NO_CONNECT, "Unable to connect "
                                                          "to lsmd, daemon started?")
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
            data = json.dumps(msg, cls=DataEncoder)
            self.__sendMsg(data)
        except socket.error as se:
            raise LsmError(common.ErrorNumber.TRANSPORT_COMMUNICATION,
                            "Error while sending a message to the plug-in",
                            str(se))

    def read_req(self):
        """
        Reads a message and returns the parsed version of it.
        """
        data = self.__recvMsg()
        if len(data):
            #common.Info(str(data))
            return json.loads(data, cls=DataDecoder)

    def rpc(self, method, args):
        """
        Sends a request and waits for a response.
        """
        self.send_req(method, args)
        (reply, id) = self.read_resp()
        assert id == 100
        return reply

    def send_error(self, id, error_code, msg, data=None):
        """
        Used to transmit an error.
        """
        e = {'id': id, 'error': {'code': error_code,
                                 'message': msg,
                                 'data': data}}
        self.__sendMsg(json.dumps(e, cls=DataEncoder))

    def send_resp(self, result, id=100):
        """
        Used to transmit a response
        """
        r = {'id': id, 'result': result}
        self.__sendMsg(json.dumps(r, cls=DataEncoder))

    def read_resp(self):
        data = self.__recvMsg()
        resp = json.loads(data, cls=DataDecoder)

        if 'result' in resp:
            return resp['result'], resp['id']
        else:
            e = resp['error']
            raise LsmError(**e)


def server(s):
    """
    Test echo server for test case.
    """
    server = Transport(s)

    msg = server.read_req()

    try:
        while msg['method'] != 'done':

            if msg['method'] == 'error':
                server.send_error(msg['id'], msg['params']['errorcode'],
                                    msg['params']['errormsg'])
            else:
                server.send_resp(msg['params'])
            msg = server.read_req()
        server.send_resp(msg['params'])
    finally:
        s.close()


class TestTransport(unittest.TestCase):
    def setUp(self):
        (self.c,self.s) = socket.socketpair(socket.AF_UNIX, socket.SOCK_STREAM)

        self.client = Transport(self.c)

        self.server = threading.Thread(target=server, args=(self.s,))
        self.server.start()

    def test_simple(self):
        tc = [ '0', ' ', '   ', '{}:""', "Some text message", 'DEADBEEF']

        for t in tc:
            self.client.send_req('test', t)
            reply, id = self.client.read_resp()
            self.assertTrue( id == 100)
            self.assertTrue( reply == t )


    def test_exceptions(self):

        e_msg = 'Test error message'
        e_code = 100

        self.client.send_req('error', {'errorcode':e_code, 'errormsg':e_msg} )
        self.assertRaises(LsmError, self.client.read_resp)

        try:
            self.client.send_req('error', {'errorcode':e_code, 'errormsg':e_msg} )
            self.client.read_resp()
        except LsmError as e:
            self.assertTrue(e.code == e_code)
            self.assertTrue(e.msg == e_msg)

    def test_slow(self):

        #Try to test the receiver getting small chunks to read
        #in a loop
        for l in range(1, 4096, 10 ):

            payload = "x" * l
            msg = {'method': 'drip', 'id': 100, 'params': payload}
            data = json.dumps(msg, cls=DataEncoder)

            wire = string.zfill(len(data), Transport.HDR_LEN) + data

            self.assertTrue(len(msg) >= 1)

            for i in wire:
                self.c.send(i)

            reply, id = self.client.read_resp()
            self.assertTrue(payload == reply)

    def tearDown(self):
        self.client.send_req("done", None)
        resp, id = self.client.read_resp()
        self.assertTrue(resp is None)
        self.server.join()


if __name__ == "__main__":
    unittest.main()
