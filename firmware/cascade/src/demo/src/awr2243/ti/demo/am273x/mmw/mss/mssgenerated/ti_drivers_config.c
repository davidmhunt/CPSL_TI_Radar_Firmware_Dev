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

#include "ti_drivers_config.h"

/*
 * QSPI
 */


/* QSPI attributes */
static QSPI_Attrs gQspiAttrs[CONFIG_QSPI_NUM_INSTANCES] =
{
    {
        .baseAddr             = CSL_MSS_QSPI_U_BASE,
        .memMapBaseAddr       = CSL_EXT_FLASH_U_BASE,
        .inputClkFreq         = 80000000U,
        .intrNum              = 35U,
        .intrEnable           = FALSE,
        .dmaEnable            = FALSE,
        .intrPriority         = 4U,
        .rxLines              = QSPI_RX_LINES_QUAD,
        .chipSelect           = QSPI_CS0,
        .csPol                = QSPI_CS_POL_ACTIVE_LOW,
        .dataDelay            = QSPI_DATA_DELAY_0,
        .frmFmt               = QSPI_FF_POL0_PHA0,
        .wrdLen               = 8,
        .baudRateDiv          = 0,
    },
};
/* QSPI objects - initialized by the driver */
static QSPI_Object gQspiObjects[CONFIG_QSPI_NUM_INSTANCES];
/* QSPI driver configuration */
QSPI_Config gQspiConfig[CONFIG_QSPI_NUM_INSTANCES] =
{
    {
        &gQspiAttrs[CONFIG_QSPI0],
        &gQspiObjects[CONFIG_QSPI0],
    },
};

uint32_t gQspiConfigNum = CONFIG_QSPI_NUM_INSTANCES;

/*
 * EDMA
 */
/* EDMA atrributes */
static EDMA_Attrs gEdmaAttrs[CONFIG_EDMA_NUM_INSTANCES] =
{
    {

        .baseAddr           = CSL_RCSS_TPCC_A_U_BASE,
        .compIntrNumber     = CSL_MSS_INTR_RCSS_TPCC_A_INTAGG,
        .intrAggEnableAddr  = CSL_RCSS_CTRL_U_BASE + CSL_RCSS_CTRL_RCSS_TPCC_A_INTAGG_MASK,
        .intrAggEnableMask  = 0x1FF & (~(2U << 2)),
        .intrAggStatusAddr  = CSL_RCSS_CTRL_U_BASE + CSL_RCSS_CTRL_RCSS_TPCC_A_INTAGG_STATUS,
        .intrAggClearMask   = (2U << 2),
        .initPrms           =
        {
            .regionId     = 2,
            .queNum       = 0,
            .initParamSet = FALSE,
            .ownResource    =
            {
                .qdmaCh      = 0x30U,
                .dmaCh[0]    = 0xFFFFFFFFU,
                .dmaCh[1]    = 0x00000001U,
                .tcc[0]      = 0xFFFFFFFFU,
                .tcc[1]      = 0x00000001U,
                .paramSet[0] = 0x00000000U,
                .paramSet[1] = 0x00000000U,
                .paramSet[2] = 0xFFFFFFFFU,
                .paramSet[3] = 0x00000000U,
            },
            .reservedDmaCh[0]    = 0x00000FFFU,
            .reservedDmaCh[1]    = 0x00000000U,
        },
    },
    {

        .baseAddr           = CSL_MSS_TPCC_A_U_BASE,
        .compIntrNumber     = CSL_MSS_INTR_MSS_TPCC_A_INTAGG,
        .intrAggEnableAddr  = CSL_MSS_CTRL_U_BASE + CSL_MSS_CTRL_MSS_TPCC_A_INTAGG_MASK,
        .intrAggEnableMask  = 0x1FF & (~(2U << 2)),
        .intrAggStatusAddr  = CSL_MSS_CTRL_U_BASE + CSL_MSS_CTRL_MSS_TPCC_A_INTAGG_STATUS,
        .intrAggClearMask   = (2U << 2),
        .initPrms           =
        {
            .regionId     = 2,
            .queNum       = 0,
            .initParamSet = FALSE,
            .ownResource    =
            {
                .qdmaCh      = 0x3FU,
                .dmaCh[0]    = 0xFFFFFFFFU,
                .dmaCh[1]    = 0x00FFFFFFU,
                .tcc[0]      = 0xFFFFFFFFU,
                .tcc[1]      = 0x00FFFFFFU,
                .paramSet[0] = 0xFFFFFFFFU,
                .paramSet[1] = 0xFFFFFFFFU,
                .paramSet[2] = 0xFFFFFFFFU,
                .paramSet[3] = 0x00FFFFFFU,
            },
            .reservedDmaCh[0]    = 0x00000001U,
            .reservedDmaCh[1]    = 0x00000000U,
        },
    },
};

/* EDMA objects - initialized by the driver */
static EDMA_Object gEdmaObjects[CONFIG_EDMA_NUM_INSTANCES];
/* EDMA driver configuration */
EDMA_Config gEdmaConfig[CONFIG_EDMA_NUM_INSTANCES] =
{
    {
        &gEdmaAttrs[CONFIG_EDMA0],
        &gEdmaObjects[CONFIG_EDMA0],
    },
    {
        &gEdmaAttrs[CONFIG_EDMA1],
        &gEdmaObjects[CONFIG_EDMA1],
    },
};

uint32_t gEdmaConfigNum = CONFIG_EDMA_NUM_INSTANCES;

/*
 * IPC Notify
 */
#include <drivers/ipc_notify.h>
#include <drivers/ipc_notify/v1/ipc_notify_v1.h>

/* this function is called within IpcNotify_init, this function returns core specific IPC config */
void IpcNotify_getConfig(IpcNotify_InterruptConfig **interruptConfig, uint32_t *interruptConfigNum)
{
    /* extern globals that are specific to this core */
    extern IpcNotify_InterruptConfig gIpcNotifyInterruptConfig_r5fss0_0[];
    extern uint32_t gIpcNotifyInterruptConfigNum_r5fss0_0;

    *interruptConfig = &gIpcNotifyInterruptConfig_r5fss0_0[0];
    *interruptConfigNum = gIpcNotifyInterruptConfigNum_r5fss0_0;
}

/*
 * IPC RP Message
 */
#include <drivers/ipc_rpmsg.h>

/* Number of CPUs that are enabled for IPC RPMessage */
#define IPC_RPMESSAGE_NUM_CORES           (3U)
/* Number of VRINGs for the numner of CPUs that are enabled for IPC */
#define IPC_RPMESSAGE_NUM_VRINGS          (IPC_RPMESSAGE_NUM_CORES*(IPC_RPMESSAGE_NUM_CORES-1))
/* Number of a buffers in a VRING, i.e depth of VRING queue */
#define IPC_RPMESSAGE_NUM_VRING_BUF       (1U)
/* Max size of a buffer in a VRING */
#define IPC_RPMESSAGE_MAX_VRING_BUF_SIZE  (1152U)
/* Size of each VRING is
 *     number of buffers x ( size of each buffer + space for data structures of one buffer (32B) )
 */
#define IPC_RPMESSAGE_VRING_SIZE          RPMESSAGE_VRING_SIZE(IPC_RPMESSAGE_NUM_VRING_BUF, IPC_RPMESSAGE_MAX_VRING_BUF_SIZE)

/* VRING base address, all VRINGs are put one after other in the below region.
 *
 * IMPORTANT: Make sure of below,
 * - The section defined below should be placed at the exact same location in memory for all the CPUs
 * - The memory should be marked as non-cached for all the CPUs
 * - The section should be marked as NOLOAD in all the CPUs linker command file
 */
/* In this case gRPMessageVringMem size is 7296 bytes */
uint8_t gRPMessageVringMem[IPC_RPMESSAGE_NUM_VRINGS][IPC_RPMESSAGE_VRING_SIZE] __attribute__((aligned(128), section(".bss.ipc_vring_mem")));



/*
 * MIBSPI
 */
/* MIBSPI atrributes */
static MIBSPI_Attrs gMibspiAttrs[CONFIG_MIBSPI_NUM_INSTANCES] =
{
    {
        .mibspiInstId           = MIBSPI_INST_ID_RCSS_SPIA,
        .ptrSpiRegBase          = (CSL_mss_spiRegs *)CSL_RCSS_SPIA_U_BASE,
        .ptrMibSpiRam           = (CSL_mibspiRam   *)CSL_RCSS_SPIA_RAM_U_BASE,
        .clockSrcFreq           = 200000000U,
        .interrupt0Num          = 147U,
        .interrupt1Num          = 148U,
        .mibspiRamSize          = CSL_MIBSPIRAM_MAX_ELEMENTS,
        .numTransferGroups      = 1U,
        .numParallelModePins    = MIBSPI_FEATURE_PARALLEL_MODE_DIS,
        .featureBitMap          = MIBSPI_FEATURE_SPIENA_PIN_DIS,
        .numDmaReqLines         = 1U,
        .dmaReqlineCfg =
        {
            [0] =
            {
                EDMA_RCSS_TPCC_A_EVT_SPIA_DMA_REQ0,
                EDMA_RCSS_TPCC_A_EVT_SPIA_DMA_REQ1
            },
        }
    },
    {
        .mibspiInstId           = MIBSPI_INST_ID_RCSS_SPIB,
        .ptrSpiRegBase          = (CSL_mss_spiRegs *)CSL_RCSS_SPIB_U_BASE,
        .ptrMibSpiRam           = (CSL_mibspiRam   *)CSL_RCSS_SPIB_RAM_U_BASE,
        .clockSrcFreq           = 200000000U,
        .interrupt0Num          = 149U,
        .interrupt1Num          = 150U,
        .mibspiRamSize          = CSL_MIBSPIRAM_MAX_ELEMENTS,
        .numTransferGroups      = 1U,
        .numParallelModePins    = MIBSPI_FEATURE_PARALLEL_MODE_DIS,
        .featureBitMap          = MIBSPI_FEATURE_SPIENA_PIN_DIS,
        .numDmaReqLines         = 1U,
        .dmaReqlineCfg =
        {
            [0] =
            {
                EDMA_RCSS_TPCC_A_EVT_SPIB_DMA_REQ0,
                EDMA_RCSS_TPCC_A_EVT_SPIB_DMA_REQ1
            },
        }
    },
    {
        .mibspiInstId           = MIBSPI_INST_ID_MSS_SPIB,
        .ptrSpiRegBase          = (CSL_mss_spiRegs *)CSL_MSS_SPIB_U_BASE,
        .ptrMibSpiRam           = (CSL_mibspiRam   *)CSL_MSS_SPIB_RAM_U_BASE,
        .clockSrcFreq           = 200000000U,
        .interrupt0Num          = 33U,
        .interrupt1Num          = 34U,
        .mibspiRamSize          = CSL_MIBSPIRAM_MAX_ELEMENTS,
        .numTransferGroups      = 1U,
        .numParallelModePins    = MIBSPI_FEATURE_PARALLEL_MODE_DIS,
        .featureBitMap          = MIBSPI_FEATURE_SPIENA_PIN_DIS,
        .numDmaReqLines         = 1U,
        .dmaReqlineCfg =
        {
            [0] =
            {
                EDMA_MSS_TPCC_A_EVT_SPIB_DMA_REQ0,
                EDMA_MSS_TPCC_A_EVT_SPIB_DMA_REQ1
            },
        }
    },
};
/* MIBSPI objects - initialized by the driver */
static MIBSPI_Object gMibspiObjects[CONFIG_MIBSPI_NUM_INSTANCES];
/* MIBSPI driver configuration */
MIBSPI_Config gMibspiConfig[CONFIG_MIBSPI_NUM_INSTANCES] =
{
    {
        &gMibspiAttrs[CONFIG_MIBSPI0],
        &gMibspiObjects[CONFIG_MIBSPI0],
    },
    {
        &gMibspiAttrs[CONFIG_MIBSPI1],
        &gMibspiObjects[CONFIG_MIBSPI1],
    },
    {
        &gMibspiAttrs[CONFIG_MIBSPI2],
        &gMibspiObjects[CONFIG_MIBSPI2],
    },
};

uint32_t gMibspiConfigNum = CONFIG_MIBSPI_NUM_INSTANCES;

/*
 * UART
 */
#include "drivers/soc.h"

/* UART atrributes */
static UART_Attrs gUartAttrs[CONFIG_UART_NUM_INSTANCES] =
{
    {
        .baseAddr           = CSL_MSS_SCIB_U_BASE,
        .inputClkFreq       = 200000000U,
    },
    {
        .baseAddr           = CSL_MSS_SCIA_U_BASE,
        .inputClkFreq       = 200000000U,
    },
};
/* UART objects - initialized by the driver */
static UART_Object gUartObjects[CONFIG_UART_NUM_INSTANCES];
/* UART driver configuration */
UART_Config gUartConfig[CONFIG_UART_NUM_INSTANCES] =
{
    {
        &gUartAttrs[CONFIG_UART1],
        &gUartObjects[CONFIG_UART1],
    },
    {
        &gUartAttrs[CONFIG_UART0],
        &gUartObjects[CONFIG_UART0],
    },
};

uint32_t gUartConfigNum = CONFIG_UART_NUM_INSTANCES;

void Drivers_uartInit(void)
{
    uint32_t i;
    for (i=0; i<CONFIG_UART_NUM_INSTANCES; i++)
    {
        SOC_RcmPeripheralId periphID;
        if(gUartAttrs[i].baseAddr == CSL_MSS_SCIA_U_BASE) {
            periphID = SOC_RcmPeripheralId_MSS_SCIA;
        } else if (gUartAttrs[i].baseAddr == CSL_MSS_SCIB_U_BASE) {
            periphID = SOC_RcmPeripheralId_MSS_SCIB;
        } else if (gUartAttrs[i].baseAddr == CSL_DSS_SCIA_U_BASE) {
            periphID = SOC_RcmPeripheralId_DSS_SCIA;
        } else {
            continue;
        }
        gUartAttrs[i].inputClkFreq = SOC_rcmGetPeripheralClock(periphID);
    }
    UART_init();
}


void Pinmux_init(void);
void PowerClock_init(void);
void PowerClock_deinit(void);

/*
 * Common Functions
 */
void System_init(void)
{
    /* DPL init sets up address transalation unit, on some CPUs this is needed
     * to access SCICLIENT services, hence this needs to happen first
     */
    Dpl_init();

    
    /* initialize PMU */
    CycleCounterP_init(SOC_getSelfCpuClk());


    PowerClock_init();
    /* Now we can do pinmux */
    Pinmux_init();
    /* finally we initialize all peripheral drivers */
    QSPI_init();
    EDMA_init();
    /* IPC Notify */
    {
        IpcNotify_Params notifyParams;
        int32_t status;

        /* initialize parameters to default */
        IpcNotify_Params_init(&notifyParams);

        /* specify the core on which this API is called */
        notifyParams.selfCoreId = CSL_CORE_ID_R5FSS0_0;

        /* list the cores that will do IPC Notify with this core
        * Make sure to NOT list 'self' core in the list below
        */
        notifyParams.numCores = 2;
        notifyParams.coreIdList[0] = CSL_CORE_ID_R5FSS0_1;
        notifyParams.coreIdList[1] = CSL_CORE_ID_C66SS0;

        /* initialize the IPC Notify module */
        status = IpcNotify_init(&notifyParams);
        DebugP_assert(status==SystemP_SUCCESS);

    }
    /* IPC RPMessage */
    {
        RPMessage_Params rpmsgParams;
        int32_t status;

        /* initialize parameters to default */
        RPMessage_Params_init(&rpmsgParams);

        /* VRING mapping from source core to destination core, '-1' means NO VRING,
            r5fss0_0 => {"r5fss0_0":-1,"r5fss0_1":0,"c66ss0":1}
            r5fss0_1 => {"r5fss0_0":2,"r5fss0_1":-1,"c66ss0":3}
            c66ss0 => {"r5fss0_0":4,"r5fss0_1":5,"c66ss0":-1}
         */
        /* TX VRINGs */
        rpmsgParams.vringTxBaseAddr[CSL_CORE_ID_R5FSS0_1] = (uintptr_t)gRPMessageVringMem[0];
        rpmsgParams.vringTxBaseAddr[CSL_CORE_ID_C66SS0] = (uintptr_t)gRPMessageVringMem[1];
        /* RX VRINGs */
        rpmsgParams.vringRxBaseAddr[CSL_CORE_ID_R5FSS0_1] = (uintptr_t)gRPMessageVringMem[2];
        rpmsgParams.vringRxBaseAddr[CSL_CORE_ID_C66SS0] = (uintptr_t)gRPMessageVringMem[4];
        /* Other VRING properties */
        rpmsgParams.vringSize = IPC_RPMESSAGE_VRING_SIZE;
        rpmsgParams.vringNumBuf = IPC_RPMESSAGE_NUM_VRING_BUF;
        rpmsgParams.vringMsgSize = IPC_RPMESSAGE_MAX_VRING_BUF_SIZE;

        /* initialize the IPC RP Message module */
        status = RPMessage_init(&rpmsgParams);
        DebugP_assert(status==SystemP_SUCCESS);
    }

    MIBSPI_init();
    Drivers_uartInit();
}

void System_deinit(void)
{
    QSPI_deinit();
    EDMA_deinit();
    RPMessage_deInit();
    IpcNotify_deInit();

    MIBSPI_deinit();
    UART_deinit();
    PowerClock_deinit();
    Dpl_deinit();
}
