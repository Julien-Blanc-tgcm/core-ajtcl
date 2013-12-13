/**
 * @file
 */
/******************************************************************************
 * Copyright (c) Open Connectivity Foundation (OCF) and AllJoyn Open
 *    Source Project (AJOSP) Contributors and others.
 *
 *    SPDX-License-Identifier: Apache-2.0
 *
 *    All rights reserved. This program and the accompanying materials are
 *    made available under the terms of the Apache License, Version 2.0
 *    which accompanies this distribution, and is available at
 *    http://www.apache.org/licenses/LICENSE-2.0
 *
 *    Copyright (c) Open Connectivity Foundation and Contributors to AllSeen
 *    Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for
 *    any purpose with or without fee is hereby granted, provided that the
 *    above copyright notice and this permission notice appear in all
 *    copies.
 *
 *     THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *     WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *     WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 *     AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *     DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 *     PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 *     TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 *     PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

#include "aj_debug.h"

#ifndef NDEBUG

#include "aj_target.h"
#include "aj_util.h"

/*
 * Set to see the raw message bytes
 */
#define DUMP_MSG_RAW  0

#define Printable(c) (((c) >= ' ') && ((c) <= '~')) ? (c) : '.'

#define CHUNKING 16

void AJ_DumpBytes(const char* tag, const uint8_t* data, uint32_t len)
{
    uint32_t i;
    char ascii[CHUNKING + 1];

    if (tag) {
        AJ_Printf("%s:\n", tag);
    }
    ascii[CHUNKING] = '\0';
    for (i = 0; i < len; i += CHUNKING) {
        uint32_t j;
        for (j = 0; j < CHUNKING; ++j, ++data) {
            if ((i + j) < len) {
                uint8_t n = *data;
                ascii[j] = Printable(n);
                if (n < 0x10) {
                    AJ_Printf("0%x ", n);
                } else {
                    AJ_Printf("%x ", n);
                }
            } else {
                ascii[j] = '\0';
                AJ_Printf("   ");
            }
        }
        ascii[j] = '\0';
        AJ_Printf("    %s\n", ascii);
    }
}

static const char* const msgType[] = { "INVALID", "CALL", "REPLY", "ERROR", "SIGNAL" };

void AJ_DumpMsg(const char* tag, AJ_Message* msg, uint8_t body)
{
    if (msg->hdr && _AJ_DbgHeader(AJ_DEBUG_ERROR, NULL, 0)) {
#if DUMP_MSG_RAW
        uint8_t* p = (uint8_t*)msg->hdr + sizeof(AJ_MsgHeader);
        uint32_t hdrBytes = ((msg->hdr->headerLen + 7) & ~7);
#endif
        AJ_Printf("%s message[%d] type %s sig=\"%s\"\n", tag, msg->hdr->serialNum, msgType[(msg->hdr->msgType <= 4) ? msg->hdr->msgType : 0], msg->signature);
        switch (msg->hdr->msgType) {
        case AJ_MSG_SIGNAL:
        case AJ_MSG_METHOD_CALL:
            AJ_Printf("        %s::%s\n", msg->iface, msg->member);
            break;

        case AJ_MSG_ERROR:
            AJ_Printf("        Error %s\n", msg->error);

        case AJ_MSG_METHOD_RET:
            AJ_Printf("        Reply serial %d\n", msg->replySerial);
            break;
        }
        AJ_Printf("        hdr len=%d\n", msg->hdr->headerLen);
#if DUMP_MSG_RAW
        AJ_DumpBytes(NULL, p,  hdrBytes);
        AJ_Printf("body len=%d\n", msg->hdr->bodyLen);
        if (body) {
            AJ_DumpBytes(NULL, p + hdrBytes, msg->hdr->bodyLen);
        }
        AJ_Printf("-----------------------\n");
#endif
    }
}

AJ_DebugLevel AJ_DbgLevel = AJ_DEBUG_INFO;

int _AJ_DbgHeader(AJ_DebugLevel level, const char* file, int line)
{
    static AJ_Time t;
    uint32_t msec;

    if (!(t.seconds | t.milliseconds)) {
        AJ_InitTimer(&t);
    }
    if (level <= AJ_DbgLevel) {
        msec = AJ_GetElapsedTime(&t, TRUE);
        if (file) {
            const char* fn = file;
            while (*fn) {
                if ((*fn == '/') || (*fn == '\\')) {
                    file = fn + 1;
                }
                ++fn;
            }
            AJ_Printf("%03d.%03d %s@%d ", msec / 1000, msec % 1000, file, line);
        } else {
            AJ_Printf("%03d.%03d ", msec / 1000, msec % 1000);
        }
        return TRUE;
    } else {
        return FALSE;
    }
}

#endif

#define AJ_CASE(_status) case _status: return # _status

const char* AJ_StatusText(AJ_Status status)
{
#ifdef NDEBUG
    /* Expectation is that thin client status codes will NOT go beyond 255 */
    static char code[4];

#ifdef _WIN32
    _snprintf(code, sizeof(code), "%03u", status);
#else
    snprintf(code, sizeof(code), "%03u", status);
#endif

    return code;
#else
    switch (status) {
        AJ_CASE(AJ_OK);
        AJ_CASE(AJ_ERR_NULL);
        AJ_CASE(AJ_ERR_UNEXPECTED);
        AJ_CASE(AJ_ERR_INVALID);
        AJ_CASE(AJ_ERR_IO_BUFFER);
        AJ_CASE(AJ_ERR_READ);
        AJ_CASE(AJ_ERR_WRITE);
        AJ_CASE(AJ_ERR_TIMEOUT);
        AJ_CASE(AJ_ERR_MARSHAL);
        AJ_CASE(AJ_ERR_UNMARSHAL);
        AJ_CASE(AJ_ERR_END_OF_DATA);
        AJ_CASE(AJ_ERR_RESOURCES);
        AJ_CASE(AJ_ERR_NO_MORE);
        AJ_CASE(AJ_ERR_SECURITY);
        AJ_CASE(AJ_ERR_CONNECT);
        AJ_CASE(AJ_ERR_UNKNOWN);
        AJ_CASE(AJ_ERR_NO_MATCH);
        AJ_CASE(AJ_ERR_SIGNATURE);
        AJ_CASE(AJ_ERR_DISALLOWED);
        AJ_CASE(AJ_ERR_FAILURE);
        AJ_CASE(AJ_ERR_RESTART);
        AJ_CASE(AJ_ERR_LINK_TIMEOUT);
        AJ_CASE(AJ_ERR_DRIVER);
        AJ_CASE(AJ_ERR_OBJECT_PATH);
        AJ_CASE(AJ_ERR_BUSY);

    default:
        return "<unknown>";
    }

#endif
}