// -*- symbian-c++ -*-

//
// localepocpyutils.h
// 
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.  All rights reserved.
// 
// Authors: Tero Hasu <tero.hasu@hut.fi>
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

#ifndef __LOCALEPOCPYUTILS_H__
#define __LOCALEPOCPYUTILS_H__

// note that we get warnings, or even errors with some compilers
// (e.g. .NET 7.1) unless this is before the Python headers
#include <e32std.h>

#include <Python.h>
#include <symbian_python_ext_util.h>

#define RETURN_NO_VALUE \
Py_INCREF(Py_None); \
return Py_None;

#define RETURN_TRUE \
Py_INCREF(Py_True); \
return Py_True;

#define RETURN_FALSE \
Py_INCREF(Py_False); \
return Py_False;

#define WAIT_STAT(stat) \
	Py_BEGIN_ALLOW_THREADS;\
	User::WaitForRequest(stat);\
	Py_END_ALLOW_THREADS

/** Returns an error code.
 */
TInt ConstructType(const PyTypeObject* aTypeTemplate,
				   char* aClassName);

#endif //  __LOCALEPOCPYUTILS_H__
