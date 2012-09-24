# On Python for Series 60, use the SIS files instead.

from distutils.core import setup, Extension
import sys

LINUX = sys.platform.startswith("linux")
MAC = sys.platform.startswith("darwin")

def getpackagedir():
    if MAC:
        return "src/mac"
    elif LINUX:
        return "src/linux"
    else:
        raise Exception("Unsupported platform")

def getextensions():
    if LINUX:
        linux_ext = Extension("_lightblueutil",
            libraries=["bluetooth"], # C libraries
            sources=["src/linux/lightblue_util.c"]
            )
        linux_obex_ext = Extension("_lightblueobex",
            define_macros=[('LIGHTBLUE_DEBUG', '0')],	# set to '1' to print debug messges
            libraries=["bluetooth", "openobex"], # C libraries
            sources=["src/linux/lightblueobex_client.c",
                     "src/linux/lightblueobex_server.c",
                     "src/linux/lightblueobex_main.c"],
            )
        return [linux_ext, linux_obex_ext]
    return []

# install the main library
setup(name="lightblue",
    version="0.4",
    author="Bea Lam",
    author_email="blammit@gmail.com",
    url="http://lightblue.sourceforge.net",
    description="Cross-platform Python Bluetooth library for Mac OS X, GNU/Linux and Python for Series 60.",
    long_description="LightBlue is a cross-platform Python Bluetooth library for Mac OS X, GNU/Linux and Python for Series 60. It provides support for device and service discovery (with and without end-user GUIs), a standard socket interface for RFCOMM sockets, sending and receiving of files over OBEX, advertising of RFCOMM and OBEX services, and access to local device information.",
    license="GPL",
    packages=["lightblue"],
    package_dir={"lightblue":getpackagedir()},
    ext_modules=getextensions(),
    classifiers = [ "Development Status :: 3 - Alpha",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: GNU General Public License (GPL)",
        "License :: OSI Approved :: GNU General Public License v3",
        "Programming Language :: Python",
        "Topic :: Software Development :: Libraries",
        "Topic :: System :: Networking",
        "Topic :: Communications",
        "Operating System :: MacOS :: MacOS X",
        "Operating System :: POSIX :: Linux",
        "Operating System :: Other OS" ]
    )


# On Mac, install LightAquaBlue framework
# if you want to install the framework somewhere other than /Library/Frameworks
# make sure the path is also changed in LightAquaBlue.py (in src/mac)
if MAC:
    if "install" in sys.argv:
        import os
        os.chdir("src/mac/LightAquaBlue")
        os.system("xcodebuild install -arch '$(NATIVE_ARCH_ACTUAL)' -target LightAquaBlue -configuration Release DSTROOT=/ INSTALL_PATH=/Library/Frameworks DEPLOYMENT_LOCATION=YES")
