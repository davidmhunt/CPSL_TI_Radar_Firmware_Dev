/*
 *  
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
 *   @file  rangeprochwaDDMA.h
 *
 *   @brief
 *      Implements range processing functionality using HWA.
 */
/**
 * @page dpu_rangehwaDDMA RangeProcHWADDMA
 * [TOC]
 *  @section toplevel_hwa Top Level Design
 *
 *  Range FFT processing is done by HWA hardware. Based on the RF parameters, rangeProcHWADDMA configures
 *  hardware accelerator FFT engine accordingly. It also configures data input and output EDMA channels to
 *  bring data in and out of range Processing memory.
 *  First, samples are brought into the HWA memory from the ADC Buffer using EDMA. Then, DC Estimation is performed on
 *  the samples. This is followed by DC Subtraction and interference statistics estimation. Then, interference mitigation 
 *  is performed on the samples. Then the FFT is performed on these samples followed by compression and storage into the
 *  radar cube.
 *
 *  HWA FFT process is triggered by hardware based trigger -"chirp data available" which is hooked up to HWA internally in hardware.
 *
 *  After compression is done, HWA generates interrupt to rangeProcHWA DPU, at the same time triggers EDMA data
 *  output channel to copy FFT results to radarCube in configured format. EDMA interrupt done interrupt is triggered by EDMA hardware
 *  after the copy is completed.
 *
 *  @section config_hwa Data Interface Parameter Range
 *
 *  Here are supported ADCBuf and radarCube interface configurations:
 *
 *  ADCBuf Data Interface
 *----------------------
 *
 *   Parameter | Supported value
 *  :----------|:----------------:
 *   dataFmt | @ref DPIF_DATAFORMAT_REAL16 only for AWR294X
 *   interleave|interleave (@ref DPIF_RXCHAN_INTERLEAVE_MODE) ADC Data ([AdcSample][RxAntenna])
 *   numChirpsPerChirpEvent|1 ONLY
 *   numRxAntennas|4
 *   compressionMethod|EGE and BFP
 *   rxAntennaPerBlock|4 (only in EGE)
 *   rangeBinsPerBlock|Power of 2 (upto 16)
 *   compression dimension|Along RX and Range Bins in EGE, Along Range Bins in BFP
 *
 * How Real-only ADC Samples are handled {#real_only}
 *----------------------
 *  AWR294X only supports real ADC data. Due to this, the memory requirement for ADC data is reduced by half (since
 *  in this case, the DPU uses 16 bit real-only data instead of "16 bit Im, 16 bit Re" complex data format.). Hence, 2048 ADC samples
 *  can now be supported by the DPU. The 1-D FFT calculated on these samples results in a complex valued radar cube. However, since
 *  FFT is taken on real samples, the latter half of the FFT turns out to be the complex conjugate of the former half. Hence, the latter
 *  half of the FFT computed is rejected and not pushed into the HWA memory bank by the HWA output formatter directly. Thus, the radar
 *  cube memory requirement also reduces by half. So, in case of real only ADC samples, the FFT Size and the number of Range Bins is 
 *  taken as half the number of ADC samples. Post the calculation of radar cube, the rest of the chain remains similar to the case of
 *  complex ADC samples.
 * 
 *  @section input_hwa Data Input
 *
 *  There are three HWA input modes supported (@ref DPU_RangeProcHWA_InputMode_e),
 *  -  @ref DPU_RangeProcHWA_InputMode_ISOLATED : \n
 *     ADCBuf buffer and HWA memory are isolated in physical memory space.
 *     Data input EDMA channel is configured to transfer data from ADCBuf to HWA M0/M1 memory
 *     in ping/pong alternate order.
 *  -  @ref DPU_RangeProcHWA_InputMode_MAPPED :\n
 *     ADCBuf buffer and HWA memory are mapped. HWA can read ADC data directly. No copy is needed. This mode is obsolete for HWA 2.0.
 *  -  @ref DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM :\n
 *     ADC data are streamed directly to HWA internal memory in a ping/pong manner. No copy is needed.
 *
 *  @section output_hwa Data Output
 *
 *  RangeProcHWA configures data output EDMA channels to transfer compressed FFT results from HWA M2/M3 to radarCube memory.
 *  Follow on to understand the output data format.
 *
 *  @section process_hwa Data Processing Implementation
 *  The following image shows the data processing chain implementation.
 * 
 *  @image html rangeDPU.png "rangeProcHWADDMA Data processing flow"
 *
 *  DC Est/Sub, Intf Est/Sub, Range FFT processing and Compression is done by HWA hardware. 
 *  As shown in the following diagram, after @ref DPU_RangeProcHWA_config is completed,
 *  in every frame, rangeProcHWA is triggered through @ref DPU_RangeProcHWA_control \n
 *  using command @ref DPU_RangeProcHWA_Cmd_triggerProc. If the hardware resources are overlaid with other modules,
 *  @ref DPU_RangeProcHWA_config can be called before the next frame start. \n
 *
 * @image html hwa_callflow.png "rangeProcHWA call flow"
 *
 *  @subsection rpDDMACompression Compression
 *  @subsubsection EGE EGE Compression
 *  The EGE (Exponential Goulomb Encoder) data compression algorithm is used to compress data across range bins and rx antennas. Parameters issued
 *  by the user are the number of range bins per block and compression ratio. A block consisting of
 *  all samples corresponding to all Rx Antennas and the number of range bins per block are compressed
 *  together. Thus, the size of the block, which was of size (range bins per block) * (rx antennas) * (bytes per sample)
 *  before compression, would become a (range bins per block) * (rx antennas) * (bytes per sample) * (achieved compression ratio)
 *  sized block post compression. Hence, all samples in a single block will be taken into consideration for a single compression operation. 
 *  For example, if a block with samples worth 64 bytes is compressed with a compression ratio of 0.5, the resultant 
 *  block would have a size of 32 bytes. Note that since blocks must be word aligned, the compression ratio
 *  achieved might be higher than the compression ratio desired.
 * 
 *  The following image shows the EGE compression operation for a single chirp-
 * 
 *  @image html compressionInterleaveEGE.png "EGE Compression of single chirp FFT data (interleaved)"
 * 
 *  @subsubsection BFP BFP Compression
 *  The BFP (Block Floating Point) data compression algorithm is used to compress data across range bins. Parameters issued
 *  by the user are the number of range bins per block and compression ratio. A block consisting of
 *  all samples corresponding to the number of range bins per block are compressed
 *  together. Thus, the size of the block, which was of size (range bins per block) * (bytes per sample)
 *  before compression, would become a (range bins per block) * (bytes per sample) * (achieved compression ratio)
 *  sized block post compression. Hence, all samples in a single block will be taken into consideration for a single compression operation.
 *  For example, if a block with samples worth 64 bytes is compressed with a compression ratio of 0.5, the resultant
 *  block would have a size of 32 bytes. Note that since blocks must be word aligned, the compression ratio
 *  achieved might be higher than the compression ratio desired.
 *
 *  The following image shows the BFP compression operation for a single chirp-
 *
 *  @image html compressionInterleaveBFP.png "BFP Compression of single chirp FFT data (interleaved)"
 *
 *  Ultimately, the DPU would result in a compressed radar cube of the following format.
 * 
 *  @image html radarCubeCompressed.png "Output Radar Cube"
 * 
 *  Here, in case of EGE compression, each Block consists of compressed range bins per block * RX antenna samples. And in case of BFP compression,
 *  Block #N represents RX antenna number of compressed blocks, each consisting range bins per block of a receive antenna.
 *
 *  @subsection dcEst DC Estimation/Subtraction
 *  The DC estimation block estimates the time-domain average of the stream of samples along the ADC Sample dimension.
 *  DC estimation is based on accumulation followed by a fine scaling and a right shift, which can be programmed by the application.
 *  It must be noted that while the support for disabling DC Estimation/Subtraction is not available as of yet, the DC subtraction step
 *  can easily be bypassed by disabling the same in the "PING DC SUBTRACTION, INTERFERENCE STATISTICS ESTIMATION PARAMSET" section
 *  of @ref rangeProcHWA_ConfigHWA function.
 * 
 *  @subsection intf Interference Localization and Mitigation
 *  In an FMCW radar transceiver, interference from another radar typically manifests itself as a time-domain spike in a few samples. 
 *  This spike corresponds to the time duration when the chirping frequency of both radars overlap with each other. 
 *  Such a time-domain spike caused by interference can lead to degradation in the noise floor at the FFT output, 
 *  causing degradation in detection performance. In order to mitigate the impact of interference, the DPU, using the HWA,
 *  block provides capability to perform interference localization to identify samples corrupted by interference, 
 *  followed by interference mitigation to repair those samples. It must be noted that while the support for disabling Interference
 *  Localization/Mitigation is not available as of yet, the mitigation step can easily be bypassed by disabling the same in 
 *  the "PING INTERFERENCE MITIGATION, FFT PARAMSET" section of @ref rangeProcHWA_ConfigHWA function.
 *
 */

/**************************************************************************
 *************************** Include Files ********************************
 **************************************************************************/
#ifndef RANGEPROCHWA_H
#define RANGEPROCHWA_H

/* Standard Include Files. */
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>

/* MCU Plus SDK Drivers include files */
#include <drivers/hwa.h>

/* mmWave SDK Data Path Include Files */
#include <ti/datapath/dpif/dpif_adcdata.h>
#include <ti/datapath/dpif/dpif_radarcube.h>
#include <ti/datapath/dpif/dp_error.h>
#include <ti/datapath/dpedma/dpedmahwa.h>
#include <ti/datapath/dpedma/dpedma.h>
#include <ti/datapath/dpu/rangeproc/rangeproc_common.h>

#ifdef __cplusplus
extern "C" {
#endif
 
/*! Number of HWA parameter sets */
#define DPU_RANGEPROCHWA_NUM_HWA_PARAM_SETS                 4U
/* Addition of paramsets for both the devices in rangeprocessing */
#ifdef CASCADE_EVM
#define DPU_RANGEPROCHWA_NUM_HWA_PARAM_SETS_DDMA        16U
#else
#define DPU_RANGEPROCHWA_NUM_HWA_PARAM_SETS_DDMA        10U
#endif
#define DPU_RANGEPROCHWA_MAX_NUM_HWA_PARAM_SETS_BFP_DDMA    16U

/*! Alignment for DC range signal mean buffer - if DPU is running on DSP(C66) */
#define DPU_RANGEPROCHWA_DCRANGESIGMEAN_BYTE_ALIGNMENT_DSP 8U

/*! Alignment for DC range signal mean buffer - if DPU is running on R5F */
#define DPU_RANGEPROCHWA_DCRANGESIGMEAN_BYTE_ALIGNMENT_R5F 4U

/*! Alignment for radar cube on R5F */
#define DPU_RANGEPROCHWA_RADARCUBE_BYTE_ALIGNMENT_R5F    CSL_CACHE_L1D_LINESIZE

/*! Alignment for radar cube on DSP */
#define DPU_RANGEPROCHWA_RADARCUBE_BYTE_ALIGNMENT_DSP     CSL_CACHE_L1D_LINESIZE

#define DPU_RANGEPROCHWADDMA_NUM_INTFMITIG_WIN_HWACOMMONCFG_SIZE (5U)

#define RANGEPROCHWADDMA_NUM_EDMA_INTERRUPTS 1U

/** @addtogroup DPU_RANGEPROC_ERROR_CODE
 *  Base error code for the rangeProc DPU is defined in the
 *  \include ti/datapath/dpif/dp_error.h
 @{ */

/**
 * @brief   Error Code: Invalid argument
 */
#define DPU_RANGEPROCHWA_EINVAL                  (DP_ERRNO_RANGE_PROC_BASE-1)

/**
 * @brief   Error Code: Out of memory
 */
#define DPU_RANGEPROCHWA_ENOMEM                  (DP_ERRNO_RANGE_PROC_BASE-2)

/**
 * @brief   Error Code: Internal error
 */
#define DPU_RANGEPROCHWA_EINTERNAL               (DP_ERRNO_RANGE_PROC_BASE-3)

/**
 * @brief   Error Code: Not implemented
 */
#define DPU_RANGEPROCHWA_ENOTIMPL                (DP_ERRNO_RANGE_PROC_BASE-4)

/**
 * @brief   Error Code: In Progress
 */
#define DPU_RANGEPROCHWA_EINPROGRESS             (DP_ERRNO_RANGE_PROC_BASE-5)

/**
 * @brief   Error Code: Invalid control command
 */
#define DPU_RANGEPROCHWA_ECMD                    (DP_ERRNO_RANGE_PROC_BASE-6)

/**
 * @brief   Error Code: Semaphore error
 */
#define DPU_RANGEPROCHWA_ESEMA                   (DP_ERRNO_RANGE_PROC_BASE-7)

/**
 * @brief   Error Code: DC range signal removal configuration error
 */
#define DPU_RANGEPROCHWA_EDCREMOVAL              (DP_ERRNO_RANGE_PROC_BASE-8)

/**
 * @brief   Error Code: ADCBuf data interface configuration error
 */
#define DPU_RANGEPROCHWA_EADCBUF_INTF            (DP_ERRNO_RANGE_PROC_BASE-9)

/**
 * @brief   Error Code: ADCBuf data interface configuration error
 */
#define DPU_RANGEPROCHWA_ERADARCUBE_INTF         (DP_ERRNO_RANGE_PROC_BASE-10)

/**
 * @brief   Error Code: HWA windowing configuration error
 */
#define DPU_RANGEPROCHWA_EWINDOW                 (DP_ERRNO_RANGE_PROC_BASE-11)

/**
 * @brief   Error Code: Incorrect number of butterfly stages specified for scaling 
 */
#define DPU_RANGEPROCHWA_EBUTTERFLYSCALE         (DP_ERRNO_RANGE_PROC_BASE-12)



/**
@}
*/

/**
 * @brief
 *  RangeProc data input mode
 *
 * @details
 *  This enum defines if the rangeProc input data is from RF front end or it is in M0 but 
 *  standalone from RF.
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef uint32_t DPU_RangeProcHWA_InputMode;
/*! @brief     Range input is integrated with DFE input 
                     ADC buffer is mapped to HWA memory 
                     DMA data from ADC buffer to HWA is NOT required
                     This is not supported in AM273X */
#define DPU_RangeProcHWA_InputMode_MAPPED              (uint32_t) 0U

/*! @brief     Range input is integrated with DFE input 
                     ADC buffer is not mapped to HWA memory,
                     DMA data from ADCBuf to HWA memory is 
                     needed in range processing */
#define DPU_RangeProcHWA_InputMode_ISOLATED            (uint32_t) 1U  

/*! @brief      Range input is stored into HWA internal memory 
                     in ping/pong manner*/
#define DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM    (uint32_t) 2U

/**
 * @brief
 *  rangeProc control command
 *
 * @details
 *  The enum defines the rangeProc supported run time command
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef uint32_t DPU_RangeProcHWA_Cmd;
#define DPU_RangeProcHWA_Cmd_dcRangeCfg         (uint32_t) 0
#define DPU_RangeProcHWA_Cmd_triggerProc        (uint32_t) 1

/**
 * @brief
 *  rangeProc FFT tuning parameters for HWA based Range FFT
 *
 * @details
 *  This structure allows users to tune the scaling factors for HWA based Range FFTs
 *  (currently unused)
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_FFTtuning_t{
    /*! @brief  Specify amount of right (divide) shift to apply
           to convert HWA internal 24-bit Range FFT output to 16-bit RadarCube.
           User should adjust this based on the setup where sensor is deployed and
           sensors setting for Tx O/P power/RX gain and their application needs */
    uint16_t    fftOutputDivShift;

    /*! @brief  Specify number of Last butterfly stages to scale to avoid clipping within 
           HWA FFT stages. Given the ADC data bit width of 16-bits and internal 24-bit width
           of HWA, user has around 8-bits to grow Range FFT output and should not need to use butterfly scaling
           for FFT sizes upto 256. Beyond that fft size, user should adjust this based on the setup 
           where sensor is deployed and sensors setting for Tx O/P power/RX gain*/
    uint16_t    numLastButterflyStagesToScale;

}DPU_RangeProcHWA_FFTtuning;

/**
 * @brief
 *  RangeProc HWA configuration
 *
 * @details
 *  The structure is used to hold the HWA configuration needed for Range FFT
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_HwaConfig_t
{
    /*! @brief     HWA paramset Start index */
    uint8_t         paramSetStartIdx;

    /*! @brief     Number of HWA param sets must be @ref DPU_RANGEPROCHWA_NUM_HWA_PARAM_SETS */
    uint8_t         numParamSet;

    /*! @brief     Flag to indicate if HWA windowing is symmetric
                    see HWA_WINDOW_SYMM definitions in HWA driver's doxygen documentation
     */
    uint8_t         hwaWinSym;    

    /*! @brief     HWA windowing RAM offset in number of samples */
    uint16_t        hwaWinRamOffset;

    /*! @brief     Data Input Mode, */
    DPU_RangeProcHWA_InputMode      dataInputMode;

    /*! @brief Pointer to HWA Interference Mitigation window */
    uint8_t         hwaInterfMitigWindow[DPU_RANGEPROCHWADDMA_NUM_INTFMITIG_WIN_HWACOMMONCFG_SIZE];

    /*! @brief     HWA hardware trigger source. This is used only in @ref DPU_RangeProcHWA_InputMode_HWA_INTERNAL_MEM mode */
    uint8_t         hardwareTrigSrc;

}DPU_RangeProcHWA_HwaConfig;

/**
 * @brief
 *  RangeProc EDMA configuration
 *
 * @details
 *  The structure is used to hold the EDMA configuration needed for Range FFT
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_EDMAInputConfig_t
{
    /*! @brief     EDMA configuration for rangeProc data Input
                    This is needed only in @ref DPU_RangeProcHWA_InputMode_ISOLATED
     */
    DPEDMA_ChanCfg        dataIn;

    /*! @brief     EDMA configuration for rangeProc data Input Signature */
    DPEDMA_ChanCfg        dataInSignature;
}DPU_RangeProcHWA_EDMAInputConfig;

/**
 * @brief
 *  RangeProc EDMA configuration
 *
 * @details
 *  The structure is used to hold the EDMA configuration needed for Range FFT
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_EDMAOutputConfigFmt1_t
{
    /*! @brief     EDMA configuration for rangeProc data Out- ping 
                    It must be a HWACC triggered EDMA channel.
     */
    DPEDMA_ChanCfg        dataOutPing;

    /*! @brief     EDMA configuration for rangeProc data Out- pong 
                    It must be a HWACC triggered EDMA channel
     */
    DPEDMA_ChanCfg        dataOutPong;
}DPU_RangeProcHWA_EDMAOutputConfigFmt1;

/**
 * @brief
 *  RangeProc EDMA configuration
 *
 * @details
 *  The structure is used to hold the EDMA configuration needed for Range FFT
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_EDMAOutputConfigFmt2_t
{
    /*! @brief     EDMA configuration for rangeProc data Out- ping 
                    It must be a HWACC triggered EDMA channel
     */
    DPEDMA_3LinkChanCfg   dataOutPing;
    DPEDMA_ChanCfg        dataOutPingData[3];

    /*! @brief     EDMA configuration for rangeProc data Out- pong 
                    It must be a HWACC triggered EDMA channel
     */
    DPEDMA_3LinkChanCfg   dataOutPong;
    DPEDMA_ChanCfg        dataOutPongData[3];
}DPU_RangeProcHWA_EDMAOutputConfigFmt2;

/**
 * @brief
 *  RangeProc output EDMA configuration
 *
 * @details
 *  The structure is used to hold the EDMA configuration needed for Range FFT
 *
 *  Fmt1: Generic EDMA ping/pong output mode
 *       - 1 ping/pong EDMA channel, 
 *       - 1 ping/pong HWA signature channel
 *
 *  Fmt2: Specific EDMA ping/pong output mode used ONLY for 3 TX anntenna for radar cube
 *        layout format: @ref DPIF_RADARCUBE_FORMAT_1, ADCbuf interleave mode 
 *        @ref DPIF_RXCHAN_NON_INTERLEAVE_MODE
 *       - 1 ping/pong dummy EDMA channel with 3 shadow channels
         - 3 ping/pong dataOut channel
 *       - 1 ping/pong HWA signature channel 
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_EDMAOutputConfig_t
{
    /*! @brief     EDMA data output Signature */
    DPEDMA_ChanCfg        dataOutSignature;

    union
    {
        /*! @brief     EDMA data output fmt1 @ref DPU_RangeProcHWA_EDMAOutputConfigFmt1 */
        DPU_RangeProcHWA_EDMAOutputConfigFmt1     fmt1;

        /*! @brief     EDMA data output fmt2 @ref DPU_RangeProcHWA_EDMAOutputConfigFmt2 */
        DPU_RangeProcHWA_EDMAOutputConfigFmt2     fmt2;
    }u;
}DPU_RangeProcHWA_EDMAOutputConfig;

/**
 * @brief
 *  RangeProcHWA hardware resources
 *
 * @details
 *  The structure is used to hold the hardware resources needed for Range FFT
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_HW_Resources_t
{
    /*! @brief     EDMA Handle */
    EDMA_Handle         edmaHandle;

    /*! @brief     HWA configuration */
    DPU_RangeProcHWA_HwaConfig      hwaCfg;

    /*! @brief     EDMA configuration for rangeProc data Input */
    DPU_RangeProcHWA_EDMAInputConfig edmaInCfg;

    /*! @brief     EDMA configuration for rangeProc data Output */ 
    DPU_RangeProcHWA_EDMAOutputConfig edmaOutCfg;

    /*! @brief     Pointer to Calibrate DC Range signature buffer 
                    The size of the buffer = DPU_RANGEPROC_SIGNATURE_COMP_MAX_BIN_SIZE *
                                        numTxAntenna * numRxAntenna * sizeof(cmplx32ImRe_t)
        For R5F:\n
        Byte alignment Requirement = @ref DPU_RANGEPROCHWA_DCRANGESIGMEAN_BYTE_ALIGNMENT_R5F \n
        For DSP (C66X):\n
        Byte alignment Requirement = @ref DPU_RANGEPROCHWA_DCRANGESIGMEAN_BYTE_ALIGNMENT_DSP \n
     */
    cmplx32ImRe_t       *dcRangeSigMean;

    /*! @brief     DC range calibration scratch buffer size */
    uint32_t            dcRangeSigMeanSize;

    /*! @brief      Radar cube data interface. Radar cube buffer (radarCube.data)
        For R5F:\n
        Byte alignment Requirement = @ref DPU_RANGEPROCHWA_RADARCUBE_BYTE_ALIGNMENT_R5F \n
        For DSP (C66X):\n
        Byte alignment Requirement = @ref DPU_RANGEPROCHWA_RADARCUBE_BYTE_ALIGNMENT_DSP \n
     */
    DPIF_RadarCube      radarCube;

    /* EDMA Interrupt Object */
    Edma_IntrObject     *edmaTransferCompleteIntrObj;

}DPU_RangeProcHWA_HW_Resources;

/**
 * @brief
 *  RangeProcHWA Compression hardware resources
 *
 * @details
 *  The structure is used to hold the hardware resources needed for compression of Range FFT
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_CompressionCfg
{
    /*! @brief Flag that indicates if compression is enabled */
    bool  isEnabled;

    /*! @brief Compression Method, 0 indicates EGE and 1 indicates BFP */
    uint8_t  compressionMethod;

    /*! @brief Compression ration, a value between 0 and 1 */
    float  compressionRatio;

    /*! @brief Indicates the number of range bins to be compressed in a single compression block */
    uint16_t  rangeBinsPerBlock;

    /*! @brief Can be greater than 1 only for DPIF_RADARCUBE_FORMAT_2. 
               For DPIF_RADARCUBE_FORMAT_1 this should be set to 1 */
    uint16_t  numRxAntennaPerBlock;

    /*! @brief extra compression parameters for BFP, dependent on enabled #RX antenna*/
    uint8_t bfpCompExtraParamSets;

}DPU_RangeProcHWA_CompressionCfg;

/**
 * @brief
 *  RangeProcHWA Interference mitigation configuration
 *
 * @details
 *  The structure is used to hold the hardware resources needed for interference mitigation in Range DPU
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWADDMA_intfStatsdBCfg_t
{

    /*! @brief Interference mitigation magnitude SNR in dB */
    uint32_t intfMitgMagSNRdB;

    /*! @brief Interference mitigation magdiff SNR in dB */
    uint32_t intfMitgMagDiffSNRdB;

}DPU_RangeProcHWADDMA_intfStatsdBCfg;

/**
 * @brief
 *  RangeProcHWA static configuration
 *
 * @details
 *  The structure is used to hold the static configuraiton used by rangeProcHWA
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_StaticConfig_t
{
    /*! @brief  Number of transmit antennas */
    uint8_t     numTxAntennas;

    /*! @brief  Number of virtual antennas */
    uint8_t     numVirtualAntennas;

    /*! @brief  Number of range bins */
    uint16_t    numRangeBins;

    /*! @brief  Number of bins used in Range FFT Calculation */
    uint16_t    numFFTBins;

    /*! @brief  1 if ADC Samples are real */
    uint16_t    isChirpDataReal;

    /*! @brief  Number of chirps per frame */
    uint16_t    numChirpsPerFrame;

    /*! @brief  Range FFT window coefficients, Appliation provided windows coefficients
                After @ref DPU_RangeProcHWA_config(), windowing buffer is not used by rangeProcHWA DPU,
                Hence memory can be released
     */
    int32_t    *window;

    /*! @brief     Range FFT window coefficients size in bytes 
                    non-symmetric window, size = sizeof(uint32_t) * numADCSamples
                    symmetric window, size = sizeof(uint32_t)*(numADCSamples round up to even number )/2
     */
    uint32_t    windowSize;

    /*! @brief      ADCBuf buffer interface */
    DPIF_ADCBufData     ADCBufData;

    /*! @brief      Flag to reset dcRangeSigMean buffer
                     1 - to reset the dcRangeSigMean buffer and counter
                     0 - do not reset
     */
    uint8_t     resetDcRangeSigMeanBuffer;

    /*! @brief     Range FFT Tuning Params */
    DPU_RangeProcHWA_FFTtuning    rangeFFTtuning;

    /*! @brief Compression Configuration */
    DPU_RangeProcHWA_CompressionCfg  compressionCfg;

    /*! @brief Interference mitigation Configuration */
    DPU_RangeProcHWADDMA_intfStatsdBCfg intfStatsCfgdB;

#ifdef SOC_AM273X
    /*! @brief     LVDS Streaming of data is enabled */
    uint32_t                    isLVDSStreamEnabled;

    /*! @brief     Offset for CSIRX Pong buffer */
    int32_t                     csirxOffset;
#endif

}DPU_RangeProcHWA_StaticConfig;

/**
 * @brief
 *  RangeProcHWA dynamic configuration
 *
 * @details
 *  The structure is used to hold the dynamic configuraiton used by rangeProcHWA
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_DynamicConfig_t
{
    /*! @brief      Pointer to Calibrate DC Range signature configuration */
    DPU_RangeProc_CalibDcRangeSigCfg *calibDcRangeSigCfg;
}DPU_RangeProcHWA_DynamicConfig;

/**
 * @brief
 *  Range FFT configuration
 *
 * @details
 *  The structure is used to hold the configuration needed for Range FFT
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_Config_t
{
    /*! @brief     rangeProc hardware resources */
    DPU_RangeProcHWA_HW_Resources   hwRes;

    /*! @brief     rangeProc static configuration */
    DPU_RangeProcHWA_StaticConfig   staticCfg;

    /*! @brief     rangeProc dynamic configuration */
    DPU_RangeProcHWA_DynamicConfig  dynCfg;
}DPU_RangeProcHWA_Config;

/**
 * @brief
 *  rangeProcHWA output parameters populated during rangeProc Processing time
 *
 * @details
 *  The structure is used to hold the output parameters for rangeProcHWA
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_InitParams_t
{
    /*! @brief     HWA Handle */
    HWA_Handle          hwaHandle;
}DPU_RangeProcHWA_InitParams;

/**
 * @brief
 *  rangeProcHWA output parameters populated during rangeProc Processing time
 *
 * @details
 *  The structure is used to hold the output parameters for rangeProcHWA
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_RangeProcHWA_OutParams_t
{
    /*! @brief      End of Chirp indication for rangeProcHWA */
    bool                endOfChirp;

    /*! @brief     rangeProcHWA stats */
    DPU_RangeProc_stats  stats;
}DPU_RangeProcHWA_OutParams;

/**
 * @brief
 *  rangeProc DPU Handle
 *
 *  \ingroup DPU_RANGEPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef void* DPU_RangeProcHWA_Handle ;

/*================================================================
               rangeProcHWA DPU exposed APIs            
 ================================================================*/
DPU_RangeProcHWA_Handle DPU_RangeProcHWA_init
(
    DPU_RangeProcHWA_InitParams     *initParams,
    volatile uint8_t            subframeCounter,
    int32_t*                        errCode
);

int32_t DPU_RangeProcHWA_config
(
    DPU_RangeProcHWA_Handle     handle,
    DPU_RangeProcHWA_Config*    rangeHwaCfg
);

int32_t DPU_RangeProcHWA_process
(
    DPU_RangeProcHWA_Handle     handle,
    DPU_RangeProcHWA_OutParams* outParams
);

int32_t DPU_RangeProcHWA_control
(
    DPU_RangeProcHWA_Handle handle,
    DPU_RangeProcHWA_Cmd    cmd,
    void*                   arg,
    uint32_t                argSize
);

int32_t DPU_RangeProcHWA_deinit
(
    DPU_RangeProcHWA_Handle     handle
);

#ifdef __cplusplus
}
#endif

#endif
