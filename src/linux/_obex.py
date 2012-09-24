# Copyright (c) 2009 Bea Lam. All rights reserved.
#
# This file is part of LightBlue.
#
# LightBlue is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# LightBlue is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.

# You should have received a copy of the GNU General Public License
# along with LightBlue.  If not, see <http://www.gnu.org/licenses/>.

import types
import datetime

import _lightbluecommon
import _obexcommon
import _lightblueobex    # python extension

from _obexcommon import OBEXError

_HEADER_MASK = 0xc0
_HEADER_UNICODE = 0x00
_HEADER_BYTE_SEQ = 0x40
_HEADER_1BYTE = 0x80
_HEADER_4BYTE = 0xc0


# public attributes
__all__ = ("sendfile", "recvfile", "OBEXClient")



class OBEXClient(object):
    __doc__ = _obexcommon._obexclientclassdoc

    def __init__(self, address, channel):
        if not isinstance(address, types.StringTypes):
            raise TypeError("address must be string, was %s" % type(address))
        if not type(channel) == int:
            raise TypeError("channel must be int, was %s" % type(channel))

        self.__sock = None
        self.__client = None
        self.__serveraddr = (address, channel)
        self.__connectionid = None

    def connect(self, headers={}):
        if self.__client is None:
            self.__setUp()

        try:
            resp = self.__client.request(_lightblueobex.CONNECT,
                    self.__convertheaders(headers), None)
        except IOError, e:
            raise OBEXError(str(e))

        result = self.__createresponse(resp)
        if result.code == _obexcommon.OK:
            self.__connectionid = result.headers.get("connection-id", None)
        else:
            self.__closetransport()
        return result


    def disconnect(self, headers={}):
        self.__checkconnected()
        try:
            try:
                resp = self.__client.request(_lightblueobex.DISCONNECT,
                        self.__convertheaders(headers), None)
            except IOError, e:
                raise OBEXError(str(e))
        finally:
            # close bt connection regardless of disconnect response
            self.__closetransport()
        return self.__createresponse(resp)


    def put(self, headers, fileobj):
        if not hasattr(fileobj, "read"):
            raise TypeError("file-like object must have read() method")
        self.__checkconnected()

        try:
            resp = self.__client.request(_lightblueobex.PUT,
                    self.__convertheaders(headers), None, fileobj)
        except IOError, e:
            raise OBEXError(str(e))
        return self.__createresponse(resp)


    def delete(self, headers):
        self.__checkconnected()
        try:
            resp = self.__client.request(_lightblueobex.PUT,
                    self.__convertheaders(headers), None)
        except IOError, e:
            raise OBEXError(str(e))
        return self.__createresponse(resp)


    def get(self, headers, fileobj):
        if not hasattr(fileobj, "write"):
            raise TypeError("file-like must have write() method")
        self.__checkconnected()
        try:
            resp = self.__client.request(_lightblueobex.GET,
                    self.__convertheaders(headers), None, fileobj)
        except IOError, e:
            raise OBEXError(str(e))
        return self.__createresponse(resp)


    def setpath(self, headers, cdtoparent=False, createdirs=False):
        self.__checkconnected()
        flags = 0
        if cdtoparent:
            flags |= 1
        if not createdirs:
            flags |= 2
        import array
        setpathdata = array.array('B', (flags, 0))  # zero for constants byte
        try:
            resp = self.__client.request(_lightblueobex.SETPATH,
                    self.__convertheaders(headers), buffer(setpathdata))
        except IOError, e:
            raise OBEXError(str(e))
        return self.__createresponse(resp)


    def __setUp(self):
        if self.__client is None:
            import bluetooth
            self.__sock = bluetooth.BluetoothSocket(bluetooth.RFCOMM)
            try:
                self.__sock.connect((self.__serveraddr[0],
                                     self.__serveraddr[1]))
            except bluetooth.BluetoothError, e:
                raise OBEXError(str(e))
            try:
                self.__client = _lightblueobex.OBEXClient(self.__sock.fileno())
            except IOError, e:
                raise OBEXError(str(e))

    def __closetransport(self):
        try:
            self.__sock.close()
        except:
            pass
        self.__connectionid = None
        self.__client = None

    def __checkconnected(self):
        if self.__client is None:
            raise OBEXError("must connect() before sending other requests")

    def __createresponse(self, resp):
        headers = resp[1]
        for hid, value in headers.items():
            if hid == 0x44:
                headers[hid] = _obexcommon._datetimefromstring(value[:])
            elif hid == 0xC4:
                headers[hid] = datetime.datetime.fromtimestamp(value)
            elif type(value) == buffer:
                headers[hid] = value[:]
        return _obexcommon.OBEXResponse(resp[0], headers)

    def __convertheaders(self, headers):
        result = {}
        for header, value in headers.items():
            if isinstance(header, types.StringTypes):
                hid = \
                    _obexcommon._HEADER_STRINGS_TO_IDS.get(header.lower())
            else:
                hid = header
            if hid is None:
                raise ValueError("unknown header '%s'" % header)
            if isinstance(value, datetime.datetime):
                value = value.strftime("%Y%m%dT%H%M%S")
            self.__checkheadervalue(header, hid, value)
            result[hid] = value
        if self.__connectionid is not None:
            result[_lightblueobex.CONNECTION_ID] = self.__connectionid
        return result

    def __checkheadervalue(self, header, hid, value):
        mask = hid & _HEADER_MASK
        if mask == _HEADER_UNICODE:
            if not isinstance(value, types.StringTypes):
                raise TypeError("value for '%s' must be string, was %s" %
                    (str(header), type(value)))
        elif mask == _HEADER_BYTE_SEQ:
            try:
                buffer(value)
            except:
                raise TypeError("value for '%s' must be string, array or other buffer type, was %s" % (str(header), type(value)))
        elif mask == _HEADER_1BYTE:
            if not isinstance(value, int):
                raise TypeError("value for '%s' must be int, was %s" %
                    (str(header), type(value)))
        elif mask == _HEADER_4BYTE:
            if not isinstance(value, int) and not isinstance(value, long):
                raise TypeError("value for '%s' must be int, was %s" %
                    (str(header), type(value)))

    # set method docstrings
    definedmethods = locals()   # i.e. defined methods in OBEXClient
    for name, doc in _obexcommon._obexclientdocs.items():
        try:
            definedmethods[name].__doc__ = doc
        except KeyError:
            pass


# ---------------------------------------------------------------------

def sendfile(address, channel, source):
    if not _lightbluecommon._isbtaddr(address):
        raise TypeError("address '%s' is not a valid bluetooth address" \
            % address)
    if not isinstance(channel, int):
        raise TypeError("channel must be int, was %s" % type(channel))
    if not isinstance(source, types.StringTypes) and \
            not hasattr(source, "read"):
        raise TypeError("source must be string or file-like object with read() method")

    if isinstance(source, types.StringTypes):
        headers = {"name": source}
        fileobj = file(source, "rb")
        closefileobj = True
    else:
        if hasattr(source, "name"):
            headers = {"name": source.name}
        fileobj = source
        closefileobj = False

    client = OBEXClient(address, channel)
    client.connect()

    try:
        resp = client.put(headers, fileobj)
    finally:
        if closefileobj:
            fileobj.close()
        try:
            client.disconnect()
        except:
            pass    # always ignore disconnection errors

    if resp.code != _obexcommon.OK:
        raise OBEXError("server denied the Put request")


# ---------------------------------------------------------------------


# This OBEXObjectPushServer class provides an Object Push server for the
# recvfile() function, and accepts Connect, Disconnect and Put requests.
# It uses the OBEXServer class in the _lightblueobex extension, which provides
# a generic OBEX server class that can handle any type of requests. You can
# use that class to implement other types of OBEX servers, e.g. for the File
# Transfer Profile.

class OBEXObjectPushServer(object):

    def __init__(self, fileno, fileobject):
        if not hasattr(fileobject, "write"):
            raise TypeError("fileobject must be file-like object with write() method")
        self.__fileobject = fileobject
        self.__server = _lightblueobex.OBEXServer(fileno, self.error,
                self.newrequest, self.requestdone)

    def run(self):
        timeout = 60
        self.__gotfile = False
        self.__disconnected = False
        self.__error = None

        while True:
            result = self.__server.process(timeout)
            if result < 0:
                #print "-> error during process()"
                if self.__error is None:
                    self.__error = (OBEXError, "error while running server")
                break
            if result == 0:
                #print "-> process() timed out"
                break
            if self.__gotfile:
                #print "-> got file!"
                break
            if self.__error is not None and not self.__busy:
                #print "-> server error detected..."
                break

        if self.__gotfile:
            # wait briefly for disconnect request
            while not self.__disconnected:
                if self.__server.process(3) <= 0:
                    break

        if not self.__gotfile:
            if self.__error is not None:
                exc, msg = self.__error
                if exc == IOError:
                    exc = OBEXError
                raise exc(msg)

            raise OBEXError("client did not send a file")


    def newrequest(self, opcode, reqheaders, nonheaderdata, hasbody):
        #print "-> newrequest", opcode, reqheaders, nonheaderdata, hasbody
        #print "-> incoming file name:", reqheaders.get(0x01)
        self.__busy = True

        if opcode == _lightblueobex.PUT:
            return (_lightblueobex.SUCCESS, {}, self.__fileobject)
        elif opcode in (_lightblueobex.CONNECT, _lightblueobex.DISCONNECT):
            return (_lightblueobex.SUCCESS, {}, None)
        else:
            return (_lightblueobex.NOT_IMPLEMENTED, {}, None)

    def requestdone(self, opcode):
        #print "-> requestdone", opcode
        if opcode == _lightblueobex.DISCONNECT:
            self.__disconnected = True
        elif opcode == _lightblueobex.PUT:
            self.__gotfile = True
        self.__busy = False

    def error(self, exc, msg):
        #print "-> error:", exc, msg
        if self.__error is not None:
            #print "-> (keeping previous error)"
            return
        self.__error = (exc, msg)


# ---------------------------------------------------------------------

def recvfile(sock, dest):
    if sock is None:
        raise TypeError("Given socket is None")
    if not isinstance(dest, (types.StringTypes, types.FileType)):
        raise TypeError("dest must be string or file-like object with write() method")

    if isinstance(dest, types.StringTypes):
        fileobj = open(dest, "wb")
        closefileobj = True
    else:
        fileobj = dest
        closefileobj = False

    try:
        conn, addr = sock.accept()
        # print "A client connected:", addr
        server = OBEXObjectPushServer(conn.fileno(), fileobj)
        server.run()
        conn.close()
    finally:
        if closefileobj:
            fileobj.close()
