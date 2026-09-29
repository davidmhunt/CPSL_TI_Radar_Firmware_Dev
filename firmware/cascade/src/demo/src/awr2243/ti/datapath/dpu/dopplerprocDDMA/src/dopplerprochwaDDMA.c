/**
 *   @file  dopplerprochwaDDMA.c
 *
 *   @brief
 *      Implements Data path Doppler processing Unit using HWA.
 *
 *  \par
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

/* MCU+SDK Include files */
#include <kernel/dpl/SemaphoreP.h>
#include <kernel/dpl/HeapP.h>
#include <kernel/dpl/ClockP.h>
#include <kernel/dpl/CacheP.h>
#include <kernel/dpl/CycleCounterP.h>
#include <kernel/dpl/DebugP.h>

/* HWA_SOC Include files */
#include <drivers/soc.h>

/* Utils */
#include <ti/utils/mathutils/mathutils.h>

/* Data Path Include files */
#include <ti/datapath/dpedma/dpedma.h>
#include <ti/datapath/dpedma/dpedmahwa.h>
#include <ti/datapath/dpu/dopplerprocDDMA/dopplerprochwaDDMA.h>
#include <ti/datapath/dpu/dopplerprocDDMA/include/dopplerprochwaDDMAinternal.h>

#include <ti/control/mmwavelink/mmwavelink.h>
#define USE_FAST_DOPPLERPROC_DPU
/******************************
* DECOMPRESSION STAGE *********
*******************************/

#define DOPPLERPROCHWA_DDMA_DECOMP_NUM_HWA_PARAMSETS            2
#define DOPPLERPROCHWA_DDMA_BFP_DECOMP_NUM_HWA_PARAMSETS        8
#define DECOMP_PING_HWA_PARAMSET_RELATIVE_IDX       0
#define DECOMP_PONG_HWA_PARAMSET_RELATIVE_IDX       1

#define DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PING_IN  0
#define DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PING_OUT 2
#define DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PONG_IN  4
#define DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PONG_OUT 6

#define DPU_DOPPLERHWADDMA_ADDR_DECOMP_PING_IN     HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PING_IN])
#define DPU_DOPPLERHWADDMA_ADDR_DECOMP_PING_OUT    HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PING_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_DECOMP_PONG_IN     HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PONG_IN])
#define DPU_DOPPLERHWADDMA_ADDR_DECOMP_PONG_OUT    HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PONG_OUT])

/******************************
* DOPPLER STAGE ***************
*******************************/
#ifdef USE_FAST_DOPPLERPROC_DPU
#define DOPPLERPROCHWA_DDMA_DOPPLER_NUM_HWA_PARAMSETS       10
#else
#define DOPPLERPROCHWA_DDMA_DOPPLER_NUM_HWA_PARAMSETS       12
#endif
#define DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PING_IN      0
#define DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PING_OUT     DPU_DOPPLERHWADDMA_MEM_BANK_LOGABS_PING_IN
#define DPU_DOPPLERHWADDMA_MEM_BANK_LOGABS_PING_IN          4
#define DPU_DOPPLERHWADDMA_MEM_BANK_LOGABS_PING_OUT         DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PING_IN
#define DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PING_IN           0
#define DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PING_OUT          DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PING_IN
#define DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PING_IN      1
#define DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PING_OUT     0
#define DPU_DOPPLERHWADDMA_MEM_BANK_SUMTX_PING_IN           DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PING_OUT
#define DPU_DOPPLERHWADDMA_MEM_BANK_SUMTX_PING_OUT          0 /* This will actually be stored at an offset in M0 */

#define DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PONG_IN      2
#define DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PONG_OUT     DPU_DOPPLERHWADDMA_MEM_BANK_LOGABS_PONG_IN
#define DPU_DOPPLERHWADDMA_MEM_BANK_LOGABS_PONG_IN          6
#define DPU_DOPPLERHWADDMA_MEM_BANK_LOGABS_PONG_OUT         DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PONG_IN
#define DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PONG_IN           2
#define DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PONG_OUT          DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PONG_IN
#define DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PONG_IN      3
#define DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PONG_OUT     2
#define DPU_DOPPLERHWADDMA_MEM_BANK_SUMTX_PONG_IN           DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PONG_OUT
#define DPU_DOPPLERHWADDMA_MEM_BANK_SUMTX_PONG_OUT          2 /* This will actually be stored at an offset in M2 */

#define DPU_DOPPLERHWADDMA_ADDR_DOPPLERFFT_PING_IN      HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PING_IN])
#define DPU_DOPPLERHWADDMA_ADDR_DOPPLERFFT_PING_OUT     HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PING_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_LOGABS_PING_IN          HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_LOGABS_PING_IN])
#define DPU_DOPPLERHWADDMA_ADDR_LOGABS_PING_OUT         HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_LOGABS_PING_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_SUMRX_PING_IN           HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PING_IN])
#define DPU_DOPPLERHWADDMA_ADDR_SUMRX_PING_OUT          HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PING_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_DDMAMETRIC_PING_IN      HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PING_IN])
#define DPU_DOPPLERHWADDMA_ADDR_DDMAMETRIC_PING_OUT     HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PING_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_SUMTX_PING_IN           HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_SUMTX_PING_IN])
#define DPU_DOPPLERHWADDMA_ADDR_SUMTX_PING_OUT          HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_SUMTX_PING_OUT])

#define DPU_DOPPLERHWADDMA_ADDR_DOPPLERFFT_PONG_IN      HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PONG_IN])
#define DPU_DOPPLERHWADDMA_ADDR_DOPPLERFFT_PONG_OUT     HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PONG_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_LOGABS_PONG_IN          HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_LOGABS_PONG_IN])
#define DPU_DOPPLERHWADDMA_ADDR_LOGABS_PONG_OUT         HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_LOGABS_PONG_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_SUMRX_PONG_IN           HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PONG_IN])
#define DPU_DOPPLERHWADDMA_ADDR_SUMRX_PONG_OUT          HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_SUMRX_PONG_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_DDMAMETRIC_PONG_IN      HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PONG_IN])
#define DPU_DOPPLERHWADDMA_ADDR_DDMAMETRIC_PONG_OUT     HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PONG_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_SUMTX_PONG_IN           HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_SUMTX_PONG_IN])
#define DPU_DOPPLERHWADDMA_ADDR_SUMTX_PONG_OUT          HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_SUMTX_PONG_OUT])

#ifdef USE_FAST_DOPPLERPROC_DPU
#define DPU_DOPPLERHWADDMA_DOPPLER_FFT_PING_HWA_PARAMSET_RELATIVE_IDX       1
#define DPU_DOPPLERHWADDMA_LOG_ABS_PING_HWA_PARAMSET_RELATIVE_IDX           2
#define DPU_DOPPLERHWADDMA_DDMA_METRIC_PING_HWA_PARAMSET_RELATIVE_IDX       3
#define DPU_DOPPLERHWADDMA_SUM_TX_PING_HWA_PARAMSET_RELATIVE_IDX            4

#define DPU_DOPPLERHWADDMA_DOPPLER_FFT_PONG_HWA_PARAMSET_RELATIVE_IDX       6
#define DPU_DOPPLERHWADDMA_LOG_ABS_PONG_HWA_PARAMSET_RELATIVE_IDX           7
#define DPU_DOPPLERHWADDMA_DDMA_METRIC_PONG_HWA_PARAMSET_RELATIVE_IDX       8
#define DPU_DOPPLERHWADDMA_SUM_TX_PONG_HWA_PARAMSET_RELATIVE_IDX            9

#define DPU_DOPPLERHWADDMA_DOPPLER_FFT_PONG_HWA_PARAMSET_RELATIVE_IDX_SUMTX_DISABLED       5

#else
#define DPU_DOPPLERHWADDMA_DOPPLER_FFT_PING_HWA_PARAMSET_RELATIVE_IDX       1
#define DPU_DOPPLERHWADDMA_LOG_ABS_PING_HWA_PARAMSET_RELATIVE_IDX           2
#define DPU_DOPPLERHWADDMA_SUM_RX_PING_HWA_PARAMSET_RELATIVE_IDX            3
#define DPU_DOPPLERHWADDMA_DDMA_METRIC_PING_HWA_PARAMSET_RELATIVE_IDX       4
#define DPU_DOPPLERHWADDMA_SUM_TX_PING_HWA_PARAMSET_RELATIVE_IDX            5

#define DPU_DOPPLERHWADDMA_DOPPLER_FFT_PONG_HWA_PARAMSET_RELATIVE_IDX       7
#define DPU_DOPPLERHWADDMA_LOG_ABS_PONG_HWA_PARAMSET_RELATIVE_IDX           8
#define DPU_DOPPLERHWADDMA_SUM_RX_PONG_HWA_PARAMSET_RELATIVE_IDX            9
#define DPU_DOPPLERHWADDMA_DDMA_METRIC_PONG_HWA_PARAMSET_RELATIVE_IDX       10
#define DPU_DOPPLERHWADDMA_SUM_TX_PONG_HWA_PARAMSET_RELATIVE_IDX            11

#define DPU_DOPPLERHWADDMA_DOPPLER_FFT_PONG_HWA_PARAMSET_RELATIVE_IDX_SUMTX_DISABLED       6
#endif

/******************************
* AZIM-CFAR STAGE *************
*******************************/

#define DOPPLERPROCHWA_DDMA_AZIMCFAR_NUM_HWA_PARAMSETS          10

#define DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PING_IN           0
#define DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PING_OUT          DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PING_IN
#define DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PING_IN              4
#define DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PING_OUT             0
#define DPU_DOPPLERHWADDMA_MEM_BANK_LOCALMAX_PING_IN          4
#define DPU_DOPPLERHWADDMA_MEM_BANK_LOCALMAX_PING_OUT         1

#define DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PONG_IN           2
#define DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PONG_OUT          DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PONG_IN
#define DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PONG_IN              6
#define DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PONG_OUT             2
#define DPU_DOPPLERHWADDMA_MEM_BANK_LOCALMAX_PONG_IN          6
#define DPU_DOPPLERHWADDMA_MEM_BANK_LOCALMAX_PONG_OUT         3

#define DPU_DOPPLERHWADDMA_ADDR_AZIMFFT_PING_IN           HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PING_IN])
#define DPU_DOPPLERHWADDMA_ADDR_AZIMFFT_PING_OUT          HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PING_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_CFAR_PING_IN              HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PING_IN])
#define DPU_DOPPLERHWADDMA_ADDR_CFAR_PING_OUT             HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PING_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_LOCALMAX_PING_IN          HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_LOCALMAX_PING_IN])
#define DPU_DOPPLERHWADDMA_ADDR_LOCALMAX_PING_OUT         HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_LOCALMAX_PING_OUT])

#define DPU_DOPPLERHWADDMA_ADDR_AZIMFFT_PONG_IN           HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PONG_IN])
#define DPU_DOPPLERHWADDMA_ADDR_AZIMFFT_PONG_OUT          HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PONG_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_CFAR_PONG_IN              HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PONG_IN])
#define DPU_DOPPLERHWADDMA_ADDR_CFAR_PONG_OUT             HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PONG_OUT])
#define DPU_DOPPLERHWADDMA_ADDR_LOCALMAX_PONG_IN          HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_LOCALMAX_PONG_IN])
#define DPU_DOPPLERHWADDMA_ADDR_LOCALMAX_PONG_OUT         HWADRV_ADDR_TRANSLATE_CPU_TO_HWA(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_LOCALMAX_PONG_OUT])

#define DPU_DOPPLERHWADDMA_AZIMFFT_PING_HWA_PARAMSET_RELATIVE_IDX       1
#define DPU_DOPPLERHWADDMA_CFAR_PING_HWA_PARAMSET_RELATIVE_IDX          2
#define DPU_DOPPLERHWADDMA_LOCALMAX_PING_HWA_PARAMSET_RELATIVE_IDX      3

#define DPU_DOPPLERHWADDMA_AZIMFFT_PONG_HWA_PARAMSET_RELATIVE_IDX       6
#define DPU_DOPPLERHWADDMA_CFAR_PONG_HWA_PARAMSET_RELATIVE_IDX          7
#define DPU_DOPPLERHWADDMA_LOCALMAX_PONG_HWA_PARAMSET_RELATIVE_IDX      8

#define PING 0
#define PONG 1

#define EXTRACT_BIT(n, k) ((n & ( 1 << k )) >> k)

DPU_DopplerProcHWA_Obj dopplerProcObjPool[RL_MAX_SUBFRAMES] __attribute__((aligned(HeapP_BYTE_ALIGNMENT)));

// #define DOPPLERPROCHWADDMA_DPU_TIMING
#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
#define DOPPLERPROCHWADDMA_DPU_TIMING_NUM_STAMPS_TO_STORE   (16U)
volatile uint32_t stampGlobal[DOPPLERPROCHWADDMA_DPU_TIMING_NUM_STAMPS_TO_STORE] = {0U};
void insertStamp(int32_t operation)
{
    stampGlobal[operation] = CycleCounterP_getCount32();
}
#endif

/*===========================================================
 *                    Internal Functions
 *===========================================================*/
#ifdef DOPPLER_PROC_DDMA_DPU_DEBUG
uint32_t gedmaCallback = 0;
#endif

/* IDMA Offsets */
#define IDMA1_STATUS_OFFSET     0x20100
#define IDMA1_SOURCE_OFFSET     0x20108
#define IDMA1_DEST_OFFSET       0x2010C
#define IDMA1_COUNT_OFFSET      0x20110

#ifdef CASCADE_EVM
/* Address offset on the Shuffle LUT RAM to store the virtual array mapping */
#define SHUFFLE_LUT_OFFSET 32U
#endif

/**
 *  @b Description
 *  @n
 *      EDMA completion call back function.
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 */
static void DPU_DopplerProcHWA_edmaDoneIsrCallback(Edma_IntrHandle intrHandle,
   void *args)
{
    if (args != NULL) {
        SemaphoreP_post((SemaphoreP_Object*)args);
    }
#ifdef DOPPLER_PROC_DDMA_DPU_DEBUG
    gedmaCallback++;
#endif

}

/**
 *  @b Description
 *  @n
 *      Finds max index in an array
 *
 *  @param[in] arr          - array
 *  @param[in] numSamples   - number of elements in the array (should be even)
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval max index in the array
 */
static  int32_t findMaxIdx(int32_t * restrict arr,  uint32_t numSamples,  uint32_t skipIdx)
{
    /* -------------------------------------------------------------------- */
    /*  Find the maximum and index of the input.                            */
    /* -------------------------------------------------------------------- */

    int32_t i,j;
    // Note, to really speedup the findmaxIdx function, it is recommended that
    // the variables numSamples and skipIdx be hardcoded (based on the chirp design)
    // For example, for the R79 configuration on the 2944, the following initializations
    // are valid
    //numSamples = 6;
    //skipIdx = 128;
    int32_t midPt = numSamples/2;
    uint32_t maxIdx1 = 0;
	uint32_t maxVal1 = arr[maxIdx1 * skipIdx];
	uint32_t maxIdx2 = midPt;
	uint32_t maxVal2 = arr[maxIdx2 * skipIdx];


	_nassert((int) arr % 4 == 0);
	#pragma MUST_ITERATE(1, , )
	for (i = 1; i < midPt; i++)
	{

		if(arr[i * skipIdx] > maxVal1)
		{
			maxVal1 = arr[i * skipIdx];
			maxIdx1 = i;
		}

		j = i + midPt;

		if(arr[j * skipIdx] > maxVal2)
		{
			maxVal2 = arr[j * skipIdx];
			maxIdx2 = j;
		}
	}

	if (maxVal2 > maxVal1)
		return maxIdx2;
	else
		return maxIdx1;
}

/**
 *  @b Description
 *  @n
 *      Performs DDMA Demodulation
 *
 *  @param[in] obj          - DPU obj
 *  @param[in] cfg          - DPU configuration
 *  @param[in] blockIdx     - Decompressed block index
 *  @param[in] rangeBinIdx  - Range bin index (starts from 0 for any decompressed block)
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval error code.
 */

/* Cascade Implementation */
#ifdef CASCADE_EVM
/* Rearrange Matrix for Virtual Channels  for the Azimuth FFT*/
uint8_t arrayToAntMapping[NUM_RXANT * NUM_TXANT_AZIM] __attribute__((aligned(8)))
														=   { 0, 1, 2, 3, 8, 9, 10, 16,
                                                            17, 18, 19, 24, 25, 26, 27, 32,
                                                            33, 34, 35, 4, 5, 6, 7, 12,
                                                            13, 14, 20, 21, 22, 23, 28, 29,
                                                            30, 31, 36, 37, 38, 39, 0, 0};
#endif

int32_t DPU_DopplerProcHWA_DDMADemod(DPU_DopplerProcHWA_Obj      *obj,
                                    DPU_DopplerProcHWA_Config    *cfg,
                                    uint32_t blockIdx,
                                    uint32_t rangeBinIdx)
{
    /*
    DDMA Metric Memory -  [DopplerSubBand][BandIdx]
    For example, for 4 Tx
                    S1  S2  S3  S4  S5  S6 (DDMA Metric)
    DopSubBand1
    DopSubBand2
    ..
    DopSubBandN
    -------------------------------------------------------
    Doppler FFT Memory -  [Doppler Bin][Tx][Rx]
    -------------------------------------------------------
    Pseudocode:
        for i in DopplerSubBand
            find max(DDMAMetric[i][:])
            compute rotIdx
            compute copy indices
            perform copy
    */

    uint32_t numDopplerSubBins;
    int32_t * restrict DDMAMetricMat;
    int32_t * restrict DDMAMetricSubMat;
    uint8_t * restrict dopplerFFTMat;
    uint8_t * restrict destDetSubMat;
    uint8_t * restrict dopMaxSubBandMat;
    int32_t maxIdx;
    uint32_t startIdxTxToCopy1, startIdxTxToCopy2, numTxToCopy1, numTxToCopy2;
    uint32_t bytesPerSample;
    uint8_t * restrict src;
    uint8_t * restrict dst;
    uint32_t dopSubIdx;
    int32_t retVal = 0;
    volatile uint32_t status;

    /* Ping: even rangeBinIdx; Pong: odd rangeBinIdx */
    numDopplerSubBins = obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal;
    bytesPerSample = obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample;
    DDMAMetricMat = (int32_t *) cfg->hwRes.DDMAMetricScratchBuf[rangeBinIdx%2];
    dopplerFFTMat = (uint8_t *) cfg->hwRes.dopplerFFTScratchBuf[rangeBinIdx%2];
    destDetSubMat = (uint8_t *) cfg->hwRes.dopFFTSubMat +
                                ((blockIdx * obj->decompCfg.rangeBinsPerBlock + rangeBinIdx) *
                                (cfg->staticCfg.numRxAntennas * obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample
                                 * cfg->staticCfg.numTxAntennas * numDopplerSubBins));
    dopMaxSubBandMat = (uint8_t *)cfg->hwRes.dopMaxSubBandScratchBuf[rangeBinIdx%2];

    /* Iterate over DopplerSubBins */
    for (dopSubIdx = 0; dopSubIdx < numDopplerSubBins; dopSubIdx++)
    {
        DDMAMetricSubMat = &DDMAMetricMat[dopSubIdx];

        /* Find max of DDMA Metric for the particular doppler sub band */
        maxIdx = findMaxIdx(DDMAMetricSubMat, obj->dopplerDemodCfg.numBandsTotal, numDopplerSubBins);

        dopMaxSubBandMat[dopSubIdx] = (uint8_t) maxIdx;

        /* Based on maxIdx, copy could be done in either one or two steps. Two step
        copy happens when there is a wrap-around
        First copy will be done from startIdxTxToCopy1 index for numTxToCopy1 rows.
        Second copy, if needed, will be done from 0 index for numTxToCopy2 rows.*/
        startIdxTxToCopy1 = maxIdx;
        if ((maxIdx + cfg->staticCfg.numTxAntennas - 1) >= obj->dopplerDemodCfg.numBandsTotal){
            numTxToCopy1 = obj->dopplerDemodCfg.numBandsTotal - maxIdx;
            numTxToCopy2 = cfg->staticCfg.numTxAntennas - numTxToCopy1;
        }
        else{
            numTxToCopy1 = cfg->staticCfg.numTxAntennas;
            numTxToCopy2 = 0;
        }
        startIdxTxToCopy2 = 0;

        /* Compute source and dest addresses and perform memory copies
            For the cascade devices, the rearrangement is done using the Shuffle LUT RAM */

        /* First copy */
        src = ((uint8_t *)dopplerFFTMat);
        src = src + (dopSubIdx * obj->dopplerDemodCfg.numBandsTotal + startIdxTxToCopy1) * cfg->staticCfg.numRxAntennas * bytesPerSample;

        dst = ((uint8_t *)destDetSubMat);
        dst = dst + (dopSubIdx * obj->dopplerDemodCfg.numBandsActive) * cfg->staticCfg.numRxAntennas * bytesPerSample;

        if(dopSubIdx != 0){
                while (1){
                    /* Poll IDMA status */
                    status = CSL_REG32_RD(CSL_DSP_ICFG_U_BASE + IDMA1_STATUS_OFFSET);
                    if(!status){
                        break;
                    }
                };
            }

        /* Populate IDMA configuration and trigger. The IDMA, or Internal DMA transfer 1 (IDMA1), can be used to
        transfer data between internal DSP memories. The procedure for the same is writing the Source,
        destination and transfer count in bytes, in that order. Two transfers can be queued in the IDMA
        at once, by overwriting the same registers once again. */
        CSL_REG32_WR(CSL_DSP_ICFG_U_BASE + IDMA1_SOURCE_OFFSET, (uint32_t)src);
        CSL_REG32_WR(CSL_DSP_ICFG_U_BASE + IDMA1_DEST_OFFSET, (uint32_t)dst);
        CSL_REG32_WR(CSL_DSP_ICFG_U_BASE + IDMA1_COUNT_OFFSET, (uint32_t)(cfg->staticCfg.numRxAntennas * bytesPerSample * numTxToCopy1));

        /* Second copy */
        if(numTxToCopy2 > 0){
            src = ((uint8_t *)dopplerFFTMat);
            src = src + (dopSubIdx * obj->dopplerDemodCfg.numBandsTotal + startIdxTxToCopy2) * cfg->staticCfg.numRxAntennas * bytesPerSample;

            dst = ((uint8_t *)destDetSubMat);
            dst = dst + (dopSubIdx * obj->dopplerDemodCfg.numBandsActive) * cfg->staticCfg.numRxAntennas * bytesPerSample + cfg->staticCfg.numRxAntennas * bytesPerSample * numTxToCopy1;

            /* IDMA Trigger */
            CSL_REG32_WR(CSL_DSP_ICFG_U_BASE + IDMA1_SOURCE_OFFSET, (uint32_t)src);
            CSL_REG32_WR(CSL_DSP_ICFG_U_BASE + IDMA1_DEST_OFFSET, (uint32_t)dst);
            CSL_REG32_WR(CSL_DSP_ICFG_U_BASE + IDMA1_COUNT_OFFSET, (uint32_t)(cfg->staticCfg.numRxAntennas * bytesPerSample * numTxToCopy2));
        }
    }

    /* Wait for final IDMA completion */
    while (1){
        status = CSL_REG32_RD(CSL_DSP_ICFG_U_BASE + IDMA1_STATUS_OFFSET);
        if(!status){
            break;
        }
    };


    return retVal;

}

#define MAX_NUM_OBJ_PER_RANGE_BIN (6U)
int32_t SNRList[MAX_NUM_OBJ_PER_RANGE_BIN] __attribute__((aligned(8)));
DetObjParams currRangeGateArray[MAX_NUM_OBJ_PER_RANGE_BIN];


/**
 *  @b Description
 *  @n
 *      Given an SNR, it checks if it is larger than the smallest SNR value in
 *      the SNRList array. If that is the case, it displaces the smallest value.
 *      The position of the value it displaced is returned.
 *      Using this function, we can find the largest MAX_NUM_OBJ_PER_RANGE_BIN
 *      SNRs in a very large list of SNRs.
 *
 *      SNR is expected to be positive.
 *
 *  @param[in] SNR          - SNR to check
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval 'position of the displaced SNR' or -1 if the input is smaller than all the SNRs.
 */
int32_t updateSNRList(int32_t SNR)
{
	_nassert((int) SNRList % 8 == 0);
	int32_t minSNR1 = SNRList[0];
	int32_t minSNR2 = SNRList[1];
	int32_t minLoc1 = 0;
	int32_t minLoc2 = 1;
	int32_t i, minSNR, minLoc;

	for (i = 2; i < MAX_NUM_OBJ_PER_RANGE_BIN; i+=2)
	{
		if (minSNR1 > SNRList[i])
		{
			minLoc1 = i;
			minSNR1 = SNRList[i];
		}
		if (minSNR2 > SNRList[i+1])
		{
			minLoc2 = i+1;
			minSNR2 = SNRList[i+1];
		}
	}

	if (minSNR1 > minSNR2)
	{
		minSNR = minSNR2;
		minLoc = minLoc2;
	}
	else
	{
		minSNR = minSNR1;
		minLoc = minLoc1;
	}

	if (SNR > minSNR)
	{
		SNRList[minLoc] = SNR;
		return minLoc;
	}
	else
	{
		return -1;
	}
}

/**
 *  @b Description
 *  @n
 *      Clears the SNRList Array. In order to populate the SNRlist in ascending order,
 *      the initialization parameters for the SNRlist are negative numbers in
 *      ascending order.
 *
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval none
 */
void clearSNRList()
{
	int32_t i;
	_nassert((int) SNRList % 8 == 0);
	for (i = 0; i < MAX_NUM_OBJ_PER_RANGE_BIN; i++)
	{
		SNRList[i] = -MAX_NUM_OBJ_PER_RANGE_BIN+i;
	}
}

/**
 *  @b Description
 *  @n
 *      Performs Object List creation
 *
 *  @param[in] obj          - DPU obj
 *  @param[in] cfg          - DPU configuration
 *  @param[in] blockIdx     - Decompressed block index
 *  @param[in] rangeBinIdx  - Range bin index (starts from 0 for any decompressed block)
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval error code.
 */
int32_t DPU_DopplerProcHWA_extractObjectList(DPU_DopplerProcHWA_Obj      * restrict obj,
                                            DPU_DopplerProcHWA_Config    * restrict cfg,
                                            uint32_t blockIdx,
                                            uint32_t rangeBinIdx)
{

    uint8_t * restrict azimFFTMat;
    uint8_t * restrict cfarMat;
    uint8_t * restrict localMaxMat;
    uint8_t * restrict dopSubMaxMat;
    uint8_t * restrict dopFFTMat;
	DetObjParams * restrict currObjParams;
	uint32_t i, numTxAntAzim, numTxAntElev;
    uint32_t numCfarPeaks, DopIdxCurr, AzimIdxCurr, CFARNoiseCurr, numRowsPerAzim;
    uint32_t RowIdx, BitIdx, rowVal, bit, cfarResReal, cfarResImag;
    uint32_t azimPeakSamplem1, azimPeakSample, p1Idx, azimPeakSamplep1, dopFFTMatStartIdx, dopFFTSubMatSizePing;
    int32_t m1Idx,retVal = 0;
    uint16_t cfarPeaksToLoop;
	uint32_t numDopplerBinsPerSubBand = (obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal);
	uint32_t numBytesPerRDBin, numBytesPerRDBinAzim, numBytesPerRDBinElev;
	uint32_t dopFFTBytesPerSample = obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample;
	int32_t SNR, posIdx;
	uint32_t numObjPerRangeGate = 0;
    numTxAntAzim = cfg->staticCfg.numAzimTxAntennas;
    numTxAntElev = cfg->staticCfg.numTxAntennas - numTxAntAzim;
	numBytesPerRDBin = dopFFTBytesPerSample * cfg->staticCfg.numRxAntennas * (numTxAntAzim + numTxAntElev);
#ifdef CASCADE_EVM
	/* The two chip cascade board from TI has 38 virtual antennas in the bottom row instead of 40
	 * There are 8 receivers and 5 transmitters in the bottom row. However, 2 of the
	 * virtual  antennas are coincident with 2 other virtual antennas reducing the overall number by
	 * 2. */
    numBytesPerRDBinAzim = (dopFFTBytesPerSample * cfg->staticCfg.numRxAntennas * numTxAntAzim) - 2U;
#else
    numBytesPerRDBinAzim = (dopFFTBytesPerSample * cfg->staticCfg.numRxAntennas * numTxAntAzim);
#endif
    numBytesPerRDBinElev = dopFFTBytesPerSample * cfg->staticCfg.numRxAntennas * (numTxAntElev);

    if(rangeBinIdx % 2 == 0){
        numCfarPeaks = obj->numCfarPeaksPing;
    }
    else{
        numCfarPeaks = obj->numCfarPeaksPong;
    }

    /* Assign local matrix pointers based on ping/pong */
    azimFFTMat = (uint8_t *) cfg->hwRes.azimFFTScratchBuf[rangeBinIdx%2];
    cfarMat = (uint8_t *) cfg->hwRes.cfarScratchBuf[rangeBinIdx%2];
    localMaxMat = (uint8_t *) cfg->hwRes.localMaxScratchBuf[rangeBinIdx%2];
    dopSubMaxMat = (uint8_t *) cfg->hwRes.dopMaxSubBandScratchBuf[rangeBinIdx%2];
    numRowsPerAzim = (obj->cfarAzimFFTCfg.numAzimFFTBins % 32 == 0) ?
                        (obj->cfarAzimFFTCfg.numAzimFFTBins / 32) : (obj->cfarAzimFFTCfg.numAzimFFTBins / 32) + 1;
    dopFFTSubMatSizePing = numBytesPerRDBin * numDopplerBinsPerSubBand;
    dopFFTMat = (uint8_t *) ((uint8_t *) cfg->hwRes.dopFFTSubMat + (rangeBinIdx%2) * dopFFTSubMatSizePing);

    cfarPeaksToLoop = (numCfarPeaks > cfg->hwRes.maxCfarPeaksToDetect)?(cfg->hwRes.maxCfarPeaksToDetect):(numCfarPeaks);

	if (obj->numObjOut >= cfg->staticCfg.maxNumObj)
	{
		return retVal;
	}

	/* Reinitialize the SNR list. */
	clearSNRList();

    /* Loop through CFAR peaks and check whether an object is present for a particular peak. If it is present,
       store its parameters in the output list */
    for(i = 0; i < cfarPeaksToLoop; i++)
    {
        /* To get the real and imag part */
        cfarResImag = *(uint32_t *)(&cfarMat[i * sizeof(cmplx32ImRe_t)]);
        cfarResReal = *(uint32_t *)(&cfarMat[i * sizeof(cmplx32ImRe_t) + sizeof(cmplx32ImRe_t)/2]);
        CFARNoiseCurr = cfarResImag;

        AzimIdxCurr = (cfarResReal) >> 12;
        DopIdxCurr = (cfarResReal) - (AzimIdxCurr << 12);

        if(DopIdxCurr > numDopplerBinsPerSubBand)
        {
			continue;
        }

        RowIdx = (DopIdxCurr * numRowsPerAzim) + (AzimIdxCurr / 32);
        BitIdx = AzimIdxCurr - 32 * (AzimIdxCurr / 32);

        rowVal = *(uint32_t *)(&localMaxMat[RowIdx * sizeof(uint32_t)]);
        bit = EXTRACT_BIT(rowVal, BitIdx);

		if(bit && ((obj->numObjOut + numObjPerRangeGate) < cfg->staticCfg.maxNumObj))
        {
			/* We have a common peak. Now, check to see if there is space
			 * in this range gate for this peak. It should be one of
			 * the largest MAX_NUM_OBJ_PER_RANGE_BIN peaks in the range-
			 * gate. */

			/* Extract the signal power. */
			azimPeakSample   = *(uint16_t *)(&azimFFTMat[(AzimIdxCurr + DopIdxCurr * obj->cfarAzimFFTCfg.numAzimFFTBins)
                                             * obj->cfarAzimFFTCfg.azimFFTIOCfg.output.bytesPerSample]);
            /* Compute the SNR w.r.t to the CFAR noise. */
            SNR = (int32_t) azimPeakSample - (int32_t)CFARNoiseCurr;

            /* Check if it is one of highest  MAX_NUM_OBJ_PER_RANGE_BIN SNR
             * object in the range bin. If it is, update the SNRlist with
             * that position, and return the position. */
            posIdx = updateSNRList(SNR);

            if (posIdx < 0)
			{
				/* Not among the largest. Ignore. */
				continue;
			}

			/* Object found */
            /* Place it in its alloted position. */
            currObjParams = &currRangeGateArray[posIdx];
            currObjParams->azimIdx = AzimIdxCurr;
            currObjParams->dopIdx = DopIdxCurr;
            currObjParams->rangeIdx = (blockIdx * obj->decompCfg.rangeBinsPerBlock) + rangeBinIdx;
            currObjParams->subBandIdx = (uint32_t )dopSubMaxMat[DopIdxCurr];
            currObjParams->dopIdxActual =  (DopIdxCurr + (currObjParams->subBandIdx * numDopplerBinsPerSubBand));
			if (currObjParams->dopIdxActual > obj->numDopplerBins)
			{
				currObjParams->dopIdxActual -= obj->numDopplerBins;
			}

			// Note that CFARNoise (the parameter to which the CUT is compared against) is not used anywhere else.
			currObjParams->dopCfarNoise = CFARNoiseCurr;

            dopFFTMatStartIdx = DopIdxCurr * numBytesPerRDBin;

            /* Obtain doppler FFT samples corresponding to the azimuth antenna samples.  */ //TODO: IDMA this.
            memcpy(&currObjParams->azimSamples,
                    &dopFFTMat[dopFFTMatStartIdx],
                    numBytesPerRDBinAzim);

            memcpy(&currObjParams->elevSamples,
                    &dopFFTMat[dopFFTMatStartIdx + obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample * cfg->staticCfg.numRxAntennas * (numTxAntAzim)],
                    numBytesPerRDBinElev);

            /* Obtain the azim values at and around the peak. This will
             * be used for interpolation, to get a better estimate of the
             * azimuth angle. */
			/* m1Idx is the index before the peak. */
            m1Idx = ((int32_t)AzimIdxCurr) - 1;
			if (m1Idx < 0)
			{
				m1Idx = obj->cfarAzimFFTCfg.numAzimFFTBins-1;
			}
            /* p1Idx is the index after the peak. */
            p1Idx = (AzimIdxCurr + 1);
			if (p1Idx >= obj->cfarAzimFFTCfg.numAzimFFTBins)
			{
				p1Idx = 0;
			}

            azimPeakSamplem1 = *(uint16_t *)(&azimFFTMat[(m1Idx + DopIdxCurr * obj->cfarAzimFFTCfg.numAzimFFTBins)
                                             * obj->cfarAzimFFTCfg.azimFFTIOCfg.output.bytesPerSample]);
            azimPeakSamplep1 = *(uint16_t *)(&azimFFTMat[(p1Idx + DopIdxCurr * obj->cfarAzimFFTCfg.numAzimFFTBins)
                                             * obj->cfarAzimFFTCfg.azimFFTIOCfg.output.bytesPerSample]);
            currObjParams->azimPeakSamples[0] = azimPeakSamplem1;
            currObjParams->azimPeakSamples[1] = azimPeakSample;
            currObjParams->azimPeakSamples[2] = azimPeakSamplep1;

			numObjPerRangeGate++;
			if (numObjPerRangeGate > MAX_NUM_OBJ_PER_RANGE_BIN)
			{
				numObjPerRangeGate = MAX_NUM_OBJ_PER_RANGE_BIN;
			}
        }
    }

	if (numObjPerRangeGate > 0)
	{
		memcpy(&cfg->hwRes.detObjList[obj->numObjOut], currRangeGateArray, sizeof(DetObjParams)*numObjPerRangeGate);
		obj->numObjOut+= numObjPerRangeGate;
	}

    return retVal;
}

/**
 *  @b Description
 *  @n
 *      Configures HWA for Decompression stage of Doppler processing.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval error code.
 */

static int32_t DPU_DopplerProcHWA_configHwaDecompression
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
)
{
    HWA_ParamConfig         hwaParamCfg[DOPPLERPROCHWA_DDMA_BFP_DECOMP_NUM_HWA_PARAMSETS];
    HWA_InterruptConfig     paramISRConfig;
    uint32_t                paramsetIdx = 0;
    int32_t                 errCode;
    uint8_t                 destChanPing;
    uint8_t                 hwParamsetIdx = cfg->hwRes.hwaCfg.decompStageHwaStateMachineCfg.paramSetStartIdx;
    dopplerProcHWADDMADecompressionCfg* pDPDecompParams;
    uint8_t                 rxAntIdx;

#if defined(SOC_AWR294X)
    volatile uint8_t PGVer = SOC_RCM_ES1_PG_VER;
    PGVer=SOC_rcmGetEfusePGVer();
#endif
    pDPDecompParams = &obj->decompCfg;

    memset((void*) &hwaParamCfg, 0, DOPPLERPROCHWA_DDMA_BFP_DECOMP_NUM_HWA_PARAMSETS * sizeof(HWA_ParamConfig));

    /********************************************************************************/

    /*******************************/
    /* PING DECOMPRESSION PARAMSET */
    /*******************************/
{{
    /* adcbuf not mapped, HWA is triggered after edma copy is done */
    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_DMA;
    hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;

    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_COMPRESS;

    /* ACCELMODE CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.ditherEnable = HWA_FEATURE_BIT_ENABLE;  // Enable dither to suppress quantization spurs
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.compressDecompress = HWA_CMP_DCMP_DECOMPRESS;
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.method = pDPDecompParams->compressionMethod;
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.passSelect = HWA_COMPRESS_PATHSELECT_BOTHPASSES;
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.headerEnable = HWA_FEATURE_BIT_ENABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.scaleFactorBW = 4; //log2(sample bits)

#if defined(SOC_AWR294X)
    if(PGVer == SOC_RCM_ES2_PG_VER){
        hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.selLfsr = 0;
    }
#endif
    /* SRC CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_DECOMP_PING_IN;

    hwaParamCfg[paramsetIdx].source.srcRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    hwaParamCfg[paramsetIdx].source.srcSign = HWA_SAMPLES_UNSIGNED;
    hwaParamCfg[paramsetIdx].source.srcConjugate = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].source.srcScale = 0;

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_DECOMP_PING_OUT;

    hwaParamCfg[paramsetIdx].dest.dstRealComplex = HWA_SAMPLES_FORMAT_COMPLEX;
    hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT; /* 16 bit real, 16 bit imag */
    hwaParamCfg[paramsetIdx].dest.dstSign = HWA_SAMPLES_SIGNED;
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0;

    if(pDPDecompParams->compressionMethod == HWA_COMPRESS_METHOD_BFP)
    {
        /***************************************/
        /* PING BFP DECOMPRESSION PARAMSET RX1 */
        /***************************************/

        hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.BFPMantissaBW = floor((pDPDecompParams->inputBytesPerBlock*8U - hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.scaleFactorBW)\
                                                                                /(pDPDecompParams->outputSamplesPerBlock * 2U));

        /* SRC CONFIG */
        hwaParamCfg[paramsetIdx].source.srcAcnt = pDPDecompParams->inputSamplesPerBlock - 1;
        hwaParamCfg[paramsetIdx].source.srcAIdx = pDPDecompParams->bytesPerSample;
        hwaParamCfg[paramsetIdx].source.srcBcnt = pDPDecompParams->numBlocksPerPing/pDPDecompParams->rxAntPerBlock - 1;
        hwaParamCfg[paramsetIdx].source.srcBIdx = pDPDecompParams->inputBytesPerBlock * pDPDecompParams->rxAntPerBlock;

        /* DEST CONFIG */
        hwaParamCfg[paramsetIdx].dest.dstAcnt = pDPDecompParams->outputSamplesPerBlock - 1;
        hwaParamCfg[paramsetIdx].dest.dstAIdx = pDPDecompParams->bytesPerSample * pDPDecompParams->rxAntPerBlock;
        hwaParamCfg[paramsetIdx].dest.dstBIdx = pDPDecompParams->outputBytesPerBlock * pDPDecompParams->rxAntPerBlock;

        errCode = HWA_configParamSet(obj->hwaHandle,
                            hwParamsetIdx,
                            &hwaParamCfg[paramsetIdx],NULL);
        if (errCode != 0)
        {
            goto exit;
        }

        /***************************************/
        /* PING BFP DECOMPRESSION PARAMSET RXN */
        /***************************************/
        for(rxAntIdx=1; rxAntIdx<pDPDecompParams->rxAntPerBlock; rxAntIdx++)
        {
            paramsetIdx++;
            hwParamsetIdx++;
            hwaParamCfg[paramsetIdx] = hwaParamCfg[paramsetIdx-1];
            hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
            hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_DECOMP_PING_IN + rxAntIdx*pDPDecompParams->inputBytesPerBlock;
            hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_DECOMP_PING_OUT + rxAntIdx*pDPDecompParams->bytesPerSample;
            errCode = HWA_configParamSet(obj->hwaHandle,
                                        hwParamsetIdx,
                                        &hwaParamCfg[paramsetIdx],NULL);
            if (errCode != 0)
            {
                goto exit;
            }
        }
    }

    else if(pDPDecompParams->compressionMethod == HWA_COMPRESS_METHOD_EGE)
    {
        /***********************************/
        /* PING EGE DECOMPRESSION PARAMSET */
        /***********************************/
        hwaParamCfg[paramsetIdx].accelModeArgs.compressMode.EGEKarrayLength = 3; //log2(8)

        /* SRC CONFIG */
        hwaParamCfg[paramsetIdx].source.srcAcnt = pDPDecompParams->inputSamplesPerBlock - 1;
        hwaParamCfg[paramsetIdx].source.srcAIdx = pDPDecompParams->bytesPerSample;
        hwaParamCfg[paramsetIdx].source.srcBcnt = pDPDecompParams->numBlocksPerPing - 1;
        hwaParamCfg[paramsetIdx].source.srcBIdx = pDPDecompParams->inputBytesPerBlock;

        /* DEST CONFIG */
        hwaParamCfg[paramsetIdx].dest.dstAcnt = pDPDecompParams->outputSamplesPerBlock - 1;
        hwaParamCfg[paramsetIdx].dest.dstAIdx = pDPDecompParams->bytesPerSample;
        hwaParamCfg[paramsetIdx].dest.dstBIdx = pDPDecompParams->outputBytesPerBlock;

        errCode = HWA_configParamSet(obj->hwaHandle,
                            hwParamsetIdx,
                            &hwaParamCfg[paramsetIdx],NULL);
        if (errCode != 0)
        {
        goto exit;
        }
    }

    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.decompEdmaCfg.edmaOut.pingPong[PING].channel, &destChanPing);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChanPing;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }

}}

    /*******************************/
    /* PONG DECOMPRESSION PARAMSET */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx] = hwaParamCfg[DECOMP_PING_HWA_PARAMSET_RELATIVE_IDX];

    /* adcbuf not mapped, HWA is triggered after edma copy is done */
    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_DMA;
    hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;

    /* SRC CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_DECOMP_PONG_IN;

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_DECOMP_PONG_OUT;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                hwParamsetIdx,
                                &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    if(pDPDecompParams->compressionMethod==HWA_COMPRESS_METHOD_BFP)
    {
        for(rxAntIdx=1; rxAntIdx<pDPDecompParams->rxAntPerBlock; rxAntIdx++)
        {
            paramsetIdx++;
            hwParamsetIdx++;
            hwaParamCfg[paramsetIdx] = hwaParamCfg[paramsetIdx-1];
            hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
            hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_DECOMP_PONG_IN + rxAntIdx*pDPDecompParams->inputBytesPerBlock;
            hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_DECOMP_PONG_OUT + rxAntIdx*pDPDecompParams->bytesPerSample;
            errCode = HWA_configParamSet(obj->hwaHandle,
                                        hwParamsetIdx,
                                        &hwaParamCfg[paramsetIdx],NULL);
            if (errCode != 0)
            {
                goto exit;
            }
        }
    }

    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.decompEdmaCfg.edmaOut.pingPong[PONG].channel, &destChanPing);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChanPing;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }
}}

exit:
    return(errCode);
}

/**
 *  @b Description
 *  @n
 *      Configures HWA for Doppler processing and pre-demodulation stage.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval error code.
 */
static int32_t DPU_DopplerProcHWA_configHwaDopplerFFTDDMADemod
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
)
{
    HWA_ParamConfig         hwaParamCfg[DOPPLERPROCHWA_DDMA_DOPPLER_NUM_HWA_PARAMSETS];
    uint32_t                paramsetIdx = 0;
    HWA_InterruptConfig     paramISRConfig;
    uint32_t                hwParamsetIdx = cfg->hwRes.hwaCfg.dopplerStageHwaStateMachineCfg.paramSetStartIdx;
    int32_t                 errCode = 0U;
    uint8_t                 destChan;
    uint32_t                fftSizeTemp;
    uint32_t                index;

    memset((void*) &hwaParamCfg, 0, DOPPLERPROCHWA_DDMA_DOPPLER_NUM_HWA_PARAMSETS * sizeof(HWA_ParamConfig));

    /* Disable paramset interrupts */
    for(index = 0; index < DOPPLERPROCHWA_DDMA_DOPPLER_NUM_HWA_PARAMSETS; index++)
    {
        errCode = HWA_disableParamSetInterrupt(obj->hwaHandle, index + hwParamsetIdx,
                HWA_PARAMDONE_INTERRUPT_TYPE_CPU_INTR1 | HWA_PARAMDONE_INTERRUPT_TYPE_DMA);
        if (errCode != 0)
        {
            goto exit;
        }
    }

    /***********************/
    /* PING DUMMY PARAMSET */
    /***********************/
{

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_DMA;
    hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;

    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_NONE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
}

    /*******************************/
    /* PING DOPPLER FFT PARAMSET */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;

    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubEnable = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* ACCELMODE CONFIG (FFT) */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_ENABLE;
    if(obj->numDopplerBins % 3 == 0){
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(obj->numDopplerBins/3);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_ENABLE;
    }
    else{
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(obj->numDopplerBins);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_DISABLE;
    }
#ifdef CASCADE_EVM
    /* Butterfly scaling enabled as overflow was observed in the case of cascade EVM */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.butterflyScaling = 7U;
#else
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.butterflyScaling = 0U;
#endif
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.windowEn = HWA_FEATURE_BIT_ENABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.windowStart = cfg->hwRes.hwaCfg.winRamOffset;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.winSymm = cfg->hwRes.hwaCfg.winSym;

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_LOG2_DISABLED;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_DOPPLERFFT_PING_IN;

    hwaParamCfg[paramsetIdx].source.srcAcnt = cfg->staticCfg.numChirps - 1; /* this is samples - 1 */
    hwaParamCfg[paramsetIdx].source.srcAIdx = cfg->staticCfg.numRxAntennas * obj->dopplerDemodCfg.dopplerIOCfg.input.bytesPerSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = cfg->staticCfg.numRxAntennas - 1;
    hwaParamCfg[paramsetIdx].source.srcBIdx = obj->dopplerDemodCfg.dopplerIOCfg.input.bytesPerSample;

    hwaParamCfg[paramsetIdx].source.srcRealComplex = obj->dopplerDemodCfg.dopplerIOCfg.input.isReal;

    if(obj->dopplerDemodCfg.dopplerIOCfg.input.bytesPerSample == 2 ||
        (obj->dopplerDemodCfg.dopplerIOCfg.input.bytesPerSample == 4 &&
        (!obj->dopplerDemodCfg.dopplerIOCfg.input.isReal))){
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].source.srcSign = obj->dopplerDemodCfg.dopplerIOCfg.input.isSigned;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8;

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_DOPPLERFFT_PING_OUT;

    hwaParamCfg[paramsetIdx].dest.dstAcnt = obj->numDopplerBins - 1;
    hwaParamCfg[paramsetIdx].dest.dstAIdx = cfg->staticCfg.numRxAntennas * obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample;
    hwaParamCfg[paramsetIdx].dest.dstBIdx = obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample;

    hwaParamCfg[paramsetIdx].dest.dstRealComplex = obj->dopplerDemodCfg.dopplerIOCfg.output.isReal;
    if(obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample == 2 ||
        (obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample == 4 &&
        (!obj->dopplerDemodCfg.dopplerIOCfg.output.isReal))){
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].dest.dstSign = obj->dopplerDemodCfg.dopplerIOCfg.output.isSigned;
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

}}
#ifdef USE_FAST_DOPPLERPROC_DPU

    /*******************************/
    /* PING LOG ABS SUM RX PARAMSET */
    /*******************************/

{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_CFAR;

    /* ACCELMODE CONFIG (CFAR) */
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.operMode = HWA_CFAR_OPER_MODE_LOG_INPUT_COMPLEX_LINEARCFAR; /* cfarInpMode = 0, cfarLogMode = 0, cfarAbsMode = 11b */
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.numGuardCells = 0;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.nAvgDivFactor = mathUtils_ceilLog2(cfg->staticCfg.numRxAntennas);
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.cyclicModeEn = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.nAvgMode = HWA_NOISE_AVG_MODE_CFAR_CA;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.numNoiseSamplesRight = cfg->staticCfg.numRxAntennas/2;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.numNoiseSamplesLeft = 0;//cfg->staticCfg.numRxAntennas/2;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.outputMode = HWA_CFAR_OUTPUT_MODE_I_nAVG_ALL_Q_CUT;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.cfarAdvOutMode = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.peakGroupEn = HWA_FEATURE_BIT_DISABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_LOGABS_PING_IN;

    hwaParamCfg[paramsetIdx].source.srcAcnt = cfg->staticCfg.numRxAntennas;
    hwaParamCfg[paramsetIdx].source.srcAIdx = obj->dopplerDemodCfg.logAbsIOCfg.input.bytesPerSample;
    hwaParamCfg[paramsetIdx].source.srcBIdx = cfg->staticCfg.numRxAntennas* obj->dopplerDemodCfg.logAbsIOCfg.input.bytesPerSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = obj->numDopplerBins - 1;
    hwaParamCfg[paramsetIdx].source.srcRealComplex = obj->dopplerDemodCfg.logAbsIOCfg.input.isReal;
    if(obj->dopplerDemodCfg.logAbsIOCfg.input.bytesPerSample == 2 ||
        (obj->dopplerDemodCfg.logAbsIOCfg.input.bytesPerSample == 4 &&
        (!obj->dopplerDemodCfg.logAbsIOCfg.input.isReal))){
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].source.srcSign = obj->dopplerDemodCfg.logAbsIOCfg.input.isSigned;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8;

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_SUMRX_PING_OUT;
    hwaParamCfg[paramsetIdx].dest.dstAcnt = cfg->staticCfg.numRxAntennas; // Output (sum)
    hwaParamCfg[paramsetIdx].dest.dstAIdx = obj->dopplerDemodCfg.sumRxIOCfg.output.bytesPerSample;
    hwaParamCfg[paramsetIdx].dest.dstBIdx = obj->dopplerDemodCfg.sumRxIOCfg.output.bytesPerSample;
    hwaParamCfg[paramsetIdx].dest.dstRealComplex = obj->dopplerDemodCfg.sumRxIOCfg.output.isReal;
    if(obj->dopplerDemodCfg.sumRxIOCfg.output.bytesPerSample == 2 ||
        (obj->dopplerDemodCfg.sumRxIOCfg.output.bytesPerSample == 4 &&
        (!obj->dopplerDemodCfg.sumRxIOCfg.output.isReal))){
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].dest.dstSign = obj->dopplerDemodCfg.sumRxIOCfg.output.isSigned;
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 2;//cfg->staticCfg.numRxAntennas;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
    /* The Doppler FFT EDMA Out is being triggered here instead of after the previous paramset.
    This is because the output of the previous paramset is the input to the current paramset and hence
    if we performed an EDMA transfer immediately after the previous paramset, the current paramset
    would not have been able to access the input membank. */
    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PING].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }

}}
#else

    /*******************************/
    /* PING LOG ABS PARAMSET */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;

    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubEnable = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* ACCELMODE CONFIG (FFT) */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_DISABLE;

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_LOG2_ENABLED;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_LOGABS_PING_IN;

    hwaParamCfg[paramsetIdx].source.srcAcnt = obj->numDopplerBins * cfg->staticCfg.numRxAntennas - 1; /* this is samples - 1 */
    hwaParamCfg[paramsetIdx].source.srcAIdx = obj->dopplerDemodCfg.logAbsIOCfg.input.bytesPerSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = 1 - 1;
    hwaParamCfg[paramsetIdx].source.srcBIdx = obj->numDopplerBins * cfg->staticCfg.numRxAntennas * obj->dopplerDemodCfg.logAbsIOCfg.input.bytesPerSample;

    hwaParamCfg[paramsetIdx].source.srcRealComplex = obj->dopplerDemodCfg.logAbsIOCfg.input.isReal;
    if(obj->dopplerDemodCfg.logAbsIOCfg.input.bytesPerSample == 2 ||
        (obj->dopplerDemodCfg.logAbsIOCfg.input.bytesPerSample == 4 &&
        (!obj->dopplerDemodCfg.logAbsIOCfg.input.isReal))){
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].source.srcSign = obj->dopplerDemodCfg.logAbsIOCfg.input.isSigned;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8;

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_LOGABS_PING_OUT;

    hwaParamCfg[paramsetIdx].dest.dstAcnt = obj->numDopplerBins * cfg->staticCfg.numRxAntennas - 1;
    hwaParamCfg[paramsetIdx].dest.dstAIdx = obj->dopplerDemodCfg.logAbsIOCfg.output.bytesPerSample;
    hwaParamCfg[paramsetIdx].dest.dstBIdx = obj->numDopplerBins * cfg->staticCfg.numRxAntennas * obj->dopplerDemodCfg.logAbsIOCfg.output.bytesPerSample;

    hwaParamCfg[paramsetIdx].dest.dstRealComplex = obj->dopplerDemodCfg.logAbsIOCfg.output.isReal;//HWA_SAMPLES_FORMAT_COMPLEX;
    hwaParamCfg[paramsetIdx].dest.dstSign = obj->dopplerDemodCfg.logAbsIOCfg.output.isSigned;
    if(obj->dopplerDemodCfg.logAbsIOCfg.output.bytesPerSample == 2 ||
        (obj->dopplerDemodCfg.logAbsIOCfg.output.bytesPerSample == 4 &&
        (!obj->dopplerDemodCfg.logAbsIOCfg.output.isReal))){
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    /* The Doppler FFT EDMA Out is being triggered here instead of after the previous paramset.
       This is because the output of the previous paramset is the input to the current paramset and hence
       if we performed an EDMA transfer immediately after the previous paramset, the current paramset
       would not have been able to access the input membank. */
    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PING].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }

}}

    /*******************************/
    /* PING SUM RX PARAMSET        */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;

    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubEnable = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* ACCELMODE CONFIG (FFT) */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_ENABLE;

    fftSizeTemp = mathUtils_getValidFFTSize(cfg->staticCfg.numRxAntennas);
    if(fftSizeTemp % 3 == 0){
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(fftSizeTemp/3);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_ENABLE;
    }
    else{
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(fftSizeTemp);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_DISABLE;
    }
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.butterflyScaling = 0;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.windowEn = HWA_FEATURE_BIT_DISABLE;

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_LOG2_DISABLED;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_SUMRX_PING_IN;

    hwaParamCfg[paramsetIdx].source.srcAcnt = cfg->staticCfg.numRxAntennas - 1; /* this is samples - 1 */
    hwaParamCfg[paramsetIdx].source.srcAIdx = obj->dopplerDemodCfg.sumRxIOCfg.input.bytesPerSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = obj->numDopplerBins - 1;
    hwaParamCfg[paramsetIdx].source.srcBIdx = cfg->staticCfg.numRxAntennas * obj->dopplerDemodCfg.sumRxIOCfg.input.bytesPerSample;

    hwaParamCfg[paramsetIdx].source.srcRealComplex = obj->dopplerDemodCfg.sumRxIOCfg.input.isReal;
    if(obj->dopplerDemodCfg.sumRxIOCfg.input.bytesPerSample == 2 ||
        (obj->dopplerDemodCfg.sumRxIOCfg.input.bytesPerSample == 4 &&
        (!obj->dopplerDemodCfg.sumRxIOCfg.input.isReal))){
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].source.srcSign = obj->dopplerDemodCfg.sumRxIOCfg.input.isSigned;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8;

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_SUMRX_PING_OUT;

    hwaParamCfg[paramsetIdx].dest.dstAcnt = 1 - 1;
    hwaParamCfg[paramsetIdx].dest.dstAIdx = obj->dopplerDemodCfg.sumRxIOCfg.output.bytesPerSample;
    hwaParamCfg[paramsetIdx].dest.dstBIdx = obj->dopplerDemodCfg.sumRxIOCfg.output.bytesPerSample;

    hwaParamCfg[paramsetIdx].dest.dstRealComplex = obj->dopplerDemodCfg.sumRxIOCfg.output.isReal;
    if(obj->dopplerDemodCfg.sumRxIOCfg.output.bytesPerSample == 2 ||
        (obj->dopplerDemodCfg.sumRxIOCfg.output.bytesPerSample == 4 &&
        (!obj->dopplerDemodCfg.sumRxIOCfg.output.isReal))){
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].dest.dstSign = obj->dopplerDemodCfg.sumRxIOCfg.output.isSigned;
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].dest.dstScale = mathUtils_ceilLog2(fftSizeTemp);
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
}}
#endif
    /*******************************/
    /* PING DDMA METRIC PARAMSET   */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;

    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubEnable = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* ACCELMODE CONFIG (FFT) */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_ENABLE; /* For sum calculation */
    fftSizeTemp = mathUtils_getValidFFTSize(cfg->staticCfg.numTxAntennas);
    if(fftSizeTemp % 3 == 0){
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(fftSizeTemp/3);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_ENABLE;
    }
    else{
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(fftSizeTemp);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_DISABLE;
    }

#ifdef CASCADE_EVM
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.butterflyScaling = 1;
#else
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.butterflyScaling = 0;
#endif

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.windowEn = HWA_FEATURE_BIT_DISABLE;

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_ONLY_ENABLED;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_DDMAMETRIC_PING_IN;

    hwaParamCfg[paramsetIdx].source.srcAcnt = obj->dopplerDemodCfg.numBandsActive - 1; /* this is samples - 1 */
    hwaParamCfg[paramsetIdx].source.srcAIdx = obj->dopplerDemodCfg.DDMAMetricIOCfg.input.bytesPerSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = obj->numDopplerBins - 1;
    hwaParamCfg[paramsetIdx].source.srcBIdx = obj->dopplerDemodCfg.DDMAMetricIOCfg.input.bytesPerSample;

    hwaParamCfg[paramsetIdx].source.srcRealComplex = obj->dopplerDemodCfg.DDMAMetricIOCfg.input.isReal;
    if(obj->dopplerDemodCfg.DDMAMetricIOCfg.input.bytesPerSample == 2 ||
        (obj->dopplerDemodCfg.DDMAMetricIOCfg.input.bytesPerSample == 4 &&
        (!obj->dopplerDemodCfg.DDMAMetricIOCfg.input.isReal))){
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].source.srcSign = obj->dopplerDemodCfg.DDMAMetricIOCfg.input.isSigned;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8;
    hwaParamCfg[paramsetIdx].source.shuffleMode = HWA_SRC_SHUFFLE_AB_MODE_ADIM;
    hwaParamCfg[paramsetIdx].source.shuffleStart = 0;
    hwaParamCfg[paramsetIdx].source.wrapComb = obj->numDopplerBins * obj->dopplerDemodCfg.DDMAMetricIOCfg.input.bytesPerSample;

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_DDMAMETRIC_PING_OUT;

    hwaParamCfg[paramsetIdx].dest.dstAcnt = 1 - 1;
    hwaParamCfg[paramsetIdx].dest.dstAIdx = obj->dopplerDemodCfg.DDMAMetricIOCfg.output.bytesPerSample;
    hwaParamCfg[paramsetIdx].dest.dstBIdx = obj->dopplerDemodCfg.DDMAMetricIOCfg.output.bytesPerSample;

    hwaParamCfg[paramsetIdx].dest.dstRealComplex = obj->dopplerDemodCfg.DDMAMetricIOCfg.output.isReal;
    if(obj->dopplerDemodCfg.DDMAMetricIOCfg.output.bytesPerSample == 2 ||
        (obj->dopplerDemodCfg.DDMAMetricIOCfg.output.bytesPerSample == 4 &&
        (!obj->dopplerDemodCfg.DDMAMetricIOCfg.output.isReal))){
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].dest.dstSign = obj->dopplerDemodCfg.DDMAMetricIOCfg.output.isSigned;
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[PING].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }


}}

    if(cfg->staticCfg.isSumTxEnabled){
        /*******************************/
        /* PING SUM TX PARAMSET        */
        /*******************************/
    {{

        paramsetIdx++;
        hwParamsetIdx++;

        hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_SOFTWARE; //HWA_TRIG_MODE_IMMEDIATE;
        hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;

        /* PREPROC CONFIG */
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubEnable = HWA_FEATURE_BIT_DISABLE;
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

        /* ACCELMODE CONFIG (FFT) */
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_ENABLE;

        fftSizeTemp = mathUtils_getValidFFTSize(obj->dopplerDemodCfg.numBandsTotal);
        if(fftSizeTemp % 3 == 0){
            hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(fftSizeTemp/3);
            hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_ENABLE;
        }
        else{
            hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(fftSizeTemp);
            hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_DISABLE;
        }
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.butterflyScaling = 0;
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.windowEn = HWA_FEATURE_BIT_DISABLE;

        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_ONLY_ENABLED;
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_DISABLE;

        /* SOURCE CONFIG */
        hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_SUMTX_PING_IN;

        hwaParamCfg[paramsetIdx].source.srcAcnt = obj->dopplerDemodCfg.numBandsTotal - 1; /* this is samples - 1 */
        hwaParamCfg[paramsetIdx].source.srcAIdx = obj->dopplerDemodCfg.sumTxIOCfg.input.bytesPerSample;
        hwaParamCfg[paramsetIdx].source.srcBcnt = (obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal) - 1;
        hwaParamCfg[paramsetIdx].source.srcBIdx = obj->dopplerDemodCfg.sumTxIOCfg.input.bytesPerSample;

        hwaParamCfg[paramsetIdx].source.srcRealComplex = obj->dopplerDemodCfg.sumTxIOCfg.input.isReal;
        if(obj->dopplerDemodCfg.sumTxIOCfg.input.bytesPerSample == 2 ||
            (obj->dopplerDemodCfg.sumTxIOCfg.input.bytesPerSample == 4 &&
            (!obj->dopplerDemodCfg.sumTxIOCfg.input.isReal))){
            hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
        }
        else{
            hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_32BIT;
        }
        hwaParamCfg[paramsetIdx].source.srcSign = obj->dopplerDemodCfg.sumTxIOCfg.input.isSigned;
        hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
        hwaParamCfg[paramsetIdx].source.srcScale = 8;
        hwaParamCfg[paramsetIdx].source.shuffleMode = HWA_SRC_SHUFFLE_AB_MODE_ADIM;
        hwaParamCfg[paramsetIdx].source.shuffleStart = 0;
        hwaParamCfg[paramsetIdx].source.wrapComb = obj->numDopplerBins * obj->dopplerDemodCfg.sumTxIOCfg.output.bytesPerSample;

        /* DEST CONFIG */
        hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_SUMTX_PING_OUT +
                                                obj->numDopplerBins * obj->dopplerDemodCfg.DDMAMetricIOCfg.output.bytesPerSample;

        hwaParamCfg[paramsetIdx].dest.dstAcnt = 1 - 1;
        hwaParamCfg[paramsetIdx].dest.dstAIdx = obj->dopplerDemodCfg.sumTxIOCfg.output.bytesPerSample;
        hwaParamCfg[paramsetIdx].dest.dstBIdx = obj->dopplerDemodCfg.sumTxIOCfg.output.bytesPerSample;

        hwaParamCfg[paramsetIdx].dest.dstRealComplex = obj->dopplerDemodCfg.sumTxIOCfg.output.isReal;
        if(obj->dopplerDemodCfg.sumTxIOCfg.output.bytesPerSample == 2 ||
            (obj->dopplerDemodCfg.sumTxIOCfg.output.bytesPerSample == 4 &&
            (!obj->dopplerDemodCfg.sumTxIOCfg.output.isReal))){
            hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
        }
        else{
            hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_32BIT;
        }
        hwaParamCfg[paramsetIdx].dest.dstSign = obj->dopplerDemodCfg.sumTxIOCfg.output.isSigned;
        hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE;
        hwaParamCfg[paramsetIdx].dest.dstScale = mathUtils_ceilLog2(obj->dopplerDemodCfg.numBandsTotal);
        hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0;

        errCode = HWA_configParamSet(obj->hwaHandle,
                                    hwParamsetIdx,
                                    &hwaParamCfg[paramsetIdx],NULL);
        if (errCode != 0)
        {
            goto exit;
        }

        errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[PING].channel, &destChan);
        if (errCode != 0)
        {
            goto exit;
        }
        /* enable the DMA hookup to this paramset so that data gets copied out */
        paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
        paramISRConfig.dma.dstChannel = destChan;
        errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
        if (errCode != 0)
        {
            goto exit;
        }

    }}
    }
    /***********************/
    /* PONG DUMMY PARAMSET */
    /***********************/
{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_DMA;
    hwaParamCfg[paramsetIdx].triggerSrc = hwParamsetIdx;

    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_NONE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
}

    /*******************************/
    /* PONG DOPPLER FFT PARAMSET   */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx] = hwaParamCfg[DPU_DOPPLERHWADDMA_DOPPLER_FFT_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_DOPPLERFFT_PONG_IN;
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_DOPPLERFFT_PONG_OUT;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
}}

#ifdef USE_FAST_DOPPLERPROC_DPU
    /********************************/
    /* PONG LOG ABS SUM RX PARAMSET */
    /********************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx] = hwaParamCfg[DPU_DOPPLERHWADDMA_LOG_ABS_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_LOGABS_PONG_IN;
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_SUMRX_PONG_OUT;
    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
    /* The Doppler FFT EDMA Out is being triggered here instead of after the previous paramset.
    This is because the output of the previous paramset is the input to the current paramset and hence
    if we performed an EDMA transfer immediately after the previous paramset, the current paramset
    would not have been able to access the input membank. */
    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PONG].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }


}}
#else
    /*******************************/
    /* PONG LOG ABS PARAMSET       */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx] = hwaParamCfg[DPU_DOPPLERHWADDMA_LOG_ABS_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_LOGABS_PONG_IN;
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_LOGABS_PONG_OUT;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    /* The Doppler FFT EDMA Out is being triggered here instead of after the previous paramset.
       This is because the output of the previous paramset is the input to the current paramset and hence
       if we performed an EDMA transfer immediately after the previous paramset, the current paramset
       would not have been able to access the input membank. */
    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PONG].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }

}}

    /*******************************/
    /* PONG SUM RX PARAMSET        */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx] = hwaParamCfg[DPU_DOPPLERHWADDMA_SUM_RX_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_SUMRX_PONG_IN;
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_SUMRX_PONG_OUT;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

}}
#endif
    /*******************************/
    /* PONG DDMA METRIC PARAMSET   */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx] = hwaParamCfg[DPU_DOPPLERHWADDMA_DDMA_METRIC_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_DDMAMETRIC_PONG_IN;
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_DDMAMETRIC_PONG_OUT;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[PONG].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }

}}

    if(cfg->staticCfg.isSumTxEnabled){
        /*******************************/
        /* PONG SUM TX PARAMSET        */
        /*******************************/
    {{

        paramsetIdx++;
        hwParamsetIdx++;

        hwaParamCfg[paramsetIdx] = hwaParamCfg[DPU_DOPPLERHWADDMA_SUM_TX_PING_HWA_PARAMSET_RELATIVE_IDX];
        hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_SUMTX_PONG_IN;
        hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_SUMTX_PONG_OUT +
                                                obj->numDopplerBins * obj->dopplerDemodCfg.DDMAMetricIOCfg.output.bytesPerSample;

        hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_SOFTWARE; //HWA_TRIG_MODE_IMMEDIATE;

        errCode = HWA_configParamSet(obj->hwaHandle,
                                    hwParamsetIdx,
                                    &hwaParamCfg[paramsetIdx],NULL);
        if (errCode != 0)
        {
            goto exit;
        }

        errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[PONG].channel, &destChan);
        if (errCode != 0)
        {
            goto exit;
        }

        /* enable the DMA hookup to this paramset so that data gets copied out */
        paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
        paramISRConfig.dma.dstChannel = destChan;
        errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
        if (errCode != 0)
        {
            goto exit;
        }

    }}
    }

exit:
    return(errCode);
}

/**
 *  @b Description
 *  @n
 *      Configures Azimuth, CFAR, Local Max processing in HWA.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval error code.
 */
static int32_t DPU_DopplerProcHWA_configHwaCFARAzimFFT
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
)
{
    HWA_ParamConfig         hwaParamCfg[DOPPLERPROCHWA_DDMA_AZIMCFAR_NUM_HWA_PARAMSETS];
    HWA_InterruptConfig     paramISRConfig;
    uint32_t                paramsetIdx = 0;
    uint32_t                hwParamsetIdx = cfg->hwRes.hwaCfg.azimCfarStageHwaStateMachineCfg.paramSetStartIdx;
    int32_t                 errCode = 0U;
    uint8_t                 destChan;
    uint32_t                fftSizeTemp;
    uint32_t                cfarAvgRight, cfarAvgLeft, cfarGuardCells;

    memset((void*) &hwaParamCfg, 0, DOPPLERPROCHWA_DDMA_AZIMCFAR_NUM_HWA_PARAMSETS * sizeof(HWA_ParamConfig));

    /***********************/
    /* PING DUMMY PARAMSET */
    /***********************/
{
    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_DMA;
    hwaParamCfg[paramsetIdx].triggerSrc = obj->cfarAzimFFTCfg.hwaDmaTriggerSourcePingPongIn[PING];
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_NONE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
}

    /*******************************/
    /* PING AZIM FFT PARAMSET      */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_FFT;

    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcEstResetMode = HWA_DCEST_INTERFSUM_RESET_MODE_NOUPDATE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.dcSubEnable = HWA_FEATURE_BIT_DISABLE;
    /* Enable complex multiply mode for antenna phase calibration. */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.cmultMode = HWA_COMPLEX_MULTIPLY_MODE_VECTOR_MULT;

#ifdef CASCADE_EVM
    /* ensure reading is from vector multiplication RAM as the number of coefficients exceed what is available in common config regs.*/
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.modeCfg.vectorMultiplyMode1.cmultScaleEn = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.modeCfg.vectorMultiplyMode1.vecMultiMode1RamAddrOffset = 0;
#else
	/* ensure reading is from common config regs, not the RAM */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.preProcCfg.complexMultiply.modeCfg.vectorMultiplyMode1.cmultScaleEn = HWA_FEATURE_BIT_ENABLE;
#endif

    /* ACCELMODE CONFIG (FFT) */
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftEn = HWA_FEATURE_BIT_ENABLE;

    fftSizeTemp = obj->cfarAzimFFTCfg.numAzimFFTBins;
    if(fftSizeTemp % 3 == 0){
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(fftSizeTemp/3);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_ENABLE;
    }
    else{
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize = mathUtils_ceilLog2(fftSizeTemp);
        hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.fftSize3xEn = HWA_FEATURE_BIT_DISABLE;
    }
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.butterflyScaling = 0;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.windowEn = HWA_FEATURE_BIT_DISABLE; /* No windowing at this stage */

    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.magLogEn = HWA_FFT_MODE_MAGNITUDE_LOG2_ENABLED;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.fftOutMode = HWA_FFT_MODE_OUTPUT_DEFAULT;
    hwaParamCfg[paramsetIdx].accelModeArgs.fftMode.postProcCfg.max2Denable = HWA_FEATURE_BIT_ENABLE;

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_AZIMFFT_PING_IN;

#ifdef CASCADE_EVM
    /* This is samples - 1 for zero indexing and the last 2 samples are zero and don't need to be fetched hence -2 */
    hwaParamCfg[paramsetIdx].source.srcAcnt      = cfg->staticCfg.numRxAntennas * cfg->staticCfg.numAzimTxAntennas - 3U;
    hwaParamCfg[paramsetIdx].source.shuffleMode  = HWA_SRC_SHUFFLE_AB_MODE_ADIM;
    hwaParamCfg[paramsetIdx].source.shuffleStart = SHUFFLE_LUT_OFFSET / 32U; /* Offsets only possible with 256 bit words */
#else
    /* Zero indexed number of samples hence -1 and the access will be linear*/
    hwaParamCfg[paramsetIdx].source.srcAcnt = cfg->staticCfg.numRxAntennas * cfg->staticCfg.numAzimTxAntennas - 1U;
#endif

    hwaParamCfg[paramsetIdx].source.srcAIdx = obj->cfarAzimFFTCfg.azimFFTIOCfg.input.bytesPerSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = obj->numDopplerBins/obj->dopplerDemodCfg.numBandsTotal - 1;
    hwaParamCfg[paramsetIdx].source.srcBIdx = cfg->staticCfg.numRxAntennas * cfg->staticCfg.numAzimTxAntennas * obj->cfarAzimFFTCfg.azimFFTIOCfg.input.bytesPerSample;

    hwaParamCfg[paramsetIdx].source.srcRealComplex = obj->cfarAzimFFTCfg.azimFFTIOCfg.input.isReal;
    if(obj->cfarAzimFFTCfg.azimFFTIOCfg.input.bytesPerSample == 2 ||
        (obj->cfarAzimFFTCfg.azimFFTIOCfg.input.bytesPerSample == 4 &&
        (!obj->cfarAzimFFTCfg.azimFFTIOCfg.input.isReal))){
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].source.srcSign = obj->cfarAzimFFTCfg.azimFFTIOCfg.input.isSigned;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 8;

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_AZIMFFT_PING_OUT;

    hwaParamCfg[paramsetIdx].dest.dstAcnt = fftSizeTemp - 1;
    hwaParamCfg[paramsetIdx].dest.dstAIdx = obj->cfarAzimFFTCfg.azimFFTIOCfg.output.bytesPerSample;
    hwaParamCfg[paramsetIdx].dest.dstBIdx = fftSizeTemp * obj->cfarAzimFFTCfg.azimFFTIOCfg.output.bytesPerSample;

    hwaParamCfg[paramsetIdx].dest.dstRealComplex = obj->cfarAzimFFTCfg.azimFFTIOCfg.output.isReal;
    if(obj->cfarAzimFFTCfg.azimFFTIOCfg.output.bytesPerSample == 2 ||
        (obj->cfarAzimFFTCfg.azimFFTIOCfg.output.bytesPerSample == 4 &&
        (!obj->cfarAzimFFTCfg.azimFFTIOCfg.output.isReal))){
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].dest.dstSign = obj->cfarAzimFFTCfg.azimFFTIOCfg.output.isSigned;
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

#if 0 /* This cannot be done here since the AzimFFT output data is needed by the subsequent DPUs and hence there's
        a possibility of conflict between HWA and DMA */
    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PING].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }
#endif

}}

    /*******************************/
    /* PING DOPPLER CFAR PARAMSET  */
    /*******************************/
{{

    cfarAvgRight = obj->cfarAzimFFTCfg.cfarCfg.winLen >> 1;
    cfarAvgLeft = obj->cfarAzimFFTCfg.cfarCfg.winLen >> 1;
    cfarGuardCells = obj->cfarAzimFFTCfg.cfarCfg.guardLen;

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_CFAR;

    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.peakGroupEn = obj->cfarAzimFFTCfg.cfarCfg.peakGroupingEn;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.operMode = HWA_CFAR_OPER_MODE_LOG_INPUT_REAL; /* cfarInpMode = 1, cfarLogMode = 1, cfarAbsMode = 00b */
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.numGuardCells = cfarGuardCells;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.nAvgDivFactor = obj->cfarAzimFFTCfg.cfarCfg.noiseDivShift;//not applicable in CFAR_OS
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.cyclicModeEn = obj->cfarAzimFFTCfg.cfarCfg.cyclicMode;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.nAvgMode = obj->cfarAzimFFTCfg.cfarCfg.averageMode;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.numNoiseSamplesRight = cfarAvgRight;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.numNoiseSamplesLeft =  cfarAvgLeft;
    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.outputMode = HWA_CFAR_OUTPUT_MODE_I_PEAK_IDX_Q_NEIGHBOR_NOISE_VAL;
    if (obj->cfarAzimFFTCfg.cfarCfg.averageMode == HWA_NOISE_AVG_MODE_CFAR_OS)
	{
	    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.cfarOsKvalue = obj->cfarAzimFFTCfg.cfarCfg.osKvalue;
	    hwaParamCfg[paramsetIdx].accelModeArgs.cfarMode.cfarOsEdgeKScaleEn = obj->cfarAzimFFTCfg.cfarCfg.osEdgeKscaleEn;
	}

    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_CFAR_PING_IN;

    hwaParamCfg[paramsetIdx].source.srcAcnt = obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal - 1
                                + 2 * cfarAvgRight + cfarGuardCells
                                + 2 * cfarAvgLeft + cfarGuardCells;
    hwaParamCfg[paramsetIdx].source.srcAIdx = obj->cfarAzimFFTCfg.numAzimFFTBins * obj->cfarAzimFFTCfg.cfarIOCfg.input.bytesPerSample;
    hwaParamCfg[paramsetIdx].source.srcBIdx = obj->cfarAzimFFTCfg.cfarIOCfg.input.bytesPerSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = obj->cfarAzimFFTCfg.numAzimFFTBins - 1;
    hwaParamCfg[paramsetIdx].source.srcRealComplex = obj->cfarAzimFFTCfg.cfarIOCfg.input.isReal;
    hwaParamCfg[paramsetIdx].source.srcScale = 8;
    if(obj->cfarAzimFFTCfg.cfarIOCfg.input.bytesPerSample == 2 ||
        (obj->cfarAzimFFTCfg.cfarIOCfg.input.bytesPerSample == 4 &&
        (!obj->cfarAzimFFTCfg.cfarIOCfg.input.isReal))){
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].source.srcSign = obj->cfarAzimFFTCfg.cfarIOCfg.input.isSigned;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcAcircShift = (obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal)
                                                            - (2 * cfarAvgRight + cfarGuardCells);

    if ((obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal) % 3 == 0){ /* If numSamples % 3 == 0 */
        hwaParamCfg[paramsetIdx].source.srcCircShiftWrap3 = 1; /* 'b001, means wrap in A dim */
        hwaParamCfg[paramsetIdx].source.srcAcircShiftWrap = mathUtils_ceilLog2((obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal) / 3);
    }
    else{
        hwaParamCfg[paramsetIdx].source.srcCircShiftWrap3 = 0;
        hwaParamCfg[paramsetIdx].source.srcAcircShiftWrap = mathUtils_ceilLog2(obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal);
    }

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_CFAR_PING_OUT;

    hwaParamCfg[paramsetIdx].dest.dstAcnt = cfg->hwRes.maxCfarPeaksToDetect - 1;
    hwaParamCfg[paramsetIdx].dest.dstAIdx = obj->cfarAzimFFTCfg.cfarIOCfg.output.bytesPerSample;
    hwaParamCfg[paramsetIdx].dest.dstBIdx = (obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal) * obj->cfarAzimFFTCfg.cfarIOCfg.output.bytesPerSample;

    hwaParamCfg[paramsetIdx].dest.dstRealComplex = obj->cfarAzimFFTCfg.cfarIOCfg.output.isReal;
    if(obj->cfarAzimFFTCfg.cfarIOCfg.output.bytesPerSample == 2 ||
        (obj->cfarAzimFFTCfg.cfarIOCfg.output.bytesPerSample == 4 &&
        (!obj->cfarAzimFFTCfg.cfarIOCfg.output.isReal))){
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_16BIT;
    }
    else{
        hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_32BIT;
    }
    hwaParamCfg[paramsetIdx].dest.dstSign = obj->cfarAzimFFTCfg.cfarIOCfg.output.isSigned;
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].dest.dstScale = 8;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaCfarOut.pingPong[PING].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }

}}

    /*******************************/
    /* PING LOCAL MAX PARAMSET     */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_LOCALMAX;

    /* PREPROC CONFIG */
    hwaParamCfg[paramsetIdx].accelModeArgs.localMaxMode.neighbourBitmask = 85; /* 0 1 0 1 0 1 0 1, "+" shaped comparison */
    hwaParamCfg[paramsetIdx].accelModeArgs.localMaxMode.thresholdBitMask = 0; /* ~ (1 1), enable comparison row wise and column wise */
    hwaParamCfg[paramsetIdx].accelModeArgs.localMaxMode.thresholdMode = 3; /* 1 1, use Max2D internal statistics for thresholding instead of SW based thresholds */

    /* SOURCE CONFIG */
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_LOCALMAX_PING_IN;

    hwaParamCfg[paramsetIdx].source.srcAcnt = 3 - 1; /* Fixed for Local Max */
    hwaParamCfg[paramsetIdx].source.srcAIdx = obj->cfarAzimFFTCfg.numAzimFFTBins * obj->cfarAzimFFTCfg.localMaxIOCfg.input.bytesPerSample;
    hwaParamCfg[paramsetIdx].source.srcBcnt = obj->cfarAzimFFTCfg.numAzimFFTBins / 4 - 1 + 1;
    hwaParamCfg[paramsetIdx].source.srcBIdx = 8; /* Fixed for Local Max */
    hwaParamCfg[paramsetIdx].source.srcCcnt = (obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal) - 1;
    hwaParamCfg[paramsetIdx].source.srcCIdx = obj->cfarAzimFFTCfg.numAzimFFTBins * obj->cfarAzimFFTCfg.localMaxIOCfg.input.bytesPerSample;
    if ((obj->cfarAzimFFTCfg.numAzimFFTBins) % 3 == 0){ /* If numSamples % 3 == 0 */
        hwaParamCfg[paramsetIdx].source.srcCircShiftWrap3 = 2; /* 'b020, means wrap in B dim */
        hwaParamCfg[paramsetIdx].source.srcBcircShiftWrap = mathUtils_ceilLog2((obj->cfarAzimFFTCfg.numAzimFFTBins/3) / 4);
    }
    else{
        hwaParamCfg[paramsetIdx].source.srcCircShiftWrap3 = 0;
        hwaParamCfg[paramsetIdx].source.srcBcircShiftWrap = mathUtils_ceilLog2(obj->cfarAzimFFTCfg.numAzimFFTBins / 4);
    }
    hwaParamCfg[paramsetIdx].source.wrapComb = (obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal)
                                               * obj->cfarAzimFFTCfg.numAzimFFTBins * obj->cfarAzimFFTCfg.localMaxIOCfg.input.bytesPerSample;

    hwaParamCfg[paramsetIdx].source.srcRealComplex = 0; /* Fixed for Local Max */
    hwaParamCfg[paramsetIdx].source.srcWidth = HWA_SAMPLES_WIDTH_32BIT; /* Fixed for Local Max */
    hwaParamCfg[paramsetIdx].source.srcSign = obj->cfarAzimFFTCfg.localMaxIOCfg.input.isSigned;
    hwaParamCfg[paramsetIdx].source.srcConjugate = 0;
    hwaParamCfg[paramsetIdx].source.srcScale = 0;

    /* DEST CONFIG */
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_LOCALMAX_PING_OUT;

    /* dstAcnt = ceil(numAzimFFTBins/32) - 1
       dstBIdx = 4 * ceil(numAzimFFTBins/32) */
    if(obj->cfarAzimFFTCfg.numAzimFFTBins % 32 == 0){
        hwaParamCfg[paramsetIdx].dest.dstAcnt = obj->cfarAzimFFTCfg.numAzimFFTBins/32 - 1;
        hwaParamCfg[paramsetIdx].dest.dstBIdx = 4 * obj->cfarAzimFFTCfg.numAzimFFTBins/32;
    }
    else{
        hwaParamCfg[paramsetIdx].dest.dstAcnt = obj->cfarAzimFFTCfg.numAzimFFTBins/32 + 1 - 1;
        hwaParamCfg[paramsetIdx].dest.dstBIdx = 4 * (obj->cfarAzimFFTCfg.numAzimFFTBins/32 + 1);
    }

    hwaParamCfg[paramsetIdx].dest.dstAIdx = 4; /* Fixed for Local Max */

    hwaParamCfg[paramsetIdx].dest.dstRealComplex = obj->cfarAzimFFTCfg.localMaxIOCfg.output.isReal;
    hwaParamCfg[paramsetIdx].dest.dstWidth = HWA_SAMPLES_WIDTH_32BIT;  /* Fixed for Local Max */
    hwaParamCfg[paramsetIdx].dest.dstSign = obj->cfarAzimFFTCfg.localMaxIOCfg.output.isSigned;
    hwaParamCfg[paramsetIdx].dest.dstConjugate = HWA_FEATURE_BIT_DISABLE;
    hwaParamCfg[paramsetIdx].dest.dstScale = 0;
    hwaParamCfg[paramsetIdx].dest.dstSkipInit = 0;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[PING].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }

}}

    /* While the AZIM FFT out EDMA trasfer could have been chained to the localmax out EDMA transfer directly, we observed that
       the ISR for the Azim transfer was not being entered. Hence this is a workaround till the issue gets resolved. */
    /*****************************************/
    /* PING DUMMY AZIM FFT TRANSFER PARAMSET */
    /*****************************************/
{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_NONE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PING].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }

}

    /***********************/
    /* PONG DUMMY PARAMSET */
    /***********************/
{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_DMA;
    hwaParamCfg[paramsetIdx].triggerSrc = obj->cfarAzimFFTCfg.hwaDmaTriggerSourcePingPongIn[PONG];
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_NONE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }
}

    /*******************************/
    /* PONG AZIM FFT PARAMSET      */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx] = hwaParamCfg[DPU_DOPPLERHWADDMA_AZIMFFT_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_AZIMFFT_PONG_IN;
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_AZIMFFT_PONG_OUT;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

#if 0
    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PONG].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }
#endif

}}

    /*******************************/
    /* PONG CFAR-OS PARAMSET       */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx] = hwaParamCfg[DPU_DOPPLERHWADDMA_CFAR_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_CFAR_PONG_IN;
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_CFAR_PONG_OUT;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaCfarOut.pingPong[PONG].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }

}}

    /*******************************/
    /* PONG LOCAL MAX PARAMSET     */
    /*******************************/
{{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx] = hwaParamCfg[DPU_DOPPLERHWADDMA_LOCALMAX_PING_HWA_PARAMSET_RELATIVE_IDX];
    hwaParamCfg[paramsetIdx].source.srcAddr = DPU_DOPPLERHWADDMA_ADDR_LOCALMAX_PONG_IN;
    hwaParamCfg[paramsetIdx].dest.dstAddr = DPU_DOPPLERHWADDMA_ADDR_LOCALMAX_PONG_OUT;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[PONG].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }
    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
    if (errCode != 0)
    {
        goto exit;
    }

}}

    /* While the AZIM FFT out EDMA trasfer could have been chained to the localmax out EDMA transfer directly, we observed that
       the ISR for the Azim transfer was not being entered. Hence this is a workaround till the issue gets resolved. */
    /*****************************************/
    /* PONG DUMMY AZIM FFT TRANSFER PARAMSET */
    /*****************************************/
{

    paramsetIdx++;
    hwParamsetIdx++;

    hwaParamCfg[paramsetIdx].triggerMode = HWA_TRIG_MODE_IMMEDIATE;
    hwaParamCfg[paramsetIdx].accelMode = HWA_ACCELMODE_NONE;

    errCode = HWA_configParamSet(obj->hwaHandle,
                                  hwParamsetIdx,
                                  &hwaParamCfg[paramsetIdx],NULL);
    if (errCode != 0)
    {
        goto exit;
    }

    errCode = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PONG].channel, &destChan);
    if (errCode != 0)
    {
        goto exit;
    }

    /* enable the DMA hookup to this paramset so that data gets copied out */
    paramISRConfig.interruptTypeFlag = HWA_PARAMDONE_INTERRUPT_TYPE_DMA;
    paramISRConfig.dma.dstChannel = destChan;
    errCode = HWA_enableParamSetInterrupt(obj->hwaHandle, hwParamsetIdx, &paramISRConfig);
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
 *  Doppler DPU EDMA configuration that sends Doppler FFT In data (decompression out data)
 *  from L3 to HWA memory
 *  This implementation of doppler processing involves Ping/Pong
 *  Mechanism, hence there are two sets of EDMA transfer.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval EDMA error code, see EDMA API.
 */
int32_t DPU_DopplerProcHWA_configEdmaDopplerIn
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
){

    DPEDMA_syncABCfg            syncABCfg;
    DPEDMA_ChainingCfg          chainingCfg;
    int32_t                     retVal;

    if(obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

    /* Program EDMA Data in from Decompressed Radar Cube scratch buffer to HWA Memory */
    /* PING */
    {{
    chainingCfg.chainingChannel                  = cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaInSignature.pingPong[PING].channel;
    chainingCfg.isIntermediateChainingEnabled = true;
    chainingCfg.isFinalChainingEnabled        = true;

    syncABCfg.srcAddress  = (uint32_t)(cfg->hwRes.decompScratchBuf);
    syncABCfg.destAddress = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PING_IN]);
    syncABCfg.aCount      = cfg->staticCfg.numRxAntennas * obj->dopplerDemodCfg.dopplerIOCfg.input.bytesPerSample;
    syncABCfg.bCount      = cfg->staticCfg.numChirps;
    syncABCfg.cCount      = obj->decompCfg.rangeBinsPerBlock / 2; /* Ping and Pong */
    syncABCfg.srcBIdx     = cfg->staticCfg.numRxAntennas * obj->decompCfg.rangeBinsPerBlock * obj->dopplerDemodCfg.dopplerIOCfg.input.bytesPerSample;
    syncABCfg.dstBIdx     = cfg->staticCfg.numRxAntennas * obj->dopplerDemodCfg.dopplerIOCfg.input.bytesPerSample;
    syncABCfg.srcCIdx     = obj->dopplerDemodCfg.dopplerIOCfg.input.bytesPerSample * cfg->staticCfg.numRxAntennas * 2; /* Ping and Pong */
    syncABCfg.dstCIdx     = 0; /* One range bin in a block is processed at a time */

    retVal = DPEDMA_configSyncAB(cfg->hwRes.edmaCfg.edmaHandle,
                                    &cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaIn.pingPong[PING],
                                    &chainingCfg,
                                    &syncABCfg,
                                    false,//isEventTriggered
                                    false, //isIntermediateTransferCompletionEnabled
                                    false,//isTransferCompletionEnabled
                                    NULL, //transferCompletionCallbackFxn
                                    NULL,
                                    NULL);//transferCompletionCallbackFxnArg

    /* One Hot Signature to trigger the HWA */
    retVal = DPEDMAHWA_configOneHotSignature(cfg->hwRes.edmaCfg.edmaHandle,
                                                  &cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaInSignature.pingPong[PING],
                                                  obj->hwaHandle,
                                                  obj->dopplerDemodCfg.hwaDmaTriggerSourcePingPongIn[PING],
                                                  false);
    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }
    }}

    /* PONG */
    {{
    chainingCfg.chainingChannel = cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaInSignature.pingPong[PONG].channel;
    syncABCfg.srcAddress  = (uint32_t)((uint8_t *)cfg->hwRes.decompScratchBuf +
                                        obj->dopplerDemodCfg.dopplerIOCfg.input.bytesPerSample * cfg->staticCfg.numRxAntennas);
    syncABCfg.destAddress = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PONG_IN]);
    retVal = DPEDMA_configSyncAB(cfg->hwRes.edmaCfg.edmaHandle,
                                    &cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaIn.pingPong[PONG],
                                    &chainingCfg,
                                    &syncABCfg,
                                    false,//isEventTriggered
                                    false,  //isIntermediateTransferCompletionEnabled
                                    false,//isTransferCompletionEnabled
                                    NULL, //transferCompletionCallbackFxn
                                    NULL,//transferCompletionCallbackFxnArg
                                    NULL);

    /* One Hot Signature to trigger the HWA */
    retVal = DPEDMAHWA_configOneHotSignature(cfg->hwRes.edmaCfg.edmaHandle,
                                                  &cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaInSignature.pingPong[PONG],
                                                  obj->hwaHandle,
                                                  obj->dopplerDemodCfg.hwaDmaTriggerSourcePingPongIn[PONG],
                                                  false);
    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

    }}

exit:
    return(retVal);

}

/**
 *  @b Description
 *  @n
 *  Doppler DPU EDMA configuration that sends compressed radar cube data to HWA memory for
 *  decompression.
 *  This implementation of doppler processing involves Ping/Pong
 *  Mechanism, hence there are two sets of EDMA transfer.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval EDMA error code, see EDMA API.
 */
int32_t DPU_DopplerProcHWA_configEdmaDecompressionIn
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
){

    DPEDMA_syncABCfg            syncABCfg;
    DPEDMA_ChainingCfg          chainingCfg;
    int32_t                     retVal;

    if(obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

    /* PING */
    chainingCfg.chainingChannel                  = cfg->hwRes.edmaCfg.decompEdmaCfg.edmaInSignature.pingPong[PING].channel;
    chainingCfg.isIntermediateChainingEnabled = true;
    chainingCfg.isFinalChainingEnabled        = true;

    syncABCfg.srcAddress  = (uint32_t)(obj->decompCfg.decompEdmaToHwaStartAddress);
    syncABCfg.destAddress = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PING_IN]);
    syncABCfg.aCount      = obj->decompCfg.inputBytesPerBlock * obj->decompCfg.numBlocksPerPing;
    syncABCfg.bCount      = 1;
    syncABCfg.cCount      = obj->decompCfg.numLoops;
    syncABCfg.srcBIdx     = obj->decompCfg.inputBytesPerBlock * obj->decompCfg.numBlocksPerPing * 2;
    syncABCfg.dstBIdx     = obj->decompCfg.inputBytesPerBlock * obj->decompCfg.numBlocksPerPing * 2;
    syncABCfg.srcCIdx     = obj->decompCfg.inputBytesPerBlock * obj->decompCfg.numBlocksPerPing * 2;
    syncABCfg.dstCIdx     = 0;

    retVal = DPEDMA_configSyncAB(cfg->hwRes.edmaCfg.edmaHandle,
                                    &cfg->hwRes.edmaCfg.decompEdmaCfg.edmaIn.pingPong[PING],
                                    &chainingCfg,
                                    &syncABCfg,
                                    false,//isEventTriggered
                                    false, //isIntermediateTransferCompletionEnabled
                                    false,//isTransferCompletionEnabled
                                    NULL, //transferCompletionCallbackFxn
                                    NULL, //transferCompletionCallbackFxnArg
                                    NULL); /* intrObj */

    /* One Hot Signature to trigger the HWA */
    retVal = DPEDMAHWA_configOneHotSignature(cfg->hwRes.edmaCfg.edmaHandle,
                                                  &cfg->hwRes.edmaCfg.decompEdmaCfg.edmaInSignature.pingPong[PING],
                                                  obj->hwaHandle,
                                                  obj->decompCfg.hwaDmaTriggerSourcePingPongIn[PING],
                                                  false);

    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

    /* PONG */
    chainingCfg.chainingChannel = cfg->hwRes.edmaCfg.decompEdmaCfg.edmaInSignature.pingPong[PONG].channel;
    syncABCfg.srcAddress  = (uint32_t)(((uint8_t *)obj->decompCfg.decompEdmaToHwaStartAddress) +
                                        obj->decompCfg.inputBytesPerBlock * obj->decompCfg.numBlocksPerPing);
    syncABCfg.destAddress = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PONG_IN]);
    retVal = DPEDMA_configSyncAB(cfg->hwRes.edmaCfg.edmaHandle,
                                    &cfg->hwRes.edmaCfg.decompEdmaCfg.edmaIn.pingPong[PONG],
                                    &chainingCfg,
                                    &syncABCfg,
                                    false,//isEventTriggered
                                    false, //isIntermediateTransferCompletionEnabled
                                    false,//isTransferCompletionEnabled
                                    NULL, //transferCompletionCallbackFxn
                                    NULL, //transferCompletionCallbackFxnArg
                                    NULL); /* intrObj */

    /* One Hot Signature to trigger the HWA */
    retVal = DPEDMAHWA_configOneHotSignature(cfg->hwRes.edmaCfg.edmaHandle,
                                                  &cfg->hwRes.edmaCfg.decompEdmaCfg.edmaInSignature.pingPong[PONG],
                                                  obj->hwaHandle,
                                                  obj->decompCfg.hwaDmaTriggerSourcePingPongIn[PONG],
                                                  false);
    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

exit:
    return(retVal);

}

/**
 *  @b Description
 *  @n
 *  Doppler DPU EDMA configuration for Decompressed data out of HWA into L3.
 *  This implementation of doppler processing involves Ping/Pong
 *  Mechanism, hence there are two sets of EDMA transfer.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval EDMA error code, see EDMA API.
 */
static  int32_t DPU_DopplerProcHWA_configEdmaDecompressionOut
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
)
{

    DPEDMA_syncABCfg            syncABCfg;
    DPEDMA_ChainingCfg          chainingCfg;
    int32_t                     retVal;

    if(obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

    {{

    /* PING */
    chainingCfg.chainingChannel                  = cfg->hwRes.edmaCfg.decompEdmaCfg.edmaIn.pingPong[PING].channel;
    chainingCfg.isIntermediateChainingEnabled = true;
    chainingCfg.isFinalChainingEnabled        = false;

    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PING_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.decompScratchBuf);
    syncABCfg.aCount      = obj->decompCfg.outputBytesPerBlock * obj->decompCfg.numBlocksPerPing;
    syncABCfg.bCount      = 1;
    syncABCfg.cCount      = obj->decompCfg.numLoops;
    syncABCfg.srcBIdx     = 0;
    syncABCfg.dstBIdx     = obj->decompCfg.outputBytesPerBlock * obj->decompCfg.numBlocksPerPing * 2;
    syncABCfg.srcCIdx     = 0;
    syncABCfg.dstCIdx     = obj->decompCfg.outputBytesPerBlock * obj->decompCfg.numBlocksPerPing * 2;

    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.decompEdmaCfg.edmaOut.pingPong[PING],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true,//isEventTriggered
                                    false, //isIntermediateTransferCompletionEnabled
                                    false,//isTransferCompletionEnabled
                                    NULL, //transferCompletionCallbackFxn
                                    NULL, //transferCompletionCallbackFxnArg
                                    NULL); /* intrObj */

    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

    /* PONG */
    chainingCfg.chainingChannel = cfg->hwRes.edmaCfg.decompEdmaCfg.edmaIn.pingPong[PONG].channel;
    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DECOMP_PONG_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.decompScratchBuf +
                                    obj->decompCfg.outputBytesPerBlock * obj->decompCfg.numBlocksPerPing);
    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.decompEdmaCfg.edmaOut.pingPong[PONG],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true,//isEventTriggered
                                    false, //isIntermediateTransferCompletionEnabled
                                    true,//isTransferCompletionEnabled
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->decompEdmaOutDoneSemaHandle), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.decompEdmaCfg.edmaIntrObjDecompOut); /* intrObj */

    }}

exit:
    return(retVal);

}

/**
 *  @b Description
 *  @n
 *  Doppler DPU EDMA configuration for sending out Doppler FFT data from HWA
 *  Memory to L2.
 *  This implementation of doppler processing involves Ping/Pong
 *  Mechanism, hence there are two sets of EDMA transfer.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval EDMA error code, see EDMA API.
 */
static  int32_t DPU_DopplerProcHWA_configEdmaDopplerFFTOut
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
)
{

    DPEDMA_syncABCfg            syncABCfg;
    DPEDMA_ChainingCfg          chainingCfg;
    int32_t                     retVal;
#ifndef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
    Edma_EventCallback doneCllbackFunc[2] = {NULL, NULL};
    uint32_t            doneCllbackFuncArg[2] = {NULL, NULL};
#endif
    bool                doneTransferCompletionEnabled[2] = {true, true};

    if(obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

    {{

    /* PING */
    chainingCfg.chainingChannel                  = cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PING].channel;
    chainingCfg.isIntermediateChainingEnabled = true;
    chainingCfg.isFinalChainingEnabled        = false;

    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PING_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.dopplerFFTScratchBuf[PING]);
    syncABCfg.aCount      = obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample * cfg->staticCfg.numRxAntennas;
    syncABCfg.bCount      = obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal;
    syncABCfg.cCount      = obj->dopplerDemodCfg.numBandsTotal;
    syncABCfg.srcBIdx     = cfg->staticCfg.numRxAntennas * obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample;
    syncABCfg.dstBIdx     = obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample * cfg->staticCfg.numRxAntennas * obj->dopplerDemodCfg.numBandsTotal;
    syncABCfg.srcCIdx     = obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample * cfg->staticCfg.numRxAntennas
                            * obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal;
    syncABCfg.dstCIdx     = cfg->staticCfg.numRxAntennas * obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample;

    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PING],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true,//isEventTriggered // UPON HWA COMPLETION
                                    false, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PING],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->dopFFTEdmaOutDoneSemaHandle[PING]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaIntrObjDopplerFFTOut.pingPong[PING]);  /* intrObj */
#else
                                    doneCllbackFunc[PING], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PING], //transferCompletionCallbackFxnArg
                                    NULL);  /* intrObj */
#endif
    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

    /* PONG */
    chainingCfg.chainingChannel = cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PONG].channel;
    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DOPPLERFFT_PONG_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.dopplerFFTScratchBuf[PONG]);

    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PONG],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true,//isEventTriggered
                                    false, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PONG],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->dopFFTEdmaOutDoneSemaHandle[PONG]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaIntrObjDopplerFFTOut.pingPong[PONG]);  /* intrObj */
#else
                                    doneCllbackFunc[PONG], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PONG], //transferCompletionCallbackFxnArg
                                    NULL);  /* intrObj */
#endif
    }}

exit:
    return(retVal);

}


/**
 *  @b Description
 *  @n
 *  EDMA Configuration to send DDMA Metric data out to L2 from HWA
 *  This implementation of doppler processing involves Ping/Pong
 *  Mechanism, hence there are two sets of EDMA transfer.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval EDMA error code, see EDMA API.
 */
static  int32_t DPU_DopplerProcHWA_configEdmaDDMAMetricOut
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
)
{

    DPEDMA_syncABCfg            syncABCfg;
    DPEDMA_ChainingCfg          chainingCfg;
    int32_t                     retVal;
#ifndef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
    Edma_EventCallback doneCllbackFunc[2] = {NULL, NULL};
    uint32_t            doneCllbackFuncArg[2] = {NULL, NULL};
#endif
    bool                doneTransferCompletionEnabled[2] = {true, true};

    if(obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

    {{

    /* PING */
    chainingCfg.chainingChannel                  = cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[PING].channel;
    chainingCfg.isIntermediateChainingEnabled = false;
    chainingCfg.isFinalChainingEnabled        = false;

    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PING_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.DDMAMetricScratchBuf[PING]);
    syncABCfg.aCount      = obj->dopplerDemodCfg.DDMAMetricIOCfg.output.bytesPerSample * obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal * obj->dopplerDemodCfg.numBandsTotal;
    syncABCfg.bCount      = 1;
    syncABCfg.cCount      = 1;
    syncABCfg.srcBIdx     = 1;
    syncABCfg.dstBIdx     = 1;
    syncABCfg.srcCIdx     = 1;
    syncABCfg.dstCIdx     = 1;

    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[PING],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true,//isEventTriggered // UPON HWA COMPLETION
                                    false, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PING],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->DDMAMetricEdmaOutDoneSemaHandle[PING]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaIntrObjDDMAMetricOut.pingPong[PING]); /* Interrupt object */
#else
                                    doneCllbackFunc[PING], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PING], //transferCompletionCallbackFxnArg
                                    NULL); /* Interrupt object */
#endif
    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

    /* PONG */
    chainingCfg.chainingChannel = cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[PONG].channel;
    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PONG_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.DDMAMetricScratchBuf[PONG]);
    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[PONG],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true,//isEventTriggered
                                    false, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PONG],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->DDMAMetricEdmaOutDoneSemaHandle[PONG]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaIntrObjDDMAMetricOut.pingPong[PONG]); /* Interrupt object */
#else
                                    doneCllbackFunc[PONG], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PONG], //transferCompletionCallbackFxnArg
                                    NULL); /* Interrupt object */
#endif
    }}

exit:
    return(retVal);

}

/**
 *  @b Description
 *  @n
 *  Doppler DPU EDMA configuration to transfer sumTx data from HWA Memory to L3.
 *  This implementation of doppler processing involves Ping/Pong
 *  Mechanism, hence there are two sets of EDMA transfer.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval EDMA error code, see EDMA API.
 */
static  int32_t DPU_DopplerProcHWA_configEdmaDopplerFFTSumTxOut
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
)
{

    DPEDMA_syncACfg            syncACfg;
    DPEDMA_ChainingCfg          chainingCfg;
    int32_t                     retVal;
#ifndef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
    Edma_EventCallback doneCllbackFunc[2] = {NULL, NULL};
    uint32_t            doneCllbackFuncArg[2] = {NULL, NULL};
#endif
    bool                doneTransferCompletionEnabled[2] = {true, true};

    if(obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }


    {{

    /* PING */
    chainingCfg.chainingChannel                  = cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[PING].channel;
    chainingCfg.isIntermediateChainingEnabled = false;
    chainingCfg.isFinalChainingEnabled        = false;

    syncACfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PING_OUT]
                                    + (obj->dopplerDemodCfg.DDMAMetricIOCfg.output.bytesPerSample * obj->numDopplerBins));
    syncACfg.destAddress = (uint32_t)(cfg->hwRes.detMatrix.data);
    syncACfg.aCount      = obj->dopplerDemodCfg.sumTxIOCfg.output.bytesPerSample
                            * obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal;
    syncACfg.bCount      = obj->decompCfg.rangeBinsPerBlock / 2;
    syncACfg.cCount      = obj->decompCfg.numOuterBlocks;
    syncACfg.srcBIdx     = 0;
    syncACfg.dstBIdx     = obj->dopplerDemodCfg.sumTxIOCfg.output.bytesPerSample
                            * (obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal)
                            * 2;
    syncACfg.srcCIdx     = 0;
    syncACfg.dstCIdx     = obj->dopplerDemodCfg.sumTxIOCfg.output.bytesPerSample
                            * (obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal)
                            * 2;

    retVal = DPEDMA_configSyncA(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[PING],
                                    &chainingCfg,
                                    &syncACfg,
                                    true,//isEventTriggered // UPON HWA COMPLETION
                                    true, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PING],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->sumTXEdmaOutDoneSemaHandle[PING]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaIntrObjSumtxOut.pingPong[PING]);
#else
                                    doneCllbackFunc[PING], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PING], //transferCompletionCallbackFxnArg
                                    NULL);
#endif

    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

    /* PONG */
    chainingCfg.chainingChannel = cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[PONG].channel;
    syncACfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_DDMAMETRIC_PONG_OUT]
                                     + (obj->dopplerDemodCfg.DDMAMetricIOCfg.output.bytesPerSample * obj->numDopplerBins));
    syncACfg.destAddress = (uint32_t)((uint8_t *)cfg->hwRes.detMatrix.data
                                     + (obj->dopplerDemodCfg.sumTxIOCfg.output.bytesPerSample
                                         * obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal));
    retVal = DPEDMA_configSyncA(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[PONG],
                                    &chainingCfg,
                                    &syncACfg,
                                    true,//isEventTriggered
                                    true, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PONG],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->sumTXEdmaOutDoneSemaHandle[PONG]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaIntrObjSumtxOut.pingPong[PONG]);  /* Interrupt object */
#else
                                    doneCllbackFunc[PONG], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PONG], //transferCompletionCallbackFxnArg
                                    NULL);  /* Interrupt object */
#endif

    }}

exit:
    return(retVal);

}

/**
 *  @b Description
 *  @n
 *  Transfers Doppler FFT Output (Azimuth FFT Input) to HWA memory for Azimuth FFT Processing
 *  This implementation of doppler processing involves Ping/Pong
 *  Mechanism, hence there are two sets of EDMA transfer.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval EDMA error code, see EDMA API.
 */
static  int32_t DPU_DopplerProcHWA_configEdmaAzimFFTIn
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg,
    uint32_t                    srcAddr
)
{

    DPEDMA_syncABCfg            syncABCfg;
    DPEDMA_ChainingCfg          chainingCfg;
    int32_t                     retVal;
    Edma_EventCallback doneCllbackFunc[2] = {NULL, NULL};
    uint32_t            doneCllbackFuncArg[2] = {NULL, NULL};
    bool                doneTransferCompletionEnabled[2] = {false, false};

    if(obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

    {{

    /* PING */
    chainingCfg.chainingChannel                  = cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTInSignature.pingPong[PING].channel;
    chainingCfg.isIntermediateChainingEnabled = true;
    chainingCfg.isFinalChainingEnabled        = true;

    syncABCfg.srcAddress  = srcAddr;
    syncABCfg.destAddress = obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PING_IN];
    syncABCfg.aCount      = cfg->staticCfg.numRxAntennas * cfg->staticCfg.numAzimTxAntennas * obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample;
    syncABCfg.bCount      = obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal;
    syncABCfg.cCount      = obj->decompCfg.rangeBinsPerBlock / 2;
    syncABCfg.srcBIdx     = cfg->staticCfg.numRxAntennas * cfg->staticCfg.numTxAntennas
                            * obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample;
    syncABCfg.dstBIdx     = cfg->staticCfg.numRxAntennas * cfg->staticCfg.numAzimTxAntennas * obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample;
    syncABCfg.srcCIdx     = 0;
    syncABCfg.dstCIdx     = 0;

    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTIn.pingPong[PING],
                                    &chainingCfg,
                                    &syncABCfg,
                                    false,//isEventTriggered // UPON HWA COMPLETION
                                    false, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PING],//isTransferCompletionEnabled
                                    doneCllbackFunc[PING], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PING], //transferCompletionCallbackFxnArg
                                    NULL); /* Interrupt object */
    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

    /* One Hot Signature to trigger the HWA */
    retVal = DPEDMAHWA_configOneHotSignature(cfg->hwRes.edmaCfg.edmaHandle,
                                                  &cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTInSignature.pingPong[PING],
                                                  obj->hwaHandle,
                                                  obj->cfarAzimFFTCfg.hwaDmaTriggerSourcePingPongIn[PING],
                                                  false);
    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }


    /* PONG */
    chainingCfg.chainingChannel = cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTInSignature.pingPong[PONG].channel;
    syncABCfg.srcAddress  = (uint32_t)((uint8_t *)srcAddr +
                            cfg->staticCfg.numRxAntennas * cfg->staticCfg.numTxAntennas
                             * obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample
                             * obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal);
    syncABCfg.destAddress = obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PONG_IN];
    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTIn.pingPong[PONG],
                                    &chainingCfg,
                                    &syncABCfg,
                                    false,//isEventTriggered
                                    false, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PONG],//isTransferCompletionEnabled
                                    doneCllbackFunc[PONG], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PONG], //transferCompletionCallbackFxnArg
                                    NULL); /* Interrupt object */

    /* One Hot Signature to trigger the HWA */
    retVal = DPEDMAHWA_configOneHotSignature(cfg->hwRes.edmaCfg.edmaHandle,
                                                &cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTInSignature.pingPong[PONG],
                                                obj->hwaHandle,
                                                obj->cfarAzimFFTCfg.hwaDmaTriggerSourcePingPongIn[PONG],
                                                false);
    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

    }}

exit:
    return(retVal);

}

/**
 *  @b Description
 *  @n
 *  EDMA Configuration to send Azimuth FFT data out to L2 from HWA
 *  This implementation of doppler processing involves Ping/Pong
 *  Mechanism, hence there are two sets of EDMA transfer.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval EDMA error code, see EDMA API.
 */
static  int32_t DPU_DopplerProcHWA_configEdmaAzimFFTOut
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
)
{

    DPEDMA_syncABCfg            syncABCfg;
    DPEDMA_ChainingCfg          chainingCfg;
    int32_t                     retVal;
#ifndef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
    Edma_EventCallback doneCllbackFunc[2] = {NULL, NULL};
    uint32_t            doneCllbackFuncArg[2] = {NULL, NULL};
#endif
    bool                doneTransferCompletionEnabled[2] = {true, true};
    uint32_t    azimFFTOutSize;

    if(obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

    azimFFTOutSize = obj->cfarAzimFFTCfg.numAzimFFTBins
                    * (obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal)
                    * obj->cfarAzimFFTCfg.azimFFTIOCfg.output.bytesPerSample;


    {{

    /* PING */
    chainingCfg.chainingChannel                  = cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PING].channel;
    chainingCfg.isIntermediateChainingEnabled = false;
    chainingCfg.isFinalChainingEnabled        = false;

    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PING_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.azimFFTScratchBuf[PING]);
    syncABCfg.aCount      = azimFFTOutSize;
    syncABCfg.bCount      = 1;
    syncABCfg.cCount      = 1;
    syncABCfg.srcBIdx     = azimFFTOutSize * 2;
    syncABCfg.dstBIdx     = azimFFTOutSize * 2;
    syncABCfg.srcCIdx     = 0;
    syncABCfg.dstCIdx     = 0;

    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PING],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true,//isEventTriggered
                                    true, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PING],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->azimFFTEdmaOutDoneSemaHandle[PING]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaIntrObjAzimFFTOut.pingPong[PING]); /* intrObj */
#else
                                    doneCllbackFunc[PING], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PING], //transferCompletionCallbackFxnArg
                                    NULL); /* intrObj */
#endif

    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

    /* PONG */
    chainingCfg.chainingChannel = cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PONG].channel;
    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_AZIMFFT_PONG_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.azimFFTScratchBuf[PONG]);
    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PONG],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true,//isEventTriggered
                                    true, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PONG],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->azimFFTEdmaOutDoneSemaHandle[PONG]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaIntrObjAzimFFTOut.pingPong[PONG]); /* intrObj */
#else
                                    doneCllbackFunc[PONG], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PONG], //transferCompletionCallbackFxnArg
                                    NULL); /* intrObj */
#endif

    }}

exit:
    return(retVal);

}

/**
 *  @b Description
 *  @n
 *  EDMA Configuration to send CFAR data out to L2 from HWA
 *  This implementation of doppler processing involves Ping/Pong
 *  Mechanism, hence there are two sets of EDMA transfer.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval EDMA error code, see EDMA API.
 */
static  int32_t DPU_DopplerProcHWA_configEdmaCfarOut
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
)
{

    DPEDMA_syncABCfg            syncABCfg;
    DPEDMA_ChainingCfg          chainingCfg;
    int32_t                     retVal;
#ifndef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
    Edma_EventCallback doneCllbackFunc[2] = {NULL, NULL};
    uint32_t            doneCllbackFuncArg[2] = {NULL, NULL};
#endif
    bool                doneTransferCompletionEnabled[2] = {true, true};

    if(obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

    {{

    /* PING */
    chainingCfg.chainingChannel                  = cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaCfarOut.pingPong[PING].channel;
    chainingCfg.isIntermediateChainingEnabled = false;
    chainingCfg.isFinalChainingEnabled        = false;

    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PING_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.cfarScratchBuf[PING]);
    syncABCfg.aCount      = cfg->hwRes.maxCfarPeaksToDetect * obj->cfarAzimFFTCfg.cfarIOCfg.output.bytesPerSample;
    syncABCfg.bCount      = 1;
    syncABCfg.cCount      = 1;
    syncABCfg.srcBIdx     = DECOMP_HWA_MEMBANK_SIZE * 2 - 1;
    syncABCfg.dstBIdx     = DECOMP_HWA_MEMBANK_SIZE * 2 - 1;
    syncABCfg.srcCIdx     = 0;
    syncABCfg.dstCIdx     = 0;

    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaCfarOut.pingPong[PING],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true,//isEventTriggered // UPON HWA COMPLETION
                                    true, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PING],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->dopCFAREdmaOutDoneSemaHandle[PING]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaIntrObjCfarOut.pingPong[PING]); /* intrObj */
#else
                                    doneCllbackFunc[PING], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PING], //transferCompletionCallbackFxnArg
                                    NULL); /* intrObj */
#endif

    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

    /* PONG */
    chainingCfg.chainingChannel = cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaCfarOut.pingPong[PONG].channel;
    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_CFAR_PONG_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.cfarScratchBuf[PONG]);
    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaCfarOut.pingPong[PONG],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true,//isEventTriggered
                                    true, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PONG],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->dopCFAREdmaOutDoneSemaHandle[PONG]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaIntrObjCfarOut.pingPong[PONG]); /* intrObj */
#else
                                    doneCllbackFunc[PONG], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PONG], //transferCompletionCallbackFxnArg
                                    NULL); /* intrObj */

#endif
    }}

exit:
    return(retVal);

}

/**
 *  @b Description
 *  @n
 *  EDMA Configuration to send Local Max data out to L2 from HWA
 *  This implementation of doppler processing involves Ping/Pong
 *  Mechanism, hence there are two sets of EDMA transfer.
 *
 *  @param[in] obj    - DPU obj
 *  @param[in] cfg    - DPU configuration
 *
 *  \ingroup    DPU_DOPPLERPROC_INTERNAL_FUNCTION
 *
 *  @retval EDMA error code, see EDMA API.
 */
static  int32_t DPU_DopplerProcHWA_configEdmaLocalMaxOut
(
    DPU_DopplerProcHWA_Obj      *obj,
    DPU_DopplerProcHWA_Config   *cfg
)
{

    DPEDMA_syncABCfg            syncABCfg;
    DPEDMA_ChainingCfg          chainingCfg;
    int32_t                     retVal;
#ifndef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
    Edma_EventCallback doneCllbackFunc[2] = {NULL, NULL};
    uint32_t            doneCllbackFuncArg[2] = {NULL, NULL};
#endif
    bool                doneTransferCompletionEnabled[2] = {true, true};
    uint32_t    localMaxOutSize;

    if(obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

    if(obj->cfarAzimFFTCfg.numAzimFFTBins % 32 == 0){
        localMaxOutSize = (obj->cfarAzimFFTCfg.numAzimFFTBins / 32)
                            * (obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal) * 4;
    }
    else{
        localMaxOutSize = ((obj->cfarAzimFFTCfg.numAzimFFTBins / 32) + 1)
                            * (obj->numDopplerBins / obj->dopplerDemodCfg.numBandsTotal) * 4;
    }

    {{

    /* PING */
    chainingCfg.chainingChannel                  = cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[PING].channel;
    chainingCfg.isIntermediateChainingEnabled = false;
    chainingCfg.isFinalChainingEnabled        = false;

    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_LOCALMAX_PING_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.localMaxScratchBuf[PING]);
    syncABCfg.aCount      = localMaxOutSize;
    syncABCfg.bCount      = 1;
    syncABCfg.cCount      = 1;
    syncABCfg.srcBIdx     = localMaxOutSize * 2;
    syncABCfg.dstBIdx     = localMaxOutSize * 2;
    syncABCfg.srcCIdx     = 0;
    syncABCfg.dstCIdx     = 0;

    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[PING],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true, // UPON HWA COMPLETION
                                    true, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PING],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->localMaxEdmaOutDoneSemaHandle[PING]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaIntrObjLocalMaxOut.pingPong[PING]); /* intrObj */
#else
                                    doneCllbackFunc[PING], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PING], //transferCompletionCallbackFxnArg
                                    NULL); /* intrObj */
#endif

    if (retVal != SystemP_SUCCESS)
    {
        goto exit;
    }

    /* PONG */
    chainingCfg.chainingChannel = cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[PONG].channel;
    syncABCfg.srcAddress  = (uint32_t)(obj->hwaMemBankAddr[DPU_DOPPLERHWADDMA_MEM_BANK_LOCALMAX_PONG_OUT]);
    syncABCfg.destAddress = (uint32_t)(cfg->hwRes.localMaxScratchBuf[PONG]);
    retVal = DPEDMA_configSyncAB(   obj->edmaHandle,
                                    &cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[PONG],
                                    &chainingCfg,
                                    &syncABCfg,
                                    true,//isEventTriggered
                                    true, //isIntermediateTransferCompletionEnabled
                                    doneTransferCompletionEnabled[PONG],//isTransferCompletionEnabled
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                                    DPU_DopplerProcHWA_edmaDoneIsrCallback, //transferCompletionCallbackFxn
                                    (void *)((uint32_t)&obj->localMaxEdmaOutDoneSemaHandle[PONG]), //transferCompletionCallbackFxnArg
                                    cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaIntrObjLocalMaxOut.pingPong[PONG]); /* intrObj */
#else
                                    doneCllbackFunc[PONG], //transferCompletionCallbackFxn
                                    (void *)doneCllbackFuncArg[PONG], //transferCompletionCallbackFxnArg
                                    NULL); /* intrObj */
#endif

    }}

exit:
    return(retVal);

}



/*===========================================================
 *                    Doppler Proc External APIs
 *===========================================================*/

/**
 *  @b Description
 *  @n
 *      dopplerProc DPU init function. It allocates memory to store
 *  its internal data object and returns a handle if it executes successfully.
 *
 *  @param[in]   initCfg Pointer to initial configuration parameters
 *  @param[in]   subframeCounter subFrame index for dpu initialization
 *  @param[out]  errCode Pointer to errCode generates by the API
 *
 *  \ingroup    DPU_DOPPLERPROC_EXTERNAL_FUNCTION
 *
 *  @retval
 *      Success     - valid handle
 *  @retval
 *      Error       - NULL
 */
DPU_DopplerProcHWA_Handle DPU_DopplerProcHWA_init
(
    DPU_DopplerProcHWA_InitParams *initCfg,
    volatile uint8_t       subframeCounter,
    int32_t                       *errCode
)
{
    DPU_DopplerProcHWA_Obj  *obj = NULL;
    HWA_MemInfo             hwaMemInfo;
    uint32_t                i;
    int32_t             status = SystemP_SUCCESS;

    *errCode       = 0;

    if((initCfg == NULL) || (initCfg->hwaHandle == NULL))
    {
        *errCode = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

    obj = (DPU_DopplerProcHWA_Obj *)&dopplerProcObjPool[subframeCounter];
    if(obj == NULL)
    {
        *errCode = DPU_DOPPLERPROCHWA_ENOMEM;
        goto exit;
    }

    /* Initialize memory */
    memset((void *)obj, 0U, sizeof(DPU_DopplerProcHWA_Obj));

    /* Save init config params */
    obj->hwaHandle   = initCfg->hwaHandle;

    /* Creating semaphores */
    {{

    /* Create semaphore for HWA decompression done */
    status = SemaphoreP_constructBinary(&obj->decompEdmaOutDoneSemaHandle, 0);
    if(status != SystemP_SUCCESS)
    {
        *errCode = DPU_DOPPLERPROCHWA_ESEMA;
        goto exit;
    }

#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
    for(i=0; i<2; i++)
    {
        /* Create semaphore for HWA doppler FFT done */
        status = SemaphoreP_constructBinary(&obj->dopFFTEdmaOutDoneSemaHandle[i], 0);
        if(status != SystemP_SUCCESS)
        {
            *errCode = DPU_DOPPLERPROCHWA_ESEMA;
            goto exit;
        }

        /* Create semaphore for HWA DDMA Metric done */
        status = SemaphoreP_constructBinary(&obj->DDMAMetricEdmaOutDoneSemaHandle[i], 0);
        if(status != SystemP_SUCCESS)
        {
            *errCode = DPU_DOPPLERPROCHWA_ESEMA;
            goto exit;
        }

        /* Create semaphore for HWA sum TX done */
        status = SemaphoreP_constructBinary(&obj->sumTXEdmaOutDoneSemaHandle[i], 0);
        if(status != SystemP_SUCCESS)
        {
            *errCode = DPU_DOPPLERPROCHWA_ESEMA;
            goto exit;
        }

        /* Create semaphore for HWA azimuth FFT done */
        status = SemaphoreP_constructBinary(&obj->azimFFTEdmaOutDoneSemaHandle[i], 0);
        if(status != SystemP_SUCCESS)
        {
            *errCode = DPU_DOPPLERPROCHWA_ESEMA;
            goto exit;
        }

        /* Create semaphore for HWA doppler CFAR done */
        status = SemaphoreP_constructBinary(&obj->dopCFAREdmaOutDoneSemaHandle[i], 0);
        if(status != SystemP_SUCCESS)
        {
            *errCode = DPU_DOPPLERPROCHWA_ESEMA;
            goto exit;
        }

        /* Create semaphore for HWA local max done */
        status = SemaphoreP_constructBinary(&obj->localMaxEdmaOutDoneSemaHandle[i], 0);
        if(status != SystemP_SUCCESS)
        {
            *errCode = DPU_DOPPLERPROCHWA_ESEMA;
            goto exit;
        }
    }
#endif
    }}

    /* Populate HWA base addresses and offsets. This is done only once, at init time.*/
    *errCode =  HWA_getHWAMemInfo(obj->hwaHandle, &hwaMemInfo);
    if (*errCode < 0)
    {
        goto exit;
    }

    /* check if we have enough memory banks*/
    if(hwaMemInfo.numBanks < DPU_DOPPLERPROCHWA_NUM_HWA_MEMBANKS)
    {
        *errCode = DPU_DOPPLERPROCHWA_EHWARES;
        goto exit;
    }

    for (i = 0; i < DPU_DOPPLERPROCHWA_NUM_HWA_MEMBANKS; i++)
    {
        obj->hwaMemBankAddr[i] = hwaMemInfo.baseAddress + i * hwaMemInfo.bankSize;
    }

exit:
    if(*errCode < 0)
    {
        if(obj != NULL)
        {
            obj = NULL;
        }
    }
   return ((DPU_DopplerProcHWA_Handle)obj);
}

/**
  *  @b Description
  *  @n
  *   Doppler DPU configuration
  *
  *  @param[in]   handle     DPU handle.
  *  @param[in]   cfg        Pointer to configuration parameters.
  *  @param[in]   isFullCfg  Perform a full DPU Config (as opposed to a minimal one).
  *
  *  \ingroup    DPU_DOPPLERPROC_EXTERNAL_FUNCTION
  *
  *  @retval
  *      Success      = 0
  *  @retval
  *      Error       != 0 @ref DPU_DOPPLERPROC_ERROR_CODE
  */
int32_t DPU_DopplerProcHWA_config
(
    DPU_DopplerProcHWA_Handle    handle,
    DPU_DopplerProcHWA_Config    *cfg,
	int32_t isFullCfg
)
{
    DPU_DopplerProcHWA_Obj   *obj;
    int32_t                  retVal = 0;
    uint32_t idx;
    int32_t                 scratchVal;

#if defined(SOC_AWR294X)
    volatile uint8_t PGVer = SOC_RCM_ES1_PG_VER;
    PGVer=SOC_rcmGetEfusePGVer();
#endif

    obj = (DPU_DopplerProcHWA_Obj *)handle;
    if((obj == NULL) || (cfg==NULL))
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

	if (isFullCfg == 1)
	{
		obj->edmaHandle  = cfg->hwRes.edmaCfg.edmaHandle;

		obj->numDopplerChirps = cfg->staticCfg.numChirps;
		obj->numDopplerBins = mathUtils_getValidFFTSize(cfg->staticCfg.numChirps);

		/* Decompression params */
		{{
	    obj->decompCfg.isEnabled = cfg->staticCfg.decompCfg.isEnabled;

	    obj->decompCfg.compressionMethod = cfg->staticCfg.decompCfg.compressionMethod;
	    obj->decompCfg.bytesPerSample = sizeof(cmplx16ImRe_t);
	    obj->decompCfg.rxAntPerBlock = cfg->staticCfg.decompCfg.numRxAntennaPerBlock;
	    obj->decompCfg.rangeBinsPerBlock = cfg->staticCfg.decompCfg.rangeBinsPerBlock;
        if(obj->decompCfg.compressionMethod == HWA_COMPRESS_METHOD_BFP)
        {
            obj->decompCfg.outputSamplesPerBlock = cfg->staticCfg.decompCfg.rangeBinsPerBlock;
        }
        else
        {
            obj->decompCfg.outputSamplesPerBlock = obj->decompCfg.rxAntPerBlock * cfg->staticCfg.decompCfg.rangeBinsPerBlock;
        }
	    obj->decompCfg.outputBytesPerBlock = obj->decompCfg.outputSamplesPerBlock * obj->decompCfg.bytesPerSample;
	    obj->decompCfg.numBlocks = cfg->staticCfg.numRangeBins * cfg->staticCfg.numRxAntennas / obj->decompCfg.outputSamplesPerBlock;
	    obj->decompCfg.inputBytesPerBlock = (uint16_t)(((uint16_t) ((cfg->staticCfg.decompCfg.compressionRatio *
	                                    obj->decompCfg.outputBytesPerBlock + 3)/4)) * 4); /* Word aligned */
	    obj->decompCfg.inputSamplesPerBlock = obj->decompCfg.inputBytesPerBlock / obj->decompCfg.bytesPerSample;
	    obj->decompCfg.achievedCompressionRatio = (float) obj->decompCfg.inputBytesPerBlock / obj->decompCfg.outputBytesPerBlock;
	    obj->decompCfg.decompEdmaToHwaStartAddress = (void *)cfg->hwRes.radarCube.data;
        if(obj->decompCfg.compressionMethod == HWA_COMPRESS_METHOD_BFP)
        {
            obj->decompCfg.numChirpsPerPing = DECOMP_HWA_MEMBANK_SIZE / (obj->decompCfg.outputBytesPerBlock * obj->decompCfg.rxAntPerBlock);

            /* dstCIdx of DPU_DopplerProcHWA_configEdmaDecompressionOut must be less that 32768 */
            if((obj->decompCfg.numChirpsPerPing * obj->decompCfg.rxAntPerBlock * obj->decompCfg.outputBytesPerBlock * 2) >= 32768){
                obj->decompCfg.numChirpsPerPing = obj->decompCfg.numChirpsPerPing/2;
            }
            obj->decompCfg.outerBlockSizeCompressed = obj->decompCfg.inputBytesPerBlock * obj->decompCfg.rxAntPerBlock * cfg->staticCfg.numChirps;
        }
        else
        {
            obj->decompCfg.numChirpsPerPing = DECOMP_HWA_MEMBANK_SIZE / obj->decompCfg.outputBytesPerBlock;

            /* dstCIdx of DPU_DopplerProcHWA_configEdmaDecompressionOut must be less that 32768 */
            if((obj->decompCfg.numChirpsPerPing * obj->decompCfg.outputBytesPerBlock * 2) >= 32768){
                obj->decompCfg.numChirpsPerPing = obj->decompCfg.numChirpsPerPing/2;
            }
            obj->decompCfg.outerBlockSizeCompressed = obj->decompCfg.inputBytesPerBlock * cfg->staticCfg.numChirps;
        }

	    obj->decompCfg.numOuterBlocks = cfg->staticCfg.numRangeBins / cfg->staticCfg.decompCfg.rangeBinsPerBlock;

	    /* numLoops is ceil(numChirps/numChirpsPerPing)/2 */
	    if(cfg->staticCfg.numChirps % obj->decompCfg.numChirpsPerPing == 0){
	        obj->decompCfg.numLoops = (cfg->staticCfg.numChirps / obj->decompCfg.numChirpsPerPing);
	        if(obj->decompCfg.numLoops % 2 == 0){
	            obj->decompCfg.numLoops = obj->decompCfg.numLoops/2;
	        }
	    }
	    else{
	        obj->decompCfg.numLoops = (cfg->staticCfg.numChirps / obj->decompCfg.numChirpsPerPing + 1);
	        if(obj->decompCfg.numLoops % 2 == 0){
	            obj->decompCfg.numLoops = obj->decompCfg.numLoops/2;
	        }
	    }
	    if(obj->decompCfg.numLoops < 1){
	        obj->decompCfg.numLoops = 2;
	    }
			//CacheP_wbInv(obj, sizeof(DPU_DopplerProcHWA_Obj), CacheP_TYPE_ALLD);
	    obj->decompCfg.numChirpsPerPing = cfg->staticCfg.numChirps/(obj->decompCfg.numLoops * 2);
	    if(obj->decompCfg.numChirpsPerPing < 1){
	        retVal = DPU_DOPPLERPROCHWA_ERROR_NUMCHIRPSPERPING;
	        goto exit;
	    }

        if(obj->decompCfg.compressionMethod == HWA_COMPRESS_METHOD_BFP)
        {
            obj->decompCfg.numBlocksPerPing = obj->decompCfg.numChirpsPerPing*obj->decompCfg.rxAntPerBlock;
        }
        else
        {
            obj->decompCfg.numBlocksPerPing = obj->decompCfg.numChirpsPerPing;
        }

	    if(obj->decompCfg.rxAntPerBlock != cfg->staticCfg.numRxAntennas){
	        retVal = DPU_DOPPLERPROCHWA_ERROR_NUMRXANTPERBLOCK_DECOMPRESSION;
	        goto exit;
	    }

	    /* RangeBinsPerBlock should be a power of 2 and greater > 1 */
	    if(obj->decompCfg.rangeBinsPerBlock <=1 || ((obj->decompCfg.rangeBinsPerBlock & (obj->decompCfg.rangeBinsPerBlock - 1)) != 0)){
	        retVal = DPU_DOPPLERPROCHWA_ERROR_RANGEBINSPERBLOCK_DECOMPRESSION;
	        goto exit;
	    }

	    if((obj->decompCfg.compressionMethod != HWA_COMPRESS_METHOD_EGE)&&(obj->decompCfg.compressionMethod != HWA_COMPRESS_METHOD_BFP)){
	        retVal = DPU_DOPPLERPROCHWA_ERROR_METHOD_DECOMPRESSION;
	        goto exit;
	    }

	    /* Populate HWA Common config structure */

        obj->decompCfg.hwaCommonConfig.configMask = HWA_COMMONCONFIG_MASK_STATEMACHINE_CFG; /* numLoops, paramStartIdx, paramStopIdx combined here */

        if(obj->decompCfg.compressionMethod == HWA_COMPRESS_METHOD_EGE)
        {
            obj->decompCfg.hwaCommonConfig.configMask |= HWA_COMMONCONFIG_MASK_EGECOMRESS_KPARAM;

            /* EGE Compression values */
            obj->decompCfg.hwaCommonConfig.compressConfig.EGEKparam[0] = 3;
            obj->decompCfg.hwaCommonConfig.compressConfig.EGEKparam[1] = 4;
            obj->decompCfg.hwaCommonConfig.compressConfig.EGEKparam[2] = 5;
            obj->decompCfg.hwaCommonConfig.compressConfig.EGEKparam[3] = 7;
            obj->decompCfg.hwaCommonConfig.compressConfig.EGEKparam[4] = 9;
            obj->decompCfg.hwaCommonConfig.compressConfig.EGEKparam[5] = 11;
            obj->decompCfg.hwaCommonConfig.compressConfig.EGEKparam[6] = 13;
            obj->decompCfg.hwaCommonConfig.compressConfig.EGEKparam[7] = 15;
        }

#if defined(SOC_AWR294X)
    if(PGVer == SOC_RCM_ES2_PG_VER){
        obj->decompCfg.hwaCommonConfig.configMask |= HWA_COMMONCONFIG_MASK_CMP_LFSRSEED0;
        obj->decompCfg.hwaCommonConfig.compressConfig.cmpLfsrSeed0 = 0x0000000B; /*Some non-zero value*/
    }
#endif
	    obj->decompCfg.hwaCommonConfig.numLoops = obj->decompCfg.numLoops;
	    obj->decompCfg.hwaCommonConfig.paramStartIdx = cfg->hwRes.hwaCfg.decompStageHwaStateMachineCfg.paramSetStartIdx;
	    obj->decompCfg.hwaCommonConfig.paramStopIdx = cfg->hwRes.hwaCfg.decompStageHwaStateMachineCfg.paramSetStartIdx + cfg->hwRes.hwaCfg.decompStageHwaStateMachineCfg.numParamSets - 1U;

	    obj->decompCfg.hwaDmaTriggerSourcePingPongIn[PING] = obj->decompCfg.hwaCommonConfig.paramStartIdx + DECOMP_PING_HWA_PARAMSET_RELATIVE_IDX;
	    obj->decompCfg.hwaDmaTriggerSourcePingPongIn[PONG] = obj->decompCfg.hwaCommonConfig.paramStartIdx + DECOMP_PONG_HWA_PARAMSET_RELATIVE_IDX + cfg->staticCfg.decompCfg.bfpCompExtraParamSets/2U;

	    retVal = DPU_DopplerProcHWA_configHwaDecompression(obj, cfg);
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    retVal = DPU_DopplerProcHWA_configEdmaDecompressionIn(obj, cfg);
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    retVal = DPU_DopplerProcHWA_configEdmaDecompressionOut(obj, cfg);
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    }}

	    /* Doppler demod params */
	    {{
	    obj->dopplerDemodCfg.numBandsActive = cfg->staticCfg.numTxAntennas;

	    /* Empty subbands */
	    switch (cfg->staticCfg.numTxAntennas)
	    {
	        case 2:
	            obj->dopplerDemodCfg.numBandsEmpty = 1;
	            break;
	        case 3:
	            obj->dopplerDemodCfg.numBandsEmpty = 1;
	            break;
	        case 4:
	            obj->dopplerDemodCfg.numBandsEmpty = 2;
	            break;
            case 6:
                /* 2-Chip Cascade case */
                obj->dopplerDemodCfg.numBandsEmpty = 2;
                break;
	        default:
	            retVal = DPU_DOPPLERPROCHWA_EINVAL;
	            goto exit;
	    }

	    obj->dopplerDemodCfg.numBandsTotal = obj->dopplerDemodCfg.numBandsActive + obj->dopplerDemodCfg.numBandsEmpty;
	    if (obj->dopplerDemodCfg.numBandsTotal != cfg->staticCfg.numBandsTotal){
	        retVal = DPU_DOPPLERPROCHWA_EINVAL;
	        goto exit;
	    }

	    obj->dopplerDemodCfg.dopplerIOCfg.input.isReal = 0;
	    obj->dopplerDemodCfg.dopplerIOCfg.input.bytesPerSample = sizeof(cmplx16ImRe_t);
	    obj->dopplerDemodCfg.dopplerIOCfg.input.isSigned = 1;
	    obj->dopplerDemodCfg.dopplerIOCfg.output.isReal = 0;
	    obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample = sizeof(cmplx32ImRe_t);
	    obj->dopplerDemodCfg.dopplerIOCfg.output.isSigned = 1;

	    obj->dopplerDemodCfg.logAbsIOCfg.input.isReal = obj->dopplerDemodCfg.dopplerIOCfg.output.isReal;
	    obj->dopplerDemodCfg.logAbsIOCfg.input.bytesPerSample = obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample;
	    obj->dopplerDemodCfg.logAbsIOCfg.input.isSigned = obj->dopplerDemodCfg.dopplerIOCfg.output.isSigned;
	    obj->dopplerDemodCfg.logAbsIOCfg.output.isReal = 1;
	    obj->dopplerDemodCfg.logAbsIOCfg.output.bytesPerSample = sizeof(uint16_t);
	    obj->dopplerDemodCfg.logAbsIOCfg.output.isSigned = 0;

	    obj->dopplerDemodCfg.sumRxIOCfg.input.isReal = obj->dopplerDemodCfg.logAbsIOCfg.output.isReal;
	    obj->dopplerDemodCfg.sumRxIOCfg.input.bytesPerSample = obj->dopplerDemodCfg.logAbsIOCfg.output.bytesPerSample;
	    obj->dopplerDemodCfg.sumRxIOCfg.input.isSigned = obj->dopplerDemodCfg.logAbsIOCfg.output.isSigned;
	    obj->dopplerDemodCfg.sumRxIOCfg.output.isReal = 1;
	    obj->dopplerDemodCfg.sumRxIOCfg.output.bytesPerSample = sizeof(uint16_t);
	    obj->dopplerDemodCfg.sumRxIOCfg.output.isSigned = 0;

	    obj->dopplerDemodCfg.DDMAMetricIOCfg.input.isReal = obj->dopplerDemodCfg.sumRxIOCfg.output.isReal;
	    obj->dopplerDemodCfg.DDMAMetricIOCfg.input.bytesPerSample = obj->dopplerDemodCfg.sumRxIOCfg.output.bytesPerSample;
	    obj->dopplerDemodCfg.DDMAMetricIOCfg.input.isSigned = obj->dopplerDemodCfg.sumRxIOCfg.output.isSigned;
	    obj->dopplerDemodCfg.DDMAMetricIOCfg.output.isReal = 1;
	    obj->dopplerDemodCfg.DDMAMetricIOCfg.output.bytesPerSample = sizeof(uint32_t);
	    obj->dopplerDemodCfg.DDMAMetricIOCfg.output.isSigned = 0;

	    obj->dopplerDemodCfg.sumTxIOCfg.input.isReal = obj->dopplerDemodCfg.sumRxIOCfg.output.isReal;
	    obj->dopplerDemodCfg.sumTxIOCfg.input.bytesPerSample = obj->dopplerDemodCfg.sumRxIOCfg.output.bytesPerSample;
	    obj->dopplerDemodCfg.sumTxIOCfg.input.isSigned = obj->dopplerDemodCfg.sumRxIOCfg.output.isSigned;
	    obj->dopplerDemodCfg.sumTxIOCfg.output.isReal = 1;
	    obj->dopplerDemodCfg.sumTxIOCfg.output.bytesPerSample = sizeof(uint16_t);
	    obj->dopplerDemodCfg.sumTxIOCfg.output.isSigned = 0;

	    obj->dopplerDemodCfg.hwaDmaTriggerSourcePingPongIn[PING] = cfg->hwRes.hwaCfg.dopplerStageHwaStateMachineCfg.paramSetStartIdx + DPU_DOPPLERHWADDMA_DOPPLER_FFT_PING_HWA_PARAMSET_RELATIVE_IDX - 1;
	    if(cfg->staticCfg.isSumTxEnabled){
	        obj->dopplerDemodCfg.hwaDmaTriggerSourcePingPongIn[PONG] = cfg->hwRes.hwaCfg.dopplerStageHwaStateMachineCfg.paramSetStartIdx + DPU_DOPPLERHWADDMA_DOPPLER_FFT_PONG_HWA_PARAMSET_RELATIVE_IDX - 1;
	    }
	    else{
	        obj->dopplerDemodCfg.hwaDmaTriggerSourcePingPongIn[PONG] = cfg->hwRes.hwaCfg.dopplerStageHwaStateMachineCfg.paramSetStartIdx + DPU_DOPPLERHWADDMA_DOPPLER_FFT_PONG_HWA_PARAMSET_RELATIVE_IDX_SUMTX_DISABLED - 1;
	    }

	    retVal = DPU_DopplerProcHWA_configHwaDopplerFFTDDMADemod(obj, cfg);
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    retVal = DPU_DopplerProcHWA_configEdmaDopplerIn(obj, cfg);
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    retVal = DPU_DopplerProcHWA_configEdmaDopplerFFTOut(obj, cfg);
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    retVal = DPU_DopplerProcHWA_configEdmaDDMAMetricOut(obj, cfg);
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    if(cfg->staticCfg.isSumTxEnabled){
	        retVal = DPU_DopplerProcHWA_configEdmaDopplerFFTSumTxOut(obj, cfg);
	        if (retVal != 0)
	        {
	            goto exit;
	        }
	    }



	    }}

	    /* Azimuth FFT - CFAR params */
	    {{

	    obj->cfarAzimFFTCfg.numAzimFFTBins = cfg->staticCfg.numAzimFFTBins;

	    obj->cfarAzimFFTCfg.azimFFTIOCfg.input.isReal = 0;
	    obj->cfarAzimFFTCfg.azimFFTIOCfg.input.bytesPerSample = obj->dopplerDemodCfg.dopplerIOCfg.output.bytesPerSample;
	    obj->cfarAzimFFTCfg.azimFFTIOCfg.input.isSigned = obj->dopplerDemodCfg.dopplerIOCfg.output.isSigned;
	    obj->cfarAzimFFTCfg.azimFFTIOCfg.output.isReal = 1;
	    obj->cfarAzimFFTCfg.azimFFTIOCfg.output.bytesPerSample = sizeof(uint16_t);
	    obj->cfarAzimFFTCfg.azimFFTIOCfg.output.isSigned = 0;

	    obj->cfarAzimFFTCfg.cfarIOCfg.input.isReal = 1;
	    obj->cfarAzimFFTCfg.cfarIOCfg.input.bytesPerSample = obj->cfarAzimFFTCfg.azimFFTIOCfg.output.bytesPerSample;
	    obj->cfarAzimFFTCfg.cfarIOCfg.input.isSigned = obj->cfarAzimFFTCfg.azimFFTIOCfg.output.isSigned;
	    obj->cfarAzimFFTCfg.cfarIOCfg.output.isReal = 0;
	    obj->cfarAzimFFTCfg.cfarIOCfg.output.bytesPerSample = sizeof(cmplx32ImRe_t);
	    obj->cfarAzimFFTCfg.cfarIOCfg.output.isSigned = 0;

	    obj->cfarAzimFFTCfg.localMaxIOCfg.input.isReal = 1;
	    obj->cfarAzimFFTCfg.localMaxIOCfg.input.bytesPerSample = obj->cfarAzimFFTCfg.azimFFTIOCfg.output.bytesPerSample;
	    obj->cfarAzimFFTCfg.localMaxIOCfg.input.isSigned = obj->cfarAzimFFTCfg.azimFFTIOCfg.output.isSigned;
	    obj->cfarAzimFFTCfg.localMaxIOCfg.output.isReal = 1;
	    obj->cfarAzimFFTCfg.localMaxIOCfg.output.bytesPerSample = sizeof(uint32_t);
	    obj->cfarAzimFFTCfg.localMaxIOCfg.output.isSigned = 0;

	    obj->cfarAzimFFTCfg.cfarCfg.averageMode = cfg->staticCfg.cfarCfg.averageMode; /* CFAR_OS */
	    if(obj->cfarAzimFFTCfg.cfarCfg.averageMode != 3){ /* CFAR_OS */
	        retVal = DPU_DOPPLERPROCHWA_ERROR_METHOD_CFAR;
	        goto exit;
	    }
	    obj->cfarAzimFFTCfg.cfarCfg.winLen = cfg->staticCfg.cfarCfg.winLen;
	    obj->cfarAzimFFTCfg.cfarCfg.noiseDivShift = cfg->staticCfg.cfarCfg.noiseDivShift;
	    obj->cfarAzimFFTCfg.cfarCfg.guardLen = cfg->staticCfg.cfarCfg.guardLen;  /* Not applicable for CFAR-OS */
	    if(obj->cfarAzimFFTCfg.cfarCfg.guardLen != 0){ /* CFAR_OS */
	        retVal = DPU_DOPPLERPROCHWA_ERROR_METHOD_CFAR;
	        goto exit;
	    }
	    obj->cfarAzimFFTCfg.cfarCfg.cyclicMode = cfg->staticCfg.cfarCfg.cyclicMode;
	    obj->cfarAzimFFTCfg.cfarCfg.peakGroupingScheme = cfg->staticCfg.cfarCfg.peakGroupingScheme;
	    obj->cfarAzimFFTCfg.cfarCfg.peakGroupingEn = cfg->staticCfg.cfarCfg.peakGroupingEn;
	    obj->cfarAzimFFTCfg.cfarCfg.osKvalue = cfg->staticCfg.cfarCfg.osKvalue;
	    obj->cfarAzimFFTCfg.cfarCfg.osEdgeKscaleEn = cfg->staticCfg.cfarCfg.osEdgeKscaleEn;
	    obj->cfarAzimFFTCfg.cfarCfg.thresholdScale = cfg->staticCfg.cfarCfg.thresholdScale;

	    obj->cfarAzimFFTCfg.localMaxCfg.azimThreshold = cfg->staticCfg.localMaxCfg.azimThreshold;
	    obj->cfarAzimFFTCfg.localMaxCfg.dopplerThreshold = cfg->staticCfg.localMaxCfg.dopplerThreshold;

        /* HWA has 32 DMA chnannels;
         * In BFP compression, paramsetIdx becomes greater than 32 for azimuth stage,
         * so paramset trigger source cannot be paramsetIdx.
         * Thus, keeping the trigger source as the DMA channel number
         */
        retVal = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTInSignature.pingPong[0].channel, &obj->cfarAzimFFTCfg.hwaDmaTriggerSourcePingPongIn[PING]);
        if (retVal != 0)
        {
            goto exit;
        }

        retVal = HWA_getDMAChanIndex(obj->hwaHandle, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTInSignature.pingPong[1].channel, &obj->cfarAzimFFTCfg.hwaDmaTriggerSourcePingPongIn[PONG]);
        if (retVal != 0)
        {
            goto exit;
        }

	    /* Evaluate the float calb params into quantized values acceptable to HWA */
	    retVal = mathUtils_asymQuantInt(cfg->staticCfg.antennaCalibParams,
	                                    (void *)obj->cfarAzimFFTCfg.antennaCalibParamsQuantized,
	                                    cfg->staticCfg.numTxAntennas * cfg->staticCfg.numRxAntennas * 2,
	                                    1,
	                                    20,
	                                    1); /* Signed */
	    if (retVal != 0)
	    {
	        goto exit;
	    }

#ifndef CASCADE_EVM
		/* vector multiplication vector */
		for (idx = 0; idx < (cfg->staticCfg.numTxAntennas * cfg->staticCfg.numRxAntennas * 2); idx+=2)
	    {

	        obj->dopplerAzimHwaCommonConfig.complexMultiplyConfig.Iscale[idx/2] = obj->cfarAzimFFTCfg.antennaCalibParamsQuantized[idx+1]; /* Q20 format */
	        obj->dopplerAzimHwaCommonConfig.complexMultiplyConfig.Qscale[idx/2] = obj->cfarAzimFFTCfg.antennaCalibParamsQuantized[idx];
	    }
#else

		/* vector multiplication vector */
		/* While the rest of the processing expects real,imag format the vector multiplication module expects data in the imag,real format. */
		for (idx = 0; idx < (cfg->staticCfg.numTxAntennas * cfg->staticCfg.numRxAntennas * 2); idx+=2)
	    {
			int32_t temp;

	        temp = obj->cfarAzimFFTCfg.antennaCalibParamsQuantized[idx+1]; /* Q20 format */
	        obj->cfarAzimFFTCfg.antennaCalibParamsQuantized[idx+1] = obj->cfarAzimFFTCfg.antennaCalibParamsQuantized[idx];
	        obj->cfarAzimFFTCfg.antennaCalibParamsQuantized[idx] = temp;
	    }
        /* Populate Vector Multiply RAM in HWA */
        retVal = HWA_configRam(obj->hwaHandle, HWA_RAM_TYPE_VECTORMULTIPLY_RAM, (uint8_t *)&obj->cfarAzimFFTCfg.antennaCalibParamsQuantized[0], sizeof(obj->cfarAzimFFTCfg.antennaCalibParamsQuantized), 0);
        if (retVal != 0)
        {
            goto exit;
        }
#endif
	    /* Configure HWA paramsets */
	    retVal = DPU_DopplerProcHWA_configHwaCFARAzimFFT(obj, cfg);
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    /* Configure EDMA for sending Doppler FFT data as input to HWA */
	    retVal = DPU_DopplerProcHWA_configEdmaAzimFFTIn(obj, cfg, (uint32_t)(cfg->hwRes.dopFFTSubMat));
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    /* Configure EDMA to bring Azimuth FFT data out of HWA */
	    retVal = DPU_DopplerProcHWA_configEdmaAzimFFTOut(obj, cfg);
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    /* Configure EDMA to bring CFAR data out of HWA */
	    retVal = DPU_DopplerProcHWA_configEdmaCfarOut(obj, cfg);
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    /* Configure EDMA to bring LM data out of HWA */
	    retVal = DPU_DopplerProcHWA_configEdmaLocalMaxOut(obj, cfg);
	    if (retVal != 0)
	    {
	        goto exit;
	    }

	    }}

	    /* Config Common Registers */
	    #ifdef CASCADE_EVM
	    obj->dopplerAzimHwaCommonConfig.configMask = HWA_COMMONCONFIG_MASK_STATEMACHINE_CFG |/* numLoops, paramStartIdx, paramStopIdx combined here */
	                                                HWA_COMMONCONFIG_MASK_TWIDDITHERENABLE |
	                                                HWA_COMMONCONFIG_MASK_LFSRSEED |
	                                                HWA_COMMONCONFIG_MASK_CFARTHRESHOLDSCALE |
	                                                HWA_COMMONCONFIG_MASK_MAX2D_OFFSETBOTHDIM;
		#else
		obj->dopplerAzimHwaCommonConfig.configMask = HWA_COMMONCONFIG_MASK_STATEMACHINE_CFG |/* numLoops, paramStartIdx, paramStopIdx combined here */
	                                                HWA_COMMONCONFIG_MASK_TWIDDITHERENABLE |
	                                                HWA_COMMONCONFIG_MASK_LFSRSEED |
	                                                HWA_COMMONCONFIG_MASK_CFARTHRESHOLDSCALE |
	                                                HWA_COMMONCONFIG_MASK_MAX2D_OFFSETBOTHDIM |
	                                                HWA_COMMONCONFIG_MASK_COMPLEXMULT_SCALEARRAY;
		#endif
	    obj->dopplerAzimHwaCommonConfig.fftConfig.twidDitherEnable = HWA_FEATURE_BIT_ENABLE;
	    obj->dopplerAzimHwaCommonConfig.fftConfig.lfsrSeed = 0x0000000B; /*Some non-zero value*/
	    obj->dopplerAzimHwaCommonConfig.numLoops      = cfg->staticCfg.decompCfg.rangeBinsPerBlock / 2;

	    /* Populate Shuffle LUT RAM contents in array */
	    for(idx = 0; idx < obj->dopplerDemodCfg.numBandsTotal; idx++){
	        cfg->hwRes.shuffleRAM[idx] = idx * (obj->numDopplerBins/obj->dopplerDemodCfg.numBandsTotal);
	    }

#ifdef CASCADE_EVM
        /* Populate Shuffle LUT RAM contents for the azim FFT re-arrangement in array */
	    for(idx = 0; idx < (NUM_RXANT * NUM_TXANT_AZIM ); idx++){
	        cfg->hwRes.shuffleRAM[(SHUFFLE_LUT_OFFSET/2) + idx] = arrayToAntMapping[idx];
	    }
#endif

	    /* Populate Shuffle LUT RAM in HWA */
	    retVal = HWA_configRam(obj->hwaHandle, HWA_RAM_TYPE_SHUFFLE_RAM, (uint8_t *)&cfg->hwRes.shuffleRAM[0], sizeof(cfg->hwRes.shuffleRAM), 0);
	    if (retVal != 0)
	    {
	        goto exit;
	    }


	    /* 2D maximum value offset */
        #define CONST_LOG2_10 (3.3219f)
        scratchVal = round((float)(obj->cfarAzimFFTCfg.localMaxCfg.azimThreshold)* ((1.0f/20.0f) * CONST_LOG2_10 * 2048.0f));
	    obj->dopplerAzimHwaCommonConfig.advStatConfig.max2DoffsetDim1 = -scratchVal;
        scratchVal = round((float)(obj->cfarAzimFFTCfg.localMaxCfg.dopplerThreshold)* ((1.0f/20.0f) * CONST_LOG2_10 * 2048.0f));
	    obj->dopplerAzimHwaCommonConfig.advStatConfig.max2DoffsetDim2 = -scratchVal;

	    obj->dopplerAzimHwaCommonConfig.numLoops = cfg->staticCfg.decompCfg.rangeBinsPerBlock / 2;
	    obj->dopplerAzimHwaCommonConfig.paramStartIdx = cfg->hwRes.hwaCfg.dopplerStageHwaStateMachineCfg.paramSetStartIdx;
	    obj->dopplerAzimHwaCommonConfig.paramStopIdx =  cfg->hwRes.hwaCfg.azimCfarStageHwaStateMachineCfg.paramSetStartIdx + cfg->hwRes.hwaCfg.azimCfarStageHwaStateMachineCfg.numParamSets - 1U; ;
	    obj->dopplerAzimHwaCommonConfig.cfarConfig.thresholdScale = obj->cfarAzimFFTCfg.cfarCfg.thresholdScale;

	    /* Validate params */
	    if(!cfg ||
	       !cfg->hwRes.edmaCfg.edmaHandle ||
	       !cfg->hwRes.hwaCfg.window ||
	       !cfg->hwRes.radarCube.data
	      )
	    {
	        retVal = DPU_DOPPLERPROCHWA_EINVAL;
	        goto exit;
	    }

		if (cfg->staticCfg.isSumTxEnabled)
		{
            if(!cfg->hwRes.detMatrix.data)
            {
                retVal = DPU_DOPPLERPROCHWA_EINVAL;
                goto exit;
	        }
            /* Check if detection matrix size is sufficient*/
		    if(cfg->hwRes.detMatrix.dataSize < (cfg->staticCfg.numRangeBins *
		                                        (obj->numDopplerBins/cfg->staticCfg.numBandsTotal)* sizeof(uint16_t)))
		    {
		        retVal = DPU_DOPPLERPROCHWA_EDETMSIZE;
		        goto exit;
		    }
		}

	    /* Even though Window RAM is not used by the first stage, there's no issue
	    in programming it at this point itself */
	    /* HWA window configuration */
	    retVal = HWA_configRam(obj->hwaHandle,
	                           HWA_RAM_TYPE_WINDOW_RAM,
	                           (uint8_t *)cfg->hwRes.hwaCfg.window,
	                           cfg->hwRes.hwaCfg.windowSize, //size in bytes
	                           cfg->hwRes.hwaCfg.winRamOffset * sizeof(int32_t));
	    if (retVal != 0)
	    {
	        goto exit;
	    }
	}
    /* Enable the HWA */
    retVal = HWA_enable(obj->hwaHandle, 1);
    if (retVal != 0)
    {
        goto exit;
    }


exit:
    return retVal;
}

 /**
  *  @b Description
  *  @n Doppler DPU process function.
  *     This is the core doppler processing function and deserves an
  * extensive comment. There are multiple processes happening in
  * parallel in this function. The parallelism is necessitated by the
  * need for performeance. Hence, the function is complicated. This
  * comment will describe the process simply.
  *
  * The doppler processing stage has the following stages
  * STAGE I (DECOMPRESSION)
  * 1. Trigger a DMA to bring a compressed block of
  *    'staticCfg.decompCfg.rangeBinsPerBlock' Range gates from L3
  *    to the HWA.
  * 2. Decompress 'staticCfg.decompCfg.rangeBinsPerBlock' Range gates.
  *    Each Range gate consists of all the samples across chirps and
  *    rx antennas of a particular range bin.
  * 3. Move the decompressed data to another buffer in L3.
  *
  * For each range gate in a decompressed block, the second STAGE of the
  * this processing kicks in.
  * STAGE II (Doppler Processing)
  * 1. Move one range gate (from the decompressed data buffer in L3) to
  *    the HWA.
  * 2. Compute Doppler FFT and use it to compute the DDMA Metric
  *    (and optionally the sum across Tx. )
  * 3. Move the Doppler FFT and DDMA Metric from the HWA to the DSP(L2).
  * 4. In the DSP, use the DDMA Metric to find the correct order of Txs'
  *    by finding the 'empty' subband. This operation is performed per
  *    doppler bin. The DSP also rearranges the Doppler FFT bin in the
  *    the 'correct' order (the correct order has all the virtual
  *    channels organized in a contiguous manner.  It also discards the
  *    empty sub-band for that bin.)
  * 5. Move the rearranged Doppler FFT from the DSP's L2 to the HWA.
  * 6. In the HWA, compute the azimuth FFT across the virtual channels
  *    for the doppler FFT (i.e. for the current range gate). On the
  *    output compute a CFAR in the doppler dimension and a 2D local max
  *    in both the doppler and the azimuth dimension. The result of CFAR
  *    is a list of detected objects, and the output of the local max is
  *    a 2D bit array.
  * 7. Send the azimuth FFT, CFAR and local maxima outputs from the HWA
  *    to the DSP's L2.
  * 8. Compute the intersection of the CFAR and local maxima outputs to
  *    generate the list of detected objects per range gate. The detected
  *    points of the azimuth peak as well as their immediate neighbours
  *    are also stored.
  *
  * These operations use the HWA and DSP concurrently. Hence we process
  * two range gates at a time, so that while the HWA is processing one,
  * the DSP can process the other.
  *
  * Inside the function the following comment snippet is used to explain
  * what is expected to happen in the three cores (DSP, HWA and EDMA) at
  * each stage of the processing.
  *
  * DSP : Some operation.
  * HWA : Another HWA operation.
  * EDMA: A data transfer operation.
  *
  *  @param[in]   handle     DPU handle.
  *  @param[in]   cfg        DPU config.
  *  @param[out]  outParams  Output parameters.
  *
  *  \ingroup    DPU_DOPPLERPROC_EXTERNAL_FUNCTION
  *
  *  @retval
  *      Success     =0
  *  @retval
  *      Error      !=0 @ref DPU_DOPPLERPROC_ERROR_CODE
  */
int32_t DPU_DopplerProcHWA_process
(
    DPU_DopplerProcHWA_Handle    handle,
    DPU_DopplerProcHWA_Config    *cfg,
    DPU_DopplerProcHWA_OutParams *outParams
)
{

    volatile uint32_t   startTime;
    DPU_DopplerProcHWA_Obj *obj;
    int32_t retVal;
    bool                status;
    uint32_t rangeBinIdx;
    uint32_t blockIdx = 0;

#ifndef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
    uint32_t baseAddr, regionId;
#endif
    obj = (DPU_DopplerProcHWA_Obj *)handle;
    if (obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }
    /* Set inProgress state */
    obj->inProgress = true;

    obj->numObjOut = 0;

    startTime = CycleCounterP_getCount32();
#ifndef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
    baseAddr = EDMA_getBaseAddr(obj->edmaHandle);
    regionId = EDMA_getRegionId(obj->edmaHandle);
#endif
    /* decompEdmaToHwaStartAddress gets updated every block to fetch the compressed radar cube for next block.
     * This needs to be reset to the start address of radar cube data before the doppler processing starts.
     */
    obj->decompCfg.decompEdmaToHwaStartAddress = (void*)cfg->hwRes.radarCube.data;

    /* Run block-wise */
    for(blockIdx = 0; blockIdx < obj->decompCfg.numOuterBlocks; blockIdx++)
    {
        /* STAGE I (DECOMPRESSION)                      */
        /* Configure the HWA to perform decompression. */
        retVal = HWA_enable(obj->hwaHandle,0); if (retVal != 0)  {goto exit;}
        retVal = HWA_configCommon(obj->hwaHandle, &obj->decompCfg.hwaCommonConfig);  if (retVal != 0)  {goto exit;}
        retVal = HWA_enable(obj->hwaHandle,1); if (retVal != 0)  {goto exit;}

        /* Update the source address for the EDMA that brings in compressed data from L3 to HWA for decompression*/
        if(blockIdx != 0)
        {

            obj->decompCfg.decompEdmaToHwaStartAddress = (int32_t *)((uint8_t *)obj->decompCfg.decompEdmaToHwaStartAddress +
                                                                    obj->decompCfg.outerBlockSizeCompressed);

            retVal = DPEDMA_updateAddressAndTrigger(obj->edmaHandle,
                                (uint32_t)obj->decompCfg.decompEdmaToHwaStartAddress, /* src addr */
                                NULL,                                                 /* don't update dest addr */
                                cfg->hwRes.edmaCfg.decompEdmaCfg.edmaIn.pingPong[PING].channel, /* param Id */
                                false);                                               /* don't trigger channel */
            if (retVal != 0) { goto exit; }

            retVal = DPEDMA_updateAddressAndTrigger(obj->edmaHandle,
                                (uint32_t)obj->decompCfg.decompEdmaToHwaStartAddress
                                        + obj->decompCfg.inputBytesPerBlock * obj->decompCfg.numBlocksPerPing, /* src addr */
                                NULL,                                                 /* don't update dest addr */
                                cfg->hwRes.edmaCfg.decompEdmaCfg.edmaIn.pingPong[PONG].channel, /* param Id */
                                false);                                               /* don't trigger channel */
            if (retVal != 0) { goto exit; }

        }

        /* Decomp (Ping) : Start ping DMA Transfer to bring compressed data from L3 to HWA for decompression */
        retVal = DPEDMA_edmaStartTransferManualTrigger(obj->edmaHandle, (uint32_t)cfg->hwRes.edmaCfg.decompEdmaCfg.edmaIn.pingPong[PING].channel); if (retVal != 0) {goto exit;}

        /* Decomp (Pong) : Start pong DMA Transfer to bring compressed data from L3 to HWA for decompression */
        retVal = DPEDMA_edmaStartTransferManualTrigger(obj->edmaHandle, (uint32_t)cfg->hwRes.edmaCfg.decompEdmaCfg.edmaIn.pingPong[PONG].channel); if (retVal != 0) {goto exit;}

        /* ObjectList Create (Pong) : Given the CFAR & local Maxima results, create an object list of the last block's pong stage.
         * Note: The following three operations will happen in parallel.
           DSP : Extract object list of the previous block's last pong stage.
           HWA : Decompression of the current block of range gates.
           EDMA: Movement of decompressed data to the HWA from L3 and compressed data from HWA to L3. */
        if(blockIdx != 0){
            retVal = DPU_DopplerProcHWA_extractObjectList(obj, cfg, blockIdx - 1, rangeBinIdx - 1);
        }

        /* Wait for EDMA completion indicating that the decompressed data has been moved to L3. */
        status = SemaphoreP_pend(&obj->decompEdmaOutDoneSemaHandle, SystemP_WAIT_FOREVER);
        if (status != SystemP_SUCCESS)
        {
            retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
            goto exit;
        }

        /* STAGE II (DOPPLER FFT, DEMOD, AZIM, OBJLIST) */
        /* Disable the HWA, Configure common registers, and re-enable the HWA to perform Doppler FFT and DDMA metric computation */
        retVal = HWA_enable(obj->hwaHandle,0); if (retVal != 0) {goto exit;}
        retVal = HWA_configCommon(obj->hwaHandle, &obj->dopplerAzimHwaCommonConfig); if (retVal != 0) {goto exit;}
        retVal = HWA_enable(obj->hwaHandle,1); if (retVal != 0) {goto exit;}

        /* DopFFT and DDMA Metric (Ping) : Send the first decompressed range gate from L3 to HWA for Doppler FFT calculation
           (ping), for the first range bin in the block. */
        retVal = DPEDMA_edmaStartTransferManualTrigger(obj->edmaHandle, (uint32_t)cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaIn.pingPong[PING].channel);if (retVal != 0) {goto exit;}

        /* In this loop we process all the range gates in the block of decompressed data. Note that the loop increments by
         * 2 indicating that 2 range gates (called ping (or even) and pong (or odd)) are processed in parallel. */
        for(rangeBinIdx = 0; rangeBinIdx < cfg->staticCfg.decompCfg.rangeBinsPerBlock; rangeBinIdx+=2){

#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(0);
#endif
            /* Extract object list of the previous pong stage */
            /* ObjectList Create (Pong) : Given the CFAR & local Maxima results, create an object list of the current block's previous pong stage.
             * Note: The following operations are now to happen in parallel.
                DSP : Extract object list of the previous block's last range gate (Pong)
                HWA : Doppler FFT and DDMA metric computation of the current ping range gate.
                EDMA: Movement of decompressed data from L3 to HWA and movement of DDMA metric and doppler FFT from HWA to the DSP.  */
            if(rangeBinIdx != 0){
                retVal = DPU_DopplerProcHWA_extractObjectList(obj, cfg, blockIdx, rangeBinIdx - 1);
            }
#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(1);
#endif
            /* Send out decompressed range bin to HWA for Doppler FFT calculation (pong) */
            retVal = DPEDMA_edmaStartTransferManualTrigger(obj->edmaHandle, (uint32_t)cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaIn.pingPong[PONG].channel); if (retVal != 0) {goto exit;}

            /* Wait for Doppler FFT ping data transfer */
            /* This will signal completion of the Doppler FFT. */
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
            status = SemaphoreP_pend(&obj->dopFFTEdmaOutDoneSemaHandle[PING], SystemP_WAIT_FOREVER);
            if (status != SystemP_SUCCESS)
            {
                retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                goto exit;
            }
#else
            while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PING].channel) != 1);
            EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PING].channel);
#endif

            /* Wait for DDMA Metric ping data transfer */
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
            status = SemaphoreP_pend(&obj->DDMAMetricEdmaOutDoneSemaHandle[PING], SystemP_WAIT_FOREVER);
            if (status != SystemP_SUCCESS)
            {
                retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                goto exit;
            }
#else
            while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[PING].channel) != 1);
            EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[PING].channel);
#endif

#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(2);
#endif

            if(cfg->staticCfg.isSumTxEnabled){
                /* If we do an immediate trigger, EDMA (DDMA Metric Out) and HWA (Sum Tx Out) will
                try to access the same HWA MemBank (M0) which was seen to cause issues */
#ifdef SOC_AWR294X
                HWA_setSoftwareTrigger(obj->hwaHandle, HWA_TRIG_MODE_SOFTWARE);
#else
                HWA_setSoftwareTrigger(obj->hwaHandle);
#endif
            }
#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(3);
#endif
            /* Perform DDMA Demodulation (Ping) : Given the DDMA metric, find the most likely empty band and use it to rearrange the DDMA data.
             * Note: The following operations are now to happen in parallel.
                DSP : Use the DDMA metric to find the empty band and rearrange the Doppler FFT output so that the empty band is removed and the
                      antenna data is in the correct order.
                HWA : Doppler FFT and DDMA metric computation of the current pong range gate.
                EDMA: Movement of decompressed data from L3 to HWA and movement of DDMA metric and doppler FFT from HWA to the DSP.  */
            DPU_DopplerProcHWA_DDMADemod(obj, cfg, 0, PING);

#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(4);
#endif
            /* Start ping transfer of Doppler FFT data into HWA for Azim FFT calculation, now that the Demodulation and rearrangement is completed.  */
            retVal = DPEDMA_edmaStartTransferManualTrigger(obj->edmaHandle, (uint32_t)cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTIn.pingPong[PING].channel);if (retVal != 0) {goto exit;}

            /* Wait for Doppler FFT pong data transfer */
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
            status = SemaphoreP_pend(&obj->dopFFTEdmaOutDoneSemaHandle[PONG], SystemP_WAIT_FOREVER);
            if (status != SystemP_SUCCESS)
            {
                retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                goto exit;
            }
#else
            while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PONG].channel) != 1);
            EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDopplerFFTOut.pingPong[PONG].channel);
#endif

            /* Wait for DDMA Metric pong data transfer */
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
            status = SemaphoreP_pend(&obj->DDMAMetricEdmaOutDoneSemaHandle[PONG], SystemP_WAIT_FOREVER);
            if (status != SystemP_SUCCESS)
            {
                retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                goto exit;
            }
#else
            while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[PONG].channel) != 1);
            EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaDDMAMetricOut.pingPong[PONG].channel);
#endif

#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(5);
#endif

            if(cfg->staticCfg.isSumTxEnabled) {
                /* If we do an immediate trigger, EDMA (DDMA Metric Out) and HWA (Sum Tx Out) will
                try to access the same HWA MemBank (M0) which was seen to cause issues */
#ifdef SOC_AWR294X
                HWA_setSoftwareTrigger(obj->hwaHandle, HWA_TRIG_MODE_SOFTWARE);
#else
                HWA_setSoftwareTrigger(obj->hwaHandle);
#endif
                /* Wait for SumLogAbs (Sum Tx) ping transfer */
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                status = SemaphoreP_pend(&obj->sumTXEdmaOutDoneSemaHandle[PING], SystemP_WAIT_FOREVER);
                if (status != SystemP_SUCCESS)
                {
                    retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                    goto exit;
                }
#else
                while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[PING].channel) != 1);
                EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[PING].channel);
#endif
            }

#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(6);
#endif
            /* Perform DDMA Demodulation (Pong) :
             * Note: The following operations are now to happen in parallel.
                DSP : DDMA Demodulation of the pong range gate.
                HWA : Azimuth FFT over the ping Doppler FFT, Local Max and CFAR on the azimuth output.
                EDMA: Movement of Azimth FFT from DSP L2 to HWA and of Local Max, Azimuth FFT and CFAR from HWA to DSP.  */
            DPU_DopplerProcHWA_DDMADemod(obj, cfg, 0, PONG);
#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(7);
#endif

            /* Wait for CFAR ping data transfer out from HWA */
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
            status = SemaphoreP_pend(&obj->dopCFAREdmaOutDoneSemaHandle[PING], SystemP_WAIT_FOREVER);
            if (status != SystemP_SUCCESS)
            {
                retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                goto exit;
            }
#else
            while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaCfarOut.pingPong[PING].channel) != 1);
            EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaCfarOut.pingPong[PING].channel);
#endif

            /* Read ping CFAR peak reg count in HWA */
            retVal = HWA_readCFARPeakCountReg(obj->hwaHandle, (uint8_t *)&obj->numCfarPeaksPing, sizeof(uint32_t));if (retVal != 0) {goto exit;}
            /* This EDMA transfer is done later here since we need to read the CFAR peak count reg which cannot
            be done while another paramset is executing. Hence we cannot have any pong paramset running while we
            read the CFAR peak count register. The pong paramset can run while the DSP is creating the object list
            at the ping side, hence the EDMA transferred is triggered next here. */

            retVal = DPEDMA_edmaStartTransferManualTrigger(obj->edmaHandle, (uint32_t)cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTIn.pingPong[PONG].channel);if (retVal != 0) {goto exit;}

            /* Wait for Local Max ping data transfer out from HWA */
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
            status = SemaphoreP_pend(&obj->localMaxEdmaOutDoneSemaHandle[PING], SystemP_WAIT_FOREVER);
            if (status != SystemP_SUCCESS)
            {
                retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                goto exit;
            }
#else
            while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[PING].channel) != 1);
            EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[PING].channel);
#endif
            if(cfg->staticCfg.isSumTxEnabled){
                /* Wait for SumLogAbs (Sum Tx) pong transfer */
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
                status = SemaphoreP_pend(&obj->sumTXEdmaOutDoneSemaHandle[PONG], SystemP_WAIT_FOREVER);
                if (status != SystemP_SUCCESS)
                {
                    retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                    goto exit;
                }
#else
                while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[PONG].channel) != 1);
                EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaSumLogAbsOut.pingPong[PONG].channel);
#endif
            }

            /* Wait for Azim FFT ping data transfer out from HWA */
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
            status = SemaphoreP_pend(&obj->azimFFTEdmaOutDoneSemaHandle[PING], SystemP_WAIT_FOREVER);
            if (status != SystemP_SUCCESS)
            {
                retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                goto exit;
            }
#else
            while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PING].channel) != 1);
            EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PING].channel);
#endif

#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(8);
#endif

#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(9);
#endif
            /* Extract ping object list on the DSP - In this function the intersection of the localmax and CFAR on the doppler Azimuth operations
             * is computed and the detected object list is created.
             * Note: The following operations are now to happen in parallel.
                DSP : Extract object list (ping)
                HWA : Azimuth FFT over the pong Doppler FFT, Local Max and CFAR on the output.
                EDMA : Movement of pong Azimuth FFT data from DSP L2 to the HWA and of local max and CFAR from HWA to DSP.
             */
            retVal = DPU_DopplerProcHWA_extractObjectList(obj, cfg, blockIdx, rangeBinIdx);

#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(10);
#endif

            /* Wait for CFAR pong data transfer out from HWA */
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
            status = SemaphoreP_pend(&obj->dopCFAREdmaOutDoneSemaHandle[PONG], SystemP_WAIT_FOREVER);
            if (status != SystemP_SUCCESS)
            {
                retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                goto exit;
            }
#else
            while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaCfarOut.pingPong[PONG].channel) != 1);
            EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaCfarOut.pingPong[PONG].channel);
#endif

			/* Read pong CFAR peak reg count in HWA */
            retVal = HWA_readCFARPeakCountReg(obj->hwaHandle, (uint8_t *)&obj->numCfarPeaksPong, sizeof(uint32_t));if (retVal != 0) {goto exit;}

            /* The following steps prepare for the next range bin of the block.
             * This data is transferred here to avoid update of CFAR peak count register in dopplerFFT - logAbsSumRX paramset
             * before the peaks are read.
             */
            if(rangeBinIdx != cfg->staticCfg.decompCfg.rangeBinsPerBlock-2){
                /* Send out decompressed range bin to HWA for Doppler FFT calculation (ping) */
                retVal = DPEDMA_edmaStartTransferManualTrigger(obj->edmaHandle, (uint32_t)cfg->hwRes.edmaCfg.dopplerEdmaCfg.edmaIn.pingPong[PING].channel);if (retVal != 0) {goto exit;}
            }

            /* Wait for Local Max pong data transfer out from HWA */
#ifdef  DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
            status = SemaphoreP_pend(&obj->localMaxEdmaOutDoneSemaHandle[PONG], SystemP_WAIT_FOREVER);
            if (status != SystemP_SUCCESS)
            {
                retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                goto exit;
            }
#else
            while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[PONG].channel) != 1);
            EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaLocalMaxOut.pingPong[PONG].channel);
#endif

            /* Wait for Azim FFT pong data transfer out from HWA */
#ifdef  DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
            status = SemaphoreP_pend(&obj->azimFFTEdmaOutDoneSemaHandle[PONG], SystemP_WAIT_FOREVER);
            if (status != SystemP_SUCCESS)
            {
                retVal = DPU_DOPPLERPROCHWA_ESEMASTATUS;
                goto exit;
            }
#else
            while(EDMA_readIntrStatusRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PONG].channel) != 1);
            EDMA_clrIntrRegion(baseAddr, regionId, cfg->hwRes.edmaCfg.azimCfarEdmaCfg.edmaAzimFFTOut.pingPong[PONG].channel);
#endif

#ifdef DOPPLERPROCHWADDMA_DPU_TIMING
            insertStamp(11);
#endif
        }
    }

	/* Extract object list of the last pong stage */
    if (obj->decompCfg.numOuterBlocks)
    {
        retVal = DPU_DopplerProcHWA_extractObjectList(obj, cfg, blockIdx - 1, rangeBinIdx - 1);
    }

    outParams->numObjOut = obj->numObjOut;
    outParams->stats.numProcess++;
    outParams->stats.processingTime = CycleCounterP_getCount32() - startTime;

exit:
    if (obj != NULL)
    {
        obj->inProgress = false;
    }

    return retVal;
}


/**
  *  @b Description
  *  @n
  *  Doppler DPU deinit
  *
  *  @param[in]   handle   DPU handle.
  *
  *  \ingroup    DPU_DOPPLERPROC_EXTERNAL_FUNCTION
  *
  *  @retval
  *      Success      =0
  *  @retval
  *      Error       !=0 @ref DPU_DOPPLERPROC_ERROR_CODE
  */
int32_t DPU_DopplerProcHWA_deinit(DPU_DopplerProcHWA_Handle handle)
{
    DPU_DopplerProcHWA_Obj  *obj = NULL;
    int32_t     retVal = 0;
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
    uint8_t i=0;
#endif
    /* Sanity Check */
    obj = (DPU_DopplerProcHWA_Obj*)handle;
    if(obj == NULL)
    {
        retVal = DPU_DOPPLERPROCHWA_EINVAL;
        goto exit;
    }

    /* Delete Semaphores */
    SemaphoreP_destruct(&obj->decompEdmaOutDoneSemaHandle);
#ifdef DOPPLERPROCHWADDMA_EDMA_INTERRUPTS
    for(i=0; i<2; i++)
    {
        SemaphoreP_destruct(&obj->dopFFTEdmaOutDoneSemaHandle[i]);
        SemaphoreP_destruct(&obj->DDMAMetricEdmaOutDoneSemaHandle[i]);
        SemaphoreP_destruct(&obj->sumTXEdmaOutDoneSemaHandle[i]);
        SemaphoreP_destruct(&obj->azimFFTEdmaOutDoneSemaHandle[i]);
        SemaphoreP_destruct(&obj->dopCFAREdmaOutDoneSemaHandle[i]);
        SemaphoreP_destruct(&obj->localMaxEdmaOutDoneSemaHandle[i]);
    }
#endif
exit:
    return retVal;
}

