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

#include "lightblueobex_server.h"
#include "lightblueobex_main.h"
#include "structmember.h"

//#define LIGHTBLUEOBEX_SERVER_TEST


typedef struct {
    PyObject_HEAD
    obex_t *obex;
    int sendbufsize;

    PyObject *cb_error;
    PyObject *cb_newrequest;
    PyObject *cb_requestdone;

    int notifiednewrequest;
    int hasbodydata;
    PyObject *fileobj;
    PyObject *tempbuf;

#ifdef LIGHTBLUEOBEX_SERVER_TEST
    uint32_t puttotal;
#endif
} OBEXServer;


static void
obexserver_error(OBEXServer *self, PyObject *type, PyObject *value)
{
    PyObject *result;
    PyObject *tmpValue;

    DEBUG("%s()\n", __func__);

    if (self->cb_error == NULL) {   /* shouldn't happen, checked in init */
        DEBUG("\tcb_error() is NULL");
        return;
    }

    tmpValue = (value == NULL ? PyString_FromString("server error") : value);
    result = PyObject_CallFunctionObjArgs(self->cb_error,
            (type == NULL ? PyExc_IOError : type), value, NULL);

    if (result == NULL)
        DEBUG("\tfailed to call cb_error()\n");
    else
        Py_DECREF(result);

    /* don't want exceptions to be raised */
    /*PyErr_Clear();*/
    DEBUG("\terror was:\n");
    PyErr_Print();  /* clears the error indicator */
}

static void
obexserver_errorstr(OBEXServer *self, PyObject *exc, char *message)
{
    PyObject *msgobj;

    DEBUG("%s() - error with string msg\n", __func__);

    msgobj = PyString_FromString(message);
    obexserver_error(self, exc, msgobj);
    Py_XDECREF(msgobj);
}

static void
obexserver_errorfetch(OBEXServer *self)
{
    DEBUG("%s() - load from set error\n", __func__);

    if (!PyErr_Occurred()) {
        DEBUG("\tno error set, raise generic error\n");
        obexserver_error(self, NULL, NULL);
    } else {
        PyObject *pType;
        PyObject *pValue;
        PyObject *pTraceback;

        PyErr_Fetch(&pType, &pValue, &pTraceback);  /* value, TB can be null */
        obexserver_error(self, pType, pValue);
        Py_XDECREF(pType);
        Py_XDECREF(pValue);
        Py_XDECREF(pTraceback);
    }
}

/*
    Returns new reference that must be decref'd.
*/
static PyObject*
obexserver_notifynewrequest(OBEXServer *self, obex_object_t *obj, int obex_cmd, int *respcode)
{
    PyObject *resp;
    PyObject *respheaders;
    PyObject *tmpfileobj;

    PyObject *reqheaders;
    int nonhdrdata_len;
    PyObject *nonhdrdata_obj;
    uint8_t *nonhdrdata;

    DEBUG("%s() cmd=%d\n", __func__, obex_cmd);

    if (self->notifiednewrequest) {
        DEBUG("\tAlready called cb_newrequest");
        return NULL;
    }

    if (self->cb_newrequest == NULL) {  /* shouldn't happen */
        obexserver_errorstr(self, PyExc_IOError, "cb_newrequest is NULL");
        return NULL;
    }

    reqheaders = lightblueobex_readheaders(self->obex, obj);
    if (reqheaders == NULL) {
        obexserver_errorstr(self, PyExc_IOError,
                "error reading request headers");
        return NULL;
    }

    nonhdrdata_len = OBEX_ObjectGetNonHdrData(obj, &nonhdrdata);
    if (nonhdrdata_len < 0) {
        obexserver_errorstr(self, PyExc_IOError,
                "error reading non-header data");
        return NULL;
    }

    nonhdrdata_obj = PyBuffer_FromMemory(nonhdrdata,
            (Py_ssize_t)nonhdrdata_len);
    if (nonhdrdata_obj == NULL) {
        obexserver_errorstr(self, PyExc_IOError,
                "error reading non-header buffer");
        return NULL;
    }

    resp = PyObject_CallFunction(self->cb_newrequest, "iOOO",
            obex_cmd, reqheaders, nonhdrdata_obj,
            (self->hasbodydata ? Py_True : Py_False));
    Py_DECREF(nonhdrdata_obj);
    self->notifiednewrequest = 1;

    if (resp == NULL) {
        DEBUG("\terror calling cb_newrequest\n");
        obexserver_errorfetch(self);
        return NULL;
    }

    if ( !PyTuple_Check(resp) || PyTuple_Size(resp) < 3 ||
            !PyInt_Check(PyTuple_GetItem(resp, 0)) ||
            !PyDict_Check(PyTuple_GetItem(resp, 1)) ) {
        obexserver_errorstr(self, PyExc_TypeError,
                "callback must return (int, dict, fileobj | None) tuple");
        return NULL;
    }

    tmpfileobj = PyTuple_GetItem(resp, 2);

    if (obex_cmd == OBEX_CMD_PUT && self->hasbodydata &&
            !PyObject_HasAttrString(tmpfileobj, "write")) {
        obexserver_errorstr(self, PyExc_ValueError,
          "specified file object does not have 'write' method for Put request");
        return NULL;
    }

    if (obex_cmd == OBEX_CMD_GET &&
            !PyObject_HasAttrString(tmpfileobj, "read")) {
        obexserver_errorstr(self, PyExc_ValueError,
           "specified file object does not have 'read' method for Get request");
        return NULL;
    }

    *respcode = PyInt_AsLong(PyTuple_GetItem(resp, 0));
    if (PyErr_Occurred()) {
        PyErr_Clear();
        obexserver_errorstr(self, PyExc_IOError,
                "error reading returned response code");
        return NULL;
    }

    Py_XDECREF(self->fileobj);
    Py_INCREF(tmpfileobj);
    self->fileobj = tmpfileobj;

    respheaders = PyTuple_GetItem(resp, 1);
    Py_INCREF(respheaders);
    return respheaders;
}

static int
obexserver_setresponse(OBEXServer *self, obex_object_t *obj, int responsecode, PyObject *responseheaders)
{
    DEBUG("%s()\n", __func__);

    if (responseheaders != NULL) {
        if (lightblueobex_addheaders(self->obex, responseheaders, obj) < 0) {
            obexserver_errorstr(self, PyExc_IOError,
                    "error setting response headers");
            OBEX_ObjectSetRsp(obj, OBEX_RSP_INTERNAL_SERVER_ERROR,
                    OBEX_RSP_INTERNAL_SERVER_ERROR);
            return -1;
        }
    }

    if (responsecode == OBEX_RSP_SUCCESS || responsecode == OBEX_RSP_CONTINUE) {
        DEBUG("\tAccepting request...\n");
        OBEX_ObjectSetRsp(obj, OBEX_RSP_CONTINUE, OBEX_RSP_SUCCESS);
    } else {
        DEBUG("\tRefusing request (%s)...\n",
                OBEX_ResponseToString(responsecode));
        OBEX_ObjectSetRsp(obj, responsecode, responsecode);
    }

    return 1;
}

static void
obexserver_receivedrequest(OBEXServer *self, obex_object_t *obj, int obex_cmd)
{
    int respcode;
    PyObject *respheaders;

    DEBUG("%s()\n", __func__);

    // Put requests are taken care of in obexserver_streamavailable()
    if (obex_cmd == OBEX_CMD_PUT && self->hasbodydata) {
#ifdef LIGHTBLUEOBEX_SERVER_TEST
        obex_headerdata_t hv;
        hv.bq4 = self->puttotal;
        OBEX_ObjectAddHeader(self->obex, obj, OBEX_HDR_LENGTH, hv, 4, 0);
#endif
        return;
    }

    respheaders = obexserver_notifynewrequest(self, obj, obex_cmd, &respcode);
    if (respheaders == NULL) {
        obexserver_setresponse(self, obj, OBEX_RSP_INTERNAL_SERVER_ERROR, NULL);
    } else {
        int result;
        result = obexserver_setresponse(self, obj, respcode, respheaders);

        /* for Get requests, indicate we want OBEX_EV_STREAMEMPTY events */
        if (result >= 0 && obex_cmd == OBEX_CMD_GET &&
              (respcode == OBEX_RSP_CONTINUE || respcode == OBEX_RSP_SUCCESS)) {
            obex_headerdata_t hv;
            hv.bs = NULL;
            OBEX_ObjectAddHeader(self->obex, obj, OBEX_HDR_BODY, hv, 0,
                    OBEX_FL_STREAM_START);
        }
    }
    Py_XDECREF(respheaders);
}

static void
obexserver_streamavailable(OBEXServer *self, obex_object_t *obj)
{
    DEBUG("%s()\n", __func__);

    /* if got OBEX_EV_STREAMAVAIL, it means the request contains body data
       (and therefore is not a Put-Delete) */
    self->hasbodydata = 1;

    if (!self->notifiednewrequest) {
        PyObject *respheaders;
        int respcode;
        respheaders = obexserver_notifynewrequest(self, obj, OBEX_CMD_PUT,
                &respcode);
        if (respheaders == NULL) {
            obexserver_setresponse(self, obj, OBEX_RSP_INTERNAL_SERVER_ERROR,
                    NULL);
            return;
        }
        obexserver_setresponse(self, obj, respcode, respheaders);
        Py_DECREF(respheaders);
        if (respcode != OBEX_RSP_CONTINUE && respcode != OBEX_RSP_SUCCESS)
            return;
    }

    if (self->fileobj == NULL) {
        obexserver_errorstr(self, PyExc_IOError, "file object is null");
        return;
    }

    int result;
    result = lightblueobex_streamtofile(self->obex, obj, self->fileobj);
    if (result < 0) {
        obexserver_errorstr(self, PyExc_IOError, "error reading body data or writing to file object");
        OBEX_ObjectSetRsp(obj, OBEX_RSP_INTERNAL_SERVER_ERROR,
                OBEX_RSP_INTERNAL_SERVER_ERROR);
    }

#ifdef LIGHTBLUEOBEX_SERVER_TEST
    if (result > 0)
        self->puttotal += result;
#endif
}

static void
obexserver_streamempty(OBEXServer *self, obex_object_t *obj)
{
    DEBUG("%s()\n", __func__);

    Py_XDECREF(self->tempbuf);
    self->tempbuf = lightblueobex_filetostream(self->obex, obj, self->fileobj,
            self->sendbufsize);
    if (self->tempbuf == NULL) {
        obexserver_errorstr(self, PyExc_IOError, "error reading file object");
        OBEX_ObjectSetRsp(obj, OBEX_RSP_INTERNAL_SERVER_ERROR,
                OBEX_RSP_INTERNAL_SERVER_ERROR);
    }
}

static void
obexserver_requestdone(OBEXServer *self, obex_object_t *obj, int obex_cmd)
{
    PyObject *result;

    if (self->cb_requestdone == NULL) {  /* shouldn't happen */
        obexserver_errorstr(self, PyExc_IOError, "cb_requestdone is NULL");
        return;
    }

    result = PyObject_CallFunction(self->cb_requestdone, "i", obex_cmd);
    if (result == NULL) {
        obexserver_errorfetch(self);
    } else {
        Py_DECREF(result);
    }

    /* clean up */
    Py_XDECREF(self->tempbuf);
    self->tempbuf = NULL;
    Py_XDECREF(self->fileobj);
    self->fileobj = NULL;
}

static void
obexserver_incomingrequest(OBEXServer *self, obex_object_t *obj, int obex_cmd)
{
    self->notifiednewrequest = 0;
    self->hasbodydata = 0;
    Py_XDECREF(self->tempbuf);
    Py_XDECREF(self->fileobj);

    // signal we want to stream body data
    if (obex_cmd == OBEX_CMD_PUT) {
        if (OBEX_ObjectReadStream(self->obex, obj, NULL) < 0) {
            DEBUG("\tUnable to stream body data\n");
            OBEX_ObjectSetRsp(obj, OBEX_RSP_INTERNAL_SERVER_ERROR,
                    OBEX_RSP_INTERNAL_SERVER_ERROR);
            return;
        }
    }

    OBEX_ObjectSetRsp(obj, OBEX_RSP_CONTINUE, OBEX_RSP_SUCCESS);

#ifdef LIGHTBLUEOBEX_SERVER_TEST
    self->puttotal = 0;
#endif
}

/* can this be static? */
void
obexserver_event(obex_t *handle, obex_object_t *obj, int mode, int event, int obex_cmd, int obex_rsp)
{
    DEBUG("%s()\n", __func__);
    DEBUG("\tEvent: %d Command: %d\n", event, obex_cmd);

    OBEXServer *self = (OBEXServer *)OBEX_GetUserData(handle);
    switch (event) {
        case OBEX_EV_LINKERR:
        case OBEX_EV_PARSEERR:
            DEBUG("\tOBEX_EV_LINKERR or OBEX_EV_PARSEERR\n");
            obexserver_errorstr(self, PyExc_IOError, (event == OBEX_EV_LINKERR ?
                    "connection error" : "parse error"));
            break;
        case OBEX_EV_REQHINT:
            DEBUG("\tOBEX_EV_REQHINT\n");
            obexserver_incomingrequest(self, obj, obex_cmd);
            break;
        case OBEX_EV_REQ:
            DEBUG("\tOBEX_EV_REQ\n");
            obexserver_receivedrequest(self, obj, obex_cmd);
            break;
        case OBEX_EV_STREAMAVAIL:
            DEBUG("\tOBEX_EV_STREAMAVAIL\n");
            obexserver_streamavailable(self, obj);
            break;
        case OBEX_EV_STREAMEMPTY:
            DEBUG("\tOBEX_EV_STREAMEMPTY\n");
            obexserver_streamempty(self, obj);
            break;
        case OBEX_EV_REQDONE:
            DEBUG("\tOBEX_EV_REQDONE\n");
            obexserver_requestdone(self, obj, obex_cmd);
            break;
        default:
            DEBUG("\tNot handling event\n");
            break;
    }
}

static PyObject *
OBEXServer_process(OBEXServer *self, PyObject *args)
{
    int timeout;
    int result;

    DEBUG("%s()\n", __func__);

    if (!PyArg_ParseTuple(args, "i", &timeout))
        return NULL;

    result = OBEX_HandleInput(self->obex, timeout);
    return PyInt_FromLong(result);
}
PyDoc_STRVAR(OBEXServer_process__doc__,
"process(timeout) -> result\n\n\
Processes and reads incoming data with the given timeout (in seconds). \
Blocks if no data is available. Returns -1 on error and 0 on timeout.");


static PyObject *
OBEXServer_new(PyTypeObject *type, PyObject *args, PyObject *kwds)
{
    OBEXServer *self;
    self = (OBEXServer *)type->tp_alloc(type, 0);
    if (self != NULL) {
        self->obex = NULL;
        self->sendbufsize = 1024;
        self->cb_error = NULL;
        self->cb_newrequest = NULL;
        self->cb_requestdone = NULL;
        self->notifiednewrequest = 0;
        self->hasbodydata = 0;
        self->fileobj = NULL;
        self->tempbuf = NULL;
    }
    return (PyObject *)self;
}


static int
OBEXServer_init(OBEXServer *self, PyObject *args)
{
    int fd;
    PyObject *cb_error;
    PyObject *cb_newrequest;
    PyObject *cb_requestdone;
    int mtu = 1024;     /* todo make this a keyword arg */

    if (!PyArg_ParseTuple(args, "iOOO", &fd, &cb_error, &cb_newrequest,
            &cb_requestdone)) {
        return -1;
    }

    if (!PyCallable_Check(cb_error) || !PyCallable_Check(cb_newrequest) ||
            !PyCallable_Check(cb_requestdone)) {
        PyErr_SetString(PyExc_TypeError, "given callback is not callable");
        return -1;
    }

    if (self->cb_error == NULL) {
        Py_INCREF(cb_error);
        self->cb_error = cb_error;
    }

    if (self->cb_newrequest == NULL) {
        Py_INCREF(cb_newrequest);
        self->cb_newrequest = cb_newrequest;
    }

    if (self->cb_requestdone == NULL) {
        Py_INCREF(cb_requestdone);
        self->cb_requestdone = cb_requestdone;
    }

    if (self->obex == NULL) {
        self->obex = OBEX_Init(OBEX_TRANS_FD, obexserver_event, 0);
        if (self->obex == NULL) {
            PyErr_SetString(PyExc_IOError, "error creating OBEX object");
            return -1;
        }

        if (FdOBEX_TransportSetup(self->obex, fd, fd, mtu) < 0) {
            PyErr_SetString(PyExc_IOError, "error initialising transport");
            return -1;
        }
    }

    OBEX_SetUserData(self->obex, self);
    OBEX_SetTransportMTU(self->obex, OBEX_MAXIMUM_MTU, OBEX_MAXIMUM_MTU);
    return 0;
}

static void
OBEXServer_dealloc(OBEXServer *self)
{
    if (self->obex)
        OBEX_Cleanup(self->obex);
    Py_XDECREF(self->fileobj);
    Py_XDECREF(self->tempbuf);
    self->ob_type->tp_free((PyObject *)self);
}

static PyMemberDef OBEXServer_members[] = {
    {NULL}  /* Sentinel */
};

static PyMethodDef OBEXServer_methods[] = {
    { "process", (PyCFunction)OBEXServer_process, METH_VARARGS,
      OBEXServer_process__doc__,
    },
    {NULL}  /* Sentinel */
};


static PyTypeObject OBEXServerType = {
    PyObject_HEAD_INIT(NULL)
    0,                          /*ob_size*/
    "obexserver.OBEXServer",    /*tp_name*/
    sizeof(OBEXServer),         /*tp_basicsize*/
    0,                          /*tp_itemsize*/
    (destructor)OBEXServer_dealloc, /*tp_dealloc*/
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
    "An OBEX server class.",    /* tp_doc */
    0,		               /* tp_traverse */
    0,		               /* tp_clear */
    0,		               /* tp_richcompare */
    0,		               /* tp_weaklistoffset */
    0,		               /* tp_iter */
    0,		               /* tp_iternext */
    OBEXServer_methods,        /* tp_methods */
    OBEXServer_members,        /* tp_members */
    0,                         /* tp_getset */
    0,                         /* tp_base */
    0,                         /* tp_dict */
    0,                         /* tp_descr_get */
    0,                         /* tp_descr_set */
    0,                         /* tp_dictoffset */
    (initproc)OBEXServer_init,      /* tp_init */
    0,                         /* tp_alloc */
    OBEXServer_new,                 /* tp_new */
};


PyTypeObject *lightblueobex_getservertype(void)
{
    return &OBEXServerType;
}

