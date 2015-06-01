/**
 * @file
 */
/******************************************************************************
 * Copyright AllSeen Alliance. All rights reserved.
 *
 *    Permission to use, copy, modify, and/or distribute this software for any
 *    purpose with or without fee is hereby granted, provided that the above
 *    copyright notice and this permission notice appear in all copies.
 *
 *    THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 *    WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 *    MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 *    ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 *    WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 *    ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 *    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 ******************************************************************************/

/**
 * Per-module definition of the current module for debug logging.  Must be defined
 * prior to first inclusion of aj_debug.h
 */
#define AJ_MODULE AUTHORISATION

#include "aj_target.h"
#include "aj_authorisation.h"
#include "aj_std.h"
#include "aj_debug.h"
#include "aj_peer.h"
#include "aj_crypto_ecc.h"
#include "aj_guid.h"
#include "aj_cert.h"
#include "aj_config.h"
#include "aj_crypto.h"
#include "aj_security.h"

/**
 * Turn on per-module debug printing by setting this variable to non-zero value
 * (usually in debugger).
 */
#ifndef NDEBUG
uint8_t dbgAUTHORISATION = 0;
#endif

#define POLICY_SPECIFICATION_VERSION   1

/*
 * Policy helper struct
 * Containes a buffer of the raw marshalled data,
 * and an AJ_Policy struct referencing inside the buffer.
 */
typedef struct _Policy {
    AJ_CredField buffer;
    AJ_Policy* policy;
} Policy;

#define ACCESS_POLICY                  0
#define ACCESS_MANIFEST                1
#define ACCESS_INCOMING_DENY        0x01
#define ACCESS_OUTGOING_DENY        0x02
#define ACCESS_INCOMING_ALLOW       0x04
#define ACCESS_OUTGOING_ALLOW       0x08
/*
 * The main access control structure.
 * Maps message ids to peer's access.
 */
typedef struct _AccessControlMember {
    uint32_t id;
    const char* obj;
    const char* ifn;
    const char* mbr;
    uint8_t access[AJ_NAME_MAP_GUID_SIZE];
    struct _AccessControlMember* next;
} AccessControlMember;

static AJ_Manifest* g_manifest = NULL;
static AccessControlMember* g_access = NULL;

static void AccessControlClose()
{
    AccessControlMember* member;

    while (g_access) {
        member = g_access;
        g_access = g_access->next;
        AJ_Free(member);
    }
}

/*
 * Iterates through all object/interface descriptions
 * and saves the names and ids for each secure member.
 * The object, interface and member names are used when
 * applying policy.
 * The member id is used when applying access control
 * for each incoming or outgoing message.
 */
static AJ_Status AccessControlInit()
{
    AJ_ObjectIterator iter;
    const AJ_Object* obj;
    const AJ_InterfaceDescription* interfaces;
    AJ_InterfaceDescription iface;
    const char* ifn;
    const char* mbr;
    uint8_t secure;
    uint8_t i, m;
    AccessControlMember* member;

    AJ_InfoPrintf(("AJ_AccessControlInit()\n"));

    for (obj = AJ_InitObjectIterator(&iter, AJ_OBJ_FLAGS_ALL_INCLUDE_MASK, 0); obj != NULL; obj = AJ_NextObject(&iter)) {
        secure = obj->flags & AJ_OBJ_FLAG_SECURE;
        interfaces = obj->interfaces;
        if (!interfaces) {
            continue;
        }
        i = 0;
        while (*interfaces) {
            iface = *interfaces++;
            ifn = *iface++;
            AJ_ASSERT(ifn);
            secure |= (SECURE_TRUE == *ifn);
            /* Only access control secure objects/interfaces */
            if (secure) {
                m = 0;
                while (*iface) {
                    mbr = *iface++;
                    member = (AccessControlMember*) AJ_Malloc(sizeof (AccessControlMember));
                    if (NULL == member) {
                        goto Exit;
                    }
                    memset(member, 0, sizeof (AccessControlMember));
                    member->obj = obj->path;
                    member->ifn = ifn;
                    member->mbr = mbr;
                    member->id = AJ_ENCODE_MESSAGE_ID(iter.l, iter.n - 1, i, m);
                    member->next = g_access;
                    g_access = member;
                    AJ_InfoPrintf(("AJ_AccessControlInit(): id 0x%08X obj %s ifn %s mbr %s\n", member->id, obj->path, ifn, mbr));
                    m++;
                }
            }
            i++;
        }
    }

    return AJ_OK;

Exit:
    AccessControlClose();
    return AJ_ERR_RESOURCES;
}

/*
 * For a given message id, checks the access control
 * table for that peer.
 * Incoming and outgoing messages have their own entry.
 */
AJ_Status AJ_AccessControlCheck(uint32_t id, const char* name, uint8_t direction)
{
    AJ_Status status;
    AccessControlMember* mbr;
    uint32_t peer;
    uint8_t access;

    AJ_InfoPrintf(("AJ_AccessControlCheck(id=0x%08X, name=%s, direction=%x)\n", id, name, direction));

    if (!g_access) {
        AJ_WarnPrintf(("AJ_AccessControlCheck(id=0x%08X, name=%s, direction=%x): Access table not initialised\n", id, name, direction));
        return AJ_ERR_ACCESS;
    }

    status = AJ_GetPeerIndex(name, &peer);
    if (AJ_OK != status) {
        AJ_WarnPrintf(("AJ_AccessControlCheck(id=0x%08X, name=%s, direction=%x): Peer not in table\n", id, name, direction));
        return AJ_ERR_ACCESS;
    }

    // This linked list is a reverse ordered list
    mbr = g_access;
    while (mbr && (id != mbr->id)) {
        mbr = mbr->next;
    }

    if (NULL == mbr) {
        /*
         * We may end up here because a message was encrypted on
         * an unsecured interface (eg. ExchangeGroupKeys)
         * That means we don't have that member in our access control table.
         * This is a white list of allowed messages.
         */
        switch (id) {
        case AJ_METHOD_EXCHANGE_GROUP_KEYS:
        case AJ_METHOD_SEND_MANIFEST:
            status = AJ_OK;
            break;

        default:
            status = AJ_ERR_ACCESS;
            break;
        }
        AJ_WarnPrintf(("AJ_AccessControlCheck(id=0x%08X, name=%s, direction=%x): Member not in table %s\n", id, name, direction, AJ_StatusText(status)));
        return status;
    }

    status = AJ_ERR_ACCESS;
    access = mbr->access[peer];
    switch (direction) {
    case AJ_ACCESS_INCOMING:
        if ((ACCESS_INCOMING_ALLOW & access) && !(ACCESS_INCOMING_DENY & access)) {
            status = AJ_OK;
        }
        break;

    case AJ_ACCESS_OUTGOING:
        if ((ACCESS_OUTGOING_ALLOW & access) && !(ACCESS_OUTGOING_DENY & access)) {
            status = AJ_OK;
        }
        break;
    }
    AJ_InfoPrintf(("AJ_AccessControlCheck(id=0x%08X, name=%s, direction=%x): %s\n", id, name, direction, AJ_StatusText(status)));

    return status;
}

/*
 * Clears all previous (if any) access control for an index
 */
AJ_Status AJ_AccessControlReset(const char* name)
{
    AJ_Status status;
    AccessControlMember* node = g_access;
    uint32_t peer;

    AJ_InfoPrintf(("AJ_AccessControlReset(name=%s)\n", name));

    status = AJ_GetPeerIndex(name, &peer);
    if (AJ_OK != status) {
        AJ_WarnPrintf(("AJ_AccessControlReset(name=%s): Peer not in table\n", name));
        return status;
    }
    while (node) {
        node->access[peer] = 0;
        node = node->next;
    }

    return AJ_OK;
}

void AJ_ManifestTemplateSet(AJ_Manifest* manifest)
{
    AJ_InfoPrintf(("AJ_ManifestTemplateSet(manifest=%p)\n", manifest));
    g_manifest = manifest;
}

static void AJ_PermissionMemberFree(AJ_PermissionMember* head)
{
    AJ_PermissionMember* node;
    while (head) {
        node = head;
        head = head->next;
        AJ_Free(node);
    }
}

static void AJ_PermissionRuleFree(AJ_PermissionRule* head)
{
    AJ_PermissionRule* node;
    while (head) {
        node = head;
        head = head->next;
        AJ_PermissionMemberFree(node->members);
        AJ_Free(node);
    }
}

void AJ_ManifestFree(AJ_Manifest* manifest)
{
    if (manifest) {
        AJ_PermissionRuleFree(manifest->rules);
        AJ_Free(manifest);
    }
}

static void AJ_PermissionPeerFree(AJ_PermissionPeer* head)
{
    AJ_PermissionPeer* node;
    while (head) {
        node = head;
        head = head->next;
        if (node->pub) {
            AJ_Free(node->pub);
        }
        AJ_Free(node);
    }
}

static void AJ_PermissionACLFree(AJ_PermissionACL* head)
{
    AJ_PermissionACL* node;
    while (head) {
        node = head;
        head = head->next;
        AJ_PermissionPeerFree(node->peers);
        AJ_PermissionRuleFree(node->rules);
        AJ_Free(node);
    }
}

void AJ_PolicyFree(AJ_Policy* policy)
{
    if (policy) {
        AJ_PermissionACLFree(policy->acls);
        AJ_Free(policy);
    }
}

//SIG = a(syy)
static AJ_Status AJ_PermissionMemberMarshal(const AJ_PermissionMember* head, AJ_Message* msg)
{
    AJ_Status status;
    AJ_Arg container;

    AJ_InfoPrintf(("AJ_PermissionMemberMarshal(head=%p, msg=%p)\n", head, msg));

    status = AJ_MarshalContainer(msg, &container, AJ_ARG_ARRAY);
    if (AJ_OK != status) {
        return status;
    }
    while (head) {
        status = AJ_MarshalArgs(msg, "(syy)", head->mbr, (uint8_t) head->type, (uint8_t) head->action);
        if (AJ_OK != status) {
            return status;
        }
        head = head->next;
    }
    status = AJ_MarshalCloseContainer(msg, &container);

    return status;
}

//SIG = a(ssa(syy))
static AJ_Status AJ_PermissionRuleMarshal(const AJ_PermissionRule* head, AJ_Message* msg)
{
    AJ_Status status;
    AJ_Arg container1;
    AJ_Arg container2;

    AJ_InfoPrintf(("AJ_PermissionRuleMarshal(head=%p, msg=%p)\n", head, msg));

    status = AJ_MarshalContainer(msg, &container1, AJ_ARG_ARRAY);
    if (AJ_OK != status) {
        return status;
    }
    while (head) {
        status = AJ_MarshalContainer(msg, &container2, AJ_ARG_STRUCT);
        if (AJ_OK != status) {
            return status;
        }
        status = AJ_MarshalArgs(msg, "ss", head->obj, head->ifn);
        if (AJ_OK != status) {
            return status;
        }
        status = AJ_PermissionMemberMarshal(head->members, msg);
        if (AJ_OK != status) {
            return status;
        }
        status = AJ_MarshalCloseContainer(msg, &container2);
        if (AJ_OK != status) {
            return status;
        }
        head = head->next;
    }
    status = AJ_MarshalCloseContainer(msg, &container1);

    return status;
}

//SIG = a(ssa(syy))
AJ_Status AJ_ManifestMarshal(AJ_Manifest* manifest, AJ_Message* msg)
{
    AJ_InfoPrintf(("AJ_ManifestMarshal(manifest=%p, msg=%p)\n", manifest, msg));
    if (NULL == manifest) {
        return AJ_ERR_INVALID;
    }
    return AJ_PermissionRuleMarshal(manifest->rules, msg);
}

AJ_Status AJ_ManifestTemplateMarshal(AJ_Message* msg)
{
    return AJ_ManifestMarshal(g_manifest, msg);
}

AJ_Status AJ_MarshalDefaultPolicy(AJ_Message* msg, AJ_ECCPublicKey* pub, uint8_t* g, size_t glen)
{
    AJ_Status status;

    /* All allowed */
    AJ_PermissionMember member0_0 = { "*", AJ_MEMBER_TYPE_ANY, AJ_ACTION_PROVIDE | AJ_ACTION_OBSERVE | AJ_ACTION_MODIFY, NULL };
    /* Outgoing allowed, incoming signal allowed */
    AJ_PermissionMember member1_0 = { "*", AJ_MEMBER_TYPE_ANY, AJ_ACTION_PROVIDE, NULL };
    AJ_PermissionMember member1_1 = { "*", AJ_MEMBER_TYPE_SIGNAL, AJ_ACTION_OBSERVE, &member1_0 };

    AJ_PermissionRule rule0 = { "*", "*", &member0_0, NULL };
    AJ_PermissionRule rule1 = { "*", "*", &member1_1, NULL };

    /* Admin group */
    AJ_PermissionPeer peer0 = { AJ_PEER_TYPE_WITH_MEMBERSHIP, pub, (AJ_GUID*) g, NULL };
    /* Any authenticated peer */
    AJ_PermissionPeer peer1 = { AJ_PEER_TYPE_ANY_TRUSTED, NULL, NULL, NULL };

    /* Admin group gets all allowed */
    AJ_PermissionACL acl0 = { &peer0, &rule0, NULL };
    /* Any authenticated peer gets outgoing allowed, incoming signal allowed */
    AJ_PermissionACL acl1 = { &peer1, &rule1, &acl0 };

    AJ_Policy policy = { POLICY_SPECIFICATION_VERSION, 1, &acl1 };

    /* Marshal the policy */
    status = AJ_PolicyMarshal(&policy, msg);

    return status;
}

//SIG = a(ya(yyayay)ay)
static AJ_Status AJ_PermissionPeerMarshal(const AJ_PermissionPeer* head, AJ_Message* msg)
{
    AJ_Status status;
    AJ_Arg container1;
    AJ_Arg container2;
    AJ_Arg container3;

    AJ_InfoPrintf(("AJ_PermissionPeerMarshal(head=%p, msg=%p)\n", head, msg));

    status = AJ_MarshalContainer(msg, &container1, AJ_ARG_ARRAY);
    if (AJ_OK != status) {
        return status;
    }
    while (head) {
        status = AJ_MarshalContainer(msg, &container2, AJ_ARG_STRUCT);
        if (AJ_OK != status) {
            return status;
        }
        status = AJ_MarshalArgs(msg, "y", head->type);
        if (AJ_OK != status) {
            return status;
        }

        // Marshal key (optional)
        status = AJ_MarshalContainer(msg, &container3, AJ_ARG_ARRAY);
        if (AJ_OK != status) {
            return status;
        }
        if (head->pub) {
            status = AJ_MarshalArgs(msg, "(yyayay)", head->pub->alg, head->pub->crv, head->pub->x, KEY_ECC_SZ, head->pub->y, KEY_ECC_SZ);
            if (AJ_OK != status) {
                return status;
            }
        }
        status = AJ_MarshalCloseContainer(msg, &container3);
        if (AJ_OK != status) {
            return status;
        }

        // Marshal group (optional)
        if (head->group) {
            status = AJ_MarshalArgs(msg, "ay", head->group, AJ_GUID_LEN);
        } else {
            status = AJ_MarshalArgs(msg, "ay", head->group, 0);
        }
        if (AJ_OK != status) {
            return status;
        }

        status = AJ_MarshalCloseContainer(msg, &container2);
        if (AJ_OK != status) {
            return status;
        }
        head = head->next;
    }
    status = AJ_MarshalCloseContainer(msg, &container1);

    return status;
}

//SIG = a(a(ya(yyayay)ay)a(ssa(syy)))
static AJ_Status AJ_PermissionACLMarshal(const AJ_PermissionACL* head, AJ_Message* msg)
{
    AJ_Status status;
    AJ_Arg container1;
    AJ_Arg container2;

    AJ_InfoPrintf(("AJ_PermissionACLMarshal(head=%p, msg=%p)\n", head, msg));

    status = AJ_MarshalContainer(msg, &container1, AJ_ARG_ARRAY);
    if (AJ_OK != status) {
        return status;
    }
    while (head) {
        status = AJ_MarshalContainer(msg, &container2, AJ_ARG_STRUCT);
        if (AJ_OK != status) {
            return status;
        }
        status = AJ_PermissionPeerMarshal(head->peers, msg);
        if (AJ_OK != status) {
            return status;
        }
        status = AJ_PermissionRuleMarshal(head->rules, msg);
        if (AJ_OK != status) {
            return status;
        }
        status = AJ_MarshalCloseContainer(msg, &container2);
        if (AJ_OK != status) {
            return status;
        }
        head = head->next;
    }
    status = AJ_MarshalCloseContainer(msg, &container1);

    return status;
}

//SIG = (qua(a(ya(yyayay)ay)a(ssa(syy))))
AJ_Status AJ_PolicyMarshal(const AJ_Policy* policy, AJ_Message* msg)
{
    AJ_Status status;
    AJ_Arg container;

    AJ_InfoPrintf(("AJ_PolicyMarshal(policy=%p, msg=%p)\n", policy, msg));

    if (NULL == policy) {
        return AJ_ERR_INVALID;
    }
    status = AJ_MarshalContainer(msg, &container, AJ_ARG_STRUCT);
    if (AJ_OK != status) {
        return status;
    }
    status = AJ_MarshalArgs(msg, "qu", policy->specification, policy->version);
    if (AJ_OK != status) {
        return status;
    }
    status = AJ_PermissionACLMarshal(policy->acls, msg);
    if (AJ_OK != status) {
        return status;
    }
    status = AJ_MarshalCloseContainer(msg, &container);

    return status;
}

//SIG = a(syy)
static AJ_Status AJ_PermissionMemberUnmarshal(AJ_PermissionMember** head, AJ_Message* msg)
{
    AJ_Status status;
    AJ_Arg container1;
    AJ_Arg container2;
    AJ_PermissionMember* node;

    AJ_InfoPrintf(("AJ_PermissionMemberUnmarshal(head=%p, msg=%p)\n", head, msg));

    status = AJ_UnmarshalContainer(msg, &container1, AJ_ARG_ARRAY);
    if (AJ_OK != status) {
        goto Exit;
    }
    while (AJ_OK == status) {
        status = AJ_UnmarshalContainer(msg, &container2, AJ_ARG_STRUCT);
        if (AJ_OK != status) {
            break;
        }
        node = (AJ_PermissionMember*) AJ_Malloc(sizeof (AJ_PermissionMember));
        if (NULL == node) {
            goto Exit;
        }
        node->next = *head;
        *head = node;
        status = AJ_UnmarshalArgs(msg, "syy", &node->mbr, &node->type, &node->action);
        if (AJ_OK != status) {
            goto Exit;
        }
        status = AJ_UnmarshalCloseContainer(msg, &container2);
    }
    if (AJ_ERR_NO_MORE != status) {
        goto Exit;
    }
    status = AJ_UnmarshalCloseContainer(msg, &container1);
    if (AJ_OK != status) {
        goto Exit;
    }

    return AJ_OK;

Exit:
    //Cleanup
    AJ_PermissionMemberFree(*head);
    *head = NULL;
    return AJ_ERR_INVALID;
}

//SIG = a(ssa(syy))
static AJ_Status AJ_PermissionRuleUnmarshal(AJ_PermissionRule** head, AJ_Message* msg)
{
    AJ_Status status;
    AJ_Arg container1;
    AJ_Arg container2;
    AJ_PermissionRule* node;

    AJ_InfoPrintf(("AJ_PermissionRuleUnmarshal(head=%p, msg=%p)\n", head, msg));

    status = AJ_UnmarshalContainer(msg, &container1, AJ_ARG_ARRAY);
    if (AJ_OK != status) {
        goto Exit;
    }
    while (AJ_OK == status) {
        status = AJ_UnmarshalContainer(msg, &container2, AJ_ARG_STRUCT);
        if (AJ_OK != status) {
            break;
        }
        node = (AJ_PermissionRule*) AJ_Malloc(sizeof (AJ_PermissionRule));
        if (NULL == node) {
            goto Exit;
        }
        node->members = NULL;
        node->next = *head;
        *head = node;
        status = AJ_UnmarshalArgs(msg, "ss", &node->obj, &node->ifn);
        if (AJ_OK != status) {
            goto Exit;
        }
        status = AJ_PermissionMemberUnmarshal(&node->members, msg);
        if (AJ_OK != status) {
            goto Exit;
        }
        status = AJ_UnmarshalCloseContainer(msg, &container2);
    }
    if (AJ_ERR_NO_MORE != status) {
        goto Exit;
    }
    status = AJ_UnmarshalCloseContainer(msg, &container1);
    if (AJ_OK != status) {
        goto Exit;
    }

    return AJ_OK;

Exit:
    //Cleanup
    AJ_PermissionRuleFree(*head);
    *head = NULL;
    return AJ_ERR_INVALID;
}

//SIG = a(ssa(syy))
AJ_Status AJ_ManifestUnmarshal(AJ_Manifest** manifest, AJ_Message* msg)
{
    AJ_Status status;
    AJ_Manifest* tmp;

    AJ_InfoPrintf(("AJ_ManifestUnmarshal(manifest=%p, msg=%p)\n", manifest, msg));

    tmp = (AJ_Manifest*) AJ_Malloc(sizeof (AJ_Manifest));
    if (NULL == tmp) {
        goto Exit;
    }

    tmp->rules = NULL;
    status = AJ_PermissionRuleUnmarshal(&tmp->rules, msg);
    if (AJ_OK != status) {
        goto Exit;
    }

    *manifest = tmp;
    return AJ_OK;

Exit:
    //Cleanup
    AJ_ManifestFree(tmp);
    return AJ_ERR_INVALID;
}

//SIG = a(ya(yyayay)ay)
static AJ_Status AJ_PermissionPeerUnmarshal(AJ_PermissionPeer** head, AJ_Message* msg)
{
    AJ_Status status;
    AJ_Arg container1;
    AJ_Arg container2;
    AJ_Arg container3;
    AJ_PermissionPeer* node;
    uint8_t* data;
    size_t size;

    AJ_InfoPrintf(("AJ_PermissionPeerUnmarshal(head=%p, msg=%p)\n", head, msg));

    status = AJ_UnmarshalContainer(msg, &container1, AJ_ARG_ARRAY);
    if (AJ_OK != status) {
        goto Exit;
    }
    while (AJ_OK == status) {
        status = AJ_UnmarshalContainer(msg, &container2, AJ_ARG_STRUCT);
        if (AJ_OK != status) {
            break;
        }
        node = (AJ_PermissionPeer*) AJ_Malloc(sizeof (AJ_PermissionPeer));
        if (NULL == node) {
            status = AJ_ERR_RESOURCES;
            goto Exit;
        }
        node->pub = NULL;
        node->group = NULL;
        node->next = *head;
        *head = node;
        status = AJ_UnmarshalArgs(msg, "y", &node->type);
        if (AJ_OK != status) {
            goto Exit;
        }

        status = AJ_UnmarshalContainer(msg, &container3, AJ_ARG_ARRAY);
        if (AJ_OK != status) {
            goto Exit;
        }
        // Unmarshal key (optional)
        switch (node->type) {
        case AJ_PEER_TYPE_FROM_CA:
        case AJ_PEER_TYPE_WITH_PUBLIC_KEY:
        case AJ_PEER_TYPE_WITH_MEMBERSHIP:
            node->pub = (AJ_ECCPublicKey*) AJ_Malloc(sizeof (AJ_ECCPublicKey));
            if (NULL == node->pub) {
                goto Exit;
            }
            status = AJ_UnmarshalECCPublicKey(msg, node->pub);
            if (AJ_OK != status) {
                goto Exit;
            }
            break;
        }
        status = AJ_UnmarshalCloseContainer(msg, &container3);
        if (AJ_OK != status) {
            goto Exit;
        }

        // Unmarshal group (optional)
        status = AJ_UnmarshalArgs(msg, "ay", &data, &size);
        if (AJ_OK == status) {
            if (AJ_GUID_LEN == size) {
                node->group = (AJ_GUID*) data;
            }
        }

        status = AJ_UnmarshalCloseContainer(msg, &container2);
    }
    if (AJ_ERR_NO_MORE != status) {
        goto Exit;
    }
    status = AJ_UnmarshalCloseContainer(msg, &container1);
    if (AJ_OK != status) {
        goto Exit;
    }

    return AJ_OK;

Exit:
    //Cleanup
    AJ_PermissionPeerFree(*head);
    *head = NULL;
    return AJ_ERR_INVALID;
}

//SIG = a(a(ya(yyayay)ay)a(ssa(syy)))
static AJ_Status AJ_PermissionACLUnmarshal(AJ_PermissionACL** head, AJ_Message* msg)
{
    AJ_Status status;
    AJ_Arg container1;
    AJ_Arg container2;
    AJ_PermissionACL* node;

    AJ_InfoPrintf(("AJ_PermissionACLUnmarshal(head=%p, msg=%p)\n", head, msg));

    status = AJ_UnmarshalContainer(msg, &container1, AJ_ARG_ARRAY);
    if (AJ_OK != status) {
        goto Exit;
    }
    while (AJ_OK == status) {
        status = AJ_UnmarshalContainer(msg, &container2, AJ_ARG_STRUCT);
        if (AJ_OK != status) {
            break;
        }
        node = (AJ_PermissionACL*) AJ_Malloc(sizeof (AJ_PermissionACL));
        if (NULL == node) {
            goto Exit;
        }
        node->peers = NULL;
        node->rules = NULL;
        node->next = *head;
        *head = node;
        status = AJ_PermissionPeerUnmarshal(&node->peers, msg);
        if (AJ_OK != status) {
            break;
        }
        status = AJ_PermissionRuleUnmarshal(&node->rules, msg);
        if (AJ_OK != status) {
            break;
        }
        status = AJ_UnmarshalCloseContainer(msg, &container2);
    }
    if (AJ_ERR_NO_MORE != status) {
        goto Exit;
    }
    status = AJ_UnmarshalCloseContainer(msg, &container1);
    if (AJ_OK != status) {
        goto Exit;
    }

    return AJ_OK;

Exit:
    //Cleanup
    AJ_PermissionACLFree(*head);
    *head = NULL;
    return AJ_ERR_INVALID;
}

//SIG = (qua(a(ya(yyayay)ay)a(ssa(syy))))
AJ_Status AJ_PolicyUnmarshal(AJ_Policy** policy, AJ_Message* msg)
{
    AJ_Status status;
    AJ_Arg container;
    AJ_Policy* tmp = NULL;

    AJ_InfoPrintf(("AJ_PolicyUnmarshal(policy=%p, msg=%p)\n", policy, msg));

    tmp = (AJ_Policy*) AJ_Malloc(sizeof (AJ_Policy));
    if (NULL == tmp) {
        goto Exit;
    }

    tmp->acls = NULL;
    status = AJ_UnmarshalContainer(msg, &container, AJ_ARG_STRUCT);
    if (AJ_OK != status) {
        goto Exit;
    }
    status = AJ_UnmarshalArgs(msg, "qu", &tmp->specification, &tmp->version);
    if (AJ_OK != status) {
        goto Exit;
    }
    status = AJ_PermissionACLUnmarshal(&tmp->acls, msg);
    if (AJ_OK != status) {
        goto Exit;
    }
    status = AJ_UnmarshalCloseContainer(msg, &container);
    if (AJ_OK != status) {
        goto Exit;
    }

    *policy = tmp;
    return AJ_OK;

Exit:
    //Cleanup
    AJ_PolicyFree(tmp);
    return AJ_ERR_INVALID;
}

void AJ_ManifestDigest(AJ_CredField* manifest, uint8_t digest[SHA256_DIGEST_LENGTH])
{
    AJ_SHA256_Context ctx;

    AJ_SHA256_Init(&ctx);
    AJ_SHA256_Update(&ctx, manifest->data, manifest->size);
    AJ_SHA256_Final(&ctx, digest);
}

static void PolicyUnload(Policy* policy)
{
    AJ_CredFieldFree(&policy->buffer);
    policy->buffer.data = NULL;
    AJ_PolicyFree(policy->policy);
    policy->policy = NULL;
}

static AJ_Status PolicyLoad(Policy* policy)
{
    AJ_Status status;
    AJ_BusAttachment bus;
    AJ_MsgHeader hdr;
    AJ_Message msg;

    AJ_InfoPrintf(("PolicyLoad(policy=%p)\n", policy));

    policy->buffer.data = NULL;
    policy->buffer.size = 0;
    policy->policy = NULL;

    /* Read the policy from NVRAM */
    status = AJ_CredentialGet(AJ_CRED_TYPE_POLICY, NULL, NULL, &policy->buffer);
    if (AJ_OK != status) {
        AJ_InfoPrintf(("PolicyLoad(policy=%p): No stored policy\n", policy));
        goto Exit;
    }

    /* Unmarshal the policy */
    AJ_LocalMsg(&bus, &hdr, &msg, "(qua(a(ya(yyayay)ay)a(ssa(syy))))", policy->buffer.data, policy->buffer.size);
    status = AJ_PolicyUnmarshal(&policy->policy, &msg);
    if (AJ_OK != status) {
        AJ_InfoPrintf(("PolicyLoad(policy=%p): Unmarshal failed\n", policy));
        goto Exit;
    }

    return AJ_OK;

Exit:
    PolicyUnload(policy);
    return AJ_ERR_INVALID;
}

AJ_Status AJ_AuthorisationInit()
{
    /* Init access control list */
    return AccessControlInit();
}

void AJ_AuthorisationClose()
{
    /* Unload access control list */
    AccessControlClose();
}

static uint8_t CommonPath(const char* name, const char* desc)
{
    if (!name || !desc) {
        return 0;
    }
    /* Skip past common characters, or until a wildcard is hit */
    while (*name && *desc) {
        if ('*' == *name) {
            return 1;
        }
        if (*name++ != *desc++) {
            return 0;
        }
    }
    /* If description complete or up to space */
    return (('\0' == *desc) || (' ' == *desc));
}

/* 3 message types: SIGNAL, METHOD, PROPERTY */
static uint8_t access_incoming[3] = { AJ_ACTION_PROVIDE, AJ_ACTION_MODIFY, AJ_ACTION_OBSERVE | AJ_ACTION_MODIFY };
static uint8_t access_outgoing[3] = { AJ_ACTION_OBSERVE, AJ_ACTION_PROVIDE, AJ_ACTION_PROVIDE                    };
static void PermissionMemberApply(AccessControlMember* mbr, uint8_t type, uint8_t action, uint32_t peer, uint8_t apply)
{
    if (0 == action) {
        // Explicit deny both directions
        mbr->access[peer] |= ACCESS_INCOMING_DENY | ACCESS_OUTGOING_DENY;
    } else {
        AJ_ASSERT(type < 3);
        if (access_incoming[type] & action) {
            if (ACCESS_POLICY == apply) {
                mbr->access[peer] |= ACCESS_INCOMING_ALLOW;
            } else {
                mbr->access[peer] &= ACCESS_INCOMING_ALLOW;
            }
        }
        if (access_outgoing[type] & action) {
            if (ACCESS_POLICY == apply) {
                mbr->access[peer] |= ACCESS_OUTGOING_ALLOW;
            } else {
                mbr->access[peer] &= ACCESS_OUTGOING_ALLOW;
            }
        }
    }
}

static uint8_t MemberType(uint8_t a, uint8_t b)
{
    switch (a) {
    case AJ_MEMBER_TYPE_ANY:
        return 1;

    case AJ_MEMBER_TYPE_SIGNAL:
        return (SIGNAL == b);

    case AJ_MEMBER_TYPE_METHOD:
        return (METHOD == b);

    case AJ_MEMBER_TYPE_PROPERTY:
        return (PROPERTY == b);
    }
    return 0;
}

static void PermissionRuleApply(AJ_PermissionRule* rules, uint32_t peer, uint8_t apply)
{
    AccessControlMember* acm;
    AJ_PermissionRule* rule;
    AJ_PermissionMember* member;
    uint8_t type;
    const char* obj;
    const char* ifn;
    const char* mbr;

    AJ_InfoPrintf(("PermissionRuleApply(rules=%p, peer=%d, apply=%x)\n", rules, peer, apply));

    acm = g_access;
    while (acm) {
        rule = rules;
        while (rule) {
            obj = acm->obj;
            ifn = acm->ifn;
            if (SECURE_TRUE == *ifn) {
                ifn++;
            }
            if (CommonPath(rule->obj, obj) && CommonPath(rule->ifn, ifn)) {
                member = rule->members;
                while (member) {
                    mbr = acm->mbr;
                    type = MEMBER_TYPE(*mbr);
                    mbr++;
                    if (SESSIONLESS == *mbr) {
                        mbr++;
                    }
                    if (CommonPath(member->mbr, mbr) && MemberType(member->type, type)) {
                        PermissionMemberApply(acm, type, member->action, peer, apply);
                    }
                    member = member->next;
                }
            }
            rule = rule->next;
        }
        acm = acm->next;
    }
}

AJ_Status AJ_ManifestApply(AJ_Manifest* manifest, const char* name)
{
    AJ_Status status;
    uint32_t peer;

    AJ_InfoPrintf(("AJ_ManifestApply(manifest=%p, name=%s)\n", manifest, name));

    status = AJ_GetPeerIndex(name, &peer);
    if (AJ_OK != status) {
        AJ_WarnPrintf(("AJ_ManifestApply(manifest=%p, name=%s): Peer not in table\n", manifest, name));
        return AJ_ERR_SECURITY;
    }

    PermissionRuleApply(manifest->rules, peer, ACCESS_MANIFEST);

    return AJ_OK;
}

static uint8_t PermissionPeerFind(AJ_PermissionPeer* peer, AJ_AuthenticationContext* ctx)
{
    while (peer) {
        switch (peer->type) {
        case AJ_PEER_TYPE_ALL:
            return 1;

        case AJ_PEER_TYPE_ANY_TRUSTED:
            if (AUTH_SUITE_ECDHE_NULL != ctx->suite) {
                return 1;
            }
            break;

        case AJ_PEER_TYPE_FROM_CA:
            AJ_ASSERT(peer->pub);
            if (0 == memcmp((uint8_t*) peer->pub, (uint8_t*) &ctx->kactx.ecdsa.issuer, sizeof (AJ_ECCPublicKey))) {
                return 1;
            }
            break;

        case AJ_PEER_TYPE_WITH_PUBLIC_KEY:
            AJ_ASSERT(peer->pub);
            if (0 == memcmp((uint8_t*) peer->pub, (uint8_t*) &ctx->kactx.ecdsa.subject, sizeof (AJ_ECCPublicKey))) {
                return 1;
            }
            break;

        case AJ_PEER_TYPE_WITH_MEMBERSHIP:
            //TODO
            break;
        }
        peer = peer->next;
    }

    return 0;
}

static void PermissionACLApply(AJ_PermissionACL* acl, AJ_AuthenticationContext* ctx, uint32_t peer)
{
    while (acl) {
        if (PermissionPeerFind(acl->peers, ctx)) {
            PermissionRuleApply(acl->rules, peer, ACCESS_POLICY);
        }
        acl = acl->next;
    }
}

AJ_Status AJ_PolicyApply(AJ_AuthenticationContext* ctx, const char* name)
{
    AJ_Status status;
    Policy policy;
    uint32_t peer;
    AccessControlMember* mbr;

    AJ_InfoPrintf(("AJ_PolicyApply(ctx=%p, name=%s)\n", ctx, name));

    status = AJ_GetPeerIndex(name, &peer);
    if (AJ_OK != status) {
        AJ_WarnPrintf(("AJ_PolicyApply(ctx=%p, name=%s): Peer not in table\n", ctx, name));
        return AJ_ERR_SECURITY;
    }

    status = PolicyLoad(&policy);
    if (AJ_OK == status) {
        PermissionACLApply(policy.policy->acls, ctx, peer);
        PolicyUnload(&policy);
    } else {
        AJ_InfoPrintf(("AJ_PolicyApply(ctx=%p, name=%p): No stored policy\n", ctx, name));
        /* Initial restricted access rights */
        mbr = g_access;
        while (mbr) {
            switch (mbr->id) {
            case AJ_METHOD_SECURITY_GET_PROP:
            case AJ_PROPERTY_SEC_ECC_PUBLICKEY:
            case AJ_PROPERTY_SEC_MANIFEST_TEMPLATE:
            case AJ_METHOD_CLAIMABLE_CLAIM:
                mbr->access[peer] |= ACCESS_INCOMING_ALLOW | ACCESS_OUTGOING_ALLOW;
                break;
            }
            mbr = mbr->next;
        }
    }

    return AJ_OK;
}

AJ_Status AJ_PolicyVersion(uint32_t* version)
{
    AJ_Status status;
    Policy policy;

    status = PolicyLoad(&policy);
    if (AJ_OK != status) {
        AJ_InfoPrintf(("AJ_PolicyVersion(version=%p): Policy not loaded\n", version));
        return AJ_ERR_INVALID;
    }
    *version = policy.policy->version;
    PolicyUnload(&policy);

    return AJ_OK;
}
