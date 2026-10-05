/**
 *   @file  mss_main.c
 *
 *   @brief
 *      MSS main file of the IWR1843 SAR / LVDS raw-ADC firmware (MSS-only),
 *      derived from the TI xwr18xx millimeter wave demo
 *
 *  \par
 *  NOTE:
 *      (C) Copyright 2018 Texas Instruments, Inc.
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
/** @mainpage IWR1843 SAR / LVDS raw-ADC firmware (MSS-only)
 *
 *  Derived from the TI mmWave SDK 3.6 xwr18xx mmw demo (baseline in
 *  project.env). The object-detection chain (DPM/DPC on the DSS), the DSS
 *  image, the UART TLV output and the LVDS SW session were removed. What is
 *  left runs on the MSS (R4F) only:
 *
 *  - CLI (UART, 115200): the SDK mmWave extension (channel/adc/profile/chirp/
 *    frame cfg) plus adcbufCfg, lvdsStreamCfg, analogMonitor, CQRxSatMonitor,
 *    CQSigImgMonitor, calibData, queryDemoStatus, sensorStart, sensorStop.
 *  - ADCBUF configured from the MSS at every sensor start.
 *  - One CBUFF HW session that streams the ADC (or ADC + per-chirp SAR
 *    metadata record, or CP+ADC+CQ) data of every chirp over LVDS to the
 *    DCA1000. It is created and activated at sensor start, stays active
 *    across frame boundaries (nothing waits on a frame event), and is
 *    deactivated and deleted at sensor stop.
 *  - Per-chirp metadata (mmw_sar_meta.c, docs/lvds_data_format.md): chirp-
 *    start, chirp-available and frame-start interrupts fill the record slots
 *    (counters, RTI timestamp, saturation of the previous chirp) and the
 *    "sarStats" counters.
 *
 *  @section restart Stop / reconfigure / restart without a power cycle
 *    sensorStop waits for the BSS frame-end event, then tears down the HW
 *    session (its EDMA channels are returned) and closes ADCBUF. After
 *    flushCfg and a full cfg, sensorStart re-runs MMWave_config and the RF
 *    parser, re-opens and re-configures ADCBUF and re-creates the HW session,
 *    so every profile/chirp/frame/adcbuf/lvds parameter may change (rxGain,
 *    HPF corners, sample count and rate included). channelCfg, adcCfg and
 *    lowPower are applied by MMWave_open on the first start only; a later
 *    start with different values is rejected with a CLI error (power cycle
 *    to change them). "sensorStart 0" restarts with the previous cfg.
 *
 *  @section calib Calibration
 *    Boot calibration runs in MMWave_open (first start). Each sensorStart
 *    runs TI's one-time runtime calibration before the first frame; periodic
 *    runtime calibration is disabled (enablePeriodicity = false), so no
 *    calibration is applied in the inter-frame blank during a capture.
 *
 *  @section LVDSStreamingNotes LVDS HW data sizing (from TI's demo notes, corrected)
 *    -# Bytes per chirp: H + numAdcSamples * numRxChannels * 4 (+ 64 for the two
 *       SAR metadata record slots of dataFmt 2). H = 0 with the HSI header off;
 *       with it on, H = 52 [sizeof(HSIDataCardHeader_t) + sizeof(HSISDKHeader_t)]
 *       plus padding that makes the whole packet a multiple of 16 bytes:
 *       HSIHeader_createHeader(..., false, ...) aligns to 8 CBUFF units
 *       (hsiheader.c:286-306). TI's "round up to 256" note holds only for
 *       bAlignDataCard = true. See docs/lvds_data_format.md.
 *       It must fit in one chirp period: Tc * n * B / 8 >= that size, with
 *       Tc = idle + ramp end time (us), n = 2 lanes, B = 600 Mbps per lane
 *       (MmwDemo_mssSetHsiClk).
 *    -# A HW transfer must be at least 64 bytes (CBUFF_MIN_TRANSFER_SIZE_CBUFF_UNITS);
 *       with the HSI header on, every transfer is at least 256 bytes.
 *    -# A CBUFF link-list entry is limited to 0x3FFF CBUFF units (32766 bytes).
 *    -# EDMA channels for the HW session are listed in mmw_res.h; the
 *       CP_ADC_CQ format needs the most (11 + the CBUFF trigger channel).
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


/* BIOS/XDC Include Files. */
#include <xdc/std.h>
#include <xdc/cfg/global.h>
#include <xdc/runtime/IHeap.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Error.h>
#include <xdc/runtime/Memory.h>
#include <ti/sysbios/BIOS.h>
#include <ti/sysbios/knl/Task.h>
#include <ti/sysbios/knl/Event.h>
#include <ti/sysbios/knl/Semaphore.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/heaps/HeapBuf.h>
#include <ti/sysbios/heaps/HeapMem.h>
#include <ti/sysbios/family/arm/v7a/Pmu.h>
#include <ti/sysbios/family/arm/v7r/vim/Hwi.h>
#include <ti/sysbios/utils/Load.h>

/* mmWave SDK Include Files: */
#include <ti/common/sys_common.h>
#include <ti/common/mmwave_sdk_version.h>
#include <ti/drivers/soc/soc.h>
#include <ti/drivers/esm/esm.h>
#include <ti/drivers/crc/crc.h>
#include <ti/drivers/gpio/gpio.h>
#include <ti/drivers/mailbox/mailbox.h>
#include <ti/drivers/pinmux/pinmux.h>
#include <ti/drivers/adcbuf/ADCBuf.h>
#include <ti/drivers/cbuff/cbuff.h>
#include <ti/control/mmwave/mmwave.h>
#include <ti/drivers/osal/DebugP.h>
#include <ti/drivers/uart/UART.h>
#include <ti/utils/cli/cli.h>
#include <ti/utils/mathutils/mathutils.h>

/* Demo Include Files */
#include <ti/demo/xwr18xx/mmw/include/mmw_config.h>
#include <ti/demo/utils/mmwdemo_rfparser.h>
#include <ti/demo/utils/mmwdemo_adcconfig.h>
#include <ti/demo/utils/mmwdemo_monitor.h>
#include <ti/demo/xwr18xx/mmw/mmw_res.h>
#include <ti/demo/xwr18xx/mmw/mss/mmw_mss.h>
#include <ti/demo/xwr18xx/mmw/mss/mmw_sar_meta.h>
#include <ti/demo/utils/mmwdemo_flash.h>

/* Profiler Include Files */
#include <ti/utils/cycleprofiler/cycle_profiler.h>

#ifdef SYS_COMMON_RTRIM_MODIFY_EN
#include <ti/utils/rtrim/rtrimutils.h>
#endif

/**
 * @brief Task Priority settings:
 * Mmwave task is at higher priority because of potential async messages from BSS
 * that need quick action in real-time.
 */
#define MMWDEMO_CLI_TASK_PRIORITY                 3
#define MMWDEMO_MMWAVE_CTRL_TASK_PRIORITY         5

/**
 * @brief How long sensorStop waits for the BSS frame-end event, in BIOS clock
 *        ticks (1 ms). A legacy frame is at most 1.342 s (rl_sensor.h:986-987).
 */
#define MMWDEMO_FRAME_END_TIMEOUT_TICKS           3000U

/* These address offsets are in bytes, when configure address offset in hardware,
   these values will be converted to number of 128bits
   Buffer at offset 0x0U is reserved by BSS, hence offset starts from 0x800
 */
#define MMW_DEMO_CQ_SIGIMG_ADDR_OFFSET          0x800U
#define MMW_DEMO_CQ_RXSAT_ADDR_OFFSET           0x1000U

/* CQ data is at 16 bytes alignment for mulitple chirps */
#define MMW_DEMO_CQ_DATA_ALIGNMENT            16U

/**************************************************************************
 *************************** Global Definitions ***************************
 **************************************************************************/

/**
 * @brief
 *  Global Variable for tracking information required by the mmw Demo
 */
MmwDemo_MSS_MCB    gMmwMssMCB;

/**
 * @brief
 *  Global Variable for LDO BYPASS config   
 **********************************************************
  AS PER RECOMMENDATION FROM THE xWR1843BOOST EVM User guide
 **********************************************************
   set LDO bypass since the board has 1.0V RF supply 1 
   and 1.0V RF supply 2. Please update this API if this 
   assumption is not valid else it may DAMAGE your board!
 **********************************************************
 **********************************************************
 */
rlRfLdoBypassCfg_t gRFLdoBypassCfg =
{
    .ldoBypassEnable   = 3, /* 1.0V RF supply 1 and 1.0V RF supply 2 */
    .supplyMonIrDrop   = 1, /* IR drop of 3% */
    .ioSupplyIndicator = 0, /* 3.3 V IO supply */
};


/* Calibration Data Save/Restore defines */
#define MMWDEMO_CALIB_FLASH_SIZE	              4096
#define MMWDEMO_CALIB_STORE_MAGIC            (0x7CB28DF9U)

MmwDemo_calibData gCalibDataStorage;
#pragma DATA_ALIGN(gCalibDataStorage, 8);

/**************************************************************************
 *************************** Extern Definitions ***************************
 **************************************************************************/

extern void MmwDemo_CLIInit(uint8_t taskPriority);

/**************************************************************************
 ************************* Millimeter Wave Demo Functions prototype *************
 **************************************************************************/

/* ADCBUF + LVDS HW session (MSS data path) */
static int32_t MmwDemo_dataPathStart (void);
static void MmwDemo_dataPathStop (void);

static void MmwDemo_initTask(UArg arg0, UArg arg1);
static void MmwDemo_platformInit(MmwDemo_platformCfg *config);

/* Mmwave control functions */
static void MmwDemo_mmWaveCtrlTask(UArg arg0, UArg arg1);
static int32_t MmwDemo_mmWaveCtrlStop (void);
static int32_t MmwDemo_eventCallbackFxn(uint16_t msgId, uint16_t sbId, uint16_t sbLen, uint8_t *payload);

/* external sleep function when in idle (used in .cfg file) */
void MmwDemo_sleep(void);

/* Edma related functions */
static void MmwDemo_edmaInit(void);
static void MmwDemo_edmaOpen(void);
static void MmwDemo_EDMA_transferControllerErrorCallbackFxn(EDMA_Handle handle,
                EDMA_transferControllerErrorInfo_t *errorInfo);
static void MmwDemo_EDMA_errorCallbackFxn(EDMA_Handle handle, EDMA_errorInfo_t *errorInfo);
static void MmwDemo_checkEdmaErrors(void);

static int32_t MmwDemo_configMonitors(MmwDemo_SubFrameCfg *subFrameCfg,
                                      uint8_t numChirpsPerChirpEvent,
                                      uint8_t validProfileIdx);

/* Calibration save/restore APIs */
static int32_t MmwDemo_calibInit(void);
static int32_t MmwDemo_calibSave(MmwDemo_calibDataHeader *ptrCalibDataHdr, MmwDemo_calibData  *ptrCalibrationData);
static int32_t MmwDemo_calibRestore(MmwDemo_calibData  *calibrationData);

/**************************************************************************
 ************************* Millimeter Wave Demo Functions **********************
 **************************************************************************/
/**
 *  @b Description
 *  @n
 *      Send assert information through CLI.
 */
void _MmwDemo_debugAssert(int32_t expression, const char *file, int32_t line)
{
    if (!expression) {
        CLI_write ("Exception: %s, line %d.\n",file,line);
    }
}

/**
 *  @b Description
 *  @n
 *      Utility function to set the pending state of configuration.
 *
 *  @param[in] subFrameCfg Pointer to Sub-frame specific configuration
 *  @param[in] offset Configuration structure offset that uniquely identifies the
 *                    configuration to set to the pending state.
 *
 *  @retval None
 */
static void MmwDemo_setSubFramePendingState(MmwDemo_SubFrameCfg *subFrameCfg, uint32_t offset)
{
    switch (offset)
    {
        case MMWDEMO_ADCBUFCFG_OFFSET:
            subFrameCfg->isAdcBufCfgPending = 1;
        break;
        case MMWDEMO_LVDSSTREAMCFG_OFFSET:
            subFrameCfg->isLvdsStreamCfgPending = 1;
        break;
        default:
            MmwDemo_debugAssert(0);
        break;
    }
}

/**
 *  @b Description
 *  @n
 *      Resets (clears) all pending static (non-dynamic) configuration
 *
 */
void MmwDemo_resetStaticCfgPendingState(void)
{
    uint8_t indx;

    for(indx = 0; indx < gMmwMssMCB.numSubFrames; indx++)
    {
        gMmwMssMCB.subFrameCfg[indx].isAdcBufCfgPending = 0;
        gMmwMssMCB.subFrameCfg[indx].isLvdsStreamCfgPending = 0;
    }

    gMmwMssMCB.isAnaMonCfgPending = 0;
    gMmwMssMCB.isCalibCfgPending = 0;

}

/**
 *  @b Description
 *  @n
 *      Utility function to find out if all configuration (adcbufCfg,
 *      lvdsStreamCfg, analogMonitor, calibData) is in pending state.
 *
 *  @retval 1 if all configuration is in pending state, else return 0
 */
uint8_t MmwDemo_isAllCfgInPendingState(void)
{
    uint8_t indx, flag = 1;

    for(indx = 0; indx < gMmwMssMCB.numSubFrames; indx++)
    {
        flag = flag && (gMmwMssMCB.subFrameCfg[indx].isAdcBufCfgPending == 1);
        flag = flag && (gMmwMssMCB.subFrameCfg[indx].isLvdsStreamCfgPending == 1);
    }

    flag = flag && (gMmwMssMCB.isAnaMonCfgPending == 1);
    flag = flag && (gMmwMssMCB.isCalibCfgPending == 1);

    return(flag);
}

/**
 *  @b Description
 *  @n
 *      Utility function to find out if all configuration (adcbufCfg,
 *      lvdsStreamCfg, analogMonitor, calibData) is in non-pending (cleared) state.
 *
 *  @retval 1 if all configuration is in non-pending state, else return 0
 */
uint8_t MmwDemo_isAllCfgInNonPendingState(void)
{
    uint8_t indx, flag = 1;

    for(indx = 0; indx < gMmwMssMCB.numSubFrames; indx++)
    {
        flag = flag && (gMmwMssMCB.subFrameCfg[indx].isAdcBufCfgPending == 0);
        flag = flag && (gMmwMssMCB.subFrameCfg[indx].isLvdsStreamCfgPending == 0);
    }

    flag = flag && (gMmwMssMCB.isAnaMonCfgPending == 0);
    flag = flag && (gMmwMssMCB.isCalibCfgPending == 0);

    return(flag);
}

/**
 *  @b Description
 *  @n
 *      Utility function to apply configuration to specified sub-frame
 *
 *  @param[in] srcPtr Pointer to configuration
 *  @param[in] offset Offset of configuration within the parent structure
 *  @param[in] size   Size of configuration
 *  @param[in] subFrameNum Sub-frame Number (0 based) to apply to, broadcast to
 *                         all sub-frames if special code MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
void MmwDemo_CfgUpdate(void *srcPtr, uint32_t offset, uint32_t size, int8_t subFrameNum)
{    
    /* if subFrameNum undefined, broadcast to all sub-frames */
    if(subFrameNum == MMWDEMO_SUBFRAME_NUM_FRAME_LEVEL_CONFIG)
    {
        uint8_t  indx;
        for(indx = 0; indx < RL_MAX_SUBFRAMES; indx++)
        {
            memcpy((void *)((uint32_t) &gMmwMssMCB.subFrameCfg[indx] + offset), srcPtr, size);
            MmwDemo_setSubFramePendingState(&gMmwMssMCB.subFrameCfg[indx], offset);
        }
    }
    else
    {
        /* Apply configuration to specific subframe (or to position zero for the legacy case
           where there is no advanced frame config) */
        memcpy((void *)((uint32_t) &gMmwMssMCB.subFrameCfg[subFrameNum] + offset), srcPtr, size);
        MmwDemo_setSubFramePendingState(&gMmwMssMCB.subFrameCfg[subFrameNum], offset);
    }
}

/**************************************************************************
 ******************** Millimeter Wave Demo control path Functions *****************
 **************************************************************************/
/**
 *  @b Description
 *  @n
 *      The function is used to trigger the Front end to stop generating chirps.
 *
 *  @retval
 *      Not Applicable.
 */
static int32_t MmwDemo_mmWaveCtrlStop (void)
{
    int32_t                 errCode = 0;

    DebugP_log0("App: Issuing MMWave_stop\n");

    /* Stop the mmWave module: */
    if (MMWave_stop (gMmwMssMCB.ctrlHandle, &errCode) < 0)
    {
        MMWave_ErrorLevel   errorLevel;
        int16_t             mmWaveErrorCode;
        int16_t             subsysErrorCode;

        /* Error/Warning: Unable to stop the mmWave module */
        MMWave_decodeError (errCode, &errorLevel, &mmWaveErrorCode, &subsysErrorCode);
        if (errorLevel == MMWave_ErrorLevel_ERROR)
        {
            /* Error: Display the error message: */
            System_printf ("Error: mmWave Stop failed [Error code: %d Subsystem: %d]\n",
                            mmWaveErrorCode, subsysErrorCode);

            /* Not expected */
            MmwDemo_debugAssert(0);
        }
        else
        {
            /* Warning: This is treated as a successful stop. */
            System_printf ("mmWave Stop error ignored [Error code: %d Subsystem: %d]\n",
                            mmWaveErrorCode, subsysErrorCode);
        }
    }

    return errCode;
}


/**
 *  @b Description
 *  @n
 *      The task is used to provide an execution context for the mmWave
 *      control task
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_mmWaveCtrlTask(UArg arg0, UArg arg1)
{
    int32_t errCode;

    while (1)
    {
        /* Execute the mmWave control module: */
        if (MMWave_execute (gMmwMssMCB.ctrlHandle, &errCode) < 0)
        {
            //System_printf ("Error: mmWave control execution failed [Error code %d]\n", errCode);
            MmwDemo_debugAssert (0);
        }
    }
}

/**************************************************************************
 ******************** Millimeter Wave Demo data path Functions *******************
 **************************************************************************/

/**
 *  @b Description
 *  @n
 *      Configures the BSS-side chirp-quality monitors (RX saturation CQ2,
 *      signal/image band CQ1, analog monitor enables) and computes the CQ
 *      sizes the CP_ADC_CQ HW session needs. Called on every reconfig, after
 *      MMWave_config. The ADCBUF side of the CQ is configured at sensor start
 *      (ADCBUF is re-opened there).
 *
 *  @param[in] subFrameCfg Pointer to sub-frame config
 *  @param[in] numChirpsPerChirpEvent number of chirps per chirp event
 *  @param[in] validProfileIdx valid profile index
 *
 *  @retval
 *      0 if no error, else error (there will be system prints for these).
 */
static int32_t MmwDemo_configMonitors(MmwDemo_SubFrameCfg *subFrameCfg,
                                      uint8_t numChirpsPerChirpEvent,
                                      uint8_t validProfileIdx)
{
    MmwDemo_AnaMonitorCfg*      ptrAnaMonitorCfg;
    rlRxSatMonConf_t*           ptrSatMonCfg;
    rlSigImgMonConf_t*          ptrSigImgMonCfg;
    int32_t                     retVal = 0;
    uint16_t                    cqChirpSize;

    /* Get analog monitor configuration */
    ptrAnaMonitorCfg = &gMmwMssMCB.anaMonCfg;

    subFrameCfg->sigImgMonTotalSize = 0;
    subFrameCfg->satMonTotalSize    = 0;

    /* Config mmwaveLink to enable Saturation monitor - CQ2 */
    ptrSatMonCfg = &gMmwMssMCB.cqSatMonCfg[validProfileIdx];

    if (ptrAnaMonitorCfg->rxSatMonEn)
    {
        if (ptrSatMonCfg->profileIndx != validProfileIdx)
        {
            CLI_write ("Error: analogMonitor enables rxSaturation but CQRxSatMonitor is not configured for profile %d\n",
                       validProfileIdx);
            retVal = -1;
            goto exit;
        }

        retVal = mmwDemo_cfgRxSaturationMonitor(ptrSatMonCfg);
        if(retVal != 0)
        {
            System_printf ("Error: rlRfRxIfSatMonConfig returns error = %d for profile(%d)\n",
                           retVal, ptrSatMonCfg->profileIndx);
            goto exit;
        }
    }

    /* Config mmwaveLink to enable Signal/Image band monitor - CQ1 */
    ptrSigImgMonCfg = &gMmwMssMCB.cqSigImgMonCfg[validProfileIdx];

    if (ptrAnaMonitorCfg->sigImgMonEn)
    {
        if (ptrSigImgMonCfg->profileIndx != validProfileIdx)
        {
            CLI_write ("Error: analogMonitor enables sigImgBand but CQSigImgMonitor is not configured for profile %d\n",
                       validProfileIdx);
            retVal = -1;
            goto exit;
        }

        retVal = mmwDemo_cfgRxSigImgMonitor(ptrSigImgMonCfg);
        if(retVal != 0)
        {
            System_printf ("Error: rlRfRxSigImgMonConfig returns error = %d for profile(%d)\n",
                           retVal, ptrSigImgMonCfg->profileIndx);
            goto exit;
        }
    }

    retVal = mmwDemo_cfgAnalogMonitor(ptrAnaMonitorCfg);
    if (retVal != 0)
    {
        System_printf ("Error: rlRfAnaMonConfig returns error = %d\n", retVal);
        goto exit;
    }

    if (ptrAnaMonitorCfg->sigImgMonEn)
    {
        /* This is for 16bit format in mmw demo, signal/image band data has 2 bytes/slice
           For other format, please check DFP interface document
         */
        cqChirpSize = (ptrSigImgMonCfg->numSlices + 1) * sizeof(uint16_t);
        cqChirpSize = MATHUTILS_ROUND_UP_UNSIGNED(cqChirpSize, MMW_DEMO_CQ_DATA_ALIGNMENT);
        subFrameCfg->sigImgMonTotalSize = cqChirpSize * numChirpsPerChirpEvent;
    }

    if (ptrAnaMonitorCfg->rxSatMonEn)
    {
        /* This is for 16bit format in mmw demo, saturation data has one byte/slice
           For other format, please check DFP interface document
         */
        cqChirpSize = (ptrSatMonCfg->numSlices + 1) * sizeof(uint8_t);
        cqChirpSize = MATHUTILS_ROUND_UP_UNSIGNED(cqChirpSize, MMW_DEMO_CQ_DATA_ALIGNMENT);
        subFrameCfg->satMonTotalSize = cqChirpSize * numChirpsPerChirpEvent;
    }

exit:
    return(retVal);
}

/**
 *  @b Description
 *  @n
 *      Sensor-start half of the MSS data path: opens ADCBUF, configures it
 *      (data format, RX channels, chirp thresholds, CQ) from the stored
 *      configuration, then creates and activates the CBUFF HW session so
 *      ADC data streams from the first chirp. Called on every sensorStart
 *      (with or without reconfig) before MMWave_start; undone by
 *      @ref MmwDemo_dataPathStop.
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0 (anything created here has been torn down again)
 */
static int32_t MmwDemo_dataPathStart (void)
{
    int32_t                 retVal;
    MmwDemo_SubFrameCfg     *subFrameCfg = &gMmwMssMCB.subFrameCfg[0];
    uint16_t                rxChanOffset[SYS_COMMON_NUM_RX_CHANNEL];
    ADCBuf_CQConf           cqConfig;

    DebugP_assert(gMmwMssMCB.adcBufHandle == NULL);
    DebugP_assert(gMmwMssMCB.lvdsStream.hwSessionHandle == NULL);

    /* ADCBUF: re-created on every start */
    gMmwMssMCB.adcBufHandle = MmwDemo_ADCBufOpen(gMmwMssMCB.socHandle);
    if (gMmwMssMCB.adcBufHandle == NULL)
    {
        System_printf("Error: ADCBuf open failed\n");
        return -1;
    }

    retVal = MmwDemo_ADCBufConfig(gMmwMssMCB.adcBufHandle,
                                  gMmwMssMCB.cfg.openCfg.chCfg.rxChannelEn,
                                  subFrameCfg->numChirpsPerChirpEvent,
                                  subFrameCfg->adcBufChanDataSize,
                                  &subFrameCfg->adcBufCfg,
                                  &rxChanOffset[0]);
    if (retVal < 0)
    {
        System_printf("Error: ADCBuf config failed with error[%d]\n", retVal);
        goto error;
    }

    if (gMmwMssMCB.anaMonCfg.rxSatMonEn || gMmwMssMCB.anaMonCfg.sigImgMonEn)
    {
        /* CQ driver config */
        memset((void *)&cqConfig, 0, sizeof(ADCBuf_CQConf));
        cqConfig.cqDataWidth = 0; /* 16bit for mmw demo */
        cqConfig.cq1AddrOffset = MMW_DEMO_CQ_SIGIMG_ADDR_OFFSET; /* CQ1 starts from the beginning of the buffer */
        cqConfig.cq2AddrOffset = MMW_DEMO_CQ_RXSAT_ADDR_OFFSET;  /* Address should be 16 bytes aligned */

        retVal = ADCBuf_control(gMmwMssMCB.adcBufHandle, ADCBufMMWave_CMD_CONF_CQ, (void *)&cqConfig);
        if (retVal < 0)
        {
            System_printf ("Error: Unable to configure the CQ [Error=%d]\n", retVal);
            goto error;
        }
    }

    /* LVDS HW session for the (single) sub-frame, activated before the first chirp */
    if (subFrameCfg->lvdsStreamCfg.dataFmt != MMW_DEMO_LVDS_STREAM_CFG_DATAFMT_DISABLED)
    {
        gMmwMssMCB.lvdsStream.hwFrameDoneCount = 0;
        retVal = MmwDemo_configLVDSHwData(0);
        if (retVal < 0)
        {
            goto error;
        }
    }
    return 0;

error:
    MmwDemo_dataPathStop();
    return -1;
}

/**
 *  @b Description
 *  @n
 *      Sensor-stop half of the MSS data path, called once the BSS has stopped
 *      framing: deactivates and deletes the HW session (which returns its EDMA
 *      channels and HSI header) and closes ADCBUF. Safe to call when only part
 *      of @ref MmwDemo_dataPathStart was done.
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_dataPathStop (void)
{
    int32_t errCode;

    if (gMmwMssMCB.lvdsStream.hwSessionHandle != NULL)
    {
        /* check Edma errors (fatal) for the last frame's transfers */
        MmwDemo_checkEdmaErrors();

        /* Single sub-frame, no SW session: the HW session was never deactivated
         * after its activation at start, so deactivate it here. */
        if (CBUFF_deactivateSession(gMmwMssMCB.lvdsStream.hwSessionHandle, &errCode) < 0)
        {
            System_printf("CBUFF_deactivateSession failed with errorCode = %d\n", errCode);
            MmwDemo_debugAssert(0);
        }
        if (MmwDemo_LVDSStreamDeleteHwSession() < 0)
        {
            MmwDemo_debugAssert(0);
        }
    }

    if (gMmwMssMCB.adcBufHandle != NULL)
    {
        ADCBuf_close(gMmwMssMCB.adcBufHandle);
        gMmwMssMCB.adcBufHandle = NULL;
    }
}

/**
 *  @b Description
 *  @n
 *      Registered event function to mmwave which is invoked when an event from the
 *      BSS is received.
 *
 *  @param[in]  msgId
 *      Message Identifier
 *  @param[in]  sbId
 *      Subblock identifier
 *  @param[in]  sbLen
 *      Length of the subblock
 *  @param[in]  payload
 *      Pointer to the payload buffer
 *
 *  @retval
 *      Always return 0
 */
static int32_t MmwDemo_eventCallbackFxn(uint16_t msgId, uint16_t sbId, uint16_t sbLen, uint8_t *payload)
{
    uint16_t asyncSB = RL_GET_SBID_FROM_UNIQ_SBID(sbId);

    /* Process the received message: */
    switch (msgId)
    {
        case RL_RF_ASYNC_EVENT_MSG:
        {
            /* Received Asychronous Message: */
            switch (asyncSB)
            {
                case RL_RF_AE_CPUFAULT_SB:
                {
                    MmwDemo_debugAssert(0);
                    break;
                }
                case RL_RF_AE_ESMFAULT_SB:
                {
                    MmwDemo_debugAssert(0);
                    break;
                }
                case RL_RF_AE_ANALOG_FAULT_SB:
                {
                    MmwDemo_debugAssert(0);
                    break;
                }
                case RL_RF_AE_INITCALIBSTATUS_SB:
                {
                    rlRfInitComplete_t*  ptrRFInitCompleteMessage;
                    uint32_t            calibrationStatus;

                    /* Get the RF-Init completion message: */
                    ptrRFInitCompleteMessage = (rlRfInitComplete_t*)payload;
                    calibrationStatus = ptrRFInitCompleteMessage->calibStatus & 0x1FFFU;

                    /* Display the calibration status: */
                    CLI_write ("Debug: Init Calibration Status = 0x%x\n", calibrationStatus);
                    break;
                }
                case RL_RF_AE_FRAME_TRIGGER_RDY_SB:
                {
                    gMmwMssMCB.stats.frameTriggerReady++;
                    break;
                }
                case RL_RF_AE_MON_TIMING_FAIL_REPORT_SB:
                {
                    gMmwMssMCB.stats.failedTimingReports++;
                    break;
                }
                case RL_RF_AE_RUN_TIME_CALIB_REPORT_SB:
                {
                    gMmwMssMCB.stats.calibrationReports++;
                    break;
                }
                case RL_RF_AE_FRAME_END_SB:
                {
                    gMmwMssMCB.stats.sensorStopped++;
                    DebugP_log0("App: BSS stop (frame end) received\n");

                    /* Frames have ended: let the stop path tear down the HW session */
                    Semaphore_post(gMmwMssMCB.frameEndSemHandle);
                    break;
                }
                default:
                {
                    System_printf ("Error: Asynchronous Event SB Id %d not handled\n", asyncSB);
                    break;
                }
            }
            break;
        }
        /* Async Event from MMWL */
        case RL_MMWL_ASYNC_EVENT_MSG:
        {
            switch (asyncSB)
            {
                case RL_MMWL_AE_MISMATCH_REPORT:
                {
                    /* link reports protocol error in the async report from BSS */
                    MmwDemo_debugAssert(0);
                    break;
                }            
                case RL_MMWL_AE_INTERNALERR_REPORT:
                {
                    /* link reports internal error during BSS communication */
                    MmwDemo_debugAssert(0);
                    break;
                }
            }
            break;
        }
        default:
        {
            System_printf ("Error: Asynchronous message %d is NOT handled\n", msgId);
            break;
        }
    }
    return 0;
}

/**
 *  @b Description
 *  @n
 *      Function to Setup the HSI Clock. Required for LVDS streaming.
 *
 *  @retval
 *      0  - Success.
 *      <0 - Failed with errors
 */
int32_t MmwDemo_mssSetHsiClk(void)
{
    rlDevHsiClk_t                           hsiClkgs;
    int32_t                                 retVal;

    /*************************************************************************************
     * Setup the HSI Clock through the mmWave Link:
     *************************************************************************************/
    memset ((void*)&hsiClkgs, 0, sizeof(rlDevHsiClk_t));

    /* Setup the HSI Clock as per the Radar Interface Document:
     * - This is set to 600Mhz DDR Mode */
    hsiClkgs.hsiClk = 0x9;

    /* Setup the HSI in the radar link: */
    retVal = rlDeviceSetHsiClk(RL_DEVICE_MAP_CASCADED_1, &hsiClkgs);
    if (retVal != RL_RET_CODE_OK)
    {
        /* Error: Unable to set the HSI clock */
        System_printf ("Error: Setting up the HSI Clock Failed [Error %d]\n", retVal);
        return -1;
    }

    /*The delay below is needed only if the DCA1000EVM is being used to capture the data traces.
      This is needed because the DCA1000EVM FPGA needs the delay to lock to the
      bit clock before they can start capturing the data correctly. */
    Task_sleep(HSI_DCA_MIN_DELAY_MSEC);

    return 0;
}

/**************************************************************************
 ******************** Millimeter Wave Demo sensor management Functions **********
 **************************************************************************/

/**
 *  @b Description
 *  @n
 *      mmw demo helper Function to do one time sensor initialization. 
 *      User need to fill gMmwMssMCB.cfg.openCfg before calling this function
 *
 *  @param[in]  isFirstTimeOpen     If true then issues MMwave_open
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
int32_t MmwDemo_openSensor(bool isFirstTimeOpen)
{
    int32_t             errCode;
    MMWave_ErrorLevel   errorLevel;
    int16_t             mmWaveErrorCode;
    int16_t             subsysErrorCode;    
    int32_t             retVal;
    MMWave_CalibrationData     calibrationDataCfg;
    MMWave_CalibrationData     *ptrCalibrationDataCfg;

    /*  Open mmWave module, this is only done once */
    if (isFirstTimeOpen == true)
    {

        System_printf ("Debug: Sending rlRfSetLdoBypassConfig with %d %d %d\n",
                                            gRFLdoBypassCfg.ldoBypassEnable,
                                            gRFLdoBypassCfg.supplyMonIrDrop,
                                            gRFLdoBypassCfg.ioSupplyIndicator);

        retVal = rlRfSetLdoBypassConfig(RL_DEVICE_MAP_INTERNAL_BSS, (rlRfLdoBypassCfg_t*)&gRFLdoBypassCfg);        
        if(retVal != 0)
        {            
            System_printf("Error: rlRfSetLdoBypassConfig retVal=%d\n", retVal);
            return -1;
        }

        /**********************************************************
         **********************************************************/         

        /* Open mmWave module, this is only done once */
        /* Setup the calibration frequency:*/
        gMmwMssMCB.cfg.openCfg.freqLimitLow  = 760U;
        gMmwMssMCB.cfg.openCfg.freqLimitHigh = 810U;

        /* start/stop async events */
        gMmwMssMCB.cfg.openCfg.disableFrameStartAsyncEvent = false;
        gMmwMssMCB.cfg.openCfg.disableFrameStopAsyncEvent  = false;

        /* No custom calibration: */
        gMmwMssMCB.cfg.openCfg.useCustomCalibration        = false;
        gMmwMssMCB.cfg.openCfg.customCalibrationEnableMask = 0x0;

        /* calibration monitoring base time unit
         * setting it to one frame duration as the demo doesnt support any 
         * monitoring related functionality
         */
        gMmwMssMCB.cfg.openCfg.calibMonTimeUnit            = 1;

        if( (gMmwMssMCB.calibCfg.saveEnable != 0) &&
		(gMmwMssMCB.calibCfg.restoreEnable != 0) )
        {
            /* Error: only one can be enabled at at time */
            System_printf ("Error: MmwDemo failed with both save and restore enabled.\n");
            return -1;
	 }

        if(gMmwMssMCB.calibCfg.restoreEnable != 0)
        {
            if(MmwDemo_calibRestore(&gCalibDataStorage) < 0)
            {
	            System_printf ("Error: MmwDemo failed restoring calibration data from flash.\n");
	            return -1;
            }

            /*  Boot calibration during restore: Disable calibration for:
                 - Rx gain,
                 - Rx IQMM,
                 - Tx phase shifer,
                 - Tx Power

                 The above calibration data will be restored from flash. Since they are calibrated in a control
                 way to avoid interfaerence and spec violations.
                 In this demo, other bit fields(except the above) are enabled as indicated in customCalibrationEnableMask to perform boot time
                 calibration. The boot time calibration will overwrite the restored calibration data from flash.
                 However other bit fields can be disabled and calibration data can be restored from flash as well.

                 Note: In this demo, calibration masks are enabled for all bit fields when "saving" the data.
            */
            gMmwMssMCB.cfg.openCfg.useCustomCalibration        = true;
            gMmwMssMCB.cfg.openCfg.customCalibrationEnableMask = 0x1F0U;

            calibrationDataCfg.ptrCalibData = &gCalibDataStorage.calibData;
            calibrationDataCfg.ptrPhaseShiftCalibData = &gCalibDataStorage.phaseShiftCalibData;
            ptrCalibrationDataCfg = &calibrationDataCfg;
        }
        else
        {
            ptrCalibrationDataCfg = NULL;
        }

        /* Open the mmWave module: */
        if (MMWave_open (gMmwMssMCB.ctrlHandle, &gMmwMssMCB.cfg.openCfg, ptrCalibrationDataCfg, &errCode) < 0)
        {
            /* Error: decode and Report the error */
            MMWave_decodeError (errCode, &errorLevel, &mmWaveErrorCode, &subsysErrorCode);
            System_printf ("Error: mmWave Open failed [Error code: %d Subsystem: %d]\n",
                            mmWaveErrorCode, subsysErrorCode);
            return -1;
        }

	 /* Save calibration data in flash */
	 if(gMmwMssMCB.calibCfg.saveEnable != 0)
        {

            retVal = rlRfCalibDataStore(RL_DEVICE_MAP_INTERNAL_BSS, &gCalibDataStorage.calibData);
            if(retVal != RL_RET_CODE_OK)
            {
                /* Error: Calibration data restore failed */
	         System_printf("MSS demo failed rlRfCalibDataStore with Error[%d]\n", retVal);
                return -1;
            }

#if (defined(SOC_XWR18XX) || defined(SOC_XWR68XX))

        /* update txIndex in all chunks to get data from all Tx.
           This should be done regardless of num TX channels enabled in MMWave_OpenCfg_t::chCfg or number of Tx
           application is interested in. Data for all existing Tx channels should be retrieved
           from RadarSS and in the order as shown below.
           RadarSS will return non-zero phase shift values for all the channels enabled via
           MMWave_OpenCfg_t::chCfg and zero phase shift values for channels disabled in MMWave_OpenCfg_t::chCfg */
            gCalibDataStorage.phaseShiftCalibData.PhShiftcalibChunk[0].txIndex = 0;
            gCalibDataStorage.phaseShiftCalibData.PhShiftcalibChunk[1].txIndex = 1;
            gCalibDataStorage.phaseShiftCalibData.PhShiftcalibChunk[2].txIndex = 2;

            /* Basic validation passed: Restore the phase shift calibration data */
            retVal = rlRfPhShiftCalibDataStore(RL_DEVICE_MAP_INTERNAL_BSS, &(gCalibDataStorage.phaseShiftCalibData));
            if (retVal != RL_RET_CODE_OK)
            {
                /* Error: Phase shift Calibration data restore failed */
	         System_printf("MSS demo failed rlRfPhShiftCalibDataStore with Error[%d]\n", retVal);
                return retVal;
            }
#endif
            /* Save data in flash */
            retVal = MmwDemo_calibSave(&gMmwMssMCB.calibCfg.calibDataHdr, &gCalibDataStorage);
            if(retVal < 0)
            {
                return retVal;
            }
        }

        /*Set up HSI clock*/
        if(MmwDemo_mssSetHsiClk() < 0)
        {
            System_printf ("Error: MmwDemo_mssSetHsiClk failed.\n");
            return -1;
        }

        /* ADCBUF is opened at every sensor start (MmwDemo_dataPathStart),
         * not here. */
    }
    return 0;
}

/**
 *  @b Description
 *  @n
 *      MMW demo helper Function to configure sensor. User need to fill gMmwMssMCB.cfg.ctrlCfg and
 *      add profiles/chirp to mmWave before calling this function.
 *      Runs on every "sensorStart" with reconfig: MMWave_config, then the RF
 *      parser derives the ADCBUF/CBUFF parameters (samples, chirps per frame,
 *      ADCBUF channel size) and the BSS-side monitors are configured. ADCBUF
 *      and the HW session themselves are set up at start.
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
int32_t MmwDemo_configSensor(void)
{
    int32_t                     errCode = 0;
    MmwDemo_SubFrameCfg         *subFrameCfg = &gMmwMssMCB.subFrameCfg[0];
    MmwDemo_RFParserOutParams   RFparserOutParams;

    /* Configure the mmWave module: */
    if (MMWave_config (gMmwMssMCB.ctrlHandle, &gMmwMssMCB.cfg.ctrlCfg, &errCode) < 0)
    {
        MMWave_ErrorLevel   errorLevel;
        int16_t             mmWaveErrorCode;
        int16_t             subsysErrorCode;

        /* Error: Report the error */
        MMWave_decodeError (errCode, &errorLevel, &mmWaveErrorCode, &subsysErrorCode);
        System_printf ("Error: mmWave Config failed [Error code: %d Subsystem: %d]\n",
                        mmWaveErrorCode, subsysErrorCode);
        CLI_write ("Error: mmWave Config failed [Error code: %d Subsystem: %d]\n",
                   mmWaveErrorCode, subsysErrorCode);
        return -1;
    }

    /* Get RF frequency scale factor */
    gMmwMssMCB.rfFreqScaleFactor = SOC_getDeviceRFFreqScaleFactor(gMmwMssMCB.socHandle, &errCode);
    if (errCode < 0)
    {
        System_printf ("Error: Unable to get RF scale factor [Error:%d]\n", errCode);
        return -1;
    }

    /* Parse the profile, chirp, frame and adcbuf configs (frame mode: sub-frame 0) */
    memset((void *)&RFparserOutParams, 0, sizeof(RFparserOutParams));
    errCode = MmwDemo_RFParser_parseConfig(&RFparserOutParams, 0,
                                     &gMmwMssMCB.cfg.openCfg, &gMmwMssMCB.cfg.ctrlCfg,
                                     &subFrameCfg->adcBufCfg,
                                     gMmwMssMCB.rfFreqScaleFactor,
                                     false/* no BPM in 18xx demo */);
    if (errCode != 0)
    {
        CLI_write ("Error: chirp/profile/frame/adcbuf configuration rejected by the RF parser [Error:%d]\n", errCode);
        return -1;
    }

    subFrameCfg->numChirpsPerChirpEvent = RFparserOutParams.numChirpsPerChirpEvent;
    subFrameCfg->adcBufChanDataSize     = RFparserOutParams.adcBufChanDataSize;
    subFrameCfg->numAdcSamples          = RFparserOutParams.numAdcSamples;
    subFrameCfg->numChirpsPerSubFrame   = RFparserOutParams.numChirpsPerFrame;
    gMmwMssMCB.validProfileIdx          = RFparserOutParams.validProfileIdx;

    errCode = MmwDemo_configMonitors(subFrameCfg, RFparserOutParams.numChirpsPerChirpEvent,
                                     RFparserOutParams.validProfileIdx);
    if (errCode != 0)
    {
        return errCode;
    }

    /* dataFmt 2 (ADC + SAR metadata): every block of the chirp packet must be a
     * multiple of 8 bytes (the DCA1000 2-lane ordering works on 8-byte groups,
     * docs/lvds_data_format.md section 1), and the session is set up for
     * complex samples. */
    if (subFrameCfg->lvdsStreamCfg.dataFmt == MMW_DEMO_LVDS_STREAM_CFG_DATAFMT_ADC_META)
    {
        uint32_t numRx = 0U;
        uint32_t rxEn  = gMmwMssMCB.cfg.openCfg.chCfg.rxChannelEn;

        while (rxEn != 0U)
        {
            numRx += (rxEn & 1U);
            rxEn >>= 1;
        }
        if (subFrameCfg->adcBufCfg.adcFmt != 0U)
        {
            CLI_write ("Error: lvdsStreamCfg dataFmt 2 needs complex ADC output (adcbufCfg adcOutputFmt 0)\n");
            return -1;
        }
        if ((((uint32_t)subFrameCfg->numAdcSamples * numRx) & 1U) != 0U)
        {
            CLI_write ("Error: lvdsStreamCfg dataFmt 2 needs numAdcSamples x RX channels to be even\n");
            return -1;
        }
    }

    /* Per-chirp metadata: chirp period, lateness budget and chirps per frame */
    {
        rlProfileCfg_t       profileCfg;
        MMWave_ProfileHandle profileHandle = gMmwMssMCB.cfg.ctrlCfg.u.frameCfg.profileHandle[RFparserOutParams.validProfileIdx];

        if ((profileHandle == NULL) || (MMWave_getProfileCfg(profileHandle, &profileCfg, &errCode) < 0))
        {
            CLI_write ("Error: unable to read the profile for the chirp metadata\n");
            return -1;
        }
        errCode = MmwDemo_sarMetaConfig(&profileCfg, subFrameCfg->numChirpsPerSubFrame);
    }
    return errCode;
}

/**
 *  @b Description
 *  @n
 *      mmw demo helper Function to start sensor.
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
int32_t MmwDemo_startSensor(void)
{
    int32_t     errCode;
    MMWave_CalibrationCfg   calibrationCfg;

    /* Forget a frame-end event left over from a previous run */
    Semaphore_reset(gMmwMssMCB.frameEndSemHandle, 0);

    /*****************************************************************************
     * Data path :: ADCBUF + LVDS HW session (MSS only, nothing to wait for)
     *****************************************************************************/
    if (MmwDemo_dataPathStart() < 0)
    {
        CLI_write ("Error: ADCBUF/LVDS HW session setup failed\n");
        return -1;
    }

    /* Per-chirp metadata: new run (counters, statistics, record slots, CQ2) */
    MmwDemo_sarMetaRunStart(gMmwMssMCB.adcBufHandle, gMmwMssMCB.anaMonCfg.rxSatMonEn,
                            gMmwMssMCB.cqSatMonCfg[gMmwMssMCB.validProfileIdx].numSlices);

    /*****************************************************************************
     * RF :: now start the RF and the real time ticking
     *****************************************************************************/
    /* Initialize the calibration configuration: */
    memset ((void *)&calibrationCfg, 0, sizeof(MMWave_CalibrationCfg));
    /* Populate the calibration configuration: one-time runtime calibration at
     * start, periodic runtime calibration OFF (TI demo: every 10 frames), so no
     * calibration update is applied between frames during a capture. */
    calibrationCfg.dfeDataOutputMode = gMmwMssMCB.cfg.ctrlCfg.dfeDataOutputMode;
    calibrationCfg.u.chirpCalibrationCfg.enableCalibration    = true;
    calibrationCfg.u.chirpCalibrationCfg.enablePeriodicity    = false;
    calibrationCfg.u.chirpCalibrationCfg.periodicTimeInFrames = 0U;

    DebugP_log0("App: MMWave_start Issued\n");

    System_printf("Starting Sensor (issuing MMWave_start)\n");

    /* Start the mmWave module: The configuration has been applied successfully. */
    if (MMWave_start(gMmwMssMCB.ctrlHandle, &calibrationCfg, &errCode) < 0)
    {
        MMWave_ErrorLevel   errorLevel;
        int16_t             mmWaveErrorCode;
        int16_t             subsysErrorCode;

        /* Error/Warning: Unable to start the mmWave module */
        MMWave_decodeError (errCode, &errorLevel, &mmWaveErrorCode, &subsysErrorCode);
        System_printf ("Error: mmWave Start failed [mmWave Error: %d Subsys: %d]\n", mmWaveErrorCode, subsysErrorCode);
        CLI_write ("Error: mmWave Start failed [mmWave Error: %d Subsys: %d]\n", mmWaveErrorCode, subsysErrorCode);

        /* No frame was started: undo the data path so the next start begins clean */
        MmwDemo_dataPathStop();
        return -1;
    }

    /*****************************************************************************
     * The sensor has been started successfully. Switch on the LED
     *****************************************************************************/
    GPIO_write (gMmwMssMCB.cfg.platformCfg.SensorStatusGPIO, 1U);

    gMmwMssMCB.sensorStartCount++;
    return 0;
}

/**
 *  @b Description
 *  @n
 *      Epilog processing after sensor has stopped
 *
 *  @retval None
 */
static void MmwDemo_sensorStopEpilog(void)
{
    Task_Stat stat;
    Hwi_StackInfo stackInfo;
    Bool stackOverflow;

    System_printf("Data Path Stopped (HW session deleted, ADCBUF closed), LVDS HW frames done: %d\n",
                  gMmwMssMCB.lvdsStream.hwFrameDoneCount);

    System_printf("============================================\n");
    System_printf("MSS Task Stack Usage (Note: Task Stack Usage) ==========\n");
    System_printf("%20s %12s %12s %12s\n", "Task Name", "Size", "Used", "Free");


    Task_stat(gMmwMssMCB.taskHandles.initTask, &stat);
    System_printf("%20s %12d %12d %12d\n", "Init",
                  stat.stackSize, stat.used, stat.stackSize - stat.used);

    Task_stat(gMmwMssMCB.taskHandles.mmwaveCtrl, &stat);
    System_printf("%20s %12d %12d %12d\n", "Mmwave Control",
                  stat.stackSize, stat.used, stat.stackSize - stat.used);

    System_printf("HWI Stack (same as System Stack) Usage ============\n");
    stackOverflow = Hwi_getStackInfo(&stackInfo, TRUE);
    if (stackOverflow == TRUE)
    {
        System_printf("HWI Stack overflowed\n");
        MmwDemo_debugAssert(0);
    }
    else
    {
        System_printf("%20s %12s %12s %12s\n", " ", "Size", "Used", "Free");
        System_printf("%20s %12d %12d %12d\n", " ",
                      stackInfo.hwiStackSize, stackInfo.hwiStackPeak,
                      stackInfo.hwiStackSize - stackInfo.hwiStackPeak);
    }
}


/**
 *  @b Description
 *  @n
 *      Stops the RF, waits for the BSS frame-end event (the current frame
 *      completes), then tears down the HW session and ADCBUF. Prints epilog
 *      at the end. Afterwards the sensor can be reconfigured and restarted
 *      without a power cycle.
 *
 *  @retval  None
 */
void MmwDemo_stopSensor(void)
{
    /* Stop sensor RF; the BSS ends the current frame and reports FRAME_END */
    MmwDemo_mmWaveCtrlStop();

    /* Wait for the frame-end event (already posted if a finite frame count had
     * ended). Bounded: the stop path never hangs on a missing event. */
    if (Semaphore_pend(gMmwMssMCB.frameEndSemHandle, MMWDEMO_FRAME_END_TIMEOUT_TICKS) == FALSE)
    {
        System_printf("Warning: no BSS frame-end event within %d ms of sensorStop\n",
                      MMWDEMO_FRAME_END_TIMEOUT_TICKS);
        CLI_write("Warning: no BSS frame-end event after sensorStop; tearing down anyway\n");
    }

    /* Delete the HW session (returns its EDMA channels) and close ADCBUF */
    MmwDemo_dataPathStop();

    /* Print epilog */
    MmwDemo_sensorStopEpilog();

    /* The sensor has been stopped successfully. Switch off the LED */
    GPIO_write (gMmwMssMCB.cfg.platformCfg.SensorStatusGPIO, 0U);

    gMmwMssMCB.sensorStopCount++;

    /* print for user */
    System_printf("Sensor has been stopped: startCount: %d stopCount %d\n",
                  gMmwMssMCB.sensorStartCount,gMmwMssMCB.sensorStopCount);
}

/**************************************************************************
 ******************** Millimeter Wave Demo init Functions ************************
 **************************************************************************/

/**
 *  @b Description
 *  @n
 *      Platform specific hardware initialization.
 *
 *  @param[in]  config     Platform initialization configuraiton
 *
 *  @retval
 *      Not Applicable.
 */
static void MmwDemo_platformInit(MmwDemo_platformCfg *config)
{
    /* Setup the PINMUX to bring out the UART-1 */
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINN5_PADBE, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);    
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINN5_PADBE, SOC_XWR18XX_PINN5_PADBE_MSS_UARTA_TX);
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINN4_PADBD, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);    
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINN4_PADBD, SOC_XWR18XX_PINN4_PADBD_MSS_UARTA_RX);

    /* Setup the PINMUX to bring out the UART-3 */
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINF14_PADAJ, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINF14_PADAJ, SOC_XWR18XX_PINF14_PADAJ_MSS_UARTB_TX);

    /**********************************************************************
     * Setup the PINMUX:
     * - GPIO Output: Configure pin K13 as GPIO_2 output
     **********************************************************************/
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINK13_PADAZ, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINK13_PADAZ, SOC_XWR18XX_PINK13_PADAZ_GPIO_2);

    /**********************************************************************
     * Setup the PINMUX:
     * - for QSPI Flash
     **********************************************************************/
    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINR12_PADAP, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINR12_PADAP, SOC_XWR18XX_PINR12_PADAP_QSPI_CLK);

    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINP11_PADAQ, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINP11_PADAQ, SOC_XWR18XX_PINP11_PADAQ_QSPI_CSN);

    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINR13_PADAL, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINR13_PADAL, SOC_XWR18XX_PINR13_PADAL_QSPI_D0);

    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINN12_PADAM, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINN12_PADAM, SOC_XWR18XX_PINN12_PADAM_QSPI_D1);

    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINR14_PADAN, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINR14_PADAN, SOC_XWR18XX_PINR14_PADAN_QSPI_D2);

    Pinmux_Set_OverrideCtrl(SOC_XWR18XX_PINP12_PADAO, PINMUX_OUTEN_RETAIN_HW_CTRL, PINMUX_INPEN_RETAIN_HW_CTRL);
    Pinmux_Set_FuncSel(SOC_XWR18XX_PINP12_PADAO, SOC_XWR18XX_PINP12_PADAO_QSPI_D3);

    /**********************************************************************
     * Setup the GPIO:
     * - GPIO Output: Configure pin K13 as GPIO_2 output
     **********************************************************************/
    config->SensorStatusGPIO    = SOC_XWR18XX_GPIO_2;

    /* Initialize the DEMO configuration: */
    config->sysClockFrequency   = MSS_SYS_VCLK;
    config->loggingBaudRate     = 921600;
    config->commandBaudRate     = 115200;

    /**********************************************************************
     * Setup the DS3 LED on the EVM connected to GPIO_2
     **********************************************************************/
    GPIO_setConfig (config->SensorStatusGPIO, GPIO_CFG_OUTPUT);
}

/**
 *  @b Description
 *  @n
 *      Init EDMA.
 */
static void MmwDemo_edmaInit(void)
{
    int32_t errorCode;

    errorCode = EDMA_init(MMW_LVDS_STREAM_EDMA_INSTANCE);
    if (errorCode != EDMA_NO_ERROR)
    {
        System_printf ("Debug: EDMA instance %d initialization returned error %d\n", errorCode);
        MmwDemo_debugAssert (0);
        return;
    }

    memset(&gMmwMssMCB.EDMA_errorInfo, 0, sizeof(gMmwMssMCB.EDMA_errorInfo));
    memset(&gMmwMssMCB.EDMA_transferControllerErrorInfo, 0, sizeof(gMmwMssMCB.EDMA_transferControllerErrorInfo));
}
/**
 *  @b Description
 *  @n
 *      Call back function for EDMA CC (Channel controller) error as per EDMA API.
 *      Declare fatal error if happens, the output errorInfo can be examined if code
 *      gets trapped here.
 */
static void MmwDemo_EDMA_errorCallbackFxn(EDMA_Handle handle, EDMA_errorInfo_t *errorInfo)
{
    gMmwMssMCB.EDMA_errorInfo = *errorInfo;
    MmwDemo_debugAssert(0);
}
/**
 *  @b Description
 *  @n
 *      Call back function for EDMA transfer controller error as per EDMA API.
 *      Declare fatal error if happens, the output errorInfo can be examined if code
 *      gets trapped here.
 */
static void MmwDemo_EDMA_transferControllerErrorCallbackFxn(EDMA_Handle handle,
                EDMA_transferControllerErrorInfo_t *errorInfo)
{
    gMmwMssMCB.EDMA_transferControllerErrorInfo = *errorInfo;
    MmwDemo_debugAssert(0);
}
/**
 *  @b Description
 *  @n
 *      Open EDMA.
 */
static void MmwDemo_edmaOpen(void)
{
    int32_t             errCode;
    EDMA_instanceInfo_t edmaInstanceInfo;
    EDMA_errorConfig_t  errorConfig;
    uint8_t             tc;

    gMmwMssMCB.edmaHandle = EDMA_open(MMW_LVDS_STREAM_EDMA_INSTANCE, &errCode, &edmaInstanceInfo);
    gMmwMssMCB.numEdmaEventQueues = edmaInstanceInfo.numEventQueues;

    if (gMmwMssMCB.edmaHandle == NULL)
    {
        System_printf("Error: Unable to open the EDMA Instance err:%d\n",errCode);
        MmwDemo_debugAssert (0);
        return;
    }

    errorConfig.isConfigAllEventQueues = true;
    errorConfig.isConfigAllTransferControllers = true;
    errorConfig.isEventQueueThresholdingEnabled = true;
    errorConfig.eventQueueThreshold = EDMA_EVENT_QUEUE_THRESHOLD_MAX;
    errorConfig.isEnableAllTransferControllerErrors = true;

    gMmwMssMCB.isPollEdmaError = false;
    if (edmaInstanceInfo.isErrorInterruptConnected == true)
    {
        errorConfig.callbackFxn = MmwDemo_EDMA_errorCallbackFxn;
    }
    else
    {
        errorConfig.callbackFxn = NULL;
        gMmwMssMCB.isPollEdmaError = true;
    }

    errorConfig.transferControllerCallbackFxn = MmwDemo_EDMA_transferControllerErrorCallbackFxn;
    gMmwMssMCB.isPollEdmaTransferControllerError = false;

    for(tc = 0; tc < edmaInstanceInfo.numEventQueues; tc++)
    {
        if (edmaInstanceInfo.isTransferControllerErrorInterruptConnected[tc] == false)
        {
            errorConfig.transferControllerCallbackFxn = NULL;
            gMmwMssMCB.isPollEdmaTransferControllerError = true;
            break;
        }
    }

    if ((errCode = EDMA_configErrorMonitoring(gMmwMssMCB.edmaHandle, &errorConfig)) != EDMA_NO_ERROR)
    {
        //System_printf("Error: EDMA_configErrorMonitoring() failed with errorCode = %d\n", errCode);
        MmwDemo_debugAssert (0);
        return;
    }
}
/**
 *  @b Description
 *  @n
 *      Checks for EDMA errors, used on devices where error interrupts are not connected
 *      to the CPU. Current use case is for LVDS, note it is not very useful to
 *      check for edma errors within the CBUFF session completion interrupts
 *      because they will not happen if edma had errors. So this API should be
 *      called at opportune places in the application code, typically at some time
 *      later than the triggering of the session when it is roughly expected that
 *      the session would have completed by that time.
 */
static void MmwDemo_checkEdmaErrors(void)
{
    int32_t     errCode;
    bool        isAnyError;

    if (gMmwMssMCB.isPollEdmaError == true)
    {
        errCode = EDMA_getErrorStatus(gMmwMssMCB.edmaHandle, &isAnyError, &gMmwMssMCB.EDMA_errorInfo);
        if (errCode != EDMA_NO_ERROR)
        {
            System_printf("Error: EDMA_getErrorStatus() failed with error code = %d\n", errCode);
            MmwDemo_debugAssert(0);
        }

        if (isAnyError == true)
        {
            System_printf("EDMA channel controller has errors, see gMmwMssMCB.EDMA_errorInfo\n");
            MmwDemo_debugAssert(0);
        }
    }

    if (gMmwMssMCB.isPollEdmaTransferControllerError == true)
    {
        uint8_t tc;

        for(tc = 0; tc < gMmwMssMCB.numEdmaEventQueues; tc++)
        {
            errCode = EDMA_getTransferControllerErrorStatus(gMmwMssMCB.edmaHandle, tc,
                           &isAnyError, &gMmwMssMCB.EDMA_transferControllerErrorInfo);
            if (errCode != EDMA_NO_ERROR)
            {
                System_printf("Error: EDMA_getTransferControllerErrorStatus() failed with error code = %d\n", errCode);
                MmwDemo_debugAssert(0);
            }

            if (isAnyError == true)
            {
                System_printf("EDMA Transfer Controller instance %d has errors, see gMmwMssMCB.EDMA_transferControllerErrorInfo\n", tc);
                MmwDemo_debugAssert(0);
            }
        }
    }
}

/**
 *  @b Description
 *  @n
 *      Calibration save/restore initialization
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_calibInit(void)
{
    int32_t          retVal = 0;
    rlVersion_t    verArgs;

    /* Initialize verArgs */
    memset((void *)&verArgs, 0, sizeof(rlVersion_t));

    /* Get the version string: */
    retVal = rlDeviceGetVersion(RL_DEVICE_MAP_INTERNAL_BSS, &verArgs);
    if (retVal < 0)
    {
        System_printf ("Error: Unable to get the device version from mmWave link [Error %d]\n", retVal);
        return -1;
    }

    /* Calibration save/restore init */
    gMmwMssMCB.calibCfg.sizeOfCalibDataStorage = sizeof(MmwDemo_calibData);
    gMmwMssMCB.calibCfg.calibDataHdr.magic = MMWDEMO_CALIB_STORE_MAGIC;
    memcpy((void *)& gMmwMssMCB.calibCfg.calibDataHdr.linkVer, (void *)&verArgs.mmWaveLink, sizeof(rlSwVersionParam_t));
    memcpy((void *)& gMmwMssMCB.calibCfg.calibDataHdr.radarSSVer, (void *)&verArgs.rf, sizeof(rlFwVersionParam_t));

    /* Check if Calibration data is over the Reserved storage */
    if(gMmwMssMCB.calibCfg.sizeOfCalibDataStorage   <= MMWDEMO_CALIB_FLASH_SIZE)
    {
	    gMmwMssMCB.calibCfg.calibDataHdr.hdrLen = sizeof(MmwDemo_calibDataHeader);
	    gMmwMssMCB.calibCfg.calibDataHdr.dataLen= sizeof(MmwDemo_calibData) - sizeof(MmwDemo_calibDataHeader);

           /* Resets calibration data */
           memset((void *)&gCalibDataStorage, 0, sizeof(MmwDemo_calibData));

	    retVal = mmwDemo_flashInit();
    }
    else
    {
        System_printf ("Error: Calibration data size is bigger than reserved size\n");
        retVal = -1;
    }

    return retVal;

}

/**
 *  @b Description
 *  @n
 *      This function retrieves the calibration data from front end and saves it in flash.
 *
 *  @param[in]  ptrCalibDataHdr     	Pointer to Calibration data header
 *  @param[in]  ptrCalibrationData      Pointer to Calibration data
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_calibSave(MmwDemo_calibDataHeader *ptrCalibDataHdr, MmwDemo_calibData  *ptrCalibrationData)
{
    uint32_t				flashOffset;
    int32_t 				retVal = 0;

    /* Calculate the read size in bytes */
    flashOffset = gMmwMssMCB.calibCfg.flashOffset;

    /* Copy header  */
    memcpy((void *)&(ptrCalibrationData->calibDataHdr), ptrCalibDataHdr, sizeof(MmwDemo_calibDataHeader));

    /* Flash calibration data */
    retVal = mmwDemo_flashWrite(flashOffset, (uint32_t *)ptrCalibrationData, sizeof(MmwDemo_calibData));
    if(retVal < 0)
    {
        /* Flash Header failed */
        System_printf ("Error: MmwDemo failed flashing calibration data with error[%d].\n", retVal);
    }

    return(retVal);
}

/**
 *  @b Description
 *  @n
 *      This function reads calibration data from flash and send it to front end through MMWave_open()
 *
 *  @param[in]  ptrCalibData     	Pointer to Calibration data
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t MmwDemo_calibRestore(MmwDemo_calibData  *ptrCalibData)
{
    MmwDemo_calibDataHeader    *pDataHdr;
    int32_t 				retVal = 0;
    uint32_t				flashOffset;

    pDataHdr = &(ptrCalibData->calibDataHdr);

    /* Calculate the read size in bytes */
    flashOffset = gMmwMssMCB.calibCfg.flashOffset;

    /* Read calibration data header */
    if(mmwDemo_flashRead(flashOffset, (uint32_t *)pDataHdr, sizeof(MmwDemo_calibData) )< 0)
    {
        /* Error: only one can be enable at at time */
        System_printf ("Error: MmwDemo failed when reading calibration data from flash.\n");
        return -1;
    }

    /* Validate data header */
    if( (pDataHdr->magic != MMWDEMO_CALIB_STORE_MAGIC) ||
         (pDataHdr->hdrLen != gMmwMssMCB.calibCfg.calibDataHdr.hdrLen) ||
         (pDataHdr->dataLen != gMmwMssMCB.calibCfg.calibDataHdr.dataLen) )
    {
        /* Header validation failed */
        System_printf ("Error: MmwDemo calibration data header validation failed.\n");
        retVal = -1;
    }
    /* Matching mmwLink version:
         In this demo, we would like to save/restore with the matching mmwLink and RF FW version.
         However, this logic can be changed to use data saved from previous mmwLink and FW releases,
         as long as the data format of the calibration data matches.
     */
    else if(memcmp((void *)&pDataHdr->linkVer, (void *)&gMmwMssMCB.calibCfg.calibDataHdr.linkVer, sizeof(rlSwVersionParam_t)) != 0)
    {
        System_printf ("Error: MmwDemo failed mmwLink version validation when restoring calibration data.\n");
        retVal = -1;
    }
    else if(memcmp((void *)&pDataHdr->radarSSVer, (void *)&gMmwMssMCB.calibCfg.calibDataHdr.radarSSVer, sizeof(rlFwVersionParam_t)) != 0)
    {
        System_printf ("Error: MmwDemo failed RF FW version validation when restoring calibration data.\n");
        retVal = -1;
    }
    return(retVal);
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
static void MmwDemo_initTask(UArg arg0, UArg arg1)
{
    int32_t             errCode;
    MMWave_InitCfg      initCfg;
    UART_Params         uartParams;
    Task_Params         taskParams;
    Semaphore_Params    semParams;
    int32_t             i;

    /* Debug Message: */
    System_printf("Debug: Launched the Initialization Task\n");

    /*****************************************************************************
     * Initialize the mmWave SDK components:
     *****************************************************************************/
    /* Initialize the UART */
    UART_init();

    /* Initialize the GPIO */
    GPIO_init();

    /* Initialize the Mailbox */
    Mailbox_init(MAILBOX_TYPE_MSS);

    MmwDemo_edmaInit();

    MmwDemo_edmaOpen();

    /* Initialize LVDS streaming components */
    if ((errCode = MmwDemo_LVDSStreamInit()) < 0 )
    {
        System_printf ("Error: LVDS stream init failed with Error[%d]\n",errCode);
        return;
    }

    /* Per-chirp metadata: chirp/frame interrupt handlers, timestamp clock */
    if (MmwDemo_sarMetaInit(gMmwMssMCB.socHandle) < 0)
    {
        System_printf ("Error: SAR chirp metadata init failed\n");
        return;
    }

    /* initialize cq configs to invalid profile index to be able to detect
     * unconfigured state of these when monitors for them are enabled.
     */
    for(i = 0; i < RL_MAX_PROFILES_CNT; i++)
    {
        gMmwMssMCB.cqSatMonCfg[i].profileIndx    = (RL_MAX_PROFILES_CNT + 1);
        gMmwMssMCB.cqSigImgMonCfg[i].profileIndx = (RL_MAX_PROFILES_CNT + 1);
    }

    /* Platform specific configuration */
    MmwDemo_platformInit(&gMmwMssMCB.cfg.platformCfg);

    /*****************************************************************************
     * Open the mmWave SDK components:
     *****************************************************************************/
    /* Setup the default UART Parameters */
    UART_Params_init(&uartParams);
    uartParams.clockFrequency = gMmwMssMCB.cfg.platformCfg.sysClockFrequency;
    uartParams.baudRate       = gMmwMssMCB.cfg.platformCfg.commandBaudRate;
    uartParams.isPinMuxDone   = 1;

    /* Open the UART Instance (CLI). The data UART of the TI demo carried only
     * the TLV output and is not opened. */
    gMmwMssMCB.commandUartHandle = UART_open(0, &uartParams);
    if (gMmwMssMCB.commandUartHandle == NULL)
    {
        MmwDemo_debugAssert (0);
        return;
    }

    /* Binary semaphore posted by the BSS frame-end async event; the stop path
     * pends on it (bounded) before deleting the HW session. Created once. */
    Semaphore_Params_init(&semParams);
    semParams.mode              = Semaphore_Mode_BINARY;
    gMmwMssMCB.frameEndSemHandle = Semaphore_create(0, &semParams, NULL);

    /*****************************************************************************
     * mmWave: Initialization of the high level module
     *****************************************************************************/

    /* Initialize the mmWave control init configuration */
    memset ((void*)&initCfg, 0 , sizeof(MMWave_InitCfg));

    /* Populate the init configuration: the MSS does the full configuration in
     * isolation mode (no DSS peer). */
    initCfg.domain                  = MMWave_Domain_MSS;
    initCfg.socHandle               = gMmwMssMCB.socHandle;
    initCfg.eventFxn                = MmwDemo_eventCallbackFxn;
    initCfg.linkCRCCfg.useCRCDriver = 1U;
    initCfg.linkCRCCfg.crcChannel   = CRC_Channel_CH1;
    initCfg.cfgMode                 = MMWave_ConfigurationMode_FULL;
    initCfg.executionMode           = MMWave_ExecutionMode_ISOLATION;

    /* Initialize and setup the mmWave Control module */
    gMmwMssMCB.ctrlHandle = MMWave_init (&initCfg, &errCode);
    if (gMmwMssMCB.ctrlHandle == NULL)
    {
        /* Error: Unable to initialize the mmWave control module */
        System_printf ("Error: mmWave Control Initialization failed [Error code %d]\n", errCode);
        MmwDemo_debugAssert (0);
        return;
    }
    System_printf ("Debug: mmWave Control Initialization was successful\n");

    /* Synchronization: This will synchronize the execution of the control module
     * between the domains. This is a prerequiste and always needs to be invoked. */
    if (MMWave_sync (gMmwMssMCB.ctrlHandle, &errCode) < 0)
    {
        /* Error: Unable to synchronize the mmWave control module */
        System_printf ("Error: mmWave Control Synchronization failed [Error code %d]\n", errCode);
        MmwDemo_debugAssert (0);
        return;
    }
    System_printf ("Debug: mmWave Control Synchronization was successful\n");

#ifdef SYS_COMMON_RTRIM_MODIFY_EN
    /* Only when RTRIM MODIFICATION enabled for customer certification purpose */
    /* Read RTRIM, changing the default RTRIM value 12 to 6 */
    System_printf ("Trim Value: %d\n", rtrim_get());
    rtrim_set(6U);
    System_printf ("Trim Value: %d\n", rtrim_get());
#endif

    /*****************************************************************************
     * Launch the mmWave control execution task
     * - This should have a higher priroity than any other task which uses the
     *   mmWave control API
     *****************************************************************************/
    Task_Params_init(&taskParams);
    taskParams.priority  = MMWDEMO_MMWAVE_CTRL_TASK_PRIORITY;
    taskParams.stackSize = 3*1024;
    gMmwMssMCB.taskHandles.mmwaveCtrl = Task_create(MmwDemo_mmWaveCtrlTask, &taskParams, NULL);

    /* Calibration save/restore initialization */
    if(MmwDemo_calibInit()<0)
    {
        System_printf("Error: Calibration data initialization failed \n");
        MmwDemo_debugAssert (0);
    }

    /*****************************************************************************
     * Initialize the Profiler
     *****************************************************************************/
    Cycleprofiler_init();

    /*****************************************************************************
     * Initialize the CLI Module:
     *****************************************************************************/
    MmwDemo_CLIInit(MMWDEMO_CLI_TASK_PRIORITY);

    return;
}

/**
 *  @b Description
 *  @n
 *     Function to sleep the R4F using WFI (Wait For Interrupt) instruction.
 *     When R4F has no work left to do,
 *     the BIOS will be in Idle thread and will call this function. The R4F will
 *     wake-up on any interrupt (e.g chirp interrupt).
 *
 *  @retval
 *      Not Applicable.
 */
void MmwDemo_sleep(void)
{
    /* issue WFI (Wait For Interrupt) instruction */
    asm(" WFI ");
}

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
    Task_Params     taskParams;
    int32_t         errCode;
    SOC_Handle      socHandle;
    SOC_Cfg         socCfg;

    /* Initialize the ESM: Dont clear errors as TI RTOS does it */
    ESM_init(0U);

    /* Initialize the SOC confiugration: */
    memset ((void *)&socCfg, 0, sizeof(SOC_Cfg));

    /* Populate the SOC configuration: */
    socCfg.clockCfg = SOC_SysClock_INIT;
    socCfg.mpuCfg = SOC_MPUCfg_CONFIG;
    /* MSS-only metaimage (DSS = NULL): leave the DSP halted, as TI's MSS-only
     * xwr64xx demo does. Unhalting it would run whatever is in DSS L2. */
    socCfg.dssCfg = SOC_DSSCfg_HALT;

    /* Initialize the SOC Module: This is done as soon as the application is started
     * to ensure that the MPU is correctly configured. */
    socHandle = SOC_init (&socCfg, &errCode);
    if (socHandle == NULL)
    {
        System_printf ("Error: SOC Module Initialization failed [Error code %d]\n", errCode);
        MmwDemo_debugAssert (0);
        return -1;
    }    
    
    /* Wait for BSS powerup */
    if (SOC_waitBSSPowerUp(socHandle, &errCode) < 0)
    {
        /* Debug Message: */
        System_printf ("Debug: SOC_waitBSSPowerUp failed with Error [%d]\n", errCode);
        return 0;
    }

    /* Check if the SOC is a secure device */
    if (SOC_isSecureDevice(socHandle, &errCode))
    {
        /* Disable firewall for JTAG and LOGGER (UART) which is needed by all unit tests */
        SOC_controlSecureFirewall(socHandle, 
                                  (uint32_t)(SOC_SECURE_FIREWALL_JTAG | SOC_SECURE_FIREWALL_LOGGER),
                                  SOC_SECURE_FIREWALL_DISABLE,
                                  &errCode);
    }

    /* Initialize and populate the demo MCB */
    memset ((void*)&gMmwMssMCB, 0, sizeof(MmwDemo_MSS_MCB));

    gMmwMssMCB.socHandle = socHandle;

    /* Debug Message: */
    System_printf ("**********************************************\n");
    System_printf ("Debug: Launching the SAR LVDS firmware on MSS (MSS-only)\n");
    System_printf ("**********************************************\n");

    /* Initialize the Task Parameters. */
    Task_Params_init(&taskParams);
    gMmwMssMCB.taskHandles.initTask = Task_create(MmwDemo_initTask, &taskParams, NULL);

    /* Start BIOS */
    BIOS_start();
    return 0;
}

