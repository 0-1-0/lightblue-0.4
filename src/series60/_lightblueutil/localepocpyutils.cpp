// -*- symbian-c++ -*-

//
// localepocpyutils.cpp
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Utilities to assist in the creation of native Python extensions.
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

#include "localepocpyutils.h"

TInt ConstructType(const PyTypeObject* aTypeTemplate,
				   char* aClassName)
	{
	//// construct the type;
	//// sets object refcount to 1
	PyTypeObject* typeObj = PyObject_New(PyTypeObject, &PyType_Type);
	*typeObj = *aTypeTemplate; // fill in from a template
	typeObj->ob_type = &PyType_Type; // fill in the missing bit

	//// store a reference to the type, for our own use
	TInt error = SPyAddGlobalString(aClassName, (PyObject*)typeObj);
	if (error < 0)
		{
		// the error codes for the above are not documented,
		// but have seen the code, and believe return -1 on failure
		return error;
		}
	//// a "global" hash now has a reference, too
	Py_INCREF(typeObj);

	return KErrNone;
	}
