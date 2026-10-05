/**
 *   @file  data_path.c
 *
 *   @brief
 *      Implements Data path processing functionality.
 *
 *  \par
 *  NOTE:
 *      (C) Copyright 2016-2021 Texas Instruments, Inc.
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

#include <kernel/dpl/AddrTranslateP.h>
#include <ti/common/syscommon.h>
#include <ti/control/mmwavelink/mmwavelink.h>
#include <ti/control/mmwave/mmwave.h>

#include <ti_drivers_open_close.h>
#include <ti_drivers_config.h>
#include <ti/demo/am273x/mmw/dss/mmw_dss.h>

/**************************************************************************
 *************************** Global Definitions ********************************
 **************************************************************************/

/**
 * @brief
 *  Global Variable for tracking information required by the mmw Demo
 */
extern MmwDemo_DSS_MCB    gMmwDssMCB;

/**
 * @brief
 *  Global Variables for tracking CSIRX debug information
 */
extern Mmw_Demo_CSIRX_Config gCSIRXCfg;
extern MmwDemo_CSIRX_State gCSIRXState[MMWAVE_RADAR_DEVICES];
extern uint32_t gCSIRXErrorCode[MMWAVE_RADAR_DEVICES];
extern uint32_t gCSIRXFrameCounter;
extern volatile bool gCSIRXFrameReceived;


/* CSI Rx Callback Args */
uint32_t csirx0CommonCallBackArg = (uint32_t)CONFIG_CSIRX0;
uint32_t csirx1CommonCallBackArg = (uint32_t)CONFIG_CSIRX1;
int32_t i32ErrCode;
CSIRX_ContextConfig *ptrCsirxContextConfig = NULL;

/**************************************************************************
 *************************** Static Function Prototype***************************
 **************************************************************************/
static void MmwDemo_hwaInit(MmwDemo_DataPathObj *obj);
static void MmwDemo_hwaOpen(MmwDemo_DataPathObj *obj);
static void MmwDemo_hwaClose(MmwDemo_DataPathObj *obj);
static void MmwDemo_csirxInit(MmwDemo_DataPathObj *obj);
static void MmwDemo_csirxOpen(MmwDemo_DataPathObj *obj);
static void MmwDemo_csirxConfig(MmwDemo_DataPathObj *obj);
void MmwDemo_csirxCommonCallback(CSIRX_Handle handle, void *arg,
                              struct CSIRX_CommonIntr_s *IRQ);
void MmwDemo_csirxCombinedEOFcallback(CSIRX_Handle handle, uint32_t arg);

#ifdef LVDS_STREAM
extern void MmwDemo_configureTransfer(void);
#endif
/**
 *  @b Description
 *  @n
 *      HWA driver init
 *
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_hwaInit(MmwDemo_DataPathObj *obj)
{
    /* Initialize the HWA */
    HWA_init();
}


/**
 *  @b Description
 *  @n
 *      Open HWA driver instance
 *
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_hwaOpen(MmwDemo_DataPathObj *obj)
{
    int32_t             errCode;

    /* Open the HWA Instance */
    obj->hwaHandle = HWA_open(0, NULL, &errCode);
    
    if (obj->hwaHandle == NULL)
    {
        MmwDemo_debugAssert (0);
        return;
    }
}

/**
 *  @b Description
 *  @n
 *      Close HWA driver instance
 *
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_hwaClose(MmwDemo_DataPathObj *obj)
{
    int32_t             errCode;

    /* Close the HWA Instance */
    errCode = HWA_close(obj->hwaHandle);
    if (errCode != 0)
    {
        MmwDemo_debugAssert (0);
        return;
    }
}


/**
 *  @b Description
 *  @n
 *      Close EDMA driver instance
 *
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_edmaClose(MmwDemo_DataPathObj *obj)
{
    EDMA_close(obj->edmaHandle);
}

/**
 *  @b Description
 *  @n
 *      Initializes  CSIRX.
 *      
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_csirxInit(MmwDemo_DataPathObj *obj)
{
    int32_t errorCode;
    uint32_t u32DevIdx;
    /* Initialize global variables */
    memset(gCSIRXState, 0, MMWAVE_RADAR_DEVICES * sizeof(MmwDemo_CSIRX_State));

	/* Looping over number of devices to create the respective csirxHandles */
    for(u32DevIdx = 0U; u32DevIdx < MMWAVE_RADAR_DEVICES; u32DevIdx++)
        {
            obj->csirxHandle[u32DevIdx] = NULL;

            gCSIRXState[u32DevIdx].isReceivedPayloadCorrect = true;

            gCSIRXErrorCode[u32DevIdx] = 0;       /* Only for debug */
        }

        errorCode = CSIRX_init();
        if (errorCode != SystemP_SUCCESS)
        {
            test_print("Error: CSIRX initialization returned error %d\n", errorCode);
            DebugP_assert (0);
            return;
        }
}

/**
 *  @b Description
 *  @n
 *      Open CSI RX instance.
 * 
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_csirxOpen(MmwDemo_DataPathObj *obj)
{
    uint32_t    u32DevIdx;
    int32_t     errorCode = SystemP_SUCCESS;
  
    /* Looping over number of devices to open Instance */
	for(u32DevIdx = 0U; u32DevIdx < MMWAVE_RADAR_DEVICES; u32DevIdx++)
    {  
        /* Open Instance */
        if(u32DevIdx == 0)
        {
            obj->csirxHandle[u32DevIdx] = CSIRX_open(CONFIG_CSIRX0);
        }
        else
        {
            obj->csirxHandle[u32DevIdx] = CSIRX_open(CONFIG_CSIRX1);
        }

        if(obj->csirxHandle[u32DevIdx] == NULL)
        {
            MmwDemo_debugAssert (0);
            return;
        }

        printf("CSIRX instance opened\n");

        /* reset csi */
        errorCode = CSIRX_reset(obj->csirxHandle[u32DevIdx]);
        if(errorCode != SystemP_SUCCESS)
        {
            MmwDemo_debugAssert (0);
            return;
        }
    }
}

/**
 *  @b Description
 *  @n
 *      Close CSI RX instance.
 * 
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_csirxClose(MmwDemo_DataPathObj *obj)
{
    int32_t     errorCode;
    uint32_t    u32DevIdx;

    /* Loop across all devices to disable context */
    for(u32DevIdx = 0U; u32DevIdx < MMWAVE_RADAR_DEVICES; u32DevIdx++)
    {
        /* disable context */
        errorCode = CSIRX_contextDisable(obj->csirxHandle[u32DevIdx], 0);
        if(errorCode != SystemP_SUCCESS)
        {
            MmwDemo_debugAssert (0);
            return;
        }

        /* disable interface */
        errorCode = CSIRX_commonDisable(obj->csirxHandle[u32DevIdx]);
        if(errorCode != SystemP_SUCCESS)
        {
            MmwDemo_debugAssert (0);
            return;
        }

        /* close instance */
        errorCode = CSIRX_close(obj->csirxHandle[u32DevIdx]);
        if(errorCode != SystemP_SUCCESS)
        {
            MmwDemo_debugAssert (0);
            return;
        }
    }
}


/**
 *  @b Description
 *  @n
 *      Performs CSI Configuration
 * 
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_csirxConfig(MmwDemo_DataPathObj *obj)
{
    int32_t errorCode;
    uint32_t u32DevIdx;
    volatile bool isComplexIOresetDone, isForceRxModeDeasserted;
    volatile uint32_t numComplexIOresetDonePolls, numComplexIOPowerStatusPolls,
             numForceRxModeDeassertedPolls;
    volatile uint8_t complexIOpowerStatus;

    /* Performs CSI configuration across all devices */
    for(u32DevIdx = 0U; u32DevIdx < MMWAVE_RADAR_DEVICES; u32DevIdx++)
    {

#ifdef LVDS_STREAM
#ifdef CASCADE_EVM
    /* Configure memory in L3 Memory. */
    /* pingAddress = 0x88000000 */
    /* pongAddress = 0x88000000 + 0x4000 */
        if(u32DevIdx == 0 ) /* for first radar */
        {
            gConfigCsirx0ContextConfig[MMW_DEMO_CSI2_CONTEXT].pingPongConfig.pingAddress = CSL_DSS_L3_U_BASE + CSL_DSS_HWA_BANK_SIZE + CSL_DSS_HWA_BANK_SIZE;
            gConfigCsirx0ContextConfig[MMW_DEMO_CSI2_CONTEXT].pingPongConfig.pongAddress = CSL_DSS_L3_U_BASE;
            ptrCsirxContextConfig = &gConfigCsirx0ContextConfig[MMW_DEMO_CSI2_CONTEXT];
            gCsirxCommonConfig[CONFIG_CSIRX0].intrCallbacks.commonCallbackArgs = (void*)&csirx0CommonCallBackArg;
            gCsirxCommonConfig[CONFIG_CSIRX0].intrCallbacks.combinedEndOfFrameCallbackArgs = (void*)&csirx0CommonCallBackArg;
            gCsirxCommonConfig[CONFIG_CSIRX0].intrCallbacks.combinedEndOfLineCallbackArgs = (void*)&csirx0CommonCallBackArg;
        }
        /* Cascade Implementation */
        else /* for second radar */
        {
            gConfigCsirx1ContextConfig[MMW_DEMO_CSI2_CONTEXT].pingPongConfig.pingAddress = CSL_DSS_L3_U_BASE + CSL_DSS_HWA_BANK_SIZE + CSL_DSS_HWA_BANK_SIZE + CSL_DSS_HWA_BANK_SIZE;  
            gConfigCsirx1ContextConfig[MMW_DEMO_CSI2_CONTEXT].pingPongConfig.pongAddress = CSL_DSS_L3_U_BASE + CSL_DSS_HWA_BANK_SIZE; 
            ptrCsirxContextConfig = &gConfigCsirx1ContextConfig[MMW_DEMO_CSI2_CONTEXT];
            gCsirxCommonConfig[CONFIG_CSIRX1].intrCallbacks.commonCallbackArgs = (void*)&csirx1CommonCallBackArg;
            gCsirxCommonConfig[CONFIG_CSIRX1].intrCallbacks.combinedEndOfFrameCallbackArgs = (void*)&csirx1CommonCallBackArg;
            gCsirxCommonConfig[CONFIG_CSIRX1].intrCallbacks.combinedEndOfLineCallbackArgs = (void*)&csirx1CommonCallBackArg;
        }

#else
    /* Configure memory in L3 Memory. */
        /* pingAddress = 0x88000000 */
        /* pongAddress = 0x88000000 + 0x4000 */
        gConfigCsirx0ContextConfig[0].pingPongConfig.pingAddress = CSL_DSS_L3_U_BASE;
        gConfigCsirx0ContextConfig[0].pingPongConfig.pongAddress = CSL_DSS_L3_U_BASE + CSL_DSS_HWA_BANK_SIZE;

#endif

#else
#ifdef CASCADE_EVM
        /* Configure memory in HWA Memory. */
        if(u32DevIdx == 0 ) /* for first radar */ 
        {
            gConfigCsirx0ContextConfig[0].pingPongConfig.pingAddress = CSL_DSS_HWA_DMA0_RAM_BANK0_BASE;
            gConfigCsirx0ContextConfig[0].pingPongConfig.pongAddress = CSL_DSS_HWA_DMA0_RAM_BANK1_BASE;
            ptrCsirxContextConfig = &gConfigCsirx0ContextConfig[MMW_DEMO_CSI2_CONTEXT];
            gCsirxCommonConfig[CONFIG_CSIRX0].intrCallbacks.commonCallbackArgs = (void*)&csirx0CommonCallBackArg;
            gCsirxCommonConfig[CONFIG_CSIRX0].intrCallbacks.combinedEndOfLineCallbackArgs = (void*)&csirx0CommonCallBackArg;
        }
        /* Cascade Implementation */
        else  /* for second radar */
        {
            gConfigCsirx1ContextConfig[0].pingPongConfig.pingAddress = CSL_DSS_HWA_DMA0_RAM_BANK4_BASE;  /* Ping address for second radar */
            gConfigCsirx1ContextConfig[0].pingPongConfig.pongAddress = CSL_DSS_HWA_DMA0_RAM_BANK5_BASE;  /* Pong address for second radar */
            ptrCsirxContextConfig = &gConfigCsirx1ContextConfig[MMW_DEMO_CSI2_CONTEXT];
            gCsirxCommonConfig[CONFIG_CSIRX1].intrCallbacks.commonCallbackArgs = (void*)&csirx1CommonCallBackArg;
            gCsirxCommonConfig[CONFIG_CSIRX1].intrCallbacks.combinedEndOfLineCallbackArgs = (void*)&csirx1CommonCallBackArg;
        }
#else
    /* Configure memory in HWA Memory. */
        gConfigCsirx0ContextConfig[0].pingPongConfig.pingAddress = CSL_DSS_HWA_DMA0_RAM_BANK0_BASE;
        gConfigCsirx0ContextConfig[0].pingPongConfig.pongAddress = CSL_DSS_HWA_DMA0_RAM_BANK1_BASE;
#endif
#endif

        /* config complex IO - lanes and IRQ */
        errorCode = CSIRX_complexioSetConfig(obj->csirxHandle[u32DevIdx], &gCsirxComplexioConfig[u32DevIdx]);
        if(errorCode != SystemP_SUCCESS)
        {
            MmwDemo_debugAssert (0);
            return;
        }

        /* deassert complex IO reset */
        errorCode = CSIRX_complexioDeassertReset(obj->csirxHandle[u32DevIdx]);
        if(errorCode != SystemP_SUCCESS)
        {
            MmwDemo_debugAssert (0);
            return;
        }

        /* config DPHY */
        errorCode = CSIRX_dphySetConfig(obj->csirxHandle[u32DevIdx], &gCsirxDphyConfig[u32DevIdx]);
        if(errorCode != SystemP_SUCCESS)
        {
            MmwDemo_debugAssert (0);
            return;
        }

        errorCode = CSIRX_complexioSetPowerCommand(obj->csirxHandle[u32DevIdx], 1);
        if(errorCode != SystemP_SUCCESS)
        {
            MmwDemo_debugAssert (0);
            return;
        }

        uint8_t isComplexIOpowerStatus = 1;
        do
        {
            errorCode = CSIRX_complexioGetPowerStatus(obj->csirxHandle[u32DevIdx], &isComplexIOpowerStatus);
            if(errorCode != SystemP_SUCCESS)
            {
                MmwDemo_debugAssert (0);
                return;
            }

            numComplexIOresetDonePolls++;
        }while((isComplexIOpowerStatus == 0));

        /* config common */
        errorCode = CSIRX_commonSetConfig(obj->csirxHandle[u32DevIdx], &gCsirxCommonConfig[u32DevIdx]);
        if(errorCode != SystemP_SUCCESS)
        {
            MmwDemo_debugAssert (0);
            return;
        }

#ifdef CASCADE_EVM
        errorCode = CSIRX_contextSetConfig(obj->csirxHandle[u32DevIdx], MMW_DEMO_CSI2_CONTEXT, &ptrCsirxContextConfig[MMW_DEMO_CSI2_CONTEXT]);
#else
        errorCode = CSIRX_contextSetConfig(obj->csirxHandle[u32DevIdx], MMW_DEMO_CSI2_CONTEXT, &gConfigCsirx0ContextConfig[0]);
#endif
        //test_print("CSL_DSS_HWA_BANK_SIZE = %X\n", CSL_DSS_HWA_BANK_SIZE);
        if(errorCode != SystemP_SUCCESS)
        {
            //System_printf("CSIRX_configContext failed, errorCode = %d\n", errorCode);
            MmwDemo_debugAssert (0);
            return;
        }

        /* enable context */
        errorCode = CSIRX_contextEnable(obj->csirxHandle[u32DevIdx], MMW_DEMO_CSI2_CONTEXT);
        if(errorCode != SystemP_SUCCESS)
        {
            //System_printf("CSIRX_enableContext failed, errorCode = %d\n", errorCode);
            MmwDemo_debugAssert (0);
            return;
        }

        /* enable interface */
        errorCode = CSIRX_commonEnable(obj->csirxHandle[u32DevIdx]);
        if(errorCode != SystemP_SUCCESS)
        {
            MmwDemo_debugAssert (0);
            return;
        }
 
    }

    SemaphoreP_post(&gMmwDssMCB.CSI2RXConfigCompleteSemHandle);

    /* Reset will be really effective when both front-end will start to drive CSI-2 lines */
    for(u32DevIdx = 0U; u32DevIdx < MMWAVE_RADAR_DEVICES; u32DevIdx++)
    {
        /* Wait until complex IO reset complete */
        numComplexIOresetDonePolls = 0;
        do
        {
            errorCode= CSIRX_complexioIsResetDone(obj->csirxHandle[u32DevIdx], (bool *)&isComplexIOresetDone);
            if(errorCode != SystemP_SUCCESS)
            {
                MmwDemo_debugAssert (0);
                return;
            }
            if (isComplexIOresetDone == false)
            {
                ClockP_usleep(1 * 1000U);
            }
            numComplexIOresetDonePolls++;
        }while((isComplexIOresetDone == false));
    }

    if(isComplexIOresetDone == false)
    {
        MmwDemo_debugAssert (0);
        return;
    }
}

/**
 *  @b Description
 *  @n
 *      Callback function for common.irq interrupt, generated when
 *      end of frame code and line code detected, as per the set configuration.
 * 
 *  @param[in] handle      CSIRX Handle
 *  @param[in] arg         Callback function argument
 *  @param[out] IRQ        CSIRX common irq
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_csirxCommonCallback(CSIRX_Handle handle, void *arg,
                              struct CSIRX_CommonIntr_s *IRQ)
{
    uint8_t i;
    uint32_t csiRxInstance = *((uint32_t*)arg);
    uint32_t frameCounter = gCSIRXState[csiRxInstance].contextIRQcounts[csiRxInstance].frameEndCodeDetect + 1;

    MmwDemo_debugAssert (handle != NULL);

    /*MmwDemo_debugAssert (*arg == MMW_DEMO_CSI2_COMMON_CB_ARG);*/
    gCSIRXState[csiRxInstance].callbackCount.common++;

    gCSIRXState[csiRxInstance].IRQ.common = *IRQ;

    /* Counts book-keeping */
    if(IRQ->isOcpError == true)
    {
        gCSIRXState[csiRxInstance].commonIRQcount.isOCPerror++;
    }
    if(IRQ->isComplexioError == true)
    {
        gCSIRXState[csiRxInstance].commonIRQcount.isComplexIOerror++;
    }
    if(IRQ->isFifoOverflow == true)
    {
        gCSIRXState[csiRxInstance].commonIRQcount.isFIFOoverflow++;
    }

    if(IRQ->isComplexioError)
    {
        gCSIRXErrorCode[csiRxInstance] = CSIRX_complexioGetPendingIntr(handle, &gCSIRXState[csiRxInstance].IRQ.complexIOlanes);
        if(gCSIRXErrorCode[csiRxInstance] != SystemP_SUCCESS)
        {
            //System_printf("Error occured while recieving the frame-%d\n", frameCounter);
        }
        MmwDemo_debugAssert(gCSIRXErrorCode[csiRxInstance] == SystemP_SUCCESS);

        gCSIRXErrorCode[csiRxInstance] = CSIRX_complexioClearAllIntr(handle);
        MmwDemo_debugAssert(gCSIRXErrorCode[csiRxInstance] == SystemP_SUCCESS);
    }

    for(i = 0; i < CONFIG_CSIRX_NUM_INSTANCES; i++)
    {
        if(IRQ->isContextIntr[i] == true)
        {
            gCSIRXErrorCode[csiRxInstance] = CSIRX_contextGetPendingIntr(handle, i, &gCSIRXState[csiRxInstance].IRQ.context[i]);
            MmwDemo_debugAssert(gCSIRXErrorCode[csiRxInstance] == SystemP_SUCCESS);

            if(gCSIRXState[csiRxInstance].IRQ.context[i].isFrameEndCodeDetect == true)
            {
                gCSIRXState[csiRxInstance].contextIRQcounts[i].frameEndCodeDetect++;
            }

            gCSIRXErrorCode[csiRxInstance] = CSIRX_contextClearAllIntr(handle, i);
            MmwDemo_debugAssert(gCSIRXErrorCode[csiRxInstance] == SystemP_SUCCESS);
        }
    }
}

void MmwDemo_combinedEOLcallback(CSIRX_Handle handle, uint32_t arg)
{
#ifdef LVDS_STREAM
    uint32_t baseAddr, regionId;
    int32_t errCode  = 0U;

    DebugP_assert(handle != NULL);

    baseAddr = EDMA_getBaseAddr(gEdmaHandle[CONFIG_EDMA0]);
    DebugP_assert(baseAddr != 0);

    regionId = EDMA_getRegionId(gEdmaHandle[CONFIG_EDMA0]);
    DebugP_assert(regionId < SOC_EDMA_NUM_REGIONS);

    EDMA_enableTransferRegion(baseAddr, regionId, DPC_OBJDET_DPU_RANGEPROC_EDMAIN_CH, EDMA_TRIG_MODE_MANUAL);

    if(!gMmwDssMCB.lvdsStream.isSWSessionActivated)
    {
        /* Activate CBUFF SW Session to transmit ADC data. */
        /* If SW LVDS stream is enabled, start the session here. User data will immediately
        start to stream over LVDS.*/
        if(CBUFF_activateSession (gMmwDssMCB.lvdsStream.swSessionHandle, &errCode) < 0)
        {
            /* Error: Unable to activate the CBUFF SW session */
            test_print("Error: CBUFF_activateSession unable to activate CBUFF SW session with [Error=%d]\n", errCode);
            DebugP_assert(0);
        }

        /* Only first time SW session is activated with CBUFF_activateSession() to
         * configure LVDS interface for the Chirp transfer. This configuration
         * is no required to change for streaming next chirps data as existing
         * configuration is same for the next chirps. */
        gMmwDssMCB.lvdsStream.isSWSessionActivated = true;
    }
    else
    {
        /* Configure SRC Address of CBUFF EDMA channel. This is required when CSIRX is
         * configured in Lane Switching mode. */
        /* No change in the size of the data to be transferred. */
        MmwDemo_configureTransfer();

        /* Trigger CBUFF SW Session. */
        *((volatile uint32_t *)CSL_DSS_CBUFF_U_BASE) |= 1 << 25;
        *((volatile uint32_t *)CSL_DSS_CBUFF_U_BASE) |= 1 << 24;
    }
#endif

    gCSIRXState[*((uint32_t*)arg)].callbackCount.combinedEOL++;

}

/**
 *  @b Description
 *  @n
 *      Callback function for when end of frame detected,
 *      used for debug purposes
 * 
 *  @param[in] handle      CSIRX Handle
 *  @param[in] arg         Callback function argument
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_csirxCombinedEOFcallback(CSIRX_Handle handle, uint32_t arg)
{
    MmwDemo_debugAssert(handle != NULL);
    MmwDemo_debugAssert(arg == MMW_DEMO_CSI2_COMBINED_EOF_CB_ARG);
    gCSIRXState[*((uint32_t*)arg)].callbackCount.combinedEOF++;
}

/**
 *  @b Description
 *  @n
 *  This function is called at the init time to initialze data path driver.
 *
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_dataPathInit(MmwDemo_DataPathObj *obj)
{
    /* Initialize HWA */
    MmwDemo_hwaInit(obj);

    /* Initialize CSIRX */
    MmwDemo_csirxInit(obj);

}

/**
 *  @b Description
 *  @n
 *      This function is called at the init time to open data path driver instances.
 *
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_dataPathOpen(MmwDemo_DataPathObj *obj)
{
    /*****************************************************************************
     * Open HWA, EDMA, CSIRX drivers instances
     *****************************************************************************/
    gMmwDssMCB.dataPathObj.edmaHandle[0] = gEdmaHandle[0];
    MmwDemo_hwaOpen(obj);
    MmwDemo_csirxOpen(obj);

}

/**
 *  @b Description
 *  @n
 *      This function is called at the init time to configure data path driver instances.
 *
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_dataPath_CsirxConfig(MmwDemo_DataPathObj *obj)
{
    /* Configure the CSIRX driver */
    MmwDemo_csirxConfig(obj); 

}

/**
 *  @b Description
 *  @n
 *  This function is called to close data path driver instances.
 *
 *  @param[in] obj      Pointer to data path object
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_dataPathClose(MmwDemo_DataPathObj *obj)
{
    /* DPC close */
    DPM_deinit(obj->objDetDpmHandle);
    
    /* Close HWA driver */
    MmwDemo_hwaClose(obj);

    /* Close EDMA driver */
    MmwDemo_edmaClose(obj);

    /* Close CSIRX driver */
    MmwDemo_csirxClose(obj); 
}
