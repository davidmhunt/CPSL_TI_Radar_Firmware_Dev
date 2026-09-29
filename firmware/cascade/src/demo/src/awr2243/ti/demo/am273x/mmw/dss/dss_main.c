/**
 *   @file  dss_main.c
 *
 *   @brief
 *      This is the main file which implements the millimeter wave Demo
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
#include <math.h>


/* MCU Plus Include Files. */
#include <kernel/dpl/SystemP.h>
#include <kernel/dpl/CacheP.h>
#include <kernel/dpl/CycleCounterP.h>
#include <kernel/dpl/TaskP.h>
#include <ti_drivers_config.h>
#include <ti_board_config.h>
#include <ti_drivers_open_close.h>
#include <ti_board_open_close.h>
#include "FreeRTOS.h"
#include "task.h"
#include <drivers/csirx.h>

/* mmWave SDK Include Files: */
#include <ti/common/syscommon.h>
#include <ti/common/mmwavesdk_version.h>
#include <ti/control/dpm/dpm.h>
#ifdef MMWDEMO_TDM
#include <ti/datapath/dpc/objectdetection/objdethwa/objectdetection.h>
#else
#include <ti/datapath/dpc/objectdetection/objdethwaDDMA/objectdetection.h>
#endif
#include <ti/utils/mathutils/mathutils.h>

/* Demo Include Files */
#include <ti/demo/am273x/mmw/include/mmw_config.h>
#include <ti/demo/am273x/mmw/dss/mmw_dss.h>

/**
 * @brief Task Priority settings:
 */
#define MMWDEMO_DPC_OBJDET_FRAMETRIG_TASK_PRIORITY  4
#define MMWDEMO_DPC_OBJDET_DPM_TASK_PRIORITY      5
#define MMWDEMO_DPC_OBJDET_CSIRX_TASK_PRIORITY      6

#ifdef LVDS_STREAM
/**
 * @brief
 *  The DCA1000EVM FPGA needs a minimum delay of 12ms between Bit clock starts and
 *  actual LVDS Data start to lock the LVDS PLL IP. This is documented in the DCA UG
 */
#define HSI_DCA_MIN_DELAY_MSEC     (12U * 1000U)
#endif


/*! L3 RAM buffer for object detection DPC */
uint8_t gMmwL3[CSL_DSS_L3_U_SIZE - SYS_COMMON_HSRAM_SIZE - 0x100000];

/* EDMA 4K silicon bug related : Align heap to 4K address boundary so that
 * non heap related changes (such as program code) does not alter the 4K related
 * behavior */
#pragma DATA_ALIGN(gMmwL3, 4096U);
#pragma DATA_SECTION(gMmwL3, ".l3ram");

 /*! L2 RAM buffer for object detection DPC */
#ifdef MMWDEMO_DDM
#if defined (CASCADE_EVM) && defined (LVDS_STREAM)
#define MMWDEMO_OBJDET_L2RAM_SIZE (60U * 1024U) /* Memory adjustment to avoid memory errors during the build */
#elif LVDS_STREAM
#define MMWDEMO_OBJDET_L2RAM_SIZE (80U * 1024U)
#else
#define MMWDEMO_OBJDET_L2RAM_SIZE (82U * 1024U)
#endif
#else
#define MMWDEMO_OBJDET_L2RAM_SIZE (49U * 1024U)
#endif
uint8_t gDPC_ObjDetL2Heap[MMWDEMO_OBJDET_L2RAM_SIZE];

/* EDMA 4K silicon bug related : Align heap to 4K address boundary so that
 * non heap related changes (such as program code) does not alter the 4K related
 * behavior */
#pragma DATA_ALIGN(gDPC_ObjDetL2Heap, 4096U);
#pragma DATA_SECTION(gDPC_ObjDetL2Heap, ".dpc_l2Heap");

 /*! HSRAM for processing results */
#pragma DATA_SECTION(gHSRAM, ".demoSharedMem");
#pragma DATA_ALIGN(gHSRAM, 64U);

#define APP_TASK_PRI         (3U)
#define APP_TASK_STACK_SIZE  (1*1024U)
#define APP_DPM_TASK_STACK_SIZE  (2*1024U)
#define APP_DPC_TASK_STACK_SIZE  (2*1024U)
#define APP_CSIRX_TASK_STACK_SIZE  (2*1024U)

TaskHandle_t    gAppInitTask;
StaticTask_t    gAppInitTaskObj;

StaticTask_t    gAppDpmTaskObj;

StaticTask_t    gAppDpcTaskObj;

TaskHandle_t    gAppCsiRxTask;
StaticTask_t    gAppCsiRxTaskObj;

/* Task Stack variables.*/
StackType_t gInitTskStackMain[APP_TASK_STACK_SIZE] __attribute__((aligned(64)));
StackType_t gCsirxTskStackMain[APP_CSIRX_TASK_STACK_SIZE] __attribute__((aligned(64)));
StackType_t gDpmTskStackMain[APP_DPM_TASK_STACK_SIZE] __attribute__((aligned(64)));
StackType_t gFrameTrigTskStackMain[APP_DPC_TASK_STACK_SIZE] __attribute__((aligned(64)));

/* Start-of-Frame callback function prototype; this is a part of the CSIRX configuration */
void mmwDemo_DPC_ObjectDetection_csirxSOF0callback(CSIRX_Handle handle, uint32_t arg, uint8_t contextId);

/**************************************************************************
 *************************** Global Definitions ***************************
 **************************************************************************/

/**
 * @brief
 *  Global Variable for tracking information required by the mmw Demo
 */
MmwDemo_DSS_MCB    gMmwDssMCB = {0U};

/**
 * @brief
 *  Global Variable for DPM result buffer
 */
DPM_Buffer  resultBuffer;

/**
 * @brief
 *  Global Variable for HSRAM buffer used to share results to remote
 */
MmwDemo_HSRAM gHSRAM;

/**
 * @brief
 *  Global Variable used for storing state information for Debug purpose
 */
MmwDemo_CSIRX_State gCSIRXState[MMWAVE_RADAR_DEVICES] = {0};

/**
 * @brief
 *  Global Variable asserted upon frame receival
 */
volatile bool gCSIRXFrameReceived = 0;

/**
 * @brief
 *  Global Variable for error monitoring
 */
uint32_t gCSIRXErrorCode = 0;

/**
 * @brief
 *  Global Variable for frame counting
 */
uint32_t gCSIRXFrameCounter = 0;

/**************************************************************************
 ******************* Millimeter Wave Demo Functions Prototype *******************
 **************************************************************************/
static void MmwDemo_dssInitTask(void* args);
static void MmwDemo_DPC_ObjectDetection_reportFxn
(
    DPM_Report  reportType,
    uint32_t    instanceId,
    int32_t     errCode,
    uint32_t    arg0,
    uint32_t    arg1
);
static void MmwDemo_DPC_ObjectDetection_processFrameBeginCallBackFxn(uint8_t subFrameIndx);
static void MmwDemo_DPC_ObjectDetection_processInterFrameBeginCallBackFxn(uint8_t subFrameIndx);
static void MmwDemo_updateObjectDetStats
(
    DPC_ObjectDetection_Stats       *currDpcStats,
    MmwDemo_output_message_stats    *outputMsgStats
);

static int32_t MmwDemo_copyResultToHSRAM
(
    MmwDemo_HSRAM           *ptrHsramBuffer,
    DPC_ObjectDetection_ExecuteResult *result,
    MmwDemo_output_message_stats *outStats
);
static void MmwDemo_DPC_ObjectDetection_dpmTask(void* args);
static void MmwDemo_sensorStopEpilog(void);
static void MmwDemo_DPC_ObjectDetection_csirxTask(void* args);
static void MmwDemo_DPC_ObjectDetection_FrameTrigTask(void* args);

/**************************************************************************
 ************************* Millimeter Wave Demo Functions **********************
 **************************************************************************/

/**
 *  @b Description
 *  @n
 *      Epilog processing after sensor has stopped
 *
 *  @retval None
 */
static void MmwDemo_sensorStopEpilog(void)
{

    test_print("Data Path Stopped (last frame processing done)\n");

}

/**
 *  @b Description
 *  @n
 *      DPM Registered Report Handler. The DPM Module uses this registered function to notify
 *      the application about DPM reports.
 *
 *  @param[in]  reportType
 *      Report Type
 *  @param[in]  instanceId
 *      Instance Identifier which generated the report
 *  @param[in]  errCode
 *      Error code if any.
 *  @param[in] arg0
 *      Argument 0 interpreted with the report type
 *  @param[in] arg1
 *      Argument 1 interpreted with the report type
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_DPC_ObjectDetection_reportFxn
(
    DPM_Report  reportType,
    uint32_t    instanceId,
    int32_t     errCode,
    uint32_t    arg0,
    uint32_t    arg1
)
{

    /* Only errors are logged on the console: */
    if (errCode != 0)
    {
        /* Error: Detected log on the console and die all errors are FATAL currently. */
        test_print ("Error: DPM Report %d received with error:%d arg0:0x%x arg1:0x%x\n",
                        reportType, errCode, arg0, arg1);
        DebugP_assert (0);
    }

    /* Processing further is based on the reports received: This is the control of the profile
     * state machine: */
    switch (reportType)
    {
        case DPM_Report_IOCTL:
        {
            /*****************************************************************
             * DPC has been configured without an error:
             * - This is an indication that the profile configuration commands
             *   went through without any issues.
             *****************************************************************/
            DebugP_log("DSSApp: DPM Report IOCTL, command = %d\n", arg0);
            break;
        }
        case DPM_Report_DPC_STARTED:
        {
            /*****************************************************************
             * DPC has been started without an error:
             * - notify sensor management task that DPC is started.
             *****************************************************************/
            DebugP_log("DSSApp: DPM Report start\n");
            break;
        }
        case DPM_Report_NOTIFY_DPC_RESULT:
        {
            /*****************************************************************
             * DPC Results have been passed:
             * - This implies that we have valid profile results which have
             *   been received from the profile.
             *****************************************************************/

            break;
        }
        case DPM_Report_NOTIFY_DPC_RESULT_ACKED:
        {
            /*****************************************************************
             * DPC Results have been acked:
             * - This implies that MSS received the results.
             *****************************************************************/

            break;
        }
        case DPM_Report_DPC_ASSERT:
        {
            DPM_DPCAssert*  ptrAssert;

            /*****************************************************************
             * DPC Fault has been detected:
             * - This implies that the DPC has crashed.
             * - The argument0 points to the DPC assertion information
             *****************************************************************/
            ptrAssert = (DPM_DPCAssert*)arg0;
            test_print ("DSS Exception: %s, line %d.\n", ptrAssert->fileName,
                       ptrAssert->lineNum);
            break;
        }
        case DPM_Report_DPC_STOPPED:
        {
            /*****************************************************************
             * DPC has been stopped without an error:
             * - This implies that the DPC can either be reconfigured or
             *   restarted.
             *****************************************************************/
            DebugP_log("DSSApp: DPM Report stop\n");

            MmwDemo_sensorStopEpilog();
            break;
        }
        case DPM_Report_DPC_INFO:
        {
            /* Currently objDetHwa does not use this feature. */
            break;
        }
        default:
        {
            DebugP_assert (0);
            break;
        }
    }
    return;
}

/**
 *  @b Description
 *  @n
 *      Call back function that was registered during config time and is going
 *      to be called in DPC processing at the beginning of frame/sub-frame processing,
 *      we use this to issue BIOS calls for computing CPU load during inter-frame
 *
 *  @param[in] subFrameIndx Sub-frame index of the sub-frame during which processing
 *             this function was called.
 *
 *  @retval None
 */
static void MmwDemo_DPC_ObjectDetection_processFrameBeginCallBackFxn(uint8_t subFrameIndx)
{
    gMmwDssMCB.dataPathObj.subFrameStats[subFrameIndx].interFrameCPULoad = TaskP_loadGetTotalCpuLoad() / 100;
    TaskP_loadResetAll();
}

/**
 *  @b Description
 *  @n
 *      Call back function that was registered during config time and is going
 *      to be called whenever start of frame interrupt is registered.
 *
 *  @param[in] handle CSIRX Handle
 *  @param[in] arg Internal argument to the callback function
 *  @param[in] contextId Context ID
 * 
 *  @retval None
 */
void mmwDemo_DPC_ObjectDetection_csirxSOF0callback(CSIRX_Handle handle, uint32_t arg, uint8_t contextId)
{
    DebugP_assert(handle != NULL);
    DebugP_assert(contextId == MMW_DEMO_CSI2_CONTEXT);
    gCSIRXState[arg].callbackCount.SOF0++;

    // if(gMmwDssMCB.DPMTrigFrameSemHandle != NULL)
    {
        /*Signal the frame trigger to the Frame trigger processing task*/
        SemaphoreP_post(&gMmwDssMCB.DPMTrigFrameSemHandle);
    }

}

/**
 *  @b Description
 *  @n
 *      Call back function that was registered during config time and is going
 *      to be called in DPC processing at the beginning of
 *      inter-frame/inter-sub-frame processing,
 *      we use this to issue BIOS calls for computing CPU load during active frame
 *      (chirping)
 *
 *  @param[in] subFrameIndx Sub-frame index of the sub-frame during which processing
 *             this function was called.
 *
 *  @retval None
 */
static void MmwDemo_DPC_ObjectDetection_processInterFrameBeginCallBackFxn(uint8_t subFrameIndx)
{
    gMmwDssMCB.dataPathObj.subFrameStats[subFrameIndx].activeFrameCPULoad = TaskP_loadGetTotalCpuLoad() / 100;
    TaskP_loadResetAll();
}


/**
 *  @b Description
 *  @n
 *      Update stats based on the stats from DPC
 *
 *  @param[in]  currDpcStats        Pointer to DPC status
 *  @param[in]  outputMsgStats      Pointer to Output message stats 
 *
 *  @retval
 *      Not Applicable.
 */
 void MmwDemo_updateObjectDetStats
(
    DPC_ObjectDetection_Stats       *currDpcStats,
    MmwDemo_output_message_stats    *outputMsgStats
)
{
    static uint32_t prevInterFrameEndTimeStamp = 0U;

    /* Calculate interframe proc time */
    outputMsgStats->interFrameProcessingTime =
            (currDpcStats->interFrameEndTimeStamp - currDpcStats->interFrameStartTimeStamp)/DSP_CLOCK_MHZ; /* In micro seconds */

    outputMsgStats->interChirpProcessingMargin = currDpcStats->interChirpProcessingMargin/DSP_CLOCK_MHZ;

    /* Calculate interFrame processing Margin for previous frame, but saved to current frame */
    outputMsgStats->interFrameProcessingMargin =
        (currDpcStats->frameStartTimeStamp - prevInterFrameEndTimeStamp - currDpcStats->subFramePreparationCycles)/DSP_CLOCK_MHZ;

    prevInterFrameEndTimeStamp = currDpcStats->interFrameEndTimeStamp;
}


/**
 *  @b Description
 *  @n
 *      Copy DPC results and output stats to HSRAM to share with MSS
 *
 *  @param[in]  ptrHsramBuffer      Pointer to HSRAM buffer memory
 *  @param[in]  result              Pointer to DPC results
 *  @param[in]  outStats            Pointer to Output message stats
 *
 *  @retval
 *      Not Applicable.
 */
static int32_t MmwDemo_copyResultToHSRAM
(
    MmwDemo_HSRAM           *ptrHsramBuffer,
    DPC_ObjectDetection_ExecuteResult *result,
    MmwDemo_output_message_stats *outStats
)
{
    uint8_t             *ptrCurrBuffer;
    uint32_t            totalHsramSize;
    uint32_t            itemPayloadLen;

    /* Save result in HSRAM */
    if(ptrHsramBuffer == NULL)
    {
        return -1;
    }

    /* Save result in HSRAM */
    if(result != NULL)
    {
        itemPayloadLen = sizeof(DPC_ObjectDetection_ExecuteResult);
        memcpy((void *)&ptrHsramBuffer->result, (void *)result, itemPayloadLen);
    }
    else
    {
        return -1;
    }

    /* Save output Stats in HSRAM */
    if(outStats != NULL)
    {
        itemPayloadLen = sizeof(MmwDemo_output_message_stats);
        memcpy((void *)&ptrHsramBuffer->outStats, (void *)outStats, itemPayloadLen);
    }

    /* Set payload pointer to HSM buffer */
    ptrCurrBuffer = &ptrHsramBuffer->payload[0];
    totalHsramSize = MMWDEMO_HSRAM_PAYLOAD_SIZE;

    /* Save ObjOut in HSRAM */
    if(result->objOut != NULL)
    {
        itemPayloadLen = sizeof(DPIF_PointCloudCartesian) * result->numObjOut;
        if((totalHsramSize- itemPayloadLen) > 0)
        {
            memcpy(ptrCurrBuffer, (void *)result->objOut, itemPayloadLen);

            ptrHsramBuffer->result.objOut = (DPIF_PointCloudCartesian *)ptrCurrBuffer;
            ptrCurrBuffer+= itemPayloadLen;
            totalHsramSize -=itemPayloadLen;
        }
        else
        {
            return -1;
        }
    }

#ifdef MMWDEMO_TDM
    /* Save ObjOutSideInfo in HSRAM */
    if(result->objOutSideInfo != NULL)
    {
        itemPayloadLen = sizeof(DPIF_PointCloudSideInfo) * result->numObjOut;
        if((totalHsramSize- itemPayloadLen) > 0)
        {
            memcpy(ptrCurrBuffer, (void *)result->objOutSideInfo, itemPayloadLen);
            ptrHsramBuffer->result.objOutSideInfo = (DPIF_PointCloudSideInfo *)ptrCurrBuffer;
            ptrCurrBuffer+= itemPayloadLen;
            totalHsramSize -=itemPayloadLen;
        }
        else
        {
            return -1;
        }
    }
#endif

    /* Save DPC_ObjectDetection_Stats in HSRAM */
    if(result->stats != NULL)
    {
        itemPayloadLen = sizeof(DPC_ObjectDetection_Stats);
        if((totalHsramSize- itemPayloadLen) > 0)
        {
            memcpy(ptrCurrBuffer, (void *)result->stats, itemPayloadLen);
            ptrHsramBuffer->result.stats = (DPC_ObjectDetection_Stats *)ptrCurrBuffer;
            ptrCurrBuffer+= itemPayloadLen;
            totalHsramSize -=itemPayloadLen;
        }
        else
        {
            return -1;
        }
    }

#ifdef MMWDEMO_TDM
    /* Save compRxChanBiasMeasurement in HSRAM */
    if(result->compRxChanBiasMeasurement != NULL)
    {
        itemPayloadLen = sizeof(DPU_AoAProc_compRxChannelBiasCfg);
        if((totalHsramSize- itemPayloadLen) > 0)
        {
            memcpy(ptrCurrBuffer, (void *)result->compRxChanBiasMeasurement, itemPayloadLen);
            ptrHsramBuffer->result.compRxChanBiasMeasurement = (DPU_AoAProc_compRxChannelBiasCfg *)ptrCurrBuffer;
            ptrCurrBuffer+= itemPayloadLen;
            totalHsramSize -=itemPayloadLen;
        }
        else
        {
            return -1;
        }
    }
#endif

#ifdef MMWDEMO_DDM
    /* Save compRxChanBiasMeasurement in HSRAM */
    if(result->compRxChanBiasMeasurement != NULL)
    {
        itemPayloadLen = sizeof(Measure_compRxChannelBiasCfg);
        if((totalHsramSize- itemPayloadLen) > 0)
        {
            memcpy(ptrCurrBuffer, (void *)result->compRxChanBiasMeasurement, itemPayloadLen);
            ptrHsramBuffer->result.compRxChanBiasMeasurement = (Measure_compRxChannelBiasCfg *)ptrCurrBuffer;
            ptrCurrBuffer+= itemPayloadLen;
            totalHsramSize -=itemPayloadLen;
        }
        else
        {
            return -1;
        }
    }

    /* save the FFT clip status in HSRAM */
    if(result->FFTClipCount !=NULL)
    {
        itemPayloadLen = sizeof(result->FFTClipCount);
        if((totalHsramSize- itemPayloadLen) > 0)
        {
            memcpy(ptrHsramBuffer->result.FFTClipCount, (void *)result->FFTClipCount, itemPayloadLen);
            totalHsramSize -=itemPayloadLen;
        }
        else
        {
            return -1;
        }
    }
#endif

    return totalHsramSize;
}

/**
 *  @b Description
 *  @n
 *      DPM Execution Task. DPM execute results are processed here:
 *      a) Update states based on timestamp from DPC.
 *      b) Copy results to shared memory to be shared with MSS.
 *      c) Send Results to MSS by calling DPM_sendResult()
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_DPC_ObjectDetection_dpmTask(void* args)
{
    int32_t     retVal;
    DPC_ObjectDetection_ExecuteResult *result;
    volatile uint32_t              startTime;

    while (1)
    {
        /* Execute the DPM module: */
        retVal = DPM_execute (gMmwDssMCB.dataPathObj.objDetDpmHandle, &resultBuffer);
        if (retVal < 0) {
            test_print ("Error: DPM execution failed [Error code %d]\n", retVal);
            MmwDemo_debugAssert (0);
        }
        else
        {
            if ((resultBuffer.size[0] == sizeof(DPC_ObjectDetection_ExecuteResult)))
            {
                result = (DPC_ObjectDetection_ExecuteResult *)resultBuffer.ptrBuffer[0];

                /* Get the time stamp before copy data to HSRAM */
                startTime = CycleCounterP_getCount32();

                /* Update processing stats and added it to buffer 1*/
                MmwDemo_updateObjectDetStats(result->stats,
                                                &gMmwDssMCB.dataPathObj.subFrameStats[result->subFrameIdx]);

                /* Cache invalidation for gHSRAM needed to avoid incoherency issues between DSS and MSS */
                CacheP_inv(&gHSRAM,SYS_COMMON_HSRAM_SIZE,CacheP_TYPE_ALL);

                /* Copy result data to HSRAM */
                if ((retVal = MmwDemo_copyResultToHSRAM(&gHSRAM, result, &gMmwDssMCB.dataPathObj.subFrameStats[result->subFrameIdx])) >= 0)
                {
                    /* Update interframe margin with HSRAM copy time */
                    gHSRAM.outStats.interFrameProcessingMargin -= ((CycleCounterP_getCount32() - startTime)/DSP_CLOCK_MHZ);

                    /* Update DPM buffer */
                    resultBuffer.ptrBuffer[0] = (uint8_t *)&gHSRAM.result;
                    resultBuffer.ptrBuffer[1] = (uint8_t *)&gHSRAM.outStats;
                    resultBuffer.size[1] = sizeof(MmwDemo_output_message_stats);


                    /* YES: Results are available send them. */
                    retVal = DPM_sendResult (gMmwDssMCB.dataPathObj.objDetDpmHandle, true, &resultBuffer);
                    if (retVal < 0)
                    {
                        test_print ("Error: Failed to send results [Error: %d] to remote\n", retVal);
                    }
                }
                else
                {
                    test_print ("Error: Failed to copy processing results to HSRAM, error=%d\n", retVal);
                    MmwDemo_debugAssert (0);
                }
            }
        }
    }
}

/**
 *  @b Description
 *  @n
 *      Task which configures the CSI and waits for data from the frontend.
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_DPC_ObjectDetection_csirxTask(void* args)
{
    MmwDemo_dataPath_CsirxConfig(&gMmwDssMCB.dataPathObj);

    vTaskDelete(NULL);

    return;
}


/**
 *  @b Description
 *  @n
 *      Task which sends the Frame Trigger to the DPC.
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_DPC_ObjectDetection_FrameTrigTask(void* args)
{
    int32_t errCode;

    while(1)
    {
    
        SemaphoreP_pend(&gMmwDssMCB.DPMTrigFrameSemHandle, SystemP_WAIT_FOREVER);
        /* send DPC_OBJDET_IOCTL__TRIGGER_FRAME */
        errCode = DPM_ioctl (gMmwDssMCB.dataPathObj.objDetDpmHandle,
                             DPC_OBJDET_IOCTL__TRIGGER_FRAME,
                             NULL,
                             0);
        if (errCode < 0)
        {
            test_print ("Error: Unable to send DPC_OBJDET_IOCTL__TRIGGER_FRAME [Error:%d]\n", errCode);
            MmwDemo_debugAssert (0);
            return;
        }
            
    }
      
}


/**
 *  @b Description
 *  @n
 *      System Initialization Task which initializes the various
 *      components in the system.
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_dssInitTask(void* args)
{
    int32_t             errCode = 0;
    DPM_InitCfg         dpmInitCfg;
    DPC_ObjectDetection_InitParams      objDetInitParams;
    uint32_t            edmaCCIdx;

    Drivers_open();
    Board_driversOpen();

    CycleCounterP_reset();

#ifdef LVDS_STREAM
    /* Configure HSI interface Clock */
    HW_WR_REG32(CSL_MSS_TOPRCM_U_BASE + CSL_MSS_TOPRCM_HSI_CLK_SRC_SEL, 0x222);
#ifdef CASCADE_EVM
    HW_WR_REG32(CSL_MSS_TOPRCM_U_BASE + CSL_MSS_TOPRCM_HSI_DIV_VAL, 0x111);
#else
    HW_WR_REG32(CSL_MSS_TOPRCM_U_BASE + CSL_MSS_TOPRCM_HSI_DIV_VAL, 0x333);
#endif
#endif

    /* Initialize and populate the demo MCB */
    memset ((void*)&gMmwDssMCB, 0, sizeof(MmwDemo_DSS_MCB));

#ifdef LVDS_STREAM
    /* Populate edma handle for CBUFF. */
    gMmwDssMCB.lvdsStream.edmaHandle = gEdmaHandle[CONFIG_EDMA1];
#endif

    SemaphoreP_constructBinary(&gMmwDssMCB.CSI2RXConfigCompleteSemHandle, 0);
    SemaphoreP_constructBinary(&gMmwDssMCB.DPMTrigFrameSemHandle, 0);
    SemaphoreP_constructBinary(&gMmwDssMCB.demoInitTaskCompleteSemHandle, 0);

    /*****************************************************************************
     * Driver Init:
     *****************************************************************************/

    /*****************************************************************************
     * Driver Open/Configuraiton:
     *****************************************************************************/

    /* Initialize the Data Path: */
    MmwDemo_dataPathInit(&gMmwDssMCB.dataPathObj); 
    MmwDemo_dataPathOpen(&gMmwDssMCB.dataPathObj); 

    /* Launch the CSIRX Task */
    gAppCsiRxTask = xTaskCreateStatic( MmwDemo_DPC_ObjectDetection_csirxTask,
                                  "mmwdemo_dpc_task",
                                  APP_CSIRX_TASK_STACK_SIZE,
                                  NULL,
                                  MMWDEMO_DPC_OBJDET_CSIRX_TASK_PRIORITY,
                                  gCsirxTskStackMain,
                                  &gAppCsiRxTaskObj );
    configASSERT(gAppCsiRxTask != NULL);

    /* Launch the Frame Trigger task */
    gMmwDssMCB.objFrameTrigTaskHandle = xTaskCreateStatic( MmwDemo_DPC_ObjectDetection_FrameTrigTask,
                                  "mmwdemo_dpc_task",
                                  APP_TASK_STACK_SIZE,
                                  NULL,
                                  MMWDEMO_DPC_OBJDET_FRAMETRIG_TASK_PRIORITY,
                                  gFrameTrigTskStackMain,
                                  &gAppDpcTaskObj );
    configASSERT(gMmwDssMCB.objFrameTrigTaskHandle != NULL);

    SemaphoreP_pend(&gMmwDssMCB.CSI2RXConfigCompleteSemHandle, SystemP_WAIT_FOREVER);

    /*****************************************************************************
     * Initialization of the DPM Module:
     *****************************************************************************/
    memset ((void *)&objDetInitParams, 0, sizeof(DPC_ObjectDetection_InitParams));

    /* Note this must be after MmwDemo_dataPathOpen() above which opens the hwa */
    objDetInitParams.hwaHandle = gMmwDssMCB.dataPathObj.hwaHandle;
    objDetInitParams.L3ramCfg.addr = (void *)&gMmwL3[0];
    objDetInitParams.L3ramCfg.size = sizeof(gMmwL3);
    objDetInitParams.CoreLocalRamCfg.addr = &gDPC_ObjDetL2Heap[0];
    objDetInitParams.CoreLocalRamCfg.size = sizeof(gDPC_ObjDetL2Heap);
    for (edmaCCIdx = 0; edmaCCIdx < EDMA_NUM_CC; edmaCCIdx++)
    {
        objDetInitParams.edmaHandle[edmaCCIdx] = gMmwDssMCB.dataPathObj.edmaHandle[edmaCCIdx];
    }

    /* DPC Call-back config */
    objDetInitParams.processCallBackCfg.processFrameBeginCallBackFxn =
        MmwDemo_DPC_ObjectDetection_processFrameBeginCallBackFxn;
    objDetInitParams.processCallBackCfg.processInterFrameBeginCallBackFxn =
        MmwDemo_DPC_ObjectDetection_processInterFrameBeginCallBackFxn;

    memset ((void *)&dpmInitCfg, 0, sizeof(DPM_InitCfg));

    /* Setup the configuration: */
    dpmInitCfg.ptrProcChainCfg  = &gDPC_ObjectDetectionCfg;
    dpmInitCfg.instanceId       = 0xFEEDFEED;
    dpmInitCfg.domain           = DPM_Domain_REMOTE;
    dpmInitCfg.reportFxn        = MmwDemo_DPC_ObjectDetection_reportFxn;
    dpmInitCfg.arg              = &objDetInitParams;
    dpmInitCfg.argSize          = sizeof(DPC_ObjectDetection_InitParams);

    /* Initialize the DPM Module: */
    gMmwDssMCB.dataPathObj.objDetDpmHandle = DPM_init (&dpmInitCfg, &errCode);
    if (gMmwDssMCB.dataPathObj.objDetDpmHandle == NULL)
    {
        test_print ("Error: Unable to initialize the DPM Module [Error: %d]\n", errCode);
        MmwDemo_debugAssert (0);
        return;
    }

    /* Launch the DPM Task */
    gMmwDssMCB.objDetDpmTaskHandle = xTaskCreateStatic( MmwDemo_DPC_ObjectDetection_dpmTask,
                                  "mmwdemo_dpm_task",
                                  APP_DPC_TASK_STACK_SIZE,
                                  NULL,
                                  MMWDEMO_DPC_OBJDET_DPM_TASK_PRIORITY,
                                  gDpmTskStackMain,
                                  &gAppDpmTaskObj );
    configASSERT(gMmwDssMCB.objDetDpmTaskHandle != NULL);


    /* Synchronization: This will synchronize the execution of the control module
     * between the domains. This is a prerequiste and always needs to be invoked. */
    while (1)
    {
        int32_t syncStatus;

        /* Get the synchronization status: */
        syncStatus = DPM_synch (gMmwDssMCB.dataPathObj.objDetDpmHandle, &errCode);
        if (syncStatus < 0)
        {
            /* Error: Unable to synchronize the framework */
            test_print ("Error: DPM Synchronization failed [Error code %d]\n", errCode);
            MmwDemo_debugAssert (0);
            return;
        }
        if (syncStatus == 1)
        {
            /* Synchronization acheived: */
            break;
        }
        /* Sleep and poll again: */
        ClockP_usleep(1 * 1000U);
    }
    test_print ("Debug: DPM Module Sync is done\n");

    /* Never return for this task. */
    SemaphoreP_pend(&gMmwDssMCB.demoInitTaskCompleteSemHandle, SystemP_WAIT_FOREVER);

    return;
}

#ifdef LVDS_STREAM
/**
 *  @b Description
 *  @n
 *      Configures CBUFF EDMA channel SRC address for Ping/Pong 
 *      Switch
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_configureTransfer(void)
{
    static Bool pingPongSwitchFlag = true;

    if(pingPongSwitchFlag)
    {
        /* Update PaRAM set Source address for capturing CSIRX data received on PONG buffer. */
        EDMA_dmaSetPaRAMEntry(CONFIG_EDMA1_BASE_ADDR, MMW_LVDS_STREAM_CBUFF_EDMA_CH_0, EDMACC_PARAM_ENTRY_SRC,
                              (uint32_t) SOC_virtToPhy((void *)gConfigCsirx0ContextConfig[0].pingPongConfig.pongAddress));
        EDMA_dmaSetPaRAMEntry(CONFIG_EDMA1_BASE_ADDR, MMW_LVDS_STREAM_CBUFF_EDMA_SHADOW_CH_0, EDMACC_PARAM_ENTRY_SRC,
                              (uint32_t) SOC_virtToPhy((void *)gConfigCsirx0ContextConfig[0].pingPongConfig.pongAddress));

        #ifdef CASCADE_EVM
		/* Update PaRAM set Source address for capturing CSIRX data received on PONG buffer belonging to Radar 2*/
        EDMA_dmaSetPaRAMEntry(CONFIG_EDMA1_BASE_ADDR, MMW_LVDS_STREAM_SW_SESSION_EDMA_CH_0, EDMACC_PARAM_ENTRY_SRC,
                              (uint32_t) SOC_virtToPhy((void *)gConfigCsirx1ContextConfig[0].pingPongConfig.pongAddress));
        EDMA_dmaSetPaRAMEntry(CONFIG_EDMA1_BASE_ADDR, MMW_LVDS_STREAM_SW_SESSION_EDMA_SHADOW_CH_0, EDMACC_PARAM_ENTRY_SRC,
                              (uint32_t) SOC_virtToPhy((void *)gConfigCsirx1ContextConfig[0].pingPongConfig.pongAddress));
        #endif

        pingPongSwitchFlag = false;
    }
    else
    {
        /* Update PaRAM set Source address for capturing CSIRX data received on PING buffer. */
        EDMA_dmaSetPaRAMEntry(CONFIG_EDMA1_BASE_ADDR, MMW_LVDS_STREAM_CBUFF_EDMA_CH_0, EDMACC_PARAM_ENTRY_SRC,
                              (uint32_t) SOC_virtToPhy((void *)gConfigCsirx0ContextConfig[0].pingPongConfig.pingAddress));
        EDMA_dmaSetPaRAMEntry(CONFIG_EDMA1_BASE_ADDR, MMW_LVDS_STREAM_CBUFF_EDMA_SHADOW_CH_0, EDMACC_PARAM_ENTRY_SRC,
                              (uint32_t) SOC_virtToPhy((void *)gConfigCsirx0ContextConfig[0].pingPongConfig.pingAddress));

        #ifdef CASCADE_EVM
		/* Update PaRAM set Source address for capturing CSIRX data received on PING buffer belonging to Radar 2 */
        EDMA_dmaSetPaRAMEntry(CONFIG_EDMA1_BASE_ADDR, MMW_LVDS_STREAM_SW_SESSION_EDMA_CH_0, EDMACC_PARAM_ENTRY_SRC,
                              (uint32_t) SOC_virtToPhy((void *)gConfigCsirx1ContextConfig[0].pingPongConfig.pingAddress));
        EDMA_dmaSetPaRAMEntry(CONFIG_EDMA1_BASE_ADDR, MMW_LVDS_STREAM_SW_SESSION_EDMA_SHADOW_CH_0, EDMACC_PARAM_ENTRY_SRC,
                              (uint32_t) SOC_virtToPhy((void *)gConfigCsirx1ContextConfig[0].pingPongConfig.pingAddress));

        #endif

        pingPongSwitchFlag = true;
    }

    return;
}
#endif

/**
 *  @b Description
 *  @n
 *      Entry point into the Millimeter Wave Demo
 *
 *  @retval
 *      Not Applicable.
 */
int main (void)
{
    /* init SOC specific modules */
    System_init();
    Board_init();


    gAppInitTask = xTaskCreateStatic( MmwDemo_dssInitTask,
                                  "mmwdemo_dss_init_task",
                                  APP_TASK_STACK_SIZE,
                                  NULL,
                                  APP_TASK_PRI,
                                  gInitTskStackMain,
                                  &gAppInitTaskObj );
    configASSERT(gAppInitTask != NULL);

    /* Start the scheduler to start the tasks executing. */
    vTaskStartScheduler();

    /* The following line should never be reached because vTaskStartScheduler()
    will only return if there was not enough FreeRTOS heap memory available to
    create the Idle and (if configured) Timer tasks.  Heap management, and
    techniques for trapping heap exhaustion, are described in the book text. */
    DebugP_assertNoLog(0);
}
