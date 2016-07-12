/**
 * @file
 */
/******************************************************************************
 * Copyright AllSeen Alliance. All rights reserved.
 *
 * SDPX-License-Identifier: ISC
 ******************************************************************************/

/**
 * Per-module definition of the current module for debug logging.  Must be defined
 * prior to first inclusion of aj_debug.h
 */
#define AJ_MODULE TARGET_SERIAL

#include <ajtcl/aj_target.h>
#include <ajtcl/aj_status.h>
#include <ajtcl/aj_debug.h>

/**
 * Turn on per-module debug printing by setting this variable to non-zero value
 * (usually in debugger).
 */
#ifndef NDEBUG
uint8_t dbgTARGET_SERIAL = 0;
#endif

#include <sys/types.h>

/**
 * This function initialized the UART piece of the transport.
 */
AJ_Status AJ_SerialTargetInit(const char* ttyName)
{
    AJ_ErrPrintf(("AJ_SerialTargetInit(): Serial undefined on this target\n"));
    assert(0);
    return AJ_ERR_UNEXPECTED;
}



AJ_Status AJ_UART_Tx(uint8_t* buffer, uint16_t len)
{
    AJ_ErrPrintf(("AJ_UART_Tx(): Serial undefined on this target\n"));
    assert(0);
    return AJ_ERR_UNEXPECTED;
}



void OI_HCIIfc_DeviceHasBeenReset(void)
{
    AJ_ErrPrintf(("OI_HCIIfc_DeviceHasBeenReset(): Serial undefined on this target\n"));
    assert(0);
}


const char* OI_HciDataTypeText(uint8_t hciDataType)
{
    AJ_ErrPrintf(("OI_HciDataTypeText(): Serial undefined on this target\n"));
    assert(0);
    return("ERROR: Serial undefined on this target \n");
}

void WaitForAck(void)
{
    AJ_ErrPrintf(("WaitForAck(): Serial undefined on this target\n"));
    assert(0);
}

void OI_HCIIfc_SendCompleted(uint8_t sendType,
                             AJ_Status status)
{
    AJ_ErrPrintf(("OI_HCIIfc_SendCompleted(): Serial undefined on this target\n"));
    assert(0);
}


