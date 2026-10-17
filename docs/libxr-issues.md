# XR ESP32-S3 CMSIS-DAP probe — findings for libxr maintainers (2026-10-16)

Status: observations from one bring-up session. "Observed" = measured here; "Hypothesis" = not verified.

## Status update (2026-10-16, late) - read this first
Items below were written against libxr-c `52862a09`. Current state in the libxr-c worktree (branch feat/usb-step5-c-esp, local only):
| item | state |
|---|---|
| B5 trailing ZLP holds a queue slot | fixed, a1d233ec (ZLP only below the DAP packet size) |
| C9 BusyLoop `volatile` (9 cycles/iter) | fixed, 71c83895 (register counter + `atomic_signal_fence`, also Jtag) |
| NEW: OUT command never ends when its length is a multiple of 64 (< DAP packet size, FS USB) | fixed, 03d2026e (end by command length); host tests only, not yet on the board with packet size 1024 |
| NEW: DAP_TransferAbort answered with 2 bytes | fixed, 03d2026e (no response); not checked against OpenOCD/pyOCD behaviour |
| NEW: DAP_ExecuteCommands (0x7F) ignored the commands in its own packet | fixed, 03d2026e |
| DAP_Delay rounded up to 1 ms | not changed on purpose (spec 10.3-a: no busy wait in the owner); decision pending for sub-ms delays |
| C9 "2.78 MHz plateau, ceil never gives 1" | claim corrected by the maintainer: iterations can be 1; the plateau is ~86 cycles fixed cost per bit; `loops_per_us = 0` selects the no-delay path (fastest) |
Evidence for the OUT-termination bug: EP1 OUT DOEPTSIZ dump while a 128-byte command was pending with DAP packet size 1024; with packet size 64 the stall disappears. See results file section "OpenOCD flash programming ... root causes".

Not libxr bugs found while chasing the same OpenOCD flash failures (so nobody searches for them in libxr):
- Target IWDG resets the G431 during long flash operations when the debugger does not freeze it (`mmw 0xE0042008 0x1800 0` after halt).
- Residual single `SWD ack not OK @ N JUNK` at 2 MHz depended on pad drive strength / probe loading: ESP32-S3 pads at the IDF default drive level 2 fail intermittently in the OpenOCD flash flow, level 3 fails 0.55 % of SRAM block loops, levels 0/1 had no failures (64+ runs). Oscilloscope shows ringing and over/undershoot on bare jumper wires (SWDIO). The app sets level 1 via `gpio_set_drive_capability`. Question for maintainers: should SwdGeneralGPIO / the ESP GPIO class expose pad drive strength (or document series resistors)? Not measured: 33 ohm series resistors, SWCLK waveform.

Unmerged SWD fix to look at: `origin/fix/swd-write-trailing-idle-clocks` (e816f439, "trailing idle clocks after write data phase", author Xiao, 2026-07-04, F302CB target) is not in master, dev or the integration branches. It targets the same symptom string (`ack not OK @ N JUNK` after a posted write). Applied here (local, reverted afterwards) it did NOT reduce our failures (20/32 at 2 MHz, default drive); that says nothing about its F302 case. Needs an owner decision: merge, rework or drop. It also needs rebasing (the constant hunk conflicts with the current header).

## Retest results after 03d2026e (board, 2026-10-16 evening)
| item | result |
|---|---|
| OUT command ends by its own length (packet size 1024) | VERIFIED on board: OpenOCD program+verify, 64/64 (2 MHz 16, 4 MHz 16, 12 MHz 32), IWDG frozen, drive level 1 |
| **NEW REGRESSION in 03d2026e: zero-padded OUT requests with 4 commands in flight hang the device** | see below |
| A3 iProduct string | reproduced: WinUSB reads iProduct `CMSIS-DAP v2`, not the string given to `MakeLanguagePack` ("XR-DAP-S3"); the PnP FriendlyName shows XR-DAP-S3. Low priority |
| A2 single-class DapLinkV2 device not bound to WinUSB | reproduced on a legal layout (EP0 + EP1 only, fresh PID 0x4A02): PnP problem code 28, no service; composite with CDC binds fine. Real; needs a device-level compatible-ID descriptor or a documented requirement |
| A1 host wedge after heavy traffic | the original symptom was not reproduced; the only hang found on a legal layout is the regression below (device stops answering, host sees WinError 121; hard reset recovers; no replug needed) |
| B6 EP buffer smaller than the DAP packet size, no construction-time check | still true by code reading (no REQUIRE in DapLinkV2Class/ESP32USBDevice for it); an earlier run with a 512-byte buffer failed silently. Small REQUIRE suggested |
| 6 commands in flight stalls | not a libxr bug: our own client exceeded the device-reported packet count (4); OpenOCD obeys it and had no problem |
| TransferWithoutDelay samples SWDIO right after the falling edge | no failure seen in this setup: stress firmware (12-word SRAM write + read-back loop, drive level 1, no analyzer), 90 s each, 16 MHz request (unpaced, natural ~12.5 MHz) 0/686100 errors, 12 MHz request (paced) 0/686080 errors. The earlier ack errors at 12.5 MHz appeared only with analyzer probes attached; not isolated further. Drop unless it reappears |

### More from the board (libxr-c 03d2026e)
- The padded-request hang needs no SWD: 0x7F with 255 x DAP_Info (512-byte request zero-padded to 1024, 1021-byte response), 4 requests in flight hangs the device; 3 in flight works (338 KiB/s). Easiest repro: `D:/Codex/tmp/rml/xrusb_ceiling2.py` (PAD handled inside the script, pad=True run).
- DAP_ExecuteCommands (0x7F) response has no command-count byte: `[7F, 3, 3 x DAP_Info(0xFF)]` -> `7f 00 02 00 04 00 02 00 04 00 02 00 04`. The ARM definition (from memory) is `[7F, count, responses...]`. Please check.
- USB-only ceiling (no SWD) measured with my Python client: 448 KiB/s for 1 KB responses. CORRECTION: that is the client's limit (one IN read in flight), not the device's; OpenOCD reads the same firmware at 578 KiB/s (12 MHz, 128 KiB dump) and 266 KiB/s at 4 MHz (on-device 297). The device's real full-speed ceiling is >= 582 KiB/s and unmeasured. A USB dock made no difference.
- SWD commands execute inside the USB interrupt (155109 of 155109 transfers, core 0). A prototype that runs them in a task (patch `libxr-patches/0005-dap-executor-task-prototype.patch`) is NOT better: OpenOCD dump_image 4 MHz 266 -> 183 KiB/s, 12 MHz 578 -> 545 KiB/s (Python client: +24 % on 84-word scattered reads at 10 MHz, -13 % on 4 MHz block reads). Cause of the loss not found. Keep the owner model; the remaining concern is real-time behaviour (ISR held 1-12 ms; WAIT retries could be far longer; untested).

### Regression: padded OUT requests, 4 commands in flight (libxr-c 03d2026e; a1d233ec is fine)
- Setup: ESP32-S3 full-speed USB, `DapLinkV2Class<..., 1024, 4, 1024>`, Windows WinUSB client `D:/Codex/tmp/rml/xrblock_usb.py`, command `NPAIR=2 PAD=1 python xrblock_usb.py 10000000` (target halted, G431). Each round sends 4 requests back to back: AP write TAR (9 bytes), 255-word block read (5 bytes), again TAR, block read; PAD=1 zero-pads every OUT transfer to 1024 bytes (what RMLink's client does).
- a1d233ec: passes, 252 KiB/s, 0 errors (2 of 2 runs, hard reset in between).
- 03d2026e: the first exchange times out (WinError 121); afterwards even a single DAP_Info from a new session times out. 3 of 3 runs, hard reset in between. Hard reset recovers.
- Not failing on 03d2026e: PAD=1 with NPAIR=1 (2 commands in flight, 218 KiB/s); PAD=0 with NPAIR=2 (452.9 KiB/s); OpenOCD flashing (it does not pad); scattered reads with 8 padded requests in flight.
- Hypothesis (not verified, no device state dump taken): the new "all-zero full packet after a complete command is dropped" path, combined with the command ring and the 4-slot response queue (TAR response + 1 KB block response), does not re-arm the OUT endpoint when the ring or queue is full; each padded request is 16 full packets, 15 of them zero.

## Setup
- Board: ESP32-S3 N8R8 (8 MB flash), IDF 5.5.2, 240 MHz. Target: STM32G431KB (DPIDR 2ba01477, CPUID 410fc241), core halted + IWDG/WWDG frozen for benches.
- libxr: public master `5161373` (v2.0.1, 2026-10-11). NOT the integration tree (`bd13c8f5`, local only). libxr tree unmodified; the project is `D:\Codex\tmp\xrdap-s3\app`.
- Stack: `DapLinkV2Class<…>` + `SwdGeneralGPIO<S3DedicGpio,S3DedicGpio>` (own LibXR::GPIO class on S3 dedicated GPIO), `ESP32USBDevice` + `CDCUart`. SWCLK=GPIO5, SWDIO=GPIO4.
- Host: Windows 11 (USB), lab Pi over Wi-Fi (TCP).

## A. ESP32USBDevice / USB

> **CORRECTION (added after another session's finding):** my USB layout was illegal for the ESP32-S3. The configuration list was EP0, EP1 IN (DAP), EP2 OUT (DAP), EP3 IN (CDC data), EP4 OUT (CDC data), **EP5 IN (CDC notification)**; the S3 has IN endpoints only on EP0 and EP1-EP4. Master's `ESP32USBDevice` constructor assigns endpoint numbers sequentially per entry and silently accepted this (only `DEV_ASSERT(ep < ENDPOINT_COUNT)`). So A1 below is **not** a clean report of a libxr hang: it was observed with an invalid endpoint layout and has not been re-tested with a legal one. The other session's REQUIRE (IN endpoint number > 4 is fatal at construction) addresses the silent acceptance. Also unverified: that in master the ~5 ms SWD processing per packet runs inside the USB interrupt/callback context, which could by itself disturb the host. A2-A4 and sections B/C are unaffected (A2's composite requirement was observed with the same illegal layout, so re-check it too).
1. **Windows host wedges after heavy bulk traffic (observed with the illegal layout above; unresolved, needs re-test).**
   - Light sessions (DAP_Info/DPIDR/CPUID) are stable: 6 sessions at 20 s intervals OK.
   - After one heavy session (3 pipelined 507 B DAP_Transfer requests × ~60 rounds ×3, 339 B responses, 1024 B padded OUT), the next session fails at `WinUsb_GetDescriptor` with WinError 31. First heavy session itself completes with zero data errors. Reproduced 4 times.
   - Chip reset (RTS / esptool hard_reset) does not recover; only physically replugging the USB cable does.
   - Device side while wedged (1 Hz register heartbeat, base 0x60080000): SOF frame number keeps advancing, `DSTS.SUSPSTS=0`, `DOEPCTL0=80008000` (EP0 OUT armed), later `DIEPCTL0=80008000` with `DIEPINT0=0` (EP0 IN armed, never completes), `DAINT=0`. No panic, no reboot in UART. Windows event log has no USB error entries.
   - Hypothesis only: host marks device/port failed. Not verified: no USBPcap / usbmon capture, not tried on Linux.
   - Question: does your integration tree's ESP work (EP0/stop path/cache-sync range) address this? Reporter does not claim it does.
2. **Single-function DapLinkV2 device is not bound to WinUSB (observed; recheck, see correction).** With only `DapLinkV2Class` in the configuration, Windows reports problem code 28 (Class FF, no driver) even after changing PID. Adding `CDCUart` (composite) makes `usbccgp` + WINUSB bind to MI_00 correctly. Hypothesis: the MS OS 2.0 set uses a function subset header, which Windows applies only to composite devices. Ask: document this, or emit a device-level compatible-ID descriptor for single-class devices.
3. **iProduct string read via WinUSB returned `CMSIS-DAP v2`, not the product string passed to `MakeLanguagePack` ("XR-DAP-S3") (observed once, not investigated).** Device Manager showed "XR-DAP-S3" earlier, before the driver bound. Possible string-index collision between product and interface strings. Low priority.
4. **Linker error with `IRAM_ATTR` on header-inline class methods (observed):** `dangerous relocation: l32r: literal placed after use` for a LibXR::GPIO subclass in a header. Workaround used: no IRAM_ATTR. Suggest documenting a linker-fragment approach for hot SWD code.

## B. DapLinkV2Class
5. **[FIXED a1d233ec] A response that is exactly as long as the DAP packet gets a trailing ZLP and keeps a response-queue slot (root-caused on libxr-c `52862a09`, one-line fix verified).**
   - `KickResponse` appends a zero-length packet whenever `len % MaxPacketSize == 0`. A TransferBlock read of 255 words answers 4 + 1020 = 1024 bytes, which equals the DAP packet size. The host reads with a DAP-packet-sized buffer, so its read completes when the buffer is full and it never takes the ZLP. The ZLP stays armed on the IN endpoint (DIEPTSIZ = 0x20080000: PKTCNT 1, XFERSIZE 0), the response is not retired until it is sent, and one of the 4 queue slots stays occupied until the host issues another read.
   - Symptom with a deep pipeline: 4 commands in flight (two TAR + two 1 KB block reads) stalls the OUT pipe (`WinUsb_WritePipe` times out). Device state at the stall (1 Hz dump): `q_cnt=4 rd_act=0 wr_act=1 cmd_pend=0`, EP1 OUT not armed because the queue is full; the host is blocked in a write and does not read, so neither side moves. 2 commands in flight work, 4 do not.
   - Fix (local, uncommitted in the libxr-c worktree): `need_zlp = (len % mps == 0) && (len < GetDapPacketSize())`. After it, 4 commands in flight work with 0 errors (452 KiB/s block read at 12.5 MHz, same board and host). Note this changes the rule another session decided to keep ("append a ZLP for integer multiples of the packet size"); the ZLP is still needed for multiples shorter than the DAP packet size.
   - Exceeding 4 in flight (e.g. 6 commands) still stalls; that is within the limit the device reports: `DAP_Info 0xFE` returns `PACKET_COUNT_EFFECTIVE` (4), not the template's `AdvertisedPacketCount` (8). **An earlier version of this item said the device advertises 8; that was wrong (I read the template parameter, not the DAP_Info answer) and is withdrawn.**
   - Host side: a client that zero-pads every OUT transfer to the DAP packet size (RMLink's does) halves wired throughput (10 MHz block read 235 -> 438 KiB/s once padding was removed).
6. **Endpoint buffer must be ≥ MaxDapPacketSize (1024), no construction-time check (observed).** I first used 512 B EP buffers while DAP_Info advertised 1024; hosts pad OUT transfers to the advertised size. Wrong sizing is silent. (The integration tree's constructor REQUIRE seems to target this class of problem.)
7. **No transport-agnostic entry point (observed need).** `ProcessOneCommand` is private, so a TCP transport (OpenOCD `cmsis_dap_tcp.c` framing, "DAP\0"+LE16 len+type+reserved) cannot reuse the command processor. I used `#define private public` around the include in one TU. Ask: a public `ProcessRequest(req, len, resp, cap, out_len)` (no endpoint state), or a transport interface.
8. **4 SWD transfers per scattered word (observed, counted by a pass-through `Swd` wrapper).** Sequence "AP write TAR, AP read DRW" ×84 per packet: 162059 transfers for ~40.3k words ≈ 4.0/word (RMLink's CMSIS-DAP: 3; with posted-TAR: 2). I understand the KB-accepted AP-write semantics (CTRL/STAT check, RDBUFF only for posted-read completion). Question: is the CTRL/STAT check required per TAR write in a read-only sequence, or could it be deferred to the boundary?

## C. SwdGeneralGPIO / ESP32
9. **[BusyLoop FIXED 71c83895; plateau explanation CORRECTED, see status update] BusyLoop delay path: coarse and slow (measured with a logic analyzer, DSLogic 50 MS/s, 46-clock DPIDR reads).**
   - `BusyLoop` keeps its counter in a `volatile` local. Disassembly: every iteration is `memw / l32i / addi / memw / s32i / bnez`. Measured on the S3 (240 MHz, interrupts off, IRAM): **9.00 cycles per iteration, 12 cycles fixed cost** even for N=0 (`while (sink--)` still runs one full pass). Compared in the same harness: `atomic_signal_fence` loop 4.00/iter (3 fixed), register loop with empty asm 4.00/iter, `atomic_thread_fence` 5.00/iter, Xtensa `loop` instruction 1.00/iter. Without any of these (plain local counter) GCC 14.2 -O2 deletes the loop.
   - Because `half_period_loops_ = ceil(loops_per_us * half_ns / 1000)` (0 below 1000), the smallest non-zero delay is 2 iterations. Measured actual SWCLK in `Transfer` for requests 6, 8 and 10 MHz is the same **2.78 MHz**; 1/2/3/4/5 MHz give 0.85/1.47/1.92/2.27/2.27 MHz. The plateau is real but is a property of this delay implementation, not of the S3.
   - Inside a transaction the clock is regular: transaction time is within 0.1-0.6 us of 46 clock periods, gap between transactions 0.4-1.0 us. **An earlier version of this item claimed ~9 us fixed overhead per transfer; that was a fitting artifact and is withdrawn.**
   - Requests above ~13 MHz (with `loops_per_us=23..26`) switch to `TransferWithoutDelay`, which reads SWDIO right after writing the falling edge. In this setup those transactions failed (partial bursts). With the clock paced by a cycle counter inside the SWCLK `Write()` and the SWDIO read happening after the paced falling edge, the same target answered at a measured 10.0 MHz (period 100 ns). Hypothesis (not isolated): the early sampling point, not the frequency, is the cause.
   - Suggested libxr change (applied locally, uncommitted, in the libxr-c worktree): `BusyLoop` with `std::atomic_signal_fence`. It changes the real clock of every board calibrated against the old loop (e.g. the CH32 `kSwdDelayLoopsPerUs`), so those need recalibration.
10. **ESP32GPIO speed not measured.** I did not benchmark `ESP32GPIO` (gpio_hal) as the SWD pin type; I went straight to dedicated GPIO. No claim about it.
11. **Dedicated GPIO is per-core (observed).** `ee.wr_mask_gpio_out` only drives pins on the core that created the bundle. TCP/SWD task on core 1 with bundles created on core 0: SWD reads returned ACK 7. Moving the task to core 0 fixed it. Ask: a libxr ESP dedicated-GPIO class (documented core affinity) would avoid this trap.
12. **8 MHz works in this stack with zero errors (9989 words/s), whereas RMLink fw 0.2.28 shows errors at 8 MHz on the same target/wiring.** 24 MHz request: target power-up timeout (wiring/target limit, not investigated).

## Measurements (Wi-Fi TCP 3241, 84-word scattered reads, 8-deep pipeline, errors = 0 for 1–20 MHz)
| request | XR words/s | RMLink 0.2.28 | RMLink + posted-TAR |
|---|---|---|---|
| 1 MHz | 2942 | 6024 | ~8.4k |
| 4 MHz | 7780 | 19883 | ~26.2k |
| 8 MHz | 9989 | invalid | invalid |
| 10 MHz | 11675 | 36521 | 42–44k |
| 12/16/20 MHz | 11475/11677/13635 | – | – |
At 10 MHz: ~5.2 ms DAP processing + ~2 ms serial TCP RX/TX per 84-word packet.

## Questions for maintainers
1. Is the host-wedge in A1 known? It was seen with an illegal EP5-IN layout (see correction), so it should be re-tested with a legal layout on master and on the integration tree.
1b. Should master reject IN endpoint numbers above 4 on ESP32-S3 at construction (the other session's REQUIRE)?
2. Public API for processing a DAP request without USB endpoints (B7)?
3. Intended policy for CTRL/STAT checks in scattered reads (B8)?
4. Preferred way to calibrate/clock SwdGeneralGPIO on ESP (C9), and where should an ESP dedicated-GPIO class live?
5. WinUSB single-class binding (A2): document or fix?

## Artifacts
- Results: `D:\Codex\tmp\rml\results-20261016.md`; project: `D:\Codex\tmp\xrdap-s3`; UART logs `uart2.log`, `uart5.log`.
- RMLink baseline firmware and bench scripts: `D:\Codex\tmp\rml`.

## Final numbers (2026-10-16 night) - supersede earlier USB/SWD ceiling statements above
- libxr-c e196f094 fixes verified on the board: padded 4-in-flight regression gone (3/3), OpenOCD flash 4 MHz 8/8 and 12 MHz 16/16 at packet 1024, single-class device binds WinUSB (problem code 0). A2 and the regression can be closed.
- Full-speed USB ceiling of this device with a fully pipelined host (overlapped I/O, 4 commands in flight = device-reported count): ~920 KiB/s of block-read data (TAR write + 255-word block read pairs), 621 KiB/s for 0x7F with 512 B requests and 1021 B responses. The earlier 450 KiB/s (sync Python client) and 578 KiB/s (OpenOCD) are host-limited. With 6 or 8 commands in flight throughput drops to ~480 KiB/s (no hang).
- SWD: a hand-written dedicated-GPIO engine is error-free up to 14.5 MHz on the bare-wire setup (1254 KiB/s block read) vs 12.5 MHz / 1071 KiB/s for SwdGeneralGPIO without delay; faster fails (target does not answer), cause not separated from wiring. Not worth integrating while USB is the limit (~920 KiB/s).
- SWDIO output enable through the GPIO enable register takes ~16 CPU cycles to act; relevant for any fast bit-bang driver on ESP32-S3.

## SWD speed limit explained (2026-10-16 night)
CORRECTION of the paragraph that stood here: SwdGeneralGPIO's no-delay path (`clk low; read; clk high`) is NOT an early-sampling position in time. Its low phase is only 2-3 CPU cycles and the rest of the ~19-cycle period is the high phase, so the read happens about 2-3 cycles before the next rising edge, i.e. position B (end of the period, the CMSIS-DAP position), as the delay path and the edge-paced variant do. The earlier TransferWithoutDelay-samples-early hypothesis is not supported. What the sweep shows: with the read just before the rising edge (B) the S3 setup is error-free up to ~15 MHz and fails from ~16 MHz; sampling right after the rising edge (C) reaches 17 MHz but is marginal at every frequency; sampling at the falling edge with symmetric half periods (A) fails from ~11.5 MHz. The round-trip delay (~50-55 ns, estimate) is what limits the frequency. Not needed while USB (~920 KiB/s) is the bottleneck.

## Board verification of 52fa2e92 (2026-10-16 night) - for the libxr session
1. First host command after boot is lost (every reset; python client and OpenOCD alike; 30 s wait does not help); later sessions fine. New since e196f094. Repro: reset, run any WinUSB client that sends DAP_Info first: the read times out; run it again: works.
2. Sync client writing 4 requests before reading deadlocks the device (needs a hard reset): `NPAIR=2 xrblock_usb.py 10000000`, or `xrusb_ceiling2.py` depth 4. 3 in flight and 2 in flight work; an overlapped client (read always queued) works with 4/6/8 in flight, padded or not. e196f094 handled the 4 synchronous writes. Suspect: the window/outstanding count now stops the 4th OUT until a response is taken while the host is blocked in that write. pyOCD-style clients (write packet_count packets, then read) may hit this; not tested.
3. Step() thread costs throughput when SWD-bound: block read 4 MHz 4 in flight 289 -> 183 KiB/s (slower than 2 in flight, 227), 12 MHz 920 -> 808; OpenOCD dump 4 MHz 266 -> 185. Small commands 18955/s (-5 %). Possible hand-off latency between the Step thread and the receive/send side (e.g. next command is only accepted after the response is published, or a wakeup only via semaphore).
4. Verified OK: padded requests with an overlapped client, 6-8 in flight without hang, OpenOCD flash 4 MHz 8/8 and 12 MHz 16/16, probe-rs benchmark, single-class WinUSB binding (problem code 0, service WINUSB).

## Placement advice for the Step() thread on ESP32-S3 (measured, 2026-10-16 night)
Putting the Step() thread on the same core as the USB interrupt costs low-clock throughput (block read 4 MHz 4 in flight 289 -> 183 KiB/s, OpenOCD dump 266 -> 185). With the SWD dedicated-GPIO bundles created on the other core and Step() pinned there (USB on core 0, SWD + Step on core 1): 291 KiB/s and 261 KiB/s again, and OpenOCD 12 MHz dump 637-643 vs 578 (ISR version). Suggest documenting: on dual-core targets run Step() on a different core than the USB interrupt; on ESP32-S3 create the dedicated-GPIO pins on that core. Details in results-20261016.md (section "Dual-core placement").

## Writes interleaved with small commands lose the receive/execute overlap (2026-10-17)
Block writes (DAP_TransferBlock, 254 words, 1021-byte request) at 12 MHz, overlapped client, 2 and 4 in flight: back-to-back block writes run at 775 KiB/s (ISR version, a1d233ec) / 564 (52fa2e92 Step thread, SWD on core 1); with a 9-byte TAR write command between every pair they drop to 434 / 332 KiB/s (2.29 ms vs 1.28 ms per block = execution time + receive time added instead of overlapped). Raising the command buffer count from 2 to 4 in a1d233ec does not help (434). Reads do not have this problem (the big item is the response). Repro: `D:/Codex/tmp/rml/xrwonly.py`. Pure OUT throughput (1016-byte requests, no SWD work) is ~985 KiB/s, so the bus is not the limit. Suspect: something other than the slot count gates reception of the next large command while a small one waits (window/outstanding count, or the point where the OUT is re-armed).

## Root causes of slow writes - for the libxr session (2026-10-17, measured on 52fa2e92 with an instrumented copy)
1. Command pool of 2 slots: with TAR + big write alternating, ARM_OUT is blocked (no free command slot) while the big write executes, so receive and execution do not overlap: TAR + block write pairs at 4 in flight 332 KiB/s. Command pool of 4 (= window): 565 KiB/s.
2. Two-step receive costs half of the OUT throughput: pure OUT (1016-byte commands, no SWD) 490 KiB/s vs 985 on a1d233ec. With the first receive asking for the whole DAP packet: 777 KiB/s, and with both changes TAR + block write pairs reach 783 KiB/s. The two-step receive exists to end commands whose length is a multiple of 64; a replacement that keeps whole-packet receive for the normal case (e.g. let a SOF-time check end a transfer whose received bytes already form a complete command) avoids the extra gap.
3. Device-side in-flight histogram (instrumentation in the copy): probe-rs 2 in flight always, OpenOCD 2-4 (reads) / 2 (writes).
Instrumented copy: D:/Codex/tmp/xrdap-s3/libxr-trace (trace buffer `LibXR::USB::g_dap_trace`); repro scripts xrwonly.py, xrout2_pipe.py, xrwrite_pipe.py in D:/Codex/tmp/rml.
