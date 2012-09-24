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

// This is a modified version of apnresolver.cpp from the PDIS project source
// code. Original license is below.


// -*- symbian-c++ -*-

//
// apnresolver.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Implements non-interactive Bluetooth device discovery, by
// utilizing the functionality provided by the native RHostResolver class.
//

// Permission is hereby granted, free of charge, to any person
// obtaining a copy of this software and associated documentation files
// (the "Software"), to deal in the Software without restriction,
// including without limitation the rights to use, copy, modify, merge,
// publish, distribute, sublicense, and/or sell copies of the Software,
// and to permit persons to whom the Software is furnished to do so,
// subject to the following conditions:
//
// The above copyright notice and this permission notice shall be
// included in all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
// EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
// MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
// NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
// BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
// ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
// CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
// SOFTWARE.



#include <bt_sock.h>
#include <e32base.h>
#include <es_sock.h>
#include "localepocpyutils.h"
#include "panic.h"
#include "settings.h"

// --------------------------------------------------------------------
// CAoResolver...

class CAoResolver : public CActive
	{
public:
	static CAoResolver* NewL();
	~CAoResolver();
	//void Discover(PyObject* aCallback, PyObject* aParam);
	void Discover(PyObject* aCallback, PyObject* aParam, bool getNames);
	void Next();
private:
	CAoResolver();
	void ConstructL();
private: // CActive
	void RunL();
	void DoCancel();
private:
	RSocketServ iSocketServ;
	RHostResolver iHostResolver;

	TInquirySockAddr iInquirySockAddr;
	TNameEntry iNameEntry;

	void Free();
	PyObject* iCallback;
	PyObject* iParam;
	bool iLookupNames;  

	PyThreadState* iThreadState;

	CTC_DEF_HANDLE(ctc);
	};

void CAoResolver::Free()
	{
	if (iCallback)
		{
		Py_DECREF(iCallback);
		iCallback = NULL;
		}
	if (iParam)
		{
		Py_DECREF(iParam);
		iParam = NULL;
		}
	}

CAoResolver* CAoResolver::NewL()
	{
	CAoResolver* obj = new (ELeave) CAoResolver;
	CleanupStack::PushL(obj);
	obj->ConstructL();
	CleanupStack::Pop();
	return obj;
	}

CAoResolver::CAoResolver() : CActive(EPriorityStandard)
	{
	CActiveScheduler::Add(this);
	CTC_STORE_HANDLE(ctc);
	}

void CAoResolver::ConstructL()
	{
	User::LeaveIfError(iSocketServ.Connect());

	TProtocolName protocolName;
	_LIT(KBtLinkManager, "BTLinkManager");
	protocolName.Copy(KBtLinkManager);
	TProtocolDesc protocolDesc;
	User::LeaveIfError(iSocketServ.FindProtocol(protocolName, protocolDesc));
	User::LeaveIfError(iHostResolver.Open(iSocketServ,
										  protocolDesc.iAddrFamily,
										  protocolDesc.iProtocol));
	}

CAoResolver::~CAoResolver()
	{
	CTC_CHECK(ctc);

	Cancel();
	Free();

	if (iHostResolver.SubSessionHandle() != 0)
		{
		iHostResolver.Close();
		}

	if (iSocketServ.Handle() != 0)
		{
		iSocketServ.Close();
		}
	}

//void CAoResolver::Discover(PyObject* aCallback, PyObject* aParam)
void CAoResolver::Discover(PyObject* aCallback, PyObject* aParam, bool lookupNames)
	{
	if (IsActive())
		{
		AoSocketPanic(EPanicRequestAlreadyPending);
		}

	Free();
	AssertNonNull(aCallback);
	AssertNonNull(aParam);
	iCallback = aCallback;
	Py_INCREF(aCallback);
	iParam = aParam;
	Py_INCREF(aParam);

	iThreadState = PyThreadState_Get();

	iInquirySockAddr.SetIAC(KGIAC);
	//iInquirySockAddr.SetAction(KHostResInquiry|KHostResName);
	
	// MODIFIED: use lookupNames parameter
	iLookupNames = lookupNames;
	if (iLookupNames) 
	    iInquirySockAddr.SetAction(KHostResInquiry|KHostResName|KHostResIgnoreCache);
    else 
        iInquirySockAddr.SetAction(KHostResInquiry|KHostResIgnoreCache);
    

	iHostResolver.GetByAddress(iInquirySockAddr, iNameEntry, iStatus);
	SetActive();
	}

void CAoResolver::Next()
	{
	if (IsActive())
		{
		AoSocketPanic(EPanicRequestAlreadyPending);
		}
	if (!iCallback)
		{
		AoSocketPanic(EPanicNextBeforeFirst);
		}

	iHostResolver.Next(iNameEntry, iStatus);
	SetActive();
	}

void CAoResolver::RunL()
	{
	TInt error = iStatus.Int();

	AssertNonNull(iCallback);
	AssertNonNull(iParam);

	PyEval_RestoreThread(iThreadState);

	PyObject* arg;
	if (error == KErrNone)
		{
		TSockAddr& sockAddr = iNameEntry().iAddr;
		TBTDevAddr btDevAddr = static_cast<TBTSockAddr>(sockAddr).BTAddr();
		TBuf<32> addrBuf;
		btDevAddr.GetReadable(addrBuf);

		THostName& hostName = iNameEntry().iName;

		//arg = Py_BuildValue("(iu#u#O)", error,
	   	//					addrBuf.Ptr(),
		//					addrBuf.Length(),
		//					hostName.Ptr(),
		//					hostName.Length(),
		//					iParam);
											
		// MODIFIED - add a 3-item tuple containing device class info into 'arg'
        TInquirySockAddr &isa = TInquirySockAddr::Cast(iNameEntry().iAddr);	
		TInt16 serviceClass = (TInt16)isa.MajorServiceClass();		
        TInt8 majorClass = (TInt8)isa.MajorClassOfDevice();
        TInt8 minorClass = (TInt8)isa.MinorClassOfDevice();		
        
        if (iLookupNames)
            {
    		arg = Py_BuildValue("(iu#u#(iii)O)", error,
    							addrBuf.Ptr(),
    							addrBuf.Length(),
    							hostName.Ptr(),
    							hostName.Length(),
    							serviceClass,
    							majorClass,
    							minorClass,
    							iParam);			
		    }				
		else 
		    {    		    
            // pass None instead of the hostName
    		arg = Py_BuildValue("(iu#O(iii)O)", error,
    							addrBuf.Ptr(),
    							addrBuf.Length(),
    							Py_None,
    							serviceClass,
    							majorClass,
    							minorClass,
    							iParam);	    							
		    }
							
		}
	else
		{
		//arg = Py_BuildValue("(iOOO)", error, Py_None, Py_None, iParam);
		
		// MODIFIED - pass an extra Py_None, since there's now the extra
		// tuple containing device class info
		arg = Py_BuildValue("(iOOOO)", error, Py_None, Py_None, Py_None, iParam);
		}

	if (arg)
		{
		PyObject* result = PyObject_CallObject(iCallback, arg);
		Py_DECREF(arg);
		Py_XDECREF(result);
		if (!result)
			{
			// Callbacks are not supposed to throw exceptions.
			// Make sure that the error gets noticed.
			PyErr_Clear();
			AoSocketPanic(EPanicExceptionInCallback);
			}
		}
	else
		{
		// It is misleading for an exception stack trace
		// to pop up later in some other context.
		// Perhaps we shall simply accept that an out
		// of memory condition will cause all sorts of
		// weird problems that we cannot properly act on.
		// We will just put a stop to things right here.
		PyErr_Clear();
		AoSocketPanic(EPanicOutOfMemory);
		}

	PyEval_SaveThread();

	// the callback may have done anything, including
	// deleting the object whose method we are in,
	// so do not attempt to access any property anymore
	}

void CAoResolver::DoCancel()
	{
	// iHostResolver will have been initialized if we get this
	// far, so calling Cancel() on it is fine
	iHostResolver.Cancel();
	}

// --------------------------------------------------------------------
// object structure...

// we store the state we require in a Python object
typedef struct
	{
	PyObject_VAR_HEAD;
	CAoResolver* iResolver;
	} apn_resolver_object;

// --------------------------------------------------------------------
// instance methods...

static PyObject* apn_resolver_discover(apn_resolver_object* self,
									   PyObject* args)
	{
	PyObject* cb;
	PyObject* param;
	
	char getNames;
	
	//if (!PyArg_ParseTuple(args, "OO", &cb, &param))
	
	// MODIFIED: add a boolean argument
	if (!PyArg_ParseTuple(args, "OOb", &cb, &param, &getNames))
		{
		return NULL;
		}
	if (!PyCallable_Check(cb))
		{
		PyErr_SetString(PyExc_TypeError, "parameter must be callable");
		return NULL;
		}

	if (!self->iResolver)
		{
		AoSocketPanic(EPanicUseBeforeInit);
		}

	//self->iResolver->Discover(cb, param);
	self->iResolver->Discover(cb, param, getNames);

	RETURN_NO_VALUE;
	}

static PyObject* apn_resolver_next(apn_resolver_object* self,
								   PyObject* /*args*/)
	{
	if (!self->iResolver)
		{
		AoSocketPanic(EPanicUseBeforeInit);
		}

	self->iResolver->Next();

	RETURN_NO_VALUE;
	}

static PyObject* apn_resolver_cancel(apn_resolver_object* self,
									 PyObject* /*args*/)
	{
	if (self->iResolver)
		{
		self->iResolver->Cancel();
		}
	RETURN_NO_VALUE;
	}

/** Creates the Symbian object (the Python object has already
	been created). This must be done in the thread that will
	be using the object, as we want to register with the active
	scheduler of that thread.
*/
static PyObject* apn_resolver_open(apn_resolver_object* self,
								   PyObject* /*args*/)
	{
	AssertNull(self->iResolver);
	TRAPD(error, self->iResolver = CAoResolver::NewL());
	if (error)
		{
		return SPyErr_SetFromSymbianOSErr(error);
		}
	RETURN_NO_VALUE;
	}

/** Destroys the Symbian object, but not the Python object.
	This must be done in the thread that used the object,
	as we must deregister with the correct active scheduler.
*/
static PyObject* apn_resolver_close(apn_resolver_object* self,
									PyObject* /*args*/)
	{
	delete self->iResolver;
	self->iResolver = NULL;
	RETURN_NO_VALUE;
	}

const static PyMethodDef apn_resolver_methods[] =
	{
	{"open", (PyCFunction)apn_resolver_open, METH_NOARGS},
	{"discover", (PyCFunction)apn_resolver_discover, METH_VARARGS},
	{"next", (PyCFunction)apn_resolver_next, METH_NOARGS},
	{"cancel", (PyCFunction)apn_resolver_cancel, METH_NOARGS},
	{"close", (PyCFunction)apn_resolver_close, METH_NOARGS},
	{NULL, NULL} // sentinel
	};

static void apn_dealloc_resolver(apn_resolver_object *self)
	{
	delete self->iResolver;
	self->iResolver = NULL;
	PyObject_Del(self);
	}

static PyObject *apn_resolver_getattr(apn_resolver_object *self,
									  char *name)
	{
	return Py_FindMethod((PyMethodDef*)apn_resolver_methods,
						 (PyObject*)self, name);
	}

// --------------------------------------------------------------------
// type...

const PyTypeObject apn_resolver_typetmpl =
	{
	PyObject_HEAD_INIT(NULL)
	0,										   /*ob_size*/
	"aosocketnativenew.AoResolver",			  /*tp_name*/
	sizeof(apn_resolver_object),					  /*tp_basicsize*/
	0,										   /*tp_itemsize*/
	/* methods */
	(destructor)apn_dealloc_resolver,				  /*tp_dealloc*/
	0,										   /*tp_print*/
	(getattrfunc)apn_resolver_getattr,				  /*tp_getattr*/
	0,										   /*tp_setattr*/
	0,										   /*tp_compare*/
	0,										   /*tp_repr*/
	0,										   /*tp_as_number*/
	0,										   /*tp_as_sequence*/
	0,										   /*tp_as_mapping*/
	0										  /*tp_hash*/
	};

TInt apn_resolver_ConstructType()
	{
	return ConstructType(&apn_resolver_typetmpl, "AoResolver");
	}

// --------------------------------------------------------------------
// module methods...

#define AoResolverType ((PyTypeObject*)SPyGetGlobalString("AoResolver"))

// Returns NULL if cannot allocate.
// The reference count of any returned object will be 1.
// The created socket object will be initialized,
// but the socket will not be open.
static apn_resolver_object* NewResolverObject()
	{
	apn_resolver_object* newResolver =
		// sets refcount to 1 if successful,
		// so decrefing should delete
		PyObject_New(apn_resolver_object, AoResolverType);
	if (newResolver == NULL)
		{
		// raise an exception with the reason set by PyObject_New
		return NULL;
		}

	newResolver->iResolver = NULL;

	return newResolver;
	}

// allocates a new AoResolver object, or raises and exception
PyObject* apn_resolver_new(PyObject* /*self*/, PyObject* /*args*/)
	{
	return reinterpret_cast<PyObject*>(NewResolverObject());
	}
