/**
 *   @file  mmw_sar_meta.c
 *
 *   @brief
 *      Per-chirp SAR metadata: interrupt handlers that fill the record slots
 *      CBUFF streams after each chirp's ADC data (lvdsStreamCfg dataFmt 2),
 *      the saturation result from the CQ2 report, the 64-bit timestamp and
 *      the sarStats counters. Format: docs/lvds_data_format.md.
 *      Project file (not TI source).
 *
 *  Interrupts (SDK common/sys_common_xwr18xx_mss.h:352-374; VIM: a lower
 *  number has the higher priority):
 *   - 98  FRAME_START_INT  SOC listener: counter cross-check per frame
 *   - 99  CHIRP_START_INT  own Hwi: fills chirp n's record (slot n & 1)
 *   - 123 CHIRP_AVAIL_IRQ  SOC listener: reads chirp n's CQ2 (saturation)
 *  Every handler is O(1) (the CQ2 loop is bounded by 64 primary slices),
 *  makes no driver calls and runs its state update with interrupts off.
 *
 *  Why two slots: chirp n's record is written at chirp n's start and CBUFF
 *  reads it at the end of packet n, which starts at chirp n's chirp-available
 *  event. With one slot, chirp n+1's start could overwrite it while packet n
 *  is still being sent. Slot (n & 1) is next written at chirp n+2's start;
 *  packet n has finished by then, because it must finish within one chirp
 *  period of its chirp-available event (else ADCBUF would be overrun).
 */

/**************************************************************************
 *************************** Include Files ********************************
 **************************************************************************/
#include <stdint.h>
#include <string.h>

/* BIOS/XDC Include Files. */
#include <xdc/std.h>
#include <xdc/runtime/System.h>
#include <xdc/runtime/Types.h>
#include <ti/sysbios/hal/Hwi.h>
#include <ti/sysbios/knl/Clock.h>
#include <ti/sysbios/timers/rti/Timer.h>

/* MMWSDK Include Files. */
#include <ti/common/sys_common.h>
#include <ti/drivers/soc/soc.h>
#include <ti/drivers/osal/HwiP.h>
#include <ti/drivers/adcbuf/ADCBuf.h>
#include <ti/drivers/cbuff/cbuff.h>
#include <ti/drivers/uart/UART.h>
#include <ti/utils/cli/cli.h>

/* Demo Include Files */
#include <ti/demo/xwr18xx/mmw/mss/mmw_mss.h>
#include <ti/demo/xwr18xx/mmw/mss/mmw_sar_meta.h>

extern MmwDemo_MSS_MCB    gMmwMssMCB;

/**************************************************************************
 *************************** Local Definitions ****************************
 **************************************************************************/

/**
 * @brief RTI free-running counter 0 (RTIFRC0). SYS/BIOS uses RTI counter 0 for
 *        its Clock tick: base 0xFFFFFC00 (generated BIOS config), FRC0 at
 *        offset 0x10 (BIOS ti/sysbios/timers/rti/Timer.xdc:274), prescale 1 so
 *        FRC0 counts RTICLK 200 MHz / 2 = 100 MHz (BIOS timers/rti/Timer.c:376,
 *        697). It runs during WFI (it is what wakes the CPU for the BIOS tick),
 *        unlike the R4F PMU cycle counter.
 */
#define MMWDEMO_SAR_RTI_FRC0            (*(volatile uint32_t *)0xFFFFFC10U)

/*! @brief Expected timestamp clock; checked against BIOS at init */
#define MMWDEMO_SAR_TS_HZ               100000000U

/*! @brief Period of the clock function that keeps the 64-bit extension
 *         current while no chirps run (BIOS ticks of 1 ms; FRC0 wraps 42.9 s) */
#define MMWDEMO_SAR_TS_REFRESH_TICKS    1000U

/** @name satWord packing: one 32-bit word, so the chirp-start handler reads a
 *        consistent (chirp, result) pair even if it preempts the chirp-available
 *        handler. bit 31 valid, bits 30..8 chirp index mod 2^23, bits 7..0 slices.
 *  @{ */
#define MMWDEMO_SAR_SAT_VALID           0x80000000U
#define MMWDEMO_SAR_SAT_IDX_MASK        0x007FFFFFU
#define MMWDEMO_SAR_SAT_IDX_SHIFT       8U
#define MMWDEMO_SAR_SAT_MAX_LAG         254U
/** @} */

/*! @brief Largest CQ2 slice count (rl_monitoring.h:1877-1982: N <= 127) */
#define MMWDEMO_SAR_CQ2_MAX_SLICES      127U

/**
 * @brief Running counters, reset at every sensorStart (CLI "sarStats").
 */
typedef struct MmwDemo_SarMetaStats_t
{
    uint32_t    chirps;             /*!< chirps counted: chirp-start interrupts + missed ones */
    uint32_t    chirpStartIsr;      /*!< chirp-start interrupts taken                          */
    uint32_t    frames;             /*!< frame-start interrupts                                */
    uint32_t    chirpAvail;         /*!< chirp-available interrupts                            */
    uint32_t    lateIsr;            /*!< records flagged LATE                                  */
    uint32_t    missedChirpIsr;     /*!< chirp-start interrupts inferred missed (SKIP)         */
    uint32_t    frameResync;        /*!< frame starts that found the counters out of step      */
    uint32_t    availResync;        /*!< frame starts that corrected the chirp-available count */
    uint32_t    satChirps;          /*!< chirps with at least one saturated primary slice      */
} MmwDemo_SarMetaStats;

/**
 * @brief Module state. The cfg part is written in task context while no
 *        chirps run; the rest only with interrupts disabled.
 */
typedef struct MmwDemo_SarMeta_t
{
    /* cfg: per run */
    uint32_t                numChirpsPerFrame;  /*!< 0 = no run configured: handlers only count */
    uint32_t                tcTicks;            /*!< idle + rampEnd, 10 ns ticks                */
    uint32_t                lateBudgetTicks;    /*!< adcStart + Ns/fs, 10 ns ticks               */
    uint32_t                gap0Ticks;          /*!< last chirp of a frame -> chirp 0 of the next
                                                     (frame period - (N-1) * Tc), 0 = no check     */
    volatile uint32_t       *cq2Addr;           /*!< CQ2 (MSS address), NULL = monitor off      */
    uint32_t                cq2MaxSlices;       /*!< slices configured in CQRxSatMonitor        */
    uint16_t                runFlags;           /*!< flags set on every record of the run       */
    uint16_t                runIdx;             /*!< sensorStart count                          */

    /* run state */
    uint32_t                frameIdx;           /*!< frame of the next chirp                    */
    uint32_t                chirpInFrame;       /*!< index of the next chirp in its frame       */
    uint32_t                framesStarted;      /*!< frame-start interrupts in this run         */
    uint32_t                availCount;         /*!< chirp-available interrupts in this run     */
    uint32_t                satWord;            /*!< newest saturation result, packed           */
    uint16_t                pendingFlags;       /*!< flags for the next record only             */
    uint64_t                lastChirpTs;        /*!< tsTicks of the previous chirp              */

    /* 64-bit extension of RTI FRC0 (since boot, never reset) */
    uint32_t                tsHi;
    uint32_t                tsLastLo;
    uint32_t                tsHz;               /*!< BIOS-reported FRC0 frequency               */

    MmwDemo_SarMetaStats    stats;
} MmwDemo_SarMeta;

/**************************************************************************
 *************************** Global Definitions ***************************
 **************************************************************************/

/**
 * @brief The record slots, streamed by the CBUFF HW session as its user buffer.
 *        Placed in L3 (EDMA-reachable; .cbuffL3Memory is the SDK CBUFF unit
 *        test's pattern, drivers/cbuff/test/common/test_common.c:88-96).
 */
#pragma DATA_SECTION(gMmwDemoSarChirpMetaSlots, ".cbuffL3Memory");
#pragma DATA_ALIGN(gMmwDemoSarChirpMetaSlots, 16);
MmwDemo_SarChirpMeta gMmwDemoSarChirpMetaSlots[MMWDEMO_SAR_META_NUM_SLOTS];

static MmwDemo_SarMeta gMmwDemoSarMeta;

/**************************************************************************
 ************************** Interrupt-level helpers ***********************
 **************************************************************************/

/**
 *  @b Description
 *  @n
 *      Returns RTI FRC0 extended to 64 bits. Call with interrupts disabled;
 *      it must run at least once per FRC0 wrap (42.9 s), which every handler
 *      and the 1 s clock function guarantee.
 */
static inline uint64_t MmwDemo_sarTsNow(MmwDemo_SarMeta *s)
{
    uint32_t lo = MMWDEMO_SAR_RTI_FRC0;

    if (lo < s->tsLastLo)
    {
        s->tsHi++;
    }
    s->tsLastLo = lo;
    return (((uint64_t)s->tsHi) << 32) | (uint64_t)lo;
}

/**
 *  @b Description
 *  @n
 *      CHIRP_START_INT handler: fills the record of the chirp that is starting
 *      into slot (globalChirpIdx & 1) and advances the counters.
 */
static void MmwDemo_sarChirpStartIsr(uintptr_t arg)
{
    MmwDemo_SarMeta         *s = &gMmwDemoSarMeta;
    MmwDemo_SarChirpMeta    *rec;
    UInt                    key;
    uint64_t                ts;
    uint32_t                n;
    uint32_t                c;
    uint32_t                missed = 0U;
    uint32_t                global;
    uint32_t                satWord;
    uint32_t                lag;
    uint16_t                flags;

    key = Hwi_disable();
    ts  = MmwDemo_sarTsNow(s);
    s->stats.chirpStartIsr++;

    n = s->numChirpsPerFrame;
    if (n == 0U)
    {
        /* No run configured (cannot happen while chirping) */
        Hwi_restore(key);
        return;
    }

    flags = (uint16_t)(s->runFlags | s->pendingFlags);
    s->pendingFlags = 0U;
    c = s->chirpInFrame;

    if (c >= n)
    {
        /* The frame-start handler has not run yet for this frame: this chirp
         * opens the next frame (the frame-start check accepts this order). */
        c = 0U;
        s->frameIdx++;
    }

    /* Expected spacing from the previous chirp: Tc within a frame, Tc + Tb
     * (gap0Ticks) into chirp 0 of the next frame. A longer gap means late or
     * missed interrupts (ISR-to-ISR spacing, so a lower bound on lateness).
     * Not checked on the run's first chirp, nor at chirp 0 without a gap0. */
    {
        uint32_t base = (c > 0U) ? s->tcTicks : s->gap0Ticks;

        if ((s->lastChirpTs != 0U) && (base != 0U) && (s->tcTicks != 0U))
        {
            uint64_t dt64   = ts - s->lastChirpTs;
            uint32_t dt     = (dt64 > 0x7FFFFFFFULL) ? 0x7FFFFFFFU : (uint32_t)dt64;
            uint32_t excess = (dt > base) ? (dt - base) : 0U;

            if (excess > (s->tcTicks >> 1))
            {
                missed = (excess + (s->tcTicks >> 1)) / s->tcTicks;
                flags |= MMWDEMO_SAR_FLAG_SKIP;
                s->stats.missedChirpIsr += missed;
                c += missed;
                if (c >= n)
                {
                    s->frameIdx += c / n;
                    c = c % n;
                }
            }
            else if (excess > s->lateBudgetTicks)
            {
                flags |= MMWDEMO_SAR_FLAG_LATE;
                s->stats.lateIsr++;
            }
        }
    }

    global = (s->frameIdx * n) + c;
    rec    = &gMmwDemoSarChirpMetaSlots[global & 1U];

    rec->magic             = MMWDEMO_SAR_META_MAGIC;
    rec->version           = MMWDEMO_SAR_META_VERSION;
    rec->frameIdx          = s->frameIdx;
    rec->chirpInFrame      = (uint16_t)c;
    rec->numChirpsPerFrame = (uint16_t)n;
    rec->globalChirpIdx    = global;
    rec->runIdx            = s->runIdx;
    rec->tsTicks           = ts;

    /* Newest saturation result (CQ2 read at a chirp-available event) and the
     * chirp it belongs to: lag = this chirp - that chirp (normally 1). */
    satWord = s->satWord;
    lag     = (global - ((satWord >> MMWDEMO_SAR_SAT_IDX_SHIFT) & MMWDEMO_SAR_SAT_IDX_MASK)) & MMWDEMO_SAR_SAT_IDX_MASK;
    if (((satWord & MMWDEMO_SAR_SAT_VALID) != 0U) && (lag <= MMWDEMO_SAR_SAT_MAX_LAG))
    {
        flags         |= MMWDEMO_SAR_FLAG_SAT_VALID;
        rec->satSlices = (uint8_t)(satWord & 0xFFU);
        rec->satRefLag = (uint8_t)lag;
    }
    else
    {
        rec->satSlices = 0U;
        rec->satRefLag = 0U;
    }
    rec->flags = flags;

    /* Make the record stores complete before CBUFF's EDMA can read the slot */
    __asm(" DSB");

    s->chirpInFrame = c + 1U;
    s->lastChirpTs  = ts;
    s->stats.chirps += 1U + missed;

    Hwi_restore(key);
}

/**
 *  @b Description
 *  @n
 *      FRAME_START_INT listener: frame f starts, so the counters must say
 *      "frame f-1 complete" (normal order) or "chirp 0 of frame f done" (the
 *      chirp-start handler ran first). Anything else is resynchronised and
 *      flagged on the next record. Also resynchronises the chirp-available
 *      count, which must be f * N at a frame start.
 */
static void MmwDemo_sarFrameStartIsr(uintptr_t arg)
{
    MmwDemo_SarMeta *s = &gMmwDemoSarMeta;
    UInt            key;
    uint32_t        n;
    uint32_t        f;
    uint32_t        expectAvail;

    key = Hwi_disable();
    (void)MmwDemo_sarTsNow(s);
    s->stats.frames++;

    n = s->numChirpsPerFrame;
    if (n != 0U)
    {
        f = s->framesStarted;
        s->framesStarted++;

        if (((s->frameIdx + 1U) == f) && (s->chirpInFrame == n))
        {
            /* Normal: frame f-1 had all its chirps */
            s->frameIdx     = f;
            s->chirpInFrame = 0U;
        }
        else if ((s->frameIdx == f) && (s->chirpInFrame <= n))
        {
            /* First frame of the run, or the chirp-start handler already opened
             * frame f (this interrupt was serviced late, possibly after several
             * chirps): the counters are right, keep them. */
        }
        else
        {
            s->frameIdx      = f;
            s->chirpInFrame  = 0U;
            s->pendingFlags |= MMWDEMO_SAR_FLAG_RESYNC;
            s->stats.frameResync++;
        }

        expectAvail = f * n;
        if (s->availCount != expectAvail)
        {
            s->availCount = expectAvail;
            s->stats.availResync++;
        }
    }
    Hwi_restore(key);
}

/**
 *  @b Description
 *  @n
 *      CHIRP_AVAIL_IRQ listener: chirp n's ADC data and CQ2 report have just
 *      become valid (CQ RAM is ping-pong, refreshed every chirp: ICD rev 2.23
 *      section 10.2). Counts the primary slices with a non-zero saturation
 *      count and publishes (n, count) for the next chirp-start handler.
 *      CQ2 in 16-bit mode (ICD Fig. 10.9): byte 0 = slices reported M, then
 *      P1 S1 P2 S2 ...: primary slice bytes are the odd byte offsets.
 */
static void MmwDemo_sarChirpAvailIsr(uintptr_t arg)
{
    MmwDemo_SarMeta     *s   = &gMmwDemoSarMeta;
    volatile uint32_t   *cq2 = s->cq2Addr;
    uint32_t            sat  = 0U;
    uint32_t            idx;
    uint32_t            m;
    uint32_t            i;
    UInt                key;

    if (cq2 != NULL)
    {
        /* Word reads (CQ RAM is in the ADCBUF space); little-endian bytes */
        m = cq2[0] & 0xFFU;
        if (m > s->cq2MaxSlices)
        {
            m = s->cq2MaxSlices;
        }
        for (i = 1U; i <= m; i += 2U)
        {
            if (((cq2[i >> 2] >> ((i & 3U) << 3)) & 0xFFU) != 0U)
            {
                sat++;
            }
        }
    }

    key = Hwi_disable();
    (void)MmwDemo_sarTsNow(s);
    idx = s->availCount;
    s->availCount++;
    s->stats.chirpAvail++;
    if (cq2 != NULL)
    {
        s->satWord = MMWDEMO_SAR_SAT_VALID |
                     ((idx & MMWDEMO_SAR_SAT_IDX_MASK) << MMWDEMO_SAR_SAT_IDX_SHIFT) |
                     (sat & 0xFFU);
        if (sat != 0U)
        {
            s->stats.satChirps++;
        }
    }
    Hwi_restore(key);
}

/**
 *  @b Description
 *  @n
 *      BIOS clock function (1 s): keeps the 64-bit timestamp extension
 *      current while no chirps run.
 */
static Void MmwDemo_sarTsRefreshFxn(UArg arg)
{
    UInt key = Hwi_disable();
    (void)MmwDemo_sarTsNow(&gMmwDemoSarMeta);
    Hwi_restore(key);
}

/**************************************************************************
 *************************** Task-level API *******************************
 **************************************************************************/

/**
 *  @b Description
 *  @n
 *      One-time setup at boot: registers the three interrupt handlers and the
 *      timestamp clock function, and checks the timestamp clock rate.
 *
 *  @retval  0 on success, <0 on error
 */
int32_t MmwDemo_sarMetaInit(void *socHandle)
{
    SOC_SysIntListenerCfg   listenerCfg;
    HwiP_Params             hwiParams;
    Clock_Params            clockParams;
    Types_FreqHz            freq;
    int32_t                 errCode;

    memset((void *)&gMmwDemoSarMeta, 0, sizeof(gMmwDemoSarMeta));
    memset((void *)&gMmwDemoSarChirpMetaSlots[0], 0, sizeof(gMmwDemoSarChirpMetaSlots));

    /* Timestamp clock: BIOS's RTI counter 0 frequency (intFreq / (prescale + 1)) */
    Timer_getFreq((Timer_Handle)Clock_getTimerHandle(), &freq);
    gMmwDemoSarMeta.tsHz = freq.lo;
    if ((freq.hi != 0U) || (freq.lo != MMWDEMO_SAR_TS_HZ))
    {
        System_printf("Warning: RTI FRC0 runs at %u Hz, not %u Hz: tsTicks units differ from the format doc\n",
                      freq.lo, MMWDEMO_SAR_TS_HZ);
    }
    gMmwDemoSarMeta.tsLastLo = MMWDEMO_SAR_RTI_FRC0;

    Clock_Params_init(&clockParams);
    clockParams.period    = MMWDEMO_SAR_TS_REFRESH_TICKS;
    clockParams.startFlag = TRUE;
    if (Clock_create(MmwDemo_sarTsRefreshFxn, MMWDEMO_SAR_TS_REFRESH_TICKS, &clockParams, NULL) == NULL)
    {
        System_printf("Error: SAR timestamp clock create failed\n");
        return -1;
    }

    /* Chirp start: not registered by the SOC driver, so a plain Hwi */
    HwiP_Params_init(&hwiParams);
    hwiParams.name = "SarChirpStartISR";
    if (HwiP_create((int32_t)SOC_XWR18XX_MSS_CHIRP_START_INT, MmwDemo_sarChirpStartIsr, &hwiParams) == NULL)
    {
        System_printf("Error: chirp-start Hwi create failed\n");
        return -1;
    }

    /* Frame start and chirp available: owned by the SOC driver, add listeners */
    memset((void *)&listenerCfg, 0, sizeof(listenerCfg));
    listenerCfg.systemInterrupt = SOC_XWR18XX_MSS_FRAME_START_INT;
    listenerCfg.listenerFxn     = MmwDemo_sarFrameStartIsr;
    if (SOC_registerSysIntListener((SOC_Handle)socHandle, &listenerCfg, &errCode) == NULL)
    {
        System_printf("Error: frame-start listener failed [Error=%d]\n", errCode);
        return -1;
    }

    listenerCfg.systemInterrupt = SOC_XWR18XX_MSS_CHIRP_AVAIL_IRQ;
    listenerCfg.listenerFxn     = MmwDemo_sarChirpAvailIsr;
    if (SOC_registerSysIntListener((SOC_Handle)socHandle, &listenerCfg, &errCode) == NULL)
    {
        System_printf("Error: chirp-available listener failed [Error=%d]\n", errCode);
        return -1;
    }
    return 0;
}

/**
 *  @b Description
 *  @n
 *      Stores the per-run constants (called on reconfig, before the run).
 *      Chirp period and lateness budget come from the profile in 10 ns units,
 *      the RTI tick (rl_sensor.h:650-670: idle, ADC start and ramp end LSB 10 ns;
 *      digOutSampleRate in ksps; framePeriodicity LSB 5 ns).
 *
 *  @retval  0 on success, <0 on error
 */
int32_t MmwDemo_sarMetaConfig(const rlProfileCfg_t *profileCfg, uint16_t numChirpsPerFrame,
                              uint32_t framePeriodicity)
{
    uint32_t adcTicks = 0U;
    uint32_t tc       = profileCfg->idleTimeConst + profileCfg->rampEndTime;
    uint32_t frameTicks = framePeriodicity / 2U;   /* 5 ns LSB (rl_sensor.h:980-989) -> 10 ns */
    uint32_t busy;

    if (profileCfg->digOutSampleRate != 0U)
    {
        /* Ns / (rate ksps) seconds = Ns * 1e5 / rate ticks of 10 ns */
        adcTicks = ((uint32_t)profileCfg->numAdcSamples * 100000U) / (uint32_t)profileCfg->digOutSampleRate;
    }

    /* Written while no chirps run; the handlers read it only during a run */
    gMmwDemoSarMeta.numChirpsPerFrame = numChirpsPerFrame;
    gMmwDemoSarMeta.tcTicks           = tc;
    gMmwDemoSarMeta.lateBudgetTicks   = profileCfg->adcStartTimeConst + adcTicks;
    /* last chirp of frame f starts (N-1)*Tc after chirp 0; chirp 0 of f+1 one frame period after chirp 0 of f */
    busy = (numChirpsPerFrame > 0U) ? ((uint32_t)(numChirpsPerFrame - 1U) * tc) : 0U;
    gMmwDemoSarMeta.gap0Ticks         = (frameTicks > busy) ? (frameTicks - busy) : 0U;
    return 0;
}

/**
 *  @b Description
 *  @n
 *      Starts a run: clears the slots (magic 0 = invalid, so nothing from the
 *      previous run can validate), counters, saturation result and statistics,
 *      and latches the CQ2 address when the saturation monitor is on. Called
 *      after ADCBUF and its CQ are configured, before MMWave_start.
 */
void MmwDemo_sarMetaRunStart(ADCBuf_Handle adcBufHandle, uint8_t satMonEnabled, uint16_t satNumSlices)
{
    MmwDemo_SarMeta *s = &gMmwDemoSarMeta;
    uint32_t        cq2Addr = 0U;
    int32_t         errCode;
    UInt            key;

    if ((satMonEnabled != 0U) && (adcBufHandle != NULL))
    {
        cq2Addr = ADCBUF_MMWave_getCQBufAddr(adcBufHandle, ADCBufMMWave_CQType_CQ2, &errCode);
        if (errCode != 0)
        {
            cq2Addr = 0U;
        }
    }

    /* CBUFF error interrupt counter: per run (the chirp/frame-start error bits
     * in the CBUFF status register are sticky since boot) */
    if (gMmwMssMCB.lvdsStream.cbuffHandle != NULL)
    {
        CBUFF_control(gMmwMssMCB.lvdsStream.cbuffHandle, CBUFF_Command_CLEAR_CBUFF_STATS, NULL, 0U, &errCode);
    }

    key = Hwi_disable();
    memset((void *)&gMmwDemoSarChirpMetaSlots[0], 0, sizeof(gMmwDemoSarChirpMetaSlots));
    memset((void *)&s->stats, 0, sizeof(s->stats));
    s->cq2Addr       = (volatile uint32_t *)cq2Addr;
    s->cq2MaxSlices  = (satNumSlices > MMWDEMO_SAR_CQ2_MAX_SLICES) ? MMWDEMO_SAR_CQ2_MAX_SLICES : satNumSlices;
    s->runFlags      = (cq2Addr != 0U) ? MMWDEMO_SAR_FLAG_SAT_MON : 0U;
    s->runIdx++;
    s->frameIdx      = 0U;
    s->chirpInFrame  = 0U;
    s->framesStarted = 0U;
    s->availCount    = 0U;
    s->satWord       = 0U;
    s->pendingFlags  = 0U;
    s->lastChirpTs   = 0U;
    Hwi_restore(key);
}

/**
 *  @b Description
 *  @n
 *      CLI "sarStats": prints a consistent snapshot of the counters. Works
 *      while the sensor runs.
 */
void MmwDemo_sarMetaPrintStats(void)
{
    MmwDemo_SarMetaStats    st;
    CBUFF_Stats             cbuffStats;
    uint64_t                ts;
    uint16_t                runIdx;
    uint16_t                runFlags;
    int32_t                 errCode;
    UInt                    key;

    key      = Hwi_disable();
    st       = gMmwDemoSarMeta.stats;
    runIdx   = gMmwDemoSarMeta.runIdx;
    runFlags = gMmwDemoSarMeta.runFlags;
    ts       = MmwDemo_sarTsNow(&gMmwDemoSarMeta);
    Hwi_restore(key);

    memset((void *)&cbuffStats, 0, sizeof(cbuffStats));
    if (gMmwMssMCB.lvdsStream.cbuffHandle != NULL)
    {
        CBUFF_control(gMmwMssMCB.lvdsStream.cbuffHandle, CBUFF_Command_GET_CBUFF_STATS,
                      (void *)&cbuffStats, sizeof(cbuffStats), &errCode);
    }

    CLI_write("run %d (sensor state %d), dataFmt %d, satMon %d\n", runIdx, gMmwMssMCB.sensorState,
              gMmwMssMCB.subFrameCfg[0].lvdsStreamCfg.dataFmt,
              ((runFlags & MMWDEMO_SAR_FLAG_SAT_MON) != 0U) ? 1 : 0);
    CLI_write("chirps %u frames %u chirpStartIsr %u chirpAvail %u\n",
              st.chirps, st.frames, st.chirpStartIsr, st.chirpAvail);
    CLI_write("saturatedChirps %u\n", st.satChirps);
    CLI_write("lateIsr %u missedChirpIsr %u frameResync %u availResync %u\n",
              st.lateIsr, st.missedChirpIsr, st.frameResync, st.availResync);
    CLI_write("cbuffErrIrq %u cbuffChirpErr %d cbuffFrameStartErr %d (error bits sticky since boot), lvdsFramesDone %u\n",
              cbuffStats.numErrorInterrupts, cbuffStats.chirpError, cbuffStats.frameStartError,
              gMmwMssMCB.lvdsStream.hwFrameDoneCount);
    CLI_write("tsTicks 0x%08x%08x (%u Hz)\n", (uint32_t)(ts >> 32), (uint32_t)ts, gMmwDemoSarMeta.tsHz);
}
