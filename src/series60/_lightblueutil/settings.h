// -*- symbian-c++ -*-

//
// settings.h
//
// Copyright 2004 Helsinki Institute for Information Technology (HIIT)
// and the authors.	 All rights reserved.
//
// Authors: Tero Hasu <tero.hasu@hut.fi>
//
// Some compile-time settings for this library.
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

#ifndef __SETTINGS_H__
#define __SETTINGS_H__

#ifdef __WINS__
#define ON_WINS 1
#else
#define ON_WINS 0
#endif

#define SUPPORT_PEROON 0 // ON_WINS

#define NO_SESSION_HANDLE_ACCESS SUPPORT_PEROON

/* Handle() and SubSessionHandle() always appear to return 0
   with Peroon sockets. Do not know if it is safe to call
   Close() on a closed Peroon socket; it is not okay with
   Symbian sockets. But we shall keep track of what is open
   an what is not, using these macros.
*/
#if NO_SESSION_HANDLE_ACCESS
#define IS_SESSION_OPEN(x) (x##IsOpen)
#define IS_SUBSESSION_OPEN(x) (x##IsOpen)
#define DEF_SESSION_OPEN(x) TBool x##IsOpen
#define SET_SESSION_OPEN(x) x##IsOpen = ETrue
#define SET_SESSION_CLOSED(x) x##IsOpen = EFalse
#else
#define IS_SESSION_OPEN(x) ((x).Handle() != 0)
#define IS_SUBSESSION_OPEN(x) ((x).SubSessionHandle() != 0)
#define DEF_SESSION_OPEN(x)
#define SET_SESSION_OPEN(x)
#define SET_SESSION_CLOSED(x) Mem::FillZ(&x, sizeof(x))
#endif

#define CHECK_THREAD_CORRECT 1

#if CHECK_THREAD_CORRECT
#define CTC_DEF_HANDLE(x) TThreadId x##ThreadId
#define CTC_STORE_HANDLE(x) x##ThreadId = RThread().Id()
#define CTC_IS_SAME_HANDLE(x) (x##ThreadId == RThread().Id())
#define CTC_PANIC(x) AoSocketPanic(EPanicAccessWithWrongThread)
#define CTC_CHECK(x) if (!CTC_IS_SAME_HANDLE(x)) CTC_PANIC(x)
#else
#define CTC_DEF_HANDLE(x)
#define CTC_STORE_HANDLE(x)
#define CTC_IS_SAME_HANDLE(x) ETrue
#define CTC_PANIC(x)
#define CTC_CHECK(x)
#endif

#endif // __SETTINGS_H__
