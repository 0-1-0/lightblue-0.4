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

/*
 * Extension module to access BlueZ operations not provided in PyBluez. 
 */

#include "Python.h"

#include <bluetooth/bluetooth.h>
#include <bluetooth/hci.h>
#include <bluetooth/hci_lib.h>

/*
 * Returns name of local device
 */
static PyObject* lb_hci_read_local_name(PyObject *self, PyObject *args)
{
    int err = 0;
    int timeout = 0;
    int fd = 0;
    char name[249];

    if (!PyArg_ParseTuple(args, "ii", &fd, &timeout))
        return NULL;

    Py_BEGIN_ALLOW_THREADS
    err = hci_read_local_name(fd, sizeof(name)-1, name, timeout);
    Py_END_ALLOW_THREADS

    if (err != 0)
        return PyErr_SetFromErrno(PyExc_IOError);

    return PyString_FromString(name);    
}

/*
 * Returns address of local device
 */
static PyObject* lb_hci_read_bd_addr(PyObject *self, PyObject *args)
{
    int err = 0;
    int timeout = 0;
    int fd = 0;
    bdaddr_t ba;
    char addrstr[19] = {0};
    
    if (!PyArg_ParseTuple(args, "ii", &fd, &timeout))
        return NULL;

    Py_BEGIN_ALLOW_THREADS
    err = hci_read_bd_addr(fd, &ba, timeout);
    Py_END_ALLOW_THREADS

    if (err != 0)
        return PyErr_SetFromErrno(PyExc_IOError);

    ba2str(&ba, addrstr);
    return PyString_FromString(addrstr);
}

/*
 * Returns class of device of local device as a
 * (service, major, minor) tuple 
 */
static PyObject* lb_hci_read_class_of_dev(PyObject *self, PyObject *args)
{
    int err = 0;
    int timeout = 0;
    int fd = 0;
    uint8_t cod[3];
    
    if (!PyArg_ParseTuple(args, "ii", &fd, &timeout))
        return NULL;
        
    Py_BEGIN_ALLOW_THREADS
    err = hci_read_class_of_dev(fd, cod, timeout);
    Py_END_ALLOW_THREADS

    if (err != 0)
        return PyErr_SetFromErrno(PyExc_IOError);
    
    return Py_BuildValue("(B,B,B)", cod[2] << 3, cod[1] & 0x1f, cod[0] >> 2);
}

/* list of all functions in this module */
static PyMethodDef utilmethods[] = {
    {"hci_read_local_name", lb_hci_read_local_name, METH_VARARGS },
    {"hci_read_bd_addr", lb_hci_read_bd_addr, METH_VARARGS},
    {"hci_read_class_of_dev", lb_hci_read_class_of_dev, METH_VARARGS},
    { NULL, NULL }  /* sentinel */
};

/* module initialization functions */
void init_lightblueutil(void) {
    Py_InitModule("_lightblueutil", utilmethods);
}
