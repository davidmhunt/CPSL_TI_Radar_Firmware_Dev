/*
 *   @file  mmw_cli.c
 *
 *   @brief
 *      Mmw (Milli-meter wave) DEMO CLI Implementation
 *
 *  \par
 *  NOTE:
 *      (C) Copyright 2018-2021 Texas Instruments, Inc.
 *
 *  Redistribution and use in source and binary forms, with or without
 *  modification, are permitted provided that the following conditions
 *  are met:
 *
 *    Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *
 *    Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the
 *    distribution.
 *
 *    Neither the name of Texas Instruments Incorporated nor the names of
 *    its contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 *  THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 *  "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 *  LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 *  A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 *  OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 *  SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 *  LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 *  DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 *  THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 *  OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**************************************************************************
 *************************** Include Files ********************************
 **************************************************************************/

/* Standard Include Files. */
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* MCU + SDK Include Files: */
#include <drivers/uart.h>

/* mmWave SDK Include Files: */
#include <ti/common/syscommon.h>
#include <ti/common/mmwavesdk_version.h>
#include <ti/control/mmwavelink/mmwavelink.h>
#include <ti/utils/cli/cli.h>
#include <ti/utils/mathutils/mathutils.h>

/* Enable 3D Gtracking */
#define GTRACK_3D
#include <ti/alg/gtrack/gtrack.h>

/* Demo Include Files */
#include <ti/demo/am273x/mmw/include/mmw_config.h>
#include <ti/demo/am273x/mmw/mss/mmw_mss.h>
#include <ti/demo/am273x/mmw/mss/mss_tracker.h>
#include <ti/demo/utils/mmwdemo_adcconfig.h>
#include <ti/demo/utils/mmwdemo_rfparser.h>
#ifdef MMWDEMO_TDM
#include <ti/datapath/dpu/cfarproc/cfarproccommon.h>
#endif

/**************************************************************************
 *************************** Local function prototype****************************
 **************************************************************************/

/* CLI Extended Command Functions */
static int32_t MmwDemo_CLICfarCfg (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLISensorStart (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLISensorStop (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIGuiMonSel (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIADCBufCfg (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIAoAFovCfg (int32_t argc, char* argv[]);
#ifdef MMWDEMO_DDM
static int32_t MmwDemo_CLICompressionCfg (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLILocalMaxCfg (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIIntfMitigCfg (int32_t argc, char* argv[]);
#ifndef CASCADE_EVM
static int32_t MmwDemo_CLIAntennaCalibParams (int32_t argc, char* argv[]);
#else
static int32_t MmwDemo_CLIAntennaCalibParams (int32_t argc, char* argv[], uint32_t offset);
static int32_t MmwDemo_CLIAntennaCalibParams1 (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIAntennaCalibParams2 (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIAntennaCalibParams3 (int32_t argc, char* argv[]);
#endif
#endif
#ifdef MMWDEMO_TDM
static int32_t MmwDemo_CLIMultiObjBeamForming (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLICalibDcRangeSig (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIClutterRemoval (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLICompRangeBiasAndRxChanPhaseCfg (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLICfarFovCfg (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIExtendedMaxVelocity (int32_t argc, char* argv[]);
#endif
static int32_t MmwDemo_CLIMeasureRangeBiasAndRxChanPhaseCfg (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIChirpQualityRxSatMonCfg (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIChirpQualitySigImgMonCfg (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIAnalogMonitorCfg (int32_t argc, char* argv[]);
#ifdef LVDS_STREAM
static int32_t MmwDemo_CLILVDSLanesConfig (int32_t argc, char* argv[]);
#endif
static int32_t MmwDemo_CLIConfigDataPort (int32_t argc, char* argv[]);
#ifdef ENET_STREAM
static int32_t MmwDemo_CLIQueryLocalIp (int32_t argc, char* argv[]);
static int32_t MmwDemo_CLIEnetCfg(int32_t argc, char* argv[]);
#endif

/**************************************************************************
 *************************** Tracker Definitions **************************
 **************************************************************************/
#ifdef MMWDEMO_TRACKER_H
/* CLI Extension for the tracker */
static int32_t MmwDemo_CLIInit_Gtrack_extension(CLI_Cfg *cliCfgPtr, uint32_t *cntPtr);

/* RANSAC control constants */
bool gRansacEnabled = false;
float gRansacThresh = 0.0;
uint16_t gRansacIterations = 0;

/* Gtrack control constants */
bool gGtrackEnabled = false;
static bool gGtrackInstanceCreated = false;

/* Global gtrack handle */
void *gHTrackModule;

/* Gtrack configurations */
static GTRACK_sceneryParams gAppSceneryParams;
static GTRACK_gatingParams gAppGatingParams;
static GTRACK_stateParams gAppStateParams;
static GTRACK_allocationParams gAppAllocationParams;
#endif
/**************************************************************************
 *************************** Extern Definitions ***************************
 **************************************************************************/

extern MmwDemo_MSS_MCB    gMmwMssMCB;
extern UART_Params gUartParams[CONFIG_UART_NUM_INSTANCES];

/**************************************************************************
 *************************** Local Definitions ****************************
 **************************************************************************/

#define MMWDEMO_DATAUART_MAX_BAUDRATE_SUPPORTED 3125000

/**************************************************************************
 *************************** CLI  Function Definitions ********************
 **************************************************************************/
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for the sensor start command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLISensorStart (int32_t argc, char* argv[])
{
    bool        doReconfig = true;
    int32_t     retVal = 0;

    /*  Only following command syntax will be supported
        sensorStart
        sensorStart 0
    */
    if (argc == 2)
    {
        doReconfig = (bool) atoi (argv[1]);

        if (doReconfig == true)
        {
            CLI_write ("Error: Reconfig is not supported, only argument of 0 is\n"
                       "(do not reconfig, just re-start the sensor) valid\n");
            return -1;
        }
    }
    else
    {
        /* In case there is no argument for sensorStart, always do reconfig */
        doReconfig = true;
    }

    /***********************************************************************************
     * Do sensor state management to influence the sensor actions
     ***********************************************************************************/

    /* Error checking initial state: no partial config is allowed
       until the first sucessful sensor start state */
    if ((gMmwMssMCB.sensorState == MmwDemo_SensorState_INIT) ||
         (gMmwMssMCB.sensorState == MmwDemo_SensorState_OPENED))
    {
        MMWave_CtrlCfg ctrlCfg;

        /* need to get number of sub-frames so that next function to check
         * pending state can work */
        CLI_getMMWaveExtensionConfig (&ctrlCfg);
        gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.numSubFrames =
            MmwDemo_RFParser_getNumSubFrames(&ctrlCfg);

#ifdef MMWDEMO_TDM
        if (MmwDemo_isAllCfgInPendingState() == 0)
        {
            CLI_write ("Error: Full configuration must be provided before sensor can be started "
                       "the first time\n");

            /* Although not strictly needed, bring back to the initial value since we
             * are rejecting this first time configuration, prevents misleading debug. */
            gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.numSubFrames = 0;

            return -1;
        }
#endif

    }

    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_STARTED)
    {
        CLI_write ("Ignored: Sensor is already started\n");
        return 0;
    }

    if (doReconfig == false)
    {
#ifdef MMWDEMO_TDM
         /* User intends to issue sensor start without config, check if no
            config was issued after stop and generate error if this is the case. */
         if (MmwDemo_isAllCfgInNonPendingState() == 0)
         {
             /* Message user differently if all config was issued or partial config was
                issued. */
             if (MmwDemo_isAllCfgInPendingState())
             {
                 CLI_write ("Error: You have provided complete new configuration, "
                            "issue \"sensorStart\" (without argument) if you want it to "
                            "take effect\n");
             }
             else
             {
                 CLI_write ("Error: You have provided partial configuration between stop and this "
                            "command and partial configuration cannot be undone."
                            "Issue the full configuration and do \"sensorStart\" \n");
             }
             return -1;
         }
#endif
    }
    else
    {
        /* User intends to issue sensor start with full config, check if all config
           was issued after stop and generate error if  is the case. */
        MMWave_CtrlCfg ctrlCfg;

        /* need to get number of sub-frames so that next function to check
         * pending state can work */
        CLI_getMMWaveExtensionConfig (&ctrlCfg);
        gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.numSubFrames =
            MmwDemo_RFParser_getNumSubFrames(&ctrlCfg);

#ifdef MMWDEMO_TDM
        if (MmwDemo_isAllCfgInPendingState() == 0)
        {
            /* Message user differently if no config was issued or partial config was
               issued. */
            if (MmwDemo_isAllCfgInNonPendingState())
            {
                CLI_write ("Error: You have provided no configuration, "
                           "issue \"sensorStart 0\" OR provide "
                           "full configuration and issue \"sensorStart\"\n");
            }
            else
            {
                CLI_write ("Error: You have provided partial configuration between stop and this "
                           "command and partial configuration cannot be undone."
                           "Issue the full configuration and do \"sensorStart\" \n");
            }
            /* Although not strictly needed, bring back to the initial value since we
             * are rejecting this first time configuration, prevents misleading debug. */
            gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.numSubFrames = 0;
            return -1;
        }
#endif
    }

    /***********************************************************************************
     * Retreive and check mmwave Open related config before calling openSensor
     ***********************************************************************************/

    /*  Fill demo's MCB mmWave openCfg structure from the CLI configs*/
    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_INIT)
    {
        /* Get the open configuration: */
        CLI_getMMWaveExtensionOpenConfig (&gMmwMssMCB.cfg.openCfg);
        /* call sensor open */
        retVal = MmwDemo_openSensor(true);
        if(retVal != 0)
        {
            return -1;
        }
        gMmwMssMCB.sensorState = MmwDemo_SensorState_OPENED;
    }
    else
    {
        /* openCfg related configurations like chCfg, lowPowerMode, adcCfg
         * are only used on the first sensor start. If they are different
         * on a subsequent sensor start, then generate a fatal error
         * so the user does not think that the new (changed) configuration
         * takes effect, the board needs to be reboot for the new
         * configuration to be applied.
         */
        MMWave_OpenCfg openCfg;
        CLI_getMMWaveExtensionOpenConfig (&openCfg);
        /* Compare openCfg->chCfg*/
        if(memcmp((void *)&gMmwMssMCB.cfg.openCfg.frontEndCfg[0].chCfg, (void *)&openCfg.frontEndCfg[0].chCfg,
                          sizeof(rlChanCfg_t)) != 0)
        {
            MmwDemo_debugAssert(0);
        }

        /* Compare openCfg->lowPowerMode*/
        if(memcmp((void *)&gMmwMssMCB.cfg.openCfg.lowPowerMode, (void *)&openCfg.lowPowerMode,
                          sizeof(rlLowPowerModeCfg_t)) != 0)
        {
            MmwDemo_debugAssert(0);
        }
        /* Compare openCfg->adcOutCfg*/
        if(memcmp((void *)&gMmwMssMCB.cfg.openCfg.adcOutCfg, (void *)&openCfg.adcOutCfg,
                          sizeof(rlAdcOutCfg_t)) != 0)
        {
            MmwDemo_debugAssert(0);
        }
    }



    /***********************************************************************************
     * Retrieve mmwave Control related config before calling startSensor
     ***********************************************************************************/
    /* Get the mmWave ctrlCfg from the CLI mmWave Extension */
    if(doReconfig)
    {
        /* if MmwDemo_openSensor has non-first time related processing, call here again*/
        /* call sensor config */
        CLI_getMMWaveExtensionConfig (&gMmwMssMCB.cfg.ctrlCfg);
        retVal = MmwDemo_configSensor();
        if(retVal != 0)
        {
            return -1;
        }
    }
    retVal = MmwDemo_startSensor();
    if(retVal != 0)
    {
        return -1;
    }

    /***********************************************************************************
     * Set the state
     ***********************************************************************************/
    gMmwMssMCB.sensorState = MmwDemo_SensorState_STARTED;
    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for the sensor stop command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLISensorStop (int32_t argc, char* argv[])
{
    if ((gMmwMssMCB.sensorState == MmwDemo_SensorState_STOPPED) ||
        (gMmwMssMCB.sensorState == MmwDemo_SensorState_INIT) ||
        (gMmwMssMCB.sensorState == MmwDemo_SensorState_OPENED))
    {
        CLI_write ("Ignored: Sensor is already stopped\n");
        return 0;
    }

    MmwDemo_stopSensor();

    gMmwMssMCB.sensorState = MmwDemo_SensorState_STOPPED;
    return 0;
}

/**
 *  @b Description
 *  @n
 *      Utility function to get sub-frame number
 *
 *  @param[in] argc  Number of arguments
 *  @param[in] argv  Arguments
 *  @param[in] expectedArgc Expected number of arguments
 *  @param[out] subFrameNum Sub-frame Number (0 based)
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIGetSubframe (int32_t argc, char* argv[], int32_t expectedArgc,
                                       int8_t* subFrameNum)
{
    int8_t subframe;

    /* Sanity Check: Minimum argument check */
    if (argc != expectedArgc)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /*Subframe info is always in position 1*/
    subframe = (int8_t) atoi(argv[1]);

    if(subframe >= (int8_t)RL_MAX_SUBFRAMES)
    {
        CLI_write ("Error: Subframe number is invalid\n");
        return -1;
    }

    *subFrameNum = (int8_t)subframe;

    return 0;
}



/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for gui monitoring configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
#ifdef MMWDEMO_TRACKER_H
#define 	GUI_MON_SEL_NUM_ARG 10
#else
#define 	GUI_MON_SEL_NUM_ARG 8
#endif
static int32_t MmwDemo_CLIGuiMonSel (int32_t argc, char* argv[])
{
    MmwDemo_GuiMonSel   guiMonSel;
    int8_t              subFrameNum;

    if(MmwDemo_CLIGetSubframe(argc, argv, GUI_MON_SEL_NUM_ARG, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize the guiMonSel configuration: */
    memset ((void *)&guiMonSel, 0, sizeof(MmwDemo_GuiMonSel));

    /* Populate configuration: */
    guiMonSel.detectedObjects           = atoi (argv[2]);
    guiMonSel.logMagRange               = atoi (argv[3]);
    guiMonSel.noiseProfile              = atoi (argv[4]);
    guiMonSel.rangeAzimuthHeatMap       = atoi (argv[5]);
    guiMonSel.rangeDopplerHeatMap       = atoi (argv[6]);
    guiMonSel.statsInfo                 = atoi (argv[7]);
#ifdef MMWDEMO_TRACKER_H
    guiMonSel.ransacFilterMask          = atoi (argv[8]);
    guiMonSel.trackers                  = atoi (argv[9]);
#endif
    MmwDemo_CfgUpdate((void *)&guiMonSel, MMWDEMO_GUIMONSEL_OFFSET,
        sizeof(MmwDemo_GuiMonSel), subFrameNum);

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for AoA FOV (Field Of View) configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIAoAFovCfg (int32_t argc, char* argv[])
{

#ifdef MMWDEMO_TDM
    DPU_AoAProc_FovAoaCfg   fovCfg;
#elif defined(MMWDEMO_DDM)
    DPC_ObjectDetection_FovAoaCfg   fovCfg;
#endif

    int8_t              subFrameNum;

    if(MmwDemo_CLIGetSubframe(argc, argv, 6, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&fovCfg, 0, sizeof(fovCfg));

    /* Populate configuration: */
    fovCfg.minAzimuthDeg      = (float) atoi (argv[2]);
    fovCfg.maxAzimuthDeg      = (float) atoi (argv[3]);
    fovCfg.minElevationDeg    = (float) atoi (argv[4]);
    fovCfg.maxElevationDeg    = (float) atoi (argv[5]);

    /* Save Configuration to use later */
    MmwDemo_CfgUpdate((void *)&fovCfg, MMWDEMO_FOVAOA_OFFSET,
                      sizeof(fovCfg), subFrameNum);
    return 0;
}


#ifdef MMWDEMO_TDM
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for CFAR configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLICfarCfg (int32_t argc, char* argv[])
{
    DPU_CFARProc_CfarCfg   cfarCfg;
    uint32_t            procDirection;
    int8_t              subFrameNum;
    float               threshold;

    if(MmwDemo_CLIGetSubframe(argc, argv, 10, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&cfarCfg, 0, sizeof(cfarCfg));

    /* Populate configuration: */
    procDirection             = (uint32_t) atoi (argv[2]);
    cfarCfg.averageMode       = (uint8_t) atoi (argv[3]);
    cfarCfg.winLen            = (uint8_t) atoi (argv[4]);
    cfarCfg.guardLen          = (uint8_t) atoi (argv[5]);
    cfarCfg.noiseDivShift     = (uint8_t) atoi (argv[6]);
    cfarCfg.cyclicMode        = (uint8_t) atoi (argv[7]);
    threshold                 = (float) atof (argv[8]);
    cfarCfg.peakGroupingEn    = (uint8_t) atoi (argv[9]);

    if (threshold > 100.0)
    {
        CLI_write("Error: Maximum value for CFAR thresholdScale is 100.0 dB.\n");
        return -1;
    }

    /* threshold is a float value from 0-100dB. It needs to
       be later converted to linear scale (conversion can only be done
       when the number of virtual antennas is known) before passing it
       to CFAR DPU.
       For now, the threshold will be coded in a 16bit integer in the following
       way:
       suppose threshold is a float represented as XYZ.ABC
       it will be saved as a 16bit integer XYZAB
       that is, 2 decimal cases are saved.*/
    threshold = threshold * MMWDEMO_CFAR_THRESHOLD_ENCODING_FACTOR;
    cfarCfg.thresholdScale    = (uint16_t) threshold;

    /* Save Configuration to use later */
    if (procDirection == 0)
    {
        MmwDemo_CfgUpdate((void *)&cfarCfg, MMWDEMO_CFARCFGRANGE_OFFSET,
                          sizeof(cfarCfg), subFrameNum);
    }
    else
    {
        MmwDemo_CfgUpdate((void *)&cfarCfg, MMWDEMO_CFARCFGDOPPLER_OFFSET,
                          sizeof(cfarCfg), subFrameNum);
    }
    return 0;
}
#endif

#ifdef MMWDEMO_DDM
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for CFAR configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLICfarCfg (int32_t argc, char* argv[])
{
    DPU_DopplerProc_CfarCfg   cfarCfg;
    uint32_t            procDirection;
    int8_t              subFrameNum;
    float               threshold;

    if(MmwDemo_CLIGetSubframe(argc, argv, 13, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&cfarCfg, 0, sizeof(cfarCfg));

    /* Populate configuration: */
    procDirection             = (uint32_t) atoi (argv[2]);
    cfarCfg.averageMode       = (uint8_t) atoi (argv[3]);
    cfarCfg.winLen            = (uint8_t) atoi (argv[4]);
    cfarCfg.guardLen          = (uint8_t) atoi (argv[5]);
    cfarCfg.noiseDivShift     = (uint8_t) atoi (argv[6]);
    cfarCfg.cyclicMode        = (uint8_t) atoi (argv[7]);
    threshold                 = (float) atof (argv[8]);
    cfarCfg.peakGroupingEn    = (uint8_t) atoi (argv[9]);
    cfarCfg.osKvalue          = (uint8_t) atoi (argv[10]);
    cfarCfg.osEdgeKscaleEn    = (uint8_t) atoi (argv[11]);
    cfarCfg.isEnabled         = (uint8_t) atoi (argv[12]);

    if (threshold > 100.0)
    {
        CLI_write("Error: Maximum value for CFAR thresholdScale is 100.0 dB.\n");
        return -1;
    }

    /* threshold is a float value from 0-100dB. It needs to
       be later converted to linear scale (conversion can only be done
       when the number of virtual antennas is known) before passing it
       to CFAR DPU.
       For now, the threshold will be coded in a 16bit integer in the following
       way:
       suppose threshold is a float represented as XYZ.ABC
       it will be saved as a 16bit integer XYZAB
       that is, 2 decimal cases are saved.*/
    threshold = threshold * MMWDEMO_CFAR_THRESHOLD_ENCODING_FACTOR;
    cfarCfg.thresholdScale    = (uint16_t) threshold;

    /* Save Configuration to use later */
    if (procDirection == 0)
    {
        MmwDemo_CfgUpdate((void *)&cfarCfg, MMWDEMO_CFARCFGRANGE_OFFSET,
                          sizeof(cfarCfg), subFrameNum);
    }
    else
    {
        if(cfarCfg.isEnabled == 0){
            CLI_write("Error: Doppler CFAR Cannot be disabled.\n");
        }
        MmwDemo_CfgUpdate((void *)&cfarCfg, MMWDEMO_CFARDOPPLERCFG_OFFSET,
                          sizeof(cfarCfg), subFrameNum);
    }
    return 0;
}
#endif

#ifdef MMWDEMO_DDM
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for Compression configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLICompressionCfg (int32_t argc, char* argv[])
{
    DPU_RangeProcHWA_CompressionCfg   compressionCfg;
    int8_t                            subFrameNum;

    if(MmwDemo_CLIGetSubframe(argc, argv, 6, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&compressionCfg, 0, sizeof(compressionCfg));

    /* Populate configuration: */
    compressionCfg.isEnabled              = (bool) atoi (argv[2]);
    compressionCfg.compressionMethod      = (uint8_t) atoi (argv[3]);
    compressionCfg.compressionRatio       = (float) atof (argv[4]);
    compressionCfg.rangeBinsPerBlock      = (uint16_t) atoi (argv[5]);
    /* rxAntennasPerBlock will be fixed to the number of Rx antennas */

    /* is it a power of 2 and greater > 1? */
    if ((compressionCfg.rangeBinsPerBlock <=1)||((compressionCfg.rangeBinsPerBlock & (compressionCfg.rangeBinsPerBlock - 1)) != 0))
    {
        CLI_write("Error: rangeBinsPerBlock should be greater than 1 and a power of 2 \n");
        return -1;
    }

    MmwDemo_CfgUpdate((void *)&compressionCfg, MMWDEMO_COMPRESSIONCFG_OFFSET,
                          sizeof(compressionCfg), subFrameNum);

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for Local Max configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLILocalMaxCfg (int32_t argc, char* argv[])
{

    DPU_DopplerProc_LocalMaxCfg         localMaxCfg;
    int8_t                              subFrameNum;

    if(MmwDemo_CLIGetSubframe(argc, argv, 4, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&localMaxCfg, 0, sizeof(localMaxCfg));

    /* Populate configuration: */
    localMaxCfg.azimThreshold                = (uint16_t) atoi (argv[2]);
    localMaxCfg.dopplerThreshold             = (uint16_t) atoi (argv[3]);

    MmwDemo_CfgUpdate((void *)&localMaxCfg, MMWDEMO_LOCALMAXCFG_OFFSET,
                          sizeof(localMaxCfg), subFrameNum);

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for Interference Mitigation configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIIntfMitigCfg (int32_t argc, char* argv[])
{

    DPU_RangeProcHWADDMA_intfStatsdBCfg  intfStatsdBCfg;
    int8_t                               subFrameNum;

    if(MmwDemo_CLIGetSubframe(argc, argv, 4, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&intfStatsdBCfg, 0, sizeof(intfStatsdBCfg));

    /* Populate configuration: */
    intfStatsdBCfg.intfMitgMagSNRdB               = (uint32_t) atoi (argv[2]);
    intfStatsdBCfg.intfMitgMagDiffSNRdB           = (uint32_t) atoi (argv[3]);

    MmwDemo_CfgUpdate((void *)&intfStatsdBCfg, MMWDEMO_INTFMITIGCFG_OFFSET,
                          sizeof(intfStatsdBCfg), subFrameNum);

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for Antenna Calibration configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
#ifndef  CASCADE_EVM
static int32_t MmwDemo_CLIAntennaCalibParams (int32_t argc, char* argv[])
{

    /*! @brief      Antenna Calbration parameters in Im/Re format */
    float antennaCalibParams[SYS_COMMON_NUM_RX_CHANNEL * SYS_COMMON_NUM_TX_ANTENNAS * 2];
    int32_t argInd, i;

    /* Sanity Check: Minimum argument check */
    if (argc < (1 + SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL*2))
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&antennaCalibParams, 0, sizeof(antennaCalibParams));

    argInd = 1;
    for (i = 0; i < SYS_COMMON_NUM_TX_ANTENNAS * SYS_COMMON_NUM_RX_CHANNEL * 2; i++)
    {
        antennaCalibParams[i] = (float) atof (argv[i+argInd]);
    }

    /* Save Configuration to use later */
    memcpy((void *) &gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.antennaCalibParams,
           &antennaCalibParams, sizeof(antennaCalibParams));

    gMmwMssMCB.objDetCommonCfg.isAntennaCalibParamCfgPending = 1;

    return 0;

}

#else
//#define ALL_ONES_CALIBRATION
//#define CASCADE_CONST_CALIBRATION
#define CLI_DATA_CALIBRATION
uint8_t isAntennaCalibParamCfgPending = 0;

static int32_t MmwDemo_CLIAntennaCalibParams (int32_t argc, char* argv[], uint32_t offset)
{

    int32_t  i;

    /*! @brief      Antenna Calbration parameters in Im/Re format */
#if defined(ALL_ONES_CALIBRATION) || defined(CASCADE_CONST_CALIBRATION)
	float antennaCalibParams[SYS_CASCADE_NUM_RX_CHANNEL * SYS_CASCADE_NUM_TX_ANTENNAS*2];


#ifdef ALL_ONES_CALIBRATION
    for (i = 0; i < SYS_CASCADE_NUM_RX_CHANNEL * SYS_CASCADE_NUM_TX_ANTENNAS; i++)
	{
	        anteannaCalibParams[(2*i)] = 0.0f;
	        antennaCalibParams[(2*i) + 1] = 1.0f;
    }

    
#elif defined(CASCADE_CONST_CALIBRATION)
	antennaCalibParams[0] = 0.596954f;antennaCalibParams[1] = 0.470825f;
	antennaCalibParams[2] = 0.685608f;antennaCalibParams[3] = 0.618286f;
	antennaCalibParams[4] = 0.753876f;antennaCalibParams[5] = 0.449768f;
	antennaCalibParams[6] = 0.747955f;antennaCalibParams[7] = 0.288666f;
	antennaCalibParams[8] = -0.218781f;antennaCalibParams[9] = -0.759186f;
	antennaCalibParams[10] = -0.182404f;antennaCalibParams[11] = -0.807953f;
	antennaCalibParams[12] = -0.069641f;antennaCalibParams[13] = -0.896545f;
	antennaCalibParams[14] = -0.331177f;antennaCalibParams[15] = -0.705078f;
	antennaCalibParams[16] = 0.764679f;antennaCalibParams[17] = 0.093536f;
	antennaCalibParams[18] = 0.934235f;antennaCalibParams[19] = 0.151733f;
	antennaCalibParams[20] = 0.863373f;antennaCalibParams[21] = -0.012390f;
	antennaCalibParams[22] = 0.795166f;antennaCalibParams[23] = -0.122162f;
	antennaCalibParams[24] = -0.539856f;antennaCalibParams[25] = -0.506042f;
	antennaCalibParams[26] = -0.564972f;antennaCalibParams[27] = -0.594452f;
	antennaCalibParams[28] = -0.533844f;antennaCalibParams[29] = -0.698639f;
	antennaCalibParams[30] = -0.591064f;antennaCalibParams[31] = -0.410156f;
	antennaCalibParams[32] = 0.780396f;antennaCalibParams[33] = -0.064606f;
	antennaCalibParams[34] = 0.963562f;antennaCalibParams[35] = -0.071167f;
	antennaCalibParams[36] = 0.907562f;antennaCalibParams[37] = -0.240204f;
	antennaCalibParams[38] = 0.805450f;antennaCalibParams[39] = -0.315399f;
	antennaCalibParams[40] = -0.632172f;antennaCalibParams[41] = -0.400269f;
	antennaCalibParams[42] = -0.680420f;antennaCalibParams[43] = -0.471985f;
	antennaCalibParams[44] = -0.704132f;antennaCalibParams[45] = -0.622314f;
	antennaCalibParams[46] = -0.710876f;antennaCalibParams[47] = -0.311646f;
	antennaCalibParams[48] = 0.565033f;antennaCalibParams[49] = -0.554840f;
	antennaCalibParams[50] = 0.670288f;antennaCalibParams[51] = -0.663635f;
	antennaCalibParams[52] = 0.524445f;antennaCalibParams[53] = -0.738434f;
	antennaCalibParams[54] = 0.411194f;antennaCalibParams[55] = -0.772491f;
	antennaCalibParams[56] = -0.741119f;antennaCalibParams[57] = 0.048706f;
	antennaCalibParams[58] = -0.822876f;antennaCalibParams[59] = 0.068878f;
	antennaCalibParams[60] = -0.891205f;antennaCalibParams[61] = -0.006989f;
	antennaCalibParams[62] = -0.731293f;antennaCalibParams[63] = 0.182220f;
	antennaCalibParams[64] = 0.490234f;antennaCalibParams[65] = -0.714325f;
	antennaCalibParams[66] = 0.583038f;antennaCalibParams[67] = -0.812439f;
	antennaCalibParams[68] = 0.417419f;antennaCalibParams[69] = -0.868683f;
	antennaCalibParams[70] = 0.253021f;antennaCalibParams[71] = -0.898071f;
	antennaCalibParams[72] = -0.815979f;antennaCalibParams[73] = 0.174500f;
	antennaCalibParams[74] = -0.863220f;antennaCalibParams[75] = 0.228394f;
	antennaCalibParams[76] = -0.912628f;antennaCalibParams[77] = 0.129791f;
	antennaCalibParams[78] = -0.893524f;antennaCalibParams[79] = 0.001099f;
	antennaCalibParams[80] = 0.763123f;antennaCalibParams[81] = 0.071075f;
	antennaCalibParams[82] = 0.897400f;antennaCalibParams[83] = 0.078156f;
	antennaCalibParams[84] = 0.883179f;antennaCalibParams[85] = -0.055725f;
	antennaCalibParams[86] = 0.860077f;antennaCalibParams[87] = -0.183807f;
	antennaCalibParams[88] = -0.576630f;antennaCalibParams[89] = -0.533844f;
	antennaCalibParams[90] = -0.629547f;antennaCalibParams[91] = -0.541595f;
	antennaCalibParams[92] = -0.577332f;antennaCalibParams[93] = -0.674591f;
	antennaCalibParams[94] = -0.661865f;antennaCalibParams[95] = -0.435333f;


#endif
	isAntennaCalibParamCfgPending = 7;

	/* Save Configuration to use later */
    memcpy((void *) &gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.antennaCalibParams[0],
    &antennaCalibParams, sizeof(antennaCalibParams));
#else
	/* Each cli command for antennacalib provides 32 parameters. Three such commands are
	 * necessary to fill th 96 parameters necessary for 2 chip cascade board. */
    float antennaCalibParams[32];
	int32_t argInd;


    /* Sanity Check: Minimum argument check */
    if (argc < (1 + 32))
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        CLI_write ("Expected 32 arguments, got %d.\n ", argc-1);
        return -1;
    }
    /* Initialize configuration: */
    argInd = 1;
    for (i = 0; i < (16 * 2); i++)
    {
        antennaCalibParams[i] = (float) atof (argv[i+argInd]);
    }

    isAntennaCalibParamCfgPending |= (1 << offset);
	/* Save Configuration to use later */
    memcpy((void *) &gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.antennaCalibParams[0 + (offset*32)],
    &antennaCalibParams[0], sizeof(antennaCalibParams));
#endif




    if (isAntennaCalibParamCfgPending == 7) //All three messages have come in
    {
    	/* Rearrange Matrix for Virtual Channels  for the Azimuth FFT*/
    	uint8_t arrayToAntMappingAll[SYS_CASCADE_NUM_RX_CHANNEL * SYS_CASCADE_NUM_TX_ANTENNAS] __attribute__((aligned(8)))
														=   { 0, 1, 2, 3, 8, 9, 10, 16,
                                                            17, 18, 19, 24, 25, 26, 27, 32,
                                                            33, 34, 35, 4, 5, 6, 7, 12,
                                                            13, 14, 20, 21, 22, 23, 28, 29,
                                                            30, 31, 36, 37, 38, 39, 0, 0,
                                                            40, 41, 42, 43, 44, 45, 46, 47};
        float * antennaCalibParamsPost = gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.antennaCalibParams;
    	float antennaCalibParamsPre[SYS_CASCADE_NUM_RX_CHANNEL * SYS_CASCADE_NUM_TX_ANTENNAS * 2];
    	/* 1. Copy the existing antenna calib params to antennaCalibParamsPre. */
	    memcpy((void *) antennaCalibParamsPre, antennaCalibParamsPost, sizeof(antennaCalibParamsPre));
    	/* 2. Rearrange the data. */
		// Rearrange the calibration samples to correspond to the order of the
		// virtual Antennas in the two chip cascade board
		for (i = 0; i < SYS_CASCADE_NUM_RX_CHANNEL * SYS_CASCADE_NUM_TX_ANTENNAS; i ++)
		{

			antennaCalibParamsPost[(2*i)] = antennaCalibParamsPre[(2*arrayToAntMappingAll[i])];
			antennaCalibParamsPost[(2*i)+1] = antennaCalibParamsPre[(2*arrayToAntMappingAll[i])+1];
		}

		// Azimuth antenna position 38 and 29 are empty.
		antennaCalibParamsPost[2*38] = 0.0f;
		antennaCalibParamsPost[(2*38)+1] = 0.0f;
		antennaCalibParamsPost[2*39] = 0.0f;
		antennaCalibParamsPost[(2*39)+1] = 0.0f;

		// Indicate that antenna calibration info has been collected.
		gMmwMssMCB.objDetCommonCfg.isAntennaCalibParamCfgPending = 1;
	}
    return 0;

}

static int32_t MmwDemo_CLIAntennaCalibParams1 (int32_t argc, char* argv[])
{

	return MmwDemo_CLIAntennaCalibParams(argc, argv, 0);
}

static int32_t MmwDemo_CLIAntennaCalibParams2 (int32_t argc, char* argv[])
{

	return MmwDemo_CLIAntennaCalibParams(argc, argv, 1);
}


static int32_t MmwDemo_CLIAntennaCalibParams3 (int32_t argc, char* argv[])
{

	return MmwDemo_CLIAntennaCalibParams(argc, argv, 2);
}

#endif
#endif

#ifdef MMWDEMO_TDM
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for CFAR FOV (Field Of View) configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLICfarFovCfg (int32_t argc, char* argv[])
{

    DPU_CFARProc_FovCfg   fovCfg;
    uint32_t            procDirection;
    int8_t              subFrameNum;

    if(MmwDemo_CLIGetSubframe(argc, argv, 5, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&fovCfg, 0, sizeof(fovCfg));

    /* Populate configuration: */
    procDirection             = (uint32_t) atoi (argv[2]);
    fovCfg.min                = (float) atof (argv[3]);
    fovCfg.max                = (float) atof (argv[4]);

    /* Save Configuration to use later */
    if (procDirection == 0)
    {
        MmwDemo_CfgUpdate((void *)&fovCfg, MMWDEMO_FOVRANGE_OFFSET,
                          sizeof(fovCfg), subFrameNum);
    }
    else
    {
        MmwDemo_CfgUpdate((void *)&fovCfg, MMWDEMO_FOVDOPPLER_OFFSET,
                          sizeof(fovCfg), subFrameNum);
    }
    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for extended maximum velocity configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIExtendedMaxVelocity (int32_t argc, char* argv[])
{
    DPU_AoAProc_ExtendedMaxVelocityCfg   cfg;
    int8_t              subFrameNum;

    if(MmwDemo_CLIGetSubframe(argc, argv, 3, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&cfg, 0, sizeof(cfg));

    /* Populate configuration: */
    cfg.enabled      = (uint8_t) atoi (argv[2]);

    /* Save Configuration to use later */
    MmwDemo_CfgUpdate((void *)&cfg, MMWDEMO_EXTMAXVEL_OFFSET,
                      sizeof(cfg), subFrameNum);
    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for multi object beam forming configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIMultiObjBeamForming (int32_t argc, char* argv[])
{
    DPU_AoAProc_MultiObjBeamFormingCfg cfg;
    int8_t              subFrameNum;

    if(MmwDemo_CLIGetSubframe(argc, argv, 4, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&cfg, 0, sizeof(cfg));

    /* Populate configuration: */
    cfg.enabled                     = (uint8_t) atoi (argv[2]);
    cfg.multiPeakThrsScal           = (float) atof (argv[3]);

    /* Save Configuration to use later */
    MmwDemo_CfgUpdate((void *)&cfg, MMWDEMO_MULTIOBJBEAMFORMING_OFFSET,
                      sizeof(cfg), subFrameNum);

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for DC range calibration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLICalibDcRangeSig (int32_t argc, char* argv[])
{
    DPU_RangeProc_CalibDcRangeSigCfg cfg;
    uint32_t                   log2NumAvgChirps;
    int8_t                     subFrameNum;

    if(MmwDemo_CLIGetSubframe(argc, argv, 6, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize configuration for DC range signature calibration */
    memset ((void *)&cfg, 0, sizeof(cfg));

    /* Populate configuration: */
    cfg.enabled          = (uint16_t) atoi (argv[2]);
    cfg.negativeBinIdx   = (int16_t)  atoi (argv[3]);
    cfg.positiveBinIdx   = (int16_t)  atoi (argv[4]);
    cfg.numAvgChirps     = (uint16_t) atoi (argv[5]);

    if (cfg.negativeBinIdx > 0)
    {
        CLI_write ("Error: Invalid negative bin index\n");
        return -1;
    }
    if (cfg.positiveBinIdx < 0)
    {
        CLI_write ("Error: Invalid positive bin index\n");
        return -1;
    }
    if ((cfg.positiveBinIdx - cfg.negativeBinIdx + 1) > DPU_RANGEPROC_SIGNATURE_COMP_MAX_BIN_SIZE)
    {
        CLI_write ("Error: Number of bins exceeds the limit\n");
        return -1;
    }
    log2NumAvgChirps = (uint32_t) mathUtils_ceilLog2(cfg.numAvgChirps);
    if (cfg.numAvgChirps != (1U << log2NumAvgChirps))
    {
        CLI_write ("Error: Number of averaged chirps is not power of two\n");
        return -1;
    }

    /* Save Configuration to use later */
    MmwDemo_CfgUpdate((void *)&cfg, MMWDEMO_CALIBDCRANGESIG_OFFSET,
                      sizeof(cfg), subFrameNum);

    return 0;
}

/**
 *  @b Description
 *  @n
 *      Clutter removal Configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIClutterRemoval (int32_t argc, char* argv[])
{
    DPC_ObjectDetection_StaticClutterRemovalCfg_Base cfg;
    int8_t              subFrameNum;

    if(MmwDemo_CLIGetSubframe(argc, argv, 3, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize configuration for clutter removal */
    memset ((void *)&cfg, 0, sizeof(cfg));

    /* Populate configuration: */
    cfg.enabled          = (uint16_t) atoi (argv[2]);

    /* Save Configuration to use later */
    MmwDemo_CfgUpdate((void *)&cfg, MMWDEMO_STATICCLUTTERREMOFVAL_OFFSET,
                      sizeof(cfg), subFrameNum);

    return 0;
}
#endif

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for data logger set command
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIADCBufCfg (int32_t argc, char* argv[])
{
    MmwDemo_ADCBufCfg   adcBufCfg;
    int8_t              subFrameNum;

    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_STARTED)
    {
        CLI_write ("Ignored: This command is not allowed after sensor has started\n");
        return 0;
    }

    if(MmwDemo_CLIGetSubframe(argc, argv, 6, &subFrameNum) < 0)
    {
        return -1;
    }

    /* Initialize the ADC Output configuration: */
    memset ((void *)&adcBufCfg, 0, sizeof(adcBufCfg));

    /* Populate configuration: */
    adcBufCfg.adcFmt          = (uint8_t) atoi (argv[2]);
    adcBufCfg.iqSwapSel       = (uint8_t) atoi (argv[3]);
    adcBufCfg.chInterleave    = (uint8_t) atoi (argv[4]);
    adcBufCfg.chirpThreshold  = (uint8_t) atoi (argv[5]);

    /* This demo is using HWA for 1D processing which does not allow multi-chirp
     * processing */
    if (adcBufCfg.chirpThreshold != 1)
    {
        CLI_write("Error: chirpThreshold must be 1, multi-chirp is not allowed\n");
        return -1;
    }

    /* Save Configuration to use later */
    MmwDemo_CfgUpdate((void *)&adcBufCfg,
                      MMWDEMO_ADCBUFCFG_OFFSET,
                      sizeof(MmwDemo_ADCBufCfg), subFrameNum);
    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for measurement configuration of range bias
 *      and channel phase offsets
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIMeasureRangeBiasAndRxChanPhaseCfg (int32_t argc, char* argv[])
{
    DPC_ObjectDetection_MeasureRxChannelBiasCfg   cfg;

    /* Sanity Check: Minimum argument check */
    if (argc != 4)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&cfg, 0, sizeof(cfg));

    /* Populate configuration: */
    cfg.enabled          = (uint8_t) atoi (argv[1]);
    cfg.targetDistance   = (float) atof (argv[2]);
    cfg.searchWinSize   = (float) atof (argv[3]);

    /* Save Configuration to use later */
    memcpy((void *) &gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.measureRxChannelBiasCfg,
           &cfg, sizeof(cfg));

    gMmwMssMCB.objDetCommonCfg.isMeasureRxChannelBiasCfgPending = 1;

    return 0;
}

#ifdef MMWDEMO_TDM
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for compensation of range bias and channel phase offsets
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLICompRangeBiasAndRxChanPhaseCfg (int32_t argc, char* argv[])
{
    DPU_AoAProc_compRxChannelBiasCfg   cfg;
    int32_t Re, Im;
    int32_t argInd;
    int32_t i;

    /* Sanity Check: Minimum argument check */
    if (argc != (1+1+SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL*2))
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&cfg, 0, sizeof(cfg));

    /* Populate configuration: */
    cfg.rangeBias          = (float) atof (argv[1]);

    argInd = 2;
    for (i=0; i < SYS_COMMON_NUM_TX_ANTENNAS*SYS_COMMON_NUM_RX_CHANNEL; i++)
    {
        Re = (int32_t) (atof (argv[argInd++]) * 32768.);
        MATHUTILS_SATURATE16(Re);
        cfg.rxChPhaseComp[i].real = (int16_t) Re;

        Im = (int32_t) (atof (argv[argInd++]) * 32768.);
        MATHUTILS_SATURATE16(Im);
        cfg.rxChPhaseComp[i].imag = (int16_t) Im;

    }
    /* Save Configuration to use later */
    memcpy((void *) &gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.compRxChanCfg,
           &cfg, sizeof(cfg));

    gMmwMssMCB.objDetCommonCfg.isCompRxChannelBiasCfgPending = 1;

    return 0;
}


#endif

#ifdef LVDS_STREAM
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for configuring number of LVDS lanes for streaming ADC data.
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLILVDSLanesConfig (int32_t argc, char* argv[])
{
    uint32_t numLVDSlanes = 0;

    /* Sanity Check: Minimum argument check */
    if (argc != 2)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Populate configuration: */
    numLVDSlanes          = (uint32_t) atoi (argv[1]);

    /* If both h/w and s/w are enabled, HSI header must be enabled, because
     * we don't allow mixed h/w session without HSI header
     * simultaneously with s/w session with HSI header (s/w session always
     * streams HSI header) */

    /* Valid argument for number of LVDS lanes are 0x3U (2-Lane) and
     * 0xFU (4-Lane).
     * Report error if user argument is not valid. */
    if ((numLVDSlanes != 0x3U) && (numLVDSlanes != 0xFU))
    {
        CLI_write("Error: Valid number of lanes are 3 (2-lane) and 15 (4-lane).\n");
        return -1;
    }

    gMmwMssMCB.objDetCommonCfg.preStartCommonCfg.numLVDSLanes = numLVDSlanes;

    gMmwMssMCB.objDetCommonCfg.isLVDSLaneCfgPending = 1;

    return 0;
}
#endif

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for configuring CQ RX Saturation monitor
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIChirpQualityRxSatMonCfg (int32_t argc, char* argv[])
{
    rlRxSatMonConf_t        cqSatMonCfg;

    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_STARTED)
    {
        CLI_write ("Ignored: This command is not allowed after sensor has started\n");
        return 0;
    }

    /* Sanity Check: Minimum argument check */
    if (argc != 6)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&cqSatMonCfg, 0, sizeof(rlRxSatMonConf_t));

    /* Populate configuration: */
    cqSatMonCfg.profileIndx                 = (uint8_t) atoi (argv[1]);

    if(cqSatMonCfg.profileIndx < RL_MAX_PROFILES_CNT)
    {

        cqSatMonCfg.satMonSel                   = (uint8_t) atoi (argv[2]);
        cqSatMonCfg.primarySliceDuration        = (uint16_t) atoi (argv[3]);
        cqSatMonCfg.numSlices                   = (uint16_t) atoi (argv[4]);
        cqSatMonCfg.rxChannelMask               = (uint8_t) atoi (argv[5]);

        /* Save Configuration to use later */
        gMmwMssMCB.cqSatMonCfg[cqSatMonCfg.profileIndx] = cqSatMonCfg;

        return 0;
    }
    else
    {
        return -1;
    }
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for configuring CQ Signal & Image band monitor
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIChirpQualitySigImgMonCfg (int32_t argc, char* argv[])
{
    rlSigImgMonConf_t       cqSigImgMonCfg;

    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_STARTED)
    {
        CLI_write ("Ignored: This command is not allowed after sensor has started\n");
        return 0;
    }

    /* Sanity Check: Minimum argument check */
    if (argc != 4)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Initialize configuration: */
    memset ((void *)&cqSigImgMonCfg, 0, sizeof(rlSigImgMonConf_t));

    /* Populate configuration: */
    cqSigImgMonCfg.profileIndx              = (uint8_t) atoi (argv[1]);

    if(cqSigImgMonCfg.profileIndx < RL_MAX_PROFILES_CNT)
    {
        cqSigImgMonCfg.numSlices            = (uint8_t) atoi (argv[2]);
        cqSigImgMonCfg.timeSliceNumSamples  = (uint16_t) atoi (argv[3]);

        /* Save Configuration to use later */
        gMmwMssMCB.cqSigImgMonCfg[cqSigImgMonCfg.profileIndx] = cqSigImgMonCfg;

        return 0;
    }
    else
    {
        return -1;
    }
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for enabling analog monitors
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIAnalogMonitorCfg (int32_t argc, char* argv[])
{
    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_STARTED)
    {
        CLI_write ("Ignored: This command is not allowed after sensor has started\n");
        return 0;
    }

    /* Sanity Check: Minimum argument check */
    if (argc != 3)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Save Configuration to use later */
    gMmwMssMCB.anaMonCfg.rxSatMonEn = atoi (argv[1]);
    gMmwMssMCB.anaMonCfg.sigImgMonEn = atoi (argv[2]);
    gMmwMssMCB.isAnaMonCfgPending = 1;

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for configuring the data port
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIConfigDataPort (int32_t argc, char* argv[])
{
    uint32_t baudrate;
    bool  ackPing;
    uint8_t ackData[16];
    UART_Transaction trans;

    UART_Transaction_init(&trans);

    trans.buf   = &ackData[0U];
    trans.count = sizeof(ackData);

    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_STARTED)
    {
        CLI_write ("Ignored: This command is not allowed after sensor has started\n");
        return 0;
    }

    /* Populate configuration: */
    baudrate = (uint32_t) atoi(argv[1]);
    ackPing = (bool) atoi(argv[2]);

    /* check if requested value is less than max supported value */
    if (baudrate > MMWDEMO_DATAUART_MAX_BAUDRATE_SUPPORTED)
    {
        CLI_write ("Ignored: Invalid baud rate (%d) specified\n",baudrate);
        return 0;
    }

    UART_close(gUartHandle[CONFIG_UART1]);
    gUartHandle[CONFIG_UART1] = NULL;

    gUartParams[CONFIG_UART1].baudRate = (uint32_t) atoi(argv[1]);

    gUartHandle[CONFIG_UART1] = UART_open(CONFIG_UART1, &gUartParams[CONFIG_UART1]);
    if(NULL == gUartHandle[CONFIG_UART1])
    {
        DebugP_logError("UART open failed for instance %d !!!\r\n", CONFIG_UART1);
        return 0;
    }

    gMmwMssMCB.loggingUartHandle = gUartHandle[CONFIG_UART1];

    /* regardless of baud rate update, ack back to the host over this UART
       port if handle is valid and user has requested the ack back */
    if ((gMmwMssMCB.loggingUartHandle != NULL) && (ackPing == true))
    {
        memset(ackData,0xFF,sizeof(ackData));
        UART_write(gMmwMssMCB.loggingUartHandle, &trans);
    }

    return 0;
}





/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for querying Demo status
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIQueryDemoStatus (int32_t argc, char* argv[])
{
    CLI_write ("Sensor State: %d\n",gMmwMssMCB.sensorState);
    CLI_write ("Data port baud rate: %d\n",gMmwMssMCB.cfg.platformCfg.loggingBaudRate);

    return 0;
}

#ifdef ENET_STREAM
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for querying Local IP
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIQueryLocalIp (int32_t argc, char* argv[])
{
    if(gMmwMssMCB.enetCfg.status == 1){
        CLI_write ("Local IP is: %s\n", ip4addr_ntoa((const ip4_addr_t *)&gMmwMssMCB.enetCfg.localIp));
    }
    else{
        CLI_write ("Local IP is not up yet !!\n");
    }

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for ethernet configuration
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIEnetCfg(int32_t argc, char* argv[])
{

    volatile uint32_t remoteIp[4] = {0};
    uint8_t idx;

    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_STARTED)
    {
        CLI_write ("Ignored: This command is not allowed after sensor has started\n");
        return 0;
    }

    /* Sanity Check: Minimum argument check */
    if (argc != 6)
    {
        CLI_write ("Error: Invalid usage of the CLI command\n");
        return -1;
    }

    /* Populate configuration: */
    gMmwMssMCB.enetCfg.streamEnable = (bool) atoi(argv[1]);
    /* Get the IP Address */
    for(idx = 0; idx < 4; idx++){
        remoteIp[idx] = (uint32_t)atoi(argv[idx+2]);
    }
    /* Populate the IP Address */
    gMmwMssMCB.enetCfg.remoteIp = (ip_addr_t) IPADDR4_INIT_BYTES(remoteIp[0],remoteIp[1],remoteIp[2],remoteIp[3]);
    CLI_write("Remote IP Address is %s\n", ip4addr_ntoa(&gMmwMssMCB.enetCfg.remoteIp));

    if(gMmwMssMCB.enetCfg.streamEnable){
        MmwDemo_mssEnetCfgDone();
    }

    return 0;
}
#endif

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler for save/restore calibration data to/from flash
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLICalibDataSaveRestore(int32_t argc, char* argv[])
{
    if (gMmwMssMCB.sensorState == MmwDemo_SensorState_STARTED)
    {
        CLI_write ("Ignored: This command is not allowed after sensor has started\n");
        return 0;
    }

    /* Validate inputs */
    if ( ((uint32_t) atoi(argv[1]) == 1) && ((uint32_t) atoi(argv[2] ) == 1))
    {
        CLI_write ("Error: Save and Restore can be enabled only one at a time\n");
        return -1;
    }

    /* Populate configuration: */
    gMmwMssMCB.calibCfg.saveEnable = (uint32_t) atoi(argv[1]);
    gMmwMssMCB.calibCfg.restoreEnable = (uint32_t) atoi(argv[2]);
    sscanf(argv[3], "0x%x", &gMmwMssMCB.calibCfg.flashOffset);

    gMmwMssMCB.isCalibCfgPending = 1;

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler to send out the processing chain type
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIProcChain(int32_t argc, char* argv[])
{

#ifdef MMWDEMO_DDM
        CLI_write ("ProcChain: DDM\n");
#elif defined(MMWDEMO_TDM)
        CLI_write ("ProcChain: TDM\n");
#endif

    return 0;

}

#ifdef MMWDEMO_TRACKER_H
/**
 *  @b Description
 *  @n
 *      This is the CLI Handler to enable/disable ransac
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIRansac(int32_t argc, char* argv[])
{
    if (argc != 4){
        return -1;
    }

    gRansacEnabled = atoi(argv[1]);
    gRansacIterations = atoi(argv[2]);
    gRansacThresh = atof(argv[3]);
    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler to configure and create gtrack
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIGtrackCfg(int32_t argc, char* argv[])
{
    if(argc != 11){
        return -1;
    }

    uint16_t enable;
    int32_t errCode = 0;
    GTRACK_moduleConfig config;
    GTRACK_advancedParameters advParams;

    enable = atoi(argv[1]);

    /* Delete gtrack instance if it already exists */
    if(gGtrackInstanceCreated){
        gtrack_delete(gHTrackModule);
        gHTrackModule = NULL;
        gGtrackInstanceCreated = false;
    }

    if(!enable){
        gGtrackEnabled = false;
        return 0;
    }
    else{
        /* Create gtrack instance */
        memset((void *)&config, 0, sizeof(GTRACK_moduleConfig));
        memset((void *)&advParams, 0, sizeof(GTRACK_advancedParameters));

        config.stateVectorType = GTRACK_STATE_VECTORS_3DA;
        config.verbose = GTRACK_VERBOSE_NONE;

        config.maxNumPoints = atoi(argv[2]);
        config.maxNumTracks = atoi(argv[3]);
        config.initialRadialVelocity = atof(argv[4]);
        config.maxRadialVelocity = atof(argv[5]);
        config.radialVelocityResolution = atof(argv[6]);
        config.maxAcceleration[0] = atof(argv[7]);
        config.maxAcceleration[1] = atof(argv[8]);
        config.maxAcceleration[2] = atof(argv[9]);
        config.deltaT = atof(argv[10]);

        advParams.allocationParams = &gAppAllocationParams;
        advParams.gatingParams = &gAppGatingParams;
        advParams.stateParams = &gAppStateParams;
        advParams.sceneryParams = &gAppSceneryParams;

        config.advParams = &advParams;
        gHTrackModule = gtrack_create(&config, &errCode);
        if(gHTrackModule == NULL){
            test_print("Gtrack creation error with code : %d\n", errCode);
            MmwDemo_debugAssert(0);
            return -1;
        }

        gGtrackInstanceCreated = true;
        gGtrackEnabled = true;
        return 0;
    }
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler to configure app scenery parameters for gtrack
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIGAppSceneryParams(int32_t argc, char* argv[])
{
    if(argc != 12){
        return -1;
    }

    memset(&gAppSceneryParams, 0, sizeof(gAppSceneryParams));

    gAppSceneryParams.sensorPosition.x = atof(argv[1]);
    gAppSceneryParams.sensorPosition.y = atof(argv[2]);
    gAppSceneryParams.sensorPosition.z = atof(argv[3]);

    gAppSceneryParams.sensorOrientation.azimTilt = atof(argv[4]);
    gAppSceneryParams.sensorOrientation.elevTilt = atof(argv[5]);

    gAppSceneryParams.numBoundaryBoxes = 1;
    gAppSceneryParams.boundaryBox[0].x1 = atof(argv[6]);
    gAppSceneryParams.boundaryBox[0].x2 = atof(argv[7]);
    gAppSceneryParams.boundaryBox[0].y1 = atof(argv[8]);
    gAppSceneryParams.boundaryBox[0].y2 = atof(argv[9]);
    gAppSceneryParams.boundaryBox[0].z1 = atof(argv[10]);
    gAppSceneryParams.boundaryBox[0].z2 = atof(argv[11]);

    gAppSceneryParams.numStaticBoxes = 0;

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler to configure app gating parameters for gtrack
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIGAppGatingParams(int32_t argc, char* argv[])
{
    if(argc != 6){
        return -1;
    }

    memset(&gAppGatingParams, 0, sizeof(gAppGatingParams));

    gAppGatingParams.gain = atof(argv[1]);
    gAppGatingParams.limitsArray[0] = atof(argv[2]);
    gAppGatingParams.limitsArray[1] = atof(argv[3]);
    gAppGatingParams.limitsArray[2] = atof(argv[4]);
    gAppGatingParams.limitsArray[3] = atof(argv[5]);

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI Handler to configure app state parameters for gtrack
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIGAppStateParams(int32_t argc, char* argv[])
{
    if(argc != 7){
        return -1;
    }

    memset(&gAppStateParams, 0, sizeof(gAppStateParams));

    gAppStateParams.det2actThre = atoi(argv[1]);
    gAppStateParams.det2freeThre = atoi(argv[2]);
    gAppStateParams.active2freeThre = atoi(argv[3]);
    gAppStateParams.static2freeThre = atoi(argv[4]);
    gAppStateParams.exit2freeThre = atoi(argv[5]);
    gAppStateParams.sleep2freeThre = atoi(argv[6]);

    return 0;
}


/**
 *  @b Description
 *  @n
 *      This is the CLI Handler to configure app alloc parameters for gtrack
 *
 *  @param[in] argc
 *      Number of arguments
 *  @param[in] argv
 *      Arguments
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_CLIGAppAllocParams(int32_t argc, char* argv[])
{
    if(argc != 7){
        return -1;
    }

    memset(&gAppAllocationParams, 0, sizeof(gAppAllocationParams));

    gAppAllocationParams.snrThre = atof(argv[1]);
    gAppAllocationParams.snrThreObscured = atof(argv[2]);
    gAppAllocationParams.velocityThre = atof(argv[3]);
    gAppAllocationParams.pointsThre = atoi(argv[4]);
    gAppAllocationParams.maxDistanceThre = atof(argv[5]);
    gAppAllocationParams.maxVelThre = atof(argv[6]);

    return 0;
}

/**
 *  @b Description
 *  @n
 *      This is the CLI extension to configure various tracker configurations
 *
 *  @param[in] cliCfgPtr
 *      CLI Cfg handle
 *  @param[in] cntPtr
 *      Count of CLI configs
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
int32_t MmwDemo_CLIInit_Gtrack_extension(CLI_Cfg *cliCfgPtr, uint32_t *cntPtr){

    /* Dereference the pointer values */
    uint32_t cnt = *cntPtr;
    CLI_Cfg cliCfg = *cliCfgPtr;

    cliCfg.tableEntry[cnt].cmd            = "ransac";
    cliCfg.tableEntry[cnt].helpString     = "<enable> <iterations> <thresh>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIRansac;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "gtrack";
    cliCfg.tableEntry[cnt].helpString     = "<enable> <maxNumPoints> <maxNumTracks> <initialRadialVelocity> <maxRadialVelocity> <radialVelocityResolution> <maxAcceleration0> <maxAcceleration1> <maxAcceleration2> <deltaT>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIGtrackCfg;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "appSceneryParams";
    cliCfg.tableEntry[cnt].helpString     = "<sensorPosX> <sensorPosY> <sensorPosZ> <orientationAzim> <orientationElev> <bBoxX1> <bBoxX2> <bBoxY1> <bBoxY2> <bBoxZ1> <bBoxZ2>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIGAppSceneryParams;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "appGatingParams";
    cliCfg.tableEntry[cnt].helpString     = "<gain> <limitsArr0> <limitsArr1> <limitsArr2> <limitsArr3>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIGAppGatingParams;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "appStateParams";
    cliCfg.tableEntry[cnt].helpString     = "<det2actThre> <det2freeThre> <active2freeThre> <static2freeThre> <exit2freeThre> <sleep2freeThre>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIGAppStateParams;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "appAllocParams";
    cliCfg.tableEntry[cnt].helpString     = "<snrThre> <snrThreObscured> <velocityThre> <pointsThre> <maxDistanceThre> <maxVelThre>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIGAppAllocParams;
    cnt++;

    /* Propogate changed values to calling function */
    *cntPtr = cnt;
    *cliCfgPtr = cliCfg;

    return 0;
}
#endif

/**
 *  @b Description
 *  @n
 *      This is the CLI Execution Task
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_CLIInit (uint8_t taskPriority)
{
    CLI_Cfg     cliCfg;
    char        demoBanner[256];
    uint32_t    cnt;

    /* Create Demo Banner to be printed out by CLI */
    sprintf(&demoBanner[0],
                       "******************************************\r\n" \
                       "AM273X Two Chip Cascade Demo %02d.%02d.%02d.%02d\r\n"  \
                       "******************************************\r\n",
                        MMWAVE_SDK_VERSION_MAJOR,
                        MMWAVE_SDK_VERSION_MINOR,
                        MMWAVE_SDK_VERSION_BUGFIX,
                        MMWAVE_SDK_VERSION_BUILD
            );

    /* Initialize the CLI configuration: */
    memset ((void *)&cliCfg, 0, sizeof(CLI_Cfg));

    /* Populate the CLI configuration: */
    cliCfg.cliPrompt                    = "mmwDemo:/>";
    cliCfg.cliBanner                    = demoBanner;
    cliCfg.cliUartHandle                = gMmwMssMCB.commandUartHandle;
    cliCfg.taskPriority                 = taskPriority;
    cliCfg.mmWaveHandle                 = gMmwMssMCB.ctrlHandle;
    cliCfg.enableMMWaveExtension        = 1U;
    cliCfg.usePolledMode                = true;
    cliCfg.overridePlatform             = true;
    cliCfg.overridePlatformString       = "AM273X";
#ifdef MMWDEMO_DDM
    cliCfg.procChain                    = 1;
#else
    cliCfg.procChain                    = 0;
#endif

    cnt=0;
    cliCfg.tableEntry[cnt].cmd            = "sensorStart";
    cliCfg.tableEntry[cnt].helpString     = "[doReconfig(optional, default:enabled)]";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLISensorStart;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "sensorStop";
    cliCfg.tableEntry[cnt].helpString     = "No arguments";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLISensorStop;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "guiMonitor";
#ifdef MMWDEMO_TRACKER_H
	cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <detectedObjects> <logMagRange> <noiseProfile> <rangeAzimuthHeatMap> <rangeDopplerHeatMap> <statsInfo> <ransacFilterMask> <trackers>";
#else
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <detectedObjects> <logMagRange> <noiseProfile> <rangeAzimuthHeatMap> <rangeDopplerHeatMap> <statsInfo>";
#endif
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIGuiMonSel;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "cfarCfg";
#ifdef MMWDEMO_TDM
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <procDirection> <averageMode> <winLen> <guardLen> <noiseDiv> <cyclicMode> <thresholdScale> <peakGroupingEn>";
#endif
#ifdef MMWDEMO_DDM
	cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <procDirection> <averageMode> <winLen> <guardLen> <noiseDiv> <cyclicMode> <thresholdScale> <peakGroupingEn> <osKvalue> <osEdgeKscaleEn> <isEnabled>";
#endif
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLICfarCfg;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "aoaFovCfg";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <minAzimuthDeg> <maxAzimuthDeg> <minElevationDeg> <maxElevationDeg>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIAoAFovCfg;
    cnt++;

#ifdef MMWDEMO_TDM
    cliCfg.tableEntry[cnt].cmd            = "multiObjBeamForming";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <enabled> <threshold>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIMultiObjBeamForming;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "calibDcRangeSig";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <enabled> <negativeBinIdx> <positiveBinIdx> <numAvgFrames>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLICalibDcRangeSig;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "clutterRemoval";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <enabled>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIClutterRemoval;
    cnt++;
#endif

    cliCfg.tableEntry[cnt].cmd            = "adcbufCfg";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <adcOutputFmt> <SampleSwap> <ChanInterleave> <ChirpThreshold>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIADCBufCfg;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "measureRangeBiasAndRxChanPhase";
    cliCfg.tableEntry[cnt].helpString     = "<enabled> <targetDistance> <searchWin>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIMeasureRangeBiasAndRxChanPhaseCfg;
    cnt++;

#ifdef MMWDEMO_TDM
    cliCfg.tableEntry[cnt].cmd            = "compRangeBiasAndRxChanPhase";
    cliCfg.tableEntry[cnt].helpString     = "<rangeBias> <Re00> <Im00> <Re01> <Im01> <Re02> <Im02> <Re03> <Im03> <Re10> <Im10> <Re11> <Im11> <Re12> <Im12> <Re13> <Im13> ";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLICompRangeBiasAndRxChanPhaseCfg;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "cfarFovCfg";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <procDirection> <min (meters or m/s)> <max (meters or m/s)>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLICfarFovCfg;
    cnt++;
    cliCfg.tableEntry[cnt].cmd            = "extendedMaxVelocity";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <enabled>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIExtendedMaxVelocity;
    cnt++;
#endif

    cliCfg.tableEntry[cnt].cmd            = "CQRxSatMonitor";
    cliCfg.tableEntry[cnt].helpString     = "<profile> <satMonSel> <priSliceDuration> <numSlices> <rxChanMask>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIChirpQualityRxSatMonCfg;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "CQSigImgMonitor";
    cliCfg.tableEntry[cnt].helpString     = "<profile> <numSlices> <numSamplePerSlice>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIChirpQualitySigImgMonCfg;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "analogMonitor";
    cliCfg.tableEntry[cnt].helpString     = "<rxSaturation> <sigImgBand>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIAnalogMonitorCfg;
    cnt++;

#ifdef LVDS_STREAM
    cliCfg.tableEntry[cnt].cmd            = "numLvdsLanesCfg";
    cliCfg.tableEntry[cnt].helpString     = "<numLVDSLanes>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLILVDSLanesConfig;
    cnt++;
#endif

    cliCfg.tableEntry[cnt].cmd            = "configDataPort";
    cliCfg.tableEntry[cnt].helpString     = "<baudrate> <ackPing>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIConfigDataPort;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "queryDemoStatus";
    cliCfg.tableEntry[cnt].helpString     = "";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIQueryDemoStatus;
    cnt++;

#ifdef ENET_STREAM
    cliCfg.tableEntry[cnt].cmd            = "queryLocalIp";
    cliCfg.tableEntry[cnt].helpString     = "";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIQueryLocalIp;
    cnt++;
#endif

    cliCfg.tableEntry[cnt].cmd            = "calibData";
    cliCfg.tableEntry[cnt].helpString    = "<save enable> <restore enable> <Flash offset>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLICalibDataSaveRestore;
    cnt++;

#ifdef ENET_STREAM
    cliCfg.tableEntry[cnt].cmd            = "enetStreamCfg";
    cliCfg.tableEntry[cnt].helpString     = "<isEnabled> <remoteIpD> <remoteIpC> <remoteIpB> <remoteIpA>"; /* Ip: D.C.B.A */
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIEnetCfg;
    cnt++;
#endif

#ifdef MMWDEMO_DDM
    cliCfg.tableEntry[cnt].cmd            = "compressionCfg";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <isEnabled> <compressionMethod> <compressionRatio> <rangeBinsPerBlock>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLICompressionCfg;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "localMaxCfg";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx> <azimThreshdB> <dopplerThreshdB>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLILocalMaxCfg;
    cnt++;

    cliCfg.tableEntry[cnt].cmd            = "intfMitigCfg";
    cliCfg.tableEntry[cnt].helpString     = "<subFrameIdx>  <magSNRdB> <magDiffSNRdB>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIIntfMitigCfg;
    cnt++;

#ifndef CASCADE_EVM
    cliCfg.tableEntry[cnt].cmd            = "antennaCalibParams";
    cliCfg.tableEntry[cnt].helpString     = "<Q0> <I0> .... <Q15> <I15>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIAntennaCalibParams;
    cnt++;
#else
    cliCfg.tableEntry[cnt].cmd            = "antennaCalibParams1";
    cliCfg.tableEntry[cnt].helpString     = "<Q0> <I0> .... <Q15> <I15>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIAntennaCalibParams1;
    cnt++;
	cliCfg.tableEntry[cnt].cmd            = "antennaCalibParams2";
    cliCfg.tableEntry[cnt].helpString     = "<Q16> <I16> .... <Q31> <I31>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIAntennaCalibParams2;
    cnt++;
	cliCfg.tableEntry[cnt].cmd            = "antennaCalibParams3";
    cliCfg.tableEntry[cnt].helpString     = "<Q32> <I32> .... <Q47> <I47>";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIAntennaCalibParams3;
    cnt++;

#endif
#endif
    cliCfg.tableEntry[cnt].cmd            = "procChain";
    cliCfg.tableEntry[cnt].helpString     = "";
    cliCfg.tableEntry[cnt].cmdHandlerFxn  = MmwDemo_CLIProcChain;
    cnt++;

#ifdef MMWDEMO_TRACKER_H
    /* Add the CLI extension for the tracker */
    MmwDemo_CLIInit_Gtrack_extension(&cliCfg, &cnt);
#endif

    /* Open the CLI: */
    if (CLI_open (&cliCfg) < 0)
    {
        test_print ("Error: Unable to open the CLI\n");
        return;
    }
    test_print ("Debug: CLI is operational\n");
    return;
}


