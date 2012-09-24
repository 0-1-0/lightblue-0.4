LightBlue

http://lightblue.sourceforge.net
Bea Lam <blammit@gmail.com>

LightBlue is a cross-platform Bluetooth API for Python which provides simple access to Bluetooth operations. It is available for Mac OS X, GNU/Linux and Nokia's Python for Series 60 platform for mobile phones.

LightBlue provides simple access to:    * Device and service discovery (with and without end-user GUIs)    * Standard socket interface for RFCOMM and L2CAP sockets (currently L2CAP client sockets only, and not on PyS60)    * Sending and receiving files over OBEX    * Advertising of RFCOMM and OBEX services    * Local device information

LightBlue is released under the GPL License.

See the home page at http://lightblue.sourceforge.net for more information.


Requirements
============

Mac OS X:
    Python 2.3 or later
    PyObjC (http://pyobjc.sourceforge.net)
    Xcode 2.1 or later to build LightAquaBlue framework (but you could build from a separate .xcode project for older versions)
    (Mac OS X 10.4 or later is required to do device discovery without a GUI)
    
GNU/Linux:
    Python 2.3 or later (with Tkinter if using selectdevice() or selectservice())
    PyBluez 0.9 or later (http://org.csail.mit.edu/pybluez)
    OpenOBEX 1.0.1 or later (http://openobex.sourceforge.net)
    
Python for Series 60:
    Python for Series 60 1.3.1 or later (http://sourceforge.net/projects/pys60)
    
    
Installation
============

Mac OS X and GNU/Linux:
    Just open up a shell/terminal and run the command:

        python setup.py install

Or you might need to run with root access, i.e.

	sudo python setup.py install

    On Mac OS X, the setup.py script also installs the LightAquaBlue framework into /Library/Frameworks.
    
Python for Series 60: 
    Download the appropriate SIS file for your phone from the LightBlue home page (http://lightblue.sourceforge.net). Send the file to your phone, and open and install. Or, use the Nokia PC Suite to install the SIS file.


Installation for Xcode 1.5 / Mac OS X 10.3
------------------------------------------

The LightAquaBlue framework for the Mac OS X installation is in a .xcodeproj package which can only be opened by Xcode 2.1 and later, and Xcode 2.1 does not run on Mac OS X 10.3. So to build LightBlue on Mac OS X 10.3, just create a .xcode package yourself:

- Open Xcode and choose File -> New Project. Choose "Cocoa Framework" (under the "Frameworks" drop-down list) and save the project as "LightAquaBlue". Save the project anywhere as long as it's not replacing the existing LightBlue src/mac/LightAquaBlue directory.
- Go to Project -> Add files... and add all the .h and .m files from LightBlue's src/mac/LightAquaBlue folder. Also add the OBEXFileTransferDictionary.plist, OBEXObjectPushDictionary.plist and SerialPortDictionary.plist files.
- Go to Project -> Add framework... and add IOBluetooth.framework (found at /System/Library/Frameworks/IOBluetooth.framework).
- Click on the "Targets" item in the left-hand column of the Xcode window. This should show all the .h and .m files you've added as well as a few other files. In the "Role" column, all the .h files currently have "project" roles. Click on each drop-down menu item to change all of them to "public".
- Now go to the Finder and locate the xcode project you've just created. Copy the LightAquaBlue.xcode file for the project and paste it in LightBlue's src/mac/LightAquaBlue directory.

Now go to LightBlue's root directory and run the command

	sudo python setup.py install

You should see the output of the build of the xcode project.

