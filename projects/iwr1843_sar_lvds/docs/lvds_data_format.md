# LVDS data format: `lvdsStreamCfg` dataFmt 2 (ADC + per-chirp metadata)

The contract between this firmware and any host loader; enough to write one from this file alone. `lvdsStreamCfg -1 <hdr> 2 0` (`<hdr>` 1 = HSI header on, 0
= off) makes every chirp leave the device as one packet: optional header, the chirp's ADC samples, and two 32-byte metadata record slots. Cites are
`file:line`; `meta.h`, `meta.c`, `lvds.c`, `main.c` = `src/mss/mmw_sar_meta.h`, `mmw_sar_meta.c`, `mmw_lvds_stream.c`, `mss_main.c`; `parse.py`, `common.py`
= `tools/sar_parse.py`, `sar_common.py` (the reference loader); `SarMeta.*`, `Cube.cpp` = `CPSL_TI_Radar_cpp/src/DCA1000/SarMeta.{hpp,cpp}`,
`ADCCubeConverter.cpp` (the driver's loader); `[n]` = **Sources**. Where the sources differ: §5.

## 1. Packet layout

> **Capture requirement.** Record one run per recording: arm the DCA1000 before `sensorStart`, stop it only after `sensorStop`, keep the DCA1000 UDP
> headers, and read `sarStats` (CLI; per-run counters, reset by `sensorStart`) after `sensorStop`. Why: packet k's record is found by its position k,
> because its other slot holds chirp k−1 or k+1; a recording that misses exactly the run's first packet can still validate on every packet, one chirp off.
> **Check every recording; if any check fails, it cannot be proven aligned: discard it and capture again.** `parse.py` is the reference implementation
> (`SarMeta.cpp` runs none of them). (1) Byte counts are consistent: no two datagrams overlap and the count never restarts at 0 (two recordings joined);
> lost or reordered datagrams are fine (`parse.py:149-163`). The DCA1000 restarts its counts when recording starts (`FrameAssembler.cpp:78-86`), so this
> check cannot see late arming; checks 2-4 can. (2) At least 99 % of the packets whose record bytes are all present validate (§2), and every slot k mod 2
> that starts with `SARM` carries the run's `runIdx` (a second run fails here). A recording that starts mid-packet or ≥ 2 packets late validates none
> (`parse.py:165-193`). (3) In the last packet k, the *other* slot does not hold a `SARM` record with the run's `runIdx` and `globalChirpIdx` = k + 1: no
> chirp follows the run's last one, so a recording that starts one packet late fails here (`parse.py:195-212`). (4) Bytes recorded (highest byte count +
> that datagram's length) = `chirpAvail` × B (`chirpAvail` = packets sent in the run, from `sarStats`; `parse.py:214-233`). Bench-measured (2026-10-06 bench
> run): check 4 passed on every capture incl. the 600 s soak (one-datagram tail hole tolerated); check 3 alone rejected the one-packet-late cut capture, but
> is not evaluable when the final datagram is lost (tolerated, then only check 4 covers the tail); require both; without a `sarStats` reading, check 4
> fails.

```
| HSI header (optional) | ADC samples, RX by RX, 4 B/sample | record slot 0 | record slot 1 |
0                       H                                   M               M+32            B = M+64
```

| Symbol | Meaning | Taken from the cfg | Cite |
|---|---|---|---|
| R | enabled RX channels | bits in `channelCfg` rxMask | `main.c:1124` |
| Ns | samples per RX | `profileCfg` numAdcSamples | `lvds.c:415` |
| H | header bytes | 0 with `lvdsStreamCfg` hdr 0; else 64 if R·Ns is a multiple of 4, else 56 | `common.py:81` |
| M | end of ADC block | M = H + 4·R·Ns | `common.py:81` |
| B | packet size | **B = M + 64**, no other padding | `meta.h:83`; `common.py:82` |
| Nc | chirps per frame | chirp indices × loops (`frameCfg`); also in every valid record | `lvds.c:416`; `meta.h:58` |
| Tc, Tb | chirp period, frame blank | Tc = idle + rampEnd; Tb = framePeriodicity − Nc·Tc | `meta.c:500-520` |
| swap | I/Q order | `adcbufCfg` 2nd argument (SampleSwap) | `mmw_cli.c:412` |

Packets run back to back, with no per-frame packet (the stock TI demo adds a point-cloud packet, a SW session, which this firmware lacks); R, Ns and swap
are not in the stream. Example, R = 1, Ns = 3300: B = 13 264 (header off), 13 328 (on); Ns = 3302 gives H = 56 and B = 13 328 (measured). With `dataFmt 4`
the packet also carries the CQ blocks: B = 13 616 with both CQ monitors on, 13 392 with the saturation monitor only (measured); `dataFmt 4` needs at least
one CQ block and has no metadata record.

**HSI header (H bytes).** 16 B data-card header + 36 B SDK header (`dataFmt` 6 = ADC_USER) + `0x0F` padding to a 16 B multiple, little-endian [1]; a loader
skips it. Its id, `DC 0A DA 0C DC 0A DA 0C` after step 1 below, is a sanity marker.

**ADC block.** R blocks, one per enabled RX channel in ascending RX order (assumed by `parse.py:355-358`, `Cube.cpp:223-253`; the shipped cfg has R = 1, so
>1 RX is untested on the bench), each Ns complex samples, each sample two int16 (two's complement) = 4 B. In device memory the first int16 is I when swap =
0 and Q when swap = 1 (SDK `demo/utils/mmwdemo_adcconfig.h:84-86`); the example cfg has swap 1 (Q first). Swap the pair to get (I, Q) (`parse.py:357-358`).
The firmware rejects dataFmt 2 unless the ADC output is complex (`adcbufCfg` fmt 0), ChanInterleave is 1, and R·Ns is even, so M, B and every packet start
are multiples of 8 B (an RX block need not be) (`main.c:1127-1140`).

**Byte order.** The device is little-endian. The DCA1000 delivers each 8 bytes sent as 16-bit units `u0 u1 u2 u3` in the order **`u0 u2 u1 u3`** [2]
(confirmed for ADC data, header and record: on a 60 s capture the HSI id reads `DC 0A DC 0A DA 0C DA 0C` raw and `DC 0A DA 0C DC 0A DA 0C` after step 1 at
30 090 of 30 090 packets, and `SARM` validates at 100 % of slots). Parse in two steps: (1) over the **whole recording**, swap bytes 2-3 with bytes 4-5 in
every 8-byte group (`parse.py:49-54`, `SarMeta.cpp:74-80`), i.e. bytes b0..b7 become b0 b1 b4 b5 b2 b3 b6 b7; (2) read the result as little-endian device
memory. All offsets below are after step 1.

**DCA1000 datagrams.** Each UDP datagram = 10 B header + payload: u32 sequence number, then u48 count of data bytes sent before it, both little-endian
(`common.py:56-61`, `FrameAssembler.cpp:78-86`). The payload is a slice of the byte stream above: the driver's DCA1000 packet size is 1472 B including the
header (`DCA1000Handler.cpp:17`), so payloads are ≤ 1462 B and a chirp packet spans several datagrams.

## 2. Metadata record and host reconstruction

**Which record is packet k's.** The recording begins at packet 0 (capture requirement, §1). Place each datagram's payload at its byte count
(`parse.py:57-93`; drop exact duplicate datagrams, same sequence number, count and bytes). Packet k starts at byte k·B. Its record is **slot `k mod 2`**, at
M + 32·(k mod 2) (`meta.c:256`; `parse.py:134`; `SarMeta.cpp:70`). The other slot holds a valid-looking record for chirp k−1 or k+1: never use it. Measured
(60 s capture, no reflector): k+1 in 100 % of in-frame packets (29 971 of 29 971) and k−1 in the last packet of each frame, which pins the chirp-start
interrupt near the idle start (k−1 would mean near the ramp start). Lost datagrams leave holes at known positions and alignment holds: a record with any
missing byte is invalid, missing ADC bytes are lost data (mark that chirp, shift nothing), and a truncated final packet is dropped.

**Validation (the one rule).** Slot k mod 2 is packet k's record only if `magic` = `"SARM"`, `version` = 1 and `globalChirpIdx` = k (mod 2³²), and `runIdx`
= the run's (`parse.py:247-248`, `SarMeta.cpp:85-91`). Otherwise discard the whole record, including its `tsTicks` and the saturation result it carried (for
chirp k − `satRefLag`, usually k−2: unknown unless another record repeats it), keep the packet's ADC data, which is good, and take its time from a valid
record j of the same run: t_k = t_j + (k−j)·Tc + (⌊k/Nc⌋ − ⌊j/Nc⌋)·Tb, in seconds, with Tc = (idle + rampEnd) µs × 10⁻⁶ (`profileCfg`) and Tb =
framePeriodicity ms × 10⁻³ − Nc·Tc (`frameCfg`) (`parse.py:259-265`). Use the nearest valid record before k, else the first one after. The run's `runIdx` is
`sarStats`' `run` when known, else that of the first record that validates by position (§5). Flags only explain failures; records that keep failing mean the
firmware lost count.

| Off | Type | Field | Meaning | Cite |
|---|---|---|---|---|
| 0 | u32 | `magic` | `0x4D524153`, bytes `"SARM"` (raw, before step 1: `"SA"` `01 00` `"RM"`) | `meta.h:33,53`; `meta.c:258` |
| 4 | u16 | `version` | 1; changes only with an incompatible layout | `meta.h:35,54`; `meta.c:259` |
| 6 | u16 | `flags` | table below | `meta.h:55`; `meta.c:282` |
| 8 | u32 | `frameIdx` | frame in the run, 0 first; wraps at 2³² | `meta.h:56`; `meta.c:260` |
| 12 | u16 | `chirpInFrame` | 0 … `numChirpsPerFrame`−1 | `meta.h:57`; `meta.c:261` |
| 14 | u16 | `numChirpsPerFrame` | chirps per frame, Nc | `meta.h:58`; `meta.c:262` |
| 16 | u32 | `globalChirpIdx` | = k: `frameIdx·numChirpsPerFrame + chirpInFrame` (mod 2³²) | `meta.h:59`; `meta.c:263` |
| 20 | u16 | `runIdx` | `sensorStart` count since boot (first run after boot = 1, wraps at 2¹⁶): new value = new run | `meta.h:60`; `meta.c:264,561` |
| 22 | u8 | `satSlices` | saturation result (§3) | `meta.h:61`; `meta.c:274` |
| 23 | u8 | `satRefLag` | which chirp `satSlices` belongs to (§3) | `meta.h:62`; `meta.c:275` |
| 24 | u64 | `tsTicks` | this chirp's start time, 10 ns ticks since boot (below) | `meta.h:63`; `meta.c:265` |

The layout is pinned by static asserts (`meta.h:66-77`, 32 B). Frame boundaries: there is no sync word, so find them by position only: frame f = packets
f·Nc … (f+1)·Nc−1, i.e. f = ⌊k/Nc⌋ and chirp in frame = k mod Nc (a valid record repeats both as `frameIdx`, `chirpInFrame`, the cross-check). Never search
the stream for the HSI id or `SARM` (`parse.py:12-13`).

| Bit | Flag (bits 5-15 are 0) | Set when | Cite |
|---|---|---|---|
| 0 | `SAT_VALID` | `satSlices`/`satRefLag` hold a result (§3) | `meta.c:273` |
| 1 | `SAT_MON` | the saturation monitor is enabled in this run | `meta.c:560` |
| 2 | `LATE` | the chirp-start interrupt came later than expected: the gap to the previous chirp's interrupt exceeds Tc (or Tc+Tb into chirp 0) by more than adcStart + Ns/fs; the write may have missed packet k | `meta.c:247-250` |
| 3 | `SKIP` | the gap exceeds the expected one by more than Tc/2: chirp-start interrupts were missed (those packets fail validation); counters realigned | `meta.c:235-240` |
| 4 | `RESYNC` | counters wrong at a frame start, reset to (frame starts seen − 1, chirp 0); flagged on the next record | `meta.c:337` |

**Timestamp.** `tsTicks` = the RTI counter 0 at the chirp-start interrupt [3]: **100 MHz (10 ns)** [4], 64-bit from boot, extended from the 32-bit counter
and never reset (`meta.c:162-176`; the firmware prints a warning at boot if the clock is not 100 MHz, `meta.c:445-448`); chirp start plus a few µs latency;
on the 600 s soak (no reflector, example cfg) the in-frame chirp spacing is 2000.00 µs (p99.9 deviation 3.03 µs) and the frame-boundary spacing 2300.51 µs
(max deviation 0.77 µs); **not** synchronized to host or platform time. Use differences; t = (tsTicks − first valid tsTicks)/10⁸ s.

## 3. Saturation: the lagged field, and how to align it

**Meaning.** The radar's RX saturation monitor divides one chirp's ADC sampling window into up to 64 equal *primary slices* (configured by `CQRxSatMonitor`,
enabled by `analogMonitor`; arguments in [5]). `satSlices` = how many primary slices (0 … 64, any R) saw at least one saturation event on the selected RX
channels combined (`meta.c:374-384`). 0 = clean; > 0 = some samples clipped. Monitor off: `SAT_MON` and `SAT_VALID` are 0.

**Why it lags.** The monitor's per-chirp report (called CQ2) for chirp n becomes valid at chirp n's *chirp-available event*, when its ADC samples are
complete; that same event starts sending packet n. Packet n's record was filled earlier, as chirp n started (`meta.c:256-282`), so it cannot hold chirp n's
result. The next chirp's record carries it.

**How to align.** In a valid record with `SAT_VALID` set: **chirp `globalChirpIdx − satRefLag` saturated in `satSlices` slices.** `satRefLag` is the
measured distance (≤ 254, else the result is dropped, `meta.c:270-281`): 2 when this chirp started before the previous chirp's report was read (99.61 % of
records on the 600 s soak, no reflector, example cfg); 1 only at the 1175 frame-start records (0.39 %); 0 when this chirp's interrupt ran after its own
sampling ended (never seen); 2 records had `SAT_VALID` = 0. Never assume 1: always subtract. A chirp's result can appear twice (same value) or never.

**Edges.**

- *Start of a run*: records before the run's first report (k = 0, sometimes k = 1) have `SAT_VALID` = 0. *End*: the last chirp's result is never delivered
  (no later record). *Restart*: new `runIdx`, k from 0; key results by run.
- *Frame boundary*: chirp 0 of frame f+1 reports frame f's last chirp (lag 1, across the blank); nothing is lost.

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

## 4. Loader recipe and drops

1. From the cfg: R, Ns, swap, hdr, Nc, Tc, Tb → H, M, B (§1). From `sarStats` after `sensorStop`: `chirpAvail`, `run`.
2. Read the datagrams, drop exact duplicates, place each payload at its byte count, mark received bytes in a mask, apply byte-order step 1 to stream and
   mask (pad to a multiple of 8). Run checks 1-4 (§1); reject the recording if any fails.
3. For k in 0 … `chirpAvail`−1: ADC block at k·B+H (4·R·Ns B); `adc_complete` = all its mask bytes present; record at k·B+M+32·(k mod 2), valid per §2 (all
   32 bytes present + the one rule).
4. Output per chirp: the (I, Q) int16 samples `[rx][sample]`, k, frame ⌊k/Nc⌋, chirp k mod Nc, time (record or interpolated), saturation from the alignment
   map (§3), flags.

**Drops.** Datagram loss never moves a packet (positions come from byte counts). Lost ADC bytes: keep the chirp, zero-fill, mark it incomplete
(`parse.py:269`, `Cube.cpp` `adc_complete`); a wholly absent packet stays a gap at k (`parse.py:270`); lost record bytes: record invalid, interpolate the
time, saturation unknown. Firmware-side loss shows as `SKIP` / `LATE` / `RESYNC` and in `sarStats` (`lateIsr`, `missedChirpIsr`, `frameResync`,
`availResync`, `cbuffErrIrq`, `meta.c:602-613`); `chirpStartIsr` = `chirpAvail` = `chirps` means none was missed.

## 5. Where the sources differ

Source of truth: the firmware for the record and packet layout (`meta.h` static asserts, `lvds.c`), `parse.py` for the checks and the output conventions;
`SarMeta.cpp` matches `parse.py` for records (`SarMeta.hpp:7-8`). Disagreements found while writing this file (none changes the record bytes):

| What | Differences | This file follows |
|---|---|---|
| Run identity | `parse.py:136-144` uses `sarStats` `runIdx` when present, else the first valid record; `SarMeta.cpp:86-89` always the first valid | `parse.py` (equal when the capture is aligned) |
| Check 2 threshold | text says ≥ 99 %; `parse.py:177,188` allows `max(1, n//100)` failures | `parse.py` |
| Check 3 packet | `parse.py:200-202` tests the packet holding the last byte (capped at `chirpAvail`−1), not evaluable if its other slot has a hole | `parse.py` |
| Time anchor | `parse.py:259-265` nearest valid before k, else first valid after; `SarMeta.cpp:129-160` the same but within the frame only for chirps before the first valid record | `parse.py` |
| I/Q order | `parse.py` reads swap from the cfg; the driver takes `lvds.iq_order` from its config, cross-checked (`Cube.cpp:258-259`) | set both from `adcbufCfg` |

## Sources

SDK paths are relative to `mmwave_sdk_03_06_02_00-LTS/packages/ti`; firmware paths to `src/`.

1. HSI header = 16 B data-card header + 36 B SDK header + `0x0F` padding (SDK `utils/hsiheader/hsiprotocol.h:403-590`); `mss/mmw_lvds_stream.c:450` passes
   `bAlignDataCard = false`, which pads header + data to 16 B (SDK `utils/hsiheader/src/hsiheader.c:286-306`), not TI's "256 B". Fields are little-endian on
   this device (`hsiprotocol.h:60-90`). The HSI id (LE u64 `0x0CDA0ADC0CDA0ADC`) reads `DC 0A DA 0C DC 0A DA 0C` after step 1.
2. The device sends 16-bit units, MSB first per lane, which nets out to no byte swap inside a unit; lanes and `msbFirst`: `mss/mmw_lvds_stream.c:140-144`;
   the `u0 u2 u1 u3` order is the one the host driver decodes for ADC data (`Cube.cpp:183-190`, layout `two_lane_iq_pairs`; for dataFmt 2
   `Cube.cpp:223-253`), and holds for header and record (§1).
3. Chirp start, frame start and chirp available are MSS interrupts 99, 98, 123 (SDK `common/sys_common_xwr18xx_mss.h:352-374`); handlers in
   `mss/mmw_sar_meta.c`. Measured alive on the 600 s soak: `chirpStartIsr` = `chirpAvail` = `chirps` = 299 880, unchanged after `sensorStop`.
4. BIOS `ti/sysbios/timers/rti/Timer.c:376, 497, 697`: RTI counter 0 = 200 MHz / (prescale 1 + 1). The R4F cycle counter was rejected: it stops while the
   CPU sleeps (WFI) in the idle loop. Profile LSBs: SDK `control/mmwavelink/include/rl_sensor.h:650-670`.
5. `rlRxSatMonConf_t`, SDK `control/mmwavelink/include/rl_monitoring.h:1877-1982`; TI *mmWave Radar Interface Control Document* rev 2.23 §10.2-10.2.2
   (report: byte 0 = slices M, then P1 S1 P2 S2 …; secondary slices overlap the primaries and are not counted). CLI: `CQRxSatMonitor <profile> <satMonSel>
   <primarySliceDuration> <numSlices> <rxChanMask>`; enable with `analogMonitor 1 <sigImgBand>`.
