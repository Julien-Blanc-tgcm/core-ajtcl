/******************************************************************************
 *
 *
 *  * 
 *    Copyright (c) 2016 Open Connectivity Foundation and AllJoyn Open
 *    Source Project Contributors and others.
 *    
 *    All rights reserved. This program and the accompanying materials are
 *    made available under the terms of the Apache License, Version 2.0
 *    which accompanies this distribution, and is available at
 *    http://www.apache.org/licenses/LICENSE-2.0

 ******************************************************************************/

#include <gtest/gtest.h>
extern "C" {
#include <ajtcl/aj_target.h>
#include <ajtcl/aj_debug.h>
}

/** Main entry point */
int main(int argc, char**argv, char**envArg)
{
    int status = 0;
    setvbuf(stdout, NULL, _IONBF, 0);
    setvbuf(stderr, NULL, _IONBF, 0);

    AJ_AlwaysPrintf(("\n Running ajtcl unit test\n"));
    testing::InitGoogleTest(&argc, argv);
    status = RUN_ALL_TESTS();

    AJ_AlwaysPrintf(("%s exiting with status %d \n", argv[0], status));

    return (int) status;
}