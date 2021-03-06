/**
 * @file
 */
/******************************************************************************
 *    Copyright (c) Open Connectivity Foundation (OCF), AllJoyn Open Source
 *    Project (AJOSP) Contributors and others.
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
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL
 *    WARRANTIES WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED
 *    WARRANTIES OF MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE
 *    AUTHOR BE LIABLE FOR ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL
 *    DAMAGES OR ANY DAMAGES WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR
 *    PROFITS, WHETHER IN AN ACTION OF CONTRACT, NEGLIGENCE OR OTHER
 *    TORTIOUS ACTION, ARISING OUT OF OR IN CONNECTION WITH THE USE OR
 *    PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

/**
 * Per-module definition of the current module for debug logging.  Must be defined
 * prior to first inclusion of aj_debug.h
 */
#define AJ_MODULE INTROSPECT

#include <ajtcl/aj_target.h>
#include <ajtcl/aj_debug.h>
#include <ajtcl/aj_introspect.h>
#include <ajtcl/aj_std.h>
#include <ajtcl/aj_msg.h>
#include <ajtcl/aj_msg_priv.h>
#include <ajtcl/aj_debug.h>
#include <ajtcl/aj_util.h>
#include <ajtcl/aj_debug.h>
#include <ajtcl/aj_config.h>
#include <ajtcl/aj_authorisation.h>
/**
 * Turn on per-module debug printing by setting this variable to non-zero value
 * (usually in debugger).
 */
#ifdef AJ_DEBUG_BUILD
uint8_t dbgINTROSPECT = 0;
#endif

#if defined(_WIN32)
#define strcasecmp _stricmp
#endif

/**
 * The various object lists
 */
static const AJ_Object* objectLists[AJ_MAX_OBJECT_LISTS] = { AJ_StandardObjects, NULL, NULL };

/**
 * The language list
 */
static const char* const* languageList = NULL;

/**
 * The root object
 */
const AJ_Object AJ_ROOT_OBJECT = { "/", NULL, 0, NULL };

/**
 * ObjectIterator exclusion flags mask for Introspectable objects
 */
#define AJ_OBJ_FLAGS_INTROSPECTABLE_EXCLUDE_MASK (AJ_OBJ_FLAG_HIDDEN | AJ_OBJ_FLAG_DISABLED | AJ_OBJ_FLAG_IS_PROXY)

/**
 * Struct for a reply context for a method call
 */
typedef struct _ReplyContext {
    AJ_Time callTime;    /**< Time the method call was made - used for timeouts */
    uint32_t timeout;    /**< How long to wait for a reply */
    uint32_t serial;     /**< Serial number for the reply message */
    uint32_t messageId;  /**< The unique message id for the call */
    char uniqueName[AJ_MAX_NAME_SIZE + 1]; /**< Reply sender's unique name */
} ReplyContext;

static ReplyContext replyContexts[AJ_NUM_REPLY_CONTEXTS];

/**
 * Function used by XML generator to push generated XML
 */
typedef void (*XMLWriterFunc)(void* context, const char* str, uint32_t len);

/*
 * Per object list description lookup function
 */
static AJ_DescriptionLookupFunc descriptionLookups[AJ_MAX_OBJECT_LISTS] = { NULL, NULL, NULL };

#define IN_ARG     '<'  /* 0x3C */
#define OUT_ARG    '>'  /* 0x3E */

#define SEPARATOR  ' '

#define IS_DIRECTION(c) (((c) >= IN_ARG) && ((c) <= OUT_ARG))

#define IS_SESSIONLESS(c) ((c) == SESSIONLESS)

static const char* const MemberOpen[] = {
    "  <signal",
    "  <method",
    "  <property"
};

static const char* const MemberClose[] = {
    "  </signal>\n",
    "  </method>\n",
    "/>\n",
    "  </property>\n", /* One element larger than the close to allow for a property to encompass a description tag */
};

static const char* const Access[] = {
    "\" access=\"write\"",
    "\" access=\"readwrite\"",
    "\" access=\"read\""
};

/*
 * Table contains 6 elements to allow for the ability to encompass a description tag if one exists.
 * The computed index for closure of the tag has 3 added to it when a description tag is present.
 */
static const char* const Direction[] = {
    "\" direction=\"in\"/>\n",
    "\"/>\n",
    "\" direction=\"out\"/>\n",
    "\" direction=\"in\">\n",
    "\">\n",
    "\" direction=\"out\">\n"
};

static const char introspectDocType[] =
    "<!DOCTYPE"
    " node PUBLIC \"-//allseen//DTD ALLJOYN Object Introspection 1.1//EN\"\n"
    "\"http://www.allseen.org/alljoyn/introspect-1.1.dtd\""
    ">\n";

static const char interfaceOpen[] = "<interface";
static const char interfaceClose[] = "</interface>\n";
static const char argOpen[] = "    <arg";
static const char argClose[] = "    </arg>\n";

static const char nameAttr[] = " name=\"";
static const char typeAttr[] = " type=\"";
static const char sessionlessAttr[] = " sessionless=\"";
static const char trueVal[] = "true\"";
static const char falseVal[] = "false\"";
static const char annotateSessionless[] = "    <annotation name=\"org.alljoyn.Bus.Signal.Sessionless\" value=\"true\" /";

static const char nodeOpen[] = "<node";
static const char nodeClose[] = "</node>\n";
static const char annotateSecure[] = "  <annotation name=\"org.alljoyn.Bus.Secure\" value=\"";
static const char secureTrue[] = "true\"/>\n";
static const char secureOff[] = "off\"/>\n";



static char ExpandAttribute(XMLWriterFunc XMLWriter, void* context, const char** str, const char* pre, const char* post)
{
    uint32_t len = 0;
    char next = 0;
    const char* s = *str;

    XMLWriter(context, pre, 0);
    while (*s) {
        char c = *s++;
        if (IS_DIRECTION(c) || (c == SEPARATOR)) {
            next = c;
            break;
        }
        ++len;
    }
    XMLWriter(context, *str, len);
    XMLWriter(context, post, 0);
    *str = s;

    return next;
}

static void XMLWriteTag(XMLWriterFunc XMLWriter, void* context, const char* tag, const char* attr, const char* val, uint32_t valLen, uint8_t atom)
{
    XMLWriter(context, tag, 0);
    if (attr != NULL) {
        XMLWriter(context, attr, 0);
        XMLWriter(context, val, valLen);
        XMLWriter(context, "\"", 1);
    }
    if (atom) {
        XMLWriter(context, "/>\n", 3);
    } else {
        XMLWriter(context, ">\n", 2);
    }
}

static const char* GetDescription(AJ_DescriptionLookupFunc descLookup, uint32_t descId, const char* languageTag)
{
    if (descLookup == NULL) {
        return NULL;
    }

    return descLookup(descId, languageTag);
}

static uint8_t IsDescriptionAvailable(AJ_DescriptionLookupFunc descLookup, uint32_t descId)
{
    size_t idx;
    if (descLookup == NULL || languageList == NULL) {
        return FALSE;
    }

    for (idx = 0; languageList[idx] != NULL; idx++) {
        if (descLookup(descId, languageList[idx]) != NULL) {
            return TRUE;
        }
    }

    return FALSE;
}

static void XMLWriteDescription(XMLWriterFunc XMLWriter, void* context, uint8_t level, const char* description, const char* languageTag)
{
    uint8_t i;
    if (description == NULL) {
        return;
    }

    for (i = 0; i < level; i++) {
        XMLWriter(context, "    ", 4);
    }
    XMLWriter(context, "<description", 12);
    if (languageTag != NULL && strlen(languageTag) > 0) {
        XMLWriter(context, " language=\"", 11);
        XMLWriter(context, languageTag, 0);
        XMLWriter(context, "\"", 1);
    }
    XMLWriter(context, ">", 1);
    XMLWriter(context, description, 0);
    XMLWriter(context, "</description>\n", 15);
}

static void XMLWriteUnifiedDescription(XMLWriterFunc XMLWriter, void* context, uint8_t level, const char* description, const char* languageTag)
{
    uint8_t i;
    if (description == NULL) {
        return;
    }

    for (i = 0; i < level; i++) {
        XMLWriter(context, "  ", 2);
    }

    XMLWriter(context, "<annotation name=\"org.alljoyn.Bus.DocString", 43);
    if (languageTag != NULL && strlen(languageTag) > 0) {
        XMLWriter(context, ".", 1);
        XMLWriter(context, languageTag, 0);
    }
    XMLWriter(context, "\" value=\"", 9);
    XMLWriter(context, description, 0);
    XMLWriter(context, "\"/>\n", 4);
}

static void XMLWriteUnifiedDescriptions(XMLWriterFunc XMLWriter, void* context, uint8_t level, AJ_DescriptionLookupFunc descLookup, uint32_t descId)
{
    size_t idx;
    for (idx = 0; languageList[idx] != NULL; idx++) {
        const char* description = GetDescription(descLookup, descId, languageList[idx]);
        if (description != NULL) {
            XMLWriteUnifiedDescription(XMLWriter, context, level, description, languageList[idx]);
        }
    }
}

static AJ_Status ExpandInterfaces(XMLWriterFunc XMLWriter, void* context, const AJ_InterfaceDescription* iface, AJ_DescriptionLookupFunc descLookup, uint32_t descObjId, const char* languageTag)
{
    uint32_t descId;
    uint8_t ifaceIndex = 0;
    const uint8_t unifiedFormat = (languageTag == NULL);
    if (!iface) {
        return AJ_OK;
    }
    while (*iface) {
        uint8_t memberIndex = 0;
        const char* const* entries = *iface;
        char flag = entries[0][0];
        uint8_t dBus_std_iface = FALSE;
        const char* description = NULL;

        /* Increase the interface index since we start at 1 */
        ++ifaceIndex;
        descId = descObjId | (((uint32_t)(ifaceIndex)) << 16);

        if ((flag == SECURE_TRUE) || (flag == SECURE_OFF)) {

            /* If it is a common standard interface, do not add any annotations.*/
            if ((strcmp(entries[0], AJ_IntrospectionIface[0]) == 0) ||
                (strcmp(entries[0], AJ_PropertiesIface[0]) == 0) ||
                (strcmp(entries[0], DBusPeerInterface) == 0) ||
                (strcmp(entries[0], AllSeenIntrospectableInterface) == 0)) {
                dBus_std_iface = TRUE;
            }

            /*
             * If flagged as secure or not secure, skip the first char (the '$' or '#') of the name
             */
            XMLWriteTag(XMLWriter, context, interfaceOpen, nameAttr, entries[0] + 1, 0, FALSE);
            if (!dBus_std_iface) {
                XMLWriter(context, annotateSecure, sizeof(annotateSecure) - 1);
                if (flag == SECURE_TRUE) {
                    XMLWriter(context, secureTrue, sizeof(secureTrue) - 1);
                } else {
                    XMLWriter(context, secureOff, sizeof(secureOff) - 1);
                }
            }
        } else {
            XMLWriteTag(XMLWriter, context, interfaceOpen, nameAttr, entries[0], 0, FALSE);
        }
        if (unifiedFormat) {
            if (IsDescriptionAvailable(descLookup, descId)) {
                XMLWriteUnifiedDescriptions(XMLWriter, context, 1, descLookup, descId);
            }
        } else {
            description = GetDescription(descLookup, descId, languageTag);
            if (description != NULL) {
                XMLWriteDescription(XMLWriter, context, 0, description, languageTag);
            }
        }

        while (*(++entries)) {
            uint8_t argIndex = 0;
            const char* member = *entries;
            uint8_t memberType = MEMBER_TYPE(*member++);
            uint8_t attr;
            uint8_t isSessionless = FALSE;
            uint32_t memberDescId = 0;
            uint8_t descriptionAvailable = FALSE;

            /* Increase index since we start at 1 */
            ++memberIndex;
            if (memberType > 2) {
                AJ_ErrPrintf(("ExpandInterfaces(): %s", AJ_StatusText(AJ_ERR_UNEXPECTED)));
                return AJ_ERR_UNEXPECTED;
            }
            XMLWriter(context, MemberOpen[memberType], 0);
            if (memberType == SIGNAL && IS_SESSIONLESS(*member)) {
                /*
                 * Advance so that we do not return a '&' character
                 */
                member++;
                isSessionless = TRUE;
            }
            attr = ExpandAttribute(XMLWriter, context, &member, nameAttr, "\"");
            if (memberType == PROPERTY) {
                uint8_t acc;
                if (attr == SEPARATOR) {
                    attr = member++[0];
                }
                acc = attr - WRITE_ONLY;
                if (acc > 2) {
                    AJ_ErrPrintf(("ExpandInterfaces(): %s", AJ_StatusText(AJ_ERR_UNEXPECTED)));
                    return AJ_ERR_UNEXPECTED;
                }
                ExpandAttribute(XMLWriter, context, &member, typeAttr, Access[acc]);
            } else {
                /*
                 * If we are using the AllSeen introspection then add isSessionless
                 */
                if (memberType == SIGNAL) {
                    if (!unifiedFormat) {
                        XMLWriter(context, sessionlessAttr, sizeof(sessionlessAttr) - 1);
                        if (isSessionless) {
                            XMLWriter(context, trueVal, sizeof(trueVal) - 1);
                        } else {
                            XMLWriter(context, falseVal, sizeof(falseVal) - 1);
                        }
                    } else {
                        if (isSessionless) {
                            XMLWriter(context, ">\n", 2);
                            XMLWriter(context, annotateSessionless, sizeof(annotateSessionless) - 1);
                        }
                    }
                }
                XMLWriter(context, ">\n", 2);
                while (attr) {
                    uint8_t dir;
                    uint32_t attrDescId = 0;

                    /* increase arg index since we start at 1 */
                    ++argIndex;
                    attrDescId = (descId | (((uint32_t)memberIndex) << 8) | ((uint32_t)argIndex));

                    XMLWriter(context, argOpen, sizeof(argOpen) - 1);
                    if (IS_DIRECTION(*member)) {
                        dir = *member++ - IN_ARG;
                    } else {
                        dir = ExpandAttribute(XMLWriter, context, &member, nameAttr, "\"") - IN_ARG;
                    }
                    if ((dir != 0) && (dir != 2)) {
                        AJ_ErrPrintf(("ExpandInterfaces(): %s", AJ_StatusText(AJ_ERR_UNEXPECTED)));
                        return AJ_ERR_UNEXPECTED;
                    }
                    if (memberType == SIGNAL) {
                        dir = 1;
                    }
                    /*
                     * If we have a description then wait to close the tag until after adding in a description tag
                     */
                    if (unifiedFormat) {
                        descriptionAvailable = IsDescriptionAvailable(descLookup, attrDescId);
                    } else {
                        description = GetDescription(descLookup, attrDescId, languageTag);
                        descriptionAvailable = (description != NULL) ? TRUE : FALSE;
                    }
                    if (descriptionAvailable) {
                        dir += 3;
                    }
                    attr = ExpandAttribute(XMLWriter, context, &member, typeAttr, Direction[dir]);

                    if (descriptionAvailable) {
                        if (unifiedFormat) {
                            XMLWriteUnifiedDescriptions(XMLWriter, context, 3, descLookup, attrDescId);
                        } else {
                            XMLWriteDescription(XMLWriter, context, 2, description, languageTag);
                        }
                        XMLWriter(context, argClose, sizeof(argClose) - 1);
                    }
                }
            }

            memberDescId = (descId | (((uint32_t)memberIndex) << 8));
            if (unifiedFormat) {
                descriptionAvailable = IsDescriptionAvailable(descLookup, memberDescId);
            } else {
                description = GetDescription(descLookup, memberDescId, languageTag);
                descriptionAvailable = (description != NULL) ? TRUE : FALSE;
            }
            if (descriptionAvailable) {
                if (memberType == PROPERTY) {
                    XMLWriter(context, ">\n", 2);
                    /*
                     * Move to the alternate close for a propety
                     * which is in the memberClose table of entry 4
                     */
                    memberType++;
                }
                if (unifiedFormat) {
                    XMLWriteUnifiedDescriptions(XMLWriter, context, 2, descLookup, memberDescId);
                } else {
                    XMLWriteDescription(XMLWriter, context, 1, description, languageTag);
                }
            }
            XMLWriter(context, MemberClose[memberType], 0);
        }
        XMLWriter(context, interfaceClose, sizeof(interfaceClose) - 1);
        ++iface;
    }
    return AJ_OK;
}

/*
 * Check if the path c is child of path p if so return pointer to the start of the child path
 * relative to the parent.
 */
static const char* ChildPath(const char* p, const char* c, uint32_t* sz)
{
    /*
     * Special case for parent == root (all nodes are children of root)
     */
    if ((p[0] == '/') && (p[1] == 0)) {
        ++p;
    }
    while (*p && (*p == *c)) {
        ++p;
        ++c;
    }
    if ((*p == '\0') && (*c == '/')) {
        uint32_t len = 0;
        ++c;
        while (c[len] && c[len] != '/') {
            ++len;
        }
        if (sz) {
            *sz = len;
        }
        /*
         * Return then isolated node name of the child
         */
        return len ? c : NULL;
    } else {
        return NULL;
    }
}

static const AJ_Object* FirstInstance(const char* path, const char* child, uint32_t sz)
{
    AJ_ObjectIterator iter;
    const AJ_Object* obj = AJ_InitObjectIterator(&iter, AJ_OBJ_FLAGS_ALL_INCLUDE_MASK, AJ_OBJ_FLAGS_INTROSPECTABLE_EXCLUDE_MASK);

    while (obj != NULL) {
        uint32_t len;
        const char* c = ChildPath(path, obj->path, &len);
        if (c && (len == sz) && (memcmp(c, child, sz) == 0)) {
            return obj;
        }
        obj = AJ_NextObject(&iter);
    }
    return obj;
}

/*
 * Security applies if the interface is secure or if the object or it's parent object is flagged as
 * secure and the security is not explicitly OFF for the interface.
 */
static uint32_t SecurityApplies(const char* ifc, const AJ_Object* obj)
{
    AJ_ObjectIterator iter;
    const AJ_Object* lookup;

    if (ifc) {
        if (*ifc == SECURE_TRUE) {
            return TRUE;
        }
        if (*ifc == SECURE_OFF) {
            return FALSE;
        }
    }
    if (obj->flags & AJ_OBJ_FLAG_SECURE) {
        return TRUE;
    }
    /*
     * Check that obj is not a child of a secure parent object
     */
    lookup = AJ_InitObjectIterator(&iter, AJ_OBJ_FLAGS_ALL_INCLUDE_MASK, AJ_OBJ_FLAG_IS_PROXY);
    while (lookup != NULL) {
        if ((lookup->flags & AJ_OBJ_FLAG_SECURE) && ChildPath(lookup->path, obj->path, NULL)) {
            return TRUE;
        }
        lookup = AJ_NextObject(&iter);
    }
    return FALSE;
}

/*
 * Find the best matching language tag, using the lookup algorithm in
 * RFC 4647 section 3.4.  This algorithm requires that the "supported"
 * languages be the least specific they can (e.g., "en" in order to match
 * both "en" and "en-US" if requested), and the "requested" language be
 * the most specific it can (e.g., "en-US" in order to match either "en-US"
 * or "en" if supported).
 */
#define MAX_LANG_SIZE 63
static const char* GetBestLanguage(const char* requested)
{
    if ((requested != NULL) && (*requested != 0) && (languageList != NULL)) {
        char languageToCheck[MAX_LANG_SIZE + 1];
        strncpy(languageToCheck, requested, MAX_LANG_SIZE);
        languageToCheck[MAX_LANG_SIZE] = '\0';
        for (;;) {
            // Look for a supported language matching the language to check.
            size_t idx;
            char* pos;
            for (idx = 0; languageList[idx] != NULL; idx++) {
                if (strcasecmp(languageList[idx], languageToCheck) == 0) {
                    return languageList[idx];
                }
            }

            // Drop the last subtag and try again.
            pos = strrchr(languageToCheck, '-');
            if (pos == NULL) {
                break;
            }
            *pos = 0;
        }
    }

    // No match found, so return the default language.
    return (languageList != NULL) ? languageList[0] : NULL;
}

static AJ_Status GenXML(XMLWriterFunc XMLWriter, void* context, const AJ_ObjectIterator* objIter, const AJ_Object* virtualObject, const char* languageTag)
{
    AJ_Status status = AJ_OK;
    const AJ_Object* obj;
    AJ_DescriptionLookupFunc descLookup = NULL;
    uint8_t unifiedFormat = (languageTag == NULL) ? TRUE : FALSE;

    if (objIter == NULL) {
        obj = virtualObject;
    } else {
        if (objIter->l >= ArraySize(objectLists)) {
            if (virtualObject == NULL) {
                return AJ_OK;
            } else {
                AJ_ErrPrintf(("Failed to generate XML - Invalid object iterator\n"));
                return AJ_ERR_UNEXPECTED;
            }
        }

        obj = &(objectLists[objIter->l][objIter->n - 1]);
    }
    if (obj != NULL && obj->path != NULL) {
        AJ_ObjectIterator childObjectIter;
        const AJ_Object* childObj = AJ_InitObjectIterator(&childObjectIter, AJ_OBJ_FLAGS_ALL_INCLUDE_MASK, AJ_OBJ_FLAGS_INTROSPECTABLE_EXCLUDE_MASK);
        const char* description = NULL;

        /*
         * Ignore objects that are hidden or disabled or proxy
         */
        if (obj->flags & (AJ_OBJ_FLAGS_INTROSPECTABLE_EXCLUDE_MASK)) {
            return AJ_OK;
        }
        /*
         * Find matching description lookup function.
         */
        if (objIter != NULL) {
            descLookup = descriptionLookups[objIter->l];
        }
        if (!unifiedFormat) {
            languageTag = GetBestLanguage(languageTag);
        }
        /*
         * Generate object's XML
         */
        XMLWriter(context, introspectDocType, sizeof(introspectDocType) - 1);
        XMLWriteTag(XMLWriter, context, nodeOpen, nameAttr, obj->path, 0, FALSE);
        if (SecurityApplies(NULL, obj)) {
            XMLWriter(context, annotateSecure, 51);
            XMLWriter(context, secureTrue, 8);
        }
        if (objIter != NULL) {
            uint32_t descId = (objIter->n - 1) << 24;

            if (unifiedFormat) {
                if (IsDescriptionAvailable(descLookup, descId)) {
                    XMLWriteUnifiedDescriptions(XMLWriter, context, 0, descLookup, descId);
                }
            } else {
                description = GetDescription(descLookup, descId, languageTag);
                if (description != NULL) {
                    XMLWriteDescription(XMLWriter, context, 0, description, languageTag);
                }
            }
        }

        if (status == AJ_OK) {
            if (objIter != NULL) {
                status = ExpandInterfaces(XMLWriter, context, obj->interfaces, descLookup, (objIter->n - 1) << 24, languageTag);
            } else {
                status = ExpandInterfaces(XMLWriter, context, obj->interfaces, descLookup, 0, languageTag);
            }
        }
        if (status == AJ_OK) {
            while (childObj != NULL) {
                uint32_t len;
                /*
                 * Find immediate descendants
                 */
                const char* child = ChildPath(obj->path, childObj->path, &len);
                /*
                 * If there is a child check that this is the first instance of this child.
                 */
                if (child && (FirstInstance(obj->path, child, len) == childObj)) {
                    uint8_t descriptionAvailable = FALSE;
                    uint32_t descId = (childObjectIter.n - 1) << 24;
                    if (childObjectIter.l < AJ_MAX_OBJECT_LISTS) {
                        descLookup = descriptionLookups[childObjectIter.l];
                    } else {
                        descLookup = NULL;
                    }

                    if (unifiedFormat) {
                        descriptionAvailable = IsDescriptionAvailable(descLookup, descId);
                    } else {
                        description = GetDescription(descLookup, descId, languageTag);
                        descriptionAvailable = (description != NULL) ? TRUE : FALSE;
                    }
                    if (descriptionAvailable) {
                        XMLWriteTag(XMLWriter, context, nodeOpen, nameAttr, child, len, FALSE);
                        if (unifiedFormat) {
                            XMLWriteUnifiedDescriptions(XMLWriter, context, 0, descLookup, descId);
                        } else {
                            XMLWriteDescription(XMLWriter, context, 0, description, languageTag);
                        }
                        XMLWriter(context, nodeClose, 8);
                    } else {
                        XMLWriteTag(XMLWriter, context, nodeOpen, nameAttr, child, len, TRUE);
                    }
                }
                childObj = AJ_NextObject(&childObjectIter);
            }
            XMLWriter(context, nodeClose, 8);
        }
        if (status != AJ_OK) {
            AJ_ErrPrintf(("\nFailed to generate XML - check interface descriptions of %s for errors\n", obj->path));
        }
    }
    return status;
}

/*
 * Historically, Thin Client programs print the XML for their app objects as a
 * banner indicating they are alive.  For that reason we don't force users to
 * explicity turn on XML printing.  We just default it to on as long as we are
 * in a debug build.
 */
#ifdef AJ_DEBUG_BUILD

static void PrintXML(void* context, const char* str, uint32_t len)
{
    if (len) {
        while (len--) {
            AJ_AlwaysPrintf(("%c", *str++));
        }
    } else {
        AJ_AlwaysPrintf(("%s", str));
    }
}

void AJ_PrintXML(const AJ_Object* objs)
{
    AJ_PrintXMLWithDescriptions(objs, NULL); // without descriptions
}

void AJ_PrintXMLWithDescriptions(const AJ_Object* objs, const char* languageTag)
{
    AJ_Status status;

    while (objs && objs->path) {
        if (strcmp(objs->path, "/") == 0) {
            status = GenXML(PrintXML, NULL, NULL, objs, languageTag); // with descriptions in the given language
            if (status != AJ_OK) {
                AJ_ErrPrintf(("\nFailed to generate XML - check interface descriptions of %s for errors\n", objs->path));
            }
        } else {
            AJ_ObjectIterator iter;
            const AJ_Object* lookup;
            lookup = AJ_InitObjectIterator(&iter, AJ_OBJ_FLAGS_ALL_INCLUDE_MASK, AJ_OBJ_FLAGS_INTROSPECTABLE_EXCLUDE_MASK);
            while (lookup != NULL) {
                if (strcmp(lookup->path, objs->path) == 0) {
                    break;
                }
                lookup = AJ_NextObject(&iter);
            }
            if (lookup != NULL) {
                status = GenXML(PrintXML, NULL, &iter, NULL, languageTag); // with descriptions in the given language
                if (status != AJ_OK) {
                    AJ_ErrPrintf(("\nFailed to generate XML - check interface descriptions of %s for errors\n", objs->path));
                }
            } else {
                AJ_AlwaysPrintf(("Reminder: Object not yet added to the ObjectList, do not forget to call RegisterObjects\n"));
                status = GenXML(PrintXML, NULL, NULL, objs, languageTag);
            }
        }
        objs++;
    }
}
#endif

/*
 * Function to accumulate the length of the XML that will be generated
 */
void SizeXML(void* context, const char* str, uint32_t len)
{
    if (!len) {
        len = (uint32_t)strlen(str);
    }
    *((uint32_t*)context) += len;
}

typedef struct _WriteContext {
    AJ_Message* reply;
    uint32_t len;
    AJ_Status status;
} WriteContext;

void WriteXML(void* context, const char* str, uint32_t len)
{
    WriteContext* wctx = (WriteContext*)context;
    if (wctx->status == AJ_OK) {
        if (!len) {
            len = (uint32_t)strlen(str);
        }
        wctx->status = AJ_MarshalRaw(wctx->reply, str, len);
    }
}

AJ_Status AJ_HandleIntrospectRequestInternal(const AJ_Message* msg, AJ_Message* reply, const char* languageTag)
{
    AJ_Status status = AJ_OK;
    AJ_ObjectIterator objIter;
    const AJ_Object* obj = AJ_InitObjectIterator(&objIter, AJ_OBJ_FLAGS_ALL_INCLUDE_MASK, AJ_OBJ_FLAGS_INTROSPECTABLE_EXCLUDE_MASK);
    const AJ_Object virtualObject = { msg->objPath, NULL, 0, NULL };
    WriteContext context;
    uint32_t children = 0;

    /*
     * Find the requested object in the registered object lists
     */
    while (obj != NULL) {
        /*
         * Find out which object we are introspecting. There are two possibilities:
         *
         * - The request has a complete object path to one of the application objects.
         * - The request has a path to a parent object of one or more application objects where the
         *   parent itself is just a place-holder in the object hierarchy.
         */
        if (strcmp(msg->objPath, obj->path) == 0) {
            break;
        }

        if (0 != strcmp(msg->objPath, "/")) {
            if (ChildPath(msg->objPath, obj->path, NULL)) {
                ++children;
            }
        }
        /*
         * If there was not a direct match but the requested node has children we create
         * a temporary AJ_Object for the parent and introspect that object.
         */
        if (children) {
            obj = &virtualObject;
            break;
        }
        obj = AJ_NextObject(&objIter);
    }
    if (obj != NULL && obj->path != NULL) {
        /*
         * First pass computes the size of the XML string
         */
        context.len = 0;
        if (children > 0) {
            status = GenXML(SizeXML, &context.len, NULL, &virtualObject, languageTag);
        } else {
            status = GenXML(SizeXML, &context.len, &objIter, NULL, languageTag);
        }
        if (status != AJ_OK) {
            AJ_ErrPrintf(("AJ_HandleIntrospectRequest(): Failed to generate XML. status=%s", AJ_StatusText(status)));
            return status;
        }
        /*
         * Second pass marshals the XML
         */
        AJ_InfoPrintf(("AJ_HandleIntrospectRequest() %d bytes of XML\n", context.len));
        AJ_MarshalReplyMsg(msg, reply);
        /*
         * Do a partial delivery
         */
        status = AJ_DeliverMsgPartial(reply, context.len + 5);
        /*
         * Marshal the string length
         */
        if (status == AJ_OK) {
            status = AJ_MarshalRaw(reply, &context.len, 4);
        }
        if (status == AJ_OK) {
            uint8_t nul = 0;
            context.status = AJ_OK;
            context.reply = reply;

            if (children > 0) {
                GenXML(WriteXML, &context, NULL, &virtualObject, languageTag);
            } else {
                GenXML(WriteXML, &context, &objIter, NULL, languageTag);
            }
            status = context.status;
            if (status == AJ_OK) {
                /*
                 * Marshal the terminating NUL
                 */
                status = AJ_MarshalRaw(reply, &nul, 1);
            }
        }
    } else {
        /*
         * Return a ServiceUnknown error response
         */
        AJ_WarnPrintf(("AJ_HandleIntrospectRequest() NO MATCH for %s\n", msg->objPath));
        AJ_MarshalErrorMsg(msg, reply, AJ_ErrServiceUnknown);
    }
    return status;
}

AJ_Status AJ_HandleIntrospectRequest(const AJ_Message* msg, AJ_Message* reply, const char* languageTag)
{
    return AJ_HandleIntrospectRequestInternal(msg, reply, languageTag);
}

AJ_Status AJ_GetIntrospectionData(const AJ_Message* msg, AJ_Message* reply)
{
    return AJ_HandleIntrospectRequestInternal(msg, reply, NULL);
}

AJ_Status AJ_HandleGetDescriptionLanguages(const AJ_Message* msg, AJ_Message* reply) {
    AJ_Status status = AJ_OK;
    AJ_Arg languageListArray;
    const char* const* languageTag;

    status = AJ_MarshalReplyMsg(msg, reply);

    if (status == AJ_OK) {
        status = AJ_MarshalContainer(reply, &languageListArray, AJ_ARG_ARRAY);
    }
    if (status == AJ_OK) {
        languageTag = languageList;

        while ((NULL != *languageTag) && status == AJ_OK) {
            status = AJ_MarshalArgs(reply, "s", *languageTag);
            languageTag++;
        }
    }
    if (status == AJ_OK) {
        status = AJ_MarshalCloseContainer(reply, &languageListArray);
    }

    return status;
}

/*
 * Check that the signature in the message matches the encoded signature in the string
 */
static AJ_Status CheckSignature(const char* encoding, const AJ_Message* msg)
{
    const char* sig = msg->signature ? msg->signature : "";
    char direction = (msg->hdr->msgType == AJ_MSG_METHOD_CALL) ? IN_ARG : OUT_ARG;

    /*
     * Wild card in the message is ok.
     */
    if (*sig == '*') {
        return AJ_OK;
    }
    while (*encoding) {
        /*
         * Skip until we find a direction character
         */
        while (*encoding && (*encoding++ != direction)) {
        }
        /*
         * Match a single arg to the signature
         */
        while (*encoding && (*sig == *encoding)) {
            ++sig;
            ++encoding;
        }
        if (*encoding && (*encoding != ' ')) {
            AJ_ErrPrintf(("CheckSignature(): AJ_ERR_SIGNATURE\n"));
            return AJ_ERR_SIGNATURE;
        }
    }
    /*
     * On a match we should have consumed both strings
     */
    return (*encoding == *sig) ? AJ_OK : AJ_ERR_SIGNATURE;
}

/*
 * Composes a signature from the member encoding from an interface description.
 */
static AJ_Status ComposeSignature(const char* encoding, char direction, char* sig, size_t len)
{
    while (*encoding) {
        /*
         * Skip until we find a direction character
         */
        while (*encoding && (*encoding++ != direction)) {
        }
        /*
         * Match a single arg to the signature
         */
        while (*encoding && (*encoding != ' ')) {
            if (--len == 0) {
                AJ_ErrPrintf(("ComposeSignature(): AJ_ERR_RESOURCES\n"));
                return AJ_ERR_RESOURCES;
            }
            *sig++ = *encoding++;
        }
    }
    *sig = '\0';
    return AJ_OK;
}

static AJ_Status MatchProp(const char* member, const char* prop, uint8_t op, const char** sig)
{
    AJ_Status status;
    const char* encoding = member;

    if (*encoding++ != '@') {
        AJ_InfoPrintf(("MatchProp(): AJ_ERR_NO_MATCH\n"));
        return AJ_ERR_NO_MATCH;
    }
    while (*prop) {
        if (*encoding++ != *prop++) {
            AJ_InfoPrintf(("MatchProp(): AJ_ERR_NO_MATCH\n"));
            return AJ_ERR_NO_MATCH;
        }
    }

    switch (*encoding) {
    case WRITE_ONLY:
        status = (AJ_PROP_SET == op) ? AJ_OK : AJ_ERR_DISALLOWED;
        break;

    case READ_ONLY:
        status = (AJ_PROP_GET == op) ? AJ_OK : AJ_ERR_DISALLOWED;
        break;

    case READ_WRITE:
        status = AJ_OK;
        break;

    default:
        AJ_InfoPrintf(("MatchProp(): AJ_ERR_NO_MATCH - incorrect annotation or substring match\n"));
        status = AJ_ERR_NO_MATCH;
        break;
    }
    if (AJ_OK == status) {
        *sig = ++encoding;
    }

    return status;
}

static uint32_t MatchMember(const char* encoding, const AJ_Message* msg)
{
    const char* member = msg->member;
    uint8_t memberType = (msg->hdr->msgType == AJ_MSG_METHOD_CALL) ? METHOD : SIGNAL;
    if (MEMBER_TYPE(*encoding++) != memberType) {
        return FALSE;
    }
    if (memberType == SIGNAL && IS_SESSIONLESS(*encoding)) {
        /*
         * Advance so that we do not return a '&' character
         */
        encoding++;
    }
    while (*member) {
        if (*encoding++ != *member++) {
            return FALSE;
        }
    }
    return (*encoding == '\0') || (*encoding == ' ');
}

static AJ_InterfaceDescription FindInterface(const AJ_InterfaceDescription* interfaces, const char* iface, uint8_t* idx)
{
    *idx = 0;
    if (interfaces) {
        while (*interfaces) {
            AJ_InterfaceDescription desc = *interfaces++;
            const char* intfName = *desc;

            if (desc) {
                /*
                 * Skip security specifier when comparing the interface name
                 */
                if ((*intfName == SECURE_TRUE) || (*intfName == SECURE_OFF)) {
                    ++intfName;
                }
                if (strcmp(intfName, iface) == 0) {
                    return desc;
                }
            }
            *idx += 1;
        }
    }
    return NULL;
}

/*
 * Match the object path. There are two wild card entries for object paths: '?'  matches any method
 * call and '!' matches any signal.  The method call wildcard is specifically to support the
 * introspection and ping methods. The signal wildcard allows for a single handler for a specific
 * signal emitted by any object. Note that these wildcards are expected to provide unique matches,
 * i.e. there should be no non-wildcarded entry in any object table that would also match.
 */
static uint8_t inline MatchPath(const char* path, AJ_Message* msg) {
    if ((*path == '?') && (msg->hdr->msgType == AJ_MSG_METHOD_CALL)) {
        return TRUE;
    }
    if ((*path == '!') && (msg->hdr->msgType == AJ_MSG_SIGNAL)) {
        return TRUE;
    }
    return strcmp(path, msg->objPath) == 0;
}

AJ_Status AJ_LookupMessageId(AJ_Message* msg, uint8_t* secure)
{
    uint8_t oIndex = 0;

    for (oIndex = 0; oIndex < ArraySize(objectLists); ++oIndex) {
        uint8_t pIndex = 0;
        const AJ_Object* obj = objectLists[oIndex];
        if (!obj) {
            continue;
        }
        for (; obj->path; ++pIndex, ++obj) {
            /*
             * Skip objects that are currently disabled
             */
            if (obj->flags & AJ_OBJ_FLAG_DISABLED) {
                continue;
            }
            if (MatchPath(obj->path, msg)) {
                uint8_t iIndex;
                AJ_InterfaceDescription desc = FindInterface(obj->interfaces, msg->iface, &iIndex);
                if (desc) {
                    uint8_t mIndex = 0;
                    *secure = SecurityApplies(*desc, obj);
                    /*
                     * Skip the interface name and iterate over the members of the interface
                     */
                    while (*(++desc)) {
                        if (MatchMember(*desc, msg)) {
                            msg->msgId = (oIndex << 24) | (pIndex << 16) | (iIndex << 8) | mIndex;
                            AJ_InfoPrintf(("Identified message %x\n", msg->msgId));
                            return CheckSignature(*desc, msg);
                        }
                        ++mIndex;
                    }
                }
            }
        }
    }
    AJ_ErrPrintf(("LookupMessageId(): AJ_ERR_NO_MATCH\n"));
    return AJ_ERR_NO_MATCH;
}

/*
 * Validates an index into a NULL terminated array
 */
static uint8_t CheckIndex(const void* ptr, uint8_t idx, size_t stride)
{
    if (!ptr) {
        return FALSE;
    }
    do {
        if (*((void**)ptr) == NULL) {
            AJ_ErrPrintf(("\n!!!Invalid msg identifier indicates programming error!!!\n"));
            return FALSE;
        }
        ptr = (((uint8_t*)ptr) + stride);
    } while (idx--);
    return TRUE;
}

static AJ_Status UnpackMsgId(uint32_t msgId, const char** objPath, const char** iface, const char** member, uint8_t* secure)
{
    uint8_t oIndex = (msgId >> 24);
    uint8_t pIndex = (msgId >> 16);
    uint8_t iIndex = (msgId >> 8);
    uint8_t mIndex = (uint8_t)(msgId) + 1;
    const AJ_Object* obj;
    AJ_InterfaceDescription ifc;

    if ((oIndex >= ArraySize(objectLists)) || !CheckIndex(objectLists[oIndex], pIndex, sizeof(AJ_Object))) {
        AJ_ErrPrintf(("UnpackMsgId(): AJ_ERR_INVALID\n"));
        return AJ_ERR_INVALID;
    }
    obj = &objectLists[oIndex][pIndex];
    if (!CheckIndex(obj->interfaces, iIndex, sizeof(AJ_InterfaceDescription))) {
        AJ_ErrPrintf(("UnpackMsgId(): AJ_ERR_INVALID\n"));
        return AJ_ERR_INVALID;
    }
    ifc = obj->interfaces[iIndex];
    if (!CheckIndex(ifc, mIndex, sizeof(AJ_InterfaceDescription))) {
        AJ_ErrPrintf(("UnpackMsgId(): AJ_ERR_INVALID\n"));
        return AJ_ERR_INVALID;
    }

    if (obj->flags & AJ_OBJ_FLAG_DISABLED) {
        return AJ_ERR_INVALID;
    }
    if (objPath) {
        *objPath = obj->path;
    }
    *secure = SecurityApplies(*ifc, obj);
    if (iface) {
        /*
         * Skip over security specifier if there is one
         */
        if ((ifc[0][0] == SECURE_TRUE) || (ifc[0][0] == SECURE_OFF)) {
            *iface = *ifc + 1;
        } else {
            *iface = *ifc;
        }
    }
    if (member) {
        /*
         * Skip over sessionless signal specifier if there is one
         */
        if (MEMBER_TYPE(ifc[mIndex][0]) == SIGNAL && IS_SESSIONLESS(ifc[mIndex][1])) {
            *member = ifc[mIndex] + 2;
        } else {
            *member = ifc[mIndex] + 1;
        }
    }
    return AJ_OK;
}

AJ_Status AJ_MarshalPropertyArgs(AJ_Message* msg, uint32_t propId)
{
    AJ_Status status;
    const char* iface;
    const char* prop;
    uint8_t secure;
    size_t pos;
    AJ_Arg arg;

    status = UnpackMsgId(propId, NULL, &iface, &prop, &secure);
    if (status != AJ_OK) {
        AJ_ErrPrintf(("AJ_MarhsalPropertyArgs(): status=%s\n", AJ_StatusText(status)));
        return status;
    }
    if (secure) {
        msg->hdr->flags |= AJ_FLAG_ENCRYPTED;
    }
    if (msg->hdr->flags & AJ_FLAG_ENCRYPTED) {
        /*
         * Check outgoing access policy
         */
        status = AJ_AccessControlCheckProperty(msg, propId, msg->destination, AJ_ACCESS_OUTGOING);
        if (AJ_OK != status) {
            return status;
        }
    }

    /*
     * Marshal interface name
     */
    AJ_MarshalArgs(msg, "s", iface);
    /*
     * Marshal property name
     */
    pos = AJ_StringFindFirstOf(prop, "<=>");
    AJ_InitArg(&arg, AJ_ARG_STRING, 0, prop, pos);
    status = AJ_MarshalArg(msg, &arg);
    /*
     * If setting a property handle the variant setup
     */
    if ((status == AJ_OK) && ((msg->msgId & 0xFF) == AJ_PROP_SET)) {
        char sig[16];
        ComposeSignature(prop, prop[pos], sig, sizeof(sig));
        status = AJ_MarshalVariant(msg, sig);
    }
    return status;
}

/*
 * Hook for unit tests
 */
#if defined(GTEST_ENABLED) || defined(AJ_DEBUG_BUILD)
AJ_MutterHook MutterHook = NULL;
#endif

AJ_Status AJ_InitMessageFromMsgId(AJ_Message* msg, uint32_t msgId, uint8_t msgType, uint8_t* secure)
{
    /*
     * Static buffer for holding the signature for the message currently being marshaled. Since this
     * implementation can only marshal one message at a time we only need one of these buffers. The
     * size of the buffer dictates the maximum size signature we can marshal. The wire protocol
     * allows up to 255 characters in a signature but that would represent an outgrageously complex
     * message argument list.
     */
    static char msgSignature[64];
    AJ_Status status = AJ_OK;

#if defined(GTEST_ENABLED) || defined(AJ_DEBUG_BUILD)
    if (MutterHook) {
        return MutterHook(msg, msgId, msgType);
    }
#endif
    if (msgType == AJ_MSG_ERROR) {
        /*
         * The only thing to initialize for errors is the msgId
         */
        msg->msgId = AJ_REPLY_ID(msgId);
        /*
         * Get the secure flag if we have a valid message id this will ensure that the error
         * response is encrypted if that is what is expected.
         */
        if (msg->msgId != AJ_INVALID_MSG_ID) {
            status = UnpackMsgId(msgId, NULL, NULL, NULL, secure);
        }
    } else {
        const char* member = NULL;
        char direction = (msgType == AJ_MSG_METHOD_CALL) ? IN_ARG : OUT_ARG;
        /*
         * The rest is just indexing into the object and interface descriptions.
         */
        if (msgType == AJ_MSG_METHOD_RET) {
            msg->msgId = AJ_REPLY_ID(msgId);
            status = UnpackMsgId(msgId, NULL, NULL, &member, secure);
        } else {
            msg->msgId = msgId;
            status = UnpackMsgId(msgId, &msg->objPath, &msg->iface, &member, secure);
            if (status == AJ_OK) {
                /*
                 * Validate the object path
                 */
                if (!msg->objPath) {
                    status = AJ_ERR_OBJECT_PATH;
                } else if (*msg->objPath == '?') {
                    /*
                     * The wildcard object path is a special for case methods implemented by all
                     * objects. In the message header need a valid object path, it doesn't matter
                     * which one by definition so use the root object because it is always valid.
                     */
                    msg->objPath = "/";
                } else if (*msg->objPath != '/') {
                    status = AJ_ERR_OBJECT_PATH;
                }
                msg->member = member;
            }
        }
        /*
         * Compose the signature from information in the member encoding.
         */
        if (status == AJ_OK) {
            status = ComposeSignature(member, direction, msgSignature, sizeof(msgSignature));
            if (status == AJ_OK) {
                msg->signature = msgSignature;
            }
        }
    }

    return status;
}

static AJ_Status CheckReturnSignature(AJ_Message* msg, uint32_t msgId)
{
    AJ_Status status;
    uint8_t secure = FALSE;
    const char* member;

    status = UnpackMsgId(msgId, NULL, NULL, &member, &secure);
    if (status != AJ_OK) {
        AJ_ErrPrintf(("CheckReturnSignature(): status=%s\n", AJ_StatusText(status)));
        return status;
    }
    /*
     * Check that if the interface was flagged secure that the reply was encrypted
     */
    if (secure && !(msg->hdr->flags & AJ_FLAG_ENCRYPTED)) {
        return AJ_ERR_SECURITY;
    }
    /*
     * Nothing to check for error messages
     */
    if (msg->hdr->msgType != AJ_MSG_ERROR) {
        status = CheckSignature(member, msg);
    }
    if (status == AJ_OK) {
        msg->msgId = AJ_REPLY_ID(msgId);
    }
    return status;
}

static ReplyContext* FindReplyContext(uint32_t serial) {
    size_t i;
    for (i = 0; i < ArraySize(replyContexts); ++i) {
        if (replyContexts[i].serial == serial) {
            return &replyContexts[i];
        }
    }
    return NULL;
}

AJ_Status AJ_IdentifyProperty(AJ_Message* msg, const char* iface, const char* prop, uint32_t* propId, const char** sigPtr, uint8_t* secure)
{
    AJ_Status status = AJ_OK;
    uint8_t oIndex = (msg->msgId >> 24);
    uint8_t pIndex = (msg->msgId >> 16);
    uint8_t iIndex;
    const AJ_Object* obj;
    AJ_InterfaceDescription desc;

#ifdef AJ_DEBUG_BUILD
    if ((oIndex >= ArraySize(objectLists)) || !CheckIndex(objectLists[oIndex], pIndex, sizeof(AJ_Object))) {
        status = AJ_ERR_INVALID;
        AJ_ErrPrintf(("AJ_IdentifyProperty(): %s\n", AJ_StatusText(status)));
        return status;
    }
#endif
    obj = &objectLists[oIndex][pIndex];

    *propId = AJ_INVALID_PROP_ID;

    desc = FindInterface(obj->interfaces, iface, &iIndex);
    if (desc) {
        uint8_t mIndex = 0;
        /*
         * Security is based on the interface the property is defined on.
         */
        *secure = SecurityApplies(*desc, obj);
        if (*secure == FALSE) {
            /*
             * For Properties interface security is based on the interface supplied
             */
            if (strcmp(*desc, AJ_PropertiesIface[0]) == 0) {
                const char* actualIface;
                status = AJ_UnmarshalArgs(msg, "s", &actualIface);
                if (status == AJ_OK) {
                    *secure = SecurityApplies(actualIface, obj);
                    status = AJ_ResetArgs(msg);
                }
            }
            if (status != AJ_OK) {
                AJ_ErrPrintf(("AJ_IdentifyProperty(): %s\n", AJ_StatusText(status)));
                return status;
            }
        }
        /*
         * Iterate over the interface members to locate the property that is being accessed.
         */
        while (*(++desc)) {
            status = MatchProp(*desc, prop, msg->msgId & 0xFF, sigPtr);
            if (status != AJ_ERR_NO_MATCH) {
                if (status == AJ_OK) {
                    *propId = (oIndex << 24) | (pIndex << 16) | (iIndex << 8) | mIndex;
                    AJ_InfoPrintf(("Identified property %s:%s id=%x sig=\"%s\"\n", iface, prop, *propId, *sigPtr));
                }
                break;
            }
            ++mIndex;
        }
    }
    return status;
}

AJ_Status AJ_UnmarshalPropertyArgs(AJ_Message* msg, uint32_t* propId, const char** sig)
{
    AJ_Status status;
    uint8_t secure = FALSE;
    char* iface;
    char* prop;

    status = AJ_UnmarshalArgs(msg, "ss", &iface, &prop);
    if (status == AJ_OK) {
        status = AJ_IdentifyProperty(msg, iface, prop, propId, sig, &secure);
        /*
         * If the interface is secure check the message must be encrypted
         */
        if ((status == AJ_OK) && secure) {
            if (!(msg->hdr->flags & AJ_FLAG_ENCRYPTED)) {
                AJ_WarnPrintf(("Security violation accessing property\n"));
                return AJ_ERR_SECURITY;
            }
            /*
             * Check incoming access policy
             */
            status = AJ_AccessControlCheckProperty(msg, *propId, msg->sender, AJ_ACCESS_INCOMING);
        }
    }
    return status;
}

AJ_Status AJ_MarshalAllPropertiesArgs(AJ_Message* replyMsg, const char* iface, AJ_BusPropGetCallback callback, void* context)
{
    AJ_Status status = AJ_ERR_MARSHAL;
    uint8_t oIndex = (replyMsg->msgId >> 24) & ~AJ_REP_ID_FLAG;
    uint8_t pIndex = replyMsg->msgId >> 16;
    uint8_t iIndex;
    const AJ_Object* obj = &objectLists[oIndex][pIndex];
    uint8_t secure = SecurityApplies(iface, obj);
    AJ_InterfaceDescription desc;
    AJ_Arg array;

    /*
     * If the interface is secure check the message must be encrypted
     */
    if (secure && !(replyMsg->hdr->flags & AJ_FLAG_ENCRYPTED)) {
        status = AJ_ERR_SECURITY;
        AJ_WarnPrintf(("Security violation accessing property\n"));
        goto Exit;
    }

    desc = FindInterface(obj->interfaces, iface, &iIndex);
    if (desc != NULL) {
        uint8_t mIndex = 0;

        status = AJ_MarshalContainer(replyMsg, &array, AJ_ARG_ARRAY);
        if (status != AJ_OK) {
            goto Exit;
        }

        /*
         * Iterate over the interface members to locate the properties that are being accessed.
         */
        while (*(++desc) != NULL) {
            const char* prop = *desc;
            size_t pos;
            AJ_Arg dict;
            AJ_Arg key;
            uint32_t propId = AJ_ENCODE_PROPERTY_ID(oIndex, pIndex, iIndex, mIndex++);

            /*
             * Consume member type in member definition and skip member if not a Property
             */
            if (MEMBER_TYPE(*(prop++)) != PROPERTY) {
                continue;
            }

            /*
             * Skip Write Only properties
             */
            pos = AJ_StringFindFirstOf(prop, "<=>");
            if ((pos > 1) && (prop[pos - 1] == WRITE_ONLY)) {
                continue;
            }

            if (secure) {
                /*
                 * Check incoming access policy
                 */
                status = AJ_AccessControlCheckProperty(replyMsg, propId, replyMsg->destination, AJ_ACCESS_INCOMING);
                if (AJ_OK != status) {
                    /* Skip this property */
                    continue;
                }
            }

            status = AJ_MarshalContainer(replyMsg, &dict, AJ_ARG_DICT_ENTRY);
            if (status != AJ_OK) {
                goto Exit;
            }

            /*
             * Marshal property name
             */
            AJ_InitArg(&key, AJ_ARG_STRING, 0, prop, pos);
            status = AJ_MarshalArg(replyMsg, &key);
            /*
             * Marshal property value as Variant setting up the signature
             */
            if (status == AJ_OK) {
                char sig[16];
                ComposeSignature(prop, prop[pos], sig, sizeof(sig));
                status = AJ_MarshalVariant(replyMsg, sig);
            }
            /*
             * Marshal property value argument
             */
            if ((status == AJ_OK) && (callback != NULL)) {
                status = callback(replyMsg, propId, context);
            }

            if (status == AJ_OK) {
                status = AJ_MarshalCloseContainer(replyMsg, &dict);
            }

            if (status != AJ_OK) {
                goto Exit;
            }
        }

        status = AJ_MarshalCloseContainer(replyMsg, &array);
    }

Exit:
    return status;
}

AJ_Status AJ_IdentifyMessage(AJ_Message* msg)
{
    AJ_Status status = AJ_ERR_NO_MATCH;
    uint8_t secure = FALSE;
#if defined(GTEST_ENABLED) || defined(AJ_DEBUG_BUILD)
    if (MutterHook) {
        return AJ_OK;
    }
#endif
    msg->msgId = AJ_INVALID_MSG_ID;
    if ((msg->hdr->msgType == AJ_MSG_METHOD_CALL) || (msg->hdr->msgType == AJ_MSG_SIGNAL)) {
        /*
         * Methods and signals
         */
        status = AJ_LookupMessageId(msg, &secure);
        if ((status == AJ_OK) && secure && !(msg->hdr->flags & AJ_FLAG_ENCRYPTED)) {
            AJ_ErrPrintf(("AJ_IdentifyMessage(): AJ_ERR_SECURITY\n"));
            status = AJ_ERR_SECURITY;
        }
        /*
         * Generate an error response for an invalid method call rather than reporting an invalid
         * message id to the application.
         */
        if ((status != AJ_OK) && (msg->hdr->msgType == AJ_MSG_METHOD_CALL) && !(msg->hdr->flags & AJ_FLAG_NO_REPLY_EXPECTED)) {
            AJ_Message reply;
            AJ_Status resStatus = AJ_OK;

            AJ_DumpMsg("Rejecting unidentified method call", msg, FALSE);
            AJ_MarshalStatusMsg(msg, &reply, status);
            resStatus = AJ_DeliverMsg(&reply);
            if (AJ_OK != resStatus) {
                AJ_ErrPrintf(("AJ_IdentifyMessage(): failed to send error response by AJ_DeliverMsg() %s\n", AJ_StatusText(resStatus)));
            }
            /*
             * Cleanup the message we are ignoring.
             */
            AJ_CloseMsg(msg);
        }
    } else {
        ReplyContext* repCtx = FindReplyContext(msg->replySerial);
        if (repCtx) {
            status = CheckReturnSignature(msg, repCtx->messageId);

            if (AJ_OK == status && (msg->hdr->flags & AJ_FLAG_ENCRYPTED)) {
                /* Make sure the authenticated sender was the expected recipient
                 * of the method call.
                 */
                if (0 != strncmp(msg->sender, repCtx->uniqueName, AJ_MAX_NAME_SIZE)) {
                    status = AJ_ERR_NO_MATCH;
                }
            }

            /*
             * Release the reply context
             */
            repCtx->serial = 0;
        }
    }
    return status;
}

void AJ_RegisterObjects(const AJ_Object* localObjects, const AJ_Object* proxyObjects)
{
    AJ_ASSERT(AJ_PRX_ID_FLAG < ArraySize(objectLists));
    objectLists[AJ_APP_ID_FLAG] = localObjects;
    objectLists[AJ_PRX_ID_FLAG] = proxyObjects;
}

AJ_Status AJ_RegisterObjectsACL()
{
    AJ_Status status;

    int i;
    for (i = 0; i < AJ_MAX_OBJECT_LISTS; i++) {
        status = AJ_AuthorisationRegister(objectLists[i], i);
        if (AJ_OK != status) {
            AJ_WarnPrintf(("AJ_RegisterObjectsACL(): index(%d) %s\n", i, AJ_StatusText(status)));
            return status;
        }
    }
    return AJ_OK;
}

void AJ_RegisterDescriptionLanguages(const char* const* languages) {
    languageList = languages;
}

AJ_Status AJ_RegisterObjectListWithDescriptions(const AJ_Object* objList, uint8_t idx, AJ_DescriptionLookupFunc descLookup)
{
    if (idx >= ArraySize(objectLists)) {
        return AJ_ERR_RANGE;
    }
    objectLists[idx] = objList;
    descriptionLookups[idx] = descLookup;
    return AJ_AuthorisationRegister(objList, idx);
}

AJ_Status AJ_RegisterObjectList(const AJ_Object* objList, uint8_t idx)
{
    return AJ_RegisterObjectListWithDescriptions(objList, idx, NULL);
}

AJ_Status AJ_SetProxyObjectPath(AJ_Object* proxyObjects, uint32_t msgId, const char* objPath)
{
    int i;
    uint8_t oIndex = (msgId >> 24);
    uint8_t pIndex = (msgId >> 16);

    if ((oIndex != AJ_PRX_ID_FLAG) || (proxyObjects != objectLists[oIndex])) {
        AJ_ErrPrintf(("AJ_SetProxyObjectPath(): AJ_ERR_UNKNOWN\n"));
        return AJ_ERR_UNKNOWN;
    }
    for (i = 0; i < pIndex; ++i) {
        if (!proxyObjects[i].interfaces) {
            AJ_ErrPrintf(("AJ_SetProxyObjectPath(): AJ_ERR_UNKNOWN\n"));
            return AJ_ERR_UNKNOWN;
        }
    }
    proxyObjects[pIndex].path = objPath;
    return AJ_OK;
}

AJ_Status AJ_AllocReplyContext(AJ_Message* msg, uint32_t timeout)
{
    if (msg->hdr->flags & AJ_FLAG_NO_REPLY_EXPECTED) {
        /*
         * Not expecting a reply so don't allocate a reply context
         */
        return AJ_OK;
    } else {
        ReplyContext* repCtx = FindReplyContext(0);

        AJ_ASSERT(msg->hdr->msgType == AJ_MSG_METHOD_CALL);

        if (repCtx) {
            AJ_Status status;
            const char* unique;

            repCtx->serial = msg->hdr->serialNum;
            repCtx->messageId = msg->msgId;
            repCtx->timeout = timeout ? timeout : AJ_DEFAULT_REPLY_TIMEOUT;
            AJ_InitTimer(&repCtx->callTime);

            status = AJ_GetRemoteUniqueName(msg->destination, &unique);
            if (AJ_OK == status) {
                strncpy(repCtx->uniqueName, unique, AJ_MAX_NAME_SIZE);
                repCtx->uniqueName[AJ_MAX_NAME_SIZE] = '\0';
            } else {
                /* Lookup failed, but that doesn't matter for unencrypted messages */
                repCtx->uniqueName[0] = '\0';
            }
            return AJ_OK;
        } else {
            AJ_ErrPrintf(("AJ_AllocReplyContext(): Failed to allocate reply context.  status=AJ_ERR_RESOURCES\n"));
            return AJ_ERR_RESOURCES;
        }
    }
}

void AJ_ReleaseReplyContext(AJ_Message* msg)
{
    if (msg->hdr->msgType == AJ_MSG_METHOD_CALL) {
        ReplyContext* repCtx = FindReplyContext(msg->hdr->serialNum);
        if (repCtx) {
            repCtx->serial = 0;
        }
    }
}

uint8_t AJ_TimedOutMethodCall(AJ_Message* msg)
{
    ReplyContext* repCtx = replyContexts;
    size_t i;
    for (i = 0; i < ArraySize(replyContexts); ++i, ++repCtx) {
        if (repCtx->serial && (AJ_GetElapsedTime(&repCtx->callTime, TRUE) > repCtx->timeout)) {
            /*
             * Set the reply serial and message id for the timeout error
             */
            msg->replySerial = repCtx->serial;
            msg->msgId = AJ_REPLY_ID(repCtx->messageId);
            /*
             * Release the reply context
             */
            repCtx->serial = 0;
            return TRUE;
        }
    }
    return FALSE;
}

void AJ_ReleaseReplyContexts(void)
{
    memset(replyContexts, 0, sizeof(replyContexts));
}

AJ_Status AJ_SetObjectFlags(const char* objPath, uint8_t setFlags, uint8_t clearFlags)
{
    AJ_Status status = AJ_ERR_NO_MATCH;
    AJ_Object* list = (AJ_Object*)objectLists[AJ_APP_ID_FLAG];
    uint32_t secure = FALSE;

    if (list && objPath) {
        /*
         * Set flags on the object and all its children
         */
        while (list->path) {
            if ((strcmp(objPath, list->path) == 0) || ChildPath(objPath, list->path, NULL)) {
                list->flags &= ~clearFlags;
                list->flags |= setFlags;
                AJ_InfoPrintf(("Setting flags for %s to 0x%02x\n", list->path, list->flags));
                status = AJ_OK;
                if (AJ_OBJ_FLAG_SECURE & setFlags) {
                    secure = TRUE;
                }
            }
            ++list;
        }
    }
    if (secure) {
        /* Object became secure, register with the ACL */
        status = AJ_AuthorisationRegister(objectLists[AJ_APP_ID_FLAG], AJ_APP_ID_FLAG);
    }
    return status;
}

AJ_MemberType AJ_GetMemberType(uint32_t identifier, const char** member, uint8_t* isSecure)
{
    const char* name;
    uint8_t secure;
    AJ_Status status = UnpackMsgId(identifier, NULL, NULL, &name, &secure);
    if (status == AJ_OK) {
        if (isSecure) {
            *isSecure = secure;
        }
        if (member) {
            *member = name;
        }
        return (AJ_MemberType) * (name - 1);
    } else {
        return AJ_INVALID_MEMBER;
    }
}

const AJ_Object* AJ_InitObjectIterator(AJ_ObjectIterator* iter, uint8_t inFlags, uint8_t exFlags)
{
    iter->fin = inFlags;
    iter->fex = exFlags;
    iter->l = 0;
    iter->n = 0;
    return AJ_NextObject(iter);
}

const AJ_Object* AJ_NextObject(AJ_ObjectIterator* iter)
{
    while (iter->l < ArraySize(objectLists)) {
        const AJ_Object* list = objectLists[iter->l];

        if (list) {
            while (list[iter->n].path) {

                uint8_t objFlags = 0;
                const AJ_Object* obj = &list[iter->n++];

                /* Skip announcing the DeviceIcon interface unless it has been set explicitly */
                if ((iter->l == AJ_BUS_ID_FLAG) && (0 == strcmp(obj->path, "/About/DeviceIcon"))) {
                    if (!AJ_AboutHasIcon()) {
                        continue;
                    }
                }
                objFlags = obj->flags;

                /*
                 * For backwards compatibility the third entry is reserved for proxy objects. Going forward
                 * proxy objects are identified in the object list by the AJ_OBJ_FLAG_IS_PROXY.
                 */
                if (iter->l == AJ_PRX_ID_FLAG) {
                    objFlags &= AJ_OBJ_FLAG_IS_PROXY;
                }
                if ((objFlags & iter->fin || (objFlags == 0 && iter->fin == AJ_OBJ_FLAGS_ALL_INCLUDE_MASK)) && !(objFlags & iter->fex)) {
                    return obj;
                }
            }
        }
        iter->n = 0;
        ++iter->l;
    }
    return NULL;
}
