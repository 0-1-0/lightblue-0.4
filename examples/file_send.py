"""
Shows how to send a file over OBEX.
"""

import lightblue
import sys

if len(sys.argv) == 1:
    print "Usage: file_send.py [filename]"
    sys.exit(1)

sourcefile = sys.argv[1]

# Ask user to choose the device and service to send the file to.
address, serviceport, servicename = lightblue.selectservice()

# Send the file
lightblue.obex.sendfile(address, serviceport, sourcefile)

print "Done!"


# Note:
# Instead of calling selectservice(), you could do:
#
#     services = lightblue.findservices(addr=lightblue.selectdevice()[0],
#                                       servicetype=lightblue.OBEX)
#     address, serviceport, servicename = services[0]
#     lightblue.obex.sendfile(address, serviceport, sourcefile)
#
# This will ask the user to select a device, and then just send the file to the 
# first OBEX service found on that device. Then you don't have to ask the
# user to select a particular service.
#