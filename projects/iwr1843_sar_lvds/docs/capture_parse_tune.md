# Capture, parse, tune (host tools)

Host tools in `tools/` (Python; run from `firmware_dev/`; guide: [`tuning_guide.md`](tuning_guide.md)). `sar_parse.py`
enforces the guide's capture requirement with checks 1-4 ([`lvds_data_format.md`](lvds_data_format.md) section 1) and
refuses to write aligned output for a recording that fails any of them (`--force` writes it for debugging only).

| Tool | Does | Hardware |
|---|---|---|
| `tools/dca_capture.py` | configure and arm the DCA1000, drive `sensorStart` / `sensorStop` / `sarStats` on the CLI port, record datagrams with their UDP headers, store `chirpAvail` beside the recording | DCA1000, radar CLI port |
| `tools/sar_parse.py` | place packets by DCA1000 byte count, run checks 1-4, write `_adc.bin` (int16 I/Q, chirp-major) and `_meta.csv` (saturation lag applied) | none |
| `tools/sar_tune_report.py` | peak dBFS, clipped chirps, noise floor, range profile with the HPF response, reflector SNR; needs the `tools` uv group | none |
| `tools/sar_tune_sweep.py` | per gain / HPF point: cfg check, send cfg, capture, parse, report, one table; no power cycle | DCA1000, radar CLI port |
| `tools/sar_synth.py` | synthetic captures (clean, late, mid-packet, lost datagram, ...) for the tests and for trying the tools | none |

```bash
uv sync --group tools                     # numpy + matplotlib, only for the report and sweep (uv: see ../../README.md prerequisites)
./fw test iwr1843_sar_lvds                # host tests + cfg checks (run from firmware_dev/)
```

Capture and parse have run on the bench (the bench check in [`bench_check.md`](bench_check.md) ends with one capture
that `sar_parse.py` accepts). The tuning tools (report, sweep) are tested on synthetic captures only: tuning on a
real reflector scene is the pending Set B work (see README "Known limits").
`dca_capture.py` and the sweep default to the factory DCA1000 address 192.168.33.180 / host .30 and ports 4096 /
4098 (this repo's driver configs use others: pass `--fpga-ip --host-ip --cmd-port --data-port`), and `--timer-s`
(default 30) is the CONFIG_FPGA_GEN timer byte: check its meaning before captures longer than 30 s.
