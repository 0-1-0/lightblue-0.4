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

#ifndef LIGHTBLUEOBEX_MAIN_H
#define LIGHTBLUEOBEX_MAIN_H

#include "Python.h"
#include <openobex/obex.h>

/* compatibility with python versions before 2.5 (PEP 353) */
#if PY_VERSION_HEX < 0x02050000 && !defined(PY_SSIZE_T_MIN)
typedef int Py_ssize_t;
#define PY_SSIZE_T_MAX INT_MAX
#define PY_SSIZE_T_MIN INT_MIN
#endif


#ifndef LIGHTBLUE_DEBUG
#define LIGHTBLUE_DEBUG 0
#endif

#if LIGHTBLUE_DEBUG
#define DEBUG(format, args...) fprintf(stderr, format, ##args)
#else
#define DEBUG(format, args...)
#endif


PyObject *lightblueobex_readheaders(obex_t *obex, obex_object_t *obj);

int lightblueobex_addheaders(obex_t *obex, PyObject *headers, obex_object_t *obj);

PyObject *lightblueobex_filetostream(obex_t *obex, obex_object_t *obj, PyObject *fileobj, int bufsize);

int lightblueobex_streamtofile(obex_t *obex, obex_object_t *obj, PyObject *fileobj);

#endif
