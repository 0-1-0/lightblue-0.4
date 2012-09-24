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

#include "lightblueobex_client.h"
#include "lightblueobex_main.h"
#include "structmember.h"


typedef struct {
    PyObject_HEAD
    obex_t *obex;
    int busy;
    int timeout;
    int sendbufsize;

    int resp;
    PyObject *resp_headers;

    PyObject *error;
    PyObject *error_msg;

    PyObject *fileobj;
    PyObject *tempbuf;
} OBEXClient;


static void
obexclient_requestcleanup(OBEXClient *self)
{
    DEBUG("%s()\n", __func__);
    Py_XDECREF(self->fileobj);
    self->fileobj = NULL;
    Py_XDECREF(self->tempbuf);
    self->tempbuf = NULL;
}

static void
obexclient_seterror(OBEXClient *self, PyObject *exc, char *message)
{
    DEBUG("%s()\n", __func__);
    if (exc == NULL)
        DEBUG("\t(resetting error)\n");
    else
        DEBUG("\tError: %s\n", (message == NULL ? "(unknown)" : message));

    if (self->error != NULL) {
        DEBUG("\tIgnore new error, error already set!\n");
        return;
    }

    Py_XDECREF(self->error);
    Py_XINCREF(exc);
    self->error = exc;

    Py_XDECREF(self->error_msg);
    self->error_msg = ( message == NULL ? PyString_FromString("error") :
            PyString_FromString(message) );
}

static void
obexclient_feedstream(OBEXClient *self, obex_object_t *obj)
{
    /*
    self->fileobj shouldn't be NULL since startrequest() doesn't set
    OBEX_FL_STREAM_START (and so this shouldn't be called) if fileobj is null
    */
    Py_XDECREF(self->tempbuf);
    self->tempbuf = lightblueobex_filetostream(self->obex, obj, self->fileobj,
            self->sendbufsize);
    if (self->tempbuf == NULL) {
        obexclient_seterror(self, PyExc_IOError, "error reading file object");
    }
}

static void
obexclient_readstream(OBEXClient *self, obex_object_t *obj)
{
    int result;
    result = lightblueobex_streamtofile(self->obex, obj, self->fileobj);
    if (result < 0) {
        obexclient_seterror(self, PyExc_IOError,
                "error writing to file object");
    }
}

static void
obexclient_requestdone(OBEXClient *self, obex_object_t *obj, int obex_cmd, int obex_rsp)
{
    DEBUG("%s()\n", __func__);
    DEBUG("\tCommand: %d Response: 0x%02x\n", obex_cmd, obex_rsp);

    self->resp = obex_rsp;
    Py_XDECREF(self->resp_headers);
    self->resp_headers = lightblueobex_readheaders(self->obex, obj);
    if (self->resp_headers == NULL)
        PyErr_SetString(PyExc_IOError, "error reading response headers");

    obexclient_requestcleanup(self);
    self->busy = 0;
}


/* can this be static? */
void
obexclient_event(obex_t *handle, obex_object_t *obj, int mode, int event, int obex_cmd, int obex_rsp)
{
    DEBUG("%s()\n", __func__);
    DEBUG("\tEvent: %d Command: %d\n", event, obex_cmd);

    OBEXClient *self = (OBEXClient *)OBEX_GetUserData(handle);
    switch (event) {
        case OBEX_EV_LINKERR:
        case OBEX_EV_PARSEERR:
            obexclient_seterror(self, PyExc_IOError, (event == OBEX_EV_LINKERR ?
                    "connection error" : "parse error"));
            if (obj != NULL) {  /* check a request is in progress */
                obexclient_requestdone(self, obj, obex_cmd, obex_rsp);
            }
            break;
        case OBEX_EV_STREAMEMPTY:
            obexclient_feedstream(self, obj);
            break;
        case OBEX_EV_STREAMAVAIL:
            obexclient_readstream(self, obj);
            break;
        case OBEX_EV_REQDONE:
            obexclient_requestdone(self, obj, obex_cmd, obex_rsp);
            break;
        default:
            /* ignore abort for now - can't be done with synchronous requests */
            break;
    }
}


static int
obexclient_startrequest(OBEXClient *self, int cmd, PyObject *headers, PyObject *nonhdrdata)
{
    const uint8_t *nonhdrdata_raw;
    Py_ssize_t nonhdrdata_len;

    DEBUG("%s()\n", __func__);

    if (!PyDict_Check(headers)) {
        PyErr_Format(PyExc_TypeError, "headers must be dict, was %s",
                headers->ob_type->tp_name);
        return -1;
    }

    if (nonhdrdata != Py_None && !PyBuffer_Check(nonhdrdata)) {
        PyErr_Format(PyExc_TypeError,
                "nonhdrdata must be buffer or None, was %s",
                nonhdrdata->ob_type->tp_name);
        return -1;
    }

    if (self->busy) {
        PyErr_SetString(PyExc_IOError, "another request is in progress");
        return -1;
    }

    obex_object_t *obj = OBEX_ObjectNew(self->obex, cmd);
    if (obj == NULL) {
        PyErr_SetString(PyExc_IOError, "error starting new request");
        return -1;
    }

    if (lightblueobex_addheaders(self->obex, headers, obj) < 0) {
        OBEX_ObjectDelete(self->obex, obj);
        PyErr_SetString(PyExc_IOError, "error setting request headers");
        return -1;
    }

    if (nonhdrdata != Py_None) {
        if (PyObject_AsReadBuffer(nonhdrdata, (const void **)&nonhdrdata_raw,
                    &nonhdrdata_len) < 0) {
            /* PyObject_AsReadBuffer() sets exception if error */
            OBEX_ObjectDelete(self->obex, obj);
            return -1;
        }

        if (OBEX_ObjectSetNonHdrData(obj, nonhdrdata_raw, nonhdrdata_len) < 0) {
            OBEX_ObjectDelete(self->obex, obj);
            PyErr_SetString(PyExc_IOError,
                    "error setting request non-header data");
            return -1;
        }
    }

    if (self->fileobj != NULL) {
        if (cmd == OBEX_CMD_PUT) {
            /* don't stream data if there is no fileobj (i.e. a Put-Delete) */
            obex_headerdata_t hv;
            if (OBEX_ObjectAddHeader(self->obex, obj, OBEX_HDR_BODY, hv, 0,
                    OBEX_FL_STREAM_START) < 0) {
                PyErr_SetString(PyExc_IOError,
                        "error setting streaming mode for Put");
                return -1;
            }
        } else if (cmd == OBEX_CMD_GET) {
            if (OBEX_ObjectReadStream(self->obex, obj, NULL) < 0) {
                PyErr_SetString(PyExc_IOError,
                        "error setting streaming mode for Get");
                return -1;
            }
        }
    }

    /* reset data for the new request */
    self->busy = 1;
    obexclient_seterror(self, NULL, NULL);
    self->resp = 0x20;

    Py_XDECREF(self->resp_headers);
    self->resp_headers = NULL;

    if (OBEX_Request(self->obex, obj) < 0) {
        PyErr_SetString(PyExc_IOError, "error sending request");
        return -1;
    }

    return 1;
}


static PyObject *
OBEXClient_request(OBEXClient *self, PyObject *args)
{
    PyObject *tmp;
    int result;
    int cmd;
    PyObject *headers;
    PyObject *nonhdrdata;
    PyObject *fileobj = NULL;   /* optional */

    DEBUG("%s()\n", __func__);

    if (!PyArg_ParseTuple(args, "iO!O|O", &cmd, &PyDict_Type, &headers,
            &nonhdrdata, &fileobj)) {
        return NULL;
    }

    if (fileobj != NULL) {
        char *method;
        method = ( cmd == OBEX_CMD_PUT ? "read" : "write" );
        if (!PyObject_HasAttrString(fileobj, method)) {
            PyErr_Format(PyExc_AttributeError,
                            "file-like object must have %s() method", method);
            return NULL;
        }

        tmp = self->fileobj;
        Py_INCREF(fileobj);
        self->fileobj = fileobj;
        Py_XDECREF(tmp);
    }

    if (obexclient_startrequest(self, cmd, headers, nonhdrdata) < 0) {
        obexclient_requestcleanup(self);
        return NULL;
    }

    /* wait until request is complete */
    while (self->busy) {
        result = OBEX_HandleInput(self->obex, self->timeout);
        if (result < 0) {
            obexclient_requestcleanup(self);
            obexclient_seterror(self, PyExc_IOError, "error processing input");
            break;
        } else if (result == 0) {
            obexclient_requestcleanup(self);
            obexclient_seterror(self, PyExc_IOError,
                    "input processing timed out");
            break;
        }
    }

    /* request is now complete, obexclient_requestcleanup() will have been
       called */

    if (self->error) {
        PyErr_SetObject(self->error, self->error_msg);
        return NULL;
    }

    /* Don't throw exceptions for non-success responses.
       The caller can decide whether that should be done. */
    return Py_BuildValue("(iO)", self->resp, self->resp_headers);
}

PyDoc_STRVAR(OBEXClient_request__doc__,
"request(opcode, headers, nonheaderdata [, fileobj]) -> response\n\n\
Sends an OBEX request and returns the server response code. \
Provide a file-like object if performing a Put or Get request. \
The nonheaderdata is really only useful for specifying the flags for SetPath \
requests. For other requests, set this value to None.");


static PyObject *
OBEXClient_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    OBEXClient *self;
    self = (OBEXClient *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->obex = NULL;
        self->busy = 0;
        self->timeout = 10;     /* seconds */
        self->sendbufsize = 4096;

        self->resp = 0;
        self->resp_headers = NULL;

        self->error = NULL;
        self->error_msg = PyString_FromString("");
        if (self->error_msg == NULL) {
            Py_DECREF(self);
            return NULL;
        }

        self->fileobj = NULL;
        self->tempbuf = NULL;
    }
    return (PyObject *)self;
}


static int
OBEXClient_init(OBEXClient *self, PyObject *args, PyObject *kwds)
{
    int fd = -1;
    int writefd = -1;
    int mtu = 1024;
    unsigned int flags = 0;
    static char *kwlist[] = { "fd", "writefd", "mtu", "flags", NULL };

    if (!PyArg_ParseTupleAndKeywords(args, kwds, "i|iiI", kwlist,
            &fd, &writefd, &mtu, &flags)) {
        return -1;
    }

    if (self->obex == NULL) {
        self->obex = OBEX_Init(OBEX_TRANS_FD, obexclient_event, flags);
        if (self->obex == NULL) {
            PyErr_SetString(PyExc_IOError, "error creating OBEX object");
            return -1;
        }

        if (writefd == -1)
            writefd = fd;
        if (FdOBEX_TransportSetup(self->obex, fd, writefd, mtu) < 0) {
            PyErr_SetString(PyExc_IOError, "error initialising transport");
            return -1;
        }
    }

    OBEX_SetUserData(self->obex, self);
    OBEX_SetTransportMTU(self->obex, OBEX_MAXIMUM_MTU, OBEX_MAXIMUM_MTU);
    return 0;
}

static void
OBEXClient_dealloc(OBEXClient *self)
{
    DEBUG("%s()\n", __func__);

    if (self->obex)
        OBEX_Cleanup(self->obex);

    Py_XDECREF(self->error);
    Py_XDECREF(self->error_msg);
    Py_XDECREF(self->fileobj);
    Py_XDECREF(self->tempbuf);
    self->ob_type->tp_free((PyObject *)self);
}

static PyMemberDef OBEXClient_members[] = {
    {"timeout", T_INT, offsetof(OBEXClient, timeout), 0,
     "timeout for each request"},
    {"sendbufsize", T_INT, offsetof(OBEXClient, sendbufsize), 0,
     "size of each data chunk to read from the file object for a Put request"},
    {NULL}  /* Sentinel */
};

static PyMethodDef OBEXClient_methods[] = {
    { "request", (PyCFunction)OBEXClient_request, METH_VARARGS,
      OBEXClient_request__doc__,
    },
    {NULL}  /* Sentinel */
};


static PyTypeObject OBEXClientType = {
    PyObject_HEAD_INIT(NULL)
    0,                          /*ob_size*/
    "obexclient.OBEXClient",    /*tp_name*/
    sizeof(OBEXClient),         /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    (destructor)OBEXClient_dealloc, /*tp_dealloc*/
    0,                         /*tp_print*/
    0,                         /*tp_getattr*/
    0,                         /*tp_setattr*/
    0,                         /*tp_compare*/
    0,                         /*tp_repr*/
    0,                         /*tp_as_number*/
    0,                         /*tp_as_sequence*/
    0,                         /*tp_as_mapping*/
    0,                         /*tp_hash */
    0,                         /*tp_call*/
    0,                         /*tp_str*/
    0,                         /*tp_getattro*/
    0,                         /*tp_setattro*/
    0,                         /*tp_as_buffer*/
    Py_TPFLAGS_DEFAULT | Py_TPFLAGS_BASETYPE, /*tp_flags*/
    "An OBEX client class.",    /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    OBEXClient_methods,        /* tp_methods */
    OBEXClient_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)OBEXClient_init,      /* tp_init */
    0,                         /* tp_alloc */
    OBEXClient_new,                 /* tp_new */
};


PyTypeObject *lightblueobex_getclienttype(void)
{
    return &OBEXClientType;
}
