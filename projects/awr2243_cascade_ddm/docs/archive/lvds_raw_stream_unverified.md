# Raw ADC / LVDS stream notes (archived, unverified)

**Not verified against source, and it does not describe this build.** In this project's
`src/mmwave2chipCascade_dss.projectspec:148-149` `mmw_lvds_stream.c`/`.h` are `excludeFromBuild="true"`
and `LVDS_STREAM` is defined nowhere, so the shipped DDM image has no raw ADC LVDS stream
(`cascade_demo_guide.md` section 5). The CLI command `lvdsStreamCfg` named below does not exist in
`mmw_cli.c` (the only LVDS command is `numLvdsLanesCfg`, compiled only with `LVDS_STREAM`). The
DCA1000 UDP packet layout is a DCA1000 property, not firmware. The older text follows unchanged.

## 7. LVDS Raw ADC Data Streaming Output Format

When high-speed raw streaming is enabled via the `lvdsStreamCfg` command, the AM273x hardware serialization wrapper (CBUFF) streams raw ADC data over the LVDS lanes. This is captured by the DCA1000 capture card and sent over Ethernet.

### 7.1 DCA1000 UDP Packet Format
The DCA1000 streams packet payloads over UDP (default host port `4098`). Every UDP packet contains a **10-byte Header** followed by raw payload bytes (up to **1456 bytes**):

```
+---------------------------------------------------------+
|                  Sequence Number (4 B)                  |
+---------------------------------------------------------+
|                  Byte Count (4 B)                       |
+---------------------------------------------------------+
|                  Reserved / Upper Byte Count (2 B)      |
+---------------------------------------------------------+
|                  Payload Data (up to 1456 B)            |
+---------------------------------------------------------+
```

*   **Sequence Number (Bytes 0-3):** 32-bit little-endian integer, incrementing sequentially with every packet to detect packet drops.
*   **Byte Count (Bytes 4-7):** 32-bit little-endian integer representing the cumulative index of the first payload byte in this packet since the capture session started.
*   **Reserved (Bytes 8-9):** Sourced as `0x0000`.

### 7.2 Raw ADC Stream Memory Layout (8-Rx Virtual Array)
For the 2-chip Cascade EVM, the CBUFF software session is configured to stream **userBuffer0** (CSIRX0 - Master Transceiver) followed immediately by **userBuffer1** (CSIRX1 - Slave Transceiver). The CSIRX buffers store samples in a **non-interleaved** channel format.

Thus, for each chirp, the raw stream buffer received over Ethernet is laid out as follows:

```
Chirp Raw Stream Buffer:
+-------------------------------------------------------------------------------+
|                      Block 0: Master Transceiver (CSIRX0)                     |
+---------------------------------------+---------------------------------------+
|  Rx0 Samples (numAdcSamples * 4 B)    |  Rx1 Samples (numAdcSamples * 4 B)    |
+---------------------------------------+---------------------------------------+
|  Rx2 Samples (numAdcSamples * 4 B)    |  Rx3 Samples (numAdcSamples * 4 B)    |
+-------------------------------------------------------------------------------+
|                      Block 1: Slave Transceiver (CSIRX1)                      |
+---------------------------------------+---------------------------------------+
|  Rx4 Samples (numAdcSamples * 4 B)    |  Rx5 Samples (numAdcSamples * 4 B)    |
+---------------------------------------+---------------------------------------+
|  Rx6 Samples (numAdcSamples * 4 B)    |  Rx7 Samples (numAdcSamples * 4 B)    |
+-------------------------------------------------------------------------------+
```

*   **Block 0 Size:** $S = N_{\text{AdcSamples}} \cdot 4_{\text{Rx}} \cdot 4_{\text{Bytes/Sample}}$ bytes.
*   **Block 1 Size:** $S = N_{\text{AdcSamples}} \cdot 4_{\text{Rx}} \cdot 4_{\text{Bytes/Sample}}$ bytes.
*   **Total Chirp Data Size:** $2S$ bytes.

#### Complex Sample Encoding
Each individual complex sample consists of a 16-bit Real (I) and 16-bit Imaginary (Q) integer pair:
*   **Bytes 0-1:** Real (I) Component (signed 16-bit integer, `int16_t` in little-endian format)
*   **Bytes 2-3:** Imaginary (Q) Component (signed 16-bit integer, `int16_t` in little-endian format)

#### Host-Side Reconstruction Map
When parsing the reconstructed frame buffer (size = $\text{ChirpsPerFrame} \times 2S$) into a multi-dimensional array `cube[Frames][Chirps][RxAntennas (8)][AdcSamples]`:
1.  Align the payload bytes of successive UDP packets to form the complete frame buffer.
2.  For a given frame and chirp $c$:
    *   Set the base chirp offset $A_0 = c \times 2S$.
    *   **Master Antennas (Rx0 to Rx3):**
        *   Loop over antenna $a \in [0, 3]$:
            *   Start reading from offset $A_0 + a \times N_{\text{AdcSamples}} \times 4$.
            *   Extract $N_{\text{AdcSamples}}$ complex pairs and store them at `cube[f][c][a][s]`.
    *   **Slave Antennas (Rx4 to Rx7):**
        *   Set the slave block start offset $A_1 = A_0 + S$.
        *   Loop over antenna $a \in [0, 3]$ (mapping to physical receiver array indices $4$ to $7$):
            *   Start reading from offset $A_1 + a \times N_{\text{AdcSamples}} \times 4$.
            *   Extract $N_{\text{AdcSamples}}$ complex pairs and store them at `cube[f][c][a + 4][s]`.
