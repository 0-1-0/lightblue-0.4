"""
Shows how to receive a file over OBEX.
"""

import lightblue

# bind the socket, and advertise an OBEX service
sock = lightblue.socket()
try:
    sock.bind(("", 0))    # bind to 0 to bind to a dynamically assigned channel
    lightblue.advertise("LightBlue example OBEX service", sock, lightblue.OBEX)
    
    # Receive a file and save it as MyFile.txt. 
    # This will wait and block until a file is received.
    print "Waiting to receive file on channel %d..." % sock.getsockname()[1]
    lightblue.obex.recvfile(sock, "MyFile.txt")
    
finally:
    sock.close()
    
print "Saved received file to MyFile.txt!"


# Please note:
#
# To use a file through this example, the other device must send the file to
# the correct channel. E.g. if this example prints "Waiting to receive file on 
# channel 5..." the remote device must send the file specifically to channel 5.
# 
# * But what if you can't specify a channel or service?
# 
#   If you can send a file to a specific channel - e.g. by using
#   lightblue.obex.sendfile(), as the send_file.py example does - then you 
#   should be fine.
# 
#   But, if you're just using the system's default OBEX file-sending tool on 
#   the other device (e.g. "Send file..." from the Bluetooth drop-down menu on 
#   Mac OS X, or "Send ... Via Bluetooth" on Series 60 phones), it may only 
#   allow you to choose a device to send the file to, without choosing a
#   specific channel or service on the device. In this case, the tool is 
#   probably just  choosing the first available OBEX service on the device.
# 
#   So if you switch off all other related services, this example's service 
#   should automatically receive all OBEX files. E.g. if you're running this 
#   example on Mac OS X, go to the System Preferences' Bluetooth panel: on 
#   Mac OS X 10.4, go to the "Sharing" tab, and uncheck the "On" checkboxes for 
#   the "Bluetooth File Transfer" and "Bluetooth File Exchange" services.
#   On Mac OS X 10.3, go to the "File Exchange" tab, and for "When receiving 
#   items", select "Refuse all", and uncheck "Allow other devices to browse 
#   files on this computer".
