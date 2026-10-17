#pragma once

// Transports: USB Bulk v2 is available from boot; once Wi-Fi is connected the USB stack is
// deinitialised and the same DAP instance is served over CMSIS-DAP-over-TCP (port 3241,
// "DAP\0" framing, same as OpenOCD cmsis_dap_tcp.c).
#ifndef XRDAP_ENABLE_USB
#define XRDAP_ENABLE_USB 1
#endif
// 1 = boot into a measurement loop that only emits SWCLK/SWDIO bursts for a logic analyzer.
#ifndef XRDAP_CLOCK_TEST
#define XRDAP_CLOCK_TEST 0
#endif
// 1 = boot into a loop-timing experiment (cycles per iteration of several delay loops).
#ifndef XRDAP_LOOP_TEST
#define XRDAP_LOOP_TEST 0
#endif
// 0 = never start Wi-Fi/TCP: the probe stays a USB CMSIS-DAP device (for wired measurements).
#ifndef XRDAP_WIFI_ENABLE
#define XRDAP_WIFI_ENABLE 1
#endif
// 1 = boot into an on-device benchmark of DAP command execution (no USB, no Wi-Fi).
#ifndef XRDAP_LOCAL_BENCH
#define XRDAP_LOCAL_BENCH 0
#endif
// 1 = boot into a free-running SWD stress loop for an oscilloscope (no USB, no Wi-Fi, no PC):
//   writes and reads back target SRAM at XRDAP_SCOPE_KHZ and steps SWCLK/SWDIO drive strength
//   through default / weakest / strongest / stronger on each BOOT press (GPIO6/GPIO7 show the level).
#ifndef XRDAP_SCOPE_TEST
#define XRDAP_SCOPE_TEST 0
#endif
#ifndef XRDAP_SCOPE_START_LEVEL
#define XRDAP_SCOPE_START_LEVEL 0
#endif
#ifndef XRDAP_SCOPE_KHZ
#define XRDAP_SCOPE_KHZ 2000
#endif
// 1 = periodically log DAP class internals and EP1 registers (debug only; exposes privates).
#ifndef XRDAP_DEBUG_STATE
#define XRDAP_DEBUG_STATE 0
#endif
// DAP packet size reported to the host (also the USB command/response slot size).
#ifndef XRDAP_DAP_PACKET_SIZE
#define XRDAP_DAP_PACKET_SIZE 1024
#endif
// 1 = USB device with only the DAP v2 interface (no CDC), to check WinUSB binding of a single-class device.
// 1 = log how many SWD transfers ran in interrupt context (USB path).
#ifndef XRDAP_ISR_PROBE
#define XRDAP_ISR_PROBE 0
#endif
// 1 = prototype: run DAP commands in a task pinned to core 0 instead of in the USB interrupt.
#ifndef XRDAP_DAP_WORKER
#define XRDAP_DAP_WORKER 0
#endif
#ifndef XRDAP_DAP_WORKER_PRIO
#define XRDAP_DAP_WORKER_PRIO 10
#endif
// 1 = boot into the hand-written SWD engine sweep (drive level x nops per edge), no USB, no Wi-Fi.
#ifndef XRDAP_FAST_BENCH
#define XRDAP_FAST_BENCH 0
#endif
// 1 = use the hand-written SWD engine (fast_swd.hpp) for the DAP instead of SwdGeneralGPIO.
#ifndef XRDAP_FAST_SWD
#define XRDAP_FAST_SWD 0
#endif
// FreeRTOS priority of the thread that runs the DAP commands (Step() loop).
#ifndef XRDAP_STEP_PRIO
#define XRDAP_STEP_PRIO 10
#endif
// Core on which the SWD pins are created and the DAP Step() thread / TCP server run. The USB interrupt, the
// Wi-Fi task and the lwIP task are on core 0, so core 1 is the default (measured: Step on the USB core costs
// 6-15 % on small commands and 30+ % on low-clock block reads).
#ifndef XRDAP_SWD_CORE
#define XRDAP_SWD_CORE 1
#endif
// 0 for libxr versions where the DAP class executes commands itself (no Step()).
#ifndef XRDAP_STEP_THREAD
#define XRDAP_STEP_THREAD 1
#endif
// 1 = print the DAP receive/execute/send timeline (needs the instrumented libxr copy).
#ifndef XRDAP_DAP_TRACE
#define XRDAP_DAP_TRACE 0
#endif
// 1 = log DWC2 device registers whenever the watched ones change (hot-plug debugging).
#ifndef XRDAP_USB_REGDUMP
#define XRDAP_USB_REGDUMP 0
#endif
#ifndef XRDAP_USB_DBGCNT
#define XRDAP_USB_DBGCNT 0
#endif
#ifndef XRDAP_TCP_ACKLOG
#define XRDAP_TCP_ACKLOG 0
#endif
#ifndef XRDAP_SINGLE_CLASS
#define XRDAP_SINGLE_CLASS 0
#endif
#ifndef XRDAP_USB_PID
#define XRDAP_USB_PID 0x4A01
#endif
#ifndef XRDAP_TCP_PORT
#define XRDAP_TCP_PORT 3241
#endif
