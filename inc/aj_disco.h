#ifndef _AJ_DISCO_H
#define _AJ_DISCO_H

/**
 * @file
 */
/******************************************************************************
 *  *    Copyright (c) Open Connectivity Foundation (OCF) and AllJoyn Open
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

#include <ajtcl/aj_target.h>
#include <ajtcl/aj_bufio.h>

#ifdef __cplusplus
extern "C" {
#endif

#define IPV4ADDRSIZE_U8    4
#define IPV4ADDRSIZE_U32   1
#define IPV6ADDRSIZE_U8   16
#define IPV6ADDRSIZE_U32   4

/**
 * Information about the remote service
 */
typedef struct _AJ_Service {
    uint8_t addrTypes;                  /**< address type */
    uint16_t transportMask;             /**< restricts the transports the advertisement */
    uint16_t ipv4port;                  /**< port number of ipv4 */
    uint16_t ipv6port;                  /**< port number of ipv6 */
    uint32_t ipv4;                      /**< ipv4 address */
    uint16_t priority;                  /**< priority */
    uint32_t pv;                        /**< protocol version */
    uint32_t ipv6[IPV6ADDRSIZE_U32];    /**< ipv6 address */

    uint16_t ipv4portUdp;               /**< port number of ipv4 */
    uint16_t ipv6portUdp;               /**< port number of ipv6 */
    uint32_t ipv4Udp;                   /**< ipv4 address */
    uint32_t ipv6Udp[IPV6ADDRSIZE_U32]; /**< ipv6 address */
    uint32_t scope_id;                  /**< ipv6 scope id for a link local address */
} AJ_Service;

/**
 * Discover a remote service
 *
 * @param prefix            The service name prefix
 * @param service           Information about the service that was found
 * @param timeout           How long to wait to discover the service
 * @param selectionTimeout  How long to wait to receive router responses
 *
 * @return                  Return AJ_Status
 */
AJ_Status AJ_Discover(const char* prefix, AJ_Service* service, uint32_t timeout, uint32_t selectionTimeout);

#ifdef __cplusplus
}
#endif

#endif