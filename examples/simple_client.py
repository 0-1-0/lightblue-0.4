"""
Shows how to send "Hello world" over a RFCOMM client socket.
"""
import lightblue

# ask user to choose the device to connect to
hostaddr = lightblue.selectdevice()[0]        

# find the EchoService advertised by the simple_server.py example
echoservice = lightblue.findservices(addr=hostaddr, name="EchoService")[0]
serviceport = echoservice[1]

s = lightblue.socket()
s.connect((hostaddr, serviceport))
s.send("Hello world!")
print "Sent data, waiting for echo..."
data = s.recv(1024)
print "Got data:", data
s.close()


# Note:
# Instead of calling selectdevice() and findservices(), you could do:
#       hostaddr, serviceport, servicename = lightblue.selectservice()
# to ask the user to choose a service (instead of just choosing the device).
