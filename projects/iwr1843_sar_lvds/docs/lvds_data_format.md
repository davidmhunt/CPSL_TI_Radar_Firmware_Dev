# LVDS data format: `lvdsStreamCfg` dataFmt 2 (ADC + per-chirp metadata)

The contract between this firmware and any host parser. `lvdsStreamCfg -1 <hdr> 2 0` (`<hdr>` 1 = HSI header on, 0 =
off) makes every chirp leave the device as one packet: optional header, the chirp's ADC samples, and two 32-byte metadata
record slots. Bracketed numbers point to **Sources**. Items marked *(bench)* are checked by the bench validation (README,
Status).

## 1. Packet layout

> **Capture requirement.** Record one run per recording: arm the DCA1000 before `sensorStart`, stop it only after
> `sensorStop`, keep the DCA1000 UDP headers, and read `sarStats` (CLI; per-run counters, reset by `sensorStart`)
> after `sensorStop`. Why: packet k's record is found by its position k, because its other slot holds chirp k−1 or
> k+1; a recording that misses exactly the run's first packet can still validate on every packet, one chirp off.
> **Check every recording; if any check fails, it cannot be proven aligned: discard it and capture again.**
> (1) Byte counts are consistent: no two datagrams overlap and the count never restarts at 0 (two recordings joined);
> lost or reordered datagrams are fine. The DCA1000 restarts its counts when recording starts [7], so this check
> cannot see late arming; checks 2-4 can. (2) At least 99 % of the packets whose record bytes are all present
> validate (§2), and every slot k mod 2 that starts with `SARM` carries the run's `runIdx` (a second run fails here).
> A recording that starts mid-packet or ≥ 2 packets late validates none. (3) In the last complete packet k, the
> *other* slot does not hold a `SARM` record with the run's `runIdx` and `globalChirpIdx` = k + 1: no chirp follows
> the run's last one, so a recording that starts one packet late fails here. (4) Bytes recorded (highest byte count
> + that datagram's length) = `chirpAvail` × B (`chirpAvail` = packets sent in the run). Checks 3 and 4 are
> *(bench)*: until confirmed, require both; a recording without a `sarStats` reading is unverified.

```
| HSI header (optional) | ADC samples, RX by RX, 4 B/sample | record slot 0 | record slot 1 |
0                       H                                   M               M+32            B = M+64
```

R = enabled RX channels, Ns = `numAdcSamples`, M = H + 4·R·Ns. Packets run back to back, with no per-frame packet [8]. R, Ns and SampleSwap are not in the stream: take them from the cfg used for the run. Nc is in every valid record (`numChirpsPerFrame`).
H = 0 with the header off; with it on, H = 64 if R·Ns is a multiple of 4, else 56 [1]. Packet size **`B = M + 64`**,
no other padding. Example, R = 1, Ns = 3300: 13 264 B (header off), 13 328 B (on).

**ADC block.** R blocks, one per enabled RX channel in ascending RX order, each Ns complex samples. A sample is two
int16 (two's complement); their order in device memory follows `adcbufCfg` SampleSwap: 0 = I then Q, 1 = Q then I [5].
The firmware rejects dataFmt 2 unless `adcbufCfg` has complex output (AdcOutputFmt 0) and ChanInterleave 1, and R·Ns is
even, so M, B and every packet start are multiples of 8 B (an RX block need not be).

**Byte order.** The device is little-endian. The DCA1000 delivers each 8 bytes sent as 16-bit units `u0 u1 u2 u3` in
the order **`u0 u2 u1 u3`** [2]. Parse in two steps: (1) over the **whole packet**,
swap bytes 2-3 with bytes 4-5 in every 8-byte group; (2) read the result as little-endian device memory. All offsets
below are after step 1 (HSI id example in [1]).

## 2. Metadata record and host reconstruction

**Which record is packet k's.** The recording begins at packet 0 (capture requirement, §1). Place each datagram's
payload at its byte count from the DCA1000 UDP header (10 B, little-endian: u32 sequence number, u48 count of data bytes
sent before it) [7]. Packet k starts at byte k·B. Its record is **slot `k mod 2`**, at M + 32·(k mod 2). The other slot holds a valid-looking record for chirp k−1 or k+1: never use it. Lost datagrams leave holes at known positions and alignment holds: a
record with any missing byte is invalid, missing ADC bytes are lost data (mark that chirp, shift nothing), and a
truncated final packet is dropped. Recordings without the DCA1000 UDP headers are unsupported unless the recorder zero-fills
lost datagrams at their positions.

**Validation (the one rule).** Slot k mod 2 is packet k's record only if `magic` = `"SARM"`, `version` = 1 and
`globalChirpIdx` = k (mod 2³²), and `runIdx` = the run's (that of the first valid record). Otherwise discard the whole record, including its `tsTicks` and the saturation result it
carried (for chirp k − `satRefLag`, usually k−1: unknown unless another record repeats it), keep the packet's ADC data,
which is good, and take its time from a valid record j of the same run: t_k = t_j + (k−j)·Tc + (⌊k/Nc⌋ − ⌊j/Nc⌋)·Tb,
in seconds, with Tc = (idle + rampEnd) µs × 10⁻⁶ (`profileCfg`) and Tb = framePeriodicity ms × 10⁻³ − Nc·Tc (`frameCfg`). Flags only explain failures; records that keep
failing mean the firmware lost count.

| Off | Type | Field | Meaning |
|---|---|---|---|
| 0 | u32 | `magic` | `0x4D524153`, bytes `"SARM"` (raw, before step 1: `"SA"` `01 00` `"RM"`) |
| 4 | u16 | `version` | 1; changes only with an incompatible layout |
| 6 | u16 | `flags` | table below |
| 8 | u32 | `frameIdx` | frame in the run, 0 first; wraps at 2³² |
| 12 | u16 | `chirpInFrame` | 0 … `numChirpsPerFrame`−1 |
| 14 | u16 | `numChirpsPerFrame` | chirps per frame, Nc (≤ 8160) |
| 16 | u32 | `globalChirpIdx` | = k: `frameIdx·numChirpsPerFrame + chirpInFrame` (mod 2³²) |
| 20 | u16 | `runIdx` | `sensorStart` count since boot (wraps at 2¹⁶): new value = new run |
| 22 | u8 | `satSlices` | saturation result (§3) |
| 23 | u8 | `satRefLag` | which chirp `satSlices` belongs to (§3) |
| 24 | u64 | `tsTicks` | this chirp's start time, 10 ns ticks since boot (below) |

| Bit | Flag | Set when |
|---|---|---|
| 0 | `SAT_VALID` | `satSlices`/`satRefLag` hold a result (§3) |
| 1 | `SAT_MON` | the saturation monitor is enabled in this run |
| 2 | `LATE` | interrupt > `adcStart + Ns/fs` late: the write may have missed packet k |
| 3 | `SKIP` | chirp-start interrupts missed (those packets fail validation); counters realigned |
| 4 | `RESYNC` | counters wrong at a frame start, reset to (frame starts seen − 1, chirp 0) |
| 5-15 | | 0 |

**Timestamp.** `tsTicks` = the RTI counter at the chirp-start interrupt [3]: **100 MHz (10 ns)** [4], 64-bit from boot,
never reset or wrapped; chirp start plus a few µs latency *(bench: jitter)*; **not** synchronized to host or platform time.

## 3. Saturation: the lagged field, and how to align it

**Meaning.** The radar's RX saturation monitor divides one chirp's ADC sampling window into up to 64 equal *primary
slices* (configured by `CQRxSatMonitor`, enabled by `analogMonitor`; arguments in [6]). `satSlices` = how many primary slices (0 … 64, any R) saw at least one saturation
event on the selected RX channels combined. 0 = clean; > 0 = some samples clipped. Monitor off:
`SAT_MON` and `SAT_VALID` are 0.

**Why it lags.** The monitor's per-chirp report (called CQ2) for chirp n becomes valid at chirp n's *chirp-available
event*, when its ADC samples are complete; that same event starts sending packet n. Packet n's record was filled
earlier, as chirp n started, so it cannot hold chirp n's result. The next chirp's record carries it.

**How to align.** In a valid record with `SAT_VALID` set: **chirp `globalChirpIdx − satRefLag` saturated in `satSlices`
slices.** `satRefLag` is the measured distance: normally 1; 2 when this chirp started before the previous chirp's report
was read; 0 when this chirp's interrupt ran after its own sampling ended. Never
assume 1: always subtract. A chirp's result can appear twice (same value) or never.

**Edges.**

- *Start of a run*: records before the run's first report (k = 0, sometimes k = 1) have `SAT_VALID` = 0.
- *Frame boundary*: chirp 0 of frame f+1 reports frame f's last chirp (lag 1, across the blank); nothing is lost.
- *End of a run*: the last chirp's result is never delivered (no later record; other slots are never used).
- *Restart*: a new run (new `runIdx`, cleared result, k from 0); key results by run.

**Worked example** (3 chirps/frame, monitor on, run stopped after packet 6):

| k | frame,chirp | `SAT_VALID` | `satRefLag` | `satSlices` | Conclusion |
|---|---|---|---|---|---|
| 0 | 0, 0 | 0 | 0 | 0 | nothing yet |
| 1 | 0, 1 | 1 | 1 | 0 | chirp 0 clean |
| 2 | 0, 2 | 1 | 1 | 5 | chirp 1 saturated, 5 slices |
| 3 | 1, 0 | 1 | 1 | 0 | chirp 2 (last of frame 0) clean |
| 4 | 1, 1 | 1 | 1 | 2 | chirp 3 saturated |
| 5 | 1, 2 | 1 | 2 | 2 | chirp 4's report not read yet: chirp 3 again |
| 6 | 2, 0 | 1 | 1 | 0 | chirp 5 clean; 4 never reported; 6 unsent |

```python
sat = {}                                 # (runIdx, chirp) -> saturated slices; absent = unknown
for rec in valid_records:                # slot k%2 of packet k, validated
    if rec.flags & 0x1:                  # SAT_VALID
        sat[(rec.runIdx, rec.globalChirpIdx - rec.satRefLag)] = rec.satSlices
```

## Sources

SDK paths are relative to `mmwave_sdk_03_06_02_00-LTS/packages/ti`; firmware paths to `src/`.

1. HSI header = 16 B data-card header + 36 B SDK header (`dataFmt` 6 = ADC_USER) + `0x0F` padding (SDK
   `utils/hsiheader/hsiprotocol.h:403-590`); `mss/mmw_lvds_stream.c:450` passes `bAlignDataCard = false`, which pads
   header + data to 16 B (SDK `utils/hsiheader/src/hsiheader.c:286-306`), not TI's "256 B". HSI fields are little-endian
   on this device (`hsiprotocol.h:60-90`). Example: the HSI id (LE u64 `0x0CDA0ADC0CDA0ADC`) reads
   `DC 0A DA 0C DC 0A DA 0C` after step 1, `DC 0A DC 0A DA 0C DA 0C` raw.
2. The device sends 16-bit units, MSB first per lane, which nets out to no byte swap inside a unit; the `u0 u2 u1 u3`
   order is the one the host driver already decodes for ADC data, `CPSL_TI_Radar_cpp/src/DCA1000/ADCCubeConverter.cpp:67-86`
   (layout `two_lane_iq_pairs`); the same for header and record is *(bench)*; firmware lanes and
   `msbFirst`: `mss/mmw_lvds_stream.c:140-144`.
3. Chirp start, frame start and chirp available are MSS interrupts 99, 98, 123 (SDK
   `common/sys_common_xwr18xx_mss.h:352-374`); handlers in `mss/mmw_sar_meta.c`. No TI xwr18xx code uses 99 *(bench)*.
4. BIOS `ti/sysbios/timers/rti/Timer.c:376, 497, 697`: RTI counter 0 = 200 MHz / (prescale 1 + 1). The R4F cycle
   counter was rejected: it stops while the CPU sleeps (WFI) in the idle loop. Profile LSBs: SDK
   `control/mmwavelink/include/rl_sensor.h:650-670`.
5. SDK `demo/utils/mmwdemo_adcconfig.h:84-86`: SampleSwap 0 = I in the low half-word, 1 = Q in the low half-word.
6. `rlRxSatMonConf_t`, SDK `control/mmwavelink/include/rl_monitoring.h:1877-1982`; TI *mmWave Radar Interface Control
   Document* rev 2.23 §10.2-10.2.2 (report layout: byte 0 = slices M, then P1 S1 P2 S2 …; secondary slices overlap the
   primaries and are not counted). CLI: `CQRxSatMonitor <profile> <satMonSel> <primarySliceDuration> <numSlices>
   <rxChanMask>`; enable with `analogMonitor 1 <sigImgBand>`.
7. `CPSL_TI_Radar_cpp/src/DCA1000/FrameAssembler.cpp:54-63` (sequence number, byte count).
8. Unlike the stock TI demo, which adds a per-frame point-cloud packet (SW session). The existing C++ driver parses only
   dataFmt 1 framing, so dataFmt 2 needs new host tools.
