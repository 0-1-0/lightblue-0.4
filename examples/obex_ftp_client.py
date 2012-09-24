'''
This example shows how to use the lightblue.obex.OBEXClient class to implement a
basic client for the File Transfer Profile, which is a profile implemented on
top of OBEX. This profile allows clients to:
    - send files
    - retrieve files
    - create and remove directories
    - change the current working directory

You can find a copy of the profile specification at
<http://www.bluetooth.com/Bluetooth/Technology/Building/Specifications/>.
'''

import sys
import os
import lightblue


# This is the special Target UUID (F9EC7BC4-953C-11D2-984E-525400DC9E09) for the
# File Transfer Profile, in byte form. You can get this in Python 2.5 using the
# uuid module:
#   >>> print uuid.UUID('{F9EC7BC4-953C-11D2-984E-525400DC9E09}').bytes
FTP_TARGET_UUID = '\xf9\xec{\xc4\x95<\x11\xd2\x98NRT\x00\xdc\x9e\t'


# A note about Connection ID headers:
# Notice that the FTPClient does not send the Connection ID in any of the 
# request headers, even though this is required by the File Transfer Profile 
# specs. This is because the OBEXClient class automatically sends the Connection 
# ID with each request if it received one from the server in the initial Connect
# response headers, so you do not have to add it yourself.


class FTPClient(object):

    def __init__(self, address, port):
        self.client = lightblue.obex.OBEXClient(address, port)

    def connect(self):
        response = self.client.connect({'target': FTP_TARGET_UUID})
        if response.code != lightblue.obex.OK:
            raise Exception('OBEX server refused Connect request (server \
                response was "%s")' % response.reason)

    def disconnect(self):
        print "Disconnecting..."
        response = self.client.disconnect()
        print 'Server response:', response.reason

    def ls(self):
        import StringIO
        dirlist = StringIO.StringIO()
        response = self.client.get({'type': 'x-obex/folder-listing'}, dirlist)
        print 'Server response:', response.reason
        if response.code == lightblue.obex.OK:
            files = self._parsefolderlisting(dirlist.getvalue())
            if len(files) == 0:
                print 'No files found'
            else:
                print 'Found files:'
                for f in files:
                    print '\t', f

    def cd(self, dirname):
        if dirname == os.sep:
            # change to root dir
            response = self.client.setpath({'name': ''})
        elif dirname == '..':
            # change to parent directory
            response = self.client.setpath({}, cdtoparent=True)
        else:
            # change to subdirectory
            response = self.client.setpath({'name': dirname})
        print 'Server response:', response.reason

    def put(self, filename):
        print 'Sending %s...' % filename
        try:
            f = file(filename, 'rb')
        except Exception, e:
            print "Cannot open file %s" % filename
            return
        response = self.client.put({'name': os.path.basename(filename)}, f)
        f.close()
        print 'Server response:', response.reason

    def get(self, filename):
        if os.path.isfile(filename):
            if raw_input("Overwrite local file %s?" % filename).lower() != "y":
                return
        print 'Retrieving %s...' % filename
        f = file(filename, 'wb')
        response = self.client.get({'name': filename}, f)
        f.close()
        print 'Server response:', response.reason

    def rm(self, filename):
        response = self.client.delete({'name': filename})
        print 'Server response:', response.reason

    def mkdir(self, dirname):
        response = self.client.setpath({'name': dirname}, createdirs=True)
        print 'Server response:', response.reason

    def rmdir(self, dirname):
        response = self.client.delete({'name': dirname})
        print 'Server response:', response.reason
        if response.code == lightblue.obex.PRECONDITION_FAILED:
            print 'Directory contents must be deleted first'

    def _parsefolderlisting(self, xmldata):
        """
        Returns a list of basic details for the files and folders contained in
        the given XML folder-listing data. (The complete folder-listing XML DTD
        is documented in the IrOBEX specification.)
        """
        if len(xmldata) == 0:
            print "Error parsing folder-listing XML: no xml data"
            return []
        entries = []
        import xml.dom.minidom
        import xml.parsers.expat
        try:
            dom = xml.dom.minidom.parseString(xmldata)
        except xml.parsers.expat.ExpatError, e:
            print "Error parsing folder-listing XML (%s): '%s'" % \
                (str(e), xmldata)
            return []
        parent = dom.getElementsByTagName('parent-folder')
        if len(parent) != 0:
            entries.append('..')
        folders = dom.getElementsByTagName('folder')
        for f in folders:
            entries.append('%s/\t%s' % (f.getAttribute('name'),
                                        f.getAttribute('size')))
        files = dom.getElementsByTagName('file')
        for f in files:
            entries.append('%s\t%s' % (f.getAttribute('name'),
                                       f.getAttribute('size')))
        return entries


def processcommands(ftpclient):
    while True:
        input = raw_input('\nEnter command: ')
        cmd = input.split(" ")[0].lower()
        if not cmd:
            continue
        if cmd == 'exit':
            break

        try:
            method = getattr(ftpclient, cmd)
        except AttributeError:
            print 'Unknown command "%s".' % cmd
            print main.__doc__
            continue

        if cmd == 'ls':
            if " " in input.strip():
                print "(Ignoring path, can only list contents of current dir)"
            method()
        else:
            name = input[len(cmd)+1:]     # file or directory name required
            method(name)


def main():
    """
    Usage: python obex_ftp_client.py [address channel]

    If the address and channel are not provided, the user will be prompted to
    choose a service.

    Once the client is connected, you can enter one of these commands:
        ls
        cd <directory>  (use '..' to change to parent, or '/' to change to root)
        put <file>
        get <file>
        rm <file>
        mkdir <directory>
        rmdir <directory>
        exit
        
    Some servers accept "/" path separators within the <file> or <filename> 
    arguments. Otherwise, you will have to just send either a single directory 
    or filename, without any paths.
    """
    if len(sys.argv) > 1:
        address = sys.argv[1]
        channel = int(sys.argv[2])
    else:
        # ask user to choose a service
        # a FTP service is usually called 'FTP', 'OBEX File Transfer', etc.
        address, channel, servicename = lightblue.selectservice()
    print 'Connecting to %s on channel %d...' % (address, channel)

    ftpclient = FTPClient(address, channel)
    ftpclient.connect()
    print 'Connected.'

    try:
        processcommands(ftpclient)
    finally:
        try:
            ftpclient.disconnect()
        except Exception, e:
            print "Error while disconnecting:", e
            pass


if __name__ == "__main__":
    main()
