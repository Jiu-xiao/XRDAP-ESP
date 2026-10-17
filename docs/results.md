# RMLink posted-TAR A/B, 2026-10-16
Board: ESP32-S3 N8R8 (8 MB flash), fw 0.2.28 + local build (IDF 5.5.2), TCP packet 512 B, target STM32G431KB (DEV_ID 0x468, Cortex-M4 410fc241), core halted + IWDG/WWDG frozen via DBGMCU_APB1FZR1.
Host: lab3b Pi (hostapd XR-LAB, 10.43.0.0/24), Python TCP client, 8 pipelined 84-word scattered packets (TAR write + DRW read each), data checked against single-word reads (system-memory ROM 0x1FFF0000, stride 64 B).
| SWD clock request | baseline words/s | posted-TAR words/s | gain |
| 1 MHz  | 6024  | 8383 / 8457   | +39..40% |
| 4 MHz  | 19883 | 26145 / 26250 | +31..32% |
| 10 MHz | 36521 | 42053 / 44141 | +15..21% |
| 8 MHz  | invalid (hundreds of WAIT/mismatch packets in both builds) | | |
Zero errors over 480 packets x 84 words at 1/4/10 MHz in both builds.
Images: D:\Codex\tmp\rml\bt512 (baseline), bt2_512 (posted-TAR). Patch: D:\Codex\tmp\rml\posted-tar.patch

# XR ESP32-S3 DAP (libxr master 5161373 + S3 dedicated-GPIO SWD), Wi-Fi TCP 3241, same bench as above
Project: D:\Codex\tmp\xrdap-s3 (app/, build/). USB transport compiled out (XRDAP_ENABLE_USB=0); USB path wedges the Windows host after heavy traffic - UNRESOLVED.
| SWD clock request | XR words/s | RMLink baseline | RMLink posted-TAR |
| 1 MHz  | 2942 | 6024  | 8.4k |
| 2 MHz  | 5007 | -     | - |
| 4 MHz  | 7780 | 19883 | 26.2k |
| 8 MHz  | 9989 | invalid (errors) | invalid |
| 10 MHz | 11675 | 36521 | 42-44k |
| 12/16/20 MHz | 11475 / 11677 / 13635 | - | - |
| 24 MHz | target power-up timeout | - | - |
Zero errors 1-20 MHz. Profiling (ProfiledSwd wrapper, 10 MHz run): 162059 SWD transfers for ~40.3k words = 4.0 transfers/word (RMLink: 3, posted-TAR: 2).
Per transfer: 79.4 us @1MHz, 26.6 us @4MHz, 16.0 us @10MHz => fit t = 9 us fixed + 70 clock-equivalents/f (a 46-clock transfer should be 46). Implied real SWCLK 0.58/1.73/2.87 MHz.
Per 84-word packet at 10 MHz: ~5.2 ms in DAP processing + ~2 ms in TCP RX/TX done serially in the same task.
Dedicated GPIO instructions only work on the core that created the bundle (TCP task must be on that core).

# XR production build on libxr-c (cycle-counter edge pacing, kEdgeWriteCycles=13), Wi-Fi TCP, Pi bench
| req MHz | words/s | note |
| 1 | 4677 | analyzer attached |
| 2 | 8385 | |
| 4 | 13860 | |
| 6 | 18771 | measured SWCLK ~5.3 MHz |
| 8 | 18854 | same ~5.3 MHz (spin-loop floor) |
| 10/12/16/20 | with analyzer probes attached: read fail (ack=7); probes removed: 28340/28234/28061/27617, errors=0 | natural no-spin speed ~12.5 MHz |
Conclusion: probe loading was enough to break 12.5 MHz; without probes the bare dupont wiring works at natural speed.
Not yet tested: 33 ohm series resistors + external 10k SWDIO pull-up.

# XR production fw, USB-only build (XRDAP_WIFI_ENABLE=0), legal endpoint layout, Windows host, analyzer removed
Scattered read (84 words/cmd, TAR+DRW per word, 3 cmds in flight), words/s: 1 MHz 4792, 2 MHz 8619, 4 MHz 14479, 6 MHz 19891, 8 MHz 19945, 10 MHz 30673, 12 MHz 30826, 16 MHz 30702 (errors 0).
Block read (DAP_TransferBlock 255 words/cmd, 1 TAR+block pair in flight), KiB/s: 1 MHz 63.3, 2 MHz 102.7, 4 MHz 149.6, 6 MHz 182.9, 10 MHz 241.0, 16 MHz 235.9 (errors 0).
Findings: 1 KB responses end in a zero-length packet (client must skip empty completions). Two (TAR+block) pairs in flight (4 commands, two 1 KB responses) deadlocks the bulk pipe; device recovers with a hard reset (no replug needed). Advertised packet count 8, effective response queue min(adv,4).

# On-device DAP execution (no USB, no Wi-Fi), libxr-c + cycle-counter pacing, kEdgeWriteCycles=13, target G431 halted
| req MHz | block255 us/cmd | KiB/s | scatter84 us/cmd | words/s |
| 1  | 12450 | 80.0   | 16443 | 5108 |
| 2  | 6402  | 155.6  | 8496  | 9886 |
| 4  | 3350  | 297.3  | 4522  | 18575 |
| 6  | 2129  | 467.7  | 2932  | 28646 |
| 10 (=natural ~12.5) | 929 | 1071.2 | 1400 | 59990 |
| 16 (same) | 929 | 1071.2 | 1400 | 59990 |
End-to-end for comparison: USB block 241 KiB/s (4.1 ms/cmd), USB scatter 30.7k words/s, Wi-Fi scatter 28.3k words/s.
=> command execution is not the bottleneck; transport and single-command-in-flight are.

# USB-only build, same board/fw, client request padding OFF (requests sent at their true length instead of zero-padded to 1024 B)
Block read (255 words/cmd, 1 TAR+block pair in flight), KiB/s: 1 MHz 72.3, 2 MHz 129.0, 4 MHz 211.9, 6 MHz 287.1, 10 MHz 437.9, 16 MHz 438.8  (padded: 63.3/102.7/149.6/182.9/241.0/235.9)
Scatter (84 words/cmd, 3 in flight), words/s: 4 MHz 15772, 10 MHz 37673  (padded: 14479 / 30673)
In-flight limit: 2 (TAR+block) pairs = two 1 KB responses still deadlocks, padded or not -> the limit is on the response side.
F103 release (XRDAP-F103C8 3fba8d6, libxr b293208): DAP_Info reports packet size = min(IN,OUT MaxPacketSize) = 64, packet count 2; commands run in OnDataOutComplete with the response built directly in the IN endpoint buffer.

# Root cause of the USB pipeline stall (libxr-c 52862a09): trailing ZLP on a response == DAP packet size keeps a queue slot
State at stall: q_cnt=4 rd_act=0 wr_act=1, EP1 IN DIEPTSIZ=0x20080000 (pending ZLP), EP1 OUT not armed.
Fix: need_zlp only when len < GetDapPacketSize(). After: 4 commands in flight OK (block read 452 KiB/s at 12.5 MHz); 6 in flight still stalls (device reports PACKET_COUNT=4).

# Real tools through the XR probe over USB (libxr-c a1d233ec: command ring, ZLP rule, ExecuteExternal), target G431
OpenOCD v0.12.0-esp32-20250707 (cmsis-dap usb_bulk, packet size 1024, count 4): attach OK (DPIDR 0x2ba01477, Cortex-M4 r0p1, 6 BP / 4 WP), halt, mdw, reg pc OK.
dump_image 24576 B from 0x1FFF0000, KiB/s: 2 MHz 148.5, 4 MHz 268.0, 6 MHz 392.9, 8 MHz 394.4, 10 MHz 583.7, 12 MHz 582.5, 16 MHz 587.8.
probe-rs 0.31.0 benchmark (RAM at 0x20005000, 3 iterations), bytes/s read/write: 2048 B @3.2 MHz 187k/182k, @6.4 MHz 287k/267k, @9.6 MHz 413k/397k; 32 KiB test fails only because SRAM2 is too small there.
Own Python client (sync WinUSB), new HEAD: block read 10 MHz 459 KiB/s (NPAIR 1) / 456 (2); padded 241; scatter 84 words 4 MHz 17.3k, 10 MHz 48.6k words/s.
Device internal (no transport) block read at 12.5 MHz: 1071 KiB/s.
Note: probe-rs benchmark overwrote target SRAM2 at 0x20005000.. with test data.

# OpenOCD flash programming through the XR probe: root causes of the intermittent failures (2026-10-16, ESP32-S3 + G431, USB-only build, DAP packet size 64)
Test: `program <image> 0x08000000 verify`, images alternated (patched / original), OpenOCD v0.12.0-esp32-20250707, bare dupont wires, target runs an IWDG.
1. DAP packet size 1024 on full-speed USB: a host command whose length is an exact multiple of 64 (and < 1024) never ends (OpenOCD's bulk write sends no ZLP, the device receives a 1024-byte packet and waits for a short packet). Evidence: device state dump, EP1 OUT DOEPTSIZ. Host times out after ~6 s; OpenOCD recovery then reads a stale response ("Sent 0x12 received 0x5"). Workaround: packet size 64. Fixed in libxr-c 03d2026e (other session; host tests only, not yet run on the board with packet size 1024).
2. Target IWDG: OpenOCD does not freeze it. Packet size 64, adapter speed sweep, 8 runs each:

| adapter kHz | IWDG not frozen | IWDG frozen (`mmw 0xE0042008 0x1800 0` after halt) |
|---|---|---|
| 2000 | 1/8 | 6/8 |
| 4000 | 1/8 | 8/8 |
| 6000 | 8/8 | – |
| 8000 | 8/8 | – |
| 12000 | – | 8/8 |
3. Remaining 2 MHz failures (IWDG frozen) are single SWD ack errors in the middle of a RAM block write (`SWD ack not OK @ N JUNK`, N = 0/1/2/4/7/13; then OpenOCD reconnects, DPIDR ok, `Failed to write memory`). Target is not reset (core halts again after reconnect).
   - 2 MHz, logic analyzer probes attached: 18/18 passed. Probes removed: 14/16 passed (failed runs 7, 16). Probe loading hides the failure (same effect as the 12.5 MHz ack=7 case above).
   - Drive strength of SWCLK/SWDIO (`gpio_set_drive_capability`, ESP32-S3 levels 0..3 ~ 5/10/20/40 mA, IDF default 2), 2 MHz, 32 runs: level 0 -> 32/32.
   - Stand-alone SRAM stress firmware (`XRDAP_SCOPE_TEST`, 12-word posted block write + read-back, 2 MHz, errors/loops): level 2 (default) 0/19665, level 0 0/19665, level 1 0/19665, level 3 95/17282 (0.55 %). Only the strongest level fails in this load; the default level fails only in the OpenOCD flash flow (target running its flash loader).
   - Oscilloscope (Hantek DSO2C10, 1 V/div, SWDIO only, 2-4 us/div): damped ringing after every edge (~100-300 ns, tens of MHz), over/undershoot of the order of 0.5-1 V beyond the rails, small crosstalk ripple on flat parts, slow ~400 ns ramp in the turnaround cycle (only the internal pull-up drives the line). Photos were not labeled by drive level, SWCLK was not captured; the over/undershoot per level was not measured.
   - Chosen: level 1 (one step below the IDF default) as the app default (`XRDAP_DRIVE_CAP` = 1 in `s3_dedic_gpio.hpp`). OpenOCD program+verify with level 1: 2 MHz 32/32, 4 MHz 16/16, 12 MHz 16/16.
   - Not tried: 33 ohm series resistors, external SWDIO pull-up, SWCLK waveform, level 1 on the Wi-Fi (TCP) build, packet size 1024 build with 03d2026e.
4. libxr branch `origin/fix/swd-write-trailing-idle-clocks` (e816f439, "trailing idle clocks after write data phase", F302CB target) is not in master, dev, or the integration branches. Applied locally on top of 03d2026e (default drive level 2, 2 MHz, 32 runs): 20/32 passed with the same `ack not OK @ N JUNK` signature, so it does not address this failure; it was reverted from the libxr-c worktree (patch kept as `libxr-patches/0004-swd-trailing-idle-clocks.patch`). The failure rate was not lower than without it (2/16 and 2/16 earlier at 2 MHz, no analyzer); whether it makes things worse was not isolated (different libxr-c HEAD, different day).

# Other-session fixes in libxr-c 03d2026e (reported by the other session, host tests only: Debug, ASan/UBSan, TSan)
OUT command termination by command length (zero-padded transfers: an all-zero full packet after a complete command is dropped); TransferAbort sends no response and clears the abort flag at transfer start; DAP_ExecuteCommands (0x7F) now runs the commands carried in its own packet after the queued ones (it ignored them before, which is why my inline 0x7F test got no usable response). Not changed on purpose: DAP_Delay rounds up to 1 ms (spec section 10.3-a), software SWD clock plateau ~2.78 MHz comes from ~86 cycles fixed cost per bit, `loops_per_us = 0` selects the fastest path.

# Board retest of the remaining libxr items (libxr-c 03d2026e, USB-only builds, drive level 1)
OpenOCD program+verify with DAP packet size 1024: 2 MHz 16/16, 4 MHz 16/16, 12 MHz 32/32 (IWDG frozen). The earlier 1024-byte stall (command length multiple of 64) is gone.
Padded OUT requests, 4 commands in flight (xrblock_usb.py NPAIR=2 PAD=1, 10 MHz): a1d233ec 252 KiB/s x2 OK; 03d2026e hangs 3/3 (device stops answering, WinError 121, hard reset recovers). NPAIR=1 padded 218 KiB/s; NPAIR=2 unpadded 452.9 KiB/s.
Single-class (DAP only) USB device, PID 0x4A02: Windows problem code 28, WinUSB not bound. iProduct read via WinUSB = "CMSIS-DAP v2".
Sampling point trial (stress firmware, 90 s, level 1, no analyzer): 16 MHz request 0/686100 errors, 12 MHz request 0/686080 errors (both ~natural speed, the loop counts match).

# SWD execution context and the USB ceiling (libxr-c 03d2026e, USB-only, DAP packet 1024, drive level 1, unpadded, 4 commands in flight)
1. Where SWD runs: with the stock owner model 155109 of 155109 SWD transfers executed in interrupt context (xPortInIsrContext), core 0 (EndpointPool::RequestService -> service_.Invoke runs the class service in the calling context, i.e. the USB ISR).
2. USB-only ceiling, no SWD (xrusb_ceiling2.py: DAP_ExecuteCommands 0x7F carrying 255 x DAP_Info, 1021-byte response): depth 1 385.8 KiB/s (2.58 ms/response), depth 2 448.2, depth 3 446.9, depth 4 442.2 KiB/s (2.2 ms/response). Padded requests: 338 KiB/s at depth 1-3, depth 4 hangs the device (same regression as with SWD, so it does not need SWD).
   => CORRECTED below (item 5): this 448 KiB/s is the limit of my Python client (one IN read in flight), not of the device. Connecting through a USB dock vs directly made no difference (441.8 KiB/s depth 2 direct).
3. Executor-task prototype (libxr copy `libxr-proto`, patch `libxr-patches/0005-dap-executor-task-prototype.patch`: command execution in a FreeRTOS task pinned to core 0, owner only finishes the response; 0 of 404401 transfers in ISR). Same-session comparison, KiB/s or words/s:
   | | baseline (ISR) | executor task |
   |---|---|---|
   | block 4 MHz | 212.2 | 184.6 (-13 %) |
   | block 10 MHz | 442.1 | 448.5 |
   | scatter84 4 MHz | 14826 | 15710 (+6 %) |
   | scatter84 10 MHz | 32723 | 40654 (+24 %) |
   Worker priority 22 instead of 10: block 4 MHz 185.1, block 10 MHz 406.7, scatter 4/10 MHz 15710/39237: priority is not the cause of the 4 MHz block loss. Not understood.
4. DAP_ExecuteCommands (0x7F) response of 03d2026e has no "number of commands" byte: request [7F, 3, 3 x DAP_Info(0xFF)] answers `7f 00 02 00 04 00 02 00 04 00 02 00 04` (13 bytes); the ARM definition is [7F, count, responses...] (from memory, not rechecked against the spec).
5. OpenOCD (cmsis-dap usb_bulk, packet 1024, count 4) dump_image of 128 KiB from flash, IWDG frozen, same board and cable, direct port (3 runs each, identical):
   | firmware | 4 MHz | 12 MHz |
   |---|---|---|
   | baseline 03d2026e (SWD in USB ISR) | 266 KiB/s | 578 KiB/s |
   | executor task, prio 10 | 183 | 545 |
   | executor task, prio 22 | 183 | 548 |
   On-device (no transport) 297 KiB/s at 4 MHz and 1071 KiB/s at 12.5 MHz. So with OpenOCD the baseline already overlaps SWD and USB at 4 MHz (266 of 297) and reaches 578 at 12 MHz; the naive executor task is slower (-31 % at 4 MHz, -6 % at 12 MHz) and was also slower in the Python client for 4 MHz block reads. The reason is not understood (extra hand-off latency between ISR, task and the owner finish step is the obvious suspect, not measured). Conclusion: do not move execution out of the owner for throughput.
   Python client numbers (xrblock/xrbench/xrusb_ceiling2, 440-478 KiB/s block read at 10 MHz) are client-limited and should not be quoted as device ceilings. The true full-speed ceiling of the device is >= 582 KiB/s and has not been measured.

# True USB ceiling and SWD ceiling (libxr-c e196f094, USB-only build, drive level 1, direct USB port, 2026-10-16 night)
Retest of the other session's fixes on the board (e196f094): padded OUT requests with 4 commands in flight (NPAIR=2 PAD=1) 3/3 OK (227 KiB/s), padded 0x7F depth 4 no longer hangs (330 KiB/s), padded 8 scattered reads OK; OpenOCD program+verify 4 MHz 8/8, 12 MHz 16/16 (packet 1024); single-class DapLinkV2 device (PID 0x4A02): PnP problem code 0, service WINUSB, DAP_Info answered. A2 and the padding regression are fixed.

USB: overlapped WinUSB client (`xrusb_pipe.py`, `xrblock_pipe.py`; requests and responses use overlapped I/O so an IN read is always queued):
- 0x7F x255 DAP_Info (512 B request, 1021 B response, no SWD): depth 1 405.9, depth 2 615.6, depth 3 619.1, depth 4 621.0 KiB/s (1.61 ms per command, ~15 full-speed packets per ms on the bus counting both directions). Small DAP_Info: 20036 commands/s at depth 4 (0.05 ms each).
- Real flow, TAR write + 255-word block read pairs (OpenOCD-like, 9 B and 5 B requests, 1 KB response), 4 commands in flight:
  | SWCLK request | on-device | 4 in flight | 2 in flight | 6 or 8 in flight |
  |---|---|---|---|---|
  | 4 MHz | 297 KiB/s | 289 (97 %) | 227 | 265 |
  | 10 MHz | ~1071 | 922 | 498 | 479 |
  | 12 MHz | 1071 | 920 | 498 | 479 |
  | 16 MHz (natural 12.5) | 1071 | 900 | 500 | 479 |
  => the full-speed USB path carries about 920 KiB/s of block-read data, reached with exactly the device-reported 4 commands in flight; more in flight is slower, fewer is much slower. It is not 450 (synchronous client) and not 578 (OpenOCD): both are host-limited. At >= 10 MHz the end-to-end rate is USB-bound, not SWD-bound. Beyond ~920 KiB/s needs USB high speed (not available on the S3).
SWD (hand-written dedicated-GPIO engine `fast_swd.hpp`, on-device sweep `XRDAP_FAST_BENCH`, G431 halted, bare dupont wires, 5000 transfers of each kind per point):
- Reads (DP IDCODE), DP writes and AP writes are error-free (5000/5000 each) with 3 nops per edge = ~16.5 CPU cycles per SWCLK = 14.5 MHz; block read 1254 KiB/s (libxr natural path: 12.5 MHz, 1071 KiB/s, so +17 %). 4 nops: 12.9 MHz.
- 2 nops and below (>= ~16.5 MHz): 0/5000, the target does not answer (ack reads all ones). A SWDIO release delay of 0-32 cycles does not change this, so it is not the probe's output-disable latency; target input timing or signal integrity on the bare wires (scope needed to tell).
- SWDIO output-enable needs ~16 CPU cycles to take effect (GPIO enable register is an APB write); without a 16-cycle wait after switching to output, writes fail from ~9 MHz. Reads are not affected.
- Drive level 3 (strongest): cannot even connect with this engine at any speed; level 1 and level 2 behave identically.
- Because USB limits at ~920 KiB/s, an SWD faster than ~12 MHz gives no USB throughput. It could matter for the Wi-Fi path (not measured at these rates).

# Why SWD stops at ~16.5 MHz (hand-written engine, G431 halted, bare dupont wires, 5000 transfers per point, fast_swd.hpp)
- Pad drive strength is not the cause: all 16 combinations of SWCLK x SWDIO drive level (0..3) give the same boundary (3 nops/edge = 14.5 MHz error-free, 2 nops = 0/5000). SWCLK at level 3 cannot connect at all (any SWDIO level); SWDIO at level 3 is fine; levels 0-2 are equivalent.
- Target core clock is not the cause: RCC_CFGR read back through the bench = 0xF (SWS=3, PLL 160 MHz after setting it through the debugger; reset default is HSI16 = 0x5); the boundary does not move.
- Cause: round-trip delay budget. The engine sampled each bit just before the rising edge; the target launches data after the rising edge, so launch-to-sample is one clock period minus a few ns, and target output delay + wire + ESP32 pad input path need ~55-60 ns. Sampling later removes the cliff:
  | nops/edge, sample after the rising edge by | SWCLK | result (DPIDR read / DP write / AP write, 5000 each) | block read |
  |---|---|---|---|
  | 3, before the edge | 14.5 MHz | all OK | 1242 KiB/s |
  | 2, 1 nop after | 16.5 MHz | all OK | 1415 KiB/s |
  | 1, 2 nops after | 18.1 MHz | 4999/5000 reads, writes OK | 1552 KiB/s |
  | 0, 3 nops after | 19.9 MHz | 4997/5000 reads, writes OK | 1705 KiB/s |
  Error-free up to 16.5 MHz; 18-20 MHz works with about 0.02-0.06 % failed reads on this wiring. (Every block read shows one non-OK and one wrong word: the bench does not retry the WAIT of the first read after the TAR write; the same at every speed.)
- Practical SWD ceiling on this setup: ~16.5 MHz error-free (1415 KiB/s block read, +32 % over SwdGeneralGPIO without delay at 12.5 MHz / 1071 KiB/s). Still above the ~920 KiB/s USB ceiling, so it only matters for Wi-Fi.

# Three structural sampling positions across frequency (hand-written engine, G431 on PLL 160 MHz, drive level 1, 3000 DPIDR reads per cell, reconnect before every cell)
A = sample right after the falling edge with symmetric half periods (time T/2 after the launching edge; no libxr path does this), B = just before the rising edge (CMSIS; libxr delay path, paced path and, because of its 2-3 cycle low phase, also the no-delay path), C = right after the rising edge.
| SWCLK | A | B | C |
|---|---|---|---|
| 0.7 / 1.3 / 2.3 MHz | 3000/3000 | 3000/3000 | 3000/3000 |
| 3.3 MHz | 3000 | 3000 | 2998 |
| 4.1 MHz | 3000 | 3000 | 3000 |
| 5.2 MHz | 3000 | 3000 | 2999 |
| 7.2 MHz | 3000 | 3000 | 2499 (17 % bad) |
| 7.9 MHz | 3000 | 3000 | 2999 |
| 9.2 / 10.8 MHz | 3000 | 3000 | 3000 |
| 11.9 MHz | 0 | 3000 | 2999 |
| 13.2 MHz | 0 | 3000 | 3000 |
| 14.9 MHz | 0 | 3000 | 2998 |
| 17.0 MHz | 0 | 0 | 2999 |
| >= ~19 MHz | 0 | 0 | 0 |
- A: reliable up to ~11 MHz, hard cliff at ~11.5 MHz. B: reliable up to ~15 MHz, cliff at ~16 MHz. C: the only position that works at 17 MHz, but marginal at every frequency (it samples right where the target starts changing to the next bit, error rate 0.03-17 % at low and mid frequencies).
- Model fit: data valid window = [t_max, T + t_min] after the launching rising edge; the A and B cliffs give t_max ~ 50-55 ns; C's marginality gives t_min ~ 5-10 ns. Window width = T - ~45 ns, so ~22 MHz is the absolute limit with zero margin. B keeps a constant ~15 ns margin to the next-bit change at every frequency, which is why it is the safe default up to ~15 MHz. These numbers are estimates from CPU cycles, not scope measurements.
- A failed position leaves the target out of sync: an earlier version of this sweep without a reconnect between cells wrongly reported B and C as failing at every high frequency.

# Board verification of libxr-c 52fa2e92 (commands run in an application Step() thread), app adds a Step() task on core 0, prio 10
Baseline firmware (SwdGeneralGPIO + ProfiledSwd), USB-only, packet 1024, drive level 1:
- FIRST HOST COMMAND AFTER BOOT IS LOST: after every reset the first session's first command gets no answer (python client open times out; OpenOCD init fails with `CMD_INFO failed`); waiting 30 s makes no difference; later sessions are fine from their first command. Worked around by a warm-up session. Not seen on e196f094.
- SYNC CLIENT WITH 4 REQUESTS WRITTEN BEFORE ANY READ DEADLOCKS: `NPAIR=2 xrblock_usb.py` (padded or not) and `xrusb_ceiling2.py` depth 4 time out (WinError 121), the device then stops answering until a hard reset. 2 and 3 commands in flight are fine; overlapped client (read always queued) works with 4/6/8 in flight, also with 1024-byte zero-padded requests (340 KiB/s). Worked on e196f094 (452 KiB/s unpadded, 252 padded).
- Throughput vs the ISR-execution versions (KiB/s): overlapped block read 12 MHz 4 in flight 920 -> 808; 4 MHz 289 -> 183 (4 in flight is slower than 2 in flight, 227); OpenOCD dump 4 MHz 266 -> 185, 12 MHz 578 -> 549; 0x7F 1 KB depth 4 621 -> 579; small DAP_Info depth 4 20036 -> 18955 cmd/s (~0.05 ms per command). Same pattern as my executor-task prototype: the thread hand-off costs most when SWD-bound (low clock).
- OpenOCD program+verify: 4 MHz 8/8, 12 MHz 16/16. probe-rs 9.6 MHz benchmark works: 2048 B read 463 KB/s, write 380 KB/s.
- Single-class device (PID 0x4A02): problem code 0, service WINUSB (the first command after boot is lost here too).
Hand-written SWD engine (fast_swd.hpp, sample before the rising edge, OE settle 16 cycles) inside the DAP, same libxr-c:
- OpenOCD dump: 4 MHz 207 (baseline 185), 12 MHz 533-540 (549), 14.5 MHz 569-577, 16 MHz request (= 14.9 MHz) 569-574. Overlapped block read 4 in flight: 12 MHz 756, 14.5 MHz 829, 16 MHz 831 (baseline 12 MHz 808). probe-rs 9.6 MHz read 392 KB/s (baseline 463).
- OpenOCD program+verify 12 MHz 8/8, 14.5 MHz 16/16, 16 MHz 8/8 (flashing at 14.9 MHz works).
- Gain is 0-5 % (12 % at 4 MHz); with the Step thread the hand-off dominates, not the SWD speed. Not worth integrating.

# Dual-core placement (libxr-c 52fa2e92 Step() thread): SWD pins created on core 1 and the Step thread pinned to core 1, USB interrupt stays on core 0
Compared with Step thread on core 0 (same core as the USB interrupt) and with the earlier ISR-execution version (KiB/s):
| | ISR execution (a1d233ec..03d2026e) | Step on core 0 | Step + SWD on core 1 |
|---|---|---|---|
| overlapped block read 4 MHz, 4 in flight | 289 | 183 | 291 |
| overlapped block read 12 MHz, 4 in flight | 920 | 808 | 829 |
| OpenOCD dump 4 MHz | 266 | 185 | 261 |
| OpenOCD dump 12 MHz | 578 | 549 | 637-643 |
| OpenOCD program+verify 12 MHz | 16/16 | 16/16 | 8/8 |
| probe-rs 9.6 MHz, 2048 B read/write | 413/397 | 467/385 | 460/376 |
- Moving SWD and the Step thread to the other core removes the loss at low clocks (183 -> 291) and gives OpenOCD more than the ISR version did (+11 %); the overlapped client at 12 MHz stays ~10 % below the ISR version (cross-core hand-off, ~1.20 vs 1.08 ms per block).
- Hand-written SWD engine on core 1: overlapped 4 MHz 331 (baseline 291), 12 MHz 826, 14.5 MHz 842; OpenOCD dump 4 MHz 292, 12 MHz 627-637; probe-rs 9.6 MHz 398/380; OpenOCD flash 12 MHz 8/8. Gain over the baseline: +14 % at 4 MHz, none at 12 MHz.
- Requirement: the dedicated-GPIO bundles must be created on the core that runs Step(); the DAP class itself calls Swd::Close()/SetClockHz() from the USB side on the other core (SetClockHz only sets variables; Close()'s pin write has no effect there, the output-enable register write does).
- App option `XRDAP_SWD_CORE` (default 0): creates the SWD objects in a task pinned to that core and pins Step() (and the TCP server) there. Not verified: the Wi-Fi/TCP path with core 1.

# Small packets (xrsmall.py, SWCLK 12 MHz, 3000 sync round trips and 2000-command overlapped runs per cell)
| configuration | command | sync RTT median / p99 / max (us) | cmd/s depth 1 | cmd/s depth 4 |
|---|---|---|---|---|
| ISR execution (a1d233ec) | DAP_Info | 100 / 306 / 768 | 9328 | 20002 |
| | DP read IDCODE | 100 / 258 / 726 | 9226 | 19785 |
| | TAR write + DRW read | 101 / 254 / 732 | 9066 | 19002 |
| Step() on core 0 | DAP_Info | 100 / 306 / 463 | 9342 | 18882 |
| | DP read IDCODE | 101 / 254 / 453 | 9290 | 18670 |
| | TAR write + DRW read | 117 / 266 / 1970 | 7900 | 16241 |
| Step() + SWD on core 1 | DAP_Info | 100 / 280 / 514 | 9347 | 19567 |
| | DP read IDCODE | 100 / 230 / 1219 | 9296 | 19076 |
| | TAR write + DRW read | 104 / 224 / 360 | 8778 | 18736 |
| hand-written SWD + Step() on core 1 | DAP_Info | 100 / 291 / 449 | 9344 | 19191 |
| | DP read IDCODE | 100 / 224 / 387 | 9227 | 18914 |
| | TAR write + DRW read | 106 / 237 / 386 | 8671 | 18795 |
- Round trip is ~100 us in every configuration (host/USB bound); depth 1 is ~9300 cmd/s everywhere. The Step() thread on the USB core costs 6 % on DAP_Info and 15 % on SWD commands at depth 4 and has the worst tail (max 1970 us); with SWD + Step() on core 1 the loss is 2 % / 1-3 %, p99 and max are the best of all (p99 224 us, max 360-386 us vs 254 / 732 us with ISR execution).

# One versus two command buffers (ISR-execution libxr a1d233ec, CMD_BUFFER_COUNT 2 -> 1 in a copy; same app, 4 MHz / 12 MHz)
| | 2 buffers | 1 buffer |
|---|---|---|
| overlapped block read 12 MHz, 4 in flight | 926 KiB/s | 828 (-11 %) |
| overlapped block read 12 MHz, 2 in flight | 503 | 499 |
| overlapped block read 4 MHz, 4 in flight | 289 | 288 |
| OpenOCD dump 4 MHz / 12 MHz | 263 / 577 | 264 / 582 |
| small DAP_Info, depth 4 | 19778 cmd/s | 19463 (-1.6 %) |
| TAR write + DRW read, depth 4 | 18702 | 18183 (-2.8 %) |
- The second command buffer matters only when the host keeps 4 commands in flight and SWD is not the limit (USB-bound regime): +11 %. With a shallow pipeline (OpenOCD) or at low SWCLK it makes no difference. The 1-buffer result (828 KiB/s) equals what the Step()-thread libxr-c 52fa2e92 gets with SWD on core 1 (829) at the same point; worth checking that the two command slots there really overlap reception with execution (next OUT armed before the command runs, slot released at the right time).

# Why OpenOCD / probe-rs stay below the USB ceiling; writes; small packets; both directions (12 MHz, overlapped client unless noted)
- OpenOCD (-d4 log): per 1 KB it sends one DAP_Transfer of 254-255 single reads (not TransferBlock) and keeps 2-3 packets in flight (it submits write N, submits read N-1, then waits): throughput lands between our depth 2 (500 KiB/s) and depth 4 (920) results. At 4 MHz it reaches 88 % of the on-device SWD rate. Packet count 4 is what it uses as FIFO size; effective depth is lower.
- probe-rs 9.6 MHz 2048 B: read 460-467, write 376-385 KB/s, about our depth-2 numbers (pipelining not inspected).
- Directions alone: IN (1 KB responses) ~935 KiB/s, OUT (1016-byte requests, no SWD work) ~985 KiB/s; the full-speed bus is shared, OUT+IN together cannot exceed about that.
- Mixed block write + block read, depth 4: 497 KiB/s of data in total (ISR version) / 496 (Step core 1) / 516 (hand-written SWD): about half of the bus capacity.
- Block write is slower than block read: write transfer costs 1056 cycles (4.4 us) vs read 782 (3.26 us) at 12 MHz request in the libxr generic SWD path (same number of transfers: 256 per write block, 257 per read block, measured), and a hand-written SWD engine (write ~ read cost) only lifts write-only from 333 to 372 KiB/s.
- A small command between big write commands kills the receive/execute overlap: block writes only (254 words): ISR version 775 KiB/s (1.28 ms per block) vs 434 (2.29 ms) with a TAR write between each pair; Step core 1: 564 vs 332. 4 command buffers (instead of 2) do not change it (434), so it is not simply the command slot count; cause not found. OpenOCD is not affected (its write packet contains the TAR write itself).
- Small packets: ~100 us round trip, ~9300 cmd/s one at a time, 19-20k cmd/s with 4 in flight: latency/command-rate bound, not bandwidth.

# Root causes of the slow writes (libxr-c 52fa2e92 with Step() thread, SWD on core 1; instrumented copy `libxr-trace`; 12 MHz, overlapped client)
In-flight commands seen by the device at arrival of each command (outstanding count before the new one):
| workload | 0 others | 1 other (2 in flight) | 2 others | 3 others (4 in flight) |
|---|---|---|---|---|
| OpenOCD dump_image 128 KiB | 1 | 101 | 32 | 96 |
| OpenOCD load_image (RAM write) | 1 | 112 | 2 | 0 |
| probe-rs benchmark 9.6 MHz | 1 | 52700 | 0 | 0 |
| my client, TAR + block write, nominal depth 4 | 1 | 2424 | 1194 | 0 |
- probe-rs keeps exactly 2 commands in flight; OpenOCD 2 to 4 for reads and 2 for writes. Neither tool has an option for it (OpenOCD `cmsis-dap` has backend / usb interface / quirk / vid_pid / cmd / info only; the depth follows the DAP_Info packet count). probe-rs has no such option either.
- Cause 1, command pool of 2 slots: while a big write command executes, the small TAR command behind it takes the second slot, ARM_OUT reports "no free slot" (trace reason 4), and the next big write is only received after the previous one finished (2.29 ms = 1.28 exec + 1.0 receive instead of overlapped). With 4 command slots (= the window): TAR + block write pairs at 4 in flight 332 -> 565 KiB/s (same as back-to-back writes, 563).
- Cause 2, two-step receive (first one endpoint packet, then the rest by command length): pure OUT throughput (1016-byte requests, no SWD work) 490 KiB/s (2.0 ms per command) against 985 on a1d233ec (whole-packet receive). With the first receive asking for the whole DAP packet: 777 KiB/s. Probably an OUT NAK between the two reads (host retry timing); not proven.
- Both together (4 slots + whole-packet first receive): TAR + block write pairs 4 in flight 783 KiB/s, back-to-back writes 788 (ISR version: 434 and 775). Whole-packet first receive re-opens the OUT-termination hang for commands whose length is a multiple of 64 (the reason for the two-step receive); a fix has to keep both.
- More slots cannot help in INLINE (ISR) execution: only one OUT transfer is armed at a time and its completion handler (which re-arms the next one) cannot run while the ISR is busy executing; a1d233ec with 4 command buffers stays at 434 KiB/s. With the Step() thread the ISR re-arms into the next free slot immediately.
- Write transfers cost more than read transfers in the libxr generic SWD path (4.4 us vs 3.26 us at 12 MHz request; hand-written engine: write 703 vs read 761 cycles at the same pacing): the write path issues three dedicated-GPIO writes per data bit (data, clock low, clock high) against two writes and one read for a read bit; 33 bits x ~8 cycles = ~264 cycles, measured difference 274. From code reading plus arithmetic, not from a per-instruction measurement. Merging the data change with the falling clock edge (as in fast_swd.hpp) removes one write per bit.
- Device-side timeline of small commands: OUT complete -> Step() takes it 11 us, execution of a TAR write 16 us, IN transfer start -> complete 22-24 us: ~50 us on the device out of the ~100 us round trip; a 4-byte full-speed IN or 8-byte OUT transaction itself is ~10-12 us on the wire. Small-packet rate is latency bound (host stack + device), not bandwidth bound.

# OpenOCD writes, probe-rs depth, small packets, RMLink wired comparison (2026-10-17; libxr-c 52fa2e92, Step()+SWD on core 1, direct USB port, drive level 1)
- OpenOCD RAM write (`load_image` 16 KiB x10): 4 MHz 268 KiB/s, 12 MHz 506-510 KiB/s (reads: 260 / 645); 4 KiB written and read back byte-identical 10/10 at both speeds. A `verify_image` right after `load_image` reports a bogus mismatch: OpenOCD runs its CRC routine in the work area at 0x20000000 and overwrites the test region.
- OpenOCD single-word ops: read 5155/s (194 us) at 4 MHz, 7168/s (140 us) at 12 MHz; write 6192/s and 7812/s.
- probe-rs commands (device-side counters, benchmark at 9.6 MHz): 48778 DAP_Transfer of 4 bytes (single register read, 7-byte response), TransferBlock of 1021 bytes (254 words) for large writes and 5-byte TransferBlock reads with 1020-byte responses; always 2 commands in flight (52700 of 52700 arrivals). Its benchmark speed ladder is fixed (0.32/0.64/0.96/3.2/6.4/9.6 MHz) and stops at 9.6 MHz. `probe-rs read --speed N b32 ...` (64 KiB, timing difference to a 256 B read): 4 MHz 206 KiB/s, 9.6 MHz 422, 12 MHz 427: saturates at the 2-in-flight ceiling (our own depth-2 pair measurement at 12 MHz: 500).
- OpenOCD commands for dump_image: TransferBlock reads (96, 1020-byte responses) and DAP_Transfer packets with TAR + reads (32, 1019 bytes).
- Small packets, device-side timeline at 4 in flight (DAP_Info): a command arrives every 40-54 us; Step() takes it after 11 us, executes in 3 us, the 4-byte IN transfer waits ~36 us for the host's IN token. Device busy ~14-25 us per command; the 4-in-flight rate (18.8k/s) is below the full-speed bus limit (~38k/s estimated from token, data and handshake overhead, about 26 us per OUT+IN pair).
- RMLink (local build `bt`, fw 0.2.28 + pins confirmed, wired USB, DAP packet size 64, packet count 8, same board and wiring):
  | OpenOCD, same script | 1 MHz | 2 MHz | 3 MHz | 4 MHz |
  |---|---|---|---|---|
  | RMLink dump / RAM write / single-word read | 70 / 69 KiB/s / 2695/s | 127 / 126 / 3663 | 172 / 174 / 4484 | init fails (FAULT, `Failed to read memory at 0xe000ed04`) |
  | ours dump / RAM write / single-word read | 78 / 78 / 2577 | 146 / 148 / 3846 | 199 / 203 / 4484 | 262 / 268 / 5155 |
  Small commands at 4 MHz (cmd/s, 1 in flight / 4 in flight): RMLink DAP_Info 8177/16346, DP read 6942/13151, TAR+DRW read 5478/9629; ours 9347/19567, 9296/19076, 8778/18736. probe-rs does not find RMLink ("No connected probes were found"). My overlapped client crashes on RMLink's 64-byte packets (not measured). RMLink at 4 MHz fails on this bare-wire setup (same signature as our default-drive-strength errors; earlier RMLink runs at 4 MHz were with logic-analyzer probes attached).

# RMLink wired above 4 MHz: pad drive strength decides (2026-10-17)
RMLink fw 0.2.28 local build with the SWDIO/SWCLK pad drive set to level 1 (`gpio_set_drive_capability` added after the pin setup in `debug_gpio.c`, build dir `rml/bd1`, the clone's source restored afterwards); same board, wiring, target and OpenOCD script as the XR runs; libxr-c 52fa2e92 with Step()+SWD on core 1 for ours (also drive level 1).
| SWCLK request | RMLink dump 32 KiB / RAM write 64 KiB / single-word read | ours |
|---|---|---|
| 3 MHz | 172 / 173 / 4444 | 199 / 203 / 4484 |
| 4 MHz | 213 / 215 / 4878 | 262 / 268 / 5155 |
| 6 MHz | 271 / 275 / 5319 | 386 / 398 / 6173 |
| 8 MHz | 308 / 314 / 5587 | 386 / 398 / 6289 |
| 9 MHz | 305 / 314 / 5780 | 653 / 512 / 7576 |
| 10 MHz | OpenOCD write error | 653 / 512 / 7407 |
| 12 MHz | SWJ_CLOCK rejected | 640 / 520 / 7194 |
(KiB/s and operations/s.) With the default drive strength RMLink could not even attach at 4 MHz on this wiring. RMLink saturates near 310 KiB/s from 8 MHz (DAP packet size 64, 15 words per block). Our 6 and 8 MHz requests land on the same real clock (~5.3 MHz, the known gap between the spin-loop floor and the natural 12.5 MHz), which is why those two rows are identical.
Small commands at 8 MHz (cmd/s, 1 / 4 in flight): RMLink DAP_Info 8803/16660, DP read 7728/14012, TAR+DRW read 6441/11520; ours 9290/19199, 9215/18686, 7117/18128.

# 16 MHz test in the DAP (2026-10-17, FastSwd with 2 nops per edge = ~17.0 MHz, sampling after the rising edge; build-hi1)
- OpenOCD program+verify at request 16000 kHz: 8/8 (writes fine).
- OpenOCD dump_image 128 KiB at 16000 kHz: 4 of 5 runs failed (transfer count mismatch 254 vs 28, "Failed to read memory", polling failed); 1 completed (175 ms). Same build at 14500 kHz (3 nops, ~14.9 MHz): 2/2 OK, 177-180 ms.
- Consistent with the sweep (B 0/3000, C 2999/3000 at 17.0 MHz): reads fail at ~17 MHz, ~14.9 MHz is the highest usable setting. Intermediate rates between 14.9 and 17 MHz are not reachable with this engine (dyn pacing base is 11 cycles per half period, larger than the 10 cycles of 16 MHz).
- B variant (build-hi0) built but not run: sweep shows 0/3000 for B at this rate.

# Retest on libxr-c 6fd7828f (borrow mode, 4 command slots), build-c2 = same app as build-c1 (libxr SwdGeneralGPIO, SWD+Step on core 1), 2026-10-17
- First command after boot: the very first session after the flash/reset opened and answered (warm-up "opened OK" twice). Not reproduced (one reset tested).
- Sync client, 4 requests written before reading: `NPAIR=2 xrblock_usb.py 10000000` 259 KiB/s, 0 errors, no hang; `xrusb_ceiling2.py` depth 1-4 padded and unpadded all complete (609 KiB/s unpadded depth 4, 446 padded).
- Pure OUT (1016 B request, no SWD): depth 1 590, depth 2-4 763-766 KiB/s (52fa2e92: 490; ISR a1d233ec: 985).
- TAR+block write pairs (xrwonly): depth 4 777 KiB/s (52fa2e92: 332), depth 2 419; block writes only 786 (ISR 775).
- Overlapped TAR+block-read pairs (xrblock_pipe): 12 MHz 2/4/6/8 in flight 498/529/529/520 KiB/s (52fa2e92 core 1: 498/920/-/479); 4 MHz 228/234/234/232 (52fa2e92: 227/289/265). 4-in-flight read is now much slower than before: REGRESSION vs 52fa2e92 to be reported.
- OpenOCD program+verify 12 MHz 8/8, 4 MHz 4/4; dump_image 128 KiB: 12 MHz 182 ms = 703 KiB/s (52fa2e92 549-643), 4 MHz 485 ms = 264 KiB/s (185-207), content identical to the image.

# Retest on libxr-c b493fed8 (staged response pool), build-c3, 2026-10-17
- Overlapped TAR+block-read pairs: 12 MHz 2/4/6/8 in flight 497/966/982/986 KiB/s (6fd7828f: 498/529/529/520; 52fa2e92: 498/920/-/479); 4 MHz 225/292/291/290 (6fd7828f 228/234; 52fa2e92 227/289).
- Pure OUT depth 1-4: 592/762/765/766 (unchanged). TAR+block write depth 4 771, block writes only 782 (unchanged). Sync NPAIR=2 289 KiB/s no hang; ceiling2 depth 4 613 / padded 454.
- OpenOCD program+verify 12 MHz 8/8, 4 MHz 4/4; dump 12 MHz 185 ms = 692 KiB/s, 4 MHz 490 ms = 261 KiB/s, content identical.

# First-command reset loop, pyOCD, probe-rs on b493fed8 (2026-10-17)
- 20 board resets (esptool hard reset, 3 s wait), then open a WinUSB session WITHOUT warm-up and send DAP_Info: 20/20 OK (build-c3). The "first command lost after boot" is gone.
- probe-rs 0.31 benchmark 9.6 MHz: 2048 B read 468 KB/s, write 373 KB/s (before: 463/380); idle-low build 457/376.
- pyOCD 0.45.1 (venv D:\Codex\tmp\pyocd-venv, generic cortex_m target): FAILED to attach with "No ACK" on every libxr version tested (52fa2e92 and b493fed8). Cause traced with raw packets (rml\pyocd_trace.py, rml\idle_test.py): pyOCD sends DAP_TransferConfigure with idle_cycles = 2. SwdGeneralGPIO::IdleClocks drives SWDIO HIGH during the idle clocks, so the target reads the idle clocks as the start of a request and the next real request gets ack 7 (no ACK). idle=0 works, idle=1/2/4 fail. CMSIS-DAP reference drives SWDIO low during idle cycles. Change SwdGeneralGPIO::IdleClocks to drive low (libxr-patches\0006-swd-idle-clocks-low.patch, one line): idle 0/1/2/4 all work; OpenOCD program+verify 12 MHz 4/4, 4 MHz 2/2, dump 696 / 262 KiB/s; probe-rs unchanged. The hand-written FastSwd::IdleClocks has the same high-idle code (app side, not fixed).
- pyOCD with the fix: attach OK, 12 MHz read32/write32 OK; Python API 128 KiB flash read 4 MHz 237 / 12 MHz 542 KiB/s (identical to the image), 16 KiB RAM write 348 / 713 KiB/s (read-back OK). `pyocd flash` needs the STM32G431 CMSIS pack (download), not tested.

# Keil uVision 5 (UV4 5.x, ARMCC 5.06u7, CMSIS_AGDI.dll) on b493fed8 and idle-low variant, 2026-10-17
- Minimal RAM-only Cortex-M4 project (generic ARMCM4, no STM32G4 DFP installed so no flash download), debug init script reads 32 KiB of flash word by word, writes/reads back 1 KiB of RAM. Files: D:\Codex\tmp\keil-xrdap (gen.py generates p4 / p12 projects for 4 / 12 MHz).
- build-c3 (SWDIO high during idle clocks) and build-idle (low): both 4 MHz and 12 MHz connect, flash sum f645fd38 equals the image, RAM mismatches 0. So Keil's CMSIS-DAP driver works on both; it does not trigger the idle-clock bug (it sends idle cycles 0 by default).
- Keil quirk: after one debug session uVision rewrites t.uvoptx and the next `UV4 -d` run on the same project hung (60-90 s timeouts, also after board resets); regenerating the project before every run fixed it. Not looked at further (host-side tool behaviour, probe stays healthy: OpenOCD afterwards fine).
- Not tested in Keil: flash download with a G431 flash algorithm (needs the STM32G4xx DFP download), Keil with a non-zero idle-cycle setting.

# Keil flash + debug on the real G431 (user-operated, 2026-10-17, build-idle on libxr-c b493fed8)
- STM32G4xx_DFP 2.2.0 installed by hand (Packs\Keil\STM32G4xx_DFP\2.2.0). Project D:\Codex\tmp\keil-xrdap\g431 (gen_g431.py; 96 KiB pseudo-random blob, flash algorithm STM32G43x-4x_128, dbg.ini freezes IWDG/WWDG).
- Headless `UV4 -f` at 4 MHz: Erase/Program/Verify OK in 6.9 s; OpenOCD read-back identical to the generated blob. A first project without the Utilities (flash programming) section made Download grey; fixed in gen_g431.py.
- User ran Download and Debug in the Keil GUI: passed (user report; clock settings and details not recorded).

# Hot-plug (user unplugs/replugs the native-USB DAP cable, board stays powered through the CH343 UART cable), 2026-10-17
- libxr-c b493fed8 (build-idle/c3), streaming TAR+block reads (rml\hotplug.py): the first unplug was detected by the client (WinError 31) but the device never came back; later replugs showed "Unknown USB device (device descriptor request failed)", problem code 43. Device is not dead: an esptool hard reset (RTS) re-enumerates it and it works again.
- Same with no host session open (idle device, watcher rml\pnp_watch.ps1): first replug -> "(no DAP device)" then "Unknown USB device (descriptor request failed)".
- libxr-c 52fa2e92 (build-c1): same failure on the first replug. So it is not a regression of the new versions.
- RMLink firmware (TinyUSB) flashed on the same board, same cable and ports: unplug/replug re-enumerates normally (device back ~1.2 s after the unplug). => cable, port and board wiring are fine; the failure is in the libxr ESP32 USB device path (or how the app uses it).
- Code facts: driver/esp/esp_usb_dev.cpp forces gotgctl bvalidoven=1/bvalidovval=1 (no VBUS sensing, pull-up stays on); handles usbrst/enumdone/usbsusp/wkupint. Root cause not found; no register dump yet.

# Hot-plug root cause (2026-10-17, libxr-c b493fed8 = 52fa2e92 behaviour, ESP32-S3 DWC2 device driver driver/esp/esp_usb_dev.cpp)
- Evidence (instrumented copy xrdap-s3\libxr-usbdbg, register/ISR-counter log over UART, rml\usbreg_run*.log): after the cable is pulled the core goes into suspend (DSTS.suspsts=1), after the replug the host sends a bus reset; HandleBusReset runs (rst_in 1->2) and after it GAHBCFG.glbllntrmsk (bit 0) = 0, GINTMSK/DAINTMSK = 0: the controller is deaf, no further interrupt, no EP0 setup is answered -> Windows "Unknown USB device (device descriptor request failed)".
- Cause: HandleBusReset's bounded stop check did not settle (an IN endpoint stayed enabled after the unplug), so it escalates through OnStopTimeout -> reset chain -> ResetDone, which does ResetCoreRegisters + RebuildCoreRegisters + sftdiscon=0 but never sets glbllntrmsk = 1 again (Start() does at line 173, ResetDone does not).
- One-line fix in ResetDone (libxr-patches\0007-esp-usb-reset-reenable-global-irq.patch): `dev->gahbcfg_reg.glbllntrmsk = 1;` before `started = true`. With it (build-reg2 + patch): 3 unplug/replug cycles, every one re-enumerates (reset count 1->2->3, GAHBCFG stays 0x1a9, DAINTMSK back to 0007000f after the setup/config phase), user confirmed Windows recognises the device.
- Not yet tested with the fix: hot-plug while a host session streams (rml\hotplug.py), unplug during an active transfer, bus reset from the host side only. Why the stop check never settles on an unplug (IN endpoint stays enabled) not investigated.

# Streaming hot-plug with the fix (build-reg2 = b493fed8 + glbllntrmsk fix, 4 MHz, rml\hotplug.py 150 s, TAR+block reads 4 requests in flight), 2026-10-17
- 3 unplug/replug cycles (quick replug, 3 s, 10 s): 3 losses detected (WinError 31 / 995), 3 automatic reconnects with data verified, 14923 rounds, 0 bad data, no board reset. OpenOCD program+verify afterwards OK.

# Final wired regression on libxr-c 22ba8642 (pushed; build-fix = fff5f68d + the two working-tree fixes, source-equivalent), app build-fix (Wi-Fi off, SWD+Step core 1, drive cap 1), 2026-10-17
- WinUSB binding with fff5f68d (MS OS 2.0 descriptors): CMSIS-DAP v2 service WINUSB, problem code 0; 5/5 first command after reset without warm-up.
- OpenOCD program+verify with two DIFFERENT 100 KB images (the earlier "a/b" images had identical content, so those alternating runs were not a real change test): 4 MHz 16/16 (+8/8), 12 MHz 16/16 (+8/8) with a/b; with rnd1/rnd2 8/8 each; dump 128 KiB 4 MHz 266, 12 MHz 692-711 KiB/s, content identical.
- probe-rs: download+verify 4 MHz OK; 9.6 MHz benchmark 2048 B read 467 KB/s, write 387 KB/s.
- pyOCD 0.45.1 (generic cortex_m via Python API): 4 MHz read 239 / write 360, 12 MHz read 546 / write 774 KiB/s, read-back identical. `pyocd flash --pack Keil.STM32G4xx_DFP.2.2.0.pack -t stm32g431kbtx` with random 100 KB images: 4 MHz 4.5 s, 12 MHz 4.1 s (programmed 98 pages), read-back identical; chip erase + image also OK.
- Keil (g431 project, flash algorithm STM32G43x-4x_128): headless download OK, OpenOCD read-back identical to the 96 KiB blob.
- Hot-plug during flashing (OpenOCD program loop at 4 MHz, user unplugged twice): runs interrupted by the unplug fail with "error reading/writing USB data" (expected), runs while the cable is out fail with "unable to find a matching CMSIS-DAP device", openocd does not hang; after the replug the device re-enumerates (CMSIS-DAP v2, code 0) and program+verify 4/4 at 4 MHz and 4/4 at 12 MHz succeed again (flash content repaired, dump identical).
- Host-side bus reset without unplugging: not run (no admin shell); the firmware path is the same bus-reset interrupt the replug exercises.

# Wi-Fi / TCP on libxr-c 22ba8642 (app build-wifi: Wi-Fi on, SWD + Step + TCP server on core 1, drive cap 1), host = lab3b Pi, 2026-10-17
- Boot: Wi-Fi connected (STA 10.43.0.168), USB deinitialised after Wi-Fi up, TCP server listening on 3241. Probe reachable only from the Pi (10.43.0.0/24 behind lab3b); the Windows PC cannot reach it. The OpenOCD in the ESP-IDF tools (v0.12.0-esp32-20250707) has no `cmsis-dap backend tcp` ("invalid argument tcp"), so OpenOCD over TCP is not tested yet; the Pi has no OpenOCD (apt candidate 0.12.0+rpt20250909 exists, not installed).
- Functional (Pi scripts, rml\swdtcp.py / swdbench.py / tcp_block.py): DPIDR, CPUID, DBGMCU id, scattered reads, 255-word block reads, 254-word block writes with read-back all correct.
- TCP throughput (KiB/s, Python client on the Pi, 1 KB DAP packets, TAR+block pairs): 4 MHz read 135/146/160 (depth 1/2/4), write 137/148/164; 12 MHz read 210/245/263, write 203/228/268. SWD-bound at 4 MHz would be ~297, at 12 MHz ~1071 on-device, so TCP over Wi-Fi is the limit at 12 MHz (~270 KiB/s, executed in the same task serially: RX, execute, TX).
- SWD signal margin (found by the TCP soak, reproduced over USB with Wi-Fi off, rml\usb_wsoak.py, 90 s each, 255-word read blocks and 254-word write blocks): 4 MHz 0 errors (12884 read + 6442 write blocks), 8 MHz 0 errors (18528 + 9264), 10 MHz 8 errors (30956 + 15478, all FAULT acks), 12 MHz 4-6 errors per 90 s (~46k blocks, ack=4 FAULT on reads and writes). So the clean range with this wiring and drive cap 1 is <= 8 MHz; >= 10 MHz shows rare errors (~1e-4 per block) that OpenOCD/probe-rs hide by clearing the sticky error and retrying. The earlier "error free to 14.5 MHz" came from short sweeps.
- TCP robustness (rml\tcp_robust.py, tcp_drop.py, tcp_dead_client.py): 60 connect/attach/close cycles 60/60; 10 half-frame RST clients, 10 closes with a response in flight, a second client waits and is served after the first closes, a connection idle for 60 s still works; Wi-Fi deauth of the probe (`iw station del`): back in ~2 s and serves again. BUG found: a client that vanishes without FIN/RST left the single-connection server stuck forever (60 s test: never served). Fixed in app dap_tcp.hpp with SO_KEEPALIVE (idle 5 s, interval 2 s, 3 probes): dead client dropped, next client served after ~5 s; idle live connection, 90 s soak at 8 MHz (14700 ops, 0 bad) and the other tests unchanged.

# OpenOCD over the TCP backend (Espressif OpenOCD v0.12.0-esp32-20260831 linux-arm64 downloaded to /tmp/ocd2 on lab3b, not installed; the PC's 20250707 build and the Pi's apt 0.12.0+rpt20250909 have no `cmsis-dap backend tcp`), host = Pi, 2026-10-17
- Attach + program/verify of two different 100 KB images, default queuing: 4 MHz 8/8 and 3/3; 8 MHz 22/24, 7/8, 12/12, then 24/24 (ack-logging build) and 24/24 while the Wi-Fi link was flooded (31k 1400-byte pings, 1 lost); 12 MHz 22/24, 8/8, 24/24; quirk enable (no queuing) 8 MHz 23/24. Each run ~4.1 s at 8 MHz.
- Failures seen in the early runs (6 of ~115): "Failed to write memory at 0x2000xxxx / block write failed", "checksum mismatch / Verify Failed", "Failed to read memory at 0x40022014 / failed erasing sectors". Later 72 runs (ack-logging build, flood) had none, and the probe logged no non-OK acks (cmd 0x05/0x06, WAIT excluded) in 48 runs. Not reproduced, cause not identified; the early failures overlapped a period when the user reported lab3b network trouble (video), unproven. USB with the same firmware had 0 failures in ~110 OpenOCD runs.
- Dump 128 KiB over TCP incl. attach: 4 MHz 1095 ms, 8 MHz 922 ms, 12 MHz 766 ms (~170 KiB/s at 12 MHz).

# Production defaults (build from this repo's firmware/, libxr-c 22ba8642), final checks, 2026-10-17
- Tuning in sdkconfig.defaults (QIO flash, 32 KB icache / 64 KB dcache, silent assertions, lwIP in IRAM on core 0, larger Wi-Fi buffers, BA windows 32, lwIP mailboxes 64, tcpip task priority 23): TCP block read/write at 12 MHz 259/265 -> 379/454 KiB/s, at 8 MHz 198/208 -> 261/309; USB unchanged (bus-bound, ~960-990 KiB/s read, ~770-790 write). -O3 for the main component gave no measurable gain (image +8 KB), not kept. `CONFIG_LOG_DYNAMIC_LEVEL_CONTROL=n` did not take effect and was dropped.
- USB <-> TCP hand-over (USB stays up, goes down while a TCP client is connected, comes back when it leaves): 8/8 and 6/6, 4/4 rounds.
- SWD errors are NOT a tuning effect: the untuned build had 5 and 0 write FAULTs in two 120 s USB soaks at 8 MHz, the tuned build 1 and 6. The earlier "0 errors at 8 MHz" was a single 90 s run. USB soak at 4 MHz, 300 s: 42736 read + 21368 write blocks, 0 errors. So the clean range so far is 4 MHz; 8 MHz and above show sporadic FAULTs (about 1e-4 per write block).
- TCP regression on the final build: 60 connect/attach cycles 60/60, RST/half-frame/in-flight close clients ok, second client queued then served, 60 s idle connection ok, Wi-Fi drop recovery ok, dead-client keepalive ok; OpenOCD over TCP at 4 MHz 12/12.
