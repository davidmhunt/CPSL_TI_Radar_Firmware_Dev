/**
 *   @file  mmw_sar_meta.h
 *
 *   @brief
 *      Per-chirp SAR metadata record streamed after the ADC data of every
 *      chirp (lvdsStreamCfg dataFmt 2, CBUFF ADC_USER). The byte layout is
 *      the contract in docs/lvds_data_format.md; the static asserts below
 *      pin it.
 *      Project file (not TI source).
 */
#ifndef MMW_SAR_META_H
#define MMW_SAR_META_H

#include <stdint.h>
#include <stddef.h>
#include <ti/drivers/adcbuf/ADCBuf.h>
#include <ti/control/mmwavelink/mmwavelink.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief Compile-time check. TI ARM CGT 16.9 (the SDK 3.6 compiler) has no C11
 *        _Static_assert, so this is the classic negative-array-size form: a
 *        false condition fails the build with "the size of an array must be
 *        greater than zero".
 */
#define MMWDEMO_STATIC_ASSERT(cond, name) \
    extern char mmwDemoStaticAssert_##name[(cond) ? 1 : -1]

/*! @brief magic: bytes "SARM" in memory order (little-endian u32) */
#define MMWDEMO_SAR_META_MAGIC          0x4D524153U
/*! @brief record layout version */
#define MMWDEMO_SAR_META_VERSION        1U
/*! @brief record slots per chirp packet: chirp k uses slot (k & 1) */
#define MMWDEMO_SAR_META_NUM_SLOTS      2U

/** @name flags bits (docs/lvds_data_format.md section 2) @{ */
#define MMWDEMO_SAR_FLAG_SAT_VALID      (1U << 0)   /*!< satSlices/satRefLag hold a result      */
#define MMWDEMO_SAR_FLAG_SAT_MON        (1U << 1)   /*!< RX saturation monitor on in this run   */
#define MMWDEMO_SAR_FLAG_LATE           (1U << 2)   /*!< chirp-start interrupt ran late         */
#define MMWDEMO_SAR_FLAG_SKIP           (1U << 3)   /*!< chirp-start interrupt(s) missed before  */
#define MMWDEMO_SAR_FLAG_RESYNC         (1U << 4)   /*!< counters reset at the last frame start */
/** @} */

/**
 * @brief  Per-chirp metadata record (32 bytes, little-endian, device memory
 *         image). See docs/lvds_data_format.md section 2 for every field.
 */
typedef struct MmwDemo_SarChirpMeta_t
{
    uint32_t    magic;              /*!<  0: MMWDEMO_SAR_META_MAGIC                         */
    uint16_t    version;            /*!<  4: MMWDEMO_SAR_META_VERSION                       */
    uint16_t    flags;              /*!<  6: MMWDEMO_SAR_FLAG_*                             */
    uint32_t    frameIdx;           /*!<  8: frame index in the run, 0 first               */
    uint16_t    chirpInFrame;       /*!< 12: chirp index in the frame                       */
    uint16_t    numChirpsPerFrame;  /*!< 14: chirps per frame of this run                   */
    uint32_t    globalChirpIdx;     /*!< 16: frameIdx * numChirpsPerFrame + chirpInFrame    */
    uint16_t    runIdx;             /*!< 20: sensorStart count since boot                   */
    uint8_t     satSlices;          /*!< 22: saturated primary slices of chirp (idx - lag)  */
    uint8_t     satRefLag;          /*!< 23: chirps back that satSlices refers to           */
    uint64_t    tsTicks;            /*!< 24: RTI FRC0 at the chirp-start interrupt, 10 ns  */
} MmwDemo_SarChirpMeta;

MMWDEMO_STATIC_ASSERT(sizeof(MmwDemo_SarChirpMeta) == 32U,                         sar_meta_size);
MMWDEMO_STATIC_ASSERT(offsetof(MmwDemo_SarChirpMeta, magic) == 0U,                 sar_meta_off_magic);
MMWDEMO_STATIC_ASSERT(offsetof(MmwDemo_SarChirpMeta, version) == 4U,               sar_meta_off_version);
MMWDEMO_STATIC_ASSERT(offsetof(MmwDemo_SarChirpMeta, flags) == 6U,                 sar_meta_off_flags);
MMWDEMO_STATIC_ASSERT(offsetof(MmwDemo_SarChirpMeta, frameIdx) == 8U,              sar_meta_off_frameIdx);
MMWDEMO_STATIC_ASSERT(offsetof(MmwDemo_SarChirpMeta, chirpInFrame) == 12U,         sar_meta_off_chirpInFrame);
MMWDEMO_STATIC_ASSERT(offsetof(MmwDemo_SarChirpMeta, numChirpsPerFrame) == 14U,    sar_meta_off_numChirpsPerFrame);
MMWDEMO_STATIC_ASSERT(offsetof(MmwDemo_SarChirpMeta, globalChirpIdx) == 16U,       sar_meta_off_globalChirpIdx);
MMWDEMO_STATIC_ASSERT(offsetof(MmwDemo_SarChirpMeta, runIdx) == 20U,               sar_meta_off_runIdx);
MMWDEMO_STATIC_ASSERT(offsetof(MmwDemo_SarChirpMeta, satSlices) == 22U,            sar_meta_off_satSlices);
MMWDEMO_STATIC_ASSERT(offsetof(MmwDemo_SarChirpMeta, satRefLag) == 23U,            sar_meta_off_satRefLag);
MMWDEMO_STATIC_ASSERT(offsetof(MmwDemo_SarChirpMeta, tsTicks) == 24U,              sar_meta_off_tsTicks);

/*! @brief The record slots, in CBUFF-reachable L3 memory (mmw_sar_meta.c) */
extern MmwDemo_SarChirpMeta gMmwDemoSarChirpMetaSlots[MMWDEMO_SAR_META_NUM_SLOTS];

/* Total block streamed per chirp: two slots, 64 bytes (a multiple of 8 bytes) */
MMWDEMO_STATIC_ASSERT(sizeof(MmwDemo_SarChirpMeta) * MMWDEMO_SAR_META_NUM_SLOTS == 64U, sar_meta_block);

/* Boot-time setup: chirp-start Hwi, frame-start and chirp-available
 * listeners, 1 s timestamp-extension clock. Called once from the init task. */
extern int32_t MmwDemo_sarMetaInit(void *socHandle);

/* Reconfig: per-run constants from the profile and frame (task context). */
extern int32_t MmwDemo_sarMetaConfig(const rlProfileCfg_t *profileCfg, uint16_t numChirpsPerFrame,
                                     uint32_t framePeriodicity);

/* Sensor start, after ADCBUF/CQ are configured and before MMWave_start:
 * resets counters, statistics and slots for a new run. */
extern void MmwDemo_sarMetaRunStart(ADCBuf_Handle adcBufHandle, uint8_t satMonEnabled, uint16_t satNumSlices);

/* CLI "sarStats": prints the running counters. */
extern void MmwDemo_sarMetaPrintStats(void);

#ifdef __cplusplus
}
#endif

#endif /* MMW_SAR_META_H */
