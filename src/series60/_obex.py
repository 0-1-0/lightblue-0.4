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
import os
import types

import _lightbluecommon
from _obexcommon import OBEXError

# public attributes
__all__ = ("sendfile", "recvfile")

def sendfile(address, channel, source):
    if not isinstance(source, (types.StringTypes, types.FileType)):
        raise TypeError("source must be string or built-in file object")
        
    if isinstance(source, types.StringTypes):
        try:
            _socket.bt_obex_send_file(address, channel, unicode(source))
        except Exception, e:
            raise OBEXError(str(e))
    else:
        # given file object
        if hasattr(source, "name"):
            localpath = _tempfilename(source.name)
        else:
            localpath = _tempfilename()
        
        try:
            # write the source file object's data into a file, then send it
            f = file(localpath, "wb")
            f.write(source.read())
            f.close()
            try:            
                _socket.bt_obex_send_file(address, channel, unicode(localpath))
            except Exception, e:
                raise OBEXError(str(e))                
        finally:
            # remove temporary file
            if os.path.isfile(localpath):
                try:
                    os.remove(localpath)
                except Exception, e:
                    print "[lightblue.obex] unable to remove temporary file %s: %s" %\
                        (localpath, str(e))

def recvfile(sock, dest):
    if not isinstance(dest, (types.StringTypes, types.FileType)):
        raise TypeError("dest must be string or built-in file object")     
    
    if isinstance(dest, types.StringTypes):
        _recvfile(sock, dest)
    else:
        # given file object
        localpath = _tempfilename()
        
        try:
            # receive a file and then read it into the file object
            _recvfile(sock, localpath)
            
            recvdfile = file(localpath, "rb")
            dest.write(recvdfile.read())
            recvdfile.close()
        finally:
            # remove temporary file        
            if os.path.isfile(localpath):
                try:
                    os.remove(localpath)
                except Exception, e:
                    print "[lightblue.obex] unable to remove temporary file %s: %s" %\
                        (localpath, str(e))
        

# receives file and saves to local path
def _recvfile(sock, localpath):

    # PyS60's bt_obex_receive() won't receive the file if given a file path
    # that already exists (it tells the client there's a conflict error). So
    # we need to handle this somehow, and preferably backup the original file
    # so that we can put it back if the recv fails.
    if os.path.isfile(localpath):
        # if given an existing path, rename existing file
        temppath = _tempfilename(localpath)
        os.rename(localpath, temppath)            
    else:
        temppath = None
        
    try:
        # receive a file (get internal _sock cos sock is our own SocketWrapper
        # object)
        _socket.bt_obex_receive(sock._sock, unicode(localpath))
    except _socket.error, e:
        try:
            if temppath is not None:
                # recv failed, put original file back
                os.rename(temppath, localpath)            
        finally:
            # if the renaming of the original file fails, this will still 
            # get raised
            raise OBEXError(str(e))
    else:
        # recv successful, remove the original file
        if temppath is not None:
            os.remove(temppath)    

# Must point to C:\ because can't write in start-up dir (on Z:?)
def _tempfilename(basename="C:\\lightblue_obex_received_file"):
    version = 1
    while os.path.isfile(basename):
        version += 1
        basename = basename[:-1] + str(version)
    return basename