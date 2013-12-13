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

#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>

#undef WIFI_UDP_WORKING

#include <SPI.h>
#ifdef WIFI_UDP_WORKING
#include <WiFi.h>
#else
#include <Ethernet.h>
#endif

#include <alljoyn.h>

#ifdef WIFI_UDP_WORKING
static char ssid[] = "pez-wifi";
static char pass[] = "71DF437B55"; // passphrase for the SSID
int wifiStatus = WL_IDLE_STATUS;
#endif

int AJ_Main();


void setup() {

    //Initialize serial and wait for port to open:
    Serial.begin(115200);
    while (!Serial) {
        ; // wait for serial port to connect. Needed for Leonardo only
    }

    printf("hello, world.\n");

#ifdef WIFI_UDP_WORKING
    // check for the presence of the shield:
    if (WiFi.status() == WL_NO_SHIELD) {
        printf("WiFi shield not present\n");
        // don't continue:
        while (true) ;
    }

    // attempt to connect to Wifi network:
    while (wifiStatus != WL_CONNECTED) {
        printf("Attempting to connect to WPA SSID: %s\n", ssid);

        // Connect to WEP private network
        wifiStatus = WiFi.begin(ssid, 0, pass);

        // wait 3 seconds for connection:
        delay(3000);

        // print your WiFi shield's IP address:
        IPAddress ip = WiFi.localIP();
        Serial.print("IP Address ");
        Serial.println(ip);
    }
#else
    byte mac[] = { 0x00, 0xAA, 0xBB, 0xCC, 0xDE, 0x02 };
    // start the Ethernet connection:
    if (Ethernet.begin(mac) == 0) {
        printf("Failed to configure Ethernet using DHCP\n");
        // no point in carrying on, so do nothing forevermore:
        for (;;)
            ;
    }
#endif


}



void loop() {
    AJ_Main();
}
