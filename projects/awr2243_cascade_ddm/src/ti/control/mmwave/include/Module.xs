/******************************************************************************
 * FILE PURPOSE: INCLUDE Module specification file.
 ******************************************************************************
 * FILE NAME: module.xs
 *
 * DESCRIPTION:
 *  This file contains the module specification for the package documentation.
 *
 * Copyright (C) 2016, Texas Instruments, Inc.
 *****************************************************************************/

/* Load the library utility. */
var libUtility = xdc.loadCapsule (java.lang.System.getenv("MMWAVE_SDK_INSTALL_PATH") + "/scripts/buildlib.xs");

/**************************************************************************
 * FUNCTION NAME : modBuild
 **************************************************************************
 * DESCRIPTION   :
 *  The function is used to add all the internal module specific include
 *  files to the release package
 **************************************************************************/
function modBuild()
{
    /* Add all the .h files to the release package. */
    var incFiles = libUtility.listAllFiles (".h", "include");
    for (var k = 0 ; k < incFiles.length; k++)
        Pkg.otherFiles[Pkg.otherFiles.length++] = incFiles[k];
}

