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
#define AJ_MODULE EVENTACTION_SERVICE

#include <stdio.h>
#include <ajtcl/aj_debug.h>
#include <ajtcl/aj_guid.h>
#include <ajtcl/aj_creds.h>
#include <ajtcl/alljoyn.h>

#define CONNECT_ATTEMPTS   10
static const char ServiceName[] = "example.alljoyn.Bus.eventaction.sample";
static const char ServicePath[] = "/eventaction";
static const uint16_t ServicePort = 50;
static char buffer[60];

uint8_t dbgEVENTACTION_SERVICE = 0;
/**
 * The interface name followed by the method signatures.
 *
 * See also .\inc\aj_introspect.h
 */
static const char* const sampleInterface[] = {
    "example.alljoyn.Bus.eventaction.sample",   /* The first entry is the interface name. */
    "?dummyMethod foo<i",             /* This is just a dummy entry at index 0 for illustration purposes. */
    "?joinMethod inStr1<s inStr2<s outStr>s", /* Method at index 1. */
    "!someSignal name>s",
    "!&someSessionlessSignal", /* & indicates that a signal is designated as sessionless so the introspection will describe it as such */
    NULL
};

static const char* const objDesc[] = { "Sample object description", "ES: Sample object description" };
static const char* const intfDesc[] = { "Sample interface", "ES: Sample interface" };
static const char* const joinDesc[] = { "Join two strings and return the result", "ES: Join two strings and return the result" };
static const char* const joinInArg1Desc[] = { "First part of string", "ES: First part of string" };
static const char* const joinInArg2Desc[] = { "Second part of string", "ES: Second part of string" };
static const char* const joinOutArgDesc[] = { "Return result", "ES: Return result" };
static const char* const someSignalArgDesc[] = { "EN: %s", "ES: %s" };
static const char* const someSessionlessSignalDesc[] = { "An example sessionless signal", "ES: An example sessionless signal" };

/*
 * AJ_DESCRIPTION_ID(BusObject base ID, Interface index, Member index, Arg index)
 * Interface, Member, and Arg indexes starts at 1 and represent the readible index in a list.
 * [ a, b, ... ] a would be index 1, b 2, etc.
 */
#define SAMPLE_OBJECT_ID                    0
#define SAMPLE_OBJECT_DESC                  AJ_DESCRIPTION_ID(SAMPLE_OBJECT_ID, 0, 0, 0)
#define SAMPLE_INTERFACE_DESC               AJ_DESCRIPTION_ID(SAMPLE_OBJECT_ID, 1, 0, 0)
#define SAMPLE_JOIN_DESC                    AJ_DESCRIPTION_ID(SAMPLE_OBJECT_ID, 1, 2, 0)
#define SAMPLE_JOIN_ARG_INSTR1_DESC         AJ_DESCRIPTION_ID(SAMPLE_OBJECT_ID, 1, 2, 1)
#define SAMPLE_JOIN_ARG_INSTR2_DESC         AJ_DESCRIPTION_ID(SAMPLE_OBJECT_ID, 1, 2, 2)
#define SAMPLE_JOIN_ARG_OUTSTR_DESC         AJ_DESCRIPTION_ID(SAMPLE_OBJECT_ID, 1, 2, 3)
#define SAMPLE_SOMESIGNAL_ARG_DESC          AJ_DESCRIPTION_ID(SAMPLE_OBJECT_ID, 1, 3, 1)
#define SAMPLE_SOMESESSIONLESSSIGNAL_DESC   AJ_DESCRIPTION_ID(SAMPLE_OBJECT_ID, 1, 4, 0)

static const char* const languages[] = { "en", "es", NULL };

static const char* MyTranslator(uint32_t descId, const char* lang) {
    uint8_t langIndex;

    /* Compute the location of lang in our languages array */
    langIndex = 0;
    while (lang && languages[langIndex] != NULL) {
        if (strlen(lang) > 0 && strcmp(lang, languages[langIndex]) == 0) {
            break;
        }
        ++langIndex;
    }
    /* If all languages in list did not match, then set index to 0 (default) language */
    if (languages[langIndex] == NULL) {
        langIndex = 0;
    }

    /* Return correct lang string for descId */
    switch (descId) {

    case SAMPLE_OBJECT_DESC:
        return objDesc[langIndex];

    case SAMPLE_INTERFACE_DESC:
        return intfDesc[langIndex];

    case SAMPLE_JOIN_DESC:
        return joinDesc[langIndex];

    case SAMPLE_JOIN_ARG_INSTR1_DESC:
        return joinInArg1Desc[langIndex];

    case SAMPLE_JOIN_ARG_INSTR2_DESC:
        return joinInArg2Desc[langIndex];

    case SAMPLE_JOIN_ARG_OUTSTR_DESC:
        return joinOutArgDesc[langIndex];

    case SAMPLE_SOMESIGNAL_ARG_DESC:
        sprintf(buffer, someSignalArgDesc[langIndex], "Some replacement value");
        return buffer;

    case SAMPLE_SOMESESSIONLESSSIGNAL_DESC:
        return someSessionlessSignalDesc[langIndex];

    }
    /* No description set so return NULL */
    return NULL;
}

static AJ_Status MyAboutPropGetter(AJ_Message* reply, const char* language)
{
    AJ_Status status = AJ_OK;
    AJ_Arg array;
    AJ_GUID guid;
    char guidStr[16 * 2 + 1];
    uint8_t appId[16];
    guidStr[16 * 2] = '\0';

    if ((language != NULL) && (0 != strcmp(language, languages[0])) && (0 != strcmp(language, languages[1])) && (0 != strcmp(language, ""))) {
        /* the language supplied was not supported */
        return AJ_ERR_NO_MATCH;
    }

    status = AJ_GetLocalGUID(&guid);
    if (status != AJ_OK) {
        return status;
    }
    AJ_GUID_ToString(&guid, guidStr, sizeof(guidStr));
    status = AJ_HexToRaw(guidStr, 0, appId, 16);
    if (status != AJ_OK) {
        return status;
    }

    status = AJ_MarshalContainer(reply, &array, AJ_ARG_ARRAY);
    if (status == AJ_OK) {
        status = AJ_MarshalArgs(reply, "{sv}", AJ_APP_ID_STR, "ay", appId, 16);
    }
    if (status == AJ_OK) {
        status = AJ_MarshalArgs(reply, "{sv}", AJ_APP_NAME_STR, "s", "eventaction_service");
    }
    if (status == AJ_OK) {
        status = AJ_MarshalArgs(reply, "{sv}", AJ_DEVICE_ID_STR, "s", guidStr);
    }
    if (status == AJ_OK) {
        status = AJ_MarshalArgs(reply, "{sv}", AJ_DEVICE_NAME_STR, "s", "Tester");
    }
    if (status == AJ_OK) {
        status = AJ_MarshalArgs(reply, "{sv}", AJ_MANUFACTURER_STR, "s", "QCE");
    }
    if (status == AJ_OK) {
        status = AJ_MarshalArgs(reply, "{sv}", AJ_MODEL_NUMBER_STR, "s", "1.0");
    }
    //SupportedLanguages
    if (status == AJ_OK) {
        AJ_Arg dict;
        AJ_Arg languageListArray;
        status = AJ_MarshalContainer(reply, &dict, AJ_ARG_DICT_ENTRY);
        if (status == AJ_OK) {
            status = AJ_MarshalArgs(reply, "s", AJ_SUPPORTED_LANGUAGES_STR);
        }
        if (status == AJ_OK) {
            status = AJ_MarshalVariant(reply, "as");
        }
        if (status == AJ_OK) {
            status = AJ_MarshalContainer(reply, &languageListArray, AJ_ARG_ARRAY);
        }
        if (status == AJ_OK) {
            status = AJ_MarshalArgs(reply, "s", languages[0]);
        }
        if (status == AJ_OK) {
            status = AJ_MarshalArgs(reply, "s", languages[1]);
        }
        if (status == AJ_OK) {
            status = AJ_MarshalCloseContainer(reply, &languageListArray);
        }
        if (status == AJ_OK) {
            status = AJ_MarshalCloseContainer(reply, &dict);
        }
    }
    if (status == AJ_OK) {
        status = AJ_MarshalArgs(reply, "{sv}", AJ_DESCRIPTION_STR, "s", "eventaction_service test app");
    }
    if (status == AJ_OK) {
        status = AJ_MarshalArgs(reply, "{sv}", AJ_DEFAULT_LANGUAGE_STR, "s", languages[0]);
    }
    if (status == AJ_OK) {
        status = AJ_MarshalArgs(reply, "{sv}", AJ_SOFTWARE_VERSION_STR, "s", AJ_GetVersion());
    }
    if (status == AJ_OK) {
        status = AJ_MarshalArgs(reply, "{sv}", AJ_AJSOFTWARE_VERSION_STR, "s", AJ_GetVersion());
    }
    if (status == AJ_OK) {
        status = AJ_MarshalCloseContainer(reply, &array);
    }
    return status;
}

/**
 * A NULL terminated collection of all interfaces.
 */
static const AJ_InterfaceDescription sampleInterfaces[] = {
    sampleInterface,
    NULL
};

/**
 * Objects implemented by the application. The first member in the AJ_Object structure is the path.
 * The second is the collection of all interfaces at that path.
 */
static const AJ_Object AppObjects[] = {
    { ServicePath, sampleInterfaces, AJ_OBJ_FLAG_DESCRIBED, NULL },
    { NULL }
};

/*
 * The value of the arguments are the indices of the
 * object path in AppObjects (above), interface in sampleInterfaces (above), and
 * member indices in the interface.
 * The 'cat' index is 1 because the first entry in sampleInterface is the interface name.
 * This makes the first index (index 0 of the methods) the second string in
 * sampleInterface[] which, for illustration purposes is a dummy entry.
 * The index of the method we implement for basic_service, 'cat', is 1 which is the third string
 * in the array of strings sampleInterface[].
 *
 * See also .\inc\aj_introspect.h
 */
#define EVENTACTION_SERVICE_CAT AJ_APP_MESSAGE_ID(0, 0, 1)

static AJ_Status AppHandleCat(AJ_Message* msg)
{
    const char* string0;
    const char* string1;
    char tmpBuffer[256];
    AJ_Message reply;
    AJ_Arg replyArg;

    AJ_UnmarshalArgs(msg, "ss", &string0, &string1);
    AJ_MarshalReplyMsg(msg, &reply);

    /* We have the arguments. Now do the concatenation. */
    strncpy(tmpBuffer, string0, ArraySize(tmpBuffer));
    tmpBuffer[ArraySize(tmpBuffer) - 1] = '\0';
    strncat(tmpBuffer, string1, ArraySize(tmpBuffer) - strlen(tmpBuffer) - 1);

    AJ_InitArg(&replyArg, AJ_ARG_STRING, 0, tmpBuffer, 0);
    AJ_MarshalArg(&reply, &replyArg);

    return AJ_DeliverMsg(&reply);
}

/* All times are expressed in milliseconds. */
#define CONNECT_TIMEOUT     (1000 * 60)
#define UNMARSHAL_TIMEOUT   (1000 * 5)
#define SLEEP_TIME          (1000 * 2)

int AJ_Main(void)
{
    AJ_Status status = AJ_OK;
    AJ_BusAttachment bus;
    uint8_t connected = FALSE;
    uint32_t sessionId = 0;

    /* One time initialization before calling any other AllJoyn APIs. */
    AJ_Initialize();

    /* Set the languages and a lookup function so that we can print out the default descriptions in the AJ_PrintXML call */
    AJ_RegisterDescriptionLanguages(languages);

    AJ_RegisterObjectListWithDescriptions(AppObjects, 1, MyTranslator);
    AJ_AboutRegisterPropStoreGetter(MyAboutPropGetter);

    /* This is for debug purposes and is optional. */
    AJ_AlwaysPrintf(("XML with no Descriptions\n"));
    AJ_PrintXML(AppObjects);
    AJ_AlwaysPrintf(("XML with Descriptions using language: %s\n", languages[0]));
    AJ_PrintXMLWithDescriptions(AppObjects, languages[0]);
    AJ_AlwaysPrintf(("XML with Descriptions using language: %s\n", languages[1]));
    AJ_PrintXMLWithDescriptions(AppObjects, languages[1]);
    AJ_AlwaysPrintf(("XML with empty language\n"));
    AJ_PrintXMLWithDescriptions(AppObjects, "");
    AJ_AlwaysPrintf(("XML with unsupported language (fr)\n"));
    AJ_PrintXMLWithDescriptions(AppObjects, "fr");
    AJ_AlwaysPrintf(("XML with sublanguage (EN-US)\n"));
    AJ_PrintXMLWithDescriptions(AppObjects, "EN-US");
    AJ_AlwaysPrintf(("XML in Unified Format\n"));
    AJ_PrintXMLWithDescriptions(AppObjects, NULL);

    while (TRUE) {
        AJ_Message msg;

        if (!connected) {
            status = AJ_StartService(&bus,
                                     NULL,
                                     CONNECT_TIMEOUT,
                                     FALSE,
                                     ServicePort,
                                     ServiceName,
                                     AJ_NAME_REQ_DO_NOT_QUEUE,
                                     NULL);

            if (status != AJ_OK) {
                continue;
            }

            AJ_InfoPrintf(("StartService returned %d, session_id=%u\n", status, sessionId));
            connected = TRUE;
        }

        status = AJ_UnmarshalMsg(&bus, &msg, UNMARSHAL_TIMEOUT);

        if (AJ_ERR_TIMEOUT == status) {
            continue;
        }

        if (AJ_OK == status) {
            switch (msg.msgId) {
            case AJ_METHOD_ACCEPT_SESSION:
                {
                    uint16_t port;
                    char* joiner;
                    AJ_UnmarshalArgs(&msg, "qus", &port, &sessionId, &joiner);
                    status = AJ_BusReplyAcceptSession(&msg, TRUE);
                    AJ_AlwaysPrintf(("Accepted session session_id=%u joiner=%s\n", sessionId, joiner));
                }
                break;

            case EVENTACTION_SERVICE_CAT:
                status = AppHandleCat(&msg);
                break;

            case AJ_SIGNAL_SESSION_LOST_WITH_REASON:
                {
                    uint32_t id, reason;
                    AJ_UnmarshalArgs(&msg, "uu", &id, &reason);
                    AJ_AlwaysPrintf(("Session lost. ID = %u, reason = %u\n", id, reason));
                }
                break;

            default:
                /* Pass to the built-in handlers. */
                status = AJ_BusHandleBusMessage(&msg);
                break;
            }
        }

        /* Messages MUST be discarded to free resources. */
        AJ_CloseMsg(&msg);

        if ((status == AJ_ERR_READ) || (status == AJ_ERR_WRITE)) {
            AJ_AlwaysPrintf(("AllJoyn disconnect.\n"));
            AJ_Disconnect(&bus);
            connected = FALSE;

            /* Sleep a little while before trying to reconnect. */
            AJ_Sleep(SLEEP_TIME);
        }
    }

    AJ_AlwaysPrintf(("Basic service exiting with status %d.\n", status));

    return status;
}

#ifdef AJ_MAIN
int main()
{
    return AJ_Main();
}
#endif