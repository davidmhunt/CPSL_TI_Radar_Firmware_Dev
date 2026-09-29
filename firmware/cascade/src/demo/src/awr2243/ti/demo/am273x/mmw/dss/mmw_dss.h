/**
 *   @file  mmw_dss.h
 *
 *   @brief
 *      This is the main header file for the Millimeter Wave Demo
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
#ifndef MMW_DSS_H
#define MMW_DSS_H

#include <drivers/csirx.h>
#include <kernel/dpl/SemaphoreP.h>
#include <ti_drivers_config.h>
#include "FreeRTOS.h"
#include "task.h"


#include <ti/common/mmwave_error.h>

#ifdef MMWDEMO_TDM
#include <ti/demo/am273x/mmw/mmw_resTDM.h>
#include <ti/datapath/dpc/objectdetection/objdethwa/objectdetection.h>
#else
#include <ti/demo/am273x/mmw/mmw_resDDM.h>
#include <ti/datapath/dpc/objectdetection/objdethwaDDMA/objectdetection.h>
#endif
#include <ti/demo/am273x/mmw/include/mmw_output.h>

#ifdef LVDS_STREAM
#include <ti/demo/am273x/mmw/dss/mmw_lvds_stream.h>
#endif

/* This is used to resolve RL_MAX_SUBFRAMES */
#include <ti/control/mmwave/mmwave.h>
#include <ti/control/mmwavelink/mmwavelink.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ======================================================================== */
/*                           Macros & Typedefs                              */
/* ======================================================================== */

/* CSI Configuration related definitions */
#define MMW_DEMO_CSI2_CONTEXT                        DPC_OBJDET_CSIRX_CHIRP_DATA_CONTEXT
#define MMW_DEMO_CSI2_FORMAT                         (CSIRX_FORMAT_RAW8)
#define MMW_DEMO_CSI2_VC                             (0U)
#define MMW_DEMO_CSI2_USER_DEFINED_MAPPING           (CSIRX_USER_DEFINED_FORMAT_RAW8)
#define MMW_DEMO_CSI2_NUM_BYTES_PER_BUFFER           (4U)
#define MMW_DEMO_CSI2_TOTAL_DATA_SIZE                (2048U)
#define MMW_DEMO_CSI2_BYTES_PER_FRAME                (16U)
#define MMW_DEMO_CSI2_NUM_FRAMES                     (MMW_DEMO_CSI2_TOTAL_DATA_SIZE / \
											        MMW_DEMO_CSI2_BYTES_PER_FRAME)
#define MMW_DEMO_CSI2_NUM_INSTANCES                  (2U)
#define MMW_DEMO_CSI2_NUM_LINES                      (1U)
#define MMW_DEMO_CSI2_PAYLOAD_BYTES_PER_PING_PONG_BUFFER  (4U)

/* must be less than MMW_DEMO_CSI2_NUM_FRAMES and simpler for testing to be
 * able to divide MMW_DEMO_CSI2_NUM_FRAMES */
#define MMW_DEMO_CSI2_NUM_FRAMES_PER_PING_PONG_SWITCH 	(1U)

#define MMW_DEMO_CSI2_PING_OR_PONG_BUF_SIZE \
						(MMW_DEMO_CSI2_NUM_FRAMES_PER_PING_PONG_SWITCH * \
											MMW_DEMO_CSI2_BYTES_PER_FRAME)

#define MMW_DEMO_CSI2_COMMON_CB_ARG              (0x11112222U)
#define MMW_DEMO_CSI2_COMBINED_EOF_CB_ARG        (0x33334444U)

#define MMW_DEMO_CSI2_PING_PONG_ALIGNMENT CSL_MAX(CSIRX_PING_PONG_ADDRESS_LINEOFFSET_ALIGNMENT_IN_BYTES, \
                                         CSL_CACHE_L1D_LINESIZE)

#define MMW_DEMO_CSI2_PING_OR_PONG_BUF_SIZE_ALIGNED CSL_NEXT_MULTIPLE_OF(MMW_DEMO_CSI2_PING_OR_PONG_BUF_SIZE, \
                                                                MMW_DEMO_CSI2_PING_PONG_ALIGNMENT)
/**
 * @brief
 *  Millimeter Wave Demo Data Path Object
 *
 * @details
 *  The structure is used to hold all the relevant information for the
 *  Millimeter Wave demo's data path
 */
typedef struct MmwDemo_DataPathObj_t
{
    /*! @brief Handle to hardware accelerator driver. */
    HWA_Handle          hwaHandle;

    /*! @brief dpm Handle */
    DPM_Handle          objDetDpmHandle;

    /*! @brief   Handle of the EDMA driver. */
    EDMA_Handle         edmaHandle[EDMA_NUM_CC];

    /*! @brief          Processing Stats */
    MmwDemo_output_message_stats   subFrameStats[RL_MAX_SUBFRAMES];

    /*! @brief   Handle to CSI RX interface for all devices (Cascade)*/
    CSIRX_Handle         csirxHandle[MMWAVE_RADAR_DEVICES];

} MmwDemo_DataPathObj;

/**
 * @brief
 *  Millimeter Wave Demo MCB
 *
 * @details
 *  The structure is used to hold all the relevant information for the
 *  Millimeter Wave demo
 */
typedef struct MmwDemo_DSS_MCB_t
{       
    /*! @brief     DPM Handle */
    TaskHandle_t                 objDetDpmTaskHandle;

    /*! @brief     init Task Handle */
    TaskHandle_t                 initTaskHandle;
    
    /*! @brief     Frame Trigger Task Handle */
    TaskHandle_t                 objFrameTrigTaskHandle;
  
    /*! @brief     Data Path object */
    MmwDemo_DataPathObj         dataPathObj;
    
    /*! @brief   Semaphore Object to signal  FRAME TRIGGER function */
    SemaphoreP_Object           DPMTrigFrameSemHandle;

    /*! @brief   Semaphore Object to signal  CSI2RX config complete */
    SemaphoreP_Object           CSI2RXConfigCompleteSemHandle;

    /*! @brief   Semaphore Object to pend main task */
    SemaphoreP_Object           demoInitTaskCompleteSemHandle;

#ifdef LVDS_STREAM
    /*! @brief   this structure is used to hold all the relevant information
         for the mmw demo LVDS stream*/
    MmwDemo_LVDSStream_MCB       lvdsStream;
#endif

} MmwDemo_DSS_MCB;

/* CSIRX related structures */
/*! holds configuration structures of config APIs */
typedef struct Mmw_Demo_CSIRX_Config_t
{
    CSIRX_DphyConfig DPHYcfg;
    CSIRX_ComplexioConfig complexIOcfg;
    CSIRX_CommonConfig commonCfg;
    CSIRX_ContextConfig contextCfg;
} Mmw_Demo_CSIRX_Config;

/*! holds context IRQ counts */
typedef struct MmwDemo_CSIRX_ContextIRQcount_s
{
    volatile uint32_t frameEndCodeDetect;
    uint32_t lineEndCodeDetect;
} MmwDemo_CSIRX_ContextIRQcount;

/*! holds common IRQ counts */
typedef struct MmwDemo_CSIRX_CommonIRQcount_s
{
    uint32_t isOCPerror;
    uint32_t isGenericShortPacketReceive;
    uint32_t isECConeBitShortPacketErrorCorrect;
    uint32_t isECCmoreThanOneBitCannotCorrect;
    uint32_t isComplexIOerror;
    uint32_t isFIFOoverflow;
} MmwDemo_CSIRX_CommonIRQcount;

typedef struct MmwDemo_CSIRX_IRQs_s
{
    CSIRX_ContextIntr context[CONFIG_CSIRX_NUM_INSTANCES];
    CSIRX_CommonIntr common;
    CSIRX_ComplexioLanesIntr  complexIOlanes;
} MmwDemo_CSIRX_IRQs;

/* Holds callback counts for different events */
typedef struct MmwDemo_CSIRX_CallBackCounts_s
{
    uint32_t common;
    uint32_t combinedEOL;
    uint32_t combinedEOF;
    uint32_t EOF0;
    uint32_t EOF1;
    uint32_t SOF0;
    uint32_t SOF1;
    uint32_t contextEOL[CONFIG_CSIRX_NUM_INSTANCES];
} MmwDemo_CSIRX_CallBackCounts;

/* Holds CSIRX state information */
typedef struct MmwDemo_CSIRX_State_s
{
	MmwDemo_CSIRX_CommonIRQcount commonIRQcount;
    MmwDemo_CSIRX_IRQs IRQ;
    MmwDemo_CSIRX_CallBackCounts callbackCount;
    bool isReceivedPayloadCorrect;
	MmwDemo_CSIRX_ContextIRQcount contextIRQcounts[CONFIG_CSIRX_NUM_INSTANCES];
    uint32_t frameId;
    uint32_t lineId;
	uint32_t receivedBuffer;
} MmwDemo_CSIRX_State;


/**************************************************************************
 *************************** Extern Definitions ***************************
 **************************************************************************/
extern void MmwDemo_dataPathInit(MmwDemo_DataPathObj *obj);
extern void MmwDemo_dataPathOpen(MmwDemo_DataPathObj *obj);
extern void MmwDemo_dataPath_CsirxConfig(MmwDemo_DataPathObj *obj);
extern void MmwDemo_dataPathClose(MmwDemo_DataPathObj *obj);
extern void MmwDemo_csirxCommonCallback(CSIRX_Handle handle, void *arg,
                              struct CSIRX_CommonIntr_s *IRQ); 
extern void MmwDemo_csirxCombinedEOFcallback(CSIRX_Handle handle, uint32_t arg); 


/* Sensor Management Module Exported API */
extern void _MmwDemo_debugAssert(int32_t expression, const char *file, int32_t line);
#define MmwDemo_debugAssert(expression) {                                      \
                                         DebugP_assert(expression);             \
                                        }
                                        
#ifdef __cplusplus
}
#endif

#endif /* MMW_DSS_H */

