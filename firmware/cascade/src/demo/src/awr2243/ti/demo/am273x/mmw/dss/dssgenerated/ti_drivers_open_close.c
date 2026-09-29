/*
 *  Copyright (C) 2021 Texas Instruments Incorporated
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

/*
 * Auto generated file 
 */

#include "ti_drivers_open_close.h"
#include <kernel/dpl/DebugP.h>

void Drivers_open(void)
{

    Drivers_edmaOpen();
    Drivers_csirxOpen();
}

void Drivers_close(void)
{
    Drivers_csirxClose();
    Drivers_edmaClose();
}

/*
 * CSIRX
 */

/* CSIRX Driver handles */
CSIRX_Handle gCsirxHandle[CONFIG_CSIRX_NUM_INSTANCES];

/* CSIRX flag to control phy init during driver open */
bool gCsirxPhyEnable[CONFIG_CSIRX_NUM_INSTANCES] = {
    false,
    false,
};

/* CSIRX Dphy config */
CSIRX_DphyConfig gCsirxDphyConfig[CONFIG_CSIRX_NUM_INSTANCES] = {
    { /* CONFIG_CSIRX0 */
        .ddrClockInHz = 300000000U,
        .isClockMissingDetectionEnabled = true,
        .triggerEscapeCode = { 0, 0, 0, 0 },
    },
    { /* CONFIG_CSIRX1 */
        .ddrClockInHz = 300000000U,
        .isClockMissingDetectionEnabled = true,
        .triggerEscapeCode = { 0, 0, 0, 0 },
    },
};

/* CSIRX complex IO config */
CSIRX_ComplexioConfig gCsirxComplexioConfig[CONFIG_CSIRX_NUM_INSTANCES] = {
    { /* CONFIG_CSIRX0 */
        .lanesConfig =
        {
            .dataLane =
            {
                {
                    .polarity = CSIRX_LANE_POLARITY_PLUS_MINUS,
                    .position = CSIRX_LANE_POSITION_1,
                },
                {
                    .polarity = CSIRX_LANE_POLARITY_PLUS_MINUS,
                    .position = CSIRX_LANE_POSITION_2,
                },
                {
                    .polarity = CSIRX_LANE_POLARITY_PLUS_MINUS,
                    .position = CSIRX_LANE_POSITION_4,
                },
                {
                    .polarity = CSIRX_LANE_POLARITY_PLUS_MINUS,
                    .position = CSIRX_LANE_POSITION_5,
                },
            },
            .clockLane =
            {
                .polarity = CSIRX_LANE_POLARITY_PLUS_MINUS,
                .position = CSIRX_LANE_POSITION_3,
            },
        },
        .enableIntr =
        {
            .isAllLanesEnterULPM = true,
            .isAllLanesExitULPM = true,
            .dataLane =
            {
                {
                    .isStateTransitionToULPM = true,
                    .isControlError = true,
                    .isEscapeEntryError = true,
                    .isStartOfTransmissionSyncError = true,
                    .isStartOfTransmissionError = true,
                },
                {
                    .isStateTransitionToULPM = true,
                    .isControlError = true,
                    .isEscapeEntryError = true,
                    .isStartOfTransmissionSyncError = true,
                    .isStartOfTransmissionError = true,
                },
                {
                    .isStateTransitionToULPM = true,
                    .isControlError = true,
                    .isEscapeEntryError = true,
                    .isStartOfTransmissionSyncError = true,
                    .isStartOfTransmissionError = true,
                },
                {
                    .isStateTransitionToULPM = true,
                    .isControlError = true,
                    .isEscapeEntryError = true,
                    .isStartOfTransmissionSyncError = true,
                    .isStartOfTransmissionError = true,
                },
            },
            .clockLane =
            {
                .isStateTransitionToULPM = true,
                .isControlError = true,
                .isEscapeEntryError = true,
                .isStartOfTransmissionSyncError = true,
                .isStartOfTransmissionError = true,
            },
        },
        .isPowerAuto = false,
    },
    { /* CONFIG_CSIRX1 */
        .lanesConfig =
        {
            .dataLane =
            {
                {
                    .polarity = CSIRX_LANE_POLARITY_PLUS_MINUS,
                    .position = CSIRX_LANE_POSITION_1,
                },
                {
                    .polarity = CSIRX_LANE_POLARITY_PLUS_MINUS,
                    .position = CSIRX_LANE_POSITION_2,
                },
                {
                    .polarity = CSIRX_LANE_POLARITY_PLUS_MINUS,
                    .position = CSIRX_LANE_POSITION_4,
                },
                {
                    .polarity = CSIRX_LANE_POLARITY_PLUS_MINUS,
                    .position = CSIRX_LANE_POSITION_5,
                },
            },
            .clockLane =
            {
                .polarity = CSIRX_LANE_POLARITY_PLUS_MINUS,
                .position = CSIRX_LANE_POSITION_3,
            },
        },
        .enableIntr =
        {
            .isAllLanesEnterULPM = true,
            .isAllLanesExitULPM = true,
            .dataLane =
            {
                {
                    .isStateTransitionToULPM = true,
                    .isControlError = true,
                    .isEscapeEntryError = true,
                    .isStartOfTransmissionSyncError = true,
                    .isStartOfTransmissionError = true,
                },
                {
                    .isStateTransitionToULPM = true,
                    .isControlError = true,
                    .isEscapeEntryError = true,
                    .isStartOfTransmissionSyncError = true,
                    .isStartOfTransmissionError = true,
                },
                {
                    .isStateTransitionToULPM = true,
                    .isControlError = true,
                    .isEscapeEntryError = true,
                    .isStartOfTransmissionSyncError = true,
                    .isStartOfTransmissionError = true,
                },
                {
                    .isStateTransitionToULPM = true,
                    .isControlError = true,
                    .isEscapeEntryError = true,
                    .isStartOfTransmissionSyncError = true,
                    .isStartOfTransmissionError = true,
                },
            },
            .clockLane =
            {
                .isStateTransitionToULPM = true,
                .isControlError = true,
                .isEscapeEntryError = true,
                .isStartOfTransmissionSyncError = true,
                .isStartOfTransmissionError = true,
            },
        },
        .isPowerAuto = false,
    },
};

/* Callbacks */

/* Callbacks for CONFIG_CSIRX0 */
void MmwDemo_combinedEOLcallback(CSIRX_Handle handle, void *arg);
void MmwDemo_csirxCommonCallback(CSIRX_Handle handle, void *arg, struct CSIRX_CommonIntr_s *irq);
void mmwDemo_DPC_ObjectDetection_csirxSOF0callback(CSIRX_Handle handle, void *arg, uint8_t contextId);

/* Callbacks Args for CONFIG_CSIRX0 */

/* Callbacks for CONFIG_CSIRX1 */
void MmwDemo_combinedEOLcallback(CSIRX_Handle handle, void *arg);
void MmwDemo_csirxCommonCallback(CSIRX_Handle handle, void *arg, struct CSIRX_CommonIntr_s *irq);
void mmwDemo_DPC_ObjectDetection_csirxSOF0callback(CSIRX_Handle handle, void *arg, uint8_t contextId);

/* Callbacks Args for CONFIG_CSIRX1 */

/* CSIRX common config */
CSIRX_CommonConfig gCsirxCommonConfig[CONFIG_CSIRX_NUM_INSTANCES] = {
    { /* CONFIG_CSIRX0 */
        .isSoftStoppingOnInterfaceDisable = true,
        .isHeaderErrorCheckEnabled = false,
        .isSignExtensionEnabled = false,
        .isBurstSizeExpand = true,
        .burstSize = CSIRX_BURST_SIZE_8X64,
        .isNonPostedWrites = true,
        .isOcpAutoIdle = true,
        .stopStateFsmTimeoutInNanoSecs = CSIRX_STOP_STATE_FSM_TIMEOUT_MAX,
        .endianness = CSIRX_ENDIANNESS_LITTLE_ENDIAN,
        .startOfFrameIntr0ContextId = 0,
        .startOfFrameIntr1ContextId = 0,
        .endOfFrameIntr0ContextId = 0,
        .endOfFrameIntr1ContextId = 0,
        .enableIntr =
        {
            .isOcpError = true,
            .isGenericShortPacketReceive = false,
            .isOneBitShortPacketErrorCorrect = false,
            .isMoreThanOneBitShortPacketErrorCannotCorrect = false,
            .isComplexioError = true,
            .isFifoOverflow = true,
            .isContextIntr = {
                true,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
            },
        },
        .intrCallbacks =
        {
            .combinedEndOfLineCallback = MmwDemo_combinedEOLcallback,
            .combinedEndOfLineCallbackArgs = NULL, /* will be set later in Drivers_csirxOpen */
            .combinedEndOfFrameCallback = NULL,
            .combinedEndOfFrameCallbackArgs = NULL, /* will be set later in Drivers_csirxOpen */
            .commonCallback = MmwDemo_csirxCommonCallback,
            .commonCallbackArgs = NULL, /* will be set later in Drivers_csirxOpen */
            .startOfFrameIntr0Callback = mmwDemo_DPC_ObjectDetection_csirxSOF0callback,
            .startOfFrameIntr0CallbackArgs = NULL, /* will be set later in Drivers_csirxOpen */
            .startOfFrameIntr1Callback = NULL,
            .startOfFrameIntr1CallbackArgs = NULL, /* will be set later in Drivers_csirxOpen */
        },
    },
    { /* CONFIG_CSIRX1 */
        .isSoftStoppingOnInterfaceDisable = true,
        .isHeaderErrorCheckEnabled = false,
        .isSignExtensionEnabled = false,
        .isBurstSizeExpand = true,
        .burstSize = CSIRX_BURST_SIZE_8X64,
        .isNonPostedWrites = true,
        .isOcpAutoIdle = true,
        .stopStateFsmTimeoutInNanoSecs = CSIRX_STOP_STATE_FSM_TIMEOUT_MAX,
        .endianness = CSIRX_ENDIANNESS_LITTLE_ENDIAN,
        .startOfFrameIntr0ContextId = 0,
        .startOfFrameIntr1ContextId = 0,
        .endOfFrameIntr0ContextId = 0,
        .endOfFrameIntr1ContextId = 0,
        .enableIntr =
        {
            .isOcpError = true,
            .isGenericShortPacketReceive = false,
            .isOneBitShortPacketErrorCorrect = false,
            .isMoreThanOneBitShortPacketErrorCannotCorrect = false,
            .isComplexioError = true,
            .isFifoOverflow = true,
            .isContextIntr = {
                true,
                false,
                false,
                false,
                false,
                false,
                false,
                false,
            },
        },
        .intrCallbacks =
        {
            .combinedEndOfLineCallback = MmwDemo_combinedEOLcallback,
            .combinedEndOfLineCallbackArgs = NULL, /* will be set later in Drivers_csirxOpen */
            .combinedEndOfFrameCallback = NULL,
            .combinedEndOfFrameCallbackArgs = NULL, /* will be set later in Drivers_csirxOpen */
            .commonCallback = MmwDemo_csirxCommonCallback,
            .commonCallbackArgs = NULL, /* will be set later in Drivers_csirxOpen */
            .startOfFrameIntr0Callback = mmwDemo_DPC_ObjectDetection_csirxSOF0callback,
            .startOfFrameIntr0CallbackArgs = NULL, /* will be set later in Drivers_csirxOpen */
            .startOfFrameIntr1Callback = NULL,
            .startOfFrameIntr1CallbackArgs = NULL, /* will be set later in Drivers_csirxOpen */
        },
    },
};

/* CSIRX context configurations */

/* CSIRX CONFIG_CSIRX0 context config */
CSIRX_ContextConfig gConfigCsirx0ContextConfig[CONFIG_CSIRX0_NUM_CONTEXT] =
{
    { /* context 0 */
        .virtualChannelId = 0,
        .format = CSIRX_FORMAT_RAW8,
        .userDefinedMapping = CSIRX_USER_DEFINED_FORMAT_RAW8,
        .numFramesToAcquire = 0,
        .numLinesForIntr = 1,
        .alpha = 0,
        .isByteSwapEnabled = false,
        .isGenericEnabled = false,
        .isEndOfFramePulseEnabled = true,
        .isEndOfLinePulseEnabled = true,
        .isPayloadChecksumEnable = true,
        .isGenerateIntrEveryNumLinesForIntr = false,
        .transcodeConfig =
        {
            .transcodeFormat = CSIRX_TRANSCODE_FORMAT_NO_TRANSCODE,
            .isHorizontalDownscalingBy2Enabled = false,
            .crop =
            {
                .horizontalCount = 0,
                .horizontalSkip = 0,
                .verticalCount = 0,
                .verticalSkip = 0,
            },
        },
        .pingPongConfig =
        {
            .pingAddress = 0,
            .pongAddress = 0,
            .lineOffset = 0,
            .pingPongSwitchMode = CSIRX_PING_PONG_LINE_SWITCHING,
            .numFramesForFrameBasedPingPongSwitching = 0,
            .numLinesForLineBasedPingPongSwitching = 1,
        },
        .enableIntr =
        {
            .isNumLines = false,
            .isFramesToAcquire = false,
            .isPayloadChecksumMismatch = false,
            .isLineStartCodeDetect = true,
            .isLineEndCodeDetect = false,
            .isFrameStartCodeDetect = true,
            .isFrameEndCodeDetect = true,
            .isLongPacketOneBitErrorCorrect = false,
        },
        .eolCallback = NULL,
        .eolCallbackArgs = NULL,
    },
};

/* CSIRX CONFIG_CSIRX1 context config */
CSIRX_ContextConfig gConfigCsirx1ContextConfig[CONFIG_CSIRX1_NUM_CONTEXT] =
{
    { /* context 0 */
        .virtualChannelId = 0,
        .format = CSIRX_FORMAT_RAW8,
        .userDefinedMapping = CSIRX_USER_DEFINED_FORMAT_RAW8,
        .numFramesToAcquire = 0,
        .numLinesForIntr = 1,
        .alpha = 0,
        .isByteSwapEnabled = false,
        .isGenericEnabled = false,
        .isEndOfFramePulseEnabled = true,
        .isEndOfLinePulseEnabled = true,
        .isPayloadChecksumEnable = true,
        .isGenerateIntrEveryNumLinesForIntr = false,
        .transcodeConfig =
        {
            .transcodeFormat = CSIRX_TRANSCODE_FORMAT_NO_TRANSCODE,
            .isHorizontalDownscalingBy2Enabled = false,
            .crop =
            {
                .horizontalCount = 0,
                .horizontalSkip = 0,
                .verticalCount = 0,
                .verticalSkip = 0,
            },
        },
        .pingPongConfig =
        {
            .pingAddress = 0,
            .pongAddress = 0,
            .lineOffset = 0,
            .pingPongSwitchMode = CSIRX_PING_PONG_LINE_SWITCHING,
            .numFramesForFrameBasedPingPongSwitching = 0,
            .numLinesForLineBasedPingPongSwitching = 1,
        },
        .enableIntr =
        {
            .isNumLines = false,
            .isFramesToAcquire = false,
            .isPayloadChecksumMismatch = false,
            .isLineStartCodeDetect = true,
            .isLineEndCodeDetect = false,
            .isFrameStartCodeDetect = true,
            .isFrameEndCodeDetect = true,
            .isLongPacketOneBitErrorCorrect = false,
        },
        .eolCallback = NULL,
        .eolCallbackArgs = NULL,
    },
};

int32_t Drivers_csirxInstanceOpen(uint32_t instanceId, uint16_t numContexts, CSIRX_ContextConfig *pContextConfig)
{
    int32_t status = SystemP_SUCCESS;

    gCsirxHandle[instanceId] = CSIRX_open(CONFIG_CSIRX0);
    if(gCsirxHandle[instanceId] == NULL)
    {
        status = SystemP_FAILURE;
        DebugP_logError("CSIRX %d: CSIRX_open failed !!!\r\n", instanceId);
    }
    if(status==SystemP_SUCCESS)
    {
        status = CSIRX_reset(gCsirxHandle[instanceId]);
        if(status!=SystemP_SUCCESS)
        {
            DebugP_logError("CSIRX %d: CSIRX_reset failed !!!\r\n", instanceId);
        }
    }
    if(status==SystemP_SUCCESS && gCsirxPhyEnable[instanceId])
    {
        status = CSIRX_complexioSetConfig(gCsirxHandle[instanceId], &gCsirxComplexioConfig[instanceId]);
        if(status!=SystemP_SUCCESS)
        {
            DebugP_logError("CSIRX %d: CSIRX_complexioSetConfig failed !!!\r\n", instanceId);
        }

        if(status==SystemP_SUCCESS)
        {
            status = CSIRX_complexioDeassertReset(gCsirxHandle[instanceId]);
            if(status!=SystemP_SUCCESS)
            {
                DebugP_logError("CSIRX %d: CSIRX_complexioDeassertReset failed !!!\r\n", instanceId);
            }
        }
        if(status==SystemP_SUCCESS)
        {
            uint32_t numComplexioDonePolls = 0;
            bool isResetDone = false;

            /* Wait until complex IO reset complete */
            do
            {
                status = CSIRX_complexioIsResetDone(gCsirxHandle[instanceId], &isResetDone);
                if(status!=SystemP_SUCCESS)
                {
                    break;
                }
                ClockP_usleep(1000);
                numComplexioDonePolls++;
            } while(( isResetDone == false) && (numComplexioDonePolls < 5U) );

            if(isResetDone == false)
            {
                status = SystemP_FAILURE;
            }
            if(status!=SystemP_SUCCESS)
            {
                DebugP_logError("CSIRX %d: CSIRX_complexioIsResetDone failed !!!\r\n", instanceId);
            }
        }
        if(status==SystemP_SUCCESS)
        {
            status = CSIRX_dphySetConfig(gCsirxHandle[instanceId], &gCsirxDphyConfig[instanceId]);
            if(status!=SystemP_SUCCESS)
            {
                DebugP_logError("CSIRX %d: CSIRX_dphySetConfig failed !!!\r\n", instanceId);
            }
        }
    }
    if(status==SystemP_SUCCESS)
    {
        status = CSIRX_commonSetConfig(gCsirxHandle[instanceId], &gCsirxCommonConfig[instanceId]);
        if(status!=SystemP_SUCCESS)
        {
            DebugP_logError("CSIRX %d: CSIRX_commonSetConfig failed !!!\r\n", instanceId);
        }
    }
    if(status==SystemP_SUCCESS && gCsirxPhyEnable[instanceId])
    {
        status = CSIRX_complexioAssertForceRxModeOn(gCsirxHandle[instanceId]);
        if(status!=SystemP_SUCCESS)
        {
            DebugP_logError("CSIRX %d: CSIRX_complexioAssertForceRxModeOn failed !!!\r\n", instanceId);
        }

        if(status==SystemP_SUCCESS)
        {
            uint32_t numForceRxModeDeassertedPolls = 0;
            bool isForceRxModeDeasserted = false;

            /* wait until force rx mode deasserted: This may depend on Tx */
            do
            {
                status = CSIRX_complexioIsDeassertForceRxModeOn(gCsirxHandle[instanceId],
                                                                    &isForceRxModeDeasserted);
                if(status != SystemP_SUCCESS)
                {
                    break;
                }
                ClockP_usleep(1000);
                numForceRxModeDeassertedPolls++;
            } while( (isForceRxModeDeasserted == false) && (numForceRxModeDeassertedPolls < 5) );

            if(isForceRxModeDeasserted == false)
            {
                status = SystemP_SUCCESS;
            }
            if(status!=SystemP_SUCCESS)
            {
                DebugP_logError("CSIRX %d: CSIRX_complexioIsDeassertForceRxModeOn failed !!!\r\n", instanceId);
            }
        }
    }
    if(status==SystemP_SUCCESS)
    {
        /* enable interface */
        status = CSIRX_commonEnable(gCsirxHandle[instanceId]);
        if(status!=SystemP_SUCCESS)
        {
            DebugP_logError("CSIRX %d: CSIRX_commonEnable failed !!!\r\n", instanceId);
        }
    }
    if(status==SystemP_SUCCESS && gCsirxPhyEnable[instanceId])
    {
        /* Power on complex IO */
        status = CSIRX_complexioPowerOn(gCsirxHandle[instanceId]);
        if(status!=SystemP_SUCCESS)
        {
            DebugP_logError("CSIRX %d: CSIRX_complexioPowerOn failed !!!\r\n", instanceId);
        }

        if(status == SystemP_SUCCESS)
        {
            uint32_t numComplexioPowerStatusPolls = 0;
            uint8_t powerStatus = 0;

            /* Wait until complex IO powered up */
            numComplexioPowerStatusPolls = 0;
            do
            {
                status = CSIRX_complexioGetPowerStatus(gCsirxHandle[instanceId], &powerStatus);
                if(status != SystemP_SUCCESS)
                {
                    break;
                }
                ClockP_usleep(1000);
                numComplexioPowerStatusPolls++;
            } while((powerStatus != CSIRX_COMPLEXIO_POWER_STATUS_ON) &&
                    (numComplexioPowerStatusPolls < 5) );

            if(powerStatus != CSIRX_COMPLEXIO_POWER_STATUS_ON)
            {
                status = SystemP_FAILURE;
            }
            if(status!=SystemP_SUCCESS)
            {
                DebugP_logError("CSIRX %d: CSIRX_complexioGetPowerStatus failed !!!\r\n", instanceId);
            }
        }
    }
    if(status==SystemP_SUCCESS)
    {
        uint32_t i;

        for(i = 0; i < numContexts; i++)
        {
            /* config contexts */
            status = CSIRX_contextSetConfig(gCsirxHandle[instanceId], i, &pContextConfig[i] );
            if(status!=SystemP_SUCCESS)
            {
                DebugP_logError("CSIRX %d: CSIRX_contextSetConfig for context %d failed !!!\r\n", instanceId, i);
            }
            if(status != SystemP_SUCCESS)
            {
                break;
            }
        }
    }
    if(status==SystemP_SUCCESS)
    {
        /* Debug mode, first flush FIFO - disable debug mode and enable interface */
        status = CSIRX_debugModeDisable(gCsirxHandle[instanceId]);
        if(status!=SystemP_SUCCESS)
        {
            DebugP_logError("CSIRX %d: CSIRX_debugModeDisable failed !!!\r\n", instanceId);
        }
    }
    if(status==SystemP_SUCCESS)
    {
        /* enable interface */
        status = CSIRX_commonEnable(gCsirxHandle[instanceId]);
        if(status!=SystemP_SUCCESS)
        {
            DebugP_logError("CSIRX %d: CSIRX_commonEnable failed !!!\r\n", instanceId);
        }
    }
    return status;
}

int32_t Drivers_csirxInstanceClose(uint32_t instanceId, uint16_t numContexts)
{
    uint32_t i;
    int32_t status = SystemP_SUCCESS;

    CSIRX_debugModeDisable(gCsirxHandle[instanceId]);

    for(i = 0; i < numContexts; i++)
    {
        /* enable context */
        status = CSIRX_contextDisable(gCsirxHandle[instanceId], i);
        if(status!=SystemP_SUCCESS)
        {
            DebugP_logError("CSIRX %d: CSIRX_contextDisable for context %d failed !!!\r\n", instanceId, i);
        }
    }
    if(status==SystemP_SUCCESS)
    {
        /* enable interface */
        status = CSIRX_commonDisable(gCsirxHandle[instanceId]);
        if(status!=SystemP_SUCCESS)
        {
            DebugP_logError("CSIRX %d: CSIRX_commonDisable failed !!!\r\n", instanceId);
        }
    }
    if(status==SystemP_SUCCESS)
    {
        status = CSIRX_close(gCsirxHandle[instanceId]);
        if(status!=SystemP_SUCCESS)
        {
            DebugP_logError("CSIRX %d: CSIRX_close failed !!!\r\n", instanceId);
        }
    }
    return status;
}

int32_t Drivers_csirxOpen()
{
    int32_t status = SystemP_SUCCESS;


    return status;
}

int32_t Drivers_csirxClose()
{
    int32_t status = SystemP_SUCCESS;

    return status;
}

/*
 * EDMA
 */
/* EDMA Driver handles */
EDMA_Handle gEdmaHandle[CONFIG_EDMA_NUM_INSTANCES];

/* EDMA Driver Open Parameters */
EDMA_Params gEdmaParams[CONFIG_EDMA_NUM_INSTANCES] =
{
    {
        .intrEnable = TRUE,
    },
    {
        .intrEnable = TRUE,
    },
};

void Drivers_edmaOpen(void)
{
    uint32_t instCnt;
    int32_t  status = SystemP_SUCCESS;

    for(instCnt = 0U; instCnt < CONFIG_EDMA_NUM_INSTANCES; instCnt++)
    {
        gEdmaHandle[instCnt] = NULL;   /* Init to NULL so that we can exit gracefully */
    }

    /* Open all instances */
    for(instCnt = 0U; instCnt < CONFIG_EDMA_NUM_INSTANCES; instCnt++)
    {
        gEdmaHandle[instCnt] = EDMA_open(instCnt, &gEdmaParams[instCnt]);
        if(NULL == gEdmaHandle[instCnt])
        {
            DebugP_logError("EDMA open failed for instance %d !!!\r\n", instCnt);
            status = SystemP_FAILURE;
            break;
        }
    }

    if(SystemP_FAILURE == status)
    {
        Drivers_edmaClose();   /* Exit gracefully */
    }

    return;
}

void Drivers_edmaClose(void)
{
    uint32_t instCnt;

    /* Close all instances that are open */
    for(instCnt = 0U; instCnt < CONFIG_EDMA_NUM_INSTANCES; instCnt++)
    {
        if(gEdmaHandle[instCnt] != NULL)
        {
            EDMA_close(gEdmaHandle[instCnt]);
            gEdmaHandle[instCnt] = NULL;
        }
    }

    return;
}

