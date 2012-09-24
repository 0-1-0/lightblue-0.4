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

import socket as _socket

try:
    import bluetooth    # pybluez module
    try:
        import _bluetooth   # pybluez internal implementation module
    except:
        import bluetooth._bluetooth as _bluetooth
except ImportError, e:
    raise ImportError("LightBlue requires PyBluez to be installed, " + \
        "cannot find PyBluez 'bluetooth' module: " + str(e))

import _lightbluecommon
import _lightblueutil


# public attributes
__all__ = ("finddevices", "findservices", "finddevicename",
           "gethostaddr", "gethostclass",
           "socket",
           "advertise", "stopadvertise",
           "selectdevice", "selectservice")

# device name cache
_devicenames = {}

# map lightblue protocol values to pybluez ones
_PROTOCOLS = { _lightbluecommon.RFCOMM: bluetooth.RFCOMM,
               _lightbluecommon.L2CAP: bluetooth.L2CAP }


def finddevices(getnames=True, length=10):
    return _SyncDeviceInquiry().run(getnames, length)

def findservices(addr=None, name=None, servicetype=None):
    # This always passes a uuid, to force PyBluez to use BlueZ 'search' instead
    # of 'browse', otherwise some services won't get found. If you use BlueZ's
    # <sdptool search> or <sdptool records> sometimes you'll get services that
    # aren't returned through <sdptool browse> -- I think 'browse' only returns
    # services with recognised protocols or profiles or something.

    if servicetype is None:
        uuid = "0100"   # L2CAP -- i.e. pretty much all services
    elif servicetype == _lightbluecommon.RFCOMM:
        uuid = "0003"
    elif servicetype == _lightbluecommon.OBEX:
        uuid = "0008"
    else:
        raise ValueError("servicetype must be RFCOMM, OBEX or None, was %s" % \
            servicetype)
    try:
        services = bluetooth.find_service(name=name, uuid=uuid, address=addr)
    except bluetooth.BluetoothError, e:
        raise _lightbluecommon.BluetoothError(str(e))

    if servicetype == _lightbluecommon.RFCOMM:
        # OBEX services will be included with RFCOMM services (since OBEX is
        # built on top of RFCOMM), so filter out the OBEX services
        return [_getservicetuple(s) for s in services if not _isobexservice(s)]
    else:
        return [_getservicetuple(s) for s in services]


def finddevicename(address, usecache=True):
    if not _lightbluecommon._isbtaddr(address):
        raise ValueError("%s is not a valid bluetooth address" % str(address))

    if address == gethostaddr():
        return _gethostname()

    if usecache:
        name = _devicenames.get(address)
        if name is not None:
            return name

    name = bluetooth.lookup_name(address)
    if name is None:
        raise _lightbluecommon.BluetoothError(
            "Could not find device name for %s" % address)
    _devicenames[address] = name
    return name


### local device ###

def gethostaddr():
    sock = _gethcisock()
    try:
        try:
            addr = _lightblueutil.hci_read_bd_addr(sock.fileno(), 1000)
        except IOError, e:
            raise _lightbluecommon.BluetoothError(str(e))
    finally:
        sock.close()

    return addr


def gethostclass():
    sock = _gethcisock()
    try:
        try:
            cod = _lightblueutil.hci_read_class_of_dev(sock.fileno(), 1000)
        except IOError, e:
            raise _lightbluecommon.BluetoothError(str(e))
    finally:
        sock.close()

    return _lightbluecommon._joinclass(cod)


def _gethostname():
    sock = _gethcisock()
    try:
        try:
            name = _lightblueutil.hci_read_local_name(sock.fileno(), 1000)
        except IOError, e:
            raise _lightbluecommon.BluetoothError(str(e))
    finally:
        sock.close()
    return name


### socket ###

class _SocketWrapper(object):

    def __init__(self, sock):
        self.__dict__["_sock"] = sock
        self.__dict__["_advertised"] = False
        self.__dict__["_listening"] = False

    # must implement accept() to return _SocketWrapper objects
    def accept(self):
        try:
            # access _sock._sock (i.e. pybluez socket's internal sock)
            # this is so we can raise timeout errors with a different exception
            conn, addr = self._sock._sock.accept()
        except _bluetooth.timeout, te:
            raise _socket.timeout(str(te))
        except _bluetooth.error, e:
            raise _socket.error(str(e))

        # return new _SocketWrapper that wraps a new BluetoothSocket
        newsock = bluetooth.BluetoothSocket(_sock=conn)
        return (_SocketWrapper(newsock), addr)
    accept.__doc__ = _lightbluecommon._socketdocs["accept"]

    def listen(self, backlog):
        if not self._listening:
            self._sock.listen(backlog)
            self._listening = True

    # must implement dup() to return _SocketWrapper objects
    def dup(self):
        return _SocketWrapper(self._sock.dup())
    dup.__doc__ = _lightbluecommon._socketdocs["dup"]

    def getsockname(self):
        sockname = self._sock.getsockname()
        if sockname[1] != 0:
            return (gethostaddr(), sockname[1])
        return sockname     # not connected, should be ("00:00:00:00:00:00", 0)
    getsockname.__doc__ = _lightbluecommon._socketdocs["getsockname"]

    # redefine methods that can raise timeout errors, to access _sock._sock
    # in order to raise timeout errors with a different exception
    # (otherwise they are raised as generic BluetoothException)
    _methoddef = """def %s(self, *args, **kwargs):
        try:
            return self._sock._sock.%s(*args, **kwargs)
        except _bluetooth.timeout, te:
            raise _socket.timeout(str(te))
        except _bluetooth.error, e:
            raise _socket.error(str(e))
        %s.__doc__ = _lightbluecommon._socketdocs['%s']\n"""
    for _m in ("connect", "send", "recv"):
        exec _methoddef % (_m, _m, _m, _m)
    del _m, _methoddef

    # wrap all other socket methods, to set LightBlue-specific docstrings
    _othermethods = [_m for _m in _lightbluecommon._socketdocs.keys() \
        if _m not in locals()]    # methods other than those already defined
    _methoddef = """def %s(self, *args, **kwargs):
        try:
            return self._sock.%s(*args, **kwargs)
        except _bluetooth.error, e:
            raise _socket.error(str(e))
        %s.__doc__ = _lightbluecommon._socketdocs['%s']\n"""
    for _m in _othermethods:
        exec _methoddef % (_m, _m, _m, _m)
    del _m, _methoddef


def socket(proto=_lightbluecommon.RFCOMM):
    # return a wrapped BluetoothSocket
    sock = bluetooth.BluetoothSocket(_PROTOCOLS[proto])
    return _SocketWrapper(sock)


### advertising services ###

def advertise(servicename, sock, serviceclass):
    try:
        if serviceclass == _lightbluecommon.RFCOMM:
            bluetooth.advertise_service(sock._sock,
                                servicename,
                                service_classes=[bluetooth.SERIAL_PORT_CLASS],
                                profiles=[bluetooth.SERIAL_PORT_PROFILE])
        elif serviceclass == _lightbluecommon.OBEX:
            # for pybluez, socket do need to be listening in order to
            # advertise a service, so we'll call listen() here. This should be
            # safe since user shouldn't have called listen() already, because
            # obex.recvfile() docs state that an obex server socket should
            # *not* be listening before recvfile() is called (due to Series60's
            # particular implementation)
            if not sock._listening:
                sock.listen(1)

            # advertise Object Push Profile not File Transfer Profile because
            # obex.recvfile() implementations run OBEX servers which only
            # advertise Object Push operations
            bluetooth.advertise_service(sock._sock,
                                servicename,
                                service_classes=[bluetooth.OBEX_OBJPUSH_CLASS],
                                profiles=[bluetooth.OBEX_OBJPUSH_PROFILE],
                                protocols=["0008"])     # OBEX protocol
        else:
            raise ValueError("Unknown serviceclass, " + \
                "should be either RFCOMM or OBEX constants")
        # set flag
        sock._advertised = True
    except bluetooth.BluetoothError, e:
        raise _lightbluecommon.BluetoothError(str(e))


def stopadvertise(sock):
    if not sock._advertised:
        raise _lightbluecommon.BluetoothError("no service advertised")
    try:
        bluetooth.stop_advertising(sock._sock)
    except bluetooth.BluetoothError, e:
        raise _lightbluecommon.BluetoothError(str(e))
    sock._advertised = False



### GUI ###


def selectdevice():
    import _discoveryui
    return _discoveryui.selectdevice()

def selectservice():
    import _discoveryui
    return _discoveryui.selectservice()


### classes ###



class _SyncDeviceInquiry(object):
    def __init__(self):
        super(_SyncDeviceInquiry, self).__init__()
        self._inquiry = None

    def run(self, getnames=True, length=10):
        self._founddevices = []

        self._inquiry = _MyDiscoverer(self._founddevice, self._inquirycomplete)
        try:
            self._inquiry.find_devices(lookup_names=getnames, duration=length)

            # block until inquiry finishes
            self._inquiry.process_inquiry()
        except bluetooth.BluetoothError, e:
            try:
                self._inquiry.cancel_inquiry()
            finally:
                raise _lightbluecommon.BluetoothError(e)

        return self._founddevices

    def _founddevice(self, address, deviceclass, name):
        self._founddevices.append(_getdevicetuple(address, deviceclass, name))

    def _inquirycomplete(self):
        pass


# subclass of PyBluez DeviceDiscoverer class for customised async discovery
class _MyDiscoverer(bluetooth.DeviceDiscoverer):
    # _MyDiscoverer inherits 2 major methods for discovery:
    # - find_devices(lookup_names=True, duration=8, flush_cache=True)
    # - cancel_inquiry()

    def __init__(self, founddevicecallback, completedcallback):
        bluetooth.DeviceDiscoverer.__init__(self)   # old-style superclass, no super()
        self.founddevicecallback = founddevicecallback
        self.completedcallback = completedcallback

    def device_discovered(self, address, deviceclass, name):
        self.founddevicecallback(address, deviceclass, name)

    def inquiry_complete(self):
        self.completedcallback()



### utility methods ###

def _getdevicetuple(address, deviceclass, name):
    # Return as (addr, name, cod) tuple.
    return (address, name, deviceclass)

def _getservicetuple(service):
    """
    Returns a (addr, port, name) tuple from a PyBluez service dictionary, which
    should have at least:
        - "name": service name
        - "port": service channel/PSM etc.
        - "host": address of host device
    """
    return (service["host"], service["port"], service["name"])


# assume a service is an OBEX-type service if it advertises Object Push, File
# Transfer or Synchronization classes, since their respective profiles are
# listed as using the OBEX protocol in the bluetooth specs
_obexserviceclasses = (
    bluetooth.OBEX_OBJPUSH_CLASS,
    bluetooth.OBEX_FILETRANS_CLASS,
    bluetooth.IRMC_SYNC_CMD_CLASS
    )
def _isobexservice(service):
    for sc in service["service-classes"]:
        if sc in _obexserviceclasses:
            return True
    return False


# Gets HCI socket thru PyBluez. Remember to close the returned socket.
def _gethcisock(devid=-1):
    try:
        sock = _bluetooth.hci_open_dev(devid)
    except Exception, e:
        raise _lightbluecommon.BluetoothError(
            "Cannot access local device: " + str(e))
    return sock


