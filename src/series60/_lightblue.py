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
import _lightbluecommon

# public attributes
__all__ = ("finddevices", "findservices", "finddevicename", 
           "gethostaddr", "gethostclass",
           "socket", 
           "advertise", "stopadvertise", 
           "selectdevice", "selectservice")

# details of advertised services
__advertised = {}

def finddevices(getnames=True, length=10):
    # originally this used DiscoverDevices in _lightblueutil extension, but
    # that blocks the UI

    import e32

    inquiry = _DeviceInquiry()
    inquiry.start(getnames, length)
    
    timer = None
    try:
        while not inquiry.isdone():
            # keep waiting
            timer = e32.Ao_timer()
            timer.after(0.1)
    finally:
        inquiry.stop()
        if timer is not None: timer.cancel()
    
    return inquiry.getfounddevices()

def findservices(addr=None, name=None, servicetype=None):
    if servicetype is None:
        funcs = (_socket.bt_discover, _socket.bt_obex_discover)
    elif servicetype == _lightbluecommon.RFCOMM:
        funcs = (_socket.bt_discover, )
    elif servicetype == _lightbluecommon.OBEX:
        funcs = (_socket.bt_obex_discover, )
    else:
        raise ValueError("servicetype must be RFCOMM, OBEX or None, was %s" % \
            servicetype)

    if addr is None:
        devices = finddevices()
        btaddrs = [d[0] for d in devices]
    else:
        btaddrs = [addr]
        
    services = []
    for addr in btaddrs:
        for func in funcs:
            try:
                devaddr, servicesdict = func(addr)
            except _socket.error, e:
                #raise _lightbluecommon.BluetoothError(str(e))
                print "[lightblue] cannot look up services for %s" % addr
                continue
            if name is not None:
                for servicename in servicesdict.keys():
                    if servicename != name:
                        del servicesdict[servicename]
            services.extend(_getservicetuples(devaddr, servicesdict))
    return services

def finddevicename(address, usecache=True):
    if not _lightbluecommon._isbtaddr(address):
        raise ValueError("%s is not a valid bluetooth address" % str(address))
        
    if address == gethostaddr():
        return _gethostname()
    
    try:
        # lookupName() expects address without colon separators
        import _lightblueutil        
        address_no_sep = address.replace(":", "").replace("-", "")
        name = _lightblueutil.lookupName(address_no_sep, (not usecache))
    except SymbianError, e:
        raise _lightbluecommon.BluetoothError(
            "Cannot find device name for %s: %s" % (address, str(e)))
    return name

def gethostaddr():
    import _lightblueutil
    try:
        addr = _lightblueutil.getLocalAddress()
    except SymbianError, exc:
        raise _lightbluecommon.BluetoothError(
            "Cannot read local device address: " + str(exc))
    return addr

def gethostclass():
    import _lightblueutil
    try:
        cod = _lightblueutil.getLocalDeviceClass()
    except SymbianError, exc:
        raise _lightbluecommon.BluetoothError(
            "Cannot read local device class: " + str(exc))
    return cod

def _gethostname():
    import _lightblueutil
    try:
        name = _lightblueutil.getLocalName()
    except SymbianError, exc:
        raise _lightbluecommon.BluetoothError(
            "Cannot read local device name: " + str(exc))
    return name

class _SocketWrapper(object):

    def __init__(self, sock, connaddr=()):
        self.__dict__["_sock"] = sock
        self._setconnaddr(connaddr)

    # must implement accept() to return _SocketWrapper objects        
    def accept(self):
        conn, addr = self._sock.accept()
        
        # modify returned address cos PyS60 accept() only returns address, not 
        # (addr, channel) tuple
        addrtuple = (addr.upper(), self._connaddr[1]) 
        return (_SocketWrapper(conn, addrtuple), addrtuple)
    accept.__doc__ = _lightbluecommon._socketdocs["accept"]        
        
    def bind(self, addr):
        # if port==0, find an available port
        if addr[1] == 0:
            addr = (addr[0], _getavailableport(self))
        try:
            self._sock.bind(addr)
        except Exception, e:
            raise _socket.error(str(e))
        self._setconnaddr(addr)
    bind.__doc__ = _lightbluecommon._socketdocs["bind"]

    def close(self):
        self._sock.close()
        
        # try to stop advertising
        try:
            stopadvertise(self)
        except:
            pass        
    close.__doc__ = _lightbluecommon._socketdocs["close"]        

    def connect(self, addr):
        self._sock.connect(addr)
        self._setconnaddr(addr)
    connect.__doc__ = _lightbluecommon._socketdocs["connect"]        

    def connect_ex(self, addr): 
        try:
            self.connect(addr)
        except _socket.error, e:
            return e.args[0]
        return 0
    connect_ex.__doc__ = _lightbluecommon._socketdocs["connect_ex"]                
                
    # must implement dup() to return _SocketWrapper objects                            
    def dup(self): 
        return _SocketWrapper(self._sock.dup())
    dup.__doc__ = _lightbluecommon._socketdocs["dup"]        
            
    def listen(self, backlog):
        self._sock.listen(backlog)
        
        # when listen() is called, set a default security level since S60
        # sockets are required to have a security level
        # This should be changed later to allow to set security using
        # setsockopt()
        _socket.set_security(self._sock, _socket.AUTH)
    listen.__doc__ = _lightbluecommon._socketdocs["listen"]        
    
    # PyS60 raises socket.error("Bad protocol") when this is called for stream
    # sockets, but implement it here like recv() for consistency with Linux+Mac
    def recvfrom(self, bufsize, flags=0):
        return (self._sock.recv(bufsize, flags), None)
    recvfrom.__doc__ = _lightbluecommon._socketdocs["recvfrom"]        

    # PyS60 raises socket.error("Bad protocol") when this is called for stream
    # sockets, but implement it here like send() for consistency with Linux+Mac
    def sendto(self, data, *extra):
        if len(extra) == 1:
            address = extra[0]
            flags = 0
        elif len(extra) == 2:
            flags, address = extra
        else:
            raise TypeError("sendto takes at most 3 arguments (%d given)" % \
                (len(extra) + 1))
        return self._sock.send(data, flags)     
    sendto.__doc__ = _lightbluecommon._socketdocs["sendto"]     
    
    # sendall should return None on success but PyS60 seems to have it return
    # bytes sent like send
    def sendall(self, data, flags=0):
        self.send(data, flags)
        return None
    sendall.__doc__ = _lightbluecommon._socketdocs["sendall"]       
        
    # implement to return (remote-address, common-channel) like PyBluez
    # (PyS60 implementation raises error when this method is called, saying
    # it's not implemented - maybe cos a remote BT socket doesn't really have 
    # an outgoing channel like TCP sockets? But it seems handy to return the
    # channel we're communicating over anyway i.e. the local RFCOMM channel)
    def getpeername(self):
        if not self._connaddr:
            raise _socket.error(57, "Socket is not connected")
        return self._connaddr         
    getpeername.__doc__ = _lightbluecommon._socketdocs["getpeername"]        
    
    # like getpeername(), PyS60 does not implement this method
    def getsockname(self):
        if not self._connaddr:     # sock is neither bound nor connected
            return ("00:00:00:00:00:00", 0)
        return (gethostaddr(), self._connaddr[1])
    getsockname.__doc__ = _lightbluecommon._socketdocs["getsockname"]

    def fileno(self): 
        raise NotImplementedError
    fileno.__doc__ = _lightbluecommon._socketdocs["fileno"]        

    def settimeout(self, timeout): 
        raise NotImplementedError
    settimeout.__doc__ = _lightbluecommon._socketdocs["settimeout"]                

    def gettimeout(self): 
        return None
    gettimeout.__doc__ = _lightbluecommon._socketdocs["gettimeout"]                

    def _setconnaddr(self, connaddr):
        if len(connaddr) == 2:
            connaddr = (connaddr[0].upper(), connaddr[1])
        self.__dict__["_connaddr"] = connaddr             
        
    # wrap all other socket methods, to set LightBlue-specific docstrings
    _othermethods = [_m for _m in _lightbluecommon._socketdocs.keys() \
        if _m not in locals()]    # methods other than those already defined
    _methoddef = """def %s(self, *args, **kwargs):
        return self._sock.%s(*args, **kwargs)
        %s.__doc__ = _lightbluecommon._socketdocs['%s']\n"""
    for _m in _othermethods:
        exec _methoddef % (_m, _m, _m, _m)
    del _m, _methoddef     
             
def socket(proto=_lightbluecommon.RFCOMM):
    if proto == _lightbluecommon.L2CAP:
        raise NotImplementedError("L2CAP sockets not supported on this platform")
    sock = _socket.socket(_socket.AF_BT, _socket.SOCK_STREAM, 
                          _socket.BTPROTO_RFCOMM)
    return _SocketWrapper(sock)

def _getavailableport(sock):
    # can just use bt_rfcomm_get_available_server_channel since only RFCOMM is 
    # currently supported
    return _socket.bt_rfcomm_get_available_server_channel(sock._sock)

def advertise(name, sock, servicetype):
    if servicetype == _lightbluecommon.RFCOMM:
        servicetype = _socket.RFCOMM
    elif servicetype == _lightbluecommon.OBEX:
        servicetype = _socket.OBEX
    else:
        raise ValueError("servicetype must be either RFCOMM or OBEX")        
    name = unicode(name)        
    
    # advertise the service
    _socket.bt_advertise_service(name, sock._sock, True, servicetype)
    
    # note details, for if advertising needs to be stopped later
    __advertised[id(sock)] = (name, servicetype)
                                          
def stopadvertise(sock):
    details = __advertised.get(id(sock))
    if details is None:
        raise _lightbluecommon.BluetoothError("no service advertised")
    
    name, servicetype = details
    _socket.bt_advertise_service(name, sock._sock, False, servicetype)

def selectdevice():
    import _lightblueutil
    try:
        result = _lightblueutil.selectDevice()
    except SymbianError, e:
        raise _lightbluecommon.BluetoothError(str(e))
        
    # underlying method returns class of device as tuple, not whole class
    devinfo = (result[0], result[1], _lightbluecommon._joinclass(result[2]))
    return devinfo

def selectservice():
    device = selectdevice()
    if device is None: 
        return None
            
    import appuifw
    services = findservices(addr=device[0])
    choice = appuifw.popup_menu(
        [unicode("%d: %s" % (s[1], s[2])) for s in services], 
        u"Choose service:")
    if choice is None:
        return None
    return services[choice]

# Returns a list of (addr, channel, name) service tuples from a device 
# address and a dictionary of {name: channel} mappings.
def _getservicetuples(devaddr, servicesdict):
    return [(devaddr.upper(), channel, name) for name, channel in servicesdict.items()]
        
class _DeviceInquiry(object):

    def __init__(self):
        super(_DeviceInquiry, self).__init__()
        self._founddevices = []
        self._resolver = None
        self._done = False

    def start(self, getnames, length):   
        self._founddevices = []
        self._done = False
        
        import _lightblueutil        
        self._resolver = _lightblueutil.AoResolver()
        self._resolver.open()
        self._resolver.discover(self._founddevice, None, getnames)
        
    def isdone(self):
        return self._done
        
    def stop(self):
        if self.isdone():
            return
        
        if self._resolver:
            self._resolver.cancel()
            self._resolver.close()
            self._done = True
        
    def getfounddevices(self):
        return self._founddevices[:]

    def _founddevice(self, err, addr, name, devclass, param):
        try:    
            if err == 0:  # no err
                #print "Found device", addr
                
                # PDIS AoResolver returns addres without the colons
                addr = addr[0:2] + ":" + addr[2:4] + ":" + addr[4:6] + ":" + \
                       addr[6:8] + ":" + addr[8:10] + ":" + addr[10:12]
                
                devinfo = (addr.encode("utf-8").upper(),
                           name,
                           _lightbluecommon._joinclass(devclass)) 
                self._founddevices.append(devinfo)
                
                # keep looking for devices
                self._resolver.next()
            else:
                if err == -25:    # KErrEof (no more devices) 
                    # finished discovery
                    self._resolver.close()
                    self._done = True
                else:
                    print "[lightblue] device discovery error (%d)" % err
                    
        except Exception, e:
            # catch all exceptions, the app will crash if exception is raised
            # during callback
            print "Error during _founddevice() callback: "+ str(e)
            