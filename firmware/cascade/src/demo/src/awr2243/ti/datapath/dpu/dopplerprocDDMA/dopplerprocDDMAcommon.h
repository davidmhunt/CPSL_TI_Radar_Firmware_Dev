/**
 *   @file  dopplerprocDDMAcommon.h
 *
 *   @brief
 *      Implements Common definition across dopplerProc DPU.
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
#ifndef DOPPLERPROC_COMMON_H
#define DOPPLERPROC_COMMON_H

/* Standard Include Files. */
#include <stdint.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <ti/datapath/dpedma/dpedma.h>

/** @mainpage Doppler DDMA DPU
 *
 * This DPU implements the Doppler DDMA DPU, which performs 2D FFT, DDMA Demodulation,
 * Azimuth FFT, Doppler CFAR and object list extraction.
 *
 *  @section doppler_intro_section Introduction
 *
 *  Description
 *  ----------------
 *
 *  This DPU expects as input the compressed radar cube with 1D FFT data.
 *  These are the only formats supported by this DPU. 
 *
 *  The Doppler DPU is available in HWA implementations:
 *
 *   DPU Implementation   |  Runs on cores  | Uses HWA   
 *  :---------------------|:----------------|:---------
 *  dopplerProcHWA        |  DSP            |  Yes
 *\n\n
 *  @section dpu1 DopplerProcHWA
 *
 * A list of resources required by this DPU is described in @ref DPU_DopplerProcHWA_HW_Resources_t.
 * In particular, the number of EDMA channels required is constant and does not depend on the DPU configuration. 
 * On the other hand, the number of required HWA paramsets is a function of the number of TX antennas configured in
 * the data path as described in @ref DPU_DopplerProcHWA_HwaCfg_t.
 *
 * 
 * Besides the resources described above, other parameters required for the DPU configuration are listed
 * in @ref DPU_DopplerProcHWA_StaticConfig_t.
 * 
 * The Doppler DDMA DPU performs the bulk of the remaining processing.
 *  This DPU has three stages- the Decompression stage, Doppler stage and Azim stage. 
 * It requires both HWA and DSP usage, and has 3 major stages:
 * -# Decompression stage: HWA performs decompression of a single outer block in a ping-pong
 *    fashion. No DSP intervention is required in for decompression of one outer block, after which
 *    it is processed by the next two DPU stages. After one outer block is processed by all the three DPU stages, the DSP triggers the decompression of the next outer block again.
 * -# Doppler Stage: HWA performs Doppler FFT, log-mag summation, DDMA Metric calculation and range-doppler heatmap computation, while the DSP performs DDMA demodulation.
 * -# Azim Stage: HWA performs Azimuth FFT, Doppler CFAR-OS and Local Max calculation, while the DSP performs the list extraction.
 * 
 * The figure below shows a high level overview of the DPU.
 * 
 * @image html ddmaDpu.png "Doppler DDMA DPU at a high-level" width=50% 
 * 
 * The demodulation scheme that is used is empty subband based DDMA. The number of Doppler-bins (defined as the #dopplerBins/#totalSubbands) within a sub-band ideally should 
 *  be an integer. This puts constraints on the Doppler FFT size and the number of sub-bands that need to be employed.
 * 
 *  In AWR2944 we have 4 TX channels and given the constraints of FFT size on the HWA (2^N or 3*2^N)
 *  we need to employ 2 empty sub-bands in AWR2944 thus making the total number of sub-bands equal
 *  to 6 (4-TX channels and 2 empty bands). For the case of 6 sub-bands, a Doppler FFT size of 3*2^N
 *  is used to satisfy this requirement. In the case of AWR2943, however, we have only 3 Tx antennas.
 *  Hence, we can do with a single empty subband and a Doppler FFT size of 2^N.
 * 
 * **Decompression**\n
 * Compressed data for a single outer block is copied from L3 memory to the HWA memory.
 * Decompression of the data is performed using the decompression engine of the HWA.
 * This decompression is performed in a ping-pong manner, where the number of chirps to decompress per ping/pong is 
 * calculated (based on the available HWA memory). For example, if the number of chirps is 96, the size of 
 * outer block will be 96*(sizeOfCompressedBlock) in case of EGE and 96*(sizeOfCompressedBlock)*RxAnt in case of BFP.
 * We will calculate numChirpsToDecompressPerPing such that
 * numChirpsToDecompressPerPing *(sizeOfCompressedBlock) < availableHWAMemBankMemory and will run the ping-pong loops 
 * 96/numChirpsToDecompressPerPing times. The decompressed outer block is placed in a scratch L3 RAM and is used as the 
 * input to the Doppler-FFT and subsequent processing steps. \n\n
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  Paramset           |  Decompression Paramset
 *  Trigger            |  Decompression EDMA In Signature channel transfer completion
 *  Input              |  numChirpsToDecompressPerPing blocks of compressed radar cube corresponding to the same range bins and rx antennas
 *  Input MemBank      |  M0/M2   
 *  Output             |  Decompressed partial radar cube of numRangePerBlock * numRxAntennas * numChirpsToDecompressPerPing size
 *  Output MemBank     |  M4/M6   
 *  On Completion      |  Triggers EDMA Out transfer of partially decompressed radar cube to L3.
 * 
 * 
 * Associated EDMA transfers:
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  EDMA Param         |  Transfer of compressed radar cube (partial) from L3 to HWA Mem
 *  EDMA Source Mem    |  Compressed Radar Cube (L3)
 *  EDMA Source Fmt    |  [numChirpsToDecompressPerPing][block]
 *  EDMA Dest Mem      |  M0/M2
 *  EDMA Dest Fmt      |  [numChirpsToDecompressPerPing][block]
 * 
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  EDMA Param         |  Transfer of decompressed radar cube (partial) from HWA Mem to L2
 *  EDMA Source Mem    |  M4/M6
 *  EDMA Source Fmt    |  [numChirpsToDecompressPerPing][rangeBinsPerBlock][rxAntennas]
 *  EDMA Dest Mem      |  Decompressed Radar Cube Scratch Buf (L2)
 *  EDMA Dest Fmt      |  [numChirpsToDecompressPerPing][rangeBinsPerBlock][rxAntennas]
 * 
 * EDMA transfer happens one range bin at a time for ping, and similarly for pong. Two range bins (one ping, one pong)
 * after transfer are processed in one loop run of this stage (2D FFT + DDMA Demod + Azim FFT + Doppler CFAR).

 * **Windowing and Doppler FFT**\n
 * The number of Doppler-bins within a sub-band should be an integer. This puts constraints on the Doppler FFT 
 * size and the number of sub-bands that need to be employed. 
 * In AWR2944 we have 4 TX channels and given the constraints of FFT size on the HWA (2^N or 3*2^N) 
 * we need to employ 2 empty sub-bands in AWR2944 thus making the total number of sub-bands equal 
 * to 6 (4-TX channels and 2 empty bands). For the case of 6 sub-bands, a Doppler FFT size of 3*2^N
 * is used to satisfy this requirement. Before FFT operation, input samples are multiplied by a window function. Window size and coefficients are 
 * defined in @ref DPU_DopplerProcHWA_HwaCfg_t.
 * Window coefficients must be provided by application.\n\n
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  Paramset           |  Doppler FFT Paramset
 *  Trigger            |  Doppler FFT EDMA In Signature channel transfer completion
 *  Input              |  1D FFT of a single decompressed range gate
 *  Input MemBank      |  M0/M2   
 *  Output             |  2D FFT of single range gate
 *  Output MemBank     |  M4/M6   
 *  On Completion      |  -
 * 
 * 
 * Associated EDMA transfers:
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  EDMA Param         |  Transfer of 1D FFT for single range bin from L2 to HWA Mem
 *  EDMA Source Mem    |  Decompressed Radar Cube Scratch Buf (L2)
 *  EDMA Source Fmt    |  [numChirps][rangeBinsPerBlock (indexed via rangeBinIdx)][rxAntennas]
 *  EDMA Dest Mem      |  M0/M2
 *  EDMA Dest Fmt      |  [rangeBinIdx][numChirps][rxAntennas]
 * EDMA transfer happens one range bin at a time for ping, and similarly for pong. Two range bins (one ping, one pong)
 * after transfer are processed in one loop run of this stage (2D FFT + DDMA Demod + Azim FFT + Doppler CFAR).
 * \n\n
 * 
 * **LogAbs**\n
 * Log2 of the absolute value of each sample is computed. Input sample is of type cmplx32ImRe_t and output is of type uint16_t.
 * Note that this step is not combined with the previous step itself (even though HWA does offer capability for the same)
 * because we need the pre-logAbs value of the 2D FFT in the subsequent stages. \n\n
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  Trigger            |  Doppler FFT Paramset completion
 *  Input              |  2D FFT of a single decompressed range gate
 *  Input MemBank      |  M4/M6   
 *  Output             |  LogAbs of 2D FFT
 *  Output MemBank     |  M0/M2   
 *  On Completion      |  Triggers EDMA transfer of 2D FFT (which is the output of Doppler FFT Paramset) 
 * 
 * Associated EDMA transfers:\n
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  Paramset           |  LogAbs Paramset
 *  EDMA Param         |  Transfer of 2D FFT (complex values) from HWA Mem to L2
 *  EDMA Source Mem    |  M0/M2
 *  EDMA Source Fmt    |  [numBands][dopSubBins][rxAntennas]
 *  EDMA Dest Mem      |  Doppler FFT Scratch Buf Ping/Pong (L2)
 *  EDMA Dest Fmt      |  [dopSubBins][numBands][rxAntennas]
 * 
 * **SumRx**\n
 * Non-coherent summation of all Rx antennas is computed for each Doppler bin. 
 * The sum is done using FFT in HWA, the sum is obtained in the DC (0th) bin. \n
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  Paramset           |  SumRx Paramset
 *  Trigger            |  LogAbs Paramset Completion
 *  Input              |  LogAbs of 2D FFT
 *  Input MemBank      |  M0/M2   
 *  Output             |  LogAbs of 2D FFT summed over Rx Antennas
 *  Output MemBank     |  M1/M3   
 *  On Completion      |  -     
 *
 * \n\n
 * 
 * **A note on empty subband based demodulation**\n 
 * Note that after the Doppler-FFT, the Doppler bins are grouped into contiguous sub-bands where each sub-band corresponds 
 * to a unique TX channel. Demodulation consists of associating each sub-band with the correct TX channel. We employ an empty-band 
 * DDMA based demodulation where the TX channels are modulated such that an empty sub-band exists whose location allows the determination 
 * of the correct circular shift for demodulation. 
 * An algorithm for finding the empty sub-bands utilizing the resources available in the HWA is described in the box below:\n
 * @image html emptySubBand.jpg "Finding the empty subband" width=50% 
 * 
 * A high level picture of the demodulation scheme is detailed below-
 * @image html ddmaDemod.jpg "DDM MIMO Demodulation"
 * 
 * The following image shows how the transfer of Doppler FFT takes place from the HWA MemBank to the L2 RAM.
 * Consider a K-point Doppler FFT being performed. In actuality, as many K-point FFTs are being performed for
 * a single range gate as there are number of receiver antennae. The output of the HWA doppler FFT paramset is
 * stored in the HWA Mem Bank as shown on the left, where the samples corresponding to the Rx antennae are
 * together, with the second dimension being the bins, followed by the bands. In the case of 4 Tx antennae, the
 * number of bands for performing the empty subband based DDMA is taken to be 6. Hence the K bins forming the output
 * of the Doppler FFT can be divided into six bands having K/6 samples each. Samples corresponding to Bins nK/6 + d, 
 * where d is a constant between 1 and K/6 (both inclusive) and n = 0, 1, 2, 3, 4, 5 would refer to the same object,
 * since we have divided the spectrum into 6 bands, out of which, for every object, in the corresponding range bin, 
 * 4 bands would have peaks corresponding to the 4 Tx antennas and 2 bands would be empty. We call d here as the Doppler Sub Bin.
 * For example, for a fixed range bin x, samples corresponding to Doppler Sub Bin 1 would be Bins 1, K/6+1, 2K/6+1, 3K/6+1,
 * 4K/6+1 and 5K/6+1. If an object existed at this range bin x with a velocity corresponding to Doppler Sub Bin 1, we would
 * see peaks in 4 of the 6 aforementioned bins, and two would be empty. Which bands would actually
 * have the peaks depends on the velocity of the object. The 4 bins corresponding to an object that actually have the peak
 * values are relevant to us and the two samples corresponding to the bins in the empty subbands can be discared.
 * The location of the empty subband would help us in the DDMA demodulation, i.e., the process of mapping the samples
 * corresponding to the 4 peak bins to the corresponding Tx antennas. 
 * 
 * Let us take an example, where two object A and B are at the same range but different velocities. For example, 
 * one might see peaks at Doppler Sub Bin 1 (Bin 1, Bin K/6+1, Bin 2K/6+1 and Bin 3K/6+1) for one object 
 * (call it Object A), or peaks at Doppler Sub Bin K-1 (Bin K-1, Bin K/6-1, Bin 2K/6-1 and Bin 3K/6-1) for 
 * another object (call it Object B). Note that here we consider both objects in the same range bin, and hence
 * we'll see peaks for both objects in the same Doppler FFT Paramset run. The peaks in the spectrum indicate
 * that bands 1, 2, 3 and 4 are active bands for Object A and bands 5 and 6 are empty subbands. Similarly,
 * bands 6, 1, 2 and 3 are active bands for Object B and bands 4 and 5 are empty subbands. By design (i.e., by
 * the phase shifter configuration), we say that when bands are taken in rotation after the last empty subband, the
 * first three bands corresponding to Azimuth Tx antennae (Tx 0, 2 and 3) and the fourth band corresponds to the
 * Elevation Tx antenna (Tx 1). Hence, we have successfully demodulated the samples for Objects A and B. For Object A,
 * samples of Bin 1, Bin K/6+1, Bin 2K/6+1 and Bin 3K/6+1 correspond to Tx 0, 2, 3 and 1 respectively, and for
 * Object B, samples of Bin K-1, Bin K/6-1, Bin 2K/6-1 and Bin 3K/6-1 correspond to Tx 0, 2, 3 and 1 respectively.
 * Thus, we can separate out the Azimuth and Elevation samples for their corresponding processing.
 * 
 * From the above example one might be able to gauge that the Hardware implementation of the demodulation would be 
 * much more efficient if the samples corresponding to a Doppler Sub Bin are brought together, rather than samples
 * correponding to Bands being together. Thus, the EDMA moves the Doppler FFT samples from the HWA Memory to the L2
 * RAM in such a way that Doppler Sub Bins come together, as shown in the picture below.
 * @image html ddmaDemod1.png "EDMA Transfer of Doppler FFT from HWA Memory (left) to L2 Memory (right)" width=50% 
 * 
 * **DDMA Metric**\n
 * The DDMA Metric HWA paramset would perform a sequence of moving summation operations to compute an energy 
 * level [Z1, Z2, ..,  Z6] for all the possible hypothesis (the number of hypothesis is equivalent to the number of 
 * sub-bands, for example, 6). In a later step, the DSP finds the maximum value hypothesis i.e. Max [Z1, Z2, Z3, Z4, Z5, Z6] from
 * which the empty sub-band indices can be identified. \n
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  Paramset           |  DDMA Metric Paramset
 *  Trigger            |  SumRx Paramset completion
 *  Input              |  SumRx Output
 *  Input MemBank      |  M1/M3   
 *  Output             |  Energy level of all possible hypotheses
 *  Output MemBank     |  M0/M2   
 *  On Completion      |  Triggers EDMA transfer of DDMA Metric (current paramset) output 
 * \n
 * Associated EDMA transfers:
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  EDMA Param         |  Transfer of DDMA Metric from HWA Mem to L2
 *  EDMA Source Mem    |  M3/M2
 *  EDMA Source Fmt    |  [activeSubBands][dopSubBins]
 *  EDMA Dest Mem      |  DDMA Metric Scratch Buf Ping/Pong (L2)
 *  EDMA Dest Fmt      |  [activeSubBands][dopSubBins]
 * \n\n
 * 
 * In the following image, we see how DDMA Metric is stored. The storage is done Band-wise (Band
 * together, not a Doppler Sub Bin), and DDMA Metric samples corresponding to a single Doppler
 * Sub Bin are taken and a max is computed to obtain the empty sub band index. Note that the 6 values
 * of the Doppler Sub Band shown in this example (for 4 Tx antennas) correspond to the values [Z1, ...., Z6] 
 * as explained above. From the max value, a rotation index is computed which indicates which samples
 * correspond to the active subbands, which are then copied into a separate memory buffer (DopFFTSubMat) in the order of
 * [Tx0 (Azim), Tx2 (Azim), Tx3 (Azim), Tx 1 (Elev)], and the empty band samples are discared.
 * @image html ddmaMetric.png "How the DDMA Metric is used to perform DDMA Demodulation" width=50% 
 * 
 * 
 * The figure below shows an example of the rotation-copy.
 * @image html ddmaCopy.png "Rotation-Copy from Doppler FFT Scratch Buf in L2 (Left) to Doppler FFT Sub Mat in L2 (Right)" width=50% 
 * 
 * **SumTx**\n
 * Summation of the output of SumRx paramset is further summed over all the Tx antennas. 
 * The sum is done using FFT in HWA, the sum is obtained in the DC (0th) bin. y
 * The output of this paramset would form the detection matrix. \n\n
 * 
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  Paramset           |  SumTx Paramset
 *  Trigger            |  DDMA Metric Paramset completion
 *  Input              |  SumRx Output
 *  Input MemBank      |  M1/M2 
 *  Output             |  Range/Doppler (Detection) Matrix
 *  Output MemBank     |  M0 + offset / M3 + offset   
 *  On Completion      |  -
 * \n
 * Associated EDMA transfers:
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  EDMA Param         |  Transfer of SumTx data from HWA Mem to L3
 *  EDMA Source Mem    |  M0 + offset / M3 + offset
 *  EDMA Source Fmt    |  [rangeBins][dopSubBins]
 *  EDMA Dest Mem      |  Detection Matrix (L3)
 *  EDMA Dest Fmt      |  [rangeBins][dopSubBins]
 * \n\n
 * 
 * **DDMA Demodulation**\n
 * Once the DDMA Metric is obtained, it is sent out to the L2 scratch RAM. 
 * For further processing, DSP intervention is required. 
 * The DSP first calculates the maximum sub-band index for each doppler sub-index, 
 * and hence, uses it to identify the empty sub-bands. It then picks the non-empty subbands,
 * from the Doppler FFT scratch scratch buf and copies them in the order <Azim1, .., AzimN, Elev1, .., ElevM>
 * into the Doppler FFT SubMat scratch memory, where N is the number of azimuth virtual antennas and M the number
 * elevation virtual antennas.
 * Hence, The DSP would access the complex-Doppler FFT output and re-order the virtual channels based on the empty-band 
 * indices and subsequently write to the Doppler FFT SubMat scratch L2 memory. \n\n
 * 
 * **Azimuth FFT**\n
 * Azimuth samples from the Doppler FFT SubMat L2 memory are transferred into the HWA
 * and an FFT is performed. Note that antenna calibration also may be performed, in a way
 * that before the FFT is computed, the azimuth samples in the Doppler FFT are multiplied by
 * the antenna calibration parameters/coefficients. \n\n
 * 
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  Paramset           |  Azimuth FFT Paramset
 *  Trigger            |  EDMA Azimuth FFT In completion (signature) -> Dummy paramset completion
 *  Input              |  Doppler FFT Sub Mat Output
 *  Input MemBank      |  M0/M2
 *  Output             |  Azimuth FFT Data
 *  Output MemBank     |  M4/M6   
 *  On Completion      |  -
 * \n
 * Associated EDMA transfers: (input EDMA)
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  EDMA Param         |  Transfer of 2D FFT (complex values) corresponding to Azimuth Samples from L2 to HWA Mem
 *  EDMA Source Mem    |  Doppler FFT Sub Mat (L2)
 *  EDMA Source Fmt    |  [numDopSubBins][numVirtualAntennas]
 *  EDMA Dest Mem      |  HWA Mem
 *  EDMA Dest Fmt      |  [azimFFTSize]
 * \n\n
 * 
 * **CFAR (Doppler)**\n
 * CFAR-OS is performed in the Doppler direction, on the Azimuth FFT samples. The maximum number of
 * peaks that the paramset will store is provided by the application. The output of this DPU is represented
 * as a (32 bit Real, 32 bit Imaginary) complex number where the real part of each sample is used to calculate the 
 * peak index and the imaginary part indicates the noise level. \n\n
 * 
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  Paramset           |  Doppler CFAR Paramset
 *  Trigger            |  Azimuth FFT paramset completion
 *  Input              |  Azimuth FFT output
 *  Input MemBank      |  M4/M6
 *  Output             |  Doppler CFAR-OS data
 *  Output MemBank     |  M0/M2   
 *  On Completion      |  CFAR Out EDMA triggered
 * \n
 * Associated EDMA transfers: (output EDMA)
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  EDMA Param         |  Transfer of CFAR Output from HWA Mem to L2
 *  EDMA Source Mem    |  M0/M2 (HWA)
 *  EDMA Source Fmt    |  [numCfarPeaks][32-bit Imag (Noise val)/32-bit Real (Peak location)]
 *  EDMA Dest Mem      |  CFAR Out scratch buf
 *  EDMA Dest Fmt      |  [numCfarPeaks][32-bit Imag (Noise val)/32-bit Real (Peak location)]
 * \n\n
 * 
 * **Local Max**\n
 * A local maxima is performed on the Azimuth FFT samples in both range and doppler dimension. Row-wise as well
 * as column-wise comparison is performed, and "0 1 0 1 0 1 0 1", i.e., a "+" shaped comparison is utilized.
 * The output bit pattern is stored in the destination memory packed as 32-bit words, 
 * with the LSB bit corresponding to column count of 0. \n\n
 * 
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  Paramset           |  Local Max Paramset
 *  Trigger            |  Doppler CFAR-OS paramset completion
 *  Input              |  Azimuth FFT output
 *  Input MemBank      |  M4/M6
 *  Output             |  Doppler Local Max data
 *  Output MemBank     |  M1/M3   
 *  On Completion      |  Local Max Out EDMA triggered
 * \n
 * Associated EDMA transfers: (output EDMA)
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  EDMA Param         |  Transfer of Local Max Output from HWA Mem to L2
 *  EDMA Source Mem    |  M1/M3 (HWA)
 *  EDMA Source Fmt    |  [localMaxOutSize]
 *  EDMA Dest Mem      |  Local Max Out scratch buf
 *  EDMA Dest Fmt      |  [localMaxOutSize]
 * \n\n
 * 
  * **Azimuth Dummy Paramset**\n
 *  A dummy paramset is used to trigger the Azimuth FFT Out EDMA after the completion of the Local Max 
 *  paramset. This is needed because the Azimuth FFT output acts as the input to both Local Max and 
 *  Doppler CFAR-OS paramsets, and the EDMA and HWA cannot access the same memory at the same time.\n\n
 * 
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  Paramset           |  Azimuth FFT (Dummy) Paramset
 *  Trigger            |  Local Max paramset completion
 *  Input              |  -
 *  Input MemBank      |  -
 *  Output             |  -
 *  Output MemBank     |  -  
 *  On Completion      |  Azimuth FFT Out EDMA triggered
 * \n
 * Associated EDMA transfers: (output EDMA)
 *   Parameter         |  Comment  
 *  :------------------|:--------------
 *  EDMA Param         |  Transfer of Azimuth FFT Output from HWA Mem to L2
 *  EDMA Source Mem    |  M0/M2 (HWA)
 *  EDMA Source Fmt    |  [azimuthFFTOutSize]
 *  EDMA Dest Mem      |  Azimuth FFT Out scratch buf
 *  EDMA Dest Fmt      |  [azimuthFFTOutSize]
 * \n\n
 * 
 * **Extract Object List**\n
 * This step is performed in the DSP. A loop is run over all the CFAR peaks detected. If the peak position is
 * also detected by the Local Max paramset, the following information is stored for the particular peak:
 * azimuth bin index, doppler index, CFAR noise, doppler FFT samples corresponding to the particular range/doppler
 * indices, azimuth FFT output corresponding to the sample and one previous and one sample after the corresponding sample. \n\n
 * 
 * 
 * **Exported APIs**\n
 * DPU initialization is done through @ref DPU_DopplerProcHWA_init.
 * 
 * DPU configuration is done by @ref DPU_DopplerProcHWA_config. The configuration can only be done after
 * the DPU has been initialized. The configuration parameters are described in @ref DPU_DopplerProcHWA_Config. \n\n
 *
 * The DPU is executed by calling @ref DPU_DopplerProcHWA_process. \n
 * This function will control the entire processing of the Doppler proc DDMA DPU. It will loop over the 
 * range bins, configure HWA common registers for each stage, trigger EDMA transfers and poll on EDMA
 * transfer completions. \n\n
 * 
 * 
 * **Detailed block diagram, description and timing**\n
 *
 * @image html dopplerDPU.jpg "Doppler DDMA DPU implementation" 
 * 
 * For a legend of the paramset boxes in the above figure, refer to the figure below: \n
 * 
 * @image html legend.jpg "Legend" width=20% 
 * 
 * 
 * The following shows the timing diagram of the DPU: \n
 * @image html timing.jpg "Doppler DDMA DPU timing (stage II)" width=80% 
 *
 * \n\n\n 
 *
 * 
 */



/**
@defgroup DPU_DOPPLERPROC_EXTERNAL_FUNCTION            dopplerProc DPU External Functions
@ingroup DOPPLER_PROC_DPU
@brief
*   The section has a list of all the exported API which the applications need to
*   invoke in order to use the dopplerProc DPU
*/
/**
@defgroup DPU_DOPPLERPROC_EXTERNAL_DATA_STRUCTURE      dopplerProc DPU External Data Structures
@ingroup DOPPLER_PROC_DPU
@brief
*   The section has a list of all the data structures which are exposed to the application
*/
/**
@defgroup DPU_DOPPLERPROC_ERROR_CODE                   dopplerProc DPU Error Codes
@ingroup DOPPLER_PROC_DPU
@brief
*   The section has a list of all the error codes which are generated by the dopplerProc DPU
*/
/**
@defgroup DPU_DOPPLERPROC_INTERNAL_FUNCTION            dopplerProc DPU Internal Functions
@ingroup DOPPLER_PROC_DPU
@brief
*   The section has a list of all internal API which are not exposed to the external
*   applications.
*/
/**
@defgroup DPU_DOPPLERPROC_INTERNAL_DATA_STRUCTURE      dopplerProc DPU Internal Data Structures
@ingroup DOPPLER_PROC_DPU
@brief
*   The section has a list of all internal data structures which are used internally
*   by the dopplerProc DPU module.
*/
/**
@defgroup DPU_DOPPLERPROC_INTERNAL_DEFINITION          dopplerProc DPU Internal Definitions
@ingroup DOPPLER_PROC_DPU
@brief
*   The section has a list of all internal definitions which are used internally
*   by the dopplerProc DPU.
*/



#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief
 *  dopplerProc DPU EDMA configuration parameters
 *
 * @details
 *  The structure is used to hold the EDMA configuration parameters
 *  for the Doppler Processing DPU
 *
 *  \ingroup DPU_DOPPLERPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_DopplerProc_Edma_t
{
    /*! @brief  EDMA Ping/Pong channel. */
    DPEDMA_ChanCfg  pingPong[2];
}DPU_DopplerProc_Edma;

/**
 * @brief
 *  dopplerProc DPU EDMA configuration parameters
 *
 * @details
 *  The structure is used to hold the EDMA configuration parameters
 *  for the Doppler Processing DPU
 *
 *  \ingroup DPU_DOPPLERPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_DopplerProc_EdmaIntrObj_t
{
    /*! @brief  EDMA Ping/Pong channel. */
    Edma_IntrObject  *pingPong[2];
}DPU_DopplerProc_EdmaIntrObj;


/**
 * @brief
 *  dopplerProc DPU statistics
 *
 * @details
 *  The structure is used to hold the statistics of the DPU 
 *
 *  \ingroup DPU_DOPPLERPROC_EXTERNAL_DATA_STRUCTURE
 */
typedef struct DPU_DopplerProc_Stats_t
{
    /*! @brief total number of DPU processing */
    uint32_t            numProcess;

    /*! @brief For HWA version of the DPU: total processing time including EDMA transfers.\n
               For DSP version of the DPU: total processing time excluding EDMA transfers.*/
    uint64_t            processingTime;
    
    /*! @brief time spent waiting for EDMA transfers. Valid only for DSP version of DPU.*/
    uint64_t            waitTime;
}DPU_DopplerProc_Stats;

#ifdef __cplusplus
}
#endif

#endif 
