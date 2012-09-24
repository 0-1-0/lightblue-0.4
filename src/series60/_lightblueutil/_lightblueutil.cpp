/*
 * Copyright (c) 2009 Bea Lam. All rights reserved.
 *
 * This file is part of LightBlue.
 *
 * LightBlue is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * LightBlue is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with LightBlue.  If not, see <http://www.gnu.org/licenses/>.
*/

// This file provides a Python extension for performing operations not offered 
// in the standard Python for Series 60 API, including:
//  - non-GUI discovery
//  - access to local device information


#include <e32base.h>
#include <e32std.h>
#include <es_sock.h>
#include <bt_sock.h>
#include <bttypes.h>

#if defined(__SYMBIAN_9__) || defined(__SYMBIAN_8__)
#   include <bt_subscribe.h>
#endif

#include <btextnotifiers.h>     // TBTDeviceResponseParamsPckg
#include <btdevice.h>

#include <symbian_python_ext_util.h>
#include <Python.h>


// structure that will hold the info about a remote device
struct TDeviceData
	{
	THostName iDeviceName;
	TBTDevAddr iDeviceAddr;
	TInt16 iServiceClass; 
	TInt8 iMajorClass;    
	TInt8 iMinorClass;    
	};
	
// typedef for a list of TDeviceData structures
typedef RPointerArray<TDeviceData> TDeviceDataList;


// Converts a TBTDevAddr to a bluetooth address string
// Arguments:
//  - addr - the address to convert
//  - aString - the TDes8 to hold the converted string result
static void DevAddressToString(TBTDevAddr& addr, TDes8& aString)
{
    // from PDIS miso library 
    // GetReadable() does not produce a "standard" result,
    // so have to construct a string manually.
    aString.Zero();
    _LIT8(KColon, ":");
    for (TInt i=0; i<6; i++) 
    {
        const TUint8& val = addr[i];
        aString.AppendNumFixedWidthUC(val, EHex, 2);
        if (i < 5)
            aString.Append(KColon);
    }
}

// Presents a Device Selection UI and returns the error code.
//
// Arguments:
//  - aResponse - the object to hold the details of the selected device.
//
// Returns an error code.
TInt SelectDeviceUI(TBTDeviceResponseParamsPckg& aResponse)
{
    TInt err;
    RNotifier notifier;
    
    err = notifier.Connect();
    if (err) return err;
    
    TBTDeviceSelectionParamsPckg selectionFilter;
    TRequestStatus status;
    notifier.StartNotifierAndGetResponse(
        status,
        KDeviceSelectionNotifierUid,
        selectionFilter,
        aResponse
    );
    
    User::WaitForRequest(status);
    
    err = status.Int();

    notifier.CancelNotifier(KDeviceSelectionNotifierUid);
    notifier.Close();
    return err;    
}

// Wraps SelectDeviceUI() to provide a Python method interface. 
// Takes no arguments.
//
// Returns None if user cancelled, otherwise returns a
// (name, address, (service,major,minor)) Python tuple.
static PyObject* LightBlue_SelectDevice(PyObject* self, PyObject* args) 
{
    if (!PyArg_ParseTuple(args, ""))
        return NULL;    
    
    TBTDeviceResponseParamsPckg response;
    TInt err = SelectDeviceUI(response);
     
    if (err) {
        if (err == KErrCancel) {
            // user cancelled
            Py_INCREF(Py_None);
            return Py_None;  
        } else {
            // some other error occured
            return SPyErr_SetFromSymbianOSErr(err);
        }
    }
    if (!(response().IsValidDeviceName())) {
        PyErr_SetString(PyExc_SymbianError, "discovery returned invalid data");
        return NULL;
    }
    
    // get device address
    TBuf8<6*2+5> addrString;
    TBTDevAddr addr = response().BDAddr();
    DevAddressToString(addr, addrString);
    
    // get device class details
    TBTDeviceClass deviceClass = response().DeviceClass();
    TUint16 service = deviceClass.MajorServiceClass();
    TUint8 major = deviceClass.MajorDeviceClass();
    TUint8 minor = deviceClass.MinorDeviceClass();
    
    return Py_BuildValue("s#u#(iii)",
        addrString.Ptr(), addrString.Length(),
        response().DeviceName().Ptr(), response().DeviceName().Length(),
        service,
        major,
        minor);
}

// Looks up the name of the device with the given address.
// Arguments:
//  - wantedAddr - address of the device to look up. Note that the address 
//    is expected without colons!! e.g. "000D9319C868"
//  - aDeviceName - the object to hold the name once it is found
//  - ignoreCache - if True, performs a remote name request even if the device
//    name is known in the cache from a previous request.
//
// Returns an error code.
static TInt LookupName(TBTDevAddr& wantedAddr, THostName* aDeviceName, 
    bool ignoreCache)
{
    TInt err = KErrNone;
    RSocketServ socketServer;
    RHostResolver hostResolver;
    TRequestStatus status; 
    TNameEntry nameEntry;        
    
    // make a TInquirySockAddr with the address of the device we want to look up
    TBTSockAddr sockAddr;
    sockAddr.SetBTAddr(wantedAddr);
    TInquirySockAddr addr = addr.Cast(sockAddr);
    
    // connect 
    err = socketServer.Connect();
    if (err) 
        return err;    
        
    // load protocol for discovery
	TProtocolDesc protocolDesc;
	err = socketServer.FindProtocol(_L("BTLinkManager"), protocolDesc);
	if (!err) { 
        
        // initialize host resolver
        err = hostResolver.Open(socketServer, protocolDesc.iAddrFamily, protocolDesc.iProtocol);
    	if (!err) {
        	            
        	// Request name lookup.
        	// We don't need to call SetIAC() if we're just doing name lookup.
        	// Don't put KHostResInquiry flag in SetAction(), because that
        	// will start a device inquiry, when we just want to find the one 
        	// name.
        	if (ignoreCache) {
        	    addr.SetAction(KHostResName|KHostResIgnoreCache);
	        } else {
    	        addr.SetAction(KHostResName);
	        }
        	
        	hostResolver.GetByAddress(addr, nameEntry, status);
        	
        	User::WaitForRequest(status);
        	
            if (status == KErrNone) {
                *aDeviceName = nameEntry().iName;
                err = KErrNone;
            } else {
                err = KErrGeneral;
            }
            
            hostResolver.Close();
        }
    }
    
    socketServer.Close();
    
    return err; 
}


// Wraps LookupName() to provide a Python method interface.
// Arguments:
//  - address of the device to look up. Note that the address 
//    is expected without colons!! e.g. "000D9319C868"
//  - True/False - if True, performs a remote name request even if the device
//    name is known in the cache from a previous request.
//
// Returns the name (a Python unicode string).
static PyObject* LightBlue_LookupName(PyObject* self, PyObject* args)
{   
    const char *addr;
    bool ignoreCache;
    if (!PyArg_ParseTuple(args, "sb", &addr, &ignoreCache))
        return NULL;

    THostName deviceName;
    TBTDevAddr wantedAddr;
    
    TBuf16<128> buf;
    buf.Copy(_L8(addr));
    wantedAddr.SetReadable(buf);
    
    TInt err = LookupName(wantedAddr, &deviceName, ignoreCache);
  
    if (err) 
        return SPyErr_SetFromSymbianOSErr(err);    
        
    return Py_BuildValue("u#", deviceName.Ptr(), deviceName.Length());
}


// Performs a device discovery. Blocks until all devices are found.
// Arguments:
//  - aDevDataList - details of each found device will be put in a TDeviceData
//    and added to this list
//  - lookupNames - whether to perform name lookups
//
// Returns an error code.
static TInt DiscoverDevices(TDeviceDataList* aDevDataList, bool lookupNames) 
{
    TInt err = KErrNone;
    RSocketServ socketServer;
    RHostResolver hostResolver;
    TInquirySockAddr addr;
    TRequestStatus status; 
    TNameEntry nameEntry;
        
    // connect 
    err = socketServer.Connect();
    if (err) 
        return err;
    
    // load protocol for discovery
	TProtocolDesc protocolDesc;
	err = socketServer.FindProtocol(_L("BTLinkManager"), protocolDesc);
	if (!err) { 
        
        // initialize host resolver
        err = hostResolver.Open(socketServer, protocolDesc.iAddrFamily, protocolDesc.iProtocol);
    	if (!err) {
        	
        	// start device discovery by invoking remote address lookup
        	addr.SetIAC(KGIAC);
        	if (lookupNames) {
        	    addr.SetAction(KHostResInquiry|KHostResName|KHostResIgnoreCache);
    	    } else {
        	    addr.SetAction(KHostResInquiry|KHostResIgnoreCache);
    	    }     
                    	
        	hostResolver.GetByAddress(addr, nameEntry, status);        	        	

            while( User::WaitForRequest( status ), 
                    (status == KErrNone || status == KRequestPending) )
            {                
        		// store new device data entry
        		TDeviceData *devData = new (ELeave) TDeviceData();
        		if (lookupNames) 
        		    devData->iDeviceName = nameEntry().iName;
        		    
        		devData->iDeviceAddr = 
        			static_cast<TBTSockAddr>(nameEntry().iAddr).BTAddr();
        			
                TInquirySockAddr &isa = TInquirySockAddr::Cast(nameEntry().iAddr);	
        		devData->iServiceClass = (TInt16)isa.MajorServiceClass();		
                devData->iMajorClass = (TInt8)isa.MajorClassOfDevice();
                devData->iMinorClass = (TInt8)isa.MinorClassOfDevice();
        		
        		// add device data entry to list
        		aDevDataList->Append(devData);        
        		
                // get next discovered device
                hostResolver.Next( nameEntry, status );
            }	
            
            hostResolver.Close();
        }
    }
    
    socketServer.Close();
    
    return err;
}

// Wraps DiscoverDevices() to provide a Python method interface.
// Arguments:
//  - boolean value of whether to perform name lookup 
//
// Returns list of (address, name, (service,major,minor)) tuples detailing the
// found devices.
static PyObject* LightBlue_DiscoverDevices(PyObject* self, PyObject* args)
{
    bool lookupNames;
    
    if (!PyArg_ParseTuple(args, "b", &lookupNames))
        return NULL;

    // run the discovery
    TDeviceDataList devDataList;
    TInt err = DiscoverDevices(&devDataList, lookupNames);
    if (err) 
        return SPyErr_SetFromSymbianOSErr(err);
    
    // put the results into a python list
    TInt i;
    TDeviceData *devData;
    TBuf8<6*2+5> addrString;
    
    PyObject *addrList = PyList_New(0);    // python list to hold results
    for( i=0; i<devDataList.Count(); i++ )
    {        
        devData = devDataList[i];
        
        // convert address to string
        DevAddressToString(devData->iDeviceAddr, addrString);
        
        PyObject *devValues;
        
        if (lookupNames) {
            THostName name = devData->iDeviceName;
            
            devValues = Py_BuildValue("(s#u#(iii))", 
                addrString.Ptr(), addrString.Length(),      // s# - address
                name.Ptr(), name.Length(),                  // u# - name
                devData->iServiceClass,                     // i  - service class
                devData->iMajorClass,                       // i  - major class
                devData->iMinorClass                        // i  - minor class
                );        
        } else {
            devValues = Py_BuildValue("(s#O(iii))", 
                addrString.Ptr(), addrString.Length(),      // s# - address
                Py_None,
                devData->iServiceClass,                     // i  - service class
                devData->iMajorClass,                       // i  - major class
                devData->iMinorClass                        // i  - minor class
                ); 
        }
        
        // add tuple to list
        PyList_Append(addrList, devValues);
    }    
    
    devDataList.ResetAndDestroy();
        
    return addrList;
}


// from PDIS miso library 
// Gets the local device address.
// Arguments:
//  - aAddress - object to hold the retrieved address.
//
// Returns an error code.
static TInt GetLocalAddress(TBTDevAddr& aAddress)
{
    TInt err = KErrNone;

#if defined(__SYMBIAN_9__)
    TPtr8 ptr(aAddress.Des());
    err = RProperty::Get(KPropertyUidBluetoothCategory,
                         KPropertyKeyBluetoothGetLocalDeviceAddress,
                         ptr);
#elif defined(__SYMBIAN_8__)
    TPtr8 ptr(aAddress.Des());
    err = RProperty::Get(KPropertyUidBluetoothCategory,
                         KPropertyKeyBluetoothLocalDeviceAddress,
                         ptr);
#else
    RSocketServ socketServ;
    err = socketServ.Connect();
    if (err)
        return err;
    
    // this solution comes from the "bthci" Series 60 example;
    // does not work on Symbian 8-up
    RSocket socket;
    err = socket.Open(socketServ, KBTAddrFamily, KSockSeqPacket, KL2CAP);
    if (!err) {
        TPckgBuf<TBTDevAddr> btDevAddrPckg;
        TRequestStatus status;
        socket.Ioctl(KHCILocalAddressIoctl, status, &btDevAddrPckg, KSolBtHCI);
        User::WaitForRequest(status);
        err = status.Int();
        if (!err) {
            TPtrC8 src(btDevAddrPckg().Des());
            TPtr8 dest(aAddress.Des());
            dest.Copy(src);
        }
    
        socket.Close();
    }
    
    socketServ.Close();
#endif

    return err;
}

// Wraps GetLocalAddress() to provide a Python method interface.
// Takes no arguments.
//
// Returns local device address as Python string.
static PyObject* LightBlue_GetLocalAddress(PyObject* self, PyObject* args) 
{
    TBTDevAddr addr;
    TBuf8<6*2+5> addrString;
    
    if (!PyArg_ParseTuple(args, ""))
        return NULL;    
    
    TInt err = GetLocalAddress(addr);
    if (err) 
        return SPyErr_SetFromSymbianOSErr(err);
        
    DevAddressToString(addr, addrString);
    
    return Py_BuildValue("s#", addrString.Ptr(), addrString.Length());
}


// from PDIS miso library 
// Gets the local device name.
// Arguments:
//  - aName - object to hold the retrieved name.
//
// Returns an error code.
static TInt GetLocalName(TDes& aName)
{
  TInt err = KErrNone;

  RSocketServ socketServ;
  err = socketServ.Connect();
  if (!err) {
    TProtocolName protocolName;
    // address and name queries are apparently supplied
    // by the BT stack's link manager
    _LIT(KBtLinkManager, "BTLinkManager");
    protocolName.Copy(KBtLinkManager);
    TProtocolDesc protocolDesc;
    err = socketServ.FindProtocol(protocolName, protocolDesc);
    if (!err) {
      RHostResolver hostResolver;
      err = hostResolver.Open(socketServ,
				protocolDesc.iAddrFamily,
				protocolDesc.iProtocol);
      if (!err) {
	err = hostResolver.GetHostName(aName);
	hostResolver.Close();
      }
    }
    socketServ.Close();
  }  

  return err;
}

// Wraps GetLocalName() to provide a Python method interface.
// Takes no arguments.
//
// Returns the local device name as a unicode python string.
static PyObject* LightBlue_GetLocalName(PyObject* self, PyObject* args) 
{
    TBTDeviceName deviceName;
    
    if (!PyArg_ParseTuple(args, ""))
        return NULL;
    
    TInt err = GetLocalName(deviceName);
    if (err) 
        return SPyErr_SetFromSymbianOSErr(err);
        
    return Py_BuildValue("u#", deviceName.Ptr(), deviceName.Length());
}

// Gets the local device class.
// Arguments:
//  - aDeviceData - object to hold the retrieved class data.
//
// Returns an error code.
//static TInt GetLocalDeviceClass(TDeviceData& aDeviceData)
static TInt GetLocalDeviceClass(TBTDeviceClass& aDeviceClass)
{
    TInt err = KErrNone;

#if defined(__SYMBIAN_9__)
    TInt cod;
    err = RProperty::Get(KPropertyUidBluetoothCategory,
                  KPropertyKeyBluetoothGetDeviceClass,
                  cod);
    if (err == KErrNone) {
        aDeviceClass = TBTDeviceClass(cod);
    }
#elif defined(__SYMBIAN_8__)
    TInt cod;
    err = RProperty::Get(KPropertyUidBluetoothCategory,
                  KPropertyKeyBluetoothDeviceClass,
                  cod);
    if (err == KErrNone) {
        aDeviceClass = TBTDeviceClass(cod);
    }    
#else

    RSocketServ socketServ;
    RSocket sock;
    
    err = socketServ.Connect();
    if (!err) {
        err = sock.Open(socketServ, KBTAddrFamily, KSockSeqPacket, KL2CAP);
        if (!err) {
            THCIDeviceClassBuf codBuf;
            TRequestStatus status;
            
            sock.Ioctl(KHCIReadDeviceClassIoctl, status, &codBuf, KSolBtHCI);
            User::WaitForRequest(status);
            
            if (status.Int() == KErrNone) {
                aDeviceClass = TBTDeviceClass(codBuf().iMajorServiceClass,
                                              codBuf().iMajorDeviceClass,
                                              codBuf().iMinorDeviceClass);
			}	  
            sock.Close();
        }
        socketServ.Close();
    }
#endif
    return err;
}

// Wraps GetLocalDeviceClass() to provide a Python method interface.
// Takes no arguments.
//
// Returns the local device class (an integer).
static PyObject* LightBlue_GetLocalDeviceClass(PyObject* self, PyObject* args) 
{
    //TDeviceData aDeviceData;
    TBTDeviceClass aDeviceClass;
    
    if (!PyArg_ParseTuple(args, ""))
        return NULL;
    
    TInt err = GetLocalDeviceClass(aDeviceClass);
    if (err) 
        return SPyErr_SetFromSymbianOSErr(err);
        
    return Py_BuildValue("i", aDeviceClass.DeviceClass());
}


// provide access to apn_resolver from PDIS library 
extern PyObject* apn_resolver_new(PyObject* /*self*/,
								  PyObject* /*args*/);
								  
extern TInt apn_resolver_ConstructType();								  


// define module methods
static const PyMethodDef _lightblueutil_methods[] = {
    {"lookupName", (PyCFunction)LightBlue_LookupName, METH_VARARGS},
    {"selectDevice", (PyCFunction)LightBlue_SelectDevice, METH_VARARGS},    
    {"discoverDevices", (PyCFunction)LightBlue_DiscoverDevices, METH_VARARGS},
    {"getLocalAddress", (PyCFunction)LightBlue_GetLocalAddress, METH_VARARGS},
    {"getLocalName", (PyCFunction)LightBlue_GetLocalName, METH_VARARGS},
    {"getLocalDeviceClass", (PyCFunction)LightBlue_GetLocalDeviceClass, METH_VARARGS},
    {"AoResolver", (PyCFunction)apn_resolver_new, METH_NOARGS},
    {0, 0}
};


/* initialise the module */
DL_EXPORT(void) init_lightblueutil(void)
{
    /* Create the module and add the functions */
    Py_InitModule("_lightblueutil", (PyMethodDef*)_lightblueutil_methods);
    
    if (apn_resolver_ConstructType() < 0) return;
}


/* This function is mandatory in Symbian DLL's. */
#ifndef EKA2
GLDEF_C TInt E32Dll(TDllReason)
{
  return KErrNone;
}
#endif /*EKA2*/
