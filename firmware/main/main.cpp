// XR ESP32-S3 CMSIS-DAP probe on libxr (integration branch feat/usb-step5-c-esp).
//   Boot:      USB Bulk v2 (+ CDC) so the probe works with no network.
//   Wi-Fi up:  USB is deinitialised, the same DAP instance is served over TCP 3241.
#include <atomic>
#include <cstdint>
#include <cstring>

#include "xrdap_config.hpp"
#include "libxr.hpp"
#include "soc/soc.h"
#if XRDAP_DEBUG_STATE
#define private public
#define protected public
#endif
#include "daplink_v2_profile_swd.hpp"
#if XRDAP_DEBUG_STATE
#undef private
#undef protected
#endif
#include "dap_tcp.hpp"
#if XRDAP_FAST_BENCH || XRDAP_FAST_SWD
#include "fast_swd.hpp"
#endif
#include "debug/swd_general_gpio.hpp"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "esp_timer.h"
#include "esp_timebase.hpp"
#include "profiled_swd.hpp"
#include "s3_dedic_gpio.hpp"
#include "esp_cpu.h"
#include "thread.hpp"
#include "wifi_sta.hpp"

#if XRDAP_ENABLE_USB
#include "cdc_uart.hpp"
#include "esp_usb_dev.hpp"
#endif

namespace
{
constexpr int kSwclkGpio = 5;  // same wiring as RMLink: GPIO5 = SWCLK, GPIO4 = SWDIO
constexpr int kSwdioGpio = 4;

#if XRDAP_ENABLE_USB
// ESP32-S3 has IN endpoints EP0 + EP1..EP4 only. Layout: EP1 = DAP both directions,
// EP2 = CDC data both directions, EP3 IN = CDC notification. 64-byte aligned buffers
// (DMA cache line), each direction block at least twice the maximum packet.
alignas(64) uint8_t ep0_buffer[128];  // EP0 OUT and IN each take one half (>= max packet 64)
alignas(64) uint8_t ep1_dap[4096];
alignas(64) uint8_t ep2_cdc[1024];
alignas(64) uint8_t ep3_comm[128];
#endif
}  // namespace

#if XRDAP_LOOP_TEST
namespace
{
__attribute__((noinline)) void LoopEmpty(uint32_t) {}
__attribute__((noinline)) void LoopVolatile(uint32_t n)
{
  volatile uint32_t s = n;
  while (s--)
  {
  }
}
__attribute__((noinline)) void LoopSignalFence(uint32_t n)
{
  uint32_t s = n;
  while (s--)
  {
    std::atomic_signal_fence(std::memory_order_seq_cst);
  }
}
__attribute__((noinline)) void LoopThreadFence(uint32_t n)
{
  uint32_t s = n;
  while (s--)
  {
    std::atomic_thread_fence(std::memory_order_seq_cst);
  }
}
__attribute__((noinline)) void LoopAsmReg(uint32_t n)
{
  for (uint32_t i = n; i; --i)
  {
    asm volatile("" : "+r"(i));
  }
}
__attribute__((noinline)) void LoopZeroOverhead(uint32_t n)
{
  if (n)
  {
    asm volatile("loop %0, 1f\n\tnop\n1:" ::"r"(n) : "memory");
  }
}

using LoopFn = void (*)(uint32_t);
uint32_t CyclesPerCall(LoopFn f, uint32_t n, uint32_t reps)
{
  portDISABLE_INTERRUPTS();
  const uint32_t t0 = esp_cpu_get_cycle_count();
  for (uint32_t r = 0; r < reps; ++r)
  {
    f(n);
  }
  const uint32_t t1 = esp_cpu_get_cycle_count();
  portENABLE_INTERRUPTS();
  return (t1 - t0) / reps;
}

void RunLoopExperiment()
{
  struct Variant
  {
    const char* name;
    LoopFn fn;
  };
  const Variant variants[] = {{"empty call", LoopEmpty},
                              {"volatile counter", LoopVolatile},
                              {"signal_fence", LoopSignalFence},
                              {"thread_fence", LoopThreadFence},
                              {"asm reg (addi/bnez)", LoopAsmReg},
                              {"zero-overhead loop", LoopZeroOverhead}};
  const uint32_t ns[] = {0, 1, 2, 10, 100, 1000};
  ESP_LOGI("looptest", "CPU %u MHz. cycles per call (includes the indirect call), reps averaged",
           (unsigned)CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ);
  ESP_LOGI("looptest", "%-22s N=0   N=1   N=2   N=10  N=100 N=1000 | cyc/iter(100->1000)", "variant");
  uint32_t empty0 = 0;
  for (const Variant& v : variants)
  {
    uint32_t c[6];
    for (int i = 0; i < 6; ++i)
    {
      c[i] = CyclesPerCall(v.fn, ns[i], ns[i] >= 1000 ? 100 : 1000);
    }
    if (empty0 == 0)
    {
      empty0 = c[0];
    }
    ESP_LOGI("looptest", "%-22s %-5u %-5u %-5u %-5u %-5u %-6u | %.2f   fixed(N=0)-empty=%d", v.name,
             (unsigned)c[0], (unsigned)c[1], (unsigned)c[2], (unsigned)c[3], (unsigned)c[4],
             (unsigned)c[5], (double)(c[5] - c[4]) / 900.0, (int)c[0] - (int)empty0);
  }
}
}  // namespace
#endif

#if XRDAP_LOCAL_BENCH || XRDAP_SCOPE_TEST
// Not inlined: keeps the whole DAP command path out of the large benchmark function.
template <typename D>
__attribute__((noinline)) uint16_t ExecOnce(D& dap, const uint8_t* req, uint16_t len,
                                            uint8_t* rs, uint16_t cap)
{
  uint16_t n = 0;
  (void)dap.ExecuteExternal(req, len, rs, cap, n);
  return n;
}
#endif

// Measures the cost of the same volatile countdown loop SwdGeneralGPIO uses for its half-period
// delay, so loops_per_us follows the real CPU clock instead of a guess.
static uint32_t CalibrateLoopsPerUs()
{
  constexpr uint32_t kLoops = 200000U;
  volatile uint32_t sink = kLoops;
  const uint32_t t0 = esp_cpu_get_cycle_count();
  while (sink--)
  {
  }
  const uint32_t cycles = esp_cpu_get_cycle_count() - t0;
  const uint32_t cpu_mhz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ;
  return static_cast<uint32_t>((static_cast<uint64_t>(kLoops) * cpu_mhz) / cycles);
}

using SwdLink = LibXR::Debug::SwdGeneralGPIO<XrDap::S3DedicGpio, XrDap::S3DedicGpio>;

struct SwdSet
{
  XrDap::S3DedicGpio swclk{kSwclkGpio, /*bidirectional=*/false, /*pull_up=*/false};
  XrDap::S3DedicGpio swdio{kSwdioGpio, /*bidirectional=*/true, /*pull_up=*/true};
  // loops_per_us = 0 keeps SwdGeneralGPIO on its no-delay path; SWCLK pacing is done by the
  // cycle-counter edge spin in S3DedicGpio (set from ProfiledSwd::SetClockHz).
  SwdLink swd{swclk, swdio, 0u};
#if XRDAP_FAST_SWD
  XrDap::FastSwd pswd{swclk, swdio, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U};
#else
  XrDap::ProfiledSwd pswd{swd, swclk, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U};
#endif
};

static SwdSet& GetSwdSet()
{
  static SwdSet set;
  return set;
}

#if XRDAP_USB_REGDUMP && XRDAP_USB_DBGCNT
extern "C" volatile uint32_t xr_usb_dbg[8];
#endif
extern "C" void app_main(void)
{
  static LibXR::ESP32Timebase timebase;
  LibXR::PlatformInit(3, 8192);
#if XRDAP_LOOP_TEST
  RunLoopExperiment();
  for (;;)
  {
    LibXR::Thread::Sleep(1000);
  }
#endif
  ESP_LOGI("xrdap", "boot: platform init done");

  // The dedicated-GPIO bundles are usable only on the core that creates them. All SWD objects are
  // created in GetSwdSet(), on core XRDAP_SWD_CORE, and the Step() thread runs on the same core.
#if XRDAP_SWD_CORE != 0
  {
    static volatile bool swd_ready = false;
    xTaskCreatePinnedToCore(
        [](void*)
        {
          (void)GetSwdSet();
          swd_ready = true;
          vTaskDelete(nullptr);
        },
        "swdinit", 4096, nullptr, 5, nullptr, XRDAP_SWD_CORE);
    while (!swd_ready)
    {
      vTaskDelay(1);
    }
  }
#endif
  SwdSet& swd_set = GetSwdSet();
  auto& swclk = swd_set.swclk;
  auto& swdio = swd_set.swdio;
  auto& swd = swd_set.swd;
  auto& pswd = swd_set.pswd;
  (void)swclk;
  (void)swdio;
  (void)swd;
  ESP_LOGI("xrdap", "boot: swd objects on core %d", (int)XRDAP_SWD_CORE);
#if XRDAP_FAST_SWD
  using SwdForDap = XrDap::FastSwd;
#else
  using SwdForDap = XrDap::ProfiledSwd;
#endif
  // DAP packet size advertised to the host. With USB full speed (64-byte packets) a command whose
  // length is an exact multiple of 64 and shorter than the DAP packet size has no short packet to
  // end it, so a larger size stalls the OUT pipe (seen with OpenOCD flash writes). 64 avoids it.
  using Dap = LibXR::USB::DapLinkV2Class<SwdForDap, XRDAP_DAP_PACKET_SIZE, 4u,
                                         XRDAP_DAP_PACKET_SIZE>;
  static Dap dap(LibXR::USB::Endpoint::EPNumber::EP1, LibXR::USB::Endpoint::EPNumber::EP1,
                 pswd, nullptr);
#if !XRDAP_FAST_SWD
  XrDap::g_dump_hook = [](void* p) { static_cast<XrDap::ProfiledSwd*>(p)->Dump("xrdap_swd"); };
  XrDap::g_dump_arg = &pswd;
#endif
  ESP_LOGI("xrdap", "boot: dap object");
#if XRDAP_ENABLE_USB && XRDAP_STEP_THREAD
  {
    // libxr-c runs DAP commands in an application thread: it calls Step() in a loop. The thread is
    // pinned to core 0 because dedicated-GPIO instructions only work on the core that created them.
    static Dap* step_dap = &dap;
    xTaskCreatePinnedToCore(
        [](void*)
        {
          for (;;)
          {
            (void)step_dap->Step();
          }
        },
        "dapstep", 8192, nullptr, XRDAP_STEP_PRIO, nullptr, XRDAP_SWD_CORE);
  }
#endif
#if XRDAP_DAP_WORKER
  {
    // PROTOTYPE: DAP commands run in a task pinned to core 0 (dedicated GPIO lives there) instead
    // of inside the USB interrupt that owns the class service.
    static SemaphoreHandle_t sem = xSemaphoreCreateBinary();
    dap.SetExecutor(
        [](void* c, bool in_isr)
        {
          auto s = static_cast<SemaphoreHandle_t>(c);
          if (in_isr)
          {
            BaseType_t woken = pdFALSE;
            xSemaphoreGiveFromISR(s, &woken);
            if (woken == pdTRUE)
            {
              portYIELD_FROM_ISR();
            }
          }
          else
          {
            xSemaphoreGive(s);
          }
        },
        sem);
    static Dap* dap_ptr = &dap;
    xTaskCreatePinnedToCore(
        [](void*)
        {
          for (;;)
          {
            xSemaphoreTake(sem, portMAX_DELAY);
            dap_ptr->ExecutorStep();
          }
        },
        "dapexec", 8192, nullptr, XRDAP_DAP_WORKER_PRIO, nullptr, 0);
  }
#endif

#if XRDAP_FAST_BENCH
  {
    // Free-running sweep of the hand-written SWD engine. For every pad drive level and pacing step the
    // link is stressed with plain transfers (DP IDCODE reads, DP SELECT writes, AP TAR writes, 255-word
    // block reads) at full speed; errors are classified, the real transfer rate is measured with the
    // CPU cycle counter. Connection setup always runs at a slow, safe clock.
    using LibXR::Debug::SwdProtocol::Port;
    using LibXR::Debug::SwdProtocol::Ack;
    using LibXR::Debug::SwdProtocol::Request;
    using LibXR::Debug::SwdProtocol::Response;
    static XrDap::FastSwd fs(swclk, swdio, CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U);
    constexpr double cpu_hz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000.0;
    struct Stat { uint32_t n = 0, ok = 0, wait = 0, fault = 0, noack = 0, proto = 0, par = 0, wrong = 0; };
    static uint32_t truth[256];
    static uint32_t got[256];
    auto one = [&](Port port, bool rnw, uint8_t a, uint32_t wdata, Response& r, Stat& s) -> bool
    {
      Request q;
      q.port = port;
      q.rnw = rnw;
      q.addr2b = a;
      q.wdata = wdata;
      (void)fs.Transfer(q, r);
      ++s.n;
      switch (r.ack)
      {
        case Ack::OK:
          if (rnw && !r.parity_ok) { ++s.par; return false; }
          ++s.ok;
          return true;
        case Ack::WAIT: ++s.wait; return false;
        case Ack::FAULT: ++s.fault; return false;
        case Ack::NO_ACK: ++s.noack; return false;
        default: ++s.proto; return false;
      }
    };
    Stat dummy;
    Response rr;
    uint32_t rcc_cfgr = 0;
    auto slow = [&]() { fs.SetHoldCycles(80); fs.SetOeSettleCycles(30); fs.SetReleaseSettleCycles(0); };
    auto retry_ok = [&](Port port, bool rnw, uint8_t a, uint32_t w) -> bool
    {
      for (int i = 0; i < 200; ++i)
      {
        if (one(port, rnw, a, w, rr, dummy)) { return true; }
        if (rr.ack == Ack::FAULT) { one(Port::DP, false, 0, 0x1EU, rr, dummy); }
      }
      return false;
    };
    auto connect = [&]() -> bool
    {
      slow();
      (void)fs.EnterSwd();
      if (!retry_ok(Port::DP, true, 0, 0) || rr.rdata != 0x2BA01477U) { return false; }
      retry_ok(Port::DP, false, 0, 0x1EU);        // ABORT: clear sticky errors
      retry_ok(Port::DP, false, 2, 0U);           // SELECT 0
      retry_ok(Port::DP, false, 1, 0x50000000U);  // CTRL/STAT power up
      vTaskDelay(pdMS_TO_TICKS(10));
      retry_ok(Port::AP, false, 0, 0x23000052U);  // CSW: 32-bit, auto-increment
      retry_ok(Port::AP, false, 1, 0xE000EDF0U); retry_ok(Port::AP, false, 3, 0xA05F0003U);  // halt
      retry_ok(Port::AP, false, 1, 0xE0042008U); retry_ok(Port::AP, false, 3, 0x1800U);      // freeze WDG
#ifdef XRDAP_FAST_TARGET_PLL
      // run the target core from the PLL (160 MHz) instead of the 16 MHz reset clock
      retry_ok(Port::AP, false, 1, 0x40022000U); retry_ok(Port::AP, false, 3, 0x40604U);     // FLASH_ACR: 4 wait states
      retry_ok(Port::AP, false, 1, 0x4002100CU); retry_ok(Port::AP, false, 3, 0x01005032U);  // PLLCFGR: HSI16/4*80/2
      retry_ok(Port::AP, false, 1, 0x40021000U); retry_ok(Port::AP, false, 3, 0x01000500U);  // CR: PLLON
      vTaskDelay(pdMS_TO_TICKS(5));
      retry_ok(Port::AP, false, 1, 0x40021008U); retry_ok(Port::AP, false, 3, 0x7U);        // CFGR: SW = PLL
      vTaskDelay(pdMS_TO_TICKS(5));
#endif
      retry_ok(Port::AP, false, 1, 0x40021008U);                                             // read back RCC_CFGR
      retry_ok(Port::AP, true, 3, 0U);
      retry_ok(Port::DP, true, 3, 0U);
      rcc_cfgr = rr.rdata;
      return true;
    };
    auto block_read = [&](uint32_t addr, uint32_t* out, Stat& s) -> void
    {
      Response r;
      one(Port::AP, false, 1, addr, r, s);
      for (int i = 0; i <= 255; ++i)
      {
        if (i < 255) { one(Port::AP, true, 3, 0, r, s); } else { one(Port::DP, true, 3, 0, r, s); }
        if (i > 0) { out[i - 1] = r.rdata; }
      }
    };
    struct Step { bool dyn; int v; };
    static const Step kSteps[] = {{true, 160}, {true, 80}, {true, 40}, {true, 24}, {true, 16}, {true, 10}, {true, 6},
                                  {false, 10}, {false, 8}, {false, 6}, {false, 5}, {false, 4}, {false, 3},
                                  {false, 2}, {false, 1}, {false, 0}};
    static const char kName[] = {'A', 'B', 'C'};
    bool have_truth = false;
    for (;;)
    {
      gpio_set_drive_capability(static_cast<gpio_num_t>(kSwclkGpio), GPIO_DRIVE_CAP_1);
      gpio_set_drive_capability(static_cast<gpio_num_t>(kSwdioGpio), GPIO_DRIVE_CAP_1);
      for (const Step& st : kSteps)
      {
        if (!connect())
        {
          ESP_LOGI("fastbench", "%s %3d: connect failed", st.dyn ? "spin" : "nops", st.v);
          continue;
        }
        (void)have_truth;
        uint32_t ok[3] = {0, 0, 0};
        double cyc[3] = {0, 0, 0};
        uint32_t kinds[3][4] = {};
        const int kN = 3000;
        for (int mode = -1; mode <= 1; ++mode)
        {
          if (!connect()) { continue; }   // line reset: a failed position must not poison the next one
          fs.SetOeSettleCycles(16);
          fs.SetReleaseSettleCycles(0);
          fs.SetPos(st.dyn, st.v, mode);
          Response r;
          Stat s;
          const uint32_t t0 = esp_cpu_get_cycle_count();
          for (int i = 0; i < kN; ++i)
          {
            if (one(Port::DP, true, 0, 0, r, s) && r.rdata != 0x2BA01477U) { ++s.wrong; }
          }
          cyc[mode + 1] = (double)(esp_cpu_get_cycle_count() - t0) / kN;
          ok[mode + 1] = s.ok - s.wrong;
          kinds[mode + 1][0] = s.wait; kinds[mode + 1][1] = s.fault; kinds[mode + 1][2] = s.proto; kinds[mode + 1][3] = s.noack;
          slow();
          one(Port::DP, false, 0, 0x1EU, r, dummy);
        }
        double full = cyc[0];
        for (int k = 1; k < 3; ++k) { if (cyc[k] > full) { full = cyc[k]; } }
        ESP_LOGI("fastbench", "%s %3d | ~%5.2f MHz (%5.0f cyc) | A after-falling %4u/%d | B before-rising %4u/%d | C after-rising %4u/%d",
                 st.dyn ? "spin" : "nops", st.v, 46.0 * cpu_hz / full / 1e6, full, (unsigned)ok[0], kN,
                 (unsigned)ok[1], kN, (unsigned)ok[2], kN);
        vTaskDelay(pdMS_TO_TICKS(20));
      }
      ESP_LOGI("fastbench", "sweep done");
      vTaskDelay(pdMS_TO_TICKS(5000));
    }
  }
#endif

#if XRDAP_LOCAL_BENCH
  {
    // On-device benchmark: DAP commands go straight to the command processor (ExecuteExternal),
    // so the numbers contain SWD + command processing only, no USB and no network.
    static uint8_t rq[1100];
    static uint8_t rs[1100];
    auto le32 = [](uint8_t* p, uint32_t v)
    {
      p[0] = v;
      p[1] = v >> 8;
      p[2] = v >> 16;
      p[3] = v >> 24;
    };
    auto exec = [&](const uint8_t* req, uint16_t len) -> uint16_t
    { return ExecOnce(dap, req, len, rs, sizeof(rs)); };
    auto xfer1 = [&](uint8_t request, bool has_data, uint32_t data) -> uint8_t
    {
      uint16_t n = 0;
      rq[n++] = 0x05;
      rq[n++] = 0x00;
      rq[n++] = 0x01;
      rq[n++] = request;
      if (has_data)
      {
        le32(rq + n, data);
        n += 4;
      }
      const uint16_t r = exec(rq, n);
      return r >= 3 ? rs[2] : 0xFF;
    };
    auto cpu_hz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U;
    uint8_t one[7] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    for (;;)
    {
      static const uint32_t kHz[] = {1000000, 2000000, 4000000, 6000000, 10000000, 16000000};
      for (uint32_t hz : kHz)
      {
        rq[0] = 0x02; rq[1] = 0x01; (void)exec(rq, 2);                       // DAP_Connect (SWD)
        rq[0] = 0x11; le32(rq + 1, hz); (void)exec(rq, 5);                   // SWJ_Clock
        rq[0] = 0x04; rq[1] = 0; rq[2] = 0x64; rq[3] = 0; rq[4] = 0; rq[5] = 0; (void)exec(rq, 6);
        rq[0] = 0x13; rq[1] = 0x00; (void)exec(rq, 2);                       // SWD configure
        rq[0] = 0x12; rq[1] = 56; memcpy(rq + 2, one, 7); (void)exec(rq, 9);   // line reset
        rq[0] = 0x12; rq[1] = 16; rq[2] = 0x9E; rq[3] = 0xE7; (void)exec(rq, 4);
        rq[0] = 0x12; rq[1] = 56; memcpy(rq + 2, one, 7); (void)exec(rq, 9);
        rq[0] = 0x12; rq[1] = 8; rq[2] = 0; (void)exec(rq, 3);
        const uint8_t a0 = xfer1(0x02, false, 0);                            // DPIDR read
        (void)xfer1(0x00, true, 0x1E);                                       // ABORT clear
        (void)xfer1(0x08, true, 0);                                          // SELECT 0
        (void)xfer1(0x04, true, 0x50000000);                                 // CTRL/STAT power up
        vTaskDelay(pdMS_TO_TICKS(20));
        (void)xfer1(0x01, true, 0x23000052);                                 // CSW 32-bit, auto-inc
        // halt core + freeze watchdogs (target runs an IWDG)
        rq[0] = 0x05; rq[1] = 0; rq[2] = 2;
        rq[3] = 0x05; le32(rq + 4, 0xE000EDF0); rq[8] = 0x0D; le32(rq + 9, 0xA05F0003);
        (void)exec(rq, 13);
        rq[3] = 0x05; le32(rq + 4, 0xE0042008); rq[8] = 0x0D; le32(rq + 9, 0x1800);
        (void)exec(rq, 13);
        vTaskDelay(pdMS_TO_TICKS(20));

        // (a) TransferBlock: TAR write + block read of 255 words, as the PC benchmark does
        const int kBlockReps = 200;
        uint8_t blk[5] = {0x06, 0x00, 0xFF, 0x00, 0x0F};
        uint32_t t0 = esp_cpu_get_cycle_count();
        uint8_t last_ack = 0;
        for (int i = 0; i < kBlockReps; ++i)
        {
          (void)xfer1(0x05, true, 0x1FFF0000);
          (void)exec(blk, sizeof(blk));
          last_ack = rs[3];
        }
        const uint32_t blk_cyc = (esp_cpu_get_cycle_count() - t0) / kBlockReps;

        // (a2) TransferBlock write of 254 words to SRAM, TAR write before each command
        rq[0] = 0x06; rq[1] = 0x00; rq[2] = 254; rq[3] = 0x00; rq[4] = 0x0D;
        memset(rq + 5, 0x5A, 254 * 4);
        t0 = esp_cpu_get_cycle_count();
        uint8_t wack = 0;
        for (int i = 0; i < kBlockReps; ++i)
        {
          (void)xfer1(0x05, true, 0x20003000);
          (void)exec(rq, 5 + 254 * 4);
          wack = rs[3];
        }
        const uint32_t wblk_cyc = (esp_cpu_get_cycle_count() - t0) / kBlockReps;
        ESP_LOGI("localbench-w", "req %2u MHz | block write 254 words: %5u us/cmd = %6.1f KiB/s (ack %u)",
                 (unsigned)(hz / 1000000U), (unsigned)(wblk_cyc / (cpu_hz / 1000000U)),
                 254.0 * 4.0 / 1024.0 / ((double)wblk_cyc / cpu_hz), (unsigned)wack);

        // (b) scattered read: 84 x (TAR write, DRW read) in one DAP_Transfer
        uint16_t n = 0;
        rq[n++] = 0x05; rq[n++] = 0x00; rq[n++] = 168;
        for (int i = 0; i < 84; ++i)
        {
          rq[n++] = 0x05; le32(rq + n, 0x1FFF0000 + 64U * i); n += 4;
          rq[n++] = 0x0F;
        }
        const int kScatReps = 100;
        t0 = esp_cpu_get_cycle_count();
        uint8_t scat_ack = 0;
        for (int i = 0; i < kScatReps; ++i)
        {
          (void)exec(rq, n);
          scat_ack = rs[2];
        }
        const uint32_t scat_cyc = (esp_cpu_get_cycle_count() - t0) / kScatReps;
        ESP_LOGI("localbench",
                 "req %2u MHz | DPIDR ack %u | block255: %5u us/cmd = %6.1f KiB/s (ack %u) | "
                 "scatter84: %5u us/cmd = %6.0f words/s (ack %u)",
                 (unsigned)(hz / 1000000U), (unsigned)a0,
                 (unsigned)(blk_cyc / (cpu_hz / 1000000U)),
                 255.0 * 4.0 / 1024.0 / ((double)blk_cyc / cpu_hz), (unsigned)last_ack,
                 (unsigned)(scat_cyc / (cpu_hz / 1000000U)), 84.0 / ((double)scat_cyc / cpu_hz),
                 (unsigned)scat_ack);
      }
      LibXR::Thread::Sleep(2000);
    }
  }
#endif

#if XRDAP_SCOPE_TEST
  {
    // Free-running oscilloscope stress: connect, halt the target core, then forever write 12 words
    // to SRAM (posted AP writes) and read them back, as OpenOCD's flash loader upload does.
    // Drive strength of SWCLK and SWDIO steps through kDrive[] on each BOOT press; GPIO6/GPIO7 carry
    // the level index (bit0 / bit1) so the scope can be triggered or annotated per phase.
    static uint8_t rq[256];
    static uint8_t rs[256];
    auto le32 = [](uint8_t* p, uint32_t v)
    {
      p[0] = v;
      p[1] = v >> 8;
      p[2] = v >> 16;
      p[3] = v >> 24;
    };
    auto exec = [&](const uint8_t* req, uint16_t len) -> uint16_t
    { return ExecOnce(dap, req, len, rs, sizeof(rs)); };
    auto xfer1 = [&](uint8_t request, bool has_data, uint32_t data) -> uint8_t
    {
      uint16_t n = 0;
      rq[n++] = 0x05;
      rq[n++] = 0x00;
      rq[n++] = 0x01;
      rq[n++] = request;
      if (has_data)
      {
        le32(rq + n, data);
        n += 4;
      }
      const uint16_t r = exec(rq, n);
      return r >= 3 ? rs[2] : 0xFF;
    };
    auto connect = [&]() -> bool
    {
      static const uint8_t one[7] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
      rq[0] = 0x02; rq[1] = 0x01; (void)exec(rq, 2);                         // DAP_Connect (SWD)
      rq[0] = 0x11; le32(rq + 1, XRDAP_SCOPE_KHZ * 1000U); (void)exec(rq, 5);  // SWJ_Clock
      rq[0] = 0x04; rq[1] = 0; rq[2] = 0x64; rq[3] = 0; rq[4] = 0; rq[5] = 0; (void)exec(rq, 6);
      rq[0] = 0x13; rq[1] = 0x00; (void)exec(rq, 2);                         // SWD configure
      rq[0] = 0x12; rq[1] = 56; memcpy(rq + 2, one, 7); (void)exec(rq, 9);   // line reset
      rq[0] = 0x12; rq[1] = 16; rq[2] = 0x9E; rq[3] = 0xE7; (void)exec(rq, 4);
      rq[0] = 0x12; rq[1] = 56; memcpy(rq + 2, one, 7); (void)exec(rq, 9);
      rq[0] = 0x12; rq[1] = 8; rq[2] = 0; (void)exec(rq, 3);
      const uint8_t a0 = xfer1(0x02, false, 0);                              // DPIDR read
      (void)xfer1(0x00, true, 0x1E);                                         // ABORT clear
      (void)xfer1(0x08, true, 0);                                            // SELECT 0
      (void)xfer1(0x04, true, 0x50000000);                                   // power up
      vTaskDelay(pdMS_TO_TICKS(20));
      (void)xfer1(0x01, true, 0x23000052);                                   // CSW 32-bit, auto-inc
      rq[0] = 0x05; rq[1] = 0; rq[2] = 2;                                    // halt, freeze IWDG/WWDG
      rq[3] = 0x05; le32(rq + 4, 0xE000EDF0); rq[8] = 0x0D; le32(rq + 9, 0xA05F0003);
      (void)exec(rq, 13);
      rq[3] = 0x05; le32(rq + 4, 0xE0042008); rq[8] = 0x0D; le32(rq + 9, 0x1800);
      (void)exec(rq, 13);
      return a0 == 1;
    };

    static const gpio_drive_cap_t kDrive[] = {GPIO_DRIVE_CAP_2, GPIO_DRIVE_CAP_0, GPIO_DRIVE_CAP_3,
                                              GPIO_DRIVE_CAP_1};
    constexpr uint32_t kLogMs = 2000;
    constexpr int kWords = 12;
    constexpr uint32_t kBase = 0x20003000;
    gpio_set_direction(GPIO_NUM_6, GPIO_MODE_OUTPUT);
    gpio_set_direction(GPIO_NUM_7, GPIO_MODE_OUTPUT);
    uint32_t seed = 0x12345678;
    uint32_t total_loops[4] = {};
    uint32_t total_errs[4] = {};
    // The BOOT button (GPIO0, active low) steps to the next level; otherwise the level is held.
    gpio_set_direction(GPIO_NUM_0, GPIO_MODE_INPUT);
    gpio_set_pull_mode(GPIO_NUM_0, GPIO_PULLUP_ONLY);
    uint32_t li = XRDAP_SCOPE_START_LEVEL & 3U;
    auto apply = [&]()
    {
      gpio_set_level(GPIO_NUM_6, li & 1U);
      gpio_set_level(GPIO_NUM_7, (li >> 1) & 1U);
      gpio_set_drive_capability(static_cast<gpio_num_t>(kSwclkGpio), kDrive[li]);
      gpio_set_drive_capability(static_cast<gpio_num_t>(kSwdioGpio), kDrive[li]);
      const bool up = connect();
      ESP_LOGI("scope", "level %u: drive cap %d, %u kHz, DPIDR ack %s", (unsigned)li,
               (int)kDrive[li], (unsigned)XRDAP_SCOPE_KHZ, up ? "OK" : "FAIL");
    };
    apply();
    bool pressed = false;
    TickType_t t_log = xTaskGetTickCount();
    for (;;)
    {
      {
        const bool down = gpio_get_level(GPIO_NUM_0) == 0;
        if (down && !pressed)
        {
          vTaskDelay(pdMS_TO_TICKS(30));
          if (gpio_get_level(GPIO_NUM_0) == 0)
          {
            li = (li + 1U) & 3U;
            apply();
          }
        }
        pressed = down;
      }
      if ((int32_t)(xTaskGetTickCount() - t_log) >= (int32_t)pdMS_TO_TICKS(kLogMs))
      {
        t_log = xTaskGetTickCount();
        ESP_LOGI("scope", "level %u | errors/loops by level (default,weak,strongest,stronger): "
                 "%u/%u %u/%u %u/%u %u/%u",
                 (unsigned)li, (unsigned)total_errs[0], (unsigned)total_loops[0],
                 (unsigned)total_errs[1], (unsigned)total_loops[1], (unsigned)total_errs[2],
                 (unsigned)total_loops[2], (unsigned)total_errs[3], (unsigned)total_loops[3]);
      }
      for (int rep = 0; rep < 20; ++rep)
      {
        uint32_t w[kWords];
        for (int i = 0; i < kWords; ++i)
        {
          seed = seed * 1664525U + 1013904223U;
          w[i] = seed;
        }
        bool bad = xfer1(0x05, true, kBase) != 1;                            // TAR
        uint16_t n = 0;
        rq[n++] = 0x06; rq[n++] = 0x00; rq[n++] = kWords; rq[n++] = 0x00; rq[n++] = 0x0D;
        for (int i = 0; i < kWords; ++i)
        {
          le32(rq + n, w[i]);
          n += 4;
        }
        (void)exec(rq, n);                                                   // block write
        bad |= rs[3] != 1;
        bad |= xfer1(0x05, true, kBase) != 1;
        rq[0] = 0x06; rq[1] = 0x00; rq[2] = kWords; rq[3] = 0x00; rq[4] = 0x0F;
        (void)exec(rq, 5);                                                   // block read
        bad |= rs[3] != 1 || (rs[1] | (rs[2] << 8)) != kWords;
        for (int i = 0; !bad && i < kWords; ++i)
        {
          uint32_t v;
          memcpy(&v, rs + 4 + 4 * i, 4);
          bad |= v != w[i];
        }
        ++total_loops[li];
        if (bad)
        {
          ++total_errs[li];
          (void)connect();                                                   // recover and go on
        }
      }
    }
  }
#endif

#if XRDAP_CLOCK_TEST
  {
    // Logic-analyzer measurement loop. Wiring: CH1 -> SWCLK (GPIO5), CH3 -> SWDIO (GPIO4), GND.
    // One repeating frame (~300 ms period), captured at 400 MS/s or 50 MS/s:
    //   group A: SeqWriteBits(64) bursts, libxr BusyLoop delay path
    //   group B: SeqWriteBits(64) bursts, cycle-counter edge pacing
    //   group C: 4 x DPIDR read (Transfer), libxr BusyLoop delay path
    //   group D: 4 x DPIDR read (Transfer), cycle-counter edge pacing
    // Each group has 10 bursts at 1, 2, 3, 4, 5, 6, 8, 10, 12, 16 MHz requested; bursts are ~100 us
    // apart, groups ~2 ms apart.
    static const uint32_t kHz[] = {1000000, 2000000, 3000000, 4000000,  5000000,
                                   6000000, 8000000, 10000000, 12000000, 16000000};
    // All ones = a line reset: harmless to the target, unlike an arbitrary bit pattern.
    static const uint8_t kPattern[8] = {0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF};
    const uint32_t loops = CalibrateLoopsPerUs();
    static SwdLink swd_delay(swclk, swdio, loops);
    const uint32_t cpu_hz = CONFIG_ESP_DEFAULT_CPU_FREQ_MHZ * 1000000U;
    auto spin_us = [cpu_hz](uint32_t us)
    {
      const uint32_t t0 = esp_cpu_get_cycle_count();
      const uint32_t cycles = static_cast<uint32_t>((static_cast<uint64_t>(cpu_hz) * us) / 1000000U);
      while ((esp_cpu_get_cycle_count() - t0) < cycles)
      {
      }
    };
    // Bring the target's SWD port up once so DPIDR reads are answered, and report the result.
    swclk.SetEdgeSpinCycles(0);
    swd_delay.SetClockHz(1000000);
    (void)swd_delay.EnterSwd();
    {
      uint32_t id = 0;
      LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
      (void)swd_delay.DpRead(LibXR::Debug::SwdProtocol::DpReadReg::IDCODE, id, ack);
      ESP_LOGI("clktest", "DPIDR=%08x ack=%d loops_per_us=%u", (unsigned)id, (int)ack,
               (unsigned)loops);
    }
    static uint32_t ok_count[2];
    auto reads = [](auto& link, uint32_t slot)
    {
      for (int n = 0; n < 4; ++n)
      {
        uint32_t id = 0;
        LibXR::Debug::SwdProtocol::Ack ack = LibXR::Debug::SwdProtocol::Ack::PROTOCOL;
        (void)link.DpRead(LibXR::Debug::SwdProtocol::DpReadReg::IDCODE, id, ack);
        if (ack == LibXR::Debug::SwdProtocol::Ack::OK && id == 0x2ba01477U)
        {
          ++ok_count[slot];
        }
      }
    };
    for (;;)
    {
      swclk.SetEdgeSpinCycles(0);
      for (uint32_t hz : kHz)  // group A
      {
        swd_delay.SetClockHz(hz);
        portDISABLE_INTERRUPTS();
        (void)swd_delay.SeqWriteBits(64, kPattern);
        portENABLE_INTERRUPTS();
        spin_us(100);
      }
      spin_us(2000);
      for (uint32_t hz : kHz)  // group B
      {
        (void)pswd.SetClockHz(hz);
        portDISABLE_INTERRUPTS();
        (void)pswd.SeqWriteBits(64, kPattern);
        portENABLE_INTERRUPTS();
        spin_us(100);
      }
      spin_us(2000);
      swclk.SetEdgeSpinCycles(0);
      for (uint32_t hz : kHz)  // group C
      {
        swd_delay.SetClockHz(hz);
        portDISABLE_INTERRUPTS();
        (void)swd_delay.EnterSwd();  // line reset + JTAG->SWD at this clock (own burst)
        portENABLE_INTERRUPTS();
        spin_us(100);
        portDISABLE_INTERRUPTS();
        reads(swd_delay, 0);
        portENABLE_INTERRUPTS();
        spin_us(100);
      }
      spin_us(2000);
      for (uint32_t hz : kHz)  // group D
      {
        (void)pswd.SetClockHz(hz);
        portDISABLE_INTERRUPTS();
        (void)pswd.EnterSwd();
        portENABLE_INTERRUPTS();
        spin_us(100);
        portDISABLE_INTERRUPTS();
        reads(pswd, 1);
        portENABLE_INTERRUPTS();
        spin_us(100);
      }
      ESP_LOGI("clktest", "frame: DPIDR reads OK  C(delay)=%u/40  D(edge-paced)=%u/40",
               (unsigned)ok_count[0], (unsigned)ok_count[1]);
      ok_count[0] = ok_count[1] = 0;
      LibXR::Thread::Sleep(300);
    }
  }
#endif

#if XRDAP_ENABLE_USB
#if XRDAP_SINGLE_CLASS
  static constexpr auto kLang = LibXR::USB::DescriptorStrings::MakeLanguagePack(
      LibXR::USB::DescriptorStrings::Language::EN_US, "XRobot", "XR-DAP-S3",
      "XRDAP-S3-0001");
  static LibXR::ESP32USBDevice usb(
      {
          {LibXR::RawData{ep0_buffer, sizeof(ep0_buffer)}},
          {LibXR::RawData{ep1_dap, sizeof(ep1_dap)}},
      },
      LibXR::USB::DeviceDescriptor::PacketSize0::SIZE_64, 0x303A, XRDAP_USB_PID, 0x0100,
      {&kLang}, {{&dap}});
#else
  static LibXR::USB::CDCUart cdc(LibXR::USB::Endpoint::EPNumber::EP2,
                                 LibXR::USB::Endpoint::EPNumber::EP2,
                                 LibXR::USB::Endpoint::EPNumber::EP3, 1024, 1024, 8);
  static constexpr auto kLang = LibXR::USB::DescriptorStrings::MakeLanguagePack(
      LibXR::USB::DescriptorStrings::Language::EN_US, "XRobot", "XR-DAP-S3",
      "XRDAP-S3-0001");
  static LibXR::ESP32USBDevice usb(
      {
          {LibXR::RawData{ep0_buffer, sizeof(ep0_buffer)}},
          {LibXR::RawData{ep1_dap, sizeof(ep1_dap)}},
          {LibXR::RawData{ep2_cdc, sizeof(ep2_cdc)}},
          {LibXR::RawData{ep3_comm, sizeof(ep3_comm)}, true},
      },
      LibXR::USB::DeviceDescriptor::PacketSize0::SIZE_64, 0x303A, XRDAP_USB_PID, 0x0100,
      {&kLang}, {{&dap, &cdc}});
#endif
  usb.Init(false);
  usb.Start(false);
  ESP_LOGI("xrdap", "USB transport started");
#endif

#if XRDAP_WIFI_ENABLE
  const esp_err_t wifi = XrDap::WifiStaStart(20000);
  ESP_LOGI("xrdap", "Wi-Fi start: %s", esp_err_to_name(wifi));
  static XrDap::DapTcpServer<Dap> server(dap, XRDAP_TCP_PORT);
#if XRDAP_ENABLE_USB
  // USB stays up until a TCP client connects; while one is connected the TCP transport owns the DAP
  // instance and USB is deinitialised, when the client is gone USB is started again. The USB stack
  // is controlled from a task on core 0 (the core of its interrupt), the TCP task only waits for it.
  static SemaphoreHandle_t usb_cmd_sem = xSemaphoreCreateBinary();
  static SemaphoreHandle_t usb_done_sem = xSemaphoreCreateBinary();
  static volatile int usb_cmd = 0;  // 1 = take USB down, 2 = bring it back
  static bool usb_active = true;
  xTaskCreatePinnedToCore(
      [](void*)
      {
        for (;;)
        {
          xSemaphoreTake(usb_cmd_sem, portMAX_DELAY);
          if (usb_cmd == 1 && usb_active)
          {
            usb.Stop(false);
            usb.Deinit(false);
            usb_active = false;
            ESP_LOGI("xrdap", "TCP client connected: USB deinitialised");
          }
          else if (usb_cmd == 2 && !usb_active)
          {
            usb.Init(false);
            usb.Start(false);
            usb_active = true;
            ESP_LOGI("xrdap", "TCP client gone: USB started again");
          }
          xSemaphoreGive(usb_done_sem);
        }
      },
      "usbctl", 4096, nullptr, 6, nullptr, 0);
  server.SetHooks([](void*) { usb_cmd = 1; xSemaphoreGive(usb_cmd_sem); xSemaphoreTake(usb_done_sem, portMAX_DELAY); },
                  [](void*) { usb_cmd = 2; xSemaphoreGive(usb_cmd_sem); xSemaphoreTake(usb_done_sem, portMAX_DELAY); },
                  nullptr);
#endif
  // The server also starts when Wi-Fi is not up yet: the station keeps (re)connecting by itself.
  // Dedicated-GPIO instructions only work on the core that created the bundle (XRDAP_SWD_CORE).
  server.Start(/*core=*/XRDAP_SWD_CORE);
#else
  ESP_LOGI("xrdap", "Wi-Fi disabled at build time: USB CMSIS-DAP only");
#endif
  ESP_LOGI("xrdap", "XR-DAP-S3 up (SWCLK=%d SWDIO=%d, TCP %d)", kSwclkGpio, kSwdioGpio,
           XRDAP_TCP_PORT);

#if XRDAP_USB_REGDUMP
  // read-only DWC2 / USB_WRAP register snapshot, printed whenever one of the watched registers changes
  xTaskCreatePinnedToCore(
      [](void*)
      {
        auto rd = [](uint32_t a) { return *reinterpret_cast<volatile uint32_t*>(a); };
        uint32_t prev[8] = {};
        bool first = true;
        uint32_t tick = 0;
        for (;;)
        {
          const uint32_t cur[8] = {rd(0x60080000u),            // GOTGCTL
                                   rd(0x60080014u),            // GINTSTS
                                   rd(0x60080018u),            // GINTMSK
                                   rd(0x60080804u),            // DCTL
                                   rd(0x60080808u),            // DSTS
                                   rd(0x60080E00u),            // PCGCCTL
                                   rd(0x60039000u),            // USB_WRAP OTG_CONF
                                   rd(0x60080800u)};           // DCFG
          bool diff = first;
          for (int i = 0; i < 8; ++i)
          {
            if (i == 1) continue;   // GINTSTS has the SOF bit toggling constantly; compare it without bit 3
            if (cur[i] != prev[i]) diff = true;
          }
          if ((cur[1] & ~0x8u) != (prev[1] & ~0x8u)) diff = true;
#if XRDAP_USB_DBGCNT
          { static uint32_t pc = 0; const uint32_t c = xr_usb_dbg[0] + xr_usb_dbg[4] * 1000u; if (c != pc) { pc = c; diff = true; } }
#endif
          if (diff || (++tick % 20u) == 0u)
          {
#if XRDAP_USB_DBGCNT
            ESP_LOGI("usbcnt", "isr=%u core=%u guard=%u pend=%08x rst_in=%u rst_out=%u GAHBCFG=%08x GRSTCTL=%08x DAINTMSK=%08x",
                     (unsigned)xr_usb_dbg[0], (unsigned)xr_usb_dbg[1], (unsigned)xr_usb_dbg[2], (unsigned)xr_usb_dbg[3],
                     (unsigned)xr_usb_dbg[4], (unsigned)xr_usb_dbg[5], (unsigned)rd(0x60080008u), (unsigned)rd(0x60080010u),
                     (unsigned)rd(0x6008081Cu));
#endif
            ESP_LOGI("usbreg", "t=%u GOTGCTL=%08x GINTSTS=%08x GINTMSK=%08x DCTL=%08x DSTS=%08x PCGCCTL=%08x WRAP=%08x DCFG=%08x",
                     (unsigned)(esp_timer_get_time() / 1000), (unsigned)cur[0], (unsigned)cur[1], (unsigned)cur[2],
                     (unsigned)cur[3], (unsigned)cur[4], (unsigned)cur[5], (unsigned)cur[6], (unsigned)cur[7]);
          }
          for (int i = 0; i < 8; ++i) prev[i] = cur[i];
          first = false;
          vTaskDelay(1);
        }
      },
      "usbreg", 4096, nullptr, 5, nullptr, 0);
#endif
  while (true)
  {
#if XRDAP_DAP_TRACE
    {
      static bool printed = false;
      static uint32_t last_total = 0;
      auto& tb = LibXR::USB::g_dap_trace;
      static uint32_t last_kinds_print_total = 0;
      {
        uint32_t total = 0;
        for (int k = 0; k < 11; ++k) { total += tb.hist[k]; }
        if (total != last_kinds_print_total && total > 40)
        {
          static uint32_t last_ms = 0;
          const uint32_t now_ms = (uint32_t)(esp_timer_get_time() / 1000);
          if (now_ms - last_ms > 2500)
          {
            last_ms = now_ms;
            last_kinds_print_total = total;
            for (int k = 0; k < 32; ++k)
            {
              if (tb.kcnt[k] != 0) { ESP_LOGI("dkinds", "cmd id 0x%02x len %4u : %u", (unsigned)(tb.kkey[k] >> 16), (unsigned)(tb.kkey[k] & 0xFFFF), (unsigned)tb.kcnt[k]); }
            }
            for (int k = 0; k < 32; ++k)
            {
              if (tb.rcnt[k] != 0) { ESP_LOGI("dkinds", "response len %4u : %u", (unsigned)tb.rkey[k], (unsigned)tb.rcnt[k]); }
            }
          }
        }
      }
      {
        uint32_t total = 0;
        for (int k = 0; k < 11; ++k) { total += tb.hist[k]; }
        if (total != last_total)
        {
          last_total = total;
          ESP_LOGI("dhist", "commands %u | in flight at arrival 0..10: %u %u %u %u %u %u %u %u %u %u %u", (unsigned)total,
                   (unsigned)tb.hist[0], (unsigned)tb.hist[1], (unsigned)tb.hist[2], (unsigned)tb.hist[3],
                   (unsigned)tb.hist[4], (unsigned)tb.hist[5], (unsigned)tb.hist[6], (unsigned)tb.hist[7],
                   (unsigned)tb.hist[8], (unsigned)tb.hist[9], (unsigned)tb.hist[10]);
        }
      }
      if (!printed && tb.n >= 120)
      {
        printed = true;
        ESP_LOGI("dtrace", "in-flight at arrival histogram (0..10): %u %u %u %u %u %u %u %u %u %u %u",
                 (unsigned)tb.hist[0], (unsigned)tb.hist[1], (unsigned)tb.hist[2], (unsigned)tb.hist[3],
                 (unsigned)tb.hist[4], (unsigned)tb.hist[5], (unsigned)tb.hist[6], (unsigned)tb.hist[7],
                 (unsigned)tb.hist[8], (unsigned)tb.hist[9], (unsigned)tb.hist[10]);
        static const char* kName[] = {"?", "OUT_DONE", "ARM_OUT ", "STEP_TAKE", "STEP_DONE", "IN_START", "IN_DONE"};
        const uint32_t t0 = tb.t[0];
        for (uint32_t i = 0; i < 120; ++i)
        {
          const uint32_t code = tb.c[i] >> 24;
          const uint32_t val = tb.c[i] & 0xFFFFFF;
          ESP_LOGI("dtrace", "%6u us %-9s val 0x%06x", (unsigned)(tb.t[i] - t0), code < 7 ? kName[code] : "?", (unsigned)val);
        }
      }
    }
#endif
#if XRDAP_FAST_SWD
    ESP_LOGI("fastdbg", "setclk %u (last %u Hz) close %u linereset %u transfer %u idle %u hold_nops %d hold_cycles %u",
             (unsigned)pswd.dbg[0], (unsigned)pswd.dbg_last_hz, (unsigned)pswd.dbg[1], (unsigned)pswd.dbg[2],
             (unsigned)pswd.dbg[3], (unsigned)pswd.dbg[4], pswd.HoldNops(), (unsigned)pswd.HoldCycles());
#endif
#if XRDAP_ISR_PROBE
    {
      uint32_t isr, total, cores;
      pswd.IsrStats(isr, total, cores);
      ESP_LOGI("isrprobe", "SWD transfers: %u total, %u in interrupt context, core mask 0x%x",
               (unsigned)total, (unsigned)isr, (unsigned)cores);
#if !XRDAP_FAST_SWD
      pswd.Dump("swdprof");
#endif
    }
#endif
#if XRDAP_DEBUG_STATE
    {
      const uint32_t base = 0x60080000U;
      auto r = [&](uint32_t off) { return (unsigned)REG_READ(base + off); };
      ESP_LOGI("dapdbg",
               "q_cnt=%u head=%u tail=%u rd_act=%d wr_act=%d cmd_pend=%d pend_act=%u "
               "ep_in=%d ep_out=%d | EP1 IN ctl=%08x int=%08x tsiz=%08x txfsts=%08x | "
               "EP1 OUT ctl=%08x int=%08x tsiz=%08x | GINTSTS=%08x",
               (unsigned)dap.resp_q_count_, (unsigned)dap.resp_q_head_, (unsigned)dap.resp_q_tail_,
               (int)dap.command_read_active_, (int)dap.response_write_active_,
               (int)dap.cmd_count_, (unsigned)dap.pending_action_.load(),
               dap.ep_data_in_ ? (int)dap.ep_data_in_->GetState() : -1,
               dap.ep_data_out_ ? (int)dap.ep_data_out_->GetState() : -1, r(0x920),
               r(0x928), r(0x930), r(0x938), r(0xB20), r(0xB28), r(0xB30), r(0x014));
    }
    LibXR::Thread::Sleep(500);
#else
    LibXR::Thread::Sleep(1000);
#endif
  }
}

