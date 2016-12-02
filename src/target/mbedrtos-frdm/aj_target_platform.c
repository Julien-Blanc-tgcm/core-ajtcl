/**
 * @file
 */
/******************************************************************************
 *  * 
 *    Copyright (c) 2016 Open Connectivity Foundation and AllJoyn Open
 *    Source Project Contributors and others.
 *    
 *    All rights reserved. This program and the accompanying materials are
 *    made available under the terms of the Apache License, Version 2.0
 *    which accompanies this distribution, and is available at
 *    http://www.apache.org/licenses/LICENSE-2.0

 ******************************************************************************/

#include <ajtcl/aj_target_platform.h>
#include <ajtcl/aj_nvram.h>
#include <ajtcl/aj_debug.h>

void _AJ_PlatformInit(void)
{
    BoardPrintfInit(115200);
    return;
}
uint16_t AJ_ByteSwap16(uint16_t x)
{
    return ((((x) >> 8) & 0xff) | (((x) & 0xff) << 8));
}

uint32_t swap32(uint32_t x)
{
    return AJ_ByteSwap32(x);
}

uint32_t AJ_ByteSwap32(uint32_t x)
{
    return ((x >> 24) & 0x000000FF) | ((x >> 8) & 0x0000FF00) |
           ((x << 24) & 0xFF000000) | ((x << 8) & 0x00FF0000);
}

uint64_t AJ_ByteSwap64(uint64_t x)
{
    return ((x >> 56) & 0x00000000000000FF) | ((x >> 40) & 0x000000000000FF00) |
           ((x << 56) & 0xFF00000000000000) | ((x << 40) & 0x00FF000000000000) |
           ((x >> 24) & 0x0000000000FF0000) | ((x >>  8) & 0x00000000FF000000) |
           ((x << 24) & 0x0000FF0000000000) | ((x <<  8) & 0x000000FF00000000);
}

uint8_t AJ_SeedRNG(void)
{
    return 1;
}

void _exit(int i)
{
    while (1);
}
int _kill(int pid)
{
    return 1;
}
int _getpid()
{
    return 0;
}
void _gettimeofday()
{
    return;
}
