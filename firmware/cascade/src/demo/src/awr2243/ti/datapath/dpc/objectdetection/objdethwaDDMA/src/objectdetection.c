/*
 *   @file  objectdetection.c
 *
 *   @brief
 *      Object Detection DPC implementation.
 *
 *  \par
 *  NOTE:
 *      (C) Copyright 2017 - 2021 Texas Instruments, Inc.
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
#include <stdint.h>
#include <string.h>
#include <stdio.h>

#define DBG_DPC_OBJDET

/* MCU+SDK include files */
#include <kernel/dpl/HeapP.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/CycleCounterP.h>
#include <kernel/dpl/CacheP.h>
#include <kernel/dpl/AddrTranslateP.h>
#ifdef SOC_AWR294X
#include <drivers/hw_include/cslr_soc.h>
#endif
/* mmWave SDK Include Files: */
#include <ti/common/syscommon.h>
#include <ti/utils/mathutils/mathutils.h>
#include <ti/control/dpm/dpm.h>

/* DSP Mathlib include files */
#include <ti/mathlib/src/cossp/c66/cossp.h>
#include <ti/dsplib/src/DSPF_sp_dotp_cplx/DSPF_sp_dotp_cplx.h>

#define QVALUE_NOISE          (11U)
#define QVALUE_SIGNAL         (11U)
#define DOUBLEWORD_ALIGNED    (8U)

#ifdef SUBSYS_DSS

/* C66x mathlib */
/* Suppress the mathlib.h warnings
 *  #48-D: incompatible redefinition of macro "TRUE"
 *  #48-D: incompatible redefinition of macro "FALSE"
 */
#pragma diag_push
#pragma diag_suppress 48
#include <ti/mathlib/mathlib.h>
#pragma diag_pop
#endif

 /** @addtogroup DPC_OBJDET_IOCTL__INTERNAL_DEFINITIONS
  @{ */

/*! This is supplied at command line when application builds this file. This file
 * is owned by the application and contains all resource partitioning, an
 * application may include more than one DPC and also use resources outside of DPCs.
 * The resource definitions used by this object detection DPC are prefixed by DPC_OBJDET_ */
#include APP_RESOURCE_FILE

/* Obj Det instance etc */
#include <ti/datapath/dpc/objectdetection/objdethwaDDMA/include/objectdetectioninternal.h>
#include <ti/datapath/dpc/objectdetection/objdethwaDDMA/objectdetection.h>

#if defined(SOC_AM273X) && defined(LVDS_STREAM)
#include <ti/demo/am273x/mmw/dss/mmw_lvds_stream.h>
#include <ti/demo/am273x/mmw/dss/dssgenerated/ti_drivers_open_close.h>
#endif

#ifdef DBG_DPC_OBJDET
ObjDetObj     *gObjDetObj;
#endif


/*! Radar cube data buffer alignment in bytes. */
#ifdef SUBSYS_MSS
#define DPC_OBJDET_RADAR_CUBE_DATABUF_BYTE_ALIGNMENT      DPU_RANGEPROCHWA_RADARCUBE_BYTE_ALIGNMENT_R5F
#else
#define DPC_OBJDET_RADAR_CUBE_DATABUF_BYTE_ALIGNMENT      DPU_RANGEPROCHWA_RADARCUBE_BYTE_ALIGNMENT_DSP
#endif

#if defined(SOC_AM273X) && defined(LVDS_STREAM)
#define LVDS_STREAM_PING_PONG_OFFSET   (uint32_t)(0x4000U)
#endif

/*! Macro to find the ceiling of a positive float value */
#define MATHUTILS_CEILING_POS_FLOAT(x) ((x-(uint32_t)(x)) > 0 ? (uint32_t)(x+1) : (uint32_t)(x))

/*! Detection matrix alignment is declared by CFAR dpu, we size to
 *  the max of this and CPU alignment for accessing detection matrix
 *  it is exported out of DPC in processing result so assume CPU may access
 *  it for post-DPC processing. Note currently the CFAR alignment is the same as
 *  CPU alignment so this max is redundant but it is more to illustrate the
 *  generality of alignments should be done.
 */
#define DPC_OBJDET_DET_MATRIX_DATABUF_BYTE_ALIGNMENT       (CSL_MAX(sizeof(uint16_t), \
                                                                DPU_DOPPLER_DET_MATRIX_BYTE_ALIGNMENT))

/*! Point cloud cartesian byte alignment common define used temporarily in next define */
#ifdef SUBSYS_MSS
#define DPU_AOAPROCHWA_POINT_CLOUD_CARTESIAN_BYTE_ALIGNMENT  \
        DPU_AOAPROCHWA_POINT_CLOUD_CARTESIAN_BYTE_ALIGNMENT_R5F
#else
#define DPU_AOAPROCHWA_POINT_CLOUD_CARTESIAN_BYTE_ALIGNMENT  \
        DPU_AOAPROCHWA_POINT_CLOUD_CARTESIAN_BYTE_ALIGNMENT_DSP
#endif

/*! Point cloud cartesian alignment is declared by AoA dpu, we size to
 *  the max of this and CPU alignment for accessing this as it is exported out as result of
 *  processing and so may be accessed by the CPU during post-DPC processing.
 *  Note currently the AoA alignment is the same as CPU alignment so this max is
 *  redundant but it is more to illustrate the generality of alignments should be done.
 */
#define DPC_OBJDET_POINT_CLOUD_CARTESIAN_BYTE_ALIGNMENT       (CSL_MAX(DPU_AOAPROCHWA_POINT_CLOUD_CARTESIAN_BYTE_ALIGNMENT, \
                                                                   DPIF_POINT_CLOUD_CARTESIAN_CPU_BYTE_ALIGNMENT))

/*! Point cloud side info byte alignment common define used temporarily in next define */
#ifdef SUBSYS_MSS
#define DPU_AOAPROCHWA_POINT_CLOUD_SIDE_INFO_BYTE_ALIGNMENT  \
        DPU_AOAPROCHWA_POINT_CLOUD_SIDE_INFO_BYTE_ALIGNMENT_R5F
#else
#define DPU_AOAPROCHWA_POINT_CLOUD_SIDE_INFO_BYTE_ALIGNMENT  \
        DPU_AOAPROCHWA_POINT_CLOUD_SIDE_INFO_BYTE_ALIGNMENT_DSP
#endif

/*! Point cloud side info alignment is declared by AoA dpu, we size to
 *  the max of this and CPU alignment for accessing this as it is exported out as result of
 *  processing and so may be accessed by the CPU during post-DPC processing.
 *  Note currently the AoA alignment is the same as CPU alignment so this max is
 *  redundant but it is more to illustrate the generality of alignments should be done.
 */
#define DPC_OBJDET_POINT_CLOUD_SIDE_INFO_BYTE_ALIGNMENT       (CSL_MAX(DPU_AOAPROCHWA_POINT_CLOUD_SIDE_INFO_BYTE_ALIGNMENT, \
                                                                   DPIF_POINT_CLOUD_SIDE_INFO_CPU_BYTE_ALIGNMENT))

/*! AoA DPU  azimuth static heat map byte alignment common define used temporarily in next define */
#ifdef SUBSYS_MSS
#define DPU_AOAPROCHWA_AZIMUTH_STATIC_HEAT_MAP_BYTE_ALIGNMENT  \
        DPU_AOAPROCHWA_AZIMUTH_STATIC_HEAT_MAP_BYTE_ALIGNMENT_R5F
#else
#define DPU_AOAPROCHWA_AZIMUTH_STATIC_HEAT_MAP_BYTE_ALIGNMENT  \
        DPU_AOAPROCHWA_AZIMUTH_STATIC_HEAT_MAP_BYTE_ALIGNMENT_DSP
#endif

/*! Azimuth static heat map alignment is declared by AoA dpu, we size to
 *  the max of this and CPU alignment for accessing this as it is exported out as result of
 *  processing and so may be accessed by the CPU during post-DPC processing.
 */
#define DPC_OBJDET_AZIMUTH_STATIC_HEAT_MAP_BYTE_ALIGNMENT     (CSL_MAX(DPU_AOAPROCHWA_AZIMUTH_STATIC_HEAT_MAP_BYTE_ALIGNMENT, \
                                                                   sizeof(int16_t)))

/*! Elevation angle byte alignment common define used temporarily in next define */
#ifdef SUBSYS_MSS
#define DPU_AOAPROCHWA_DET_OBJ_ELEVATION_ANGLE_BYTE_ALIGNMENT  \
        DPU_AOAPROCHWA_DET_OBJ_ELEVATION_ANGLE_BYTE_ALIGNMENT_R5F
#else
#define DPU_AOAPROCHWA_DET_OBJ_ELEVATION_ANGLE_BYTE_ALIGNMENT  \
        DPU_AOAPROCHWA_DET_OBJ_ELEVATION_ANGLE_BYTE_ALIGNMENT_DSP
#endif

/*! Elevation angle alignment is declared by AoA dpu, we size to
 *  the max of this and CPU alignment for accessing this as it is exported out as result of
 *  processing and so may be accessed by the CPU during post-DPC processing.
 *  Note currently the AoA alignment is the same as CPU alignment so this max is
 *  redundant but it is more to illustrate the generality of alignments should be done.
 */
#define DPC_OBJDET_DET_OBJ_ELEVATION_ANGLE_BYTE_ALIGNMENT     (CSL_MAX(DPU_AOAPROCHWA_DET_OBJ_ELEVATION_ANGLE_BYTE_ALIGNMENT, \
                                                                   sizeof(float)))

#define DPC_OBJDET_HWA_MAX_WINDOW_RAM_SIZE_IN_SAMPLES    (CSL_DSS_HWA_WINDOW_RAM_U_SIZE >> 3)
#define DPC_OBJDET_HWA_NUM_PARAM_SETS                    SOC_HWA_NUM_PARAM_SETS

/******************************************************************************/
/* Local definitions */

#define DPC_USE_SYMMETRIC_WINDOW_RANGE_DPU
#define DPC_USE_SYMMETRIC_WINDOW_DOPPLER_DPU
#define DPC_DPU_RANGEPROC_FFT_WINDOW_TYPE                  MATHUTILS_WIN_HANNING
#define DPC_DPU_RANGEPROC_INTERFMITIG_WINDOW_TYPE          MATHUTILS_WIN_HANNING
#define DPC_DPU_DOPPLERPROC_FFT_WINDOW_TYPE                MATHUTILS_WIN_HANNING

/*! Number of interference mitigation window samples. Used 16 as the size
    instead of 14 because mathUtils generates the first and the last samples
    as 0, which are not useful. */
#define DPC_OBJDET_RANGEPROC_NUM_INTFMITIG_WIN_SIZE_TOTAL       (16U)

/*! Interference mitigation window type */
#define DPC_OBJDET_RANGEPROC_INTERFMITIG_WINDOW_TYPE            MATHUTILS_WIN_HANNING

/*! Q Format of interference mitigation window */
#define DPC_OBJDET_QFORMAT_RANGEPROC_INTERFMITIG_WINDOW         (5U)
/* User defined heap memory and handle */
#define OBJECTDETECTION_HEAP_MEM_SIZE  (sizeof(ObjDetObj)+1024U)

/* Number of Azim FFT Bins */
#ifndef CASCADE_EVM
#define OBJECTDETECTION_NUM_AZIM_FFT_BINS (32U)
#else
#define OBJECTDETECTION_NUM_AZIM_FFT_BINS (48U)
#endif

static uint8_t gObjectDetectionHeapMem[OBJECTDETECTION_HEAP_MEM_SIZE] __attribute__((aligned(HeapP_BYTE_ALIGNMENT)));
static HeapP_Object gObjectDetectionHeapObj;

/* Compact point cloud for given DPC execute result */
DPC_ObjectDetection_PointCloudRadialCompact *gPointCloudRadialCompact;

#define OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE 32U
#ifdef SOC_AWR294X
#define OBJECTDETHWA_TIMING_CPU_CLK_FREQ_KHZ    360000
#elif SOC_AM273X
#define OBJECTDETHWA_TIMING_CPU_CLK_FREQ_KHZ    450000
#endif
// #define OBJECTDETHWA_PRINT_DPC_TIMING_INFO

/* As and when frames come in, these variables will store the actual timestamps.
   As soon as the last frame processing is completed, they will be replaced by
   time values in milliseconds, with respect to the start of the first frame of
   the OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE set. */
typedef struct timingInfo
{
    uint32_t frameStartTimes[OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE];
    uint32_t rangeEndTimes[OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE];
    uint32_t dopEndTimes[OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE];
    uint32_t aoaStartTimes[OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE];
    uint32_t aoaEndTimes[OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE];
    uint32_t resEndTimes[OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE];
    uint32_t rangeEndCnt, dopEndCnt, aoaStartCnt, aoaEndCnt, frameCnt, resEndCnt;
}timingInfo;

timingInfo gTimingInfo;

/**************************************************************************
 ************************** Local Functions *******************************
 **************************************************************************/
/**
 *  @b Description
 *  @n
 *      Utility function for reseting memory pool.
 *
 *  @param[in]  pool Handle to pool object.
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      none.
 */
static void DPC_ObjDet_MemPoolReset(MemPoolObj *pool)
{
    pool->currAddr = (uintptr_t)pool->cfg.addr;
    pool->maxCurrAddr = pool->currAddr;
}

/**
 *  @b Description
 *  @n
 *      Utility function for setting memory pool to desired address in the pool.
 *      Helps to rewind for example.
 *
 *  @param[in]  pool Handle to pool object.
 *  @param[in]  addr Address to assign to the pool's current address.
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      None
 */
static void DPC_ObjDet_MemPoolSet(MemPoolObj *pool, void *addr)
{
    pool->currAddr = (uintptr_t)addr;
    pool->maxCurrAddr = CSL_MAX(pool->currAddr, pool->maxCurrAddr);
}

/**
 *  @b Description
 *  @n
 *      Utility function for getting memory pool current address.
 *
 *  @param[in]  pool Handle to pool object.
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      pointer to current address of the pool (from which next allocation will
 *      allocate to the desired alignment).
 */
static void *DPC_ObjDet_MemPoolGet(MemPoolObj *pool)
{
    return((void *)pool->currAddr);
}

#if 0 /* may be useful in future */
/**
 *  @b Description
 *  @n
 *      Utility function for getting current memory pool usage.
 *
 *  @param[in]  pool Handle to pool object.
 *
 *  @retval
 *      Amount of pool used in bytes.
 */
static uint32_t DPC_ObjDet_MemPoolGetCurrentUsage(MemPoolObj *pool)
{
    return((uint32_t)(pool->currAddr - (uintptr_t)pool->cfg.addr));
}
#endif

/**
 *  @b Description
 *  @n
 *      Utility function for getting maximum memory pool usage.
 *
 *  @param[in]  pool Handle to pool object.
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      Amount of pool used in bytes.
 */
static uint32_t DPC_ObjDet_MemPoolGetMaxUsage(MemPoolObj *pool)
{
    return((uint32_t)(pool->maxCurrAddr - (uintptr_t)pool->cfg.addr));
}

/**
 *  @b Description
 *  @n
 *      Utility function for allocating from a static memory pool.
 *
 *  @param[in]  pool Handle to pool object.
 *  @param[in]  size Size in bytes to be allocated.
 *  @param[in]  align Alignment in bytes
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      pointer to beginning of allocated block. NULL indicates could not
 *      allocate.
 */
static void *DPC_ObjDet_MemPoolAlloc(MemPoolObj *pool,
                              uint32_t size,
                              uint8_t align)
{
    void *retAddr = NULL;
    uintptr_t addr;

    addr = CSL_MEM_ALIGN(pool->currAddr, align);
    if ((addr + size) <= ((uintptr_t)pool->cfg.addr + pool->cfg.size))
    {
        retAddr = (void *)addr;
        pool->currAddr = addr + size;
        pool->maxCurrAddr = CSL_MAX(pool->currAddr, pool->maxCurrAddr);
    }

    return (retAddr);
}

static DPM_DPCHandle DPC_ObjectDetection_init(
    DPM_Handle dpmHandle,
    DPM_InitCfg *ptrInitCfg,
    int32_t *errCode);

static int32_t DPC_ObjectDetection_execute(
    DPM_DPCHandle handle,
    DPM_Buffer *ptrResult);

static int32_t DPC_ObjectDetection_ioctl(
    DPM_DPCHandle handle,
    uint32_t cmd,
    void *arg,
    uint32_t argLen);

static int32_t DPC_ObjectDetection_start(DPM_DPCHandle handle);
static int32_t DPC_ObjectDetection_stop(DPM_DPCHandle handle);
static int32_t DPC_ObjectDetection_deinit(DPM_DPCHandle handle);
static void DPC_ObjectDetection_frameStart(DPM_DPCHandle handle);
static int32_t DPC_ObjDet_reconfigSubFrame(ObjDetObj *objDetObj, uint8_t subFrameIndx);
/**************************************************************************
 ************************* Global Declarations ****************************
 **************************************************************************/

/** @addtogroup DPC_OBJDET__GLOBAL
 @{ */

/**
 * @brief   Global used to register Object Detection DPC in DPM
 */
DPM_ProcChainCfg gDPC_ObjectDetectionCfg =
    {
        DPC_ObjectDetection_init,      /* Initialization Function:         */
        DPC_ObjectDetection_start,     /* Start Function:                  */
        DPC_ObjectDetection_execute,   /* Execute Function:                */
        DPC_ObjectDetection_ioctl,     /* Configuration Function:          */
        DPC_ObjectDetection_stop,      /* Stop Function:                   */
        DPC_ObjectDetection_deinit,    /* Deinitialization Function:       */
        NULL,                          /* Inject Data Function:            */
        NULL,                          /* Chirp Available Function:        */
        DPC_ObjectDetection_frameStart /* Frame Start Function:            */
};

/**
@}
*/


/**
 *  @b Description
 *  @n
 *      Sends Assert
 *
 *  @retval
 *      Not Applicable.
 */
void _DPC_Objdet_Assert(DPM_Handle handle, int32_t expression,
                        const char *file, int32_t line)
{
    DPM_DPCAssert fault;

    if (!expression)
    {
        fault.lineNum = (uint32_t)line;
        fault.arg0 = 0U;
        fault.arg1 = 0U;
        strncpy(fault.fileName, file, (DPM_MAX_FILE_NAME_LEN - 1));

        /* Report the fault to the DPM entities */
        DPM_ioctl(handle,
                  DPM_CMD_DPC_ASSERT,
                  (void *)&fault,
                  sizeof(DPM_DPCAssert));
    }
}

/**
 *  @b Description
 *  @n
 *      DPC frame start function registered with DPM. This is invoked on reception
 *      of the frame start ISR from the RF front-end. This API is also invoked
 *      when application issues @ref DPC_OBJDET_IOCTL__TRIGGER_FRAME to simulate
 *      a frame trigger (e.g for unit testing purpose).
 *
 *  @param[in]  handle DPM's DPC handle
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      Not applicable
 */
static void DPC_ObjectDetection_frameStart(DPM_DPCHandle handle)
{
    ObjDetObj *objDetObj = (ObjDetObj *)handle;

    objDetObj->stats.frameStartTimeStamp = CycleCounterP_getCount32();
    gTimingInfo.frameStartTimes[gTimingInfo.frameCnt % OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE] = CycleCounterP_getCount32();
    gTimingInfo.frameCnt++;

    DebugP_logInfo("ObjDet DPC: Frame Start, frameIndx = %d, subFrameIndx = %d\n",
                   objDetObj->stats.frameStartIntCounter, objDetObj->subFrameIndx);

    /* Perform DPM_notifyExecute only if the previous frame's/subFrame's result has been exported,
       i.e., the result exported ioctl has been received from the MSS. Otherwise,
       DPM_notifyExecute will be performed in the result exported ioctl */
    if (objDetObj->numTimesResultExported == objDetObj->stats.subframeStartIntCounter)
    {
        DebugP_assert(DPM_notifyExecute(objDetObj->dpmHandle, handle) == 0);
    }
    else
    {
        /* Do Nothing */
    }

    /* Increment interrupt counter for debugging, sync, and reporting purpose */
    if (objDetObj->subFrameIndx == 0)
    {
        objDetObj->stats.frameStartIntCounter++;
    }

    objDetObj->stats.subframeStartIntCounter++;
}

/**
 *  @b Description
 *  @n
 *      Computes the length of window to generate for range DPU.
 *
 *  @param[in]  cfg Range DPU configuration
 *
 *  @retval   Length of window to generate
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static uint32_t DPC_ObjDet_GetRangeWinGenLen(DPU_RangeProcHWA_Config *cfg)
{
    uint16_t numAdcSamples;
    uint32_t winGenLen;

    numAdcSamples = cfg->staticCfg.ADCBufData.dataProperty.numAdcSamples;

#ifdef DPC_USE_SYMMETRIC_WINDOW_RANGE_DPU
    winGenLen = (numAdcSamples + 1) / 2;
#else
    winGenLen = numAdcSamples;
#endif
    return (winGenLen);
}

#define DPC_OBJDET_QFORMAT_RANGE_FFT 17
#define DPC_OBJDET_QFORMAT_DOPPLER_FFT 17

/**
 *  @b Description
 *  @n
 *      Generate the range DPU window using mathutils API.
 *
 *  @param[in]  cfg Range DPU configuration, output window is generated in window
 *                  pointer in the staticCfg of this.
 *
 *  @retval   None
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static void DPC_ObjDet_GenRangeWindow(DPU_RangeProcHWA_Config *cfg)
{

    /* Symmetric window */
    uint32_t interfMitigWindow[DPC_OBJDET_RANGEPROC_NUM_INTFMITIG_WIN_SIZE_TOTAL >> 1];
    uint8_t idx;

    mathUtils_genWindow((uint32_t *)interfMitigWindow,
                        DPC_OBJDET_RANGEPROC_NUM_INTFMITIG_WIN_SIZE_TOTAL,
                        DPC_OBJDET_RANGEPROC_NUM_INTFMITIG_WIN_SIZE_TOTAL >> 1,
                        DPC_DPU_RANGEPROC_INTERFMITIG_WINDOW_TYPE,
                        DPC_OBJDET_QFORMAT_RANGEPROC_INTERFMITIG_WINDOW);

    /* Only 5 win samples are supported by the HWA */
    for (idx = 0; idx < DPU_RANGEPROCHWADDMA_NUM_INTFMITIG_WIN_HWACOMMONCFG_SIZE; idx++)
    {
        cfg->hwRes.hwaCfg.hwaInterfMitigWindow[DPU_RANGEPROCHWADDMA_NUM_INTFMITIG_WIN_HWACOMMONCFG_SIZE - 1 - idx] =
            (uint8_t)interfMitigWindow[(DPC_OBJDET_RANGEPROC_NUM_INTFMITIG_WIN_SIZE_TOTAL >> 1) - 2 - idx];
    }

    /* Range FFT window */
    mathUtils_genWindow((uint32_t *)cfg->staticCfg.window,
                        cfg->staticCfg.ADCBufData.dataProperty.numAdcSamples,
                        DPC_ObjDet_GetRangeWinGenLen(cfg),
                        DPC_DPU_RANGEPROC_FFT_WINDOW_TYPE,
                        DPC_OBJDET_QFORMAT_RANGE_FFT);
}

/**
 *  @b Description
 *  @n
 *      Computes the length of window to generate for doppler DPU.
 *
 *  @param[in]  cfg Doppler DPU configuration
 *
 *  @retval   Length of window to generate
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static uint32_t DPC_ObjDet_GetDopplerWinGenLen(DPU_DopplerProcHWA_Config *cfg)
{
    uint16_t numDopplerChirps;
    uint32_t winGenLen;

    numDopplerChirps = cfg->staticCfg.numChirps;

#ifdef DPC_USE_SYMMETRIC_WINDOW_DOPPLER_DPU
    winGenLen = (numDopplerChirps + 1) / 2;
#else
    winGenLen = numDopplerChirps;
#endif
    return (winGenLen);
}

/**
 *  @b Description
 *  @n
 *      Generate the doppler DPU window using mathutils API.
 *
 *  @param[in]  cfg Doppler DPU configuration, output window is generated in window
 *                  pointer embedded in this configuration.
 *
 *  @retval   winType window type, see mathutils.h
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static uint32_t DPC_ObjDet_GenDopplerWindow(DPU_DopplerProcHWA_Config *cfg)
{
    uint32_t winType;

    /* For too small window, force rectangular window to avoid loss of information
     * due to small window values (e.g. hanning has first and last coefficients 0) */
    if (cfg->staticCfg.numChirps <= 4)
    {
        winType = MATHUTILS_WIN_RECT;
    }
    else
    {
        winType = DPC_DPU_DOPPLERPROC_FFT_WINDOW_TYPE;
    }

    mathUtils_genWindow((uint32_t *)cfg->hwRes.hwaCfg.window,
                        cfg->staticCfg.numChirps,
                        DPC_ObjDet_GetDopplerWinGenLen(cfg),
                        winType,
                        DPC_OBJDET_QFORMAT_DOPPLER_FFT);

    return (winType);
}

#if 0
/**
 *  @b Description
 *  @n
 *     Function transfers antenna geometry definition from the common area which holds all
 *     antennas, to the area per subframe according to subframe antenna usage (antena order
 *     and number of used antennas)
 *
 *  @param[in]  staticCfg Static configuration of the sub-frame
 *  @param[in]  antDef Full antenna geometry definition
 *
 *  @retval   None
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static void DPC_ObjDet_GetAntGeometryDef(DPC_ObjectDetection_StaticCfg *staticCfg,
                                         ANTDEF_AntGeometry *antDef)
{
    uint32_t tx, rx, numTxAnt, numRxAnt;
    uint8_t *txAntOrder, *rxAntOrder;

    numTxAnt = staticCfg->numTxAntennas;
    numRxAnt = staticCfg->ADCBufData.dataProperty.numRxAntennas;
    txAntOrder = staticCfg->txAntOrder;
    rxAntOrder = staticCfg->rxAntOrder;

    for(tx = 0; tx < numTxAnt; tx++)
    {
        staticCfg->antDef.txAnt[tx] = antDef->txAnt[txAntOrder[tx]];
    }
    for(rx = 0; rx < numRxAnt; rx++)
    {
        staticCfg->antDef.rxAnt[rx] = antDef->rxAnt[rxAntOrder[rx]];
    }
}
#endif

int32_t DPC_ObjDet_computeRangeAndDoppler(SubFrameObj *subFrmObj,
                                          DetObjParams *detObjList,
                                          DPIF_PointCloudCartesian *objOut,
                                          uint32_t numObjOut)
{

    uint32_t rangeStep, dopplerStep, objIdx, numDopplerBins;
    int32_t dopIdx;
    int32_t retVal = 0;

    if ((subFrmObj == NULL) || (detObjList == NULL) || (numObjOut > DPC_OBJDET_MAX_NUM_OBJECTS))
    {
        retVal = DPC_OBJECTDETECTION_NUMOBJ_EXCEED_MAX_ERR;
        goto exit;
    }
    rangeStep = subFrmObj->staticCfg.rangeStep;
    dopplerStep = subFrmObj->staticCfg.dopplerStep;
    numDopplerBins = subFrmObj->staticCfg.numDopplerBins;

    for (objIdx = 0; objIdx < numObjOut; objIdx++)
    {

        /* Range */
        objOut[objIdx].x = rangeStep * detObjList[objIdx].rangeIdx;

        /* Velocity */
        if (detObjList[objIdx].dopIdxActual > numDopplerBins / 2)
        {
            dopIdx = detObjList[objIdx].dopIdxActual - numDopplerBins;
        }
        else
        {
            dopIdx = detObjList[objIdx].dopIdxActual;
        }

        objOut[objIdx].velocity = dopIdx * dopplerStep;
    }

exit:
    return retVal;
}

/**
 *  @b Description
 *  @n
 *     Function performs quadratic interpolation around a peak
 *
 *  @param[in]  y A Three sample array ([y0,y1,y2]) where
 *              (y1 > y2) and (y1 > y0)
 *
 *  @retval   location of the interpolated peak, relative to y1.
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static inline float DPC_ObjDet_quadInterpAroundPeak(const uint32_t * restrict y)
{

    float ym1, y0, yp1;
    float thetapk; //, yOut;

    ym1 = (float) y[0]; /* y(peak-1) */
    y0  = (float) y[1]; /* y(peak) */
    yp1 = (float) y[2]; /* y(peak+1) */

    thetapk = divsp((yp1 - ym1), (2 * (2 * y0 - yp1 - ym1)));
    /* yOut = y0 + (((yp1 - ym1) / 4) * thetapk); */

    return thetapk;

}


/**
 *  @b Description
 *  @n
 *      Allocates Shawdow paramset
 */
static void allocateEDMAShadowChannel(EDMA_Handle edmaHandle, uint32_t *param)
{
    int32_t             testStatus = SystemP_SUCCESS;
    EDMA_Config        *config;
    EDMA_Object        *object;

    config = (EDMA_Config *) edmaHandle;
    object = config->object;

    if((object->allocResource.paramSet[*param/32] & (1U << *param%32)) != (1U << *param%32))
    {
        testStatus = EDMA_allocParam(edmaHandle, param);
        DebugP_assert(testStatus == SystemP_SUCCESS);
    }

    return;
}

/**
 *  @b Description
 *  @n
 *     Function calls EDMA param, channel, tcc allocation.
 *     DDMA Datapath assumes paramsetNumber = channelNumber = TCC
 *
 *  @param[in]  handle   EDMA handle
 *  @param[in]  chNum    DMA channel number
 *  @param[in]  shadowParamId    DMA shadow paramId
 *  @param[in]  eventQueue    Event queue num
 *  @param[out]  chanCfg    Stores channel configuration
 *  @retval   None
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
void DPC_ObjDet_EDMAChannelConfigAssist(EDMA_Handle handle, uint32_t chNum, uint32_t shadowParam, uint32_t eventQueue, DPEDMA_ChanCfg *chanCfg)
{

    DebugP_assert(chanCfg != NULL);

    DPEDMA_allocateEDMAChannel(handle, &chNum, &chNum, &chNum);

    chanCfg->channel = chNum;
    chanCfg->tcc = chNum;
    chanCfg->paramId = chNum;

    chanCfg->shadowPramId = shadowParam;

    allocateEDMAShadowChannel(handle, &shadowParam);

    chanCfg->eventQueue = eventQueue;

    return;

}

/**
 *  @b Description
 *  @n
 *     Function checks if the same object is present
 *     in both the rangeCFAR detected object list and the
 *     dopplerProc detected object list (also called doppler
 *     list)
 *
 *  @param[in]  rangeIdx  range index of the object (from the doppler list)
 *  @param[in]  dopIdx    doppler index of the object (from the doppler list)
 *  @param[in]  rangeCfarList list of rangeCFAR detected objects.
 *  @param[in]  numObjToSearch number of Objects to search.
 *  @retval   boolean indicating presence (true) or absence (false).
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static int32_t isObjInRangeAndDopplerList(uint32_t rangeIdx,
                                            uint32_t dopIdx,
                                            RangeCfarListObj *rangeCfarList,
                                            uint32_t numObjToSearch)
{
    int32_t idx;
    for (idx = 0; idx < numObjToSearch; idx++)
    {
        if (rangeCfarList[idx].rangeIdx == rangeIdx)
        {
            if (rangeCfarList[idx].dopIdx == dopIdx)
            {
                return 1;
            }
        }
    }

    return 0;
}

/**
 *  This routine calculates the dot product of 2 single-precision complex
 *  float vectors. The even numbered locations hold the real parts of the
 *  complex numbers while the odd numbered locations contain the imaginary
 *  portions. It is an exact copy of the DSPF_sp_dotp_cmplx function from
 *  the DSPLIB.
 *
 *         @param x   Pointer to array holding the first floating-point vector
 *         @param y   Pointer to array holding the second floating-point vector
 *         @param nx  Number of values in the x and y vectors
 *         @param re  Pointer to the location storing the real part of the result
 *         @param im  Pointer to the location storing the imaginary part of the result
 *
 * @par Assumptions:
 *   Loop counter must be multiple of 4 and > 0. <BR>
 *   The x and y arrays must be double-word aligned. <BR>
 *
 *
 */
static inline void dotpCmplxf(const float * restrict x, const float * restrict y, int nx,
                       float * restrict re, float * restrict im)
{
    int i;
    __float2_t x0_im_re, y0_im_re, result0 = 0;
    __float2_t x1_im_re, y1_im_re, result1 = 0;
    __float2_t x2_im_re, y2_im_re, result2 = 0;
    __float2_t x3_im_re, y3_im_re, result3 = 0;
    __float2_t result;

    _nassert(nx % 4 == 0);
    _nassert(nx > 0);
    _nassert((int)x % 8 == 0);
    _nassert((int)y % 8 == 0);

    for(i = 0; i < 2 * nx; i += 8)
    {
        /* load 4 sets of input data */
        x0_im_re = _amem8_f2((void*)&x[i]);
        y0_im_re = _amem8_f2((void*)&y[i]);

        x1_im_re = _amem8_f2((void*)&x[i+2]);
        y1_im_re = _amem8_f2((void*)&y[i+2]);

        x2_im_re = _amem8_f2((void*)&x[i+4]);
        y2_im_re = _amem8_f2((void*)&y[i+4]);

        x3_im_re = _amem8_f2((void*)&x[i+6]);
        y3_im_re = _amem8_f2((void*)&y[i+6]);

        /* calculate 4 running sums */
        result0 = _daddsp(_complex_mpysp(x0_im_re, y0_im_re), result0);
        result1 = _daddsp(_complex_mpysp(x1_im_re, y1_im_re), result1);
        result2 = _daddsp(_complex_mpysp(x2_im_re, y2_im_re), result2);
        result3 = _daddsp(_complex_mpysp(x3_im_re, y3_im_re), result3);
    }

    result = _daddsp(_daddsp(result0,result1),_daddsp(result2,result3));
    *re =  _hif2(result);
    *im =  _lof2(result);
}

/*! @brief  Complex data type, natural for C66x complex
 * multiplication instructions. */
typedef struct cmplxfImRe_t_
{
    float imag; /*!< @brief imaginary part */
    float real; /*!< @brief real part */
} cmplxfImRe_t;
/*! @brief  Complex union type, natural for C66x intrinsic
 * instructions. */
typedef union cmplxfUnion_t_
{
	cmplxfImRe_t cmplx;
	float dat[2];
	double ddat;
}cmplxfUnion_t;

/*! @brief  Unsigned round (for floats). */
#define ROUND_UNSIGNED(x) ((x) + 0.5f)
#define AOA_DFT_LEN (128)
/* A simple sin-cos LUT used for the DFT computations in
 * DPC_ObjDet_estimateXYZ */
cmplxfImRe_t dftSinCosTable[AOA_DFT_LEN] __attribute__((aligned(8))) = {
#include "cossintable.c"
};

#ifdef CASCADE_EVM
/* Rearrange Matrix for Virtual Channels for the Azimuth angle estimation */
extern uint8_t arrayToAntMapping[NUM_RXANT * NUM_TXANT_AZIM];
#endif

/**
 *  @b Description
 *  @n
 *     Function estimates XYZ coordinates of objects in the object list
 *
 *  @param[in]  subFrmObj   subframe object
 *  @param[in]  objDetObj   DPC object detection object
 *  @param[in]  detObjList  Detected object list
 *  @param[out] objOut      List with x, y, z coordinates populated for each object
 *  @param[in]  numObjOut   Number of detected objects
 *  @param[out] finalNumObjOut  Number of validated objects
 *
 *  @retval   None
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
int32_t DPC_ObjDet_estimateXYZ(SubFrameObj * restrict subFrmObj,
                               ObjDetObj * restrict objDetObj,
                               const DetObjParams * restrict detObjList,
                               DPIF_PointCloudCartesian * restrict objOut,
                               uint32_t numObjOut,
                               uint32_t * restrict finalNumObjOut)
{

    uint16_t azimFFTSize = subFrmObj->dpuCfg.dopplerCfg.staticCfg.numAzimFFTBins;
    const float invAzimFFTSize = divsp(1.0f,(float) azimFFTSize);
    uint32_t objIdx, sampIdx;
    int32_t currLoc;
    float noisedB, signaldB, snrdB;
    float peakIdxOffset, peakIdxFlt;
    int32_t peakLoc;
    float    azimSinPhase;
    cmplxfUnion_t DFTValAzim, DFTValElev, elevOutput;
    float  elevSinPhase, elevCosPhase;
    float rangeStep, range, dopplerStep, x, ySquared, z;
    float wz,peakLocFlt, peakIdxFlt_DFT;
    int32_t dopIdx;
    int32_t isRangeCFAREnabled = subFrmObj->staticCfg.rangeCfarCfg.cfg.isEnabled;
    uint32_t numDopplerBins = subFrmObj->staticCfg.numDopplerBins;
    uint16_t * rangeCfarObjPerDopList = (uint16_t *)subFrmObj->dpuCfg.rangeCfarCfg.res.rangeCfarNumObjPerDopplerBinBuf;
    int16_t valSubBinObj, cfarListStartIdx, isValidObj, ValidObjIdx;
#ifdef SOC_AM273X
	/* If we are using complex baseband, then the last 10% of the range bins
	 * need not be processed, as they correspond to the transistion band of
	 * the image rejection filters. Note that 51/512 ~= 1/10.*/
	uint32_t maxRangeBin = subFrmObj->staticCfg.numRangeBins  - ((subFrmObj->staticCfg.numRangeBins * 51) >> 9)-1;
#else
	uint32_t maxRangeBin = subFrmObj->staticCfg.numRangeBins - 1;
#endif


    /* Alignment is to be done because we use the antenna calib params for
     * multiplication, using optimized DSP routines, which require a 8 byte alignment */
    cmplxfImRe_t samplesCalib[MAX_NUM_VIRT_ANT] __attribute__((aligned(8)));

    int32_t retVal = 0;
    cmplxfImRe_t cosValSinVal[MAX_NUM_AZIM_VIRT_ANT] __attribute__((aligned(8)));;
    double * azimSamplesCalib = (double *)&samplesCalib[0];
    double * elevSamplesCalib = (double *)&samplesCalib[MAX_NUM_AZIM_VIRT_ANT];
    int64_t  * restrict azimSamples;
    int64_t  * restrict elevSamples;
    double samplesFlt2;
    double *antennaCalibParams = (double *)&objDetObj->commonCfg.antennaCalibParams[0];
#ifdef CASCADE_EVM
    cmplx32ImRe_t  rearrangedAzimSamples[MAX_NUM_AZIM_VIRT_ANT] __attribute__((aligned(8)));
#endif
    cmplx32ImRe_t *overlapAntOffset;

    rangeStep = subFrmObj->staticCfg.rangeStep;
    dopplerStep = subFrmObj->staticCfg.dopplerStep;

    /* This variable will index the final object list */
    ValidObjIdx = 0;

    /* Merge Range and Doppler CFAR Lists */
    /* isValidObj will be 1 only if the same object is present in both
     * Range and Doppler CFAR lists. */
    for (objIdx = 0; objIdx < numObjOut; objIdx++)
    {
    	/* Do not bother with objects in the first range bin. */
    	if ((detObjList[objIdx].rangeIdx == 0) ||
    		(detObjList[objIdx].rangeIdx > maxRangeBin))
    	{
    		continue;
    	}



        dopIdx = detObjList[objIdx].dopIdx;
        if(isRangeCFAREnabled == 1)
        {
            if (dopIdx > 0)
            {
                /* Obtain the number of objects for a sub bin in the Range CFAR list by
                * subtracting two consecutive elements from the cummulative distribution */
                valSubBinObj = rangeCfarObjPerDopList[dopIdx] - rangeCfarObjPerDopList[dopIdx - 1];
                cfarListStartIdx = rangeCfarObjPerDopList[dopIdx - 1];
            }
            else
            {
                /* For the 0th sub bin, do not perform a subtraction */
                valSubBinObj = rangeCfarObjPerDopList[dopIdx];
                cfarListStartIdx = 0;
            }

            /* If the sub bin has valid objects in the range CFAR list, check whether the
            * object currently being looked at (from the Doppler CFAR list), is also available
            * in the range CFAR list. cfarListStartIdx tells us where to start searching for in
            * the range CFAR list, and valSubBinObj tells us how many elements to search in.
            * This saves computation time. */
            if (valSubBinObj)
            {
                isValidObj = isObjInRangeAndDopplerList(detObjList[objIdx].rangeIdx,
                                                        detObjList[objIdx].dopIdx,
                                                        (RangeCfarListObj *)&subFrmObj->dpuCfg.rangeCfarCfg.res.rangeCfarList[cfarListStartIdx],
                                                        valSubBinObj);
                if (isValidObj < 0)
                {
                    retVal = DPC_OBJECTDETECTION_RANGE_DOPPLER_NO_MATCH;
                    goto exit;
                }
            }
            else
            {
                isValidObj = 0;
            }
        }
        else
        {
            isValidObj = 1;
        }

        /* If the object has been found in both lists, proceed with angle estimation and coordinate calculation */
        if (isValidObj)
        {
#ifdef CASCADE_EVM
            /* Rearrange the azimuth FFT samples according to the virtual antenna mapping */
            for (sampIdx = 0; sampIdx < MAX_NUM_AZIM_VIRT_ANT; sampIdx ++)
            {
                rearrangedAzimSamples[sampIdx] = detObjList[objIdx].azimSamples[arrayToAntMapping[sampIdx]];
            }
            /* Remove the last 2 antenna data that are overlapping */
            overlapAntOffset = &rearrangedAzimSamples[38];
            memset((void *) overlapAntOffset, 0U, 2U * sizeof(cmplx32ImRe_t));

            azimSamples = (int64_t*) &rearrangedAzimSamples;
#else
        	azimSamples = (int64_t*) detObjList[objIdx].azimSamples;
#endif
			elevSamples = (int64_t*) detObjList[objIdx].elevSamples;

            /* 1. Interpolate around peak to get fractional estimate of Azimuth index */
            peakIdxOffset = DPC_ObjDet_quadInterpAroundPeak(detObjList[objIdx].azimPeakSamples);

            /* Correct peak index with the fractional index*/
            peakIdxFlt = (float)detObjList[objIdx].azimIdx + peakIdxOffset;
            peakIdxFlt_DFT = peakIdxFlt * (invAzimFFTSize * AOA_DFT_LEN);
            peakLoc = ROUND_UNSIGNED(peakIdxFlt_DFT);


            /* 2. Calculate DFT Factors corresponding to wx.
			 *  i.e. calculate \f$\e^{j wx}\f$
			 */
            for (sampIdx = 0; sampIdx < MAX_NUM_AZIM_VIRT_ANT; sampIdx ++)
            {
            	currLoc = (peakLoc*sampIdx)%AOA_DFT_LEN;
    			cosValSinVal[sampIdx] = dftSinCosTable[currLoc];
            }


            /* 3. Azimuth Antenna Calibration:
             * Multiply azimuth samples (azimSamples) of Doppler FFT with antenna calib params (antennaCalibParams)
			 */

            for (sampIdx = 0; sampIdx < MAX_NUM_AZIM_VIRT_ANT; sampIdx++)
            {
            	samplesFlt2 = _dintsp(azimSamples[sampIdx]);
            	azimSamplesCalib[sampIdx] =  _complex_mpysp(samplesFlt2,antennaCalibParams[sampIdx]);

            }

            /* 4. Elevation Antenna calibration
             * Multiply elev samples with antenna calib params  */
            for (sampIdx = MAX_NUM_AZIM_VIRT_ANT; sampIdx < MAX_NUM_VIRT_ANT ; sampIdx++ )
            {
            	samplesFlt2 = _dintsp(elevSamples[sampIdx - MAX_NUM_AZIM_VIRT_ANT]);
            	elevSamplesCalib[sampIdx-MAX_NUM_AZIM_VIRT_ANT] = _complex_mpysp(samplesFlt2, antennaCalibParams[sampIdx]);
            }

            /* 5. Single Bin DFT on the azimuth antennas to estimate phase at peak.
			 *
			  \f[
				X_{azim} (\omega_x) = \sum_{k=0}^{N_{azim} - 1} azimSample(k)  e^{-j k \omega_x}
			  \f]
             * Multiply DFT factors with azimuth of Doppler FFT samples corrected for antenna calibration.
			 */
            dotpCmplxf((float *)&azimSamplesCalib[0], (float *)&cosValSinVal[0], MAX_NUM_AZIM_VIRT_ANT, &DFTValAzim.cmplx.real, &DFTValAzim.cmplx.imag);


            /* 6.  Single Bin DFT on the elevation antennas to estimate phase at peak.
			 *
			  \f[
				X_{elev} (\omega_x) = \sum_{k=0}^{N_{elev} - 1} elevSample(k)  e^{-j (k+2) \omega_x}
			  \f]
             * The elevation antennas (essentially the 4 virtual antennas corresponding to the
             * elevation offset Tx antenna) are 4 in number and offset by 3 positions from the
			 * azimuth virtual array. Hence when the DFT is computed, begin from the 3rd DFT parameter.
             *
             * Both elevSamplesCalib and cosValSinVal[4] are double-word aligned. */
#ifndef CASCADE_EVM
	            dotpCmplxf((float *)&elevSamplesCalib[0], (float *)&cosValSinVal[2], 4, &DFTValElev.cmplx.real, &DFTValElev.cmplx.imag); // KG : TODO : 4 -> 8 ; 2 -> 5 ?

#else
	            cmplxfImRe_t dotprodValElev1,  dotprodValElev2;
	            dotpCmplxf((float *)&elevSamplesCalib[0], (float *)&cosValSinVal[2], 4, &dotprodValElev1.real, &dotprodValElev1.imag);
                //Dot Product of elevation samples with CosValSinVal
                dotpCmplxf((float *) &elevSamplesCalib[4], (float *) &cosValSinVal[21], 4, &dotprodValElev2.real, &dotprodValElev2.imag);
                DFTValElev.cmplx.real = dotprodValElev1.real + dotprodValElev2.real;
                DFTValElev.cmplx.imag = dotprodValElev1.imag + dotprodValElev2.imag;
#endif

            /* 7. Estimate phase difference between the peak location at azimuth antennas and elevation antennas at peak.
             *  - 1. compute the conjugate product to get the phase difference (i.e. AzimVal * conj(ElevVal)) */
            elevOutput.ddat =  _complex_conjugate_mpysp (DFTValElev.ddat, DFTValAzim.ddat);


            /* - 2. Compute the angle of the product to estimate the phase change in elevation.
			 \f[
				\omega_z = angle (\ X_{elev} (\omega_x)' \times X_{azim} (\omega_x) )\
			  \f]
			*/
			if (fabsf(elevOutput.cmplx.imag) < (0.15f*fabsf(elevOutput.cmplx.real)))
            {
            	// small angle approximation.
            	wz = divsp(elevOutput.cmplx.imag, elevOutput.cmplx.real);
            }
            else
            {
	            wz = atan2sp(elevOutput.cmplx.imag, elevOutput.cmplx.real);
	            if (wz > PI_)
	            {
	            	wz -= 2.0f*PI_;
	            }
            }

            /* 8. Obtain range using the range resolution and the range Index  */
            range = rangeStep * (float)detObjList[objIdx].rangeIdx;

            /* 9. Obtain z, x coordinates.
			 \f[
				\Phi = asin(\frac{\omega_z}{2 \pi d_z})
			  \f]

			\f[
				z = range \times sin(\phi) = range * \frac{\omega_z}{2 \pi d_z}
			\f]

			*/
            elevSinPhase = wz * (1.0f / (2.0f * PI_ * Z_ANTENNA_SPACING_BY_LAMBDA));
            if ((elevSinPhase > subFrmObj->aoaFovSinVal.minElevationSinVal) || (elevSinPhase < subFrmObj->aoaFovSinVal.maxElevationSinVal))
            {

            z = range * elevSinPhase;

			/*
			\f[
				x = range  cos(\phi)  sin(\theta) =  range  /frac{\omega_x}{2 \pi d_x}
			\f]

			*/
            peakLocFlt = (peakIdxFlt * invAzimFFTSize);
            if (peakLocFlt > 0.5f)
            {
                peakLocFlt -= 1.0f;
            }

            x = range * peakLocFlt * (1.0f / X_ANTENNA_SPACING_BY_LAMBDA);

            /* Obtain 'square of y' coordinate
             \f[
				y^2 = range^2 -x^2 - z^2
			\f]
			*/
            ySquared = (range * range) - (z * z) - (x * x);

            /* It is possible that ySquared is less than zero (i.e. a degenerate case). In such a case ignore the object.
             * If the case is not degenerate, proceed to check if the object is in the field of view (FoV).
             * If so , store the newly validated object in the final object list.*/
            if (ySquared > 0)
            {
            	/* Estimate azimuth phase.
				 \f[
					sin(\Theta) = \frac{x}{range \times cos(\Phi)}
 	 	 	 	 \f]
				*/
                elevCosPhase = sqrtsp(1 - (elevSinPhase * elevSinPhase));
                azimSinPhase = divsp(x, (range * elevCosPhase));

                /* Check if object is in azimuth FoV, If object is in FoV, proceed to store the coordinates in the final object list */
                if ((azimSinPhase > subFrmObj->aoaFovSinVal.minAzimuthSinVal) || (azimSinPhase < subFrmObj->aoaFovSinVal.maxAzimuthSinVal))
                {

                    /* Store x, y, z values */
                    objOut[ValidObjIdx].z = z;
                    objOut[ValidObjIdx].x = x;
                    objOut[ValidObjIdx].y = sqrtsp(ySquared);

                    /* Obtain and store Velocity */
                    if (detObjList[objIdx].dopIdxActual > numDopplerBins / 2)
                    {
                        dopIdx = detObjList[objIdx].dopIdxActual - numDopplerBins;
                    }
                    else
                    {
                        dopIdx = detObjList[objIdx].dopIdxActual;
                    }
                    objOut[ValidObjIdx].velocity = dopIdx * dopplerStep;

                    /* Populate the compressed point cloud data */
                    gPointCloudRadialCompact[ValidObjIdx].rangeIdx = detObjList[objIdx].rangeIdx;
                    gPointCloudRadialCompact[ValidObjIdx].azimSinPhaseQuantized = (int16_t)(azimSinPhase * 32767);
                    gPointCloudRadialCompact[ValidObjIdx].elevSinPhaseQuantized = (int16_t)(elevSinPhase * 32767);
                    gPointCloudRadialCompact[ValidObjIdx].dopplerIdx = dopIdx;

                    /* Calcute the side info of final detected object */
                    /* output is 20*log10(2)*value/2^(QVALUE) */
                    noisedB = 6.0 * ((float)detObjList[objIdx].dopCfarNoise) * (1.0f/(1 << QVALUE_NOISE));
                    signaldB = 6.0 * ((float)detObjList[objIdx].azimPeakSamples[1]) * (1.0f/(1<<QVALUE_SIGNAL));
                    snrdB = signaldB - noisedB;

                    subFrmObj->detObjOutSideInfo[ValidObjIdx].snr = (int)(10*snrdB);
                    subFrmObj->detObjOutSideInfo[ValidObjIdx].noise = (int)(10*noisedB);

                    /* Increment output list index */
                    ValidObjIdx++;
                }
            }

        }
        }
    }
    *finalNumObjOut = ValidObjIdx;

    goto exit;
exit:
    return retVal;
}

#ifdef SOC_AWR294X
/**
 *  @b Description
 *  @n
 *      Function to configure the ADC Buffer register bits
 *      as per the the RX channel offset.
 *
 *  @param[in]  channel   RX Channel (0/1/2/3)
 *  @param[in]  offset    offset for the received data
 *
 */
static channelOffsetConfig(uint8_t channel, uint16_t offset)
{
    CSL_rss_ctrlRegs *ptrrssCtrlRegs = (CSL_rss_ctrlRegs *)CSL_RSS_CTRL_U_BASE;

    switch (channel)
    {
    case 0U:

        /* Setup the offset */
        CSL_REG32_FINS_RAW(&ptrrssCtrlRegs->ADCBUFCFG2,
                           CSL_RSS_CTRL_ADCBUFCFG2_ADCBUFCFG2_ADCBUFADDRX0_MASK,
                           CSL_RSS_CTRL_ADCBUFCFG2_ADCBUFCFG2_ADCBUFADDRX0_SHIFT,
                           ((uint32_t)offset >> 4U));
        break;
    case 1U:

        /* Setup the offset */
        CSL_REG32_FINS_RAW(&ptrrssCtrlRegs->ADCBUFCFG2,
                           CSL_RSS_CTRL_ADCBUFCFG2_ADCBUFCFG2_ADCBUFADDRX1_MASK,
                           CSL_RSS_CTRL_ADCBUFCFG2_ADCBUFCFG2_ADCBUFADDRX1_SHIFT,
                           ((uint32_t)offset >> 4U));
        break;
    case 2U:

        /* Setup the offset */
        CSL_REG32_FINS_RAW(&ptrrssCtrlRegs->ADCBUFCFG3,
                           CSL_RSS_CTRL_ADCBUFCFG3_ADCBUFCFG3_ADCBUFADDRX2_MASK,
                           CSL_RSS_CTRL_ADCBUFCFG3_ADCBUFCFG3_ADCBUFADDRX2_SHIFT,
                           ((uint32_t)offset >> 4U));
        break;
    case 3U:

        /* Setup the offset */
        CSL_REG32_FINS_RAW(&ptrrssCtrlRegs->ADCBUFCFG3,
                           CSL_RSS_CTRL_ADCBUFCFG3_ADCBUFCFG3_ADCBUFADDRX3_MASK,
                           CSL_RSS_CTRL_ADCBUFCFG3_ADCBUFCFG3_ADCBUFADDRX3_SHIFT,
                           ((uint32_t)offset >> 4U));
        break;

    default:
        /* Not  supported channels, code should not end up here */
        DebugP_assert(0);
        break;
    }
}

/**
 *  @b Description
 *  @n
 *      Function to calculate the RX channel offeset based
 *      on the data size of the channel.
 *
 *  @param[in]  rxChannelEn   RX Channel Bitmap b[3:0]=>RX4:RX1
 *  @param[in]  chanDataSize  Number of bytes per RX channel
 *
 */
void DPC_ObjectDetection_ConfigureADCBuf(
    uint16_t rxChannelEn,
    uint32_t chanDataSize)
{
    uint8_t channel;
    uint16_t offset = 0;

    /* channel offset reconfigure */

    for (channel = 0; channel < SYS_COMMON_NUM_RX_CHANNEL; channel++)
    {
        if (rxChannelEn & (0x1U << channel))
        {
            channelOffsetConfig(channel, offset);
            /* Calculate offset for the next channel */
            offset += chanDataSize;
        }
    }
}
#endif

#ifdef FILE_DATA_DEBUG
/* data memeory */
#pragma DATA_SECTION(dataInBuffer, ".l3ram");
uint32_t  dataInBuffer[386*4*96]; /*to save all adc buffer data */
#pragma DATA_SECTION(adcDataIn, ".l3ram");
uint32_t  adcDataIn[386*4] \
          __attribute__ ((aligned(CSL_CACHE_L1D_LINESIZE))); /*adc buffer for one chirp*/
#endif

#ifdef RECORD_NUM_OBJ_OUT_HISTORY
uint16_t gNumObjOut[512] = {0};
uint16_t frameCnt = 0;
#endif


/**
 *  @b Description
 *  @n
 *      Computes the rx phase compensation from the detection matrix
 *      during calibration measurement procedure of these parameters.
 *
 *  @param[in]  staticCfg Pointer to static configuration
 *  @param[in]  targetDistance Target distance in meters
 *  @param[in]  searchWinSize Search window size in meters
 *  @param[in] detMatrix Pointer to detection matrix
 *  @param[in] symbolMatrix Pointer to symbol matrix
 *  @param[out] compRxChanCfg computed output range bias and rx phase comp vector
 *
 *  @retval   None
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */


static void DPC_ObjDet_RxChPhaseMeasure
(
    DPC_ObjectDetection_StaticCfg       *staticCfg,
    float                   targetDistance,
    float                   searchWinSize,
    uint16_t                *detMatrix,
    DetObjParams            *detObjList,
    uint32_t                numObjOut,
    Measure_compRxChannelBiasCfg *compRxChanCfg
)
{
    float antMagSq[staticCfg->numVirtualAntennas];
    float antMagSqMin;
    float scal;
    float truePosition;
    int32_t truePositionIndex;
    int32_t halfWinSize;
    uint8_t antennaIdx, elevIdx;
    uint32_t objIdx, rangeidx;
    int32_t iMaxPos =-1, objIdxMax =-1;
    int32_t temp;

    uint16_t numDopFFTSubBins = staticCfg->numDopplerBins / staticCfg->numBandsTotal;
    uint16_t maxVal = 0;

    truePosition = targetDistance / staticCfg->rangeStep;
    truePositionIndex = (int32_t) (truePosition + 0.5);

    halfWinSize = (int32_t) (0.5 * searchWinSize / staticCfg->rangeStep + 0.5);

    /**** Strongest target position index ****/
    for(objIdx=0; objIdx< numObjOut; objIdx++){
        /** for all detected objects, if object is in the target distance range,
         * find object with maximum SNR in that range */
        rangeidx = detObjList[objIdx].rangeIdx;

        if(( rangeidx >= (truePositionIndex - halfWinSize)) && (rangeidx <= (truePositionIndex + halfWinSize))){
            /* doppler bin =0 ; considering target at zero velocity*/
            if (detMatrix[rangeidx*numDopFFTSubBins] > maxVal)
            {
                maxVal = detMatrix[rangeidx * numDopFFTSubBins];
                iMaxPos = rangeidx;
                objIdxMax = objIdx;
            }
        }
    }

    /*should not be first range bin (0) or -1 (error) */
    if(iMaxPos>0)
    {
		compRxChanCfg->range = iMaxPos*staticCfg->rangeStep;
		compRxChanCfg->peakVal = maxVal;

     /*** Calculate antenna normalization coefficients ***/
        for (antennaIdx = 0; antennaIdx < staticCfg->numVirtualAntennas; antennaIdx++)
        {
            if(antennaIdx < staticCfg->numVirtualAntAzim)
            {
                antMagSq[antennaIdx] = (float) detObjList[objIdxMax].azimSamples[antennaIdx].real * (float) detObjList[objIdxMax].azimSamples[antennaIdx].real +
                            (float) detObjList[objIdxMax].azimSamples[antennaIdx].imag * (float) detObjList[objIdxMax].azimSamples[antennaIdx].imag;
            }
            else
            {
                elevIdx = antennaIdx - staticCfg->numVirtualAntAzim;

            	antMagSq[antennaIdx] = (float) detObjList[objIdxMax].elevSamples[elevIdx].real * (float) detObjList[objIdxMax].elevSamples[elevIdx].real +
                            (float) detObjList[objIdxMax].elevSamples[elevIdx].imag * (float) detObjList[objIdxMax].elevSamples[elevIdx].imag;
            }
        }

        antMagSqMin = antMagSq[0];
        for (antennaIdx = 1; antennaIdx < staticCfg->numVirtualAntennas; antennaIdx++)
        {
            if (antMagSq[antennaIdx] < antMagSqMin)
            {
                antMagSqMin = antMagSq[antennaIdx];
            }
        }

        for (antennaIdx = 0; antennaIdx < staticCfg->numVirtualAntennas; antennaIdx++)
        {
            scal = 16384./ antMagSq[antennaIdx] * sqrt(antMagSqMin);

            if(antennaIdx < staticCfg->numVirtualAntAzim){
                temp = (int32_t) MATHUTILS_ROUND_FLOAT(scal * detObjList[objIdxMax].azimSamples[antennaIdx].real);
                MATHUTILS_SATURATE16(temp);
                compRxChanCfg->rxChPhaseComp[antennaIdx].real = (int16_t) (temp);

                temp = (int32_t) MATHUTILS_ROUND_FLOAT(-scal * detObjList[objIdxMax].azimSamples[antennaIdx].imag);
                MATHUTILS_SATURATE16(temp);
                compRxChanCfg->rxChPhaseComp[antennaIdx].imag = (int16_t) (temp);
            }

            else{
                elevIdx = antennaIdx - staticCfg->numVirtualAntAzim;
                temp = (int32_t) MATHUTILS_ROUND_FLOAT(scal * detObjList[objIdxMax].elevSamples[elevIdx].real);
                MATHUTILS_SATURATE16(temp);
                compRxChanCfg->rxChPhaseComp[antennaIdx].real = (int16_t) (temp);

                temp = (int32_t) MATHUTILS_ROUND_FLOAT(-scal * detObjList[objIdxMax].elevSamples[elevIdx].imag);
                MATHUTILS_SATURATE16(temp);
                compRxChanCfg->rxChPhaseComp[antennaIdx].imag = (int16_t) (temp);
            }
        }
    }
    else{
		compRxChanCfg->range = 0.0f;
		compRxChanCfg->peakVal = 0;
        /* target object not found */
        for (antennaIdx = 0; antennaIdx < staticCfg->numVirtualAntennas; antennaIdx++)
        {
                compRxChanCfg->rxChPhaseComp[antennaIdx].real = 16384;
                compRxChanCfg->rxChPhaseComp[antennaIdx].imag = 0;
        }
    }
}

/**
 *  @b Description
 *  @n
 *     Check the FFT clip status.
 *
 *  @param[in]  objDetObj Pointer to DPC object
 *  @param[out] clipCount FFT Clip Count
 *
 *  @retval   None
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */

void checkFFTClipStatus(ObjDetObj *objDetObj, uint32_t* clipCount)
{
    uint16_t clipStatusResult = 0U;

    /* Check the FFT clip Register after range fft */
    HWA_readClipStatus(objDetObj->hwaHandle, &clipStatusResult, HWA_CLIPREG_TYPE_FFT);
    /* Is FFT clipped? Yes: increment the clip count */
    if(clipStatusResult!=0U)    (*clipCount)++;
    /* Clear the FFT clip register */
    HWA_clearClipStatus(objDetObj->hwaHandle, HWA_CLIPREG_TYPE_FFT);
}

/**
 *  @b Description
 *  @n
 *      DPC's (DPM registered) execute function which is invoked by the application
 *      in the DPM's execute context when the DPC issues DPM_notifyExecute API from
 *      its registered @ref DPC_ObjectDetection_frameStart API that is invoked every
 *      frame interrupt.
 *
 *  @param[in]  handle       DPM's DPC handle
 *  @param[out]  ptrResult   Pointer to the result
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
uint32_t gDopplerTime;
int32_t DPC_ObjectDetection_execute(
    DPM_DPCHandle   handle,
    DPM_Buffer *ptrResult)
{
    ObjDetObj   *objDetObj;
    SubFrameObj *subFrmObj;
    DPU_RangeProcHWA_OutParams outRangeProc;
    DPU_DopplerProcHWA_OutParams outDopplerProc;
    DPU_RangeCFARProcHWA_OutParams outRangeCfarProc;
    DetObjParams * detObjList;
    DPIF_PointCloudCartesian * objOut;
    int32_t retVal;
    DPC_ObjectDetection_ExecuteResult *result;
    DPC_ObjectDetection_ProcessCallBackCfg *processCallBack;
    int32_t i;


    objDetObj = (ObjDetObj *) handle;
    DebugP_assert (objDetObj != NULL);
    DebugP_assert (ptrResult != NULL);

    processCallBack = &objDetObj->processCallBackCfg;

    if (processCallBack->processFrameBeginCallBackFxn != NULL)
    {
        (*processCallBack->processFrameBeginCallBackFxn)(objDetObj->subFrameIndx);
    }

    result = &objDetObj->executeResult;

    subFrmObj = &objDetObj->subFrameObj[objDetObj->subFrameIndx];

#ifdef FILE_DATA_DEBUG /* Debugging example */
    uint32_t numChirpsPerFrameRef = 96;
    uint32_t numAdcSamples = 384;
    uint32_t numBytesPerInputSample = 2;
    uint32_t chirpIdxRef;
    uint32_t j;

    FILE* fileId;
    fileId = fopen("x", "rb");
    if (fileId == NULL)
    {
        printf("Error:  Cannot open rangeprochwa_test_data_294x_real.bin !\n");
        exit(0);
    }

    printf("Reading file data..");

#ifdef SOC_AWR294X
    for (i = 0; i < 96; i++)
    {
        fread( (((uint16_t *)dataInBuffer) + i * 4 * 384),
            sizeof( uint16_t ), 4 *  384, fileId );
    }
#endif
#ifdef SOC_AM273X
    for (i = 0; i < 96; i++)
    {
        fread( (((uint16_t *)dataInBuffer) + i * 4 * 384 * 2),
            sizeof( uint16_t ), 4 *  384 * 2, fileId );
    }
#endif

    /* process chirps loop in the frame*/
    for(i=0; i< 96 ; i++)
    {
        /* i % testConfig.numChirpsPerFrameRef*/
        chirpIdxRef = i - (i / numChirpsPerFrameRef * 96);
        for (j = 0; j < 4; j++)
        {
            memcpy((void *)&adcDataIn[(j * ((384 + 7) / 8 * 8))/2],
                (void *)&dataInBuffer[(chirpIdxRef * 4 * 384 + j * 384)/2], numBytesPerInputSample*numAdcSamples);
        }

        uint32_t baseAddr, regionId;

        baseAddr = EDMA_getBaseAddr(objDetObj->edmaHandle[DPC_OBJDET_DPU_RANGEPROC_EDMA_INST_ID]);
        DebugP_assert(baseAddr != 0);

        regionId = EDMA_getRegionId(objDetObj->edmaHandle[DPC_OBJDET_DPU_RANGEPROC_EDMA_INST_ID]);
        DebugP_assert(regionId < SOC_EDMA_NUM_REGIONS);

        EDMA_enableTransferRegion(baseAddr, regionId, EDMA_DSS_TPCC_A_EVT_HWA_DMA_REQ30, EDMA_TRIG_MODE_MANUAL);

        while (EDMA_readIntrStatusRegion(baseAddr, regionId, EDMA_DSS_TPCC_A_EVT_CBUFF_DMA_REQ0) != 1);

        ClockP_usleep(1 * 1000U);

        EDMA_clrIntrRegion(baseAddr, regionId, EDMA_DSS_TPCC_A_EVT_CBUFF_DMA_REQ0);
    }
#endif

#ifndef DOPPLER_FILE_DATA_DEBUG
    /* Cache invalidation is required to mitigate incoherency
     * issues associated with EDMA transfer from/to L3. */
    CacheP_wbInvAll(CacheP_TYPE_ALL);

    retVal = DPU_RangeProcHWA_process(subFrmObj->dpuRangeObj, &outRangeProc);
    if (retVal != 0)
    {
        goto exit;
    }
    DebugP_assert(outRangeProc.endOfChirp == true);

    checkFFTClipStatus(objDetObj, &result->FFTClipCount[0]);

#endif

    if (processCallBack->processInterFrameBeginCallBackFxn != NULL)
    {
        (*processCallBack->processInterFrameBeginCallBackFxn)(objDetObj->subFrameIndx);
    }

    objDetObj->stats.interFrameStartTimeStamp = CycleCounterP_getCount32();

    DPC_Objdet_Assert(objDetObj->dpmHandle, (objDetObj->interSubFrameProcToken == 0));
    objDetObj->interSubFrameProcToken++;

    gTimingInfo.rangeEndTimes[gTimingInfo.rangeEndCnt % OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE] = CycleCounterP_getCount32();
    gTimingInfo.rangeEndCnt++;

    retVal = DPU_DopplerProcHWA_process(subFrmObj->dpuDopplerObj, &subFrmObj->dpuCfg.dopplerCfg, &outDopplerProc);
    if (retVal != 0)
    {
        goto exit;
    }
    gTimingInfo.dopEndTimes[gTimingInfo.dopEndCnt % OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE] = CycleCounterP_getCount32();
    gTimingInfo.dopEndCnt++;

    checkFFTClipStatus(objDetObj, &result->FFTClipCount[1]);

    if (subFrmObj->staticCfg.rangeCfarCfg.cfg.isEnabled)
    {
        retVal = DPU_RangeCFARProcHWA_process(subFrmObj->dpuRangeCfarObj, &subFrmObj->dpuCfg.rangeCfarCfg, &outRangeCfarProc);
        if (retVal != 0)
        {
            goto exit;
        }
    }

    detObjList = subFrmObj->dpuCfg.dopplerCfg.hwRes.detObjList;
    objOut     = subFrmObj->dpuCfg.dopplerCfg.hwRes.objOut;
    gDopplerTime = outDopplerProc.stats.processingTime;

    /* Procedure for Rx channels gain/phase offset measurement */
    if(objDetObj->commonCfg.measureRxChannelBiasCfg.enabled)
    {
        DPC_ObjDet_RxChPhaseMeasure(&subFrmObj->staticCfg,
            objDetObj->commonCfg.measureRxChannelBiasCfg.targetDistance,
            objDetObj->commonCfg.measureRxChannelBiasCfg.searchWinSize,
            subFrmObj->dpuCfg.dopplerCfg.hwRes.detMatrix.data,
            detObjList,
            outDopplerProc.numObjOut,
            &objDetObj->compRxChanCfgMeasureOut);
    }

    /********************************
     * Prepare for subFrame switch
     *******************************/
    if (objDetObj->commonCfg.numSubFrames > 1U)
    {
        uint8_t nextSubFrameIdx;
        SubFrameObj *nextSubFrmObj;
#ifdef SOC_AWR294X
        DPC_ObjectDetection_ADCBufConfig nextSubFrameADCBufConfig;
#endif

        if (objDetObj->subFrameIndx == (objDetObj->commonCfg.numSubFrames - 1))
        {
            nextSubFrameIdx = 0;
        }
        else
        {
            nextSubFrameIdx = objDetObj->subFrameIndx + 1;
        }
        /* get next subframe objDetObj */
        nextSubFrmObj = &objDetObj->subFrameObj[nextSubFrameIdx];

#ifdef SOC_AWR294X
        nextSubFrameADCBufConfig = nextSubFrmObj->staticCfg.ADCBufConfig;
        /* Configure ADC for next sub-frame */
        DPC_ObjectDetection_ConfigureADCBuf(
            nextSubFrameADCBufConfig.rxChannelEn,
            nextSubFrameADCBufConfig.adcBufChanDataSize);
#endif
        DPC_ObjDet_reconfigSubFrame(objDetObj, nextSubFrameIdx);

        /* Trigger Range DPU for the next sub frame */
        retVal = DPU_RangeProcHWA_control(nextSubFrmObj->dpuRangeObj,
                                          DPU_RangeProcHWA_Cmd_triggerProc, NULL, 0);
        if (retVal < 0)
        {
            goto exit;
        }
    }
    else
    {
        /* Trigger Range DPU for the next frame */
        retVal = DPU_RangeProcHWA_control(subFrmObj->dpuRangeObj,
                                          DPU_RangeProcHWA_Cmd_triggerProc, NULL, 0);
        if (retVal < 0)
        {
            goto exit;
        }
    }

    gTimingInfo.aoaStartTimes[gTimingInfo.aoaStartCnt % OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE] = CycleCounterP_getCount32();
    gTimingInfo.aoaStartCnt++;

    retVal = DPC_ObjDet_estimateXYZ(subFrmObj, objDetObj, detObjList, objOut, outDopplerProc.numObjOut, &result->numObjOut);
    if (retVal != 0)
    {
        goto exit;
    }

    gTimingInfo.aoaEndTimes[gTimingInfo.aoaEndCnt % OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE] = CycleCounterP_getCount32();
    gTimingInfo.aoaEndCnt++;

#ifdef RECORD_NUM_OBJ_OUT_HISTORY
    // result->numObjOut   = outDopplerProc.numObjOut;
    gNumObjOut[frameCnt % 512] = result->numObjOut;
    frameCnt ++;
#endif
	/* Set DPM result */
    result->subFrameIdx = objDetObj->subFrameIndx;
    result->objOut      = objOut;
    result->objOutSideInfo  = subFrmObj->detObjOutSideInfo;
    result->detMatrix   = subFrmObj->dpuCfg.dopplerCfg.hwRes.detMatrix;
    result->pointCloudRadialCompactList = gPointCloudRadialCompact;

    if (objDetObj->commonCfg.measureRxChannelBiasCfg.enabled == 1)
    {
        result->compRxChanBiasMeasurement = &objDetObj->compRxChanCfgMeasureOut;
    }
    else
    {
        result->compRxChanBiasMeasurement = NULL;
    }

    /* For rangeProcHwa, interChirpProcessingMargin is not available */
    objDetObj->stats.interChirpProcessingMargin = 0;

    objDetObj->stats.interFrameEndTimeStamp = CycleCounterP_getCount32();
    result->stats = &objDetObj->stats;

    /* populate DPM_resultBuf - first pointer and size are for results of the
     * processing */
    ptrResult->ptrBuffer[0] = (uint8_t *)result;
    ptrResult->size[0] = sizeof(DPC_ObjectDetection_ExecuteResult);

    /* clear rest of the result */
    for (i = 1; i < DPM_MAX_BUFFER; i++)
    {
        ptrResult->ptrBuffer[i] = NULL;
        ptrResult->size[i] = 0;
    }

exit:
    return retVal;
}

/**
 *  @b Description
 *  @n
 *      Sub-frame reconfiguration, used when switching sub-frames. Invokes the
 *      DPU configuration using the configuration that was stored during the
 *      pre-start configuration so reconstruction time is saved  because this will
 *      happen in real-time.
 *  @param[in]  objDetObj Pointer to DPC object
 *  @param[in]  subFrameIndx Sub-frame index.
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 *
 * \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static int32_t DPC_ObjDet_reconfigSubFrame(ObjDetObj *objDetObj, uint8_t subFrameIndx)
{
    int32_t retVal = 0;
    SubFrameObj *subFrmObj;

    subFrmObj = &objDetObj->subFrameObj[subFrameIndx];

    DPC_ObjDet_GenRangeWindow(&subFrmObj->dpuCfg.rangeCfg);

    retVal = DPU_RangeProcHWA_config(subFrmObj->dpuRangeObj, &subFrmObj->dpuCfg.rangeCfg);
    if (retVal != 0)
    {
        goto exit;
    }

    DPC_ObjDet_GenDopplerWindow(&subFrmObj->dpuCfg.dopplerCfg);
    retVal = DPU_DopplerProcHWA_config(subFrmObj->dpuDopplerObj, &subFrmObj->dpuCfg.dopplerCfg, 1);
    if (retVal != 0)
    {
        goto exit;
    }

    if (subFrmObj->staticCfg.rangeCfarCfg.cfg.isEnabled)
    {
        retVal = DPU_RangeCFARProcHWA_config(subFrmObj->dpuRangeCfarObj, &subFrmObj->dpuCfg.rangeCfarCfg);
        if (retVal != 0)
        {
            goto exit;
        }
    }

exit:
    return(retVal);
}

/**
 *  @b Description
 *  @n
 *      DPC's (DPM registered) start function which is invoked by the
 *      application using DPM_start API.
 *
 *  @param[in]  handle  DPM's DPC handle
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t DPC_ObjectDetection_start (DPM_DPCHandle handle)
{
    ObjDetObj   *objDetObj;
    SubFrameObj *subFrmObj;
    int32_t retVal = 0;

    objDetObj = (ObjDetObj *) handle;
    DebugP_assert (objDetObj != NULL);

    objDetObj->stats.frameStartIntCounter = 0;
    objDetObj->stats.subframeStartIntCounter = 0;
    objDetObj->numTimesResultExported = 0;
    memset((void*)&objDetObj->executeResult.FFTClipCount[0], 0, sizeof(objDetObj->executeResult.FFTClipCount));
    memset((void*)&gTimingInfo, 0, sizeof(timingInfo));

    /* Start marks consumption of all pre-start configs, reset the flag to check
     * if pre-starts were issued only after common config was issued for the next
     * time full configuration happens between stop and start */
    objDetObj->isCommonCfgReceived = false;

    /* App must issue export of last frame after stop which will switch to sub-frame 0,
     * so start should always see sub-frame indx of 0, check */
    DebugP_assert(objDetObj->subFrameIndx == 0);

    /* Pre-start cfgs for sub-frames may have come in any order, so need
     * to ensure we reconfig for the current (0) sub-frame before starting */
    DPC_ObjDet_reconfigSubFrame(objDetObj, objDetObj->subFrameIndx);

    /* Trigger Range DPU, related to reconfig above */
    subFrmObj = &objDetObj->subFrameObj[objDetObj->subFrameIndx];

#ifndef DOPPLER_FILE_DATA_DEBUG
    printf("HWA Triggered!\n");
    retVal = DPU_RangeProcHWA_control(subFrmObj->dpuRangeObj,
                 DPU_RangeProcHWA_Cmd_triggerProc, NULL, 0);
    if(retVal < 0)
    {
        goto exit;
    }
#endif

    DebugP_logInfo("ObjDet DPC: Start done\n");
exit:
    return(retVal);
}

static void ObjectDetection_freeDmaChannels(EDMA_Handle  edmaHandle)
{
    uint32_t   index;
    uint32_t  dmaCh, tcc, pram, shadow;

    for(index = 0; index < 64; index++)
    {
        dmaCh = index;
        tcc = index;
        pram = index;
        shadow = index;

        DPEDMA_freeEDMAChannel(edmaHandle, &dmaCh, &tcc, &pram, &shadow);

    }

    for(index = 0; index < 128; index++)
    {
        shadow = index;
        DebugP_assert(EDMA_freeParam(edmaHandle, &shadow) == SystemP_SUCCESS);
    }

    return;
}

/**
 *  @b Description
 *  @n
 *      DPC's (DPM registered) stop function which is invoked by the
 *      application using DPM_stop API.
 *
 *  @param[in]  handle  DPM's DPC handle
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t DPC_ObjectDetection_stop (DPM_DPCHandle handle)
{
    ObjDetObj   *objDetObj;
    uint32_t i, frame0StartTime;

    objDetObj = (ObjDetObj *) handle;
    DebugP_assert (objDetObj != NULL);

    ObjectDetection_freeDmaChannels(objDetObj->edmaHandle[0]);

#if defined(SOC_AM273X) && defined(LVDS_STREAM)
    /* Delete the CBUFF SW Session. */
    MmwDemo_LVDSStreamDeleteSwSession();
#endif

    /* print the FFT clip status */
    if(objDetObj->executeResult.FFTClipCount[0]>0U)
    {
        printf("Warning! FFT clipping happend for %d times in Range FFT Stage. \n", objDetObj->executeResult.FFTClipCount[0]);
    }
    if(objDetObj->executeResult.FFTClipCount[1]>0U)
    {
        printf("Warning! FFT clipping happend for %d times in Doppler or Azimuth FFT Stage. \n", objDetObj->executeResult.FFTClipCount[1]);
    }

    frame0StartTime = gTimingInfo.frameStartTimes[(gTimingInfo.frameCnt) % OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE];
    for (i = 0; i < OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE; i++)
    {
        gTimingInfo.frameStartTimes[i] -= frame0StartTime;
        gTimingInfo.frameStartTimes[i] /= OBJECTDETHWA_TIMING_CPU_CLK_FREQ_KHZ;
        gTimingInfo.rangeEndTimes[i] -= frame0StartTime;
        gTimingInfo.rangeEndTimes[i] /= OBJECTDETHWA_TIMING_CPU_CLK_FREQ_KHZ;
        gTimingInfo.dopEndTimes[i] -= frame0StartTime;
        gTimingInfo.dopEndTimes[i] /= OBJECTDETHWA_TIMING_CPU_CLK_FREQ_KHZ;
        gTimingInfo.aoaStartTimes[i] -= frame0StartTime;
        gTimingInfo.aoaStartTimes[i] /= OBJECTDETHWA_TIMING_CPU_CLK_FREQ_KHZ;
        gTimingInfo.aoaEndTimes[i] -= frame0StartTime;
        gTimingInfo.aoaEndTimes[i] /= OBJECTDETHWA_TIMING_CPU_CLK_FREQ_KHZ;
        gTimingInfo.resEndTimes[i] -= frame0StartTime;
        gTimingInfo.resEndTimes[i] /= OBJECTDETHWA_TIMING_CPU_CLK_FREQ_KHZ;
    }

#ifdef OBJECTDETHWA_PRINT_DPC_TIMING_INFO
    printf("\n");
    printf("----DPU Timing Info (ms)----\n");
    printf("%10s|%10s|%10s|%10s|%10s|%10s\n","FrameStart","RangeEnd","DopEnd","AoAStart","AoAEnd","ResEnd");

    // CacheP_wbInv((void *)&gDpc[0], sizeof(gFrameStartTimes), CacheP_TYPE_ALL);
    for (i = 0; i < OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE; i++)
    {
        printf("%10d|%10d|%10d|%10d|%10d|%10d\n", gTimingInfo.frameStartTimes[i], gTimingInfo.rangeEndTimes[i], gTimingInfo.dopEndTimes[i], gTimingInfo.aoaStartTimes[i], gTimingInfo.aoaEndTimes[i], gTimingInfo.resEndTimes[i]);
    }
    printf("-----------\n");
#endif

    /* We can be here only after complete frame processing is done, which means
     * processing token must be 0 and subFrameIndx also 0  */

    return(0);
}

/**
 *  @b Description
 *  @n
 *     Configure range DPU.
 *
 *  @param[in]  dpuHandle Handle to DPU
 *  @param[in]  staticCfg Pointer to static configuration of the sub-frame
 *  @param[in]  dynCfg    Pointer to dynamic configuration of the sub-frame
 *  @param[in]  edmaHandle Handle to edma driver to be used for the DPU
 *  @param[in]  radarCube Pointer to DPIF radar cube, which is output of range
 *                        processing.
 *  @param[in]  CoreLocalRamObj Pointer to core local RAM object to allocate local memory
 *              for the DPU, only for scratch purposes
 *  @param[in,out]  windowOffset Window coefficients that are generated by this function
 *                               (in heap memory) are passed to DPU configuration API to
 *                               configure the HWA window RAM starting from this offset.
 *                               The end offset after this configuration will be returned
 *                               in this variable which could be the begin offset for the
 *                               next DPU window RAM.
 *  @param[out]  CoreLocalRamScratchUsage Core Local RAM's scratch usage in bytes
 *  @param[out] cfgSave Configuration that is built in local
 *                      (stack) variable is saved here. This is for facilitating
 *                      quick reconfiguration later without having to go through
 *                      the construction of the configuration.
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static int32_t DPC_ObjDet_rangeConfig(DPU_RangeProcHWA_Handle dpuHandle,
                   DPC_ObjectDetection_StaticCfg *staticCfg,
#if 0
                   DPC_ObjectDetection_DynCfg    *dynCfg,
#endif
                   EDMA_Handle                   edmaHandle,
                   DPIF_RadarCube                *radarCube,
                   MemPoolObj                    *CoreLocalRamObj,
                   uint32_t                      *windowOffset,
                   uint32_t                      *CoreLocalRamScratchUsage,
                   DPU_RangeProcHWA_Config       *cfgSave,
                   ObjDetObj                     *ptrObjDetObj)
{
    int32_t retVal = 0;
    // DPU_RangeProcHWA_Config rangeCfg;
    DPU_RangeProcHWA_HW_Resources *hwRes = &cfgSave->hwRes;
    DPU_RangeProcHWA_EDMAInputConfig *edmaIn = &hwRes->edmaInCfg;
    DPU_RangeProcHWA_EDMAOutputConfig *edmaOut = &hwRes->edmaOutCfg;
    DPU_RangeProcHWA_HwaConfig *hwaCfg = &hwRes->hwaCfg;
    int32_t *windowBuffer;
    uint32_t winGenLen; //numRxAntennas,

    memset(cfgSave, 0, sizeof(DPU_RangeProcHWA_Config));

    /* static configuration */
    cfgSave->staticCfg.ADCBufData         = staticCfg->ADCBufData;
#ifdef FILE_DATA_DEBUG
    cfgSave->staticCfg.ADCBufData.data    = (void *)&adcDataIn[0];
#endif

    cfgSave->staticCfg.numChirpsPerFrame  = staticCfg->numChirpsPerFrame;
    cfgSave->staticCfg.numRangeBins       = staticCfg->numRangeBins;
    cfgSave->staticCfg.numFFTBins         = staticCfg->numRangeFFTBins;
    cfgSave->staticCfg.numTxAntennas      = staticCfg->numTxAntennas;
    cfgSave->staticCfg.numVirtualAntennas = staticCfg->numVirtualAntennas;

#ifdef SOC_AM273X
    #ifdef LVDS_STREAM
    cfgSave->staticCfg.isLVDSStreamEnabled = 1U;
    cfgSave->staticCfg.csirxOffset = LVDS_STREAM_PING_PONG_OFFSET;
    #else
    cfgSave->staticCfg.isLVDSStreamEnabled = 0U;
    cfgSave->staticCfg.csirxOffset = 0;
    #endif
#endif

    if (cfgSave->staticCfg.numRangeBins == cfgSave->staticCfg.numFFTBins)
    {
        cfgSave->staticCfg.isChirpDataReal    = 0;
    }
    else if (cfgSave->staticCfg.numRangeBins == cfgSave->staticCfg.numFFTBins / 2)
    {
        cfgSave->staticCfg.isChirpDataReal    = 1;
    }
    else
    {
        retVal = DPC_OBJECTDETECTION_RANGE_BINS_ERR;
        goto exit;
    }
    cfgSave->staticCfg.resetDcRangeSigMeanBuffer = 1;
    cfgSave->staticCfg.rangeFFTtuning.fftOutputDivShift =
                                    staticCfg->rangeFFTtuning.fftOutputDivShift;
    cfgSave->staticCfg.rangeFFTtuning.numLastButterflyStagesToScale =
                                    staticCfg->rangeFFTtuning.numLastButterflyStagesToScale;

    memcpy(&cfgSave->staticCfg.compressionCfg,
            &staticCfg->compressionCfg,
            sizeof(DPU_RangeProcHWA_CompressionCfg));
    memcpy(&cfgSave->staticCfg.intfStatsCfgdB,
            &staticCfg->intfStatsdBCfg,
            sizeof(DPU_RangeProcHWADDMA_intfStatsdBCfg));

    /* radarCube */
    cfgSave->hwRes.radarCube = *radarCube;

    /* static configuration - windows */
    /* Generating 1D window, allocate first */
    winGenLen = DPC_ObjDet_GetRangeWinGenLen(cfgSave);
    cfgSave->staticCfg.windowSize = winGenLen * sizeof(uint32_t);
    windowBuffer = (int32_t *)DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, cfgSave->staticCfg.windowSize, sizeof(uint32_t));
    if (windowBuffer == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_RANGE_HWA_WINDOW;
        goto exit;
    }
    cfgSave->staticCfg.window = windowBuffer;
    DPC_ObjDet_GenRangeWindow(cfgSave);

    /* hwres - edma */
    hwRes->edmaHandle = edmaHandle;
    /* We have choosen ISOLATE mode, so we have to fill in dataIn */

    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAIN_CH,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAIN_SHADOW,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAIN_EVENT_QUE,
                                       &edmaIn->dataIn);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAIN_SIG_CH,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAIN_SIG_SHADOW,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAIN_SIG_EVENT_QUE,
                                       &edmaIn->dataInSignature);



    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAOUT_SIG_CH,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAOUT_SIG_SHADOW,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAOUT_SIG_EVENT_QUE,
                                       &edmaOut->dataOutSignature);

    /* Ping */
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAOUT_FMT1_PING_CH,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAOUT_FMT1_PING_SHADOW,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAOUT_FMT1_PING_EVENT_QUE,
                                       &edmaOut->u.fmt1.dataOutPing);

    /* Pong */
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAOUT_FMT1_PONG_CH,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAOUT_FMT1_PONG_SHADOW,
                                       DPC_OBJDET_DPU_RANGEPROC_EDMAOUT_FMT1_PONG_EVENT_QUE,
                                       &edmaOut->u.fmt1.dataOutPong);

    {
        {

        uint32_t intrIdx = 0;

        /* Allocate interrupt object */
        cfgSave->hwRes.edmaTransferCompleteIntrObj = &ptrObjDetObj->rangProcIntrObj[intrIdx++];
        }
    }

#ifdef SOC_AM273X
    #if defined (DPC_SKIP_CSI_TRIGGER) || defined (LVDS_STREAM)
        /* In this case HWA hardware trigger source is equal to HWA param index value*/
        hwaCfg->dataInputMode = DPU_RangeProcHWA_InputMode_ISOLATED;
    #else
        hwaCfg->dataInputMode = DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM;
        hwaCfg->hardwareTrigSrc = DPC_OBJDET_HWA_HARDWARE_TRIGGER_SOURCE;
    #endif
#else
    /* In this case HWA hardware trigger source is equal to HWA param index value*/
    hwaCfg->dataInputMode = DPU_RangeProcHWA_InputMode_ISOLATED;
#endif

#ifdef OBJ_DETECTION_DDMA_TEST
    hwaCfg->dataInputMode = DPU_RangeProcHWA_InputMode_ISOLATED;
#endif

#ifdef DPC_USE_SYMMETRIC_WINDOW_RANGE_DPU
    hwaCfg->hwaWinSym = HWA_FFT_WINDOW_SYMMETRIC;
#else
    hwaCfg->hwaWinSym = HWA_FFT_WINDOW_NONSYMMETRIC;
#endif
    hwaCfg->hwaWinRamOffset = (uint16_t) *windowOffset;
    if ((hwaCfg->hwaWinRamOffset + winGenLen) > DPC_OBJDET_HWA_MAX_WINDOW_RAM_SIZE_IN_SAMPLES)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM_HWA_WINDOW_RAM;
        goto exit;
    }
    *windowOffset += winGenLen;

    hwaCfg->numParamSet = DPU_RANGEPROCHWADDMA_NUM_HWA_PARAM_SETS + staticCfg->compressionCfg.bfpCompExtraParamSets;
    hwaCfg->paramSetStartIdx = DPC_OBJDET_DPU_RANGEPROC_PARAMSET_START_IDX;

#ifndef DOPPLER_FILE_DATA_DEBUG
    retVal = DPU_RangeProcHWA_config(dpuHandle, cfgSave);
    if (retVal != 0)
    {
        goto exit;
    }
#endif

    /* store configuration for use in intra-sub-frame processing and
     * inter-sub-frame switching, although window will need to be regenerated and
     * dc range sig should not be reset. */
    cfgSave->staticCfg.resetDcRangeSigMeanBuffer = 0;

    /* report scratch usage */
    *CoreLocalRamScratchUsage = cfgSave->staticCfg.windowSize;
exit:

    return retVal;
}

/**
 *  @b Description
 *  @n
 *     Configure Doppler DPU.
 *
 *  @param[in]  dpuHandle Handle to DPU
 *  @param[in]  staticCfg Pointer to static configuration of the sub-frame
 *  @param[in]  log2NumDopplerBins log2 of numDopplerBins of the static config.
 *  @param[in]  dynCfg Pointer to dynamic configuration of the sub-frame
 *  @param[in]  edmaHandle Handle to edma driver to be used for the DPU
 *  @param[in]  radarCubeDecompressedSizeInBytes Size of radar cube if it were
 *              not compressed
 *  @param[in]  radarCube Pointer to DPIF radar cube, which will be the input
 *              to doppler processing
 *  @param[in]  detMatrix Pointer to DPIF detection matrix, which will be the output
 *              of doppler processing
 *  @param[in]  CoreLocalRamObj Pointer to core local RAM object to allocate local memory
 *              for the DPU, only for scratch purposes
 *  @param[in,out]  windowOffset Window coefficients that are generated by this function
 *                               (in heap memory) are passed to DPU configuration API to
 *                               configure the HWA window RAM starting from this offset.
 *                               The end offset after this configuration will be returned
 *                               in this variable which could be the begin offset for the
 *                               next DPU window RAM.
 *  @param[out]  CoreLocalRamScratchUsage Core Local RAM's scratch usage in bytes
 *  @param[out] cfgSave Configuration that is built in local
 *                      (stack) variable is saved here. This is for facilitating
 *                      quick reconfiguration later without having to go through
 *                      the construction of the configuration.
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static int32_t DPC_ObjDet_dopplerConfig(SubFrameObj *obj,
                   DPU_DopplerProcHWA_Handle dpuHandle,
                   DPC_ObjectDetection_StaticCfg *staticCfg,
                   uint8_t log2NumDopplerBins,
#if 0
                   DPC_ObjectDetection_DynCfg    *dynCfg,
#endif
                   float *                       antennaCalibParamsPtr,
                   EDMA_Handle                   edmaHandle,
                   uint32_t                      radarCubeDecompressedSizeInBytes,
                   DPIF_RadarCube                *radarCube,
                   DPIF_DetMatrix                *detMatrix,
                   MemPoolObj                    *CoreLocalRamObj,
                   MemPoolObj                    *L3ramObj,
                   void *                        CoreLocalScratchStartPoolAddr,
                   volatile void *               CoreLocalScratchStartPoolAddrNextDPU,
                   volatile void *               l3RamStartPoolAddrNextDPU,
                   uint32_t                      *windowOffset,
                   uint32_t                      *CoreLocalRamScratchUsage,
                   DPU_DopplerProcHWA_Config     *cfgSave,
                   ObjDetObj                     *objDetObj)
{

    int32_t retVal = 0;
    DPU_DopplerProcHWA_Config *dopCfg = cfgSave;
    DPU_DopplerProcHWA_HW_Resources  *hwRes;
    DPU_DopplerProcHWA_StaticConfig  *dopStaticCfg;
    DPU_DopplerProcHWA_EdmaCfg *edmaCfg;
    DPU_DopplerProcHWA_HwaCfg *hwaCfg;
    uint32_t *windowBuffer, winGenLen, winType;
    uint32_t numAzimFFTBins, azimFFTOutSize, localMaxOutSize;
    uint32_t dopFFTSizeTwoRangeGates;
    void * scratchBufMem;
    uint8_t pingPongIdx;

    hwRes = &dopCfg->hwRes;
    dopStaticCfg = &dopCfg->staticCfg;
    edmaCfg = &hwRes->edmaCfg;
    hwaCfg = &hwRes->hwaCfg;

    memset(dopCfg, 0, sizeof(dopCfg));

    dopStaticCfg->numTxAntennas         = staticCfg->numTxAntennas;
    dopStaticCfg->numAzimTxAntennas     = staticCfg->numVirtualAntAzim / staticCfg->ADCBufData.dataProperty.numRxAntennas;
    dopStaticCfg->numRxAntennas         = staticCfg->ADCBufData.dataProperty.numRxAntennas;
    dopStaticCfg->numVirtualAntennas    = staticCfg->numVirtualAntennas;
    dopStaticCfg->numRangeBins          = staticCfg->numRangeBins;
    dopStaticCfg->numChirps             = staticCfg->numChirps;
    dopStaticCfg->numDopplerFFTBins     = staticCfg->numDopplerBins;
    dopStaticCfg->numBandsTotal         = staticCfg->numBandsTotal;
    dopStaticCfg->log2NumDopplerBins    = log2NumDopplerBins;
    dopStaticCfg->isSumTxEnabled        = staticCfg->isSumTxEnabled;
    dopStaticCfg->maxNumObj = DPC_OBJDET_MAX_NUM_OBJECTS;

#ifdef OBJ_DETECTION_DDMA_TEST
    dopStaticCfg->numAzimFFTBins = 4 * mathUtils_getValidFFTSize(staticCfg->ADCBufData.dataProperty.numRxAntennas *\
                                                staticCfg->numVirtualAntAzim / \
                                                staticCfg->ADCBufData.dataProperty.numRxAntennas);

#else
    /* Setting numAzimFFTBins to 32 for 3 Azim Tx (as opposed to 48) as well as 2 Azim Tx case for optimization
       (instead of using the following formula:) */
    dopStaticCfg->numAzimFFTBins = OBJECTDETECTION_NUM_AZIM_FFT_BINS;
#endif

    /* The compression config structure in rangeproc and decompression config
       structure in dopplerproc are the same. */
    memcpy(&dopStaticCfg->decompCfg, &staticCfg->compressionCfg, sizeof(DPU_DopplerProc_DecompressionCfg));

    /* Cfar Cfg */
    memcpy(&dopStaticCfg->cfarCfg, &staticCfg->cfarCfg.cfg, sizeof(DPU_DopplerProc_CfarCfg));

    /* Local Max cfg */
    memcpy(&dopStaticCfg->localMaxCfg, &staticCfg->localMaxCfg, sizeof(DPU_DopplerProc_LocalMaxCfg));

    /* Antenna Calib Cfg */
    memcpy(&dopStaticCfg->antennaCalibParams, antennaCalibParamsPtr, sizeof(dopStaticCfg->antennaCalibParams));

    /* hwRes */
    edmaCfg->edmaHandle = edmaHandle;

    /********************************************
     * Allocating memory resources              *
     *******************************************/
    {{

    /* DPU Output Resource */
    hwRes->detObjListSizeInBytes = sizeof(DetObjParams) * DPC_OBJDET_MAX_NUM_OBJECTS;
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(L3ramObj, hwRes->detObjListSizeInBytes, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__OBJ_PARAMS_RAM_DOPPLER_DECOMP_BUF;
        goto exit;
    }
    hwRes->detObjList = (DetObjParams *)scratchBufMem;

    uint32_t objOutSizeInBytes = sizeof(DPIF_PointCloudCartesian) * DPC_OBJDET_MAX_NUM_OBJECTS;
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(L3ramObj, objOutSizeInBytes, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__OBJ_PARAMS_RAM_DOPPLER_DECOMP_BUF;
        goto exit;
    }
    hwRes->objOut = (DPIF_PointCloudCartesian *)scratchBufMem;

    uint32_t sideInfoSizeInBytes = sizeof(DPIF_PointCloudSideInfo) * DPC_OBJDET_MAX_NUM_OBJECTS;
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(L3ramObj, sideInfoSizeInBytes, DOUBLEWORD_ALIGNED);
    if (scratchBufMem == NULL){
        retVal = DPC_OBJECTDETECTION_ENOMEM__OBJ_PARAMS_SIDEINFO;
        goto exit;
    }
    obj->detObjOutSideInfo = (DPIF_PointCloudSideInfo *)scratchBufMem;

    l3RamStartPoolAddrNextDPU = DPC_ObjDet_MemPoolGet(L3ramObj);
    if (l3RamStartPoolAddrNextDPU == NULL)
    {
        retVal = DPC_OBJECTDETECTION_EINVAL;
        goto exit;
    }

    /* We don't need any L2 resources to be retained till the end of the next DPU */
    CoreLocalScratchStartPoolAddrNextDPU = DPC_ObjDet_MemPoolGet(CoreLocalRamObj);
    if (CoreLocalScratchStartPoolAddrNextDPU == NULL)
    {
        retVal = DPC_OBJECTDETECTION_EINVAL;
        goto exit;
    }
    /* Resources to be saved through the doppler stage */
    {
    {
    /* This resource needs to be saved through the doppler stage */
    hwRes->decompScratchBufferSizeBytes = radarCubeDecompressedSizeInBytes /
                                (staticCfg->numRangeBins / staticCfg->compressionCfg.rangeBinsPerBlock);
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(L3ramObj, hwRes->decompScratchBufferSizeBytes, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_DECOMP_BUF;
        goto exit;
    }
    hwRes->decompScratchBuf = (uint8_t *)scratchBufMem;

    /* This resource needs to be saved through the doppler stage */
    dopFFTSizeTwoRangeGates = (staticCfg->numDopplerBins / staticCfg->numBandsTotal) * staticCfg->numVirtualAntennas * sizeof(cmplx32ImRe_t) * 2; /* Ping and Pong */
    hwRes->dopFFTSubMatSizeBytes = dopFFTSizeTwoRangeGates;
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->dopFFTSubMatSizeBytes, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_DOPFFT_SUBMAT;
        goto exit;
    }
    hwRes->dopFFTSubMat = (uint8_t *)scratchBufMem;

    /* Allocate memory for Max Doppler Sub Band Buffers */
    hwRes->dopMaxSubBandScratchBufferSizeBytes = (staticCfg->numDopplerBins / staticCfg->numBandsTotal) * sizeof(uint8_t) * 2; /* Ping and Pong */
    for (pingPongIdx = 0; pingPongIdx < 2; pingPongIdx++)
    {
        scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->dopMaxSubBandScratchBufferSizeBytes / 2, sizeof(uint32_t));
        if (scratchBufMem == NULL)
        {
            retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_MAXDOP_SUBBAND;
            goto exit;
        }
        hwRes->dopMaxSubBandScratchBuf[pingPongIdx] = (uint8_t *)scratchBufMem;
    }

    /* Assign the detection matrix, radar cube */
    hwRes->detMatrix = *detMatrix;
    hwRes->radarCube = *radarCube;
    }
    }

    /* Get the current address and set it as the scratch beginning */
    CoreLocalScratchStartPoolAddr = DPC_ObjDet_MemPoolGet(CoreLocalRamObj);
    DPC_ObjDet_MemPoolSet(CoreLocalRamObj, CoreLocalScratchStartPoolAddr);

    /* Doppler scratch resources */
    {
    {

    /* Get appropriate memory sizes */
    {
    {
    hwRes->dopplerFFTScratchBufferSizeBytes = staticCfg->numDopplerBins * staticCfg->ADCBufData.dataProperty.numRxAntennas * sizeof(cmplx32ImRe_t) * 2; /* Ping and Pong */
    hwRes->DDMAMetricScratchBufferSizeBytes = staticCfg->numDopplerBins * sizeof(uint32_t) * 2; /* Ping and Pong */

    numAzimFFTBins = dopStaticCfg->numAzimFFTBins;

    azimFFTOutSize = numAzimFFTBins * (staticCfg->numDopplerBins / staticCfg->numBandsTotal) * sizeof(uint16_t);
    hwRes->azimFFTScratchBufferSizeBytes = azimFFTOutSize * 2; /* Ping and Pong */

    hwRes->maxCfarPeaksToDetect = DPC_OBJDET_MAX_NUM_CFAR_PEAKS;
    hwRes->cfarScratchBufferSizeBytes =  hwRes->maxCfarPeaksToDetect * sizeof(cmplx32ImRe_t) * 2; /* Ping and Pong */

    if (numAzimFFTBins % 32 == 0)
    {
        localMaxOutSize = (numAzimFFTBins / 32) * (staticCfg->numDopplerBins / staticCfg->numBandsTotal) * 4;
    }
    else
    {
        localMaxOutSize = ((numAzimFFTBins / 32) + 1) * (staticCfg->numDopplerBins / staticCfg->numBandsTotal) * 4;
    }

    hwRes->localMaxScratchBufferSizeBytes = localMaxOutSize * 2; /* Ping and Pong */
    }
    }

    /* Due to the ping/pong mechanism, the following scratch resources should not overlap:
     * doppler stage ping, azim stage pong
     * doppler stage pong, azim stage ping
     * doppler stage ping, doppler stage pong
     * azim stage ping, azim stage pong
     *
     * Hence the following overlaps are allowed:
     * doppler stage ping, azim stage ping
     * doppler stage pong, azim stage pong
     *
     * Thus, two scratch buffers will be allocated, one with doppler ping scratch memories and azim
     * ping scratch memories starting from the same location in the first buffer, and the corresponding pong
     * memories in the second buffer.
     These resources need not be retained post the doppler stage and can be overwritten by the Azim stage */

    uint32_t azimScratchBufEndAddr, dopScratchBufEndAddr, currScratchPoolStartAddress;
    currScratchPoolStartAddress = (uint32_t)CoreLocalScratchStartPoolAddr;

    /* ###################################################################
     * Allocate memory for the doppler ping and azim ping scratch buffers
     * ################################################################### */
    {
    {
    /* ##### Allocate memory for Doppler FFT Ping Buffer ##### */
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->dopplerFFTScratchBufferSizeBytes / 2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_DECOMP_BUF;
        goto exit;
    }
    hwRes->dopplerFFTScratchBuf[0] = (uint8_t *)scratchBufMem;

    /* ##### Allocate memory for DDMA Metric Ping Buffer ##### */
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->DDMAMetricScratchBufferSizeBytes / 2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_DDMAMETRIC_SCRATCH;
        goto exit;
    }
    hwRes->DDMAMetricScratchBuf[0] = (uint8_t *)scratchBufMem;

    /* Get doppler stage ping scratch buffer end address */
    dopScratchBufEndAddr = (uint32_t) DPC_ObjDet_MemPoolGet(CoreLocalRamObj);

    /* ##### Allocate memory for Azim FFT Ping Buffer ##### */
    /* Go to start of current pool */
    DPC_ObjDet_MemPoolSet(CoreLocalRamObj, (void *) currScratchPoolStartAddress);
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->azimFFTScratchBufferSizeBytes / 2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_AZIMFFT_SCRATCH;
        goto exit;
    }
    hwRes->azimFFTScratchBuf[0] = (uint8_t *)scratchBufMem;

    /* ##### Allocate memory for CFAR Ping Buffer ##### */
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->cfarScratchBufferSizeBytes / 2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_CFAR_SCRATCH;
        goto exit;
    }
    hwRes->cfarScratchBuf[0] = (uint8_t *)scratchBufMem;

    /* ##### Allocate memory for LM Ping Buffer ##### */
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->localMaxScratchBufferSizeBytes / 2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_LOCALMAX_SCRATCH;
        goto exit;
    }
    hwRes->localMaxScratchBuf[0] = (uint8_t *)scratchBufMem;

    azimScratchBufEndAddr = (uint32_t) DPC_ObjDet_MemPoolGet(CoreLocalRamObj);
    }
    }

    /* Find the end address of this buffer and assign it to the start address of the next buffer */
    currScratchPoolStartAddress = (azimScratchBufEndAddr > dopScratchBufEndAddr) ? (azimScratchBufEndAddr) : (dopScratchBufEndAddr);
    DPC_ObjDet_MemPoolSet(CoreLocalRamObj, (void *)currScratchPoolStartAddress);

    /* ###################################################################
     * Allocate memory for the doppler pong and azim pong scratch buffers
     * ################################################################### */
    {
    {
    /* ##### Allocate memory for Doppler FFT Pong Buffer ##### */
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->dopplerFFTScratchBufferSizeBytes / 2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_DECOMP_BUF;
        goto exit;
    }
    hwRes->dopplerFFTScratchBuf[1] = (uint8_t *)scratchBufMem;

    /* ##### Allocate memory for DDMA Metric Pong Buffer ##### */
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->DDMAMetricScratchBufferSizeBytes / 2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_DDMAMETRIC_SCRATCH;
        goto exit;
    }
    hwRes->DDMAMetricScratchBuf[1] = (uint8_t *)scratchBufMem;

    /* Get Doppler stage ping scratch buffer end address */
    dopScratchBufEndAddr = (uint32_t) DPC_ObjDet_MemPoolGet(CoreLocalRamObj);

    /* ##### Allocate memory for Azim FFT Pong Buffer ##### */
    /* Go to start of current pool */
    DPC_ObjDet_MemPoolSet(CoreLocalRamObj, (void *) currScratchPoolStartAddress);
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->azimFFTScratchBufferSizeBytes / 2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_AZIMFFT_SCRATCH;
        goto exit;
    }
    hwRes->azimFFTScratchBuf[1] = (uint8_t *)scratchBufMem;

    /* ##### Allocate memory for CFAR Pong Buffer ##### */
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->cfarScratchBufferSizeBytes / 2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_CFAR_SCRATCH;
        goto exit;
    }
    hwRes->cfarScratchBuf[1] = (uint8_t *)scratchBufMem;

    /* ##### Allocate memory for LM Pong Buffer ##### */
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwRes->localMaxScratchBufferSizeBytes / 2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_LOCALMAX_SCRATCH;
        goto exit;
    }
    hwRes->localMaxScratchBuf[1] = (uint8_t *)scratchBufMem;

    azimScratchBufEndAddr = (uint32_t) DPC_ObjDet_MemPoolGet(CoreLocalRamObj);
    }
    }

    /* Find the end address of this buffer and assign it to the start address of the next buffer */
    currScratchPoolStartAddress = (azimScratchBufEndAddr > dopScratchBufEndAddr) ? (azimScratchBufEndAddr) : (dopScratchBufEndAddr);
    DPC_ObjDet_MemPoolSet(CoreLocalRamObj, (void *) currScratchPoolStartAddress);
    }
    }
}
}

    /********************************************
     * Allocating hw resources (decomp stage)   *
     *******************************************/
{{
	DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DECOMP_IN_PING,
                                       EDMA_DOPPLERPROC_DECOMP_IN_PING_SHADOW,
                                       0,
                                       &edmaCfg->decompEdmaCfg.edmaIn.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DECOMP_IN_PONG,
                                       EDMA_DOPPLERPROC_DECOMP_IN_PONG_SHADOW,
                                       0,
                                       &edmaCfg->decompEdmaCfg.edmaIn.pingPong[1]);

    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DECOMP_OUT_PING,
                                       EDMA_DOPPLERPROC_DECOMP_OUT_PING_SHADOW,
                                       0,
                                       &edmaCfg->decompEdmaCfg.edmaOut.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DECOMP_OUT_PONG,
                                       EDMA_DOPPLERPROC_DECOMP_OUT_PONG_SHADOW,
                                       0,
                                       &edmaCfg->decompEdmaCfg.edmaOut.pingPong[1]);

    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DECOMP_IN_HOTSIG_PING,
                                       EDMA_DOPPLERPROC_DECOMP_IN_HOTSIG_PING_SHADOW,
                                       0,
                                       &edmaCfg->decompEdmaCfg.edmaInSignature.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DECOMP_IN_HOTSIG_PONG,
                                       EDMA_DOPPLERPROC_DECOMP_IN_HOTSIG_PONG_SHADOW,
                                       0,
                                       &edmaCfg->decompEdmaCfg.edmaInSignature.pingPong[1]);

    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DECOMP_OUT_HOTSIG,
                                       EDMA_DOPPLERPROC_DECOMP_OUT_HOTSIG_SHADOW,
                                       0,
                                       &edmaCfg->decompEdmaCfg.edmaOutSignature);

    hwaCfg->decompStageHwaStateMachineCfg.paramSetStartIdx = DPC_OBJDET_DPU_DOPPLERPROCHWADDMA_PARAMSET_START_IDX + staticCfg->compressionCfg.bfpCompExtraParamSets;
    hwaCfg->decompStageHwaStateMachineCfg.numParamSets = DPU_DOPPLERPOCHWADDMA_DECOMP_NUM_HWA_PARAMSETS + staticCfg->compressionCfg.bfpCompExtraParamSets;
}}


    /********************************************
     * Allocating hw resources (doppler stage)  *
     *******************************************/
    {{

    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DOPPLER_IN_PING,
                                       EDMA_DOPPLERPROC_DOPPLER_IN_PING_SHADOW,
                                       0,
                                       &edmaCfg->dopplerEdmaCfg.edmaIn.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DOPPLER_IN_PONG,
                                       EDMA_DOPPLERPROC_DOPPLER_IN_PONG_SHADOW,
                                       0,
                                       &edmaCfg->dopplerEdmaCfg.edmaIn.pingPong[1]);


    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DOPPLER_OUT_PING,
                                       EDMA_DOPPLERPROC_DOPPLER_OUT_PING_SHADOW,
                                       0,
                                       &edmaCfg->dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DOPPLER_OUT_PONG,
                                       EDMA_DOPPLERPROC_DOPPLER_OUT_PONG_SHADOW,
                                       0,
                                       &edmaCfg->dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[1]);


    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DOPPLER_IN_HOTSIG_PING,
                                       EDMA_DOPPLERPROC_DOPPLER_IN_HOTSIG_PING_SHADOW,
                                       0,
                                       &edmaCfg->dopplerEdmaCfg.edmaInSignature.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DOPPLER_IN_HOTSIG_PONG,
                                       EDMA_DOPPLERPROC_DOPPLER_IN_HOTSIG_PONG_SHADOW,
                                       0,
                                       &edmaCfg->dopplerEdmaCfg.edmaInSignature.pingPong[1]);


    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DOPPLERFFT_OUT_PING,
                                       EDMA_DOPPLERPROC_DOPPLERFFT_OUT_PING_SHADOW,
                                       0,
                                       &edmaCfg->dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DOPPLERFFT_OUT_PONG,
                                       EDMA_DOPPLERPROC_DOPPLERFFT_OUT_PONG_SHADOW,
                                       0,
                                       &edmaCfg->dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[1]);


    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DDMAMETRIC_OUT_PING,
                                       EDMA_DOPPLERPROC_DDMAMETRIC_OUT_PING_SHADOW,
                                       0,
                                       &edmaCfg->dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DDMAMETRIC_OUT_PONG,
                                       EDMA_DOPPLERPROC_DDMAMETRIC_OUT_PONG_SHADOW,
                                       0,
                                       &edmaCfg->dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[1]);

	if (dopStaticCfg->isSumTxEnabled)
	{
        DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                        EDMA_DOPPLERPROC_SUMTX_OUT_PING,
                                        EDMA_DOPPLERPROC_SUMTX_OUT_PING_SHADOW,
                                        0,
                                        &edmaCfg->dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[0]);
        DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                        EDMA_DOPPLERPROC_SUMTX_OUT_PONG,
                                        EDMA_DOPPLERPROC_SUMTX_OUT_PONG_SHADOW,
                                        0,
                                        &edmaCfg->dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[1]);
    }


    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_DOPPLER_OUT_HOTSIG,
                                       EDMA_DOPPLERPROC_DOPPLER_OUT_HOTSIG_SHADOW,
                                       0,
                                       &edmaCfg->dopplerEdmaCfg.edmaOutSignature);


	hwaCfg->dopplerStageHwaStateMachineCfg.paramSetStartIdx = hwaCfg->decompStageHwaStateMachineCfg.paramSetStartIdx + hwaCfg->decompStageHwaStateMachineCfg.numParamSets;
	if (dopStaticCfg->isSumTxEnabled)
	{
        hwaCfg->dopplerStageHwaStateMachineCfg.numParamSets = DPU_DOPPLERPOCHWADDMA_DOPPLER_NUM_HWA_PARAMSETS;
    }
	else
	{
        hwaCfg->dopplerStageHwaStateMachineCfg.numParamSets = DPU_DOPPLERPOCHWADDMA_DOPPLER_NUM_HWA_PARAMSETS - DPU_DOPPLERPOCHWADDMA_SUMTX_NUM_HWA_PARAMSETS;
    }

    }}

    /********************************************
     * Allocating hw resources (azim stage)     *
     *******************************************/
    {{

    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_AZIMFFT_IN_PING,
                                       EDMA_DOPPLERPROC_AZIMFFT_IN_PING_SHADOW,
                                       0,
                                       &edmaCfg->azimCfarEdmaCfg.edmaAzimFFTIn.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_AZIMFFT_IN_PONG,
                                       EDMA_DOPPLERPROC_AZIMFFT_IN_PONG_SHADOW,
                                       0,
                                       &edmaCfg->azimCfarEdmaCfg.edmaAzimFFTIn.pingPong[1]);


    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_AZIMFFT_IN_HOTSIG_PING,
                                       EDMA_DOPPLERPROC_AZIMFFT_IN_HOTSIG_PING_SHADOW,
                                       0,
                                       &edmaCfg->azimCfarEdmaCfg.edmaAzimFFTInSignature.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_AZIMFFT_IN_HOTSIG_PONG,
                                       EDMA_DOPPLERPROC_AZIMFFT_IN_HOTSIG_PONG_SHADOW,
                                       0,
                                       &edmaCfg->azimCfarEdmaCfg.edmaAzimFFTInSignature.pingPong[1]);


    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_AZIMFFT_OUT_PING,
                                       EDMA_DOPPLERPROC_AZIMFFT_OUT_PING_SHADOW,
                                       0,
                                       &edmaCfg->azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_AZIMFFT_OUT_PONG,
                                       EDMA_DOPPLERPROC_AZIMFFT_OUT_PONG_SHADOW,
                                       0,
                                       &edmaCfg->azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[1]);


    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_CFAR_OUT_PING,
                                       EDMA_DOPPLERPROC_CFAR_OUT_PING_SHADOW,
                                       0,
                                       &edmaCfg->azimCfarEdmaCfg.edmaCfarOut.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_CFAR_OUT_PONG,
                                       EDMA_DOPPLERPROC_CFAR_OUT_PONG_SHADOW,
                                       0,
                                       &edmaCfg->azimCfarEdmaCfg.edmaCfarOut.pingPong[1]);


    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_LOCALMAX_OUT_PING,
                                       EDMA_DOPPLERPROC_LOCALMAX_OUT_PING_SHADOW,
                                       0,
                                       &edmaCfg->azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_DOPPLERPROC_LOCALMAX_OUT_PONG,
                                       EDMA_DOPPLERPROC_LOCALMAX_OUT_PONG_SHADOW,
                                       0,
                                       &edmaCfg->azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[1]);

    hwaCfg->azimCfarStageHwaStateMachineCfg.paramSetStartIdx = hwaCfg->dopplerStageHwaStateMachineCfg.paramSetStartIdx + hwaCfg->dopplerStageHwaStateMachineCfg.numParamSets;
    hwaCfg->azimCfarStageHwaStateMachineCfg.numParamSets = DPU_DOPPLERPOCHWADDMA_AZIM_NUM_HWA_PARAMSETS;
}
}

{
    {

        uint32_t intrIdx = 0;

        /* Allocating interrupt objects */
        edmaCfg->decompEdmaCfg.edmaIntrObjDecompOut                  = &objDetObj->dopplerProcIntrObj[intrIdx++];
        edmaCfg->dopplerEdmaCfg.edmaIntrObjDopplerFFTOut.pingPong[0] = &objDetObj->dopplerProcIntrObj[intrIdx++];
        edmaCfg->dopplerEdmaCfg.edmaIntrObjDopplerFFTOut.pingPong[1] = &objDetObj->dopplerProcIntrObj[intrIdx++];
        edmaCfg->dopplerEdmaCfg.edmaIntrObjDDMAMetricOut.pingPong[0] = &objDetObj->dopplerProcIntrObj[intrIdx++];
        edmaCfg->dopplerEdmaCfg.edmaIntrObjDDMAMetricOut.pingPong[1] = &objDetObj->dopplerProcIntrObj[intrIdx++];
        edmaCfg->dopplerEdmaCfg.edmaIntrObjSumtxOut.pingPong[0]      = &objDetObj->dopplerProcIntrObj[intrIdx++];
        edmaCfg->dopplerEdmaCfg.edmaIntrObjSumtxOut.pingPong[1]      = &objDetObj->dopplerProcIntrObj[intrIdx++];

        edmaCfg->azimCfarEdmaCfg.edmaIntrObjAzimFFTOut.pingPong[0]   = &objDetObj->dopplerProcIntrObj[intrIdx++];
        edmaCfg->azimCfarEdmaCfg.edmaIntrObjAzimFFTOut.pingPong[1]   = &objDetObj->dopplerProcIntrObj[intrIdx++];
        edmaCfg->azimCfarEdmaCfg.edmaIntrObjCfarOut.pingPong[0]      = &objDetObj->dopplerProcIntrObj[intrIdx++];
        edmaCfg->azimCfarEdmaCfg.edmaIntrObjCfarOut.pingPong[1]      = &objDetObj->dopplerProcIntrObj[intrIdx++];
        edmaCfg->azimCfarEdmaCfg.edmaIntrObjLocalMaxOut.pingPong[0]  = &objDetObj->dopplerProcIntrObj[intrIdx++];
        edmaCfg->azimCfarEdmaCfg.edmaIntrObjLocalMaxOut.pingPong[1]  = &objDetObj->dopplerProcIntrObj[intrIdx++];
    }
}

    /* hwaCfg - window */
    winGenLen = DPC_ObjDet_GetDopplerWinGenLen(dopCfg);
    hwaCfg->windowSize = winGenLen * sizeof(int32_t);
    windowBuffer = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, hwaCfg->windowSize, sizeof(uint32_t));
    if (windowBuffer == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_DOPPLER_HWA_WINDOW;
        goto exit;
    }
    hwaCfg->window = (int32_t *)windowBuffer;
    hwaCfg->winRamOffset = (uint16_t) *windowOffset;
    winType = DPC_ObjDet_GenDopplerWindow(dopCfg);
    if (winType != DPC_DPU_DOPPLERPROC_FFT_WINDOW_TYPE)
    {
        retVal = DPC_OBJECTDETECTION_WIN_ERR;
        goto exit;
    }

#ifdef DPC_USE_SYMMETRIC_WINDOW_DOPPLER_DPU
    hwaCfg->winSym = HWA_FFT_WINDOW_SYMMETRIC;
#else
    hwaCfg->winSym = HWA_FFT_WINDOW_NONSYMMETRIC;
#endif
    if ((hwaCfg->winRamOffset + winGenLen) > DPC_OBJDET_HWA_MAX_WINDOW_RAM_SIZE_IN_SAMPLES)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM_HWA_WINDOW_RAM;
        goto exit;
    }
    *windowOffset += winGenLen;

#if 0
    /* Disable first stage scaling if window type is Hanning because Hanning scales
       by half */
    if (winType == MATHUTILS_WIN_HANNING)
    {
        hwaCfg->firstStageScaling = DPU_DOPPLERPROCHWA_FIRST_SCALING_DISABLED;
    }
    else
    {
        hwaCfg->firstStageScaling = DPU_DOPPLERPROCHWA_FIRST_SCALING_ENABLED;
    }
#endif

	retVal = DPU_DopplerProcHWA_config(dpuHandle, dopCfg, 1); // 1 ->  full config
    if (retVal != 0)
    {
        goto exit;
    }

    // /* store configuration for use in intra-sub-frame processing and
    //  * inter-sub-frame switching, although window will need to be regenerated */
    // *cfgSave = dopCfg;

    /* report scratch usage */
    *CoreLocalRamScratchUsage = hwaCfg->windowSize;

#ifdef DOPPLER_FILE_DATA_DEBUG
    printf("Reading file data.. \n");
    FILE * fileId;

    fileId = fopen("C:\\SDK41Plus\\ddmaDemo\\ti\\datapath\\dpc\\dpu\\dopplerproc\\test\\testdata\\compRCube.bin", "rb");
    if (fileId == NULL)
    {
        printf("Error:  Cannot open compRCube.bin !\n");
        exit(0);
    }
    fread(((uint16_t *)hwRes->radarCube.data), sizeof( uint16_t ), 73728, fileId );
    // fread(((uint16_t *)hwRes->radarCube.data), sizeof( uint16_t ), 589824, fileId );
#endif

exit:

    return retVal;
}

/**
 *  @b Description
 *  @n
 *     Configure range DPU.
 *
 *  @param[in]  dpuHandle Handle to DPU
 *  @param[in]  staticCfg Pointer to static configuration of the sub-frame
 *  @param[in]  dynCfg    Pointer to dynamic configuration of the sub-frame
 *  @param[in]  edmaHandle Handle to edma driver to be used for the DPU
 *  @param[in]  radarCube Pointer to DPIF radar cube, which is output of range
 *                        processing.
 *  @param[in]  CoreLocalRamObj Pointer to core local RAM object to allocate local memory
 *              for the DPU, only for scratch purposes
 *  @param[in,out]  windowOffset Window coefficients that are generated by this function
 *                               (in heap memory) are passed to DPU configuration API to
 *                               configure the HWA window RAM starting from this offset.
 *                               The end offset after this configuration will be returned
 *                               in this variable which could be the begin offset for the
 *                               next DPU window RAM.
 *  @param[out]  CoreLocalRamScratchUsage Core Local RAM's scratch usage in bytes
 *  @param[out] cfgSave Configuration that is built in local
 *                      (stack) variable is saved here. This is for facilitating
 *                      quick reconfiguration later without having to go through
 *                      the construction of the configuration.
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static int32_t DPC_ObjDet_rangeCfarConfig(DPU_RangeCFARProcHWA_Handle dpuHandle,
                   DPC_ObjectDetection_StaticCfg *staticCfg,
                   EDMA_Handle                   edmaHandle,
                   DPIF_DetMatrix                *detMatrix,
                   MemPoolObj                    *CoreLocalRamObj,
                   MemPoolObj                    *L3ramObj,
                   void                          *CoreLocalScratchStartPoolAddrNextDPU,
                   void                          *l3RamStartPoolAddrNextDPU,
                   DPU_RangeCfarProcHWA_Config   *cfgSave,
                   ObjDetObj                     *ptrObjDetObj)
{
    int32_t retVal = 0;
    void * scratchBufMem;
    DPU_RangeCFARProcHWA_HW_Resources *res = &cfgSave->res;

    memset(cfgSave, 0, sizeof(DPU_RangeCfarProcHWA_Config));

    cfgSave->staticCfg.numDopplerBins     = staticCfg->numChirpsPerFrame;
    cfgSave->staticCfg.numRangeBins       = staticCfg->numRangeBins;
    cfgSave->staticCfg.numSubBandsTotal   = staticCfg->numBandsTotal;
    memcpy(&cfgSave->staticCfg.cfarCfg,
            &staticCfg->rangeCfarCfg.cfg,
            sizeof(DPU_CFARProc_CfarCfg));

    /********************************************
     * Allocating memory resources              *
     *******************************************/
    {
        {

    /* DPU Output Resource */
    res->rangeCfarListSizeBytes = sizeof(RangeCfarListObj) * DPC_OBJDET_RANGECFAR_MAX_NUM_OBJECTS;
    scratchBufMem = DPC_ObjDet_MemPoolAlloc(L3ramObj, res->rangeCfarListSizeBytes, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__OBJ_PARAMS_RAM_RANGE_CFAR_BUF;
        goto exit;
    }
    res->rangeCfarList = (RangeCfarListObj *)scratchBufMem;

    l3RamStartPoolAddrNextDPU = DPC_ObjDet_MemPoolGet(L3ramObj);
    if (l3RamStartPoolAddrNextDPU == NULL)
    {
        retVal = DPC_OBJECTDETECTION_EINVAL;
        goto exit;
    }

    CoreLocalScratchStartPoolAddrNextDPU = DPC_ObjDet_MemPoolGet(CoreLocalRamObj);
    if (CoreLocalScratchStartPoolAddrNextDPU == NULL)
    {
        retVal = DPC_OBJECTDETECTION_EINVAL;
        goto exit;
    }

    res->rangeCfarScratchBufSizeBytes = sizeof(cmplx32ImRe_t) * DPC_OBJDET_RANGECFAR_MAX_NUM_OBJECTS;

    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, res->rangeCfarScratchBufSizeBytes/2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_RANGECFAR_SCRATCH_BUF;
        goto exit;
    }
    res->rangeCfarScratchBuf[0] = (uint8_t *)scratchBufMem;

    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, res->rangeCfarScratchBufSizeBytes/2, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_RANGECFAR_SCRATCH_BUF;
        goto exit;
    }
    res->rangeCfarScratchBuf[1] = (uint8_t *)scratchBufMem;

    res->rangeCfarNumObjPerDopplerBinSizeBytes = sizeof(uint16_t) * staticCfg->numChirpsPerFrame / staticCfg->numBandsTotal;

    scratchBufMem = DPC_ObjDet_MemPoolAlloc(CoreLocalRamObj, res->rangeCfarNumObjPerDopplerBinSizeBytes, sizeof(uint32_t));
    if (scratchBufMem == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__CORE_LOCAL_RAM_RANGECFAR_NUMOBJ_PER_DOPPLER_BUF;
        goto exit;
    }
    res->rangeCfarNumObjPerDopplerBinBuf = (uint8_t *)scratchBufMem;

    /* Assign the detection matrix, radar cube */
    res->detMatrix = *detMatrix;
	    }
	}

    /* hwres - edma */
    res->edmaHandle = edmaHandle;
    res->detMatBytesPerSample = sizeof(uint16_t);
    res->maxNumCFARObj = DPC_OBJDET_RANGECFAR_MAX_NUM_OBJECTS;

    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_RANGECFARPROC_CFAR_IN_PING,
                                       EDMA_RANGECFARPROC_CFAR_IN_PING_SHADOW,
                                       DPC_OBJDET_DPU_RANGECFARPROC_EVENT_QUE,
                                       &res->edmaIn.pingPong[0]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_RANGECFARPROC_CFAR_IN_HOTSIG_PING,
                                       EDMA_RANGECFARPROC_CFAR_IN_HOTSIG_PING_SHADOW,
                                       DPC_OBJDET_DPU_RANGECFARPROC_EVENT_QUE,
                                       &res->edmaInSignature.pingPong[0]);

    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_RANGECFARPROC_CFAR_IN_PONG,
                                       EDMA_RANGECFARPROC_CFAR_IN_PONG_SHADOW,
                                       DPC_OBJDET_DPU_RANGECFARPROC_EVENT_QUE,
                                       &res->edmaIn.pingPong[1]);
    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_RANGECFARPROC_CFAR_IN_HOTSIG_PONG,
                                       EDMA_RANGECFARPROC_CFAR_IN_HOTSIG_PONG_SHADOW,
                                       DPC_OBJDET_DPU_RANGECFARPROC_EVENT_QUE,
                                       &res->edmaInSignature.pingPong[1]);

    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_RANGECFARPROC_CFAR_OUT_PING,
                                       EDMA_RANGECFARPROC_CFAR_OUT_PING_SHADOW,
                                       DPC_OBJDET_DPU_RANGECFARPROC_EVENT_QUE,
                                       &res->edmaOut.pingPong[0]);

    DPC_ObjDet_EDMAChannelConfigAssist(edmaHandle,
                                       EDMA_RANGECFARPROC_CFAR_OUT_PONG,
                                       EDMA_RANGECFARPROC_CFAR_OUT_PONG_SHADOW,
                                       DPC_OBJDET_DPU_RANGECFARPROC_EVENT_QUE,
                                       &res->edmaOut.pingPong[1]);

    {
        {

        uint32_t intrIdx = 0;

        /* Allocate interrupt object */
        cfgSave->res.edmaIntrObj.pingPong[0] = &ptrObjDetObj->rangeCfarProcIntrObj[intrIdx++];
        cfgSave->res.edmaIntrObj.pingPong[1] = &ptrObjDetObj->rangeCfarProcIntrObj[intrIdx++];
        }
    }

    res->hwaCfg.numParamSet = DPU_RANGECFARPROCHWADDMA_NUM_HWA_PARAMSETS;
    res->hwaCfg.paramSetStartIdx = DPC_OBJDET_DPU_RANGECFARPROCHWADDMA_PARAMSET_START_IDX + 2*staticCfg->compressionCfg.bfpCompExtraParamSets;

    retVal = DPU_RangeCFARProcHWA_config(dpuHandle, cfgSave);
    if (retVal != 0)
    {
        goto exit;
    }

exit:

    return retVal;
}



/**
 *  @b Description
 *  @n
 *     Performs processing related to pre-start configuration, which is per sub-frame,
 *     by configuring each of the DPUs involved in the processing chain.
 *  Memory management notes:
 *  1. Core Local Memory that needs to be preserved across sub-frames (such as range DPU's calib DC buffer)
 *     will be allocated using MemoryP_alloc.
 *  2. Core Local Memory that needs to be preserved within a sub-frame across DPU calls
 *     (the DPIF * type memory) or for intermediate private scratch memory for
 *     DPU (i.e no preservation is required from process call to process call of the DPUs
 *     within the sub-frame) will be allocated from the Core Local RAM configuration supplied in
 *     @ref DPC_ObjectDetection_InitParams given to @ref DPC_ObjectDetection_init API
 *  3. L3 memory will only be allocated from the L3 RAM configuration supplied in
 *     @ref DPC_ObjectDetection_InitParams given to @ref DPC_ObjectDetection_init API
 *     No L3 buffers are presently required that need to be preserved across sub-frames
 *     (type described in #1 above), neither are L3 scratch buffers required for
 *     intermediate processing within DPU process call.
 *
 *  @param[in]  obj Pointer to sub-frame object
 *  @param[in]  commonCfg Pointer to pre-start common configuration
 *  @param[in]  staticCfg Pointer to static configuration of the sub-frame
 *  @param[in]  dynCfg Pointer to dynamic configuration of the sub-frame
 *  @param[in]  edmaHandle Pointer to array of EDMA handles for the device, this
 *              can be distributed among the DPUs, although presently we only
 *              use the first handle for all DPUs.
 *  @param[in]  L3ramObj Pointer to L3 RAM memory pool object
 *  @param[in]  CoreLocalRamObj Pointer to Core Local RAM memory pool object
 *  @param[in]  hwaMemBankAddr pointer to HWA Memory Bank addresses that will be used
 *              to allocate various scratch areas for the DPU processing
 *  @param[in]  hwaMemBankSize Size in bytes of each of HWA memory banks
 *  @param[out] L3RamUsage Net L3 RAM memory usage in bytes as a result of allocation
 *              by the DPUs.
 *  @param[out] CoreLocalRamUsage Net Core Local RAM memory usage in bytes as a
 *              result of allocation by the DPUs.
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 */
static int32_t DPC_ObjDet_preStartConfig(SubFrameObj *obj,
                  DPC_ObjectDetection_PreStartCommonCfg *commonCfg,
                   DPC_ObjectDetection_StaticCfg *staticCfg,
                   EDMA_Handle                   edmaHandle[EDMA_NUM_CC],
                   MemPoolObj                    *L3ramObj,
                   MemPoolObj                    *CoreLocalRamObj,
                   uint32_t                      *hwaMemBankAddr,
                   uint16_t                      hwaMemBankSize,
                   uint32_t                      *L3RamUsage,
                   uint32_t                      *CoreLocalRamUsage,
                   ObjDetObj                    *ptrObjDetObj)
{
    int32_t retVal = 0;
    DPIF_RadarCube radarCube;
    DPIF_DetMatrix detMatrix;
    uint32_t hwaWindowOffset;
    uint32_t rangeCoreLocalRamScratchUsage,
             dopplerCoreLocalRamScratchUsage;
    void *CoreLocalScratchStartPoolAddr;
    float achievedCompressionRatio;
    uint32_t outputBytesPerBlock, inputBytesPerBlock;
    uint32_t radarCubeDecompressedSizeInBytes;
    void *CoreLocalScratchStartPoolAddrNextDPU = 0;
    void *l3RamStartPoolAddrNextDPU = 0;
    float radConversionFactor;

    /* save configs to object. We need to pass this stored config (instead of
       the input arguments to this function which will be in stack) to
       the DPU config functions inside of this function because the DPUs
       have pointers to dynamic configurations which are later going to be
       reused during re-configuration (intra sub-frame or inter sub-frame)
     */
    obj->staticCfg = *staticCfg;

    hwaWindowOffset = DPC_OBJDET_HWA_WINDOW_RAM_OFFSET;

    /* derived config */
    obj->log2NumDopplerBins = mathUtils_floorLog2(staticCfg->numDopplerBins);

    DPC_ObjDet_MemPoolReset(L3ramObj);
    DPC_ObjDet_MemPoolReset(CoreLocalRamObj);

    /* L3 allocations */
    /* L3 - radar cube */
    /* Input and output samples out of the rangeproc/compression DPU */
    if(staticCfg->compressionCfg.compressionMethod==HWA_COMPRESS_METHOD_BFP)
    {
        inputBytesPerBlock = 4 * staticCfg->compressionCfg.rangeBinsPerBlock;
    }
    else
    {
        inputBytesPerBlock = 4 * staticCfg->compressionCfg.numRxAntennaPerBlock * staticCfg->compressionCfg.rangeBinsPerBlock;
    }

#ifndef CASCADE_EVM
    outputBytesPerBlock = (uint16_t)(((uint16_t) ((staticCfg->compressionCfg.compressionRatio *
                            (inputBytesPerBlock) + 3)/4)) * 4); /* Word aligned */
#else
    outputBytesPerBlock = (MATHUTILS_CEILING_POS_FLOAT(inputBytesPerBlock * staticCfg->compressionCfg.compressionRatio / 4.0)) * 4; /* Word aligned */
#endif

    achievedCompressionRatio = (float) outputBytesPerBlock / (float) inputBytesPerBlock;

    radarCubeDecompressedSizeInBytes = staticCfg->numRangeBins * staticCfg->numChirps *
                                        staticCfg->ADCBufData.dataProperty.numRxAntennas * sizeof(cmplx16ReIm_t);
    radarCube.dataSize =  radarCubeDecompressedSizeInBytes * achievedCompressionRatio;
    radarCube.data = DPC_ObjDet_MemPoolAlloc(L3ramObj, radarCube.dataSize,
                                             DPC_OBJDET_RADAR_CUBE_DATABUF_BYTE_ALIGNMENT);

#if defined (SOC_AM273X) && defined (LVDS_STREAM)
    obj->staticCfg.ADCBufData.data = (void *) gConfigCsirx0ContextConfig[0].pingPongConfig.pingAddress;
#endif

    if (radarCube.data == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__L3_RAM_RADAR_CUBE;
        goto exit;
    }
    radarCube.datafmt = DPIF_RADARCUBE_FORMAT_2;

    if (staticCfg->isSumTxEnabled)
    {
        /* L3 - detection matrix */
        detMatrix.dataSize = staticCfg->numRangeBins * (staticCfg->numDopplerBins / staticCfg->numBandsTotal) * sizeof(uint16_t);
        detMatrix.data = DPC_ObjDet_MemPoolAlloc(L3ramObj, detMatrix.dataSize,
                                                DPC_OBJDET_DET_MATRIX_DATABUF_BYTE_ALIGNMENT);
        if (detMatrix.data == NULL)
        {
            retVal = DPC_OBJECTDETECTION_ENOMEM__L3_RAM_DET_MATRIX;
            goto exit;
        }
        detMatrix.datafmt = DPIF_DETMATRIX_FORMAT_1;
    }

   /* L3 - radialPointCloudCompressed */
    gPointCloudRadialCompact = DPC_ObjDet_MemPoolAlloc(L3ramObj,
                                                       sizeof(DPC_ObjectDetection_PointCloudRadialCompact) * DPC_OBJDET_MAX_NUM_OBJECTS,
                                                       DPC_OBJDET_DET_MATRIX_DATABUF_BYTE_ALIGNMENT);
    if (gPointCloudRadialCompact == NULL)
    {
        retVal = DPC_OBJECTDETECTION_ENOMEM__L3_RADIAL_POINTCLOUD_COMPACT;
        goto exit;
    }

    /* Remember pool position */
    CoreLocalScratchStartPoolAddr = DPC_ObjDet_MemPoolGet(CoreLocalRamObj);

#if defined(SOC_AM273X) && defined(LVDS_STREAM)
    /* Configure SW session for this LVDS Stream */
    #ifdef CASCADE_EVM
      if (MmwDemo_LVDSStreamSwConfig((uint32_t) gConfigCsirx0ContextConfig[0].pingPongConfig.pingAddress,
                                     (uint32_t) gConfigCsirx1ContextConfig[0].pingPongConfig.pingAddress,
                                     (uint32_t) obj->staticCfg.ADCBufData.dataSize/2) < 0)
        {
            test_print("Failed LVDS stream SW configuration\n");
            DebugP_assert(0);
        }
     #else
        if (MmwDemo_LVDSStreamSwConfig((uint32_t) gConfigCsirx0ContextConfig[0].pingPongConfig.pingAddress,
                                    (uint32_t) obj->staticCfg.ADCBufData.dataSize) < 0)
        {
            test_print("Failed LVDS stream SW configuration\n");
            DebugP_assert(0);
        }
     #endif
#endif

#ifndef DOPPLER_FILE_DATA_DEBUG
    retVal = DPC_ObjDet_rangeConfig(obj->dpuRangeObj, &obj->staticCfg,
                 edmaHandle[DPC_OBJDET_DPU_RANGEPROC_EDMA_INST_ID],
                 &radarCube, CoreLocalRamObj, &hwaWindowOffset,
                 &rangeCoreLocalRamScratchUsage, &obj->dpuCfg.rangeCfg,
                 ptrObjDetObj);
    if (retVal != 0)
    {
        goto exit;
    }
#endif

    if (obj->staticCfg.rangeCfarCfg.cfg.isEnabled)
    {
        /* allocating range cfar resources after range DPU resource allocation because of the following reasons:
         * 1. range DPU resources + range cfar resources are mostly less than dppler DPU resources size; so,
         * instead of allocating at the end of doppler dpu, allocating here, will save around 4-5KB of L2RAM.
         * 2. rangeCfarNumObjPerDopplerBinBuf is used in estimate_xyz function, so it should not overlap with
         * range dpu resources (i.e. range window in L2), as range dpu for next subframe is reconfigured before
         * estimate_xyz function during subframe switching.
         * 3. Similarly rangeCfarNumObjPerDopplerBinBuf should not overlap with doppler window, as window is generated again
         * during subframe reconfiguration and may overwrite the rangeCfarNumObjPerDopplerBinBuf.
         */
        retVal = DPC_ObjDet_rangeCfarConfig(obj->dpuRangeCfarObj, &obj->staticCfg,
                                    edmaHandle[DPC_OBJDET_DPU_RANGECFARPROC_EDMA_INST_ID],
                                    &detMatrix,
                                    CoreLocalRamObj,
                                    L3ramObj,
                                    CoreLocalScratchStartPoolAddrNextDPU,
                                    l3RamStartPoolAddrNextDPU,
                                    &obj->dpuCfg.rangeCfarCfg,
                                    ptrObjDetObj);
        if (retVal != 0)
        {
            goto exit;
        }
    }

    /* Rewind to the scratch beginning */
    DPC_ObjDet_MemPoolSet(CoreLocalRamObj, CoreLocalScratchStartPoolAddr);
    CoreLocalScratchStartPoolAddrNextDPU = CoreLocalScratchStartPoolAddr;
    l3RamStartPoolAddrNextDPU = DPC_ObjDet_MemPoolGet(L3ramObj);

    retVal = DPC_ObjDet_dopplerConfig(obj, obj->dpuDopplerObj, &obj->staticCfg,
                 obj->log2NumDopplerBins,
                 commonCfg->antennaCalibParams,
                 edmaHandle[DPC_OBJDET_DPU_DOPPLERPROC_EDMA_INST_ID],
                 radarCubeDecompressedSizeInBytes,
                 &radarCube, &detMatrix, CoreLocalRamObj, L3ramObj,
                 CoreLocalScratchStartPoolAddr,
                 (volatile void *)CoreLocalScratchStartPoolAddrNextDPU,
                 (volatile void *)l3RamStartPoolAddrNextDPU, &hwaWindowOffset,
                 &dopplerCoreLocalRamScratchUsage, &obj->dpuCfg.dopplerCfg,
                 ptrObjDetObj);
    if (retVal != 0)
    {
        goto exit;
    }

    /* Sin values of FOV */
    radConversionFactor = PI_/180.0f;
    obj->aoaFovSinVal.minAzimuthSinVal   = sinsp(radConversionFactor * obj->staticCfg.aoaFovCfg.minAzimuthDeg);
    obj->aoaFovSinVal.maxAzimuthSinVal   = sinsp(radConversionFactor * obj->staticCfg.aoaFovCfg.maxAzimuthDeg);
    obj->aoaFovSinVal.minElevationSinVal = sinsp(radConversionFactor * obj->staticCfg.aoaFovCfg.minElevationDeg);
    obj->aoaFovSinVal.maxElevationSinVal = sinsp(radConversionFactor * obj->staticCfg.aoaFovCfg.maxElevationDeg);

    /* Report RAM usage */
    *CoreLocalRamUsage = DPC_ObjDet_MemPoolGetMaxUsage(CoreLocalRamObj);
    *L3RamUsage = DPC_ObjDet_MemPoolGetMaxUsage(L3ramObj);

exit:
    return retVal;
}

/**
 *  @b Description
 *  @n
 *      DPC IOCTL commands configuration API which will be invoked by the
 *      application using DPM_ioctl API
 *
 *  @param[in]  handle   DPM's DPC handle
 *  @param[in]  cmd      Capture DPC specific commands
 *  @param[in]  arg      Command specific arguments
 *  @param[in]  argLen   Length of the arguments which is also command specific
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t DPC_ObjectDetection_ioctl(
    DPM_DPCHandle   handle,
    uint32_t            cmd,
    void*               arg,
    uint32_t argLen)
{
    ObjDetObj   *objDetObj;
    SubFrameObj *subFrmObj;
    int32_t      retVal = 0;

    /* Get the DSS MCB: */
    objDetObj = (ObjDetObj *) handle;
    DebugP_assert(objDetObj != NULL);

    /* Process the commands. Process non sub-frame specific ones first
     * so the sub-frame specific ones can share some code. */
    if (cmd == DPC_OBJDET_IOCTL__TRIGGER_FRAME)
    {
        DPC_ObjectDetection_frameStart(handle);
    }
    else if (cmd == DPC_OBJDET_IOCTL__STATIC_PRE_START_COMMON_CFG)
    {
        DPC_ObjectDetection_PreStartCommonCfg *cfg;

        DebugP_assert(argLen == sizeof(DPC_ObjectDetection_PreStartCommonCfg));

        cfg = (DPC_ObjectDetection_PreStartCommonCfg*)arg;

        objDetObj->commonCfg = *cfg;
        objDetObj->isCommonCfgReceived = true;

#if defined (SOC_AM273X) && defined (LVDS_STREAM)
        /* Initialize LVDS streaming components */
        if ((retVal = MmwDemo_LVDSStreamInit(cfg->numLVDSLanes)) < 0 )
        {
            test_print ("Error: MMW Demo LVDS stream init failed with Error[%d]\n", retVal);
        }

        /*The delay below is needed only if the DCA1000EVM is being used to capture the data traces.
        This is needed because the DCA1000EVM FPGA needs the delay to lock to the
        bit clock before they can start capturing the data correctly. */
        ClockP_usleep((12U * 1000U));
#endif

        DebugP_logInfo("ObjDet DPC: Pre-start Common Config IOCTL processed\n");
    }
    else if (cmd == DPC_OBJDET_IOCTL__DYNAMIC_EXECUTE_RESULT_EXPORTED)
    {
        DPC_ObjectDetection_ExecuteResultExportedInfo *inp;
        volatile uint32_t startTime;

        startTime = CycleCounterP_getCount32();

        DebugP_assert(argLen == sizeof(DPC_ObjectDetection_ExecuteResultExportedInfo));

        inp = (DPC_ObjectDetection_ExecuteResultExportedInfo *)arg;

        /* input sub-frame index must match current sub-frame index */
        DebugP_assert(inp->subFrameIdx == objDetObj->subFrameIndx);

        /* Reconfigure all DPUs resources for next sub-frame as all HWA and EDMA
         * resources overlap across sub-frames */
        if (objDetObj->commonCfg.numSubFrames > 1)
        {
            /* Next sub-frame */
            objDetObj->subFrameIndx++;
            if (objDetObj->subFrameIndx == objDetObj->commonCfg.numSubFrames)
            {
                objDetObj->subFrameIndx = 0;
            }
        }

        subFrmObj = &objDetObj->subFrameObj[objDetObj->subFrameIndx];


        objDetObj->stats.subFramePreparationCycles =
            CycleCounterP_getCount32() - startTime;

        /* mark end of processing of the frame/sub-frame by the DPC and the app */
        objDetObj->interSubFrameProcToken--;

        gTimingInfo.resEndTimes[gTimingInfo.resEndCnt % OBJECTDETHWA_NUM_FRAME_TIMING_TO_STORE] = CycleCounterP_getCount32();
        gTimingInfo.resEndCnt++;

        /* Perform DPM_notifyExecute only if the next frame was received before while
           the processing of the previous frame was not complete, i.e., the frame start
           ISR for the next frame was entered before the result exported ioctl was received
           for the previous frame. Otherwise, perform DPM_notifyExecute in the frame start
           ISR. */
        objDetObj->numTimesResultExported++;

        if (objDetObj->numTimesResultExported == objDetObj->stats.subframeStartIntCounter - 1)
        {
            DebugP_assert (DPM_notifyExecute (objDetObj->dpmHandle, handle) == 0);
        }
        else
        {
            // Do Nothing
        }

    }
    else
    {
        uint8_t subFrameNum;

        /* First argument is sub-frame number */
        DebugP_assert(arg != NULL);
        subFrameNum = *(uint8_t *)arg;
        subFrmObj = &objDetObj->subFrameObj[subFrameNum];

        switch (cmd)
        {
            /* Related to pre-start configuration */
            case DPC_OBJDET_IOCTL__STATIC_PRE_START_CFG:
            {
                DPC_ObjectDetection_PreStartCfg *cfg;
                DPC_ObjectDetection_DPC_IOCTL_preStartCfg_memUsage *memUsage;
                HeapP_MemStats statsStart;
                HeapP_MemStats statsEnd;

                /* Pre-start common config must be received before pre-start configs
                 * are received. */
                if (objDetObj->isCommonCfgReceived == false)
                {
                    retVal = DPC_OBJECTDETECTION_PRE_START_CONFIG_BEFORE_PRE_START_COMMON_CONFIG;
                    goto exit;
                }

                DebugP_assert(argLen == sizeof(DPC_ObjectDetection_PreStartCfg));

                /* Get system heap size before preStart configuration */
                HeapP_getHeapStats(&gObjectDetectionHeapObj, &statsStart);

                cfg = (DPC_ObjectDetection_PreStartCfg*)arg;

                memUsage = &cfg->memUsage;
                memUsage->L3RamTotal = objDetObj->L3RamObj.cfg.size;
                memUsage->CoreLocalRamTotal = objDetObj->CoreLocalRamObj.cfg.size;
                retVal = DPC_ObjDet_preStartConfig(subFrmObj,
                             &objDetObj->commonCfg, &cfg->staticCfg,
                             &objDetObj->edmaHandle[0],
                             &objDetObj->L3RamObj,
                             &objDetObj->CoreLocalRamObj,
                             &objDetObj->hwaMemBankAddr[0],
                             objDetObj->hwaMemBankSize,
                             &memUsage->L3RamUsage,
                             &memUsage->CoreLocalRamUsage,
                             objDetObj);
                if (retVal != 0)
                {
                    goto exit;
                }

                /* Get system heap size after preStart configuration */
                HeapP_getHeapStats(&gObjectDetectionHeapObj, &statsEnd);

                /* Populate system heap usage */
                memUsage->SystemHeapTotal = OBJECTDETECTION_HEAP_MEM_SIZE;
                memUsage->SystemHeapUsed = OBJECTDETECTION_HEAP_MEM_SIZE - statsEnd.availableHeapSpaceInBytes;
                memUsage->SystemHeapDPCUsed = statsStart.availableHeapSpaceInBytes - statsEnd.availableHeapSpaceInBytes;


                DebugP_logInfo("ObjDet DPC: Pre-start Config IOCTL processed (subFrameIndx = %d)\n", subFrameNum);
                break;
            }

            default:
            {
                /* Error: This is an unsupported command */
                retVal = DPC_OBJECTDETECTION_EINVAL__COMMAND;
                break;
            }
        }
    }

exit:
    return retVal;
}

/**
 *  @b Description
 *  @n
 *      DPC's (DPM registered) initialization function which is invoked by the
 *      application using DPM_init API. Among other things, this API allocates DPC instance
 *      and DPU instances (by calling DPU's init APIs) from the MemoryP osal
 *      heap. If this API returns an error of any type, the heap is not guaranteed
 *      to be in the same state as before calling the API (i.e any allocations
 *      from the heap while executing the API are not guaranteed to be deallocated
 *      in case of error), so any error from this API should be considered fatal and
 *      if the error is of _ENOMEM type, the application will
 *      have to be built again with a bigger heap size to address the problem.
 *
 *  @param[in]  dpmHandle   DPM's DPC handle
 *  @param[in]  ptrInitCfg  Handle to the framework semaphore
 *  @param[out] errCode     Error code populated on error
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static DPM_DPCHandle DPC_ObjectDetection_init(
    DPM_Handle          dpmHandle,
    DPM_InitCfg*        ptrInitCfg,
    int32_t *errCode)
{
    int32_t i;
    volatile uint8_t subFrameIdx=0U;
    ObjDetObj     *objDetObj = NULL;
    SubFrameObj   *subFrmObj = NULL;
    DPC_ObjectDetection_InitParams *dpcInitParams;
    DPU_RangeProcHWA_InitParams rangeInitParams;
    DPU_DopplerProcHWA_InitParams dopplerInitParams;
    DPU_RangeCFARProcHWA_InitParams rangeCfarInitParams;
    HWA_MemInfo         hwaMemInfo;

    *errCode = 0;

    if ((ptrInitCfg == NULL) || (ptrInitCfg->arg == NULL))
    {
        *errCode = DPC_OBJECTDETECTION_EINVAL;
        goto exit;
    }

    if (ptrInitCfg->argSize != sizeof(DPC_ObjectDetection_InitParams))
    {
        *errCode = DPC_OBJECTDETECTION_EINVAL__INIT_CFG_ARGSIZE;
        goto exit;
    }

    /* create heap for RangeProc Hwa object. */
    HeapP_construct(&gObjectDetectionHeapObj, gObjectDetectionHeapMem, OBJECTDETECTION_HEAP_MEM_SIZE);

    dpcInitParams = (DPC_ObjectDetection_InitParams *) ptrInitCfg->arg;

    objDetObj = HeapP_alloc(&gObjectDetectionHeapObj, sizeof(ObjDetObj));

#ifdef DBG_DPC_OBJDET
    gObjDetObj = objDetObj;
#endif

    if(objDetObj == NULL)
    {
        *errCode = DPC_OBJECTDETECTION_ENOMEM;
        goto exit;
    }

    /* Initialize memory */
    memset((void *)objDetObj, 0, sizeof(ObjDetObj));

    /* Copy over the DPM configuration: */
    memcpy ((void*)&objDetObj->dpmInitCfg, (void*)ptrInitCfg, sizeof(DPM_InitCfg));

    objDetObj->dpmHandle = dpmHandle;
    objDetObj->hwaHandle = dpcInitParams->hwaHandle;
    objDetObj->L3RamObj.cfg = dpcInitParams->L3ramCfg;
    objDetObj->CoreLocalRamObj.cfg = dpcInitParams->CoreLocalRamCfg;

    for(i = 0; i < EDMA_NUM_CC; i++)
    {
        objDetObj->edmaHandle[i] = dpcInitParams->edmaHandle[i];
    }

    objDetObj->processCallBackCfg = dpcInitParams->processCallBackCfg;

    /* Set HWA bank memory address */
    *errCode =  HWA_getHWAMemInfo(dpcInitParams->hwaHandle, &hwaMemInfo);
    if (*errCode != 0)
    {
        goto exit;
    }

    objDetObj->hwaMemBankSize = hwaMemInfo.bankSize;

    for (i = 0; i < hwaMemInfo.numBanks; i++)
    {
        objDetObj->hwaMemBankAddr[i] = hwaMemInfo.baseAddress +
            i * hwaMemInfo.bankSize;
    }

    rangeInitParams.hwaHandle = dpcInitParams->hwaHandle;
    dopplerInitParams.hwaHandle = dpcInitParams->hwaHandle;
    rangeCfarInitParams.hwaHandle = dpcInitParams->hwaHandle;

    for (subFrameIdx = 0; subFrameIdx < RL_MAX_SUBFRAMES; subFrameIdx++)
    {
        subFrmObj = &objDetObj->subFrameObj[subFrameIdx];
        if(subFrmObj != NULL)
        {

#ifndef DOPPLER_FILE_DATA_DEBUG
            subFrmObj->dpuRangeObj = DPU_RangeProcHWA_init(&rangeInitParams, subFrameIdx, errCode);
            if (*errCode != 0)
            {
                goto exit;
            }
#endif

            subFrmObj->dpuDopplerObj = DPU_DopplerProcHWA_init(&dopplerInitParams, subFrameIdx, errCode);
            if (*errCode != 0)
            {
                goto exit;
            }

            subFrmObj->dpuRangeCfarObj = DPU_RangeCFARProcHWA_init(&rangeCfarInitParams, subFrameIdx, errCode);
            if (*errCode != 0)
            {
                goto exit;
            }
        }
    }

exit:

    if(*errCode != 0)
    {
        if(objDetObj != NULL)
        {
            HeapP_free(&gObjectDetectionHeapObj, objDetObj);
            HeapP_destruct(&gObjectDetectionHeapObj);

            objDetObj = NULL;
        }
    }

    return ((DPM_DPCHandle)objDetObj);
}

/**
 *  @b Description
 *  @n
 *      DPC's (DPM registered) de-initialization function which is invoked by the
 *      application using DPM_deinit API.
 *
 *  @param[in]  handle  DPM's DPC handle
 *
 *  \ingroup DPC_OBJDET__INTERNAL_FUNCTION
 *
 *  @retval
 *      Success -   0
 *  @retval
 *      Error   -   <0
 */
static int32_t DPC_ObjectDetection_deinit (DPM_DPCHandle handle)
{
    ObjDetObj *objDetObj = (ObjDetObj *) handle;
    SubFrameObj   *subFrmObj;
    int32_t retVal = 0;
    int32_t i;

    if (handle == NULL)
    {
        retVal = DPC_OBJECTDETECTION_EINVAL;
        goto exit;
    }

    for(i = 0; i < RL_MAX_SUBFRAMES; i++)
    {
        subFrmObj = &objDetObj->subFrameObj[i];

        retVal = DPU_RangeProcHWA_deinit(subFrmObj->dpuRangeObj);

        if (retVal != 0)
        {
            goto exit;
        }

        retVal = DPU_DopplerProcHWA_deinit(subFrmObj->dpuDopplerObj);

        if (retVal != 0)
        {
            goto exit;
        }

        retVal = DPU_RangeCFARProcHWA_deinit(subFrmObj->dpuRangeCfarObj);

        if (retVal != 0)
        {
            goto exit;
        }

    }

    HeapP_free(&gObjectDetectionHeapObj, handle);
    HeapP_destruct(&gObjectDetectionHeapObj);
exit:

    return (retVal);
}
