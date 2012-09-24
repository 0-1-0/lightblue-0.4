"""
Shows how to run a RFCOMM server socket.
"""
import lightblue

# create and set up server socket
sock = lightblue.socket()
sock.bind(("", 0))    # bind to 0 to bind to a dynamically assigned channel
sock.listen(1)
lightblue.advertise("EchoService", sock, lightblue.RFCOMM)
print "Advertised and listening on channel %d..." % sock.getsockname()[1]

conn, addr = sock.accept()
print "Connected by", addr

data = conn.recv(1024)  
print "Echoing received data:", data
conn.send(data)

# sometimes the data isn't sent if the connection is closed immediately after
# the call to send(), so wait a second
import time
time.sleep(1)

conn.close()
sock.close()
