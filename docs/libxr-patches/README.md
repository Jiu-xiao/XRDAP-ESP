# libxr patches from the ESP32-S3 XR-DAP bring-up (2026-10-16)

Base: `libxr-c`, branch `feat/usb-step5-c-esp`, HEAD `52862a09`. Apply in order with `git apply`
(checked: applies cleanly in order 0001 -> 0002 -> 0003 and reproduces the working tree).
Nothing here is committed or pushed. Test hardware: ESP32-S3 N8R8 @ 240 MHz, target STM32G431KB
(Cortex-M4, halted, IWDG/WWDG frozen), SWCLK=GPIO5, SWDIO=GPIO4, bare Dupont wires.

## 0001 SwdGeneralGPIO::BusyLoop without `volatile`
- **Problem.** `volatile uint32_t sink = loops; while (sink--) {}` makes every iteration read and write the
  stack with `memw`: `memw / l32i / addi / memw / s32i / bnez`.
- **Measured (S3, interrupts off, IRAM, cycle counter, same harness for all):**
  `volatile` 9.00 cycles/iteration with 12 cycles fixed cost at N=0; `atomic_signal_fence` loop 4.00 (3 fixed);
  register loop with empty asm 4.00; `atomic_thread_fence` 5.00; Xtensa `loop` instruction 1.00.
  A plain local counter is deleted by GCC 14.2 -O2 (function body is just `entry/retw.n`).
- **Change.** Counter lives in a register and `std::atomic_signal_fence(std::memory_order_seq_cst)` keeps the
  loop. Standard library only; it is a compiler barrier on GCC/Clang, not a language guarantee.
- **Not measured.** The effect of the new loop on the actual SWCLK of the stock delay path. Only the loop itself
  was timed.
- **Impact.** Every board whose `loops_per_us` was tuned against the old loop (e.g. XRDAP `kSwdDelayLoopsPerUs`)
  gets a faster real clock and needs recalibration. `jtag_general_gpio.hpp` has an identical `BusyLoop` and is
  not changed here.
- **Context.** With the stock delay path the actual SWCLK on S3 (logic analyzer, 46-clock DPIDR reads) was
  0.85 / 1.47 / 1.92 / 2.27 / 2.27 MHz for requests 1/2/3/4/5 MHz and a flat 2.78 MHz for 6, 8 and 10 MHz
  (`half_period_loops_ = ceil(loops_per_us * half_ns / 1000)` is never 1, so the smallest non-zero delay is 2
  iterations).

## 0002 DapLinkV2Class::ExecuteExternal / RequestAbort
- **Why.** CMSIS-DAP over TCP (OpenOCD `cmsis_dap_tcp.c` framing) needs the command processor without USB
  endpoints; `ProcessOneCommand` is private.
- **Change.** `ExecuteExternal(req, len, resp, cap, out_len)` runs one command in the caller's thread; delayed
  commands (DAP_Delay, ResetTarget, SWJ_Pins wait, also inside a batch) are waited for inside the call.
  `CompleteResponse` hands the finished response to the caller while `external_active_` is set.
  `RequestAbort()` sets the transfer-abort flag. Adds `#include "thread.hpp"`.
- **Decision (maintainer).** No ownership flag: the application deinitialises USB before using this entry.
- **Tested.** Wi-Fi TCP server on S3 (port 3241), 84-word scattered reads and the on-device benchmark.
  `RequestAbort()` from another thread is not tested.

## 0003 Append a ZLP only when the response is shorter than the DAP packet
- **Problem.** `KickResponse` appends a ZLP when `len % MaxPacketSize == 0`. A TransferBlock read of 255 words
  answers 1024 bytes, equal to the DAP packet size. The host reads with a DAP-packet-sized buffer, completes when
  it is full and never takes the ZLP. The ZLP stays armed (`DIEPTSIZ = 0x20080000`), the response is not retired,
  and one of the 4 queue slots stays occupied until the next read.
- **Symptom.** Four commands in flight (TAR write + 1 KB block read, twice) stall the OUT pipe
  (`WinUsb_WritePipe` times out). State at the stall: `q_cnt=4 rd_act=0 wr_act=1 cmd_pend=0`, EP1 OUT not armed.
  A hard reset recovers (no replug).
- **Change.** `need_zlp = (len % mps == 0) && (len < GetDapPacketSize())`.
- **Result.** Same board and host: four commands in flight pass with 0 errors (block read 452 KiB/s at 12.5 MHz).
  Six in flight still stalls, which is above the device-reported `PACKET_COUNT` (4).
- **Conflicts with an earlier decision.** "Append a ZLP for integer multiples of the packet size" was kept on
  purpose. The ZLP is still sent for multiples shorter than the DAP packet size.
- **Not tested.** OpenOCD, pyOCD, Keil or other hosts; only a WinUSB client.

## Reproduction and data
- Results: `D:\Codex\tmp\rml\results-20261016.md`; findings list: `..\libxr-issues-20261016.md`.
- Scripts: `D:\Codex\tmp\rml\xrblock_usb.py` (block read; env `NPAIR`, `PAD`), `xrbench_usb.py` (scattered read),
  `D:\Codex\tmp\xrdap-s3\app-c` (firmware; debug-state build prints queue/endpoint state every 500 ms).
