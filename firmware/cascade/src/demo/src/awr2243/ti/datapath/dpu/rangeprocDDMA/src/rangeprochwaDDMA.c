/*
 *  NOTE:
 *      (C) Copyright 2018 - 2021 Texas Instruments, Inc.
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
/**
 *   @file  rangeprochwaDDMA.c
 *
 *   @brief
 *      Implements Range FFT data processing Unit using HWA.
 */

/**************************************************************************
 *************************** Include Files ********************************
 **************************************************************************/

/* Standard Include Files. */
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>

/* MCU+SDK Include files */
#include <drivers/hw_include/hw_types.h>
#include <kernel/dpl/SemaphoreP.h>
#include <kernel/dpl/CacheP.h>
#include <kernel/dpl/HeapP.h>
#include <drivers/edma.h>
#ifdef SUBSYS_MSS
#include <kernel/dpl/CacheP.h>
#endif

/* HWA_SOC Include files */
#include <drivers/soc.h>

/* Data Path Include files */
#include <ti/datapath/dpu/rangeprocDDMA/rangeprochwaDDMA.h>

/* MATH utils library Include files */
#include <ti/utils/mathutils/mathutils.h>

/* Internal include Files */
#include <ti/datapath/dpu/rangeprocDDMA/include/rangeprochwaDDMA_internal.h>

#include <ti/control/mmwavelink/mmwavelink.h>

rangeProcHWAObj RangeObj[RL_MAX_SUBFRAMES] __attribute__((aligned(HeapP_BYTE_ALIGNMENT)));

/* Flag to check input parameters */
#define DEBUG_CHECK_PARAMS   1

#define DCEST_PING_HWA_PARAMSET_RELATIVE_IDX        1
#define DCSUB_PING_HWA_PARAMSET_RELATIVE_IDX        2
#define FFT_PING_HWA_PARAMSET_RELATIVE_IDX          3

#ifdef CASCADE_EVM
#define DCEST_PING_HWA_PARAMSET_RELATIVE_IDX1       4
#define DCSUB_PING_HWA_PARAMSET_RELATIVE_IDX1       5
#define FFT_PING_HWA_PARAMSET_RELATIVE_IDX1         6

#define COMPRESS_PING_HWA_PARAMSET_RELATIVE_IDX     7

#define DPU_RANGEHWADDMA_MEM_BANK_DCEST_PONG_1_IN   5
#define DPU_RANGEHWADDMA_MEM_BANK_DCEST_PING_1_IN   4

#define DPU_RANGEHWADDMA_ADDR_DCEST_PING_1_IN   HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCEST_PING_1_IN])
#define DPU_RANGEHWADDMA_ADDR_DCEST_PONG_1_IN   HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCEST_PONG_1_IN])
#else
#define COMPRESS_PING_HWA_PARAMSET_RELATIVE_IDX     4
#endif

#define DPU_RANGEHWADDMA_MEM_BANK_DCEST_PING_IN     0
#define DPU_RANGEHWADDMA_MEM_BANK_DCSUB_PING_OUT    2
#define DPU_RANGEHWADDMA_MEM_BANK_FFT_PING_OUT      0
#define DPU_RANGEHWADDMA_MEM_BANK_COMP_PING_OUT     2

#define DPU_RANGEHWADDMA_MEM_BANK_DCEST_PONG_IN     1
#define DPU_RANGEHWADDMA_MEM_BANK_DCSUB_PONG_OUT    3
#define DPU_RANGEHWADDMA_MEM_BANK_FFT_PONG_OUT      1
#define DPU_RANGEHWADDMA_MEM_BANK_COMP_PONG_OUT     3

#define DPU_RANGEHWADDMA_ADDR_DCEST_PING_IN     HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCEST_PING_IN])
/* DPU_RANGEHWADDMA_ADDR_DCEST_PING_OUT is not required */
#define DPU_RANGEHWADDMA_ADDR_DCSUB_PING_IN     DPU_RANGEHWADDMA_ADDR_DCEST_PING_IN
#define DPU_RANGEHWADDMA_ADDR_DCSUB_PING_OUT    HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCSUB_PING_OUT])
#define DPU_RANGEHWADDMA_ADDR_FFT_PING_IN       DPU_RANGEHWADDMA_ADDR_DCSUB_PING_OUT
#define DPU_RANGEHWADDMA_ADDR_FFT_PING_OUT      HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_FFT_PING_OUT])
#define DPU_RANGEHWADDMA_ADDR_COMP_PING_IN      DPU_RANGEHWADDMA_ADDR_FFT_PING_OUT
#define DPU_RANGEHWADDMA_ADDR_COMP_PING_OUT     HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_COMP_PING_OUT])

#define DPU_RANGEHWADDMA_ADDR_DCEST_PONG_IN     HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCEST_PONG_IN]) 
/* DPU_RANGEHWADDMA_ADDR_DCEST_PONG_OUT is not required */
#define DPU_RANGEHWADDMA_ADDR_DCSUB_PONG_IN     DPU_RANGEHWADDMA_ADDR_DCEST_PONG_IN
#define DPU_RANGEHWADDMA_ADDR_DCSUB_PONG_OUT    HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCSUB_PONG_OUT])
#define DPU_RANGEHWADDMA_ADDR_FFT_PONG_IN       DPU_RANGEHWADDMA_ADDR_DCSUB_PONG_OUT
#define DPU_RANGEHWADDMA_ADDR_FFT_PONG_OUT      HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_FFT_PONG_OUT])
#define DPU_RANGEHWADDMA_ADDR_COMP_PONG_IN      DPU_RANGEHWADDMA_ADDR_FFT_PONG_OUT
#define DPU_RANGEHWADDMA_ADDR_COMP_PONG_OUT     HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_COMP_PONG_OUT])

/**************************************************************************
 ************************ Internal Functions Prototype       **********************
 **************************************************************************/
static void rangeProcHWADoneIsrCallback(uint32_t threadIdx, void * arg);
void rangeProcHWA_EDMA_transferCompletionCallbackFxn(Edma_IntrHandle intrHandle,
   void *args);

static int32_t rangeProcHWA_ConfigEDMATranspose
(
    rangeProc_dpParams      *dpParams,
    EDMA_Handle             handle,
    DPEDMA_ChanCfg          *chanCfg,
    DPEDMA_ChainingCfg      *chainingCfg,
    uint32_t                srcAddress,
    uint32_t                destAddress,
    bool                    isTransferCompletionEnabled,
    Edma_EventCallback      transferCompletionCallbackFxn,
    void*                   transferCompletionCallbackFxnArg,
    Edma_IntrObject         *intrObj
);

static int32_t rangeProcHWA_ConfigHWA
(
    rangeProcHWAObj     *rangeProcObj,
    uint8_t     destChanPing,
    uint8_t     destChanPong
);

static int32_t rangeProcHWA_TriggerHWA
(
    rangeProcHWAObj     *rangeProcObj
);
static int32_t rangeProcHWA_ConfigEDMA_DataOut_interleave
(
    rangeProcHWAObj     *rangeProcObj,
    rangeProc_dpParams  *DPParams,
    DPU_RangeProcHWA_HW_Resources *pHwConfig,
    uint32_t            hwaInPingOffset,
    uint32_t            hwaInPongOffset,
    uint32_t            hwaOutPingOffset,
    uint32_t            hwaOutPongOffset
);
static int32_t rangeProcHWA_ConfigEDMA_DataOut_nonInterleave
(
    rangeProcHWAObj         *rangeProcObj,
    rangeProc_dpParams      *DPParams,
    DPU_RangeProcHWA_HW_Resources *pHwConfig
);
static int32_t rangeProcHWA_ConfigEDMA_DataIn
(
    rangeProcHWAObj         *rangeProcObj,
    rangeProc_dpParams      *DPParams,
    DPU_RangeProcHWA_HW_Resources *pHwConfig
);
static int32_t rangeProcHWA_ConfigInterleaveMode
(
    rangeProcHWAObj         *rangeProcObj,
    rangeProc_dpParams      *DPParams,
    DPU_RangeProcHWA_HW_Resources *pHwConfig
);
static int32_t rangeProcHWA_ConfigNonInterleaveMode
(
    rangeProcHWAObj          *rangeProcObj,
    rangeProc_dpParams       *DPParams,
    DPU_RangeProcHWA_HW_Resources *pHwConfig
);
/**************************************************************************
 ************************RangeProcHWA Internal Functions **********************
 **************************************************************************/

/**
 *  @b Description
 *  @n
 *      HWA processing completion call back function as per HWA API.
 *      Depending on the programmed transfer completion codes,
 *      posts HWA done semaphore.
 *
 *  @param[in]  threadIdx           Thread index
 *  @param[in]  arg                 Argument to the callback function
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval     N/A
 */
volatile uint32_t hwadoneisr = 0;
static void rangeProcHWADoneIsrCallback(uint32_t threadIdx, void * arg)
{
    hwadoneisr++;
    if (arg != NULL) 
    {
        SemaphoreP_post((SemaphoreP_Object*)arg);
    }
}
/**
 *  @b Description
 *  @n
 *      EDMA processing completion call back function as per EDMA API.
 *
 *  @param[in]  arg                     Argument to the callback function
 *  @param[in]  transferCompletionCode  EDMA transfer complete code
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval     N/A
 */
volatile uint32_t EdmaCallbackcnt = 0;
void rangeProcHWA_EDMA_transferCompletionCallbackFxn(Edma_IntrHandle intrHandle,
   void *args)
{
    rangeProcHWAObj     *rangeProcObj;

    /* Get rangeProc object */
    rangeProcObj = (rangeProcHWAObj *)args;

    EdmaCallbackcnt++;
    if (intrHandle->tccNum == rangeProcObj->dataOutSignatureChan)
    {
        rangeProcObj->numEdmaDataOutCnt++;
        SemaphoreP_post(&rangeProcObj->edmaDoneSemaHandle);
    }
}

/**
 *  @b Description
 *  @n
 *      Function to calculate the DC Estimation scale and shift
 *      to be sent to the HWA.
 *
 *  @param[in]   numSamples              number of ADC samples
 *  @param[out]  scaleVal                scale to be fed to HWA
 *  @param[out]  shiftVal                shift value to be fed to HWA
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval     N/A
 */
uint32_t rangeProcHWADDMA_findDCEstStaticParams(uint32_t numSamples, uint32_t * scaleVal, uint32_t * shiftVal){
    
    uint32_t scale_best = 0U;
    uint32_t shift_best = 6U;
    uint32_t shift, scale_curr;
    
    for(shift = 0U; shift <= 14U; shift++){
        scale_curr = (1U << (shift + 8U + 6U)) / numSamples;
        if((scale_curr > scale_best) && (scale_curr < (1U << 9U) - 1U)){
            scale_best = scale_curr;
            shift_best = shift;
        }
    }

    *scaleVal = scale_best;
    *shiftVal = shift_best;
    return 0U;

}


/**
 *  @b Description
 *  @n
 *      Function to calculate the Interference mitigation static params
 *      to be sent to the HWA.
 *
 *  @param[in]   numSamples              number of ADC samples
 *  @param[in]   SNRdB                   SNR in dB
 *  @param[out]  scaleVal                scale to be fed to HWA
 *  @param[out]  shiftVal                shift value to be fed to HWA
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval     N/A
 */
uint32_t rangeProcHWADDMA_findIntfStatsStaticParams(uint32_t numSamples, uint32_t SNRdB, uint32_t * scaleVal, uint32_t * shiftVal){
    
    uint32_t scale_best = 0;
    uint32_t shift_best = 6;
    uint32_t shift, scale_curr;
    
    for(shift = 0; shift <= 6; shift++){
        scale_curr = MATHUTILS_ROUND_FLOAT((float)((pow(10, ((double)SNRdB/20.0))/numSamples*(256*2)*(1 << shift))));
        if((scale_curr > scale_best) && (scale_curr < (1 << 8) - 1)){
            scale_best = scale_curr;
            shift_best = shift;
        }
    }

    *scaleVal = scale_best;
    *shiftVal = shift_best;

    return 0;

}

/**
 *  @b Description
 *  @n
 *      Function to config a dummy channel with 3 linked paramset. Each paramset is linked
 *   to a EDMA data copy channel
 *
 *  @param[in]  dpParams                Pointer to data path parameters
 *  @param[in]  handle                  EDMA handle
 *  @param[in]  chanCfg                 EDMA channel configuraton
 *  @param[in]  chainingCfg             EDMA chaining configuration
 *  @param[in]  srcAddress              EDMA copy source address
 *  @param[in]  destAddress             EDMA copy destination address
 *  @param[in]  isTransferCompletionEnabled Number of iterations the dummy channel will be excuted.
 *  @param[in]  transferCompletionCallbackFxn Transfer completion call back function.
 *  @param[in]  transferCompletionCallbackFxnArg Argument for transfer completion call back function.
 *  @param[out] intrObj                 EDMA Interrupt Object
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval     N/A
 */
static int32_t rangeProcHWA_ConfigEDMATranspose
(
    rangeProc_dpParams      *dpParams,
    EDMA_Handle             handle,
    DPEDMA_ChanCfg          *chanCfg,
    DPEDMA_ChainingCfg      *chainingCfg,
    uint32_t                srcAddress,
    uint32_t                destAddress,
    bool                    isTransferCompletionEnabled,
    Edma_EventCallback      transferCompletionCallbackFxn,
    void*               transferCompletionCallbackFxnArg,
    Edma_IntrObject         *intrObj
)
{
    DPEDMA_syncABCfg        syncABCfg;
    int32_t                 retVal;

    /* dpedma configuration */
    syncABCfg.aCount = dpParams->numRxAntennas * sizeof(cmplx16ImRe_t); //dpParams->sizeOfInputSample; //
    syncABCfg.bCount = dpParams->numRangeBins;
    syncABCfg.cCount = dpParams->numChirpsPerFrame/2U;
    syncABCfg.srcBIdx = dpParams->numRxAntennas * sizeof(cmplx16ImRe_t);
    syncABCfg.srcCIdx = 0U;
    syncABCfg.dstBIdx = dpParams->numRxAntennas *dpParams->numChirpsPerFrame * sizeof(cmplx16ImRe_t);
    syncABCfg.dstCIdx = dpParams->numRxAntennas * 2U * sizeof(cmplx16ImRe_t);

    syncABCfg.srcAddress = srcAddress;
    syncABCfg.destAddress= destAddress;

    retVal = DPEDMA_configSyncAB(handle,
            chanCfg,
            chainingCfg,
            &syncABCfg,
            true,    /* isEventTriggered */
            false,   /* isIntermediateTransferCompletionEnabled */
            isTransferCompletionEnabled,   /* isTransferCompletionEnabled */
            transferCompletionCallbackFxn,
            transferCompletionCallbackFxnArg,
            intrObj);

    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

exit:
    return (retVal);
}

/**
 *  @b Description
 *  @n
 *      Function to config a EDMA in transpose format, when compression is enabled
 *
 *  @param[in]  dpParams                Pointer to data path parameters
 *  @param[in]  handle                  EDMA handle
 *  @param[in]  compressionParams       Compression parameters
 *  @param[in]  chanCfg                 EDMA channel configuraton
 *  @param[in]  chainingCfg             EDMA chaining configuration
 *  @param[in]  srcAddress              EDMA copy source address
 *  @param[in]  destAddress             EDMA copy destination address
 *  @param[in]  isTransferCompletionEnabled Number of iterations the dummy channel will be excuted.
 *  @param[in]  transferCompletionCallbackFxn Transfer completion call back function.
 *  @param[in]  transferCompletionCallbackFxnArg Argument for transfer completion call back function.
 *  @param[out] intrObj                 EDMA Interrupt Object
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval     N/A
 */
static int32_t rangeProcHWA_ConfigEDMATransposeCompressed
(
    rangeProc_dpParams      *dpParams,
    rangeProcHWACompressionCfg  *compressionParams,
    EDMA_Handle             handle,
    DPEDMA_ChanCfg          *chanCfg,
    DPEDMA_ChainingCfg      *chainingCfg,
    uint32_t                srcAddress,
    uint32_t                destAddress,
    bool                    isTransferCompletionEnabled,
    Edma_EventCallback      transferCompletionCallbackFxn,
    void*                   transferCompletionCallbackFxnArg,
    Edma_IntrObject         *intrObj
)
{
    DPEDMA_syncABCfg        syncABCfg;
    int32_t                 retVal;

    /* dpedma configuration */
    if(compressionParams->compressionMethod==HWA_COMPRESS_METHOD_BFP)
    {
        syncABCfg.aCount = compressionParams->outputBytesPerBlock * compressionParams->rxAntPerBlock;
        syncABCfg.bCount = compressionParams->numBlocks / compressionParams->rxAntPerBlock;
        syncABCfg.cCount = dpParams->numChirpsPerFrame/2U;
        syncABCfg.srcBIdx = compressionParams->outputBytesPerBlock * compressionParams->rxAntPerBlock;
        syncABCfg.srcCIdx = 0U;
        syncABCfg.dstBIdx = compressionParams->outputBytesPerBlock*compressionParams->rxAntPerBlock*dpParams->numChirpsPerFrame;
        syncABCfg.dstCIdx = compressionParams->outputBytesPerBlock*compressionParams->rxAntPerBlock * 2U;
    }
    else
    {
        syncABCfg.aCount = compressionParams->outputBytesPerBlock;
        syncABCfg.bCount = compressionParams->numBlocks;
        syncABCfg.cCount = dpParams->numChirpsPerFrame/2U;
        syncABCfg.srcBIdx = compressionParams->outputBytesPerBlock;
        syncABCfg.srcCIdx = 0U;
        syncABCfg.dstBIdx = compressionParams->outputBytesPerBlock*dpParams->numChirpsPerFrame;
        syncABCfg.dstCIdx = compressionParams->outputBytesPerBlock * 2U;
    }

    syncABCfg.srcAddress = srcAddress;
    syncABCfg.destAddress= destAddress;

    retVal = DPEDMA_configSyncAB(handle,
            chanCfg,
            chainingCfg,
            &syncABCfg,
            true,    /* isEventTriggered */
            false,   /* isIntermediateTransferCompletionEnabled */
            isTransferCompletionEnabled,   /* isTransferCompletionEnabled */
            transferCompletionCallbackFxn,
            transferCompletionCallbackFxnArg,
            intrObj);

    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

exit:
    return (retVal);
}

/**
 *  @b Description
 *  @n
 *      Internal function to config HWA to perform range FFT
 *
 *  @param[in]  rangeProcObj                  Pointer to rangeProc object
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
static int32_t rangeProcHWA_ConfigHWACommon
(
    rangeProcHWAObj                     *rangeProcObj
)
{
    
    int32_t     retVal;
#if defined(SOC_AWR294X)
    volatile uint8_t PGVer = SOC_RCM_ES1_PG_VER;
    PGVer=SOC_rcmGetEfusePGVer();
#endif

    if (rangeProcObj == NULL)
    {
        retVal = DPU_RANGEPROCHWA_EINVAL;
        goto exit;
    }

    HWA_CommonConfig    hwaCommonConfig;
    rangeProc_dpParams  *DPParams;
    DPU_RangeProcHWA_HwaConfig *hwaCfg;

    DPParams = &rangeProcObj->params;
    hwaCfg = &rangeProcObj->hwaCfg;

    /***********************/
    /* HWA COMMON CONFIG   */
    /***********************/
    /* Config Common Registers */
    hwaCommonConfig.configMask = HWA_COMMONCONFIG_MASK_STATEMACHINE_CFG |/* numLoops, paramStartIdx, paramStopIdx combined here */
                        HWA_COMMONCONFIG_MASK_TWIDDITHERENABLE |
                        HWA_COMMONCONFIG_MASK_LFSRSEED |
                        HWA_COMMONCONFIG_MASK_INTERF_MITG_WINDOW_PARAM |
                        HWA_COMMONCONFIG_MASK_DCEST_SCALESHIFT |
                        HWA_COMMONCONFIG_MASK_INTERFSUM_MAG |
                        HWA_COMMONCONFIG_MASK_INTERFSUM_MAGDIFF |
                        HWA_COMMONCONFIG_MASK_TWIDINCR_DELTA_FRAC;

#if defined(SOC_AWR294X)
    if(PGVer == SOC_RCM_ES2_PG_VER){
        hwaCommonConfig.configMask |= HWA_COMMONCONFIG_MASK_CMP_LFSRSEED0;
        hwaCommonConfig.compressConfig.cmpLfsrSeed0 = 0x0000000B;
    }
#endif

    if(rangeProcObj->compressionCfg.compressionMethod == HWA_COMPRESS_METHOD_EGE)
    {
        hwaCommonConfig.configMask |= HWA_COMMONCONFIG_MASK_EGECOMRESS_KPARAM;

        /* EGE Compression values */
        hwaCommonConfig.compressConfig.EGEKparam[0] = 3;
        hwaCommonConfig.compressConfig.EGEKparam[1] = 4;
        hwaCommonConfig.compressConfig.EGEKparam[2] = 5;
        hwaCommonConfig.compressConfig.EGEKparam[3] = 7;
        hwaCommonConfig.compressConfig.EGEKparam[4] = 9;
        hwaCommonConfig.compressConfig.EGEKparam[5] = 11;
        hwaCommonConfig.compressConfig.EGEKparam[6] = 13;
        hwaCommonConfig.compressConfig.EGEKparam[7] = 15;
    }

    hwaCommonConfig.fftConfig.twidDitherEnable = HWA_FEATURE_BIT_ENABLE; /* Enable Dither for FFT twiddle factor to attenuate quantization spurs. */
    hwaCommonConfig.fftConfig.lfsrSeed = 0x0000000B; /*Some non-zero value*/
    hwaCommonConfig.numLoops = DPParams->numChirpsPerFrame/2U;
    hwaCommonConfig.paramStartIdx = rangeProcObj->hwaCfg.paramSetStartIdx;
    hwaCommonConfig.paramStopIdx = rangeProcObj->hwaCfg.paramSetStartIdx + rangeProcObj->hwaCfg.numParamSet - 1U;

    /* DC Est shift and scale */
    hwaCommonConfig.dcEstimateConfig.scale = rangeProcObj->dcEstShiftScaleCfg.scale;
    hwaCommonConfig.dcEstimateConfig.shift = rangeProcObj->dcEstShiftScaleCfg.shift;
    
    /* Interf config */
    hwaCommonConfig.interfConfig.sumMagScale = rangeProcObj->intfStatsMagShiftScaleCfg.scale;
    hwaCommonConfig.interfConfig.sumMagShift = rangeProcObj->intfStatsMagShiftScaleCfg.shift;
    hwaCommonConfig.interfConfig.sumMagDiffScale = rangeProcObj->intfStatsMagDiffShiftScaleCfg.scale;
    hwaCommonConfig.interfConfig.sumMagDiffShift = rangeProcObj->intfStatsMagDiffShiftScaleCfg.shift;
    
    hwaCommonConfig.interfConfig.mitigationWindowParam[0] = hwaCfg->hwaInterfMitigWindow[0];
    hwaCommonConfig.interfConfig.mitigationWindowParam[1] = hwaCfg->hwaInterfMitigWindow[1];
    hwaCommonConfig.interfConfig.mitigationWindowParam[2] = hwaCfg->hwaInterfMitigWindow[2];
    hwaCommonConfig.interfConfig.mitigationWindowParam[3] = hwaCfg->hwaInterfMitigWindow[3];
    hwaCommonConfig.interfConfig.mitigationWindowParam[4] = hwaCfg->hwaInterfMitigWindow[4];

    hwaCommonConfig.complexMultiplyConfig.twiddleDeltaFrac = 0;

    retVal = HWA_configCommon(rangeProcObj->initParms.hwaHandle, &hwaCommonConfig);
    if (retVal != 0)
    {
        goto exit;
    }

    /**********************************************/
    /* ENABLE NUMLOOPS DONE INTERRUPT FROM HWA */
    /**********************************************/
    retVal = HWA_enableDoneInterrupt(rangeProcObj->initParms.hwaHandle,
                                        0, //thread index
                                        rangeProcHWADoneIsrCallback,
                                        (void*)&rangeProcObj->hwaDoneSemaHandle);
    if (retVal != 0)
    {
        goto exit;
    }

exit:
    return(retVal);
}

/**
 *  @b Description
 *  @n
 *      Internal function to config HWA to perform range FFT
 *
 *  @param[in]  rangeProcObj                  Pointer to rangeProc object
 *  @param[in]  destChanPing                  Destination channel id for PING
 *  @param[in]  destChanPong                  Destination channel id for PONG
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
static int32_t rangeProcHWA_ConfigHWA
(
    rangeProcHWAObj     *rangeProcObj,
    uint8_t     destChanPing,
    uint8_t     destChanPong
)
{
    HWA_InterruptConfig     paramISRConfig;
    int32_t                 errCode = 0;
    uint32_t                paramsetIdx = 0;
    uint32_t                hwParamsetIdx;
    HWA_ParamConfig         hwaParamCfg[DPU_RANGEPROCHWA_MAX_NUM_HWA_PARAM_SETS_BFP_DDMA];
    HWA_Handle                      hwaHandle;
    rangeProcHWACompressionCfg      *pDPCompParams;
    rangeProc_dpParams             *pDPParams;
    uint8_t                         index, rxAntIdx;
#if defined(SOC_AWR294X)
    volatile uint8_t PGVer = SOC_RCM_ES1_PG_VER;
    PGVer=SOC_rcmGetEfusePGVer();
#endif

    hwaHandle = rangeProcObj->initParms.hwaHandle;
    pDPParams = &rangeProcObj->params;
    pDPCompParams = &rangeProcObj->compressionCfg;

    memset(hwaParamCfg,0,sizeof(hwaParamCfg));

    hwParamsetIdx = rangeProcObj->hwaCfg.paramSetStartIdx;
    for(index = 0; index < rangeProcObj->hwaCfg.numParamSet; index++)
    {
        errCode = HWA_disableParamSetInterrupt(hwaHandle, index + rangeProcObj->hwaCfg.paramSetStartIdx, 
                HWA_PARAMDONE_INTERRUPT_TYPE_CPU_INTR1 |HWA_PARAMDONE_INTERRUPT_TYPE_DMA);
        if (errCode != 0)
        {
            goto exit;
        }
    }

    /*************************/
    /* PING 1 DUMMY PARAMSET */
    /*************************/
    {
    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_DMA;
    hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_NONE;

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
    }

    /*********************************/
    /* PING 1 DC ESTIMATION PARAMSET */
    /*********************************/
    {
    paramsetIdx++;
    hwParamsetIdx++;

    if(rangeProcObj->hwaCfg.dataInputMode == DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM)
    {
        /* At a HWA trigger time adc samples are already in HWA memory */
        hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_HARDWARE;
        hwaParamCfg[paramsetIdx].triggerSrc = rangeProcObj->hwaCfg.hardwareTrigSrc;
    }
    else
    {
        /* adcbuf not mapped, HWA is triggered after edma copy is done */
        hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_DMA;
        hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;
    }

    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;
    
    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_PARAMRESET;

    /* ACCELMODE CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_DISABLE; /* No FFT is being performed here */

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_LOG2_DISABLED;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_DCEST_PING_IN; 

    hwaParamCfg[paramsetIdx].source.srcAcnt = pDPParams->numAdcSamples - 1; /* this is samples - 1 */
#ifdef CASCADE_EVM
    hwaParamCfg[paramsetIdx].source.srcAIdx = (pDPParams->numRxAntennas/2) * pDPParams->sizeOfInputSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = (pDPParams->numRxAntennas/2)-1;
#else
    hwaParamCfg[paramsetIdx].source.srcAIdx = pDPParams->numRxAntennas * pDPParams->sizeOfInputSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = pDPParams->numRxAntennas-1;
#endif
    hwaParamCfg[paramsetIdx].source.srcBIdx = pDPParams->sizeOfInputSample;

    
    if(pDPParams->isReal)
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_REAL;
    }
    else
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    }
    hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].source.srcSign = HWA_SAMPLES_SIGNED;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8; 

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_DCSUB_PING_OUT; /* Shouldn't matter */

    hwaParamCfg[paramsetIdx].dest.dstAcnt = pDPParams->numAdcSamples - 1; /* No FFT is being performed here */
#ifdef CASCADE_EVM
    hwaParamCfg[paramsetIdx].dest.dstAIdx = (pDPParams->numRxAntennas/2) * pDPParams->sizeOfInputSample;
#else
    hwaParamCfg[paramsetIdx].dest.dstAIdx = pDPParams->numRxAntennas * pDPParams->sizeOfInputSample;
#endif
    hwaParamCfg[paramsetIdx].dest.dstBIdx = pDPParams->sizeOfInputSample;

    if(pDPParams->isReal)
    {
        hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_REAL;
    }
    else
    {
        hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    }
    hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].dest.dstSign = HWA_SAMPLES_SIGNED; 
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE; 
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0; 

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    }

    /**********************************************************************/
    /* PING 1 DC SUBTRACTION, INTERFERENCE STATISTICS ESTIMATION PARAMSET */
    /**********************************************************************/
{{
    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;
    
    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubEnable = HWA_FEATURE_BIT_ENABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubSelect = HWA_DCSUB_SELECT_DCEST;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfStat.resetMode = HWA_DCEST_INTERFSUM_RESET_MODE_PARAMRESET;

    /* ACCELMODE CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_DISABLE; /* No FFT is being performed here */

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_LOG2_DISABLED;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_DCSUB_PING_IN; 

#ifdef CASCADE_EVM
    hwaParamCfg[paramsetIdx].source.srcAIdx = (pDPParams->numRxAntennas/2) * pDPParams->sizeOfInputSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = (pDPParams->numRxAntennas/2)-1;
#else
    hwaParamCfg[paramsetIdx].source.srcAIdx = pDPParams->numRxAntennas * pDPParams->sizeOfInputSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = pDPParams->numRxAntennas-1;
#endif
    hwaParamCfg[paramsetIdx].source.srcBIdx = pDPParams->sizeOfInputSample;

    
    if(pDPParams->isReal)
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_REAL;
    }
    else
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    }
    hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].source.srcSign = HWA_SAMPLES_SIGNED;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8; 

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_DCSUB_PING_OUT; 

    hwaParamCfg[paramsetIdx].dest.dstAcnt = pDPParams->numAdcSamples - 1; /* No FFT is being performed here */
#ifdef CASCADE_EVM
    hwaParamCfg[paramsetIdx].dest.dstAIdx = (pDPParams->numRxAntennas/2) * pDPParams->sizeOfInputSample;
#else
    hwaParamCfg[paramsetIdx].dest.dstAIdx = pDPParams->numRxAntennas * pDPParams->sizeOfInputSample;
#endif
    hwaParamCfg[paramsetIdx].dest.dstBIdx = pDPParams->sizeOfInputSample;

    if(pDPParams->isReal)
    {
        hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_REAL;
    }
    else
    {
        hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    }
    hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].dest.dstSign = HWA_SAMPLES_SIGNED; 
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE; 
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0; 

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
}}

    /************************************************/
    /* PING 1 INTERFERENCE MITIGATION, FFT PARAMSET */
    /************************************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;
    
    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubEnable = HWA_FEATURE_BIT_DISABLE; /* Already done in previous paramset */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfStat.resetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
    
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfLocalize.thresholdEnable = HWA_FEATURE_BIT_ENABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfLocalize.thresholdMode = HWA_INTERFTHRESH_MODE_MAGDIFF;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfLocalize.thresholdSelect = HWA_INTERFTHRESH_SELECT_EST_INDIVIDUAL;

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfMitigation.enable = HWA_FEATURE_BIT_ENABLE; 
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfMitigation.countThreshold = 1;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfMitigation.pathSelect = HWA_INTERFMITIGATION_PATH_WINDOWZEROOUT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfMitigation.leftHystOrder = 3;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfMitigation.rightHystOrder = 3;

    /* ACCELMODE CONFIG (FFT) */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_ENABLE; 
    if(pDPParams->numFFTBins % 3 == 0){
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(pDPParams->numFFTBins/3);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_ENABLE;
    }
    else{
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(pDPParams->numFFTBins);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_DISABLE;
    }

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.butterflyScaling = 7; /* 1 << 3 - 1 */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.windowEn = HWA_FEATURE_BIT_ENABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.windowStart = rangeProcObj->hwaCfg.hwaWinRamOffset;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.winSymm = rangeProcObj->hwaCfg.hwaWinSym;

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_LOG2_DISABLED;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_FFT_PING_IN; 

    hwaParamCfg[paramsetIdx].source.srcAcnt = pDPParams->numAdcSamples - 1; /* this is samples - 1 */
#ifdef CASCADE_EVM
    hwaParamCfg[paramsetIdx].source.srcAIdx = (pDPParams->numRxAntennas/2) * pDPParams->sizeOfInputSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = (pDPParams->numRxAntennas/2)-1;
#else
    hwaParamCfg[paramsetIdx].source.srcAIdx = pDPParams->numRxAntennas * pDPParams->sizeOfInputSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = pDPParams->numRxAntennas-1;
#endif
    hwaParamCfg[paramsetIdx].source.srcBIdx = pDPParams->sizeOfInputSample;

    
    if(pDPParams->isReal)
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_REAL;
    }
    else
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    }
    hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].source.srcSign = HWA_SAMPLES_SIGNED;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8; 

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_FFT_PING_OUT; 

    hwaParamCfg[paramsetIdx].dest.dstAcnt = pDPParams->numRangeBins-1;
    hwaParamCfg[paramsetIdx].dest.dstAIdx = pDPParams->numRxAntennas * sizeof(cmplx16ImRe_t);
    hwaParamCfg[paramsetIdx].dest.dstBIdx = sizeof(cmplx16ImRe_t);

    hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].dest.dstSign = HWA_SAMPLES_SIGNED; 
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE; 
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0; 

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
}}

#ifdef CASCADE_EVM
    /**********************************/
    /* PING 2  DC ESTIMATION PARAMSET */
    /**********************************/
    {
    paramsetIdx++;
    hwParamsetIdx++;
    if(rangeProcObj->hwaCfg.dataInputMode == DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM)
    {
        /* At a HWA trigger time adc samples are already in HWA memory */
        hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_HARDWARE;
        hwaParamCfg[paramsetIdx].triggerSrc = HWA_HARDWARE_TRIGGER_SOURCE_CSI2B_CONTEXT_0_LINE_END;
    }
    else
    {
        /* adcbuf not mapped, HWA is triggered after edma copy is done */
        hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;//HWA_TRIG_MODE_DMA;
    }

    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;
    
    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_PARAMRESET;

    /* ACCELMODE CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_DISABLE; /* No FFT is being performed here */

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_LOG2_DISABLED;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_DCEST_PING_1_IN;

    hwaParamCfg[paramsetIdx].source.srcAcnt = pDPParams->numAdcSamples - 1; /* this is samples - 1 */
    hwaParamCfg[paramsetIdx].source.srcAIdx = (pDPParams->numRxAntennas/2) * pDPParams->sizeOfInputSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = (pDPParams->numRxAntennas/2)-1;
    hwaParamCfg[paramsetIdx].source.srcBIdx = pDPParams->sizeOfInputSample;

    
    if(pDPParams->isReal)
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_REAL;
    }
    else
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    }
    hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].source.srcSign = HWA_SAMPLES_SIGNED;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8; 

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_DCSUB_PING_OUT; /* Shouldn't matter */
    hwaParamCfg[paramsetIdx].dest.dstAcnt = pDPParams->numAdcSamples - 1; /* No FFT is being performed here */
    hwaParamCfg[paramsetIdx].dest.dstAIdx = (pDPParams->numRxAntennas/2) * pDPParams->sizeOfInputSample;
    hwaParamCfg[paramsetIdx].dest.dstBIdx = pDPParams->sizeOfInputSample;

    if(pDPParams->isReal)
    {
        hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_REAL;
    }
    else
    {
        hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    }
    hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].dest.dstSign = HWA_SAMPLES_SIGNED; 
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE; 
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0; 

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    }

    /**********************************************************************/
    /* PING 2 DC SUBTRACTION, INTERFERENCE STATISTICS ESTIMATION PARAMSET */
    /**********************************************************************/
{{
    paramsetIdx++;
    hwParamsetIdx++;
    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;
    
    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubEnable = HWA_FEATURE_BIT_ENABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubSelect = HWA_DCSUB_SELECT_DCEST;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfStat.resetMode = HWA_DCEST_INTERFSUM_RESET_MODE_PARAMRESET;

    /* ACCELMODE CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_DISABLE; /* No FFT is being performed here */

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_LOG2_DISABLED;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_DCEST_PING_1_IN;

    hwaParamCfg[paramsetIdx].source.srcAcnt = pDPParams->numAdcSamples - 1; /* this is samples - 1 */
    hwaParamCfg[paramsetIdx].source.srcAIdx = (pDPParams->numRxAntennas/2) * pDPParams->sizeOfInputSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = (pDPParams->numRxAntennas/2)-1;
    hwaParamCfg[paramsetIdx].source.srcBIdx = pDPParams->sizeOfInputSample;

    
    if(pDPParams->isReal)
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_REAL;
    }
    else
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    }
    hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].source.srcSign = HWA_SAMPLES_SIGNED;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8; 

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_DCSUB_PING_OUT;
    hwaParamCfg[paramsetIdx].dest.dstAcnt = pDPParams->numAdcSamples - 1; /* No FFT is being performed here */
    hwaParamCfg[paramsetIdx].dest.dstAIdx = (pDPParams->numRxAntennas/2) * pDPParams->sizeOfInputSample;
    hwaParamCfg[paramsetIdx].dest.dstBIdx = pDPParams->sizeOfInputSample;

    if(pDPParams->isReal)
    {
        hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_REAL;
    }
    else
    {
        hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    }
    hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].dest.dstSign = HWA_SAMPLES_SIGNED; 
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE; 
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0; 

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
}}

    /************************************************/
    /* PING 2 INTERFERENCE MITIGATION, FFT PARAMSET */
    /************************************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;
    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;
    
    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubEnable = HWA_FEATURE_BIT_DISABLE; /* Already done in previous paramset */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfStat.resetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
    
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfLocalize.thresholdEnable = HWA_FEATURE_BIT_ENABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfLocalize.thresholdMode = HWA_INTERFTHRESH_MODE_MAGDIFF;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfLocalize.thresholdSelect = HWA_INTERFTHRESH_SELECT_EST_INDIVIDUAL;

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfMitigation.enable = HWA_FEATURE_BIT_ENABLE; 
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfMitigation.countThreshold = 1;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfMitigation.pathSelect = HWA_INTERFMITIGATION_PATH_WINDOWZEROOUT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfMitigation.leftHystOrder = 3;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.interfMitigation.rightHystOrder = 3;

    /* ACCELMODE CONFIG (FFT) */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_ENABLE; 
    if(pDPParams->numFFTBins % 3 == 0){
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(pDPParams->numFFTBins/3);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_ENABLE;
    }
    else{
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(pDPParams->numFFTBins);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_DISABLE;
    }

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.butterflyScaling = 7; /* 1 << 3 - 1 */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.windowEn = HWA_FEATURE_BIT_ENABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.windowStart = rangeProcObj->hwaCfg.hwaWinRamOffset;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.winSymm = rangeProcObj->hwaCfg.hwaWinSym;

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_LOG2_DISABLED;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_FFT_PING_IN;
    hwaParamCfg[paramsetIdx].source.srcAcnt = pDPParams->numAdcSamples - 1; /* this is samples - 1 */
    hwaParamCfg[paramsetIdx].source.srcAIdx = (pDPParams->numRxAntennas/2) * pDPParams->sizeOfInputSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = (pDPParams->numRxAntennas/2)-1;
    hwaParamCfg[paramsetIdx].source.srcBIdx = pDPParams->sizeOfInputSample;

    
    if(pDPParams->isReal)
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_REAL;
    }
    else
    {
        hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    }
    hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].source.srcSign = HWA_SAMPLES_SIGNED;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8; 

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_FFT_PING_OUT + (pDPParams->numRxAntennas/2) * sizeof(cmplx16ImRe_t);
    hwaParamCfg[paramsetIdx].dest.dstAcnt = pDPParams->numRangeBins-1;
    hwaParamCfg[paramsetIdx].dest.dstAIdx = (pDPParams->numRxAntennas) * sizeof(cmplx16ImRe_t);
    hwaParamCfg[paramsetIdx].dest.dstBIdx = sizeof(cmplx16ImRe_t);

    hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].dest.dstSign = HWA_SAMPLES_SIGNED; 
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE; 
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0; 

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
}}
#endif

    /*****************************/
    /* PING COMPRESSION PARAMSET */
    /*****************************/
{{
    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_COMPRESS;

    /* ACCELMODE CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.ditherEnable = HWA_FEATURE_BIT_ENABLE;  // Enable dither to suppress quantization spurs
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.compressDecompress = HWA_CMP_DCMP_COMPRESS;
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.method = pDPCompParams->compressionMethod;
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.passSelect = HWA_COMPRESS_PATHSELECT_BOTHPASSES;
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.headerEnable = HWA_FEATURE_BIT_ENABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.scaleFactorBW = 4; //log2(sample bits) 
#if defined(SOC_AWR294X)
    if(PGVer == SOC_RCM_ES2_PG_VER){
        hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.selLfsr = 0;
    }
#endif

    /* SRC CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_COMP_PING_IN; 

    hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].source.srcSign = HWA_SAMPLES_SIGNED;
    hwaParamCfg[paramsetIdx].source.srcConjugate = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].source.srcScale = 8; 

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_COMP_PING_OUT; 
    hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_COMPLEX; 
    hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT; /* 16 bit real, 16 bit imag */
    hwaParamCfg[paramsetIdx].dest.dstSign = HWA_SAMPLES_UNSIGNED; 
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE; 
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0; 

    if(pDPCompParams->compressionMethod==HWA_COMPRESS_METHOD_BFP)
    {
        /*************************************/
        /* PING BFP COMPRESSION PARAMSET RX1 */
        /*************************************/
        hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.BFPMantissaBW = floor((pDPCompParams->outputBytesPerBlock*8U - hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.scaleFactorBW)\
                                                                            /(pDPCompParams->inputSamplesPerBlock * 2U));
        /* SRC CONFIG */
        hwaParamCfg[paramsetIdx].source.srcAcnt = pDPCompParams->inputSamplesPerBlock - 1;
        hwaParamCfg[paramsetIdx].source.srcAIdx = pDPCompParams->bytesPerSample * pDPCompParams->rxAntPerBlock;
        hwaParamCfg[paramsetIdx].source.srcBcnt = pDPCompParams->numBlocks/pDPCompParams->rxAntPerBlock - 1;
        hwaParamCfg[paramsetIdx].source.srcBIdx = pDPCompParams->inputBytesPerBlock * pDPCompParams->rxAntPerBlock;

        /* DEST CONFIG */
        hwaParamCfg[paramsetIdx].dest.dstAcnt = pDPCompParams->outputSamplesPerBlock - 1;
        hwaParamCfg[paramsetIdx].dest.dstAIdx = pDPCompParams->bytesPerSample;
        hwaParamCfg[paramsetIdx].dest.dstBIdx = pDPCompParams->outputBytesPerBlock * pDPCompParams->rxAntPerBlock;

        errCode = HWA_configParamSet(hwaHandle,
                                    hwParamsetIdx,
                                    &hwaParamCfg[paramsetIdx],NULL);
        if (errCode != 0)
        {
            goto exit;
        }

        /*************************************/
        /* PING BFP COMPRESSION PARAMSET RXN */
        /*************************************/
        for(rxAntIdx=1; rxAntIdx<pDPCompParams->rxAntPerBlock; rxAntIdx++)
        {
            paramsetIdx++;
            hwParamsetIdx++;
            hwaParamCfg[paramsetIdx] = hwaParamCfg[paramsetIdx-1];
            hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_COMP_PING_IN + rxAntIdx*pDPCompParams->bytesPerSample;
            hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_COMP_PING_OUT + rxAntIdx*pDPCompParams->outputBytesPerBlock;
            errCode = HWA_configParamSet(hwaHandle,
                                        hwParamsetIdx,
                                        &hwaParamCfg[paramsetIdx],NULL);
            if (errCode != 0)
            {
                goto exit;
            }
        }
    }

    else if(pDPCompParams->compressionMethod == HWA_COMPRESS_METHOD_EGE)
    {
        /*********************************/
        /* PING EGE COMPRESSION PARAMSET */
        /*********************************/
        hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.EGEKarrayLength = 3; //log2(8)

        /* SRC CONFIG */
        hwaParamCfg[paramsetIdx].source.srcAcnt = pDPCompParams->inputSamplesPerBlock - 1;
        hwaParamCfg[paramsetIdx].source.srcAIdx = pDPCompParams->bytesPerSample;
        hwaParamCfg[paramsetIdx].source.srcBcnt = pDPCompParams->numBlocks - 1;
        hwaParamCfg[paramsetIdx].source.srcBIdx = pDPCompParams->inputBytesPerBlock;

        /* DEST CONFIG */
        hwaParamCfg[paramsetIdx].dest.dstAcnt = pDPCompParams->outputSamplesPerBlock - 1;
        hwaParamCfg[paramsetIdx].dest.dstAIdx = pDPCompParams->bytesPerSample;
        hwaParamCfg[paramsetIdx].dest.dstBIdx = pDPCompParams->outputBytesPerBlock;

        errCode = HWA_configParamSet(hwaHandle,
                                    hwParamsetIdx,
                                    &hwaParamCfg[paramsetIdx],NULL);
        if (errCode != 0)
        {
            goto exit;
        }
    }

    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChanPing;

    errCode = HWA_enableParamSetInterrupt(hwaHandle,hwParamsetIdx,&paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }
}}

    /*************************/
    /* PONG 1 DUMMY PARAMSET */
    /*************************/
    {
    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_DMA;
#ifdef CASCADE_EVM
    /* As there will be 2x number of paramsets in the cascade range DPU*/
    hwaParamCfg[paramsetIdx].triggerSrc = (hwParamsetIdx/2) + 1;
#else
    hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;
#endif
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_NONE;
    errCode = HWA_configParamSet(hwaHandle, 
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
    }

    /*********************************/
    /* PONG 1 DC ESTIMATION PARAMSET */
    /*********************************/
    {
    paramsetIdx++;
    hwParamsetIdx++;
    hwaParamCfg[paramsetIdx] = hwaParamCfg[DCEST_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_DCEST_PONG_IN; 
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_DCSUB_PONG_OUT;

    if(rangeProcObj->hwaCfg.dataInputMode != DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM)
    {
        hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;
    }

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
    }

    /**********************************************************************/
    /* PONG 1 DC SUBTRACTION, INTERFERENCE STATISTICS ESTIMATION PARAMSET */
    /**********************************************************************/
    {
    paramsetIdx++;
    hwParamsetIdx++;
    hwaParamCfg[paramsetIdx] = hwaParamCfg[DCSUB_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_DCSUB_PONG_IN; 
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_DCSUB_PONG_OUT;

    if(rangeProcObj->hwaCfg.dataInputMode != DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM)
    {
        hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;
    }

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
    }

    /************************************************/
    /* PONG 1 INTERFERENCE MITIGATION, FFT PARAMSET */
    /************************************************/
    {
    paramsetIdx++;
    hwParamsetIdx++;
    hwaParamCfg[paramsetIdx] = hwaParamCfg[FFT_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_FFT_PONG_IN; 
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_FFT_PONG_OUT;

    if(rangeProcObj->hwaCfg.dataInputMode != DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM)
    {
        hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;
    }

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
    }

#ifdef CASCADE_EVM
    /*********************************/
    /* PONG 2 DC ESTIMATION PARAMSET */
    /*********************************/
    {
    paramsetIdx++;
    hwParamsetIdx++;
    hwaParamCfg[paramsetIdx] = hwaParamCfg[DCEST_PING_HWA_PARAMSET_RELATIVE_IDX1];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_DCEST_PONG_1_IN;
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_DCSUB_PONG_OUT;

    if(rangeProcObj->hwaCfg.dataInputMode != DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM)
    {
        hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;
    }

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
    }

    /**********************************************************************/
    /* PONG 2 DC SUBTRACTION, INTERFERENCE STATISTICS ESTIMATION PARAMSET */
    /**********************************************************************/
    {
    paramsetIdx++;
    hwParamsetIdx++;
    hwaParamCfg[paramsetIdx] = hwaParamCfg[DCSUB_PING_HWA_PARAMSET_RELATIVE_IDX1];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_DCEST_PONG_1_IN;
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_DCSUB_PONG_OUT;
 
    if(rangeProcObj->hwaCfg.dataInputMode != DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM)
    {
        hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;
    }

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
    }

    /************************************************/
    /* PONG 2 INTERFERENCE MITIGATION, FFT PARAMSET */
    /************************************************/
    {
    paramsetIdx++;
    hwParamsetIdx++;
    hwaParamCfg[paramsetIdx] = hwaParamCfg[FFT_PING_HWA_PARAMSET_RELATIVE_IDX1];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_FFT_PONG_IN;
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_FFT_PONG_OUT + (pDPParams->numRxAntennas/2) * sizeof(cmplx16ImRe_t);

    if(rangeProcObj->hwaCfg.dataInputMode != DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM)
    {
        hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;
    }

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
    }
#endif

    /*****************************/
    /* PONG COMPRESSION PARAMSET */
    /*****************************/
    {
    paramsetIdx++;
    hwParamsetIdx++;
    hwaParamCfg[paramsetIdx] = hwaParamCfg[COMPRESS_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_COMP_PONG_IN; 
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_COMP_PONG_OUT;

    if(rangeProcObj->hwaCfg.dataInputMode != DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM) 
    {
        hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;
    }

    errCode = HWA_configParamSet(hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    if(pDPCompParams->compressionMethod==HWA_COMPRESS_METHOD_BFP)
    {
        for(rxAntIdx=1; rxAntIdx<pDPCompParams->rxAntPerBlock; rxAntIdx++)
        {
            paramsetIdx++;
            hwParamsetIdx++;
            hwaParamCfg[paramsetIdx] = hwaParamCfg[paramsetIdx-1];
            hwaParamCfg[paramsetIdx].source.srcAddr = DPU_RANGEHWADDMA_ADDR_COMP_PONG_IN + rxAntIdx*pDPCompParams->bytesPerSample;
            hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_RANGEHWADDMA_ADDR_COMP_PONG_OUT + rxAntIdx*pDPCompParams->outputBytesPerBlock;
            errCode = HWA_configParamSet(hwaHandle,
                                        hwParamsetIdx,
                                        &hwaParamCfg[paramsetIdx],NULL);
            if (errCode != 0)
            {
                goto exit;
            }
        }
    }

    /* Enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChanPong;
    errCode = HWA_enableParamSetInterrupt(hwaHandle, 
                                           hwParamsetIdx,
                                           &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }
    }
exit:
    return(errCode);
}

/**
 *  @b Description
 *  @n
 *      Trigger HWA for range processing.
 *
 *  @param[in]  rangeProcObj              Pointer to rangeProc object
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
static int32_t rangeProcHWA_TriggerHWA
(
    rangeProcHWAObj     *rangeProcObj
)
{
    int32_t             retVal = 0;
    HWA_Handle          hwaHandle;

    /* Get HWA driver handle */
    hwaHandle = rangeProcObj->initParms.hwaHandle;

    /* Disable the HWA */
    retVal = HWA_enable(hwaHandle, 0);
    if (retVal != 0)
    {
        goto exit;
    }

    /* Configure HWA common parameters */
    retVal = rangeProcHWA_ConfigHWACommon(rangeProcObj);
    if(retVal < 0)
    {
        goto exit;
    }

    /* Enable the HWA */
    retVal = HWA_enable(hwaHandle, 1);
    if (retVal != 0)
    {
        goto exit;
    }

    /* Trigger the HWA paramset for Ping */
    retVal = HWA_setDMA2ACCManualTrig(hwaHandle, rangeProcObj->dataOutTrigger[0]);
    if (retVal != 0)
    {
        goto exit;
    }

    /* Trigger the HWA paramset for Pong */
    retVal = HWA_setDMA2ACCManualTrig(hwaHandle, rangeProcObj->dataOutTrigger[1]);
    if (retVal != 0)
    {
        goto exit;
    }

exit:
    return(retVal);
}

/**
 *  @b Description
 *  @n
 *      EDMA configuration for rangeProc data output in interleave mode
 *
 *  @param[in]  rangeProcObj              Pointer to rangeProc object
 *  @param[in]  DPParams                  Pointer to datapath parameter
 *  @param[in]  pHwConfig                 Pointer to rangeProc hardware resources
 *  @param[in]  hwaInPingOffset           Ping HWA memory Input address offset
 *  @param[in]  hwaInPongOffset           Pong HWA memory Input address offset
 *  @param[in]  hwaOutPingOffset          Ping HWA memory Output address offset
 *  @param[in]  hwaOutPongOffset          Pong HWA memory Output address offset
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
static int32_t rangeProcHWA_ConfigEDMA_DataOut_interleave
(
    rangeProcHWAObj     *rangeProcObj,
    rangeProc_dpParams  *DPParams,
    DPU_RangeProcHWA_HW_Resources *pHwConfig,
    uint32_t            hwaInPingOffset,
    uint32_t            hwaInPongOffset,
    uint32_t            hwaOutPingOffset,
    uint32_t            hwaOutPongOffset
)
{
    int32_t             errorCode = SystemP_SUCCESS;
    EDMA_Handle         handle ;
    DPEDMA_ChainingCfg       chainingCfg;
    uint16_t            pong_offset=1;

    /* Get rangeProc hardware resources pointer */
    handle = rangeProcObj->edmaHandle;

    /* Setup Chaining configuration */
    chainingCfg.chainingChannel = pHwConfig->edmaOutCfg.dataOutSignature.channel;
    chainingCfg.isIntermediateChainingEnabled = true;
    chainingCfg.isFinalChainingEnabled = true;

    if(rangeProcObj->compressionCfg.isEnabled){
        errorCode = rangeProcHWA_ConfigEDMATransposeCompressed(DPParams,
                                            &rangeProcObj->compressionCfg,
                                            handle,
                                            &pHwConfig->edmaOutCfg.u.fmt1.dataOutPing,
                                            &chainingCfg,
                                            hwaInPingOffset,
                                            (uint32_t)rangeProcObj->radarCubebuf,
                                            false,  /* isTransferCompletionEnabled */
                                            NULL,   /* transferCompletionCallbackFxn */
                                            NULL,
                                            pHwConfig->edmaTransferCompleteIntrObj);
    }
    else{
        errorCode = rangeProcHWA_ConfigEDMATranspose(DPParams,
                                            handle,
                                            &pHwConfig->edmaOutCfg.u.fmt1.dataOutPing,
                                            &chainingCfg,
                                            hwaOutPingOffset,
                                            (uint32_t)rangeProcObj->radarCubebuf,
                                            false,  /* isTransferCompletionEnabled */
                                            NULL,   /* transferCompletionCallbackFxn */
                                            NULL,
                                            pHwConfig->edmaTransferCompleteIntrObj);
    }

    if (errorCode != SystemP_SUCCESS)
    {
        goto exit;
    }
    if(rangeProcObj->compressionCfg.isEnabled){

        if(rangeProcObj->compressionCfg.compressionMethod == HWA_COMPRESS_METHOD_BFP)
        {
            pong_offset = rangeProcObj->compressionCfg.rxAntPerBlock;
        }

        errorCode = rangeProcHWA_ConfigEDMATransposeCompressed(DPParams,
                                            &rangeProcObj->compressionCfg,
                                            handle,
                                            &pHwConfig->edmaOutCfg.u.fmt1.dataOutPong,
                                            &chainingCfg,
                                            hwaInPongOffset,
                                            (uint32_t)(rangeProcObj->radarCubebuf + rangeProcObj->compressionCfg.outputSamplesPerBlock * pong_offset),
                                            true,
                                            rangeProcHWA_EDMA_transferCompletionCallbackFxn,  
                                            (void *)rangeProcObj,
                                            pHwConfig->edmaTransferCompleteIntrObj);
    }
    else{
        errorCode = rangeProcHWA_ConfigEDMATranspose(DPParams,
                                            handle,
                                            &pHwConfig->edmaOutCfg.u.fmt1.dataOutPong,
                                            &chainingCfg,
                                            hwaOutPongOffset, 
                                            (uint32_t)(rangeProcObj->radarCubebuf + DPParams->numRxAntennas),
                                            true,
                                            rangeProcHWA_EDMA_transferCompletionCallbackFxn,  
                                            (void*)rangeProcObj,
                                            pHwConfig->edmaTransferCompleteIntrObj);
    }
    if (errorCode != SystemP_SUCCESS)
    {
        goto exit;
    }

     /**************************************************************************
      *  HWA hot signature EDMA, chained to the transpose EDMA channels
      *************************************************************************/
    errorCode = DPEDMAHWA_configTwoHotSignature(handle, 
                                                  &pHwConfig->edmaOutCfg.dataOutSignature,
                                                  rangeProcObj->initParms.hwaHandle,
                                                  rangeProcObj->dataOutTrigger[0],
                                                  rangeProcObj->dataOutTrigger[1],
                                                  false);
    if (errorCode != SystemP_SUCCESS)
    {
        goto exit;
    }

exit:
    return(errorCode);
}

/**
 *  @b Description
 *  @n
 *      EDMA configuration for rangeProc data output in non-interleave mode
 *
 *  @param[in]  rangeProcObj              Pointer to rangeProc object
 *  @param[in]  DPParams                  Pointer to datapath parameter
 *  @param[in]  pHwConfig                 Pointer to rangeProc hardware resources
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
static int32_t rangeProcHWA_ConfigEDMA_DataOut_nonInterleave
(
    rangeProcHWAObj         *rangeProcObj,
    rangeProc_dpParams      *DPParams,
    DPU_RangeProcHWA_HW_Resources *pHwConfig
)
{
    /* Error: Not supported */
    return -1;
}


/**
 *  @b Description
 *  @n
 *      EDMA configuration for rangeProc data in when EDMA is used to copy data from 
 *  ADCBuf to HWA memory
 *
 *  @param[in]  rangeProcObj              Pointer to rangeProc object handle
 *  @param[in]  DPParams                  Pointer to datapath parameter
 *  @param[in]  pHwConfig                 Pointer to rangeProc hardware resources
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
static int32_t rangeProcHWA_ConfigEDMA_DataIn
(
    rangeProcHWAObj         *rangeProcObj,
    rangeProc_dpParams      *DPParams,
    DPU_RangeProcHWA_HW_Resources *pHwConfig
)
{
    int32_t             errorCode = SystemP_SUCCESS;
    EDMA_Handle         handle ;
    uint16_t            bytePerRxChan;
    DPEDMA_ChainingCfg  chainingCfg;

    /* Get rangeProc Configuration */
    handle = rangeProcObj->edmaHandle;

    bytePerRxChan = DPParams->numAdcSamples * DPParams->sizeOfInputSample;

    /**********************************************/
    /* ADCBuf -> Ping/Pong Buffer(M0 and M1)           */
    /**********************************************/
    chainingCfg.chainingChannel = pHwConfig->edmaInCfg.dataInSignature.channel;
    chainingCfg.isFinalChainingEnabled = true;
    chainingCfg.isIntermediateChainingEnabled = true;

    if (rangeProcObj->interleave == DPIF_RXCHAN_NON_INTERLEAVE_MODE)
    {
        DPEDMA_syncABCfg    syncABCfg;

        syncABCfg.srcAddress = (uint32_t)rangeProcObj->ADCdataBuf;
        syncABCfg.destAddress = rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCEST_PING_IN];

        syncABCfg.aCount = bytePerRxChan;
        syncABCfg.bCount = DPParams->numRxAntennas;
        syncABCfg.cCount =2U; /* ping and pong */

        syncABCfg.srcBIdx=rangeProcObj->rxChanOffset;
        syncABCfg.dstBIdx=rangeProcObj->rxChanOffset;
#ifdef SOC_AM273X
        if(rangeProcObj->isLVDSStreamEnabled)
        {
            syncABCfg.srcCIdx=rangeProcObj->csirxOffset;
        }
        else
        {
            syncABCfg.srcCIdx=0U;
        }
#else
        syncABCfg.srcCIdx=0U;
#endif
        syncABCfg.dstCIdx=((uint32_t)rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCEST_PONG_IN] - (uint32_t)rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCEST_PING_IN]);

        errorCode = DPEDMA_configSyncAB(handle,
                                        &pHwConfig->edmaInCfg.dataIn,
                                        &chainingCfg,
                                        &syncABCfg,
                                        true,    /* isEventTriggered */
                                        /* Intermediate and Final transfer interrupts are enabled in case
                                         * the user wants to poll the IPR(H) register (for example, 
                                         * in the range proc test case) or register an ISR for when a chirp 
                                         * transfer from to HWA memory is complete */
                                        true,   /* isIntermediateTransferInterruptEnabled */
                                        true,   /*isFinalTransferInterruptEnabled */
                                        NULL,
                                        NULL,
                                        pHwConfig->edmaTransferCompleteIntrObj);
    }
    else
    {
        DPEDMA_syncACfg    syncACfg;

        syncACfg.srcAddress = (uint32_t)rangeProcObj->ADCdataBuf;
        syncACfg.destAddress = rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCEST_PING_IN];
        syncACfg.aCount = bytePerRxChan * DPParams->numRxAntennas;
        syncACfg.bCount = 2U; /* ping and pong */

        syncACfg.dstBIdx=((uint32_t)rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCEST_PONG_IN] - (uint32_t)rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_DCEST_PING_IN]);

#ifdef SOC_AM273X
        if(rangeProcObj->isLVDSStreamEnabled)
        {
            syncACfg.srcBIdx=rangeProcObj->csirxOffset;
        }
        else
        {
            syncACfg.srcBIdx=0U;
        }
#else
        syncACfg.srcBIdx=0U;
#endif

        errorCode = DPEDMA_configSyncA_singleFrame(handle,
                                        &pHwConfig->edmaInCfg.dataIn,
                                        &chainingCfg,
                                        &syncACfg,
                                        /* Intermediate and Final transfer interrupts are enabled in case
                                         * the user wants to poll the IPR(H) register (for example, 
                                         * in the range proc test case) or register an ISR for when a chirp 
                                         * transfer from to HWA memory is complete */
                                        true,    /* isEventTriggered */
                                        true,   /* isIntermediateTransferInterruptEnabled */
                                        true,   /* isFinalTransferInterruptEnabled */
                                        NULL,
                                        NULL,
                                        pHwConfig->edmaTransferCompleteIntrObj);
    }

    if (errorCode != SystemP_SUCCESS)
    {
        goto exit;
    }

    /*************************************************/
    /* Generate Hot Signature to trigger Ping/Pong paramset   */
    /*************************************************/

    errorCode = DPEDMAHWA_configTwoHotSignature(handle, 
                                                  &pHwConfig->edmaInCfg.dataInSignature,
                                                  rangeProcObj->initParms.hwaHandle,
                                                  rangeProcObj->dataInTrigger[0],
                                                  rangeProcObj->dataInTrigger[1],
                                                  false);

    if (errorCode != SystemP_SUCCESS)
    {
        goto exit;
    }
exit:
    return(errorCode);
}

/**
 *  @b Description
 *  @n
 *      rangeProc configuration in interleaved mode
 *
 *  @param[in]  rangeProcObj                 Pointer to rangeProc object
 *  @param[in]  DPParams                     Pointer to data path common params
 *  @param[in]  pHwConfig                    Pointer to rangeProc hardware resources
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
static int32_t rangeProcHWA_ConfigInterleaveMode
(
    rangeProcHWAObj         *rangeProcObj,
    rangeProc_dpParams      *DPParams,
    DPU_RangeProcHWA_HW_Resources *pHwConfig
)
{
    int32_t             retVal = 0;
    uint8_t             destChanPing;
    uint8_t             destChanPong;
    HWA_Handle          hwaHandle;

    hwaHandle = rangeProcObj->initParms.hwaHandle;

    /* In interleave mode, only edmaOutCfgFmt is supported */
    retVal = HWA_getDMAChanIndex(hwaHandle, pHwConfig->edmaOutCfg.u.fmt1.dataOutPing.channel, &destChanPing);
    if (retVal != 0)
    {
        goto exit;
    }

    /* In interleave mode, only edmaOutCfgFmt is supported */
    retVal = HWA_getDMAChanIndex(hwaHandle, pHwConfig->edmaOutCfg.u.fmt1.dataOutPong.channel, &destChanPong);
    if (retVal != 0)
    {
        goto exit;
    }

    if(pHwConfig->hwaCfg.dataInputMode == DPU_RangeProcHWA_InputMode_ISOLATED)
    {
        /* Copy data from ADC buffer to HWA buffer */
        rangeProcHWA_ConfigEDMA_DataIn(rangeProcObj,    DPParams, pHwConfig);

        /* Range FFT configuration in HWA */
        retVal = rangeProcHWA_ConfigHWA(rangeProcObj,
            destChanPing,
            destChanPong
        );
    }
	else if (pHwConfig->hwaCfg.dataInputMode == DPU_RangeProcHWA_InputMode_MAPPED)
    {
        /* Range FFT configuration in HWA */
        retVal = rangeProcHWA_ConfigHWA(rangeProcObj,
                destChanPing,
                destChanPong
        );
    }
    else if (pHwConfig->hwaCfg.dataInputMode == DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM)
    {
        /* Range FFT configuration in HWA */
        retVal = rangeProcHWA_ConfigHWA(rangeProcObj,
                destChanPing,
                destChanPong
        );
    }
    else
    {
        retVal = DPU_RANGEPROCHWA_EINVAL;
    }
    if(retVal < 0)
    {
        goto exit;
    }

    /* EDMA configuration */
    retVal = rangeProcHWA_ConfigEDMA_DataOut_interleave(rangeProcObj,
                                                  DPParams,
                                                  pHwConfig,
                                                  (uint32_t)rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_COMP_PING_OUT],
                                                  (uint32_t)rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_COMP_PONG_OUT],
                                                  (uint32_t)rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_FFT_PING_OUT],
                                                  (uint32_t)rangeProcObj->hwaMemBankAddr[DPU_RANGEHWADDMA_MEM_BANK_FFT_PONG_OUT]);
exit:
    return (retVal);
}


/**
 *  @b Description
 *  @n
 *      rangeProc configuraiton in non-interleaved mode
 *
 *  @param[in]  rangeProcObj                 Pointer to rangeProc object
 *  @param[in]  DPParams                     Pointer to data path common params
 *  @param[in]  pHwConfig                    Pointer to rangeProc hardware resources
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
static int32_t rangeProcHWA_ConfigNonInterleaveMode
(
    rangeProcHWAObj          *rangeProcObj,
    rangeProc_dpParams       *DPParams,
    DPU_RangeProcHWA_HW_Resources *pHwConfig
)
{
    HWA_Handle          hwaHandle;
    int32_t             retVal = 0;
    uint8_t             destChanPing;
    uint8_t             destChanPong;
    uint8_t             edmaChanPing;
    uint8_t             edmaChanPong;

    hwaHandle = rangeProcObj->initParms.hwaHandle;

    edmaChanPing = pHwConfig->edmaOutCfg.u.fmt1.dataOutPing.channel;
    edmaChanPong = pHwConfig->edmaOutCfg.u.fmt1.dataOutPong.channel;

    /* Get HWA destination channel id */
    retVal = HWA_getDMAChanIndex(hwaHandle, edmaChanPing, &destChanPing);
    if (retVal != 0)
    {
        goto exit;
    }
    /* In interleave mode, only edmaOutCfgFmt is supported */
    retVal = HWA_getDMAChanIndex(hwaHandle, edmaChanPong, &destChanPong);
    if (retVal != 0)
    {
        goto exit;
    }

    /* In ADCBuf and HWA memory isolated mode, 
       - copy data from ADCBuf to HWA memory by EDMA 
       - trigger HWA */
    if(pHwConfig->hwaCfg.dataInputMode == DPU_RangeProcHWA_InputMode_ISOLATED)
    {
        /* Copy data from ADC buffer to HWA buffer */
        rangeProcHWA_ConfigEDMA_DataIn(rangeProcObj,    DPParams, pHwConfig);

        /* Range FFT configuration in HWA */
        retVal = rangeProcHWA_ConfigHWA(rangeProcObj,
            destChanPing,
            destChanPong
        );
    }
    else if(pHwConfig->hwaCfg.dataInputMode == DPU_RangeProcHWA_InputMode_MAPPED)
    {
        /* EDMA copy is not needed */

        /* Range FFT configuration in HWA */
        retVal = rangeProcHWA_ConfigHWA(rangeProcObj,
            destChanPing,
            destChanPong
        );
    }
    else if(pHwConfig->hwaCfg.dataInputMode == DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM)
    {
        /* EDMA copy is not needed */

        /* Range FFT configuration in HWA */
        retVal = rangeProcHWA_ConfigHWA(rangeProcObj,
            destChanPing,
            destChanPong
        );
    }
    else
    {
        retVal = DPU_RANGEPROCHWA_EINVAL;
    }
    if(retVal < 0)
    {
        goto exit;
    }

    /* Data output EDMA configuration */
    retVal = rangeProcHWA_ConfigEDMA_DataOut_nonInterleave(rangeProcObj,
                                             DPParams,
                                             pHwConfig);
    if (retVal < 0)
    {
        goto exit;
    }

exit:
    return (retVal);
}

/**
 *  @b Description
 *  @n
 *      Internal function to parse rangeProc configuration and save in internal rangeProc object
 *
 *  @param[in]  rangeProcObj              Pointer to rangeProc object
 *  @param[in]  pConfigIn                 Pointer to rangeProcHWA configuration structure
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
static int32_t rangeProcHWA_ParseConfig
(
    rangeProcHWAObj         *rangeProcObj,
    DPU_RangeProcHWA_Config  *pConfigIn
)
{
    int32_t                 retVal = 0;
    rangeProc_dpParams      *params;
    DPU_RangeProcHWA_StaticConfig   *pStaticCfg;
    rangeProcHWACompressionCfg      *compParams;
    uint32_t                shift, scale;

    /* Get configuration pointers */
    pStaticCfg = &pConfigIn->staticCfg;
    params    = &rangeProcObj->params;
    compParams = &rangeProcObj->compressionCfg;

    /* Save datapath parameters */
    params->numTxAntennas = pStaticCfg->numTxAntennas;
    params->numRxAntennas = pStaticCfg->ADCBufData.dataProperty.numRxAntennas;
    params->numVirtualAntennas = pStaticCfg->numVirtualAntennas;
    params->numChirpsPerChirpEvent = pStaticCfg->ADCBufData.dataProperty.numChirpsPerChirpEvent;
    params->numAdcSamples = pStaticCfg->ADCBufData.dataProperty.numAdcSamples;
    params->numRangeBins = pStaticCfg->numRangeBins;
    params->numFFTBins = pStaticCfg->numFFTBins;


#ifdef SOC_AM273X
    rangeProcObj->isLVDSStreamEnabled = pStaticCfg->isLVDSStreamEnabled;
    rangeProcObj->csirxOffset = pStaticCfg->csirxOffset;
#endif

    if(pStaticCfg->isChirpDataReal){
        params->isReal = 1;
        params->sizeOfInputSample = sizeof(int16_t);
    }
    else{
        params->isReal = 0;
        params->sizeOfInputSample = sizeof(cmplx16ImRe_t);
    }
    params->numChirpsPerFrame = pStaticCfg->numChirpsPerFrame;
    params->numDopplerChirps = pStaticCfg->numChirpsPerFrame/pStaticCfg->numTxAntennas;
    params->fftOutputDivShift = pStaticCfg->rangeFFTtuning.fftOutputDivShift;
    params->numLastButterflyStagesToScale = pStaticCfg->rangeFFTtuning.numLastButterflyStagesToScale;

    /* Save buffers */
    rangeProcObj->ADCdataBuf        = (cmplx16ImRe_t *)pStaticCfg->ADCBufData.data;

    rangeProcObj->radarCubebuf      = (cmplx16ImRe_t *)pConfigIn->hwRes.radarCube.data;

    /* Save interleave mode from ADCBuf configuraiton */
    rangeProcObj->interleave = pStaticCfg->ADCBufData.dataProperty.interleave;

    if((rangeProcObj->interleave ==DPIF_RXCHAN_NON_INTERLEAVE_MODE) &&
        (rangeProcObj->params.numRxAntennas > 1) )
    {
        /* For rangeProcDPU needs rx channel has same offset from one channel to the next channel
           Use first two channel offset to calculate the BIdx for EDMA
         */
        rangeProcObj->rxChanOffset = pStaticCfg->ADCBufData.dataProperty.rxChanOffset[1] - 
                                    pStaticCfg->ADCBufData.dataProperty.rxChanOffset[0];

        /* rxChanOffset should be 16 bytes aligned and should be big enough to hold numAdcSamples */
        if ((rangeProcObj->rxChanOffset < (rangeProcObj->params.numAdcSamples * rangeProcObj->params.sizeOfInputSample)) ||
          ((rangeProcObj->rxChanOffset & 0xF) != 0))
        {
            retVal = DPU_RANGEPROCHWA_EADCBUF_INTF;
            goto exit;
        }
    }

    /* Save RadarCube format */
    if (pConfigIn->hwRes.radarCube.datafmt == DPIF_RADARCUBE_FORMAT_2)
    {
        rangeProcObj->radarCubeLayout = rangeProc_dataLayout_RANGE_DOPPLER_TxAnt_RxAnt;
    }
    else if(pConfigIn->hwRes.radarCube.datafmt == DPIF_RADARCUBE_FORMAT_1)
    {
        rangeProcObj->radarCubeLayout = rangeProc_dataLayout_TxAnt_DOPPLER_RxAnt_RANGE;
    }
    else
    {
        retVal = DPU_RANGEPROCHWA_EINTERNAL;
        goto exit;
    }

    /* Save compression parameters */
    compParams->isEnabled = pStaticCfg->compressionCfg.isEnabled;
    compParams->compressionMethod = pStaticCfg->compressionCfg.compressionMethod;
    if(compParams->compressionMethod == HWA_COMPRESS_METHOD_BFP)
    {
        compParams->inputSamplesPerBlock = pStaticCfg->compressionCfg.rangeBinsPerBlock;
    }
    else
    {
        compParams->inputSamplesPerBlock = (pStaticCfg->compressionCfg.rangeBinsPerBlock)
                                        * (pStaticCfg->compressionCfg.numRxAntennaPerBlock);
    }
    compParams->bytesPerSample = 4; /* Complex FFT */
    
    compParams->inputBytesPerBlock = compParams->bytesPerSample * compParams->inputSamplesPerBlock;
    compParams->numBlocks = (pStaticCfg->ADCBufData.dataProperty.numRxAntennas * pStaticCfg->numRangeBins) /
                                compParams->inputSamplesPerBlock;
    compParams->outputBytesPerBlock = (MATHUTILS_CEILING_POS_FLOAT(compParams->inputBytesPerBlock * pStaticCfg->compressionCfg.compressionRatio / 4.0)) 
                            * 4; /* Word aligned */ 

    compParams->achievedCompressionRatio = (float) compParams->outputBytesPerBlock / (float) compParams->inputBytesPerBlock;
    compParams->outputSamplesPerBlock = compParams->outputBytesPerBlock/compParams->bytesPerSample;
    compParams->rangeBinsPerBlock = pStaticCfg->compressionCfg.rangeBinsPerBlock;
    compParams->rxAntPerBlock = pStaticCfg->compressionCfg.numRxAntennaPerBlock;

    /* Prepare internal hardware resouces = trigger source matchs its  paramset index */
    rangeProcObj->dataInTrigger[0]      = 1U + pConfigIn->hwRes.hwaCfg.paramSetStartIdx;
    rangeProcObj->dataInTrigger[1]      = 6U + pConfigIn->hwRes.hwaCfg.paramSetStartIdx + pStaticCfg->compressionCfg.bfpCompExtraParamSets/2U;
    rangeProcObj->dataOutTrigger[0]     = 0U + pConfigIn->hwRes.hwaCfg.paramSetStartIdx;
    rangeProcObj->dataOutTrigger[1]     = 5U + pConfigIn->hwRes.hwaCfg.paramSetStartIdx + pStaticCfg->compressionCfg.bfpCompExtraParamSets/2U;

    /* Save hardware resources that will be used at runtime */
    rangeProcObj->edmaHandle= pConfigIn->hwRes.edmaHandle;
    rangeProcObj->dataOutSignatureChan = pConfigIn->hwRes.edmaOutCfg.dataOutSignature.channel;
    rangeProcObj->dcRangeSigMean = pConfigIn->hwRes.dcRangeSigMean;
    rangeProcObj->dcRangeSigMeanSize = pConfigIn->hwRes.dcRangeSigMeanSize;
    memcpy((void *)&rangeProcObj->hwaCfg, (void *)&pConfigIn->hwRes.hwaCfg, sizeof(DPU_RangeProcHWA_HwaConfig));

    /* DC Est shift and scale */
    retVal = rangeProcHWADDMA_findDCEstStaticParams(pStaticCfg->ADCBufData.dataProperty.numAdcSamples,
                                                    &scale, &shift);
    if (retVal != 0)
    {
        goto exit;
    }   
    rangeProcObj->dcEstShiftScaleCfg.scale = scale;
    rangeProcObj->dcEstShiftScaleCfg.shift = shift;
    
    /* Interf config */
    retVal = rangeProcHWADDMA_findIntfStatsStaticParams(pStaticCfg->ADCBufData.dataProperty.numAdcSamples, 
                                                        pStaticCfg->intfStatsCfgdB.intfMitgMagSNRdB, 
                                                        &scale, &shift);
    if (retVal != 0)
    {
        goto exit;
    }
    rangeProcObj->intfStatsMagShiftScaleCfg.scale = scale;
    rangeProcObj->intfStatsMagShiftScaleCfg.shift = shift;

    retVal = rangeProcHWADDMA_findIntfStatsStaticParams(pStaticCfg->ADCBufData.dataProperty.numAdcSamples, 
                                                        pStaticCfg->intfStatsCfgdB.intfMitgMagDiffSNRdB, 
                                                        &scale, &shift);
    if (retVal != 0)
    {
        goto exit;
    }
    rangeProcObj->intfStatsMagDiffShiftScaleCfg.scale = scale;
    rangeProcObj->intfStatsMagDiffShiftScaleCfg.shift = shift;

exit:
    return(retVal);
}

/**
 *  @b Description
 *  @n
 *      Internal function to config HWA/EDMA to perform range FFT
 *
 *  @param[in]  rangeProcObj              Pointer to rangeProc object
 *  @param[in]  pHwConfig                 Pointer to rangeProc hardware resources
 *
 *  \ingroup    DPU_RANGEPROC_INTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
static int32_t rangeProcHWA_HardwareConfig
(
    rangeProcHWAObj         *rangeProcObj,
    DPU_RangeProcHWA_HW_Resources *pHwConfig
)
{
    int32_t                 retVal = 0;
    rangeProc_dpParams      *DPParams;
    DPParams    = &rangeProcObj->params;

    if (rangeProcObj->interleave == DPIF_RXCHAN_INTERLEAVE_MODE)
    {
        retVal = rangeProcHWA_ConfigInterleaveMode(rangeProcObj, DPParams, pHwConfig);
        if (retVal != 0)
        {
            goto exit;
        }
    }
    else
    {
        retVal = rangeProcHWA_ConfigNonInterleaveMode(rangeProcObj, DPParams, pHwConfig);
        if (retVal != 0)
        {
            goto exit;
        }
    }
exit:
    return(retVal);
}

/**************************************************************************
 ************************RangeProcHWA External APIs **************************
 **************************************************************************/

/**
 *  @b Description
 *  @n
 *      The function is rangeProc DPU init function. It allocates memory to store
 *  its internal data object and returns a handle if it executes successfully.
 *
 *  @param[in]  initParams              Pointer to DPU init parameters
 *  @param[in]  subframeCounter         subFrame index for dpu initialization
 *  @param[in]  errCode                 Pointer to errCode generates from the API
 *
 *  \ingroup    DPU_RANGEPROC_EXTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - valid rangeProc handle
 *  @retval
 *      Error       - NULL
 */
DPU_RangeProcHWA_Handle DPU_RangeProcHWA_init
(
    DPU_RangeProcHWA_InitParams     *initParams,
    volatile uint8_t            subframeCounter,
    int32_t*                            errCode
)
{
    rangeProcHWAObj     *rangeProcObj = NULL;
    HWA_MemInfo         hwaMemInfo;
    uint8_t             index;
    int32_t             status = SystemP_SUCCESS;

    *errCode = 0;

    if( (initParams == NULL) ||
       (initParams->hwaHandle == NULL) )
    {
        *errCode = DPU_RANGEPROCHWA_EINVAL;
        goto exit;
    }

    rangeProcObj = (rangeProcHWAObj*)&RangeObj[subframeCounter];

    if(rangeProcObj == NULL)
    {
        *errCode = DPU_RANGEPROCHWA_ENOMEM;
        goto exit;
    }

    /* Initialize memory */
    memset((void *)rangeProcObj, 0, sizeof(rangeProcHWAObj));

    memcpy((void *)&rangeProcObj->initParms, initParams, sizeof(DPU_RangeProcHWA_InitParams));

    /* Set HWA bank memory address */
    *errCode =  HWA_getHWAMemInfo(initParams->hwaHandle, &hwaMemInfo);
    if (*errCode < 0)
    {
        goto exit;
    }

    for (index = 0; index < hwaMemInfo.numBanks; index++)
    {
        rangeProcObj->hwaMemBankAddr[index] = hwaMemInfo.baseAddress + index * hwaMemInfo.bankSize;
    }

    /* Create semaphore for EDMA done */
    status = SemaphoreP_constructBinary(&rangeProcObj->edmaDoneSemaHandle, 0);
    if(status != SystemP_SUCCESS)
    {
        *errCode = DPU_RANGEPROCHWA_ESEMA;
        goto exit;
    }

    /* Create semaphore for HWA done */
    status = SemaphoreP_constructBinary(&rangeProcObj->hwaDoneSemaHandle, 0);
    if(status != SystemP_SUCCESS)
    {
        *errCode = DPU_RANGEPROCHWA_ESEMA;
        goto exit;
    }

exit:
    if(*errCode < 0)
    {
        rangeProcObj = (DPU_RangeProcHWA_Handle)NULL;
    }
    else
    {
        /* Fall through */
    }
    return ((DPU_RangeProcHWA_Handle)rangeProcObj);

}


/**
 *  @b Description
 *  @n
 *      The function is rangeProc DPU config function. It saves buffer pointer and configurations 
 *  including system resources and configures HWA and EDMA for runtime range processing.
 *  
 *  @pre    DPU_RangeProcHWA_init() has been called
 *
 *  @param[in]  handle                  rangeProc DPU handle
 *  @param[in]  pConfigIn               Pointer to rangeProc configuration data structure
 *
 *  \ingroup    DPU_RANGEPROC_EXTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
int32_t DPU_RangeProcHWA_config
(
    DPU_RangeProcHWA_Handle  handle,
    DPU_RangeProcHWA_Config  *pConfigIn
)
{
    rangeProcHWAObj                 *rangeProcObj;
    DPU_RangeProcHWA_StaticConfig   *pStaticCfg;
    HWA_Handle                      hwaHandle;
    int32_t                         retVal = 0;

    rangeProcObj = (rangeProcHWAObj *)handle;
    if(rangeProcObj == NULL)
    {
        retVal = DPU_RANGEPROCHWA_EINVAL;
        goto exit;
    }

    /* Get configuration pointers */
    pStaticCfg = &pConfigIn->staticCfg;
    hwaHandle = rangeProcObj->initParms.hwaHandle;

#if DEBUG_CHECK_PARAMS
    /* Validate params */
    if(!pConfigIn ||
      !(pConfigIn->hwRes.edmaHandle) ||
       (pConfigIn->hwRes.hwaCfg.numParamSet != (DPU_RANGEPROCHWA_NUM_HWA_PARAM_SETS_DDMA +
                                                pStaticCfg->compressionCfg.bfpCompExtraParamSets))
      )
    {
        retVal = DPU_RANGEPROCHWA_EINVAL;
        goto exit;
    }

    /* Parameter check: validate Adc data interface configuration
        Support:
            - 1 chirp per chirpEvent
            - Complex 16bit ADC data in IMRE format supported for AM273X+2243 Processing Chain
            - Real only 16bit ADC data supported for AWR294X Processing Chain
     */
#ifdef SOC_AWR294X
    if( (pStaticCfg->ADCBufData.dataProperty.dataFmt != DPIF_DATAFORMAT_REAL16) ||
       (pStaticCfg->ADCBufData.dataProperty.numChirpsPerChirpEvent != 1U) )
    {
        retVal = DPU_RANGEPROCHWA_EADCBUF_INTF;
        goto exit;
    }
#else
    if( ((pStaticCfg->ADCBufData.dataProperty.dataFmt != DPIF_DATAFORMAT_COMPLEX16_IMRE) &&
        (pStaticCfg->ADCBufData.dataProperty.dataFmt != DPIF_DATAFORMAT_REAL16)) ||
       (pStaticCfg->ADCBufData.dataProperty.numChirpsPerChirpEvent != 1U) )
    {
        retVal = DPU_RANGEPROCHWA_EADCBUF_INTF;
        goto exit;
    }
#endif

    /* Parameter check: windowing Size */
    {
        uint16_t expectedWinSize;

        if( pConfigIn->hwRes.hwaCfg.hwaWinSym == HWA_FFT_WINDOW_SYMMETRIC)
        {
            /* Only half of the windowing factor is needed for symmetric window */
            expectedWinSize = ((pStaticCfg->ADCBufData.dataProperty.numAdcSamples + 1U) / 2U ) * sizeof(uint32_t);
        }
        else
        {
            expectedWinSize = pStaticCfg->ADCBufData.dataProperty.numAdcSamples * sizeof(uint32_t);
        }

        if(pStaticCfg->windowSize != expectedWinSize)
        {
            retVal = DPU_RANGEPROCHWA_EWINDOW;
            goto exit;
        }
    }

    if(pConfigIn->hwRes.radarCube.data == NULL)
    {
        retVal = DPU_RANGEPROCHWA_ERADARCUBE_INTF;
        goto exit;
    }

    if(CSL_MEM_IS_NOT_ALIGN(pConfigIn->hwRes.radarCube.data,
#ifdef SUBSYS_MSS
                                 DPU_RANGEPROCHWA_RADARCUBE_BYTE_ALIGNMENT_R5F))
#else
                                 DPU_RANGEPROCHWA_RADARCUBE_BYTE_ALIGNMENT_DSP))
#endif
    {
        retVal = DPU_RANGEPROCHWA_ERADARCUBE_INTF;
        goto exit;
    }

    /* Refer to radar cube definition for FORMAT_x , the following are the only supported formats
        Following assumption is made upon radar cube FORMAT_x definition 
           1. data type is complex in cmplx16ImRe_t format only
           2. It is always Format 2 for DDMA (interleaved)
     */
    if( (pConfigIn->hwRes.radarCube.datafmt != DPIF_RADARCUBE_FORMAT_2) )
    {
        retVal = DPU_RANGEPROCHWA_ERADARCUBE_INTF;
        goto exit;
    }

    /* Not supported input & output format combination */
    if ((pStaticCfg->ADCBufData.dataProperty.interleave == DPIF_RXCHAN_INTERLEAVE_MODE) &&
         (pConfigIn->hwRes.radarCube.datafmt == DPIF_RADARCUBE_FORMAT_1) )
    {
        retVal = DPU_RANGEPROCHWA_ENOTIMPL;
        goto exit;
    }
    if (pStaticCfg->ADCBufData.dataProperty.numRxAntennas == 3U)
    {
        retVal = DPU_RANGEPROCHWA_ENOTIMPL;
        goto exit;
    }

    /* Parameter check: Num butterfly stages to scale */
    if (pStaticCfg->rangeFFTtuning.numLastButterflyStagesToScale > mathUtils_ceilLog2(pStaticCfg->numRangeBins))
    {
        retVal = DPU_RANGEPROCHWA_EBUTTERFLYSCALE;
        goto exit;
    }
#endif

    retVal = rangeProcHWA_ParseConfig(rangeProcObj, pConfigIn);
    if (retVal < 0)
    {
        goto exit;
    }

    /* Parameter check: radarcube buffer Size */
    if (pConfigIn->hwRes.radarCube.dataSize != (pStaticCfg->numRangeBins* sizeof(cmplx16ImRe_t) *
                                      pStaticCfg->numChirpsPerFrame *
                                      pStaticCfg->ADCBufData.dataProperty.numRxAntennas
                                      * rangeProcObj->compressionCfg.achievedCompressionRatio) )
    {
        retVal = DPU_RANGEPROCHWA_ERADARCUBE_INTF;
        goto exit;
    }

    /* Disable the HWA */
    retVal = HWA_enable(hwaHandle, 0);
    if (retVal != 0)
    {
        goto exit;
    }

#if 0
    /* Reset the internal state of the HWA */
    retVal = HWA_reset(hwaHandle);
    if (retVal != 0)
    {
        goto exit;
    }
#endif

    /* Windowing configuraiton in HWA */
    retVal = HWA_configRam(hwaHandle,
                            HWA_RAM_TYPE_WINDOW_RAM,
                            (uint8_t *)pStaticCfg->window,
                            pStaticCfg->windowSize,   /* size in bytes */
                            pConfigIn->hwRes.hwaCfg.hwaWinRamOffset * sizeof(uint32_t));
    if (retVal != 0)
    {
        goto exit;
    }

    /* Clear stats */
    rangeProcObj->numProcess = 0U;

    /* Initial configuration of rangeProc */
    retVal = rangeProcHWA_HardwareConfig(rangeProcObj, &pConfigIn->hwRes);

exit:
    return retVal;
}

/**
 *  @b Description
 *  @n
 *      The function is rangeProc DPU process function. It allocates memory to store
 *  its internal data object and returns a handle if it executes successfully.
 *
 *  @pre    DPU_RangeProcHWA_init() has been called
 *
 *  @param[in]  handle                  rangeProc DPU handle
 *  @param[in]  outParams               DPU output parameters
 *
 *  \ingroup    DPU_RANGEPROC_EXTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
int32_t DPU_RangeProcHWA_process
(
    DPU_RangeProcHWA_Handle     handle,
    DPU_RangeProcHWA_OutParams  *outParams
)
{
    rangeProcHWAObj     *rangeProcObj;
    int32_t             retVal = 0;

    rangeProcObj = (rangeProcHWAObj *)handle;
    if ((rangeProcObj == NULL) ||
        (outParams == NULL))
    {
        retVal = DPU_RANGEPROCHWA_EINVAL;
        goto exit;
    }

    /* Set inProgress state */
    rangeProcObj->inProgress = true;
    outParams->endOfChirp = false;

    /**********************************************/
    /* WAIT FOR HWA NUMLOOPS INTERRUPT            */
    /**********************************************/
    /* wait for the all paramSets done interrupt */
    SemaphoreP_pend(&rangeProcObj->hwaDoneSemaHandle, SystemP_WAIT_FOREVER);

    /**********************************************/
    /* WAIT FOR EDMA INTERRUPT                    */
    /**********************************************/
    SemaphoreP_pend(&rangeProcObj->edmaDoneSemaHandle, SystemP_WAIT_FOREVER);

    /* Range FFT is done, disable Done interrupt */
    HWA_disableDoneInterrupt(rangeProcObj->initParms.hwaHandle, 0);

    /* Disable the HWA */
    retVal = HWA_enable(rangeProcObj->initParms.hwaHandle, 0);
    if (retVal != 0)
    {
        goto exit;
    }
    /* Update stats and output parameters */
    rangeProcObj->numProcess++;

    /* Following stats is not available for rangeProcHWA */
    outParams->stats.processingTime = 0;
    outParams->stats.waitTime= 0;

    outParams->endOfChirp = true;

    /* Clear inProgress state */
    rangeProcObj->inProgress = false;

exit:

    return retVal;
}


/**
 *  @b Description
 *  @n
 *      The function is rangeProc DPU control function. 
 *
 *  @pre    DPU_RangeProcHWA_init() has been called
 *
 *  @param[in]  handle           rangeProc DPU handle
 *  @param[in]  cmd              rangeProc DPU control command
 *  @param[in]  arg              rangeProc DPU control argument pointer
 *  @param[in]  argSize          rangeProc DPU control argument size
 *
 *  \ingroup    DPU_RANGEPROC_EXTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
int32_t DPU_RangeProcHWA_control
(
    DPU_RangeProcHWA_Handle handle,
    DPU_RangeProcHWA_Cmd    cmd,
    void*                   arg,
    uint32_t                argSize
)
{
    int32_t             retVal = 0;
    rangeProcHWAObj     *rangeProcObj;

    /* Get rangeProc data object */
    rangeProcObj = (rangeProcHWAObj *)handle;

    /* Sanity check */
    if (rangeProcObj == NULL)
    {
        retVal = DPU_RANGEPROCHWA_EINVAL;
        goto exit;
    }

    /* Check if control() is called during processing time */
    if(rangeProcObj->inProgress == true)
    {
        retVal = DPU_RANGEPROCHWA_EINPROGRESS;
        goto exit;
    }

    /* Control command handling */
    switch(cmd)
    {
        case DPU_RangeProcHWA_Cmd_triggerProc:
            /* Trigger rangeProc in HWA */
            retVal = rangeProcHWA_TriggerHWA( rangeProcObj);
            if(retVal != 0)
            {
                goto exit;
            }
        break;

        default:
            retVal = DPU_RANGEPROCHWA_ECMD;
            break;
    }
exit:
    return (retVal);
}


/**
 *  @b Description
 *  @n
 *      The function is rangeProc DPU deinit function. It frees the resources used for the DPU.
 *
 *  @pre    DPU_RangeProcHWA_init() has been called
 *
 *  @param[in]  handle           rangeProc DPU handle
 *
 *  \ingroup    DPU_RANGEPROC_EXTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - 0
 *  @retval
 *      Error       - <0
 */
int32_t DPU_RangeProcHWA_deinit
(
    DPU_RangeProcHWA_Handle     handle
)
{
    rangeProcHWAObj     *rangeProcObj;
    int32_t             retVal = 0;

    /* Sanity Check */
    rangeProcObj = (rangeProcHWAObj *)handle;
    if(rangeProcObj == NULL)
    {
        retVal = DPU_RANGEPROCHWA_EINVAL;
        goto exit;
    }

    /* Delete Semaphores */
    SemaphoreP_destruct(&rangeProcObj->edmaDoneSemaHandle);
    SemaphoreP_destruct(&rangeProcObj->hwaDoneSemaHandle);
exit:

    return (retVal);
}
