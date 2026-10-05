# LVDS data format: `lvdsStreamCfg` dataFmt 2 (ADC + per-chirp metadata)

The contract between this firmware and any host parser. With `lvdsStreamCfg -1 <hdr> 2 0`, every chirp leaves the device
as one packet: an optional HSI header, the chirp's ADC samples, and a 64-byte metadata block (two record slots). Bracketed
numbers point to **Sources** at the end. **Bench status:** built, not yet run on a board; items marked *(bench)* are
checked in firmware-10.

## 1. Packet layout

One packet per chirp, back to back; there is no per-frame packet. R = enabled RX channels, Ns = `numAdcSamples`.

```
| HSI header (optional) | ADC samples, RX by RX, 4 B/sample | record slot 0 | record slot 1 |
0                       H                                   M               M+32            B = M+64
```

H = 0 with the header off; with it on, H = 64 if R·Ns is a multiple of 4, else 56 [1]. The ADC bytes are those of
dataFmt 1; M = H + 4·R·Ns.

Packet size **`B = H + 4·R·Ns + 64`**, no other padding. Example, R = 1, Ns = 3300: 13 264 B
(header off), 13 328 B (on), 88.9 µs on 2 lanes × 600 Mbps. dataFmt 2 is rejected unless the ADC output is complex and
R·Ns is even, so every block is a multiple of 8 B.

**Byte order.** The device is little-endian; CBUFF sends 16-bit units, MSB first per lane [2]. The DCA1000 delivers
each 8 bytes sent as units `u0 u1 u2 u3` in the order **`u0 u2 u1 u3`**, as the host driver already decodes for ADC data
[3] *(bench: same for header and record)*. So parse in two steps: (1) in every 8-byte group of the packet swap bytes
2-3 with 4-5; (2) read the result as little-endian device memory. All offsets below are after step 1.

## 2. Metadata record

**Which slot.** Packet k of a run (k = 0 for its first chirp) carries its record in **slot `k mod 2`**; ignore the
other slot. Chirp k's record is written at chirp k's start and read by CBUFF at the end of packet k. A single slot could
be rewritten by chirp k+1 while packet k is still being sent; slot k mod 2 is next written at chirp k+2's start, after
packet k has gone (it must finish within one chirp period, or the ADC buffer would overrun). A record is **valid** if
`magic` matches and `globalChirpIdx == k` (mod 2³²). Offsets are `_Static_assert`ed in `mss/mmw_sar_meta.h`.

| Off | Type | Field | Meaning |
|---|---|---|---|
| 0 | u32 | `magic` | `0x4D524153`, bytes `"SARM"` (raw, before step 1: `"SA"` `version` `"RM"`) |
| 4 | u16 | `version` | 1; changes only with an incompatible layout |
| 6 | u16 | `flags` | table below |
| 8 | u32 | `frameIdx` | frame in the run, 0 first; wraps at 2³² |
| 12 | u16 | `chirpInFrame` | 0 … `numChirpsPerFrame`−1 |
| 14 | u16 | `numChirpsPerFrame` | chirps per frame of this run's cfg (≤ 8160) |
| 16 | u32 | `globalChirpIdx` | = k: `frameIdx·numChirpsPerFrame + chirpInFrame` (mod 2³²) |
| 20 | u16 | `runIdx` | `sensorStart` count since boot (wraps at 2¹⁶): new value = new run |
| 22 | u8 | `satSlices` | saturation result (§3) |
| 23 | u8 | `satRefLag` | which chirp `satSlices` belongs to (§3) |
| 24 | u64 | `tsTicks` | this chirp's start time, 10 ns ticks since boot (below) |

| Bit | Flag | Set when |
|---|---|---|
| 0 | `SAT_VALID` | `satSlices`/`satRefLag` hold a result (§3) |
| 1 | `SAT_MON` | the RX saturation monitor is enabled in this run |
| 2 | `LATE` | interrupt > `adcStart + Ns/fs` later than predicted; packet k may have missed the write |
| 3 | `SKIP` | chirp-start interrupts were missed; counters re-derived from `tsTicks` (still = k) |
| 4 | `RESYNC` | counters disagreed with the frame count at the last frame start and were reset |
| 5-15 | | 0 |

**Timestamp.** `tsTicks` = the MSS RTI free-running counter `RTIFRC0`, read first thing in the chirp-start interrupt
(VIM 99) [4], which SYS/BIOS clocks at **100 MHz (10 ns ticks)**, the profile's time LSB [5]. It counts from boot
through runs, stops and WFI sleep (`sensorStart` does not reset it). Its 32 bits (wrap 42.95 s) are extended to 64 in
software at every interrupt and once a second, so it never wraps in practice. It is the interrupt time: hardware chirp
start plus a few µs latency (jitter: firmware-10). It is **not** synchronized to the host, DCA1000 or platform time.

## 3. Saturation: the lagged field, and how to align it

**Meaning.** `satSlices` = how many primary time slices (0 … 64) of one chirp's ADC window saw at least one RX ADC/IF
saturation event, summed over the RX channels selected in `CQRxSatMonitor`. 0 = that chirp is clean; > 0 = some of its
samples clipped. Source: the BSS RX saturation monitor (slice LSB 0.16 µs, ≤ 64 primary slices), enabled by
`analogMonitor 1 x` plus `CQRxSatMonitor` [6]; the firmware counts the non-zero primary-slice bytes of its per-chirp
report, CQ2 [7]. With the monitor off, `SAT_MON` and `SAT_VALID` are 0.

**Why it lags.** The hardware writes chirp n's CQ2 while chirp n is sampled. It becomes valid at chirp n's
chirp-available event and stays valid until chirp n+1's [7]. That same event starts CBUFF's transfer of packet n, whose
record was filled earlier, at chirp n's start. So record n cannot hold chirp n's result. The firmware reads CQ2 in the
chirp-available interrupt (VIM 123); the next chirp-start interrupt copies the newest result into its record. (Inline
CQ2 streaming was rejected: packet size would follow the monitor cfg, and parsers would need TI's CQ format.)

**How to align.** In a record with `SAT_VALID` set: **chirp `globalChirpIdx − satRefLag` saturated in `satSlices`
slices.** `satRefLag` is normally 1. It is 2 if the previous chirp's chirp-available interrupt had not yet run when this
chirp started (chirp-start has the higher priority), and 0 only in a `LATE` record. Never assume 1: always subtract. A
chirp's result can therefore appear twice (same value) or not at all (chirp 4 below).

**Edges.**

- *First chirp of a run* (k = 0): `SAT_VALID` = 0; no earlier result exists.
- *Frame boundary*: chirp 0 of frame f+1 reports frame f's last chirp (lag 1, across the blank); nothing is lost.
- *Last chirp of a run*: its result is never sent (no later record), but `sarStats` counts it.
- *Restart*: `sensorStart` clears the result, `runIdx` changes and k restarts at 0; nothing carries over.

**Worked example** (3 chirps/frame, monitor on so `SAT_MON` is set, run stopped after packet 6):

| k | frame, chirp | `SAT_VALID` | `satRefLag` | `satSlices` | Conclusion |
|---|---|---|---|---|---|
| 0 | 0, 0 | 0 | 0 | 0 | nothing yet |
| 1 | 0, 1 | 1 | 1 | 0 | chirp 0 clean |
| 2 | 0, 2 | 1 | 1 | 5 | chirp 1 saturated, 5 slices |
| 3 | 1, 0 | 1 | 1 | 0 | chirp 2 (last of frame 0) clean |
| 4 | 1, 1 | 1 | 1 | 2 | chirp 3 saturated |
| 5 | 1, 2 | 1 | 2 | 2 | chirp 3 again, same value |
| 6 | 2, 0 | 1 | 1 | 0 | chirp 5 clean; chirp 6 never sent |

```python
sat = {}                                 # chirp index -> saturated slices; absent = unknown
for rec in valid_records:                # rec = slot k%2 of packet k, validated (§2)
    if rec.flags & 0x1:                  # SAT_VALID
        sat[rec.globalChirpIdx - rec.satRefLag] = rec.satSlices
```

## 4. Host reconstruction

B is fixed, so the DCA1000 byte count gives k for every byte, even across lost (zero-filled) UDP packets; a new run
starts where `runIdx` changes. Resync on `"SARM"` at M + 32·(k mod 2), or on the HSI id at 0 with the header on.
Chirp time is `tsTicks / 100e6` s; at a frame boundary (`frameIdx` steps, `chirpInFrame` = 0) the step is Tc + Tb, not
Tc (`sar_feasibility.md` (c)). Lost UDP data fails validation; firmware trouble shows as `globalChirpIdx ≠ k` or
`SKIP`/`LATE`/`RESYNC` (counted by `sarStats`). Such a packet's ADC data is still good; interpolate its time.

## 5. Stock demo and core driver

Stock: per chirp ADC (dataFmt 1) or CP + ADC + CQ (4), plus a per-frame point-cloud SW-session packet. Here: no SW
session (firmware-07), dataFmt 1 and 4 unchanged, dataFmt 2 adds the record block [8]. The core C++ driver assumes stock
framing (`lvdsStreamCfg -1 0 1 0`, frames of 4·R·Ns·Nc B); dataFmt 2 needs a new parser (cross-loop; host tools: firmware-12).

## Sources

SDK paths are relative to `mmwave_sdk_03_06_02_00-LTS/packages/ti`; firmware paths to `src/`.

1. HSI header = 16 B data-card header (id `0x0CDA0ADC0CDA0ADC`) + 36 B SDK header (`dataFmt` 6 = ADC_USER,
   `userBufSize[0]` = 32 units) + `0x0F` padding (SDK `utils/hsiheader/hsiprotocol.h:403-590`).
   `mss/mmw_lvds_stream.c:441` calls `HSIHeader_createHeader(…, false, …)`, which pads to 8 CBUFF units (SDK
   `utils/hsiheader/src/hsiheader.c:286-306`); TI's "round up to 256 B" note holds only for `bAlignDataCard = true`.
2. `-me` in SDK `common/mmwave_sdk.mak:113`; `msbFirst = 1`, 2 lanes: `mss/mmw_lvds_stream.c:137-143`.
3. `CPSL_TI_Radar_cpp/src/DCA1000/ADCCubeConverter.cpp:67-86` (layout `two_lane_iq_pairs`).
4. `CHIRP_START_INT` 99, `FRAME_START_INT` 98, `CHIRP_AVAIL_IRQ` 123: SDK `common/sys_common_xwr18xx_mss.h:352-374`.
   No TI xwr18xx code uses 99, so its exact position in the chirp (idle start or ramp knee) is unverified *(bench)*; the
   two-slot design does not depend on it.
5. BIOS `ti/sysbios/timers/rti/Timer.c:376` (CPUC0 = prescale), `:497` (FRC0 = 0 at start), `:697` (freq = intFreq /
   (prescale + 1)); generated config `mmw_configPkg_mss_xwr18xx/package/cfg/mmw_mss_per4ft.c`: RTI at `0xFFFFFC00`,
   intFreq 200 MHz, prescale 1; profile LSBs SDK `control/mmwavelink/include/rl_sensor.h:650-670`. The R4F PMU cycle
   counter was rejected: it counts CPU cycles, which stop while the idle loop sleeps in WFI (`mss/mmw_mss.cfg`
   `Idle.addFunc('&MmwDemo_sleep')`), whereas the RTI keeps running (it is what wakes BIOS from WFI).
6. `rlRxSatMonConf_t`, SDK `control/mmwavelink/include/rl_monitoring.h:1877-1982`; count per slice capped at 127
   there, 255 in the ICD.
7. TI *mmWave Radar Interface Control Document* rev 2.23 (DFP `docs/`): §10.2 (CQ RAM ping-pong, refreshed every chirp),
   §10.2.2 Fig. 10.9 (CQ2, 16-bit mode: byte 0 = slices reported M, then P1 S1 P2 S2 …; secondary slices overlap the
   primaries and are not counted). CQ2 address: SDK `drivers/adcbuf/ADCBuf.h:830`.
8. TI's prebuilt xwr18xx CBUFF library omits `ADC_USER` (SDK `drivers/cbuff/platform/cbuff_xwr18xx.c:276-299`); the
   project compiles that file with `ENABLE_ALL_NON_INTERLEAVED` (`mss/mmw_mss.mak`).
