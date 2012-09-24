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

#include "lightblueobex_main.h"
#include "lightblueobex_client.h"
#include "lightblueobex_server.h"

#include <openobex/obex.h>
#include "Python.h"


#define OBEX_HI_MASK     0xc0
#define OBEX_UNICODE     0x00
#define OBEX_BYTE_STREAM 0x40
#define OBEX_BYTE        0x80
#define OBEX_INT         0xc0

/* OBEX unicode strings are UTF-16, big-endian. */
#define OBEX_BIG_ENDIAN 1       /* for encoding/decoding unicode strings */


PyObject *lightblueobex_readheaders(obex_t *obex, obex_object_t *obj)
{
    PyObject *headers;
    uint8_t hi;
    obex_headerdata_t hv;
    uint32_t hv_size;
    int r;
    PyObject *value = NULL;

    DEBUG("%s()\n", __func__);

    headers = PyDict_New();
    if (headers == NULL)
        return NULL;

    if (obex == NULL || obj == NULL || headers == NULL) {
        DEBUG("\treadheaders() got null argument\n");
        return NULL;
    }

    if (!PyDict_Check(headers)) {
        DEBUG("\treadheaders() arg must be dict\n");
        return NULL;
    }

    while (OBEX_ObjectGetNextHeader(obex, obj, &hi, &hv, &hv_size)) {
        DEBUG("\tread header: 0x%02x\n", hi);
        switch (hi & OBEX_HI_MASK) {
        case OBEX_UNICODE:
        {
            if (hv_size < 2) {
                value = PyUnicode_FromUnicode(NULL, 0);
            } else {
                /* hv_size-2 for 2-byte null terminator */
                int byteorder = OBEX_BIG_ENDIAN;
                value = PyUnicode_DecodeUTF16((const char*)hv.bs, hv_size-2,
                        NULL, &byteorder);
                if (value == NULL) {
                    DEBUG("\terror reading unicode header 0x%02x\n", hi);
                    if (PyErr_Occurred()) {
                        PyErr_Print();
                        PyErr_Clear();  /* let caller set exception */
                    }
                    return NULL;
                }
            }
            break;
        }
        case OBEX_BYTE_STREAM:
        {
            value = PyBuffer_New(hv_size);
            if (value != NULL) {
                void *buf;
                Py_ssize_t buflen;
                if (PyObject_AsWriteBuffer(value, &buf, &buflen) < 0) {
                    Py_DECREF(value);
                    DEBUG("\terror writing to buffer for header 0x%02x\n", hi);
                    return NULL;
                }
                memcpy(buf, hv.bs, buflen);
            }
            break;
        }
        case OBEX_BYTE:
        {
            value = PyInt_FromLong(hv.bq1);
            break;
        }
        case OBEX_INT:
        {
            value = PyLong_FromUnsignedLong(hv.bq4);
            break;
        }
        default:
            DEBUG("\tunknown header id encoding %d\n", (hi & OBEX_HI_MASK));
            return NULL;
        }

        if (value == NULL) {
            if (PyErr_Occurred() == NULL)
                DEBUG("\terror reading headers\n");
            return NULL;
        }
        r = PyDict_SetItem(headers, PyInt_FromLong((long)hi), value);
        Py_DECREF(value);
        if (r < 0) {
            DEBUG("\tPyDict_SetItem() error\n");
            if (PyErr_Occurred()) {
                PyErr_Print();
                PyErr_Clear();  /* let caller set exception */
            }
            return NULL;
        }
    }
    return headers;
}


static int lightblueobex_add4byteheader(obex_t *obex, obex_object_t *obj, uint8_t hi, PyObject *value)
{
    obex_headerdata_t hv;
    uint32_t intvalue;

    DEBUG("%s()\n", __func__);

    if (value == NULL)
        return -1;

    if (PyInt_Check(value)) {
        intvalue = PyInt_AsLong(value);
        if (PyErr_Occurred()) {
            DEBUG("\tcan't convert header 0x%02x int value to long", hi);
            PyErr_Clear();
            return -1;
        }
    } else if (PyLong_Check(value)) {
        intvalue = PyLong_AsUnsignedLong(value);
        if (PyErr_Occurred()) {
            DEBUG("\tcan't convert header 0x%02x long value to long", hi);
            PyErr_Clear();
            return -1;
        }
    } else {
        DEBUG("\theader value for id 0x%02x must be int or long, was %s\n",
                hi, value->ob_type->tp_name);
        return -1;
    }

    hv.bq4 = intvalue;
    return OBEX_ObjectAddHeader(obex, obj, hi, hv, 4, OBEX_FL_FIT_ONE_PACKET);
}

static int lightblueobex_addbytestreamheader(obex_t *obex, obex_object_t *obj, uint8_t hi, PyObject *bufObject)
{
    obex_headerdata_t hv;
    uint32_t hv_size;

    DEBUG("%s()\n", __func__);

    if (PyObject_AsReadBuffer(bufObject, (const void**)&hv.bs, (Py_ssize_t*)&hv_size) < 0) {
        DEBUG("\theader value for id 0x%02x must be readable buffer\n", hi);
        return -1;
    }
    return OBEX_ObjectAddHeader(obex, obj, hi, hv, hv_size, OBEX_FL_FIT_ONE_PACKET);
}

static int lightblueobex_addunicodeheader(obex_t *obex, obex_object_t *obj, uint8_t hi, PyObject *utf16string)
{
    obex_headerdata_t hv;
    Py_ssize_t len = PyUnicode_GET_SIZE(utf16string);
    uint8_t bytes[len+2];

    DEBUG("%s()\n", __func__);

    /* need to add 2-byte null terminator */
    memcpy(bytes, (const uint8_t*)PyString_AsString(utf16string), len);
    bytes[len] = 0x00;
    bytes[len+1] = 0x00;

    hv.bs = bytes;
    return OBEX_ObjectAddHeader(obex, obj, (uint8_t)hi, hv, len+2,
            OBEX_FL_FIT_ONE_PACKET);
}


int lightblueobex_addheaders(obex_t *obex, PyObject *headers, obex_object_t *obj)
{
    uint8_t hi;
    obex_headerdata_t hv;
    PyObject *key, *value;
    Py_ssize_t pos = 0;
    int r = -1;

    DEBUG("%s()\n", __func__);

    if (headers == NULL || !PyDict_Check(headers)) {
        DEBUG("\taddheaders() arg must be dict\n");
        return -1;
    }

    /* add connection-id first */
    key = PyInt_FromLong(OBEX_HDR_CONNECTION);
    if (key != NULL) {
        value = PyDict_GetItem(headers, key);   /* don't decref! */
        Py_DECREF(key);
        key = NULL;
        if (value != NULL) {
            DEBUG("\tadding connection-id\n");
            r = lightblueobex_add4byteheader(obex, obj, OBEX_HDR_CONNECTION,
                    value);
            if (r < 0) {
                DEBUG("\terror adding connection-id header\n");
                return -1;
            }
        }
    }

    /* add target header first (shouldn't have both conn-id and target) */
    key = PyInt_FromLong(OBEX_HDR_TARGET);
    if (key != NULL) {
        value = PyDict_GetItem(headers, key);   /* don't decref! */
        Py_DECREF(key);
        key = NULL;
        if (value != NULL) {
            DEBUG("\tadding target\n");
            r = lightblueobex_addbytestreamheader(obex, obj, OBEX_HDR_TARGET,
                    value);
            if (r < 0) {
                DEBUG("\terror adding target header\n");
                return -1;
            }
        }
    }

    while (PyDict_Next(headers, &pos, &key, &value)) {
        if (key == NULL || value == NULL) {
            DEBUG("\terror reading headers dict\n");
            return -1;
        }
        if (!PyInt_Check(key)) {
            DEBUG("\theader id must be int, was %s\n", key->ob_type->tp_name);
            return -1;
        }

        hi = (uint8_t)PyInt_AsUnsignedLongMask(key);
        if (hi == OBEX_HDR_CONNECTION || hi == OBEX_HDR_TARGET) {
            /* these are already added */
            continue;
        }

        DEBUG("\tadding header: 0x%02x\n", hi);

        switch (hi & OBEX_HI_MASK) {
        case OBEX_UNICODE:
        {
            PyObject *encoded = NULL;
            if (PyUnicode_Check(value)) {
                encoded = PyUnicode_EncodeUTF16(PyUnicode_AS_UNICODE(value),
                        PyUnicode_GET_SIZE(value),
                        NULL, OBEX_BIG_ENDIAN);
            } else {
                /* try converting to unicode */
                PyObject *tmp = NULL;
                tmp = PyUnicode_FromObject(value);
                if (tmp == NULL) {
                    if (PyErr_Occurred()) {
                        PyErr_Print();
                        PyErr_Clear();  /* let caller set exception */
                    }
                    DEBUG("\tfailed to convert header value for id 0x%02x to unicode\n", hi);
                    return -1;
                }
                encoded = PyUnicode_EncodeUTF16(PyUnicode_AS_UNICODE(tmp),
                        PyUnicode_GET_SIZE(tmp),
                        NULL, OBEX_BIG_ENDIAN);
                Py_DECREF(tmp);
            }
            if (encoded == NULL) {
                if (PyErr_Occurred()) {
                    PyErr_Print();
                    PyErr_Clear();  /* let caller set exception */
                }
                DEBUG("\tfailed to encode value for header 0x%02x to UTF-16 big endian\n", hi);
                return -1;
            }
            r = lightblueobex_addunicodeheader(obex, obj, hi, encoded);
            Py_DECREF(encoded);
            break;
        }
        case OBEX_BYTE_STREAM:
        {
            r = lightblueobex_addbytestreamheader(obex, obj, hi, value);
            break;
        }
        case OBEX_BYTE:
        {
            long intvalue;
            if (!PyInt_Check(value)) {
                DEBUG("\theader value for id 0x%02x must be int, was %s\n",
                    hi, value->ob_type->tp_name);
                return -1;
            }
            intvalue = PyInt_AsLong(value);
            if (PyErr_Occurred()) {
                DEBUG("\terror reading int value for 0x%02x\n", hi);
                PyErr_Clear();
                return -1;
            }
            hv.bq1 = (uint8_t)intvalue;
            r = OBEX_ObjectAddHeader(obex, obj, hi, hv, 1,
                    OBEX_FL_FIT_ONE_PACKET);
            break;
        }
        case OBEX_INT:
        {
            r = lightblueobex_add4byteheader(obex, obj, hi, value);
            break;
        }
        default:
            DEBUG("\tunknown header id encoding %d\n", (hi & OBEX_HI_MASK));
            return -1;
        }
        if (r < 0) {
            DEBUG("\terror adding header 0x%02x\n", hi);
            return -1;
        }
    }

    return 1;
}


PyObject *lightblueobex_filetostream(obex_t *obex, obex_object_t *obj, PyObject *fileobj, int bufsize)
{
    const void *data;
    Py_ssize_t datalen;        /* or unsigned int? */
    obex_headerdata_t hv;
    PyObject *buf;

    DEBUG("%s()\n", __func__);

    if (fileobj == NULL) {
        DEBUG("\tgiven file object is NULL\n");
        hv.bs = NULL;
        OBEX_ObjectAddHeader(obex, obj, OBEX_HDR_BODY, hv, 0,
                OBEX_FL_STREAM_DATAEND);
        return NULL;
    }

    buf = PyObject_CallMethod(fileobj, "read", "i", bufsize);
    if (buf == NULL) {
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyErr_Clear();  /* let caller set exception */
        }
        DEBUG("\terror calling file object read()\n");
    }

    if (buf != NULL && !PyObject_CheckReadBuffer(buf)) {
        DEBUG("\tfile object read() returned non-buffer object\n");
        Py_DECREF(buf);
        buf = NULL;
    }

    if (buf != NULL && PyObject_AsReadBuffer(buf, &data, &datalen) < 0) {
        DEBUG("\terror reading file object contents\n");
        Py_DECREF(buf);
        buf = NULL;
    }

    if (buf == NULL) {
        hv.bs = NULL;
        OBEX_ObjectAddHeader(obex, obj, OBEX_HDR_BODY, hv, 0,
                OBEX_FL_STREAM_DATAEND);
        return NULL;
    }

    hv.bs = (uint8_t*)data;
    if (OBEX_ObjectAddHeader(obex, obj, OBEX_HDR_BODY, hv, datalen,
           (datalen == 0 ? OBEX_FL_STREAM_DATAEND : OBEX_FL_STREAM_DATA)) < 0) {
        DEBUG("\terror adding body data\n");
        Py_DECREF(buf);
        buf = NULL;
    }

    return buf;
}


int lightblueobex_streamtofile(obex_t *obex, obex_object_t *obj, PyObject *fileobj)
{
    const uint8_t *buf;
    int buflen;

    DEBUG("%s()\n", __func__);

    if (fileobj == NULL)
        return -1;

    buflen = OBEX_ObjectReadStream(obex, obj, &buf);
    if (buflen == 0)
        return 0;

    if (buflen < 0) {
        DEBUG("\tunable to read body data from request\n");
        return -1;
    }

    DEBUG("\treading %d bytes\n", buflen);

    PyObject *pybuf = PyBuffer_FromMemory((void*)buf, buflen);
    if (pybuf == NULL) {
        DEBUG("\terror reading received body\n");
        return -1;
    }

    PyObject *result = PyObject_CallMethod(fileobj, "write", "O", pybuf);
    Py_DECREF(pybuf);

    if (result == NULL) {
        if (PyErr_Occurred()) {
            PyErr_Print();
            PyErr_Clear();  /* let caller set exception */
        }
        DEBUG("error calling write() on file object\n");
        return -1;
    }

    Py_DECREF(result);
    return buflen;
}


static PyMethodDef module_methods[] = {
    {NULL}  /* Sentinel */
};

#ifndef PyMODINIT_FUNC	/* declarations for DLL import/export */
#define PyMODINIT_FUNC void
#endif
PyMODINIT_FUNC
init_lightblueobex(void)
{
    PyObject *m;
    PyTypeObject *clientType;
    PyTypeObject *serverType;

    clientType = lightblueobex_getclienttype();
    serverType = lightblueobex_getservertype();

    if ( (PyType_Ready(clientType) < 0) ||
         (PyType_Ready(serverType) < 0) ) {
        return;
    }

    m = Py_InitModule3("_lightblueobex", module_methods,
                   "Module containing the OBEXClient and OBEXServer classes.");
    if (m == NULL)
      return;

    PyModule_AddIntConstant(m, "CONNECT", 0x00);
    PyModule_AddIntConstant(m, "DISCONNECT", 0x01);
    PyModule_AddIntConstant(m, "PUT", 0x02);
    PyModule_AddIntConstant(m, "GET", 0x03);
    PyModule_AddIntConstant(m, "SETPATH", 0x05);
    PyModule_AddIntConstant(m, "SESSION", 0x07);
    PyModule_AddIntConstant(m, "ABORT", 0x7f);


    /* or should these constants be in the linux obex module? */

    PyModule_AddIntConstant(m, "COUNT", 0xc0);
    PyModule_AddIntConstant(m, "NAME", 0x01);
    PyModule_AddIntConstant(m, "TYPE", 0x42);
    PyModule_AddIntConstant(m, "LENGTH", 0xc3);
    PyModule_AddIntConstant(m, "TIME", 0x44);
    PyModule_AddIntConstant(m, "DESCRIPTION", 0x05);
    PyModule_AddIntConstant(m, "TARGET", 0x46);
    PyModule_AddIntConstant(m, "HTTP", 0x47);
    PyModule_AddIntConstant(m, "BODY", 0x48);
    PyModule_AddIntConstant(m, "END_OF_BODY", 0x49);
    PyModule_AddIntConstant(m, "WHO", 0x4a);
    PyModule_AddIntConstant(m, "CONNECTION_ID", 0xcb);
    PyModule_AddIntConstant(m, "APP_PARAMETERS", 0x4c);
    PyModule_AddIntConstant(m, "AUTH_CHALLENGE", 0x4d);
    PyModule_AddIntConstant(m, "AUTH_RESPONSE", 0x4e);
    PyModule_AddIntConstant(m, "CREATOR", 0xcf);
    PyModule_AddIntConstant(m, "WAN_UUID", 0x50);
    PyModule_AddIntConstant(m, "OBJECT_CLASS", 0x51);
    PyModule_AddIntConstant(m, "SESSION_PARAMETERS", 0x52);
    PyModule_AddIntConstant(m, "SESSION_SEQUENCE_NUMBER", 0x93);

    PyModule_AddIntConstant(m, "CONTINUE", 0x10);
    PyModule_AddIntConstant(m, "SWITCH_PRO", 0x11);
    PyModule_AddIntConstant(m, "SUCCESS", 0x20);
    PyModule_AddIntConstant(m, "CREATED", 0x21);
    PyModule_AddIntConstant(m, "ACCEPTED", 0x22);
    PyModule_AddIntConstant(m, "NON_AUTHORITATIVE", 0x23);
    PyModule_AddIntConstant(m, "NO_CONTENT", 0x24);
    PyModule_AddIntConstant(m, "RESET_CONTENT", 0x25);
    PyModule_AddIntConstant(m, "PARTIAL_CONTENT", 0x26);
    PyModule_AddIntConstant(m, "MULTIPLE_CHOICES", 0x30);
    PyModule_AddIntConstant(m, "MOVED_PERMANENTLY", 0x31);
    PyModule_AddIntConstant(m, "MOVED_TEMPORARILY", 0x32);
    PyModule_AddIntConstant(m, "SEE_OTHER", 0x33);
    PyModule_AddIntConstant(m, "NOT_MODIFIED", 0x34);
    PyModule_AddIntConstant(m, "USE_PROXY", 0x35);
    PyModule_AddIntConstant(m, "BAD_REQUEST", 0x40);
    PyModule_AddIntConstant(m, "UNAUTHORIZED", 0x41);
    PyModule_AddIntConstant(m, "PAYMENT_REQUIRED", 0x42);
    PyModule_AddIntConstant(m, "FORBIDDEN", 0x43);
    PyModule_AddIntConstant(m, "NOT_FOUND", 0x44);
    PyModule_AddIntConstant(m, "METHOD_NOT_ALLOWED", 0x45);
    PyModule_AddIntConstant(m, "NOT_ACCEPTABLE", 0x46);
    PyModule_AddIntConstant(m, "PROXY_AUTH_REQUIRED", 0x47);
    PyModule_AddIntConstant(m, "REQUEST_TIME_OUT", 0x48);
    PyModule_AddIntConstant(m, "CONFLICT", 0x49);
    PyModule_AddIntConstant(m, "GONE", 0x4a);
    PyModule_AddIntConstant(m, "LENGTH_REQUIRED", 0x4b);
    PyModule_AddIntConstant(m, "PRECONDITION_FAILED", 0x4c);
    PyModule_AddIntConstant(m, "REQ_ENTITY_TOO_LARGE", 0x4d);
    PyModule_AddIntConstant(m, "REQ_URL_TOO_LARGE", 0x4e);
    PyModule_AddIntConstant(m, "UNSUPPORTED_MEDIA_TYPE", 0x4f);
    PyModule_AddIntConstant(m, "INTERNAL_SERVER_ERROR", 0x50);
    PyModule_AddIntConstant(m, "NOT_IMPLEMENTED", 0x51);
    PyModule_AddIntConstant(m, "BAD_GATEWAY", 0x52);
    PyModule_AddIntConstant(m, "SERVICE_UNAVAILABLE", 0x53);
    PyModule_AddIntConstant(m, "GATEWAY_TIMEOUT", 0x54);
    PyModule_AddIntConstant(m, "VERSION_NOT_SUPPORTED", 0x55);
    PyModule_AddIntConstant(m, "DATABASE_FULL", 0x60);
    PyModule_AddIntConstant(m, "DATABASE_LOCKED", 0x61);

    Py_INCREF(clientType);
    PyModule_AddObject(m, "OBEXClient", (PyObject *)clientType);

    Py_INCREF(serverType);
    PyModule_AddObject(m, "OBEXServer", (PyObject *)serverType);
}

