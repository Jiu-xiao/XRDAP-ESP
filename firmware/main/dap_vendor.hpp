// SPDX-License-Identifier: Apache-2.0
#pragma once

// XR 厂商扩展命令（0x80 到 0x85）：多地址读写、指针链解引用、探针端周期采样。
// 协议见 docs/protocol.md。
//
// 这些命令要直接访问 MEM-AP，所以自己管理 SELECT、CSW、TAR，不走 DAP 的传输流水线。
// 执行完会让 SWD 的 SELECT 缓存失效；上位机要把 SELECT、CSW、TAR 当作未知。
// 通过 libxr DAP 类的命令钩子接入，USB、TCP、串口三种传输行为一致。

#include <atomic>
#include <cstdint>
#include <cstring>

#include "debug/swd.hpp"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"

namespace XrDap
{

class DapVendor
{
 public:
  using Ack = LibXR::Debug::SwdProtocol::Ack;
  using DpReadReg = LibXR::Debug::SwdProtocol::DpReadReg;

  static constexpr uint8_t kVersion = 1;
  static constexpr uint32_t kFeatures = 0x0F;  // READ | WRITE | 采样 | 8 字节访问
  static constexpr uint16_t kMaxItems = 32;
  static constexpr uint8_t kMaxDepth = 8;
  static constexpr uint32_t kRingBytes = 16384;  // 必须是 2 的幂
  static constexpr uint16_t kMaxFrameBytes = 10U + (kMaxItems + 7U) / 8U + kMaxItems * 8U;
  // 串口没有"连接"的概念：串口发起的采样在这么久收不到任何请求后自动停止。
  static constexpr uint32_t kSerialLeaseMs = 5000U;

  static constexpr uint8_t kStatusOk = 0x00;
  static constexpr uint8_t kStatusArg = 0x01;
  static constexpr uint8_t kStatusState = 0x02;
  static constexpr uint8_t kStatusResource = 0x03;
  static constexpr uint8_t kStatusNoTarget = 0x04;

  static constexpr uint8_t kItemOk = 0x00;
  static constexpr uint8_t kItemSwdChain = 0x10;
  static constexpr uint8_t kItemSwdLast = 0x1F;
  static constexpr uint8_t kItemAlignChain = 0x20;
  static constexpr uint8_t kItemAlignLast = 0x2F;
  static constexpr uint8_t kItemNull = 0x30;

  /// 当前驱动 DAP 的传输。采样归发起它的传输所有，那个传输断开时采样停止。
  enum class Transport : uint8_t
  {
    USB = 0,
    TCP = 1,
    SERIAL = 2,
  };

  struct Item
  {
    uint8_t ap = 0;
    uint16_t id = 0;
    uint8_t size = 0;
    uint32_t base = 0;
    uint8_t n = 0;
    int16_t deref[kMaxDepth] = {};
    int16_t final_off = 0;
  };

  DapVendor(LibXR::Debug::Swd& swd, uint16_t max_packet) : swd_(swd), max_packet_(max_packet)
  {
  }

  /// 在指定的核上启动采样任务（必须是 SWD 所在的核）。只调用一次。
  void Begin(int core, uint32_t task_prio)
  {
    swd_mutex_ = xSemaphoreCreateMutex();
    wake_ = xSemaphoreCreateBinary();
    xTaskCreatePinnedToCore(&DapVendor::TaskEntry, "dap_vendor", 8192, this, task_prio, &task_,
                            core);
  }

  /// 传输在执行 DAP 命令之前设置。
  void SetTransport(Transport t) { transport_ = t; }

  /// 串口收到一个请求：刷新串口采样的租期。
  void NotifySerialActivity() { last_serial_ms_.store(NowMs(), std::memory_order_release); }

  /// 拥有探针的传输断开了（TCP 客户端关闭、USB 复位等）：停止采样，释放连接状态。
  void OnTransportClosed()
  {
    StopSampling();
    connected_ = false;
    owner_ = Transport::USB;
    transport_ = Transport::USB;
    swd_.InvalidateSelectCache();
  }

  /// USB 总线复位计数。USB 发起的采样在计数变化（拔出、主机复位）时停止。
  using ResetCountFn = uint32_t (*)(void*);
  void SetUsbResetSource(ResetCountFn fn, void* ctx)
  {
    usb_reset_fn_ = fn;
    usb_reset_ctx_ = ctx;
  }

  /// libxr DAP 类的命令钩子入口。返回 true 表示命令已处理。
  static bool Hook(void* ctx, const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
                   uint16_t& out)
  {
    return static_cast<DapVendor*>(ctx)->OnCommand(req, len, resp, cap, out);
  }

 private:
  static constexpr size_t kMaxAps = 8;
  static_assert((kRingBytes & (kRingBytes - 1U)) == 0U, "kRingBytes 必须是 2 的幂");

  // 一个 AP 的 CSW：base_csw 是读出来的原值（保留 Size 和 AddrInc 以外的位），
  // written_csw 是最近一次写入的值，用来省掉重复写。
  struct ApState
  {
    uint8_t ap = 0;
    uint32_t base_csw = 0;
    bool valid = false;
    uint32_t written_csw = 0;
    bool written = false;
  };

  struct SubItem
  {
    Item item;
    uint16_t div;
    uint16_t phase;
  };

  struct ApCsw
  {
    uint8_t ap;
    uint32_t base_csw;
    bool valid;
  };

  static uint32_t NowMs() { return static_cast<uint32_t>(esp_timer_get_time() / 1000); }

  bool OnCommand(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap, uint16_t& out)
  {
    if (req == nullptr || resp == nullptr || len < 1U || cap < 1U)
    {
      return false;
    }
    const uint8_t cmd = req[0];

    if (active_.load(std::memory_order_acquire))
    {
      // 采样期间探针被订阅独占：只放行 DAP_Info、DAP_HostStatus 和 0x80 到 0x85。
      if (cmd == 0x00U || cmd == 0x01U)
      {
        return false;
      }
      if (cmd < 0x80U || cmd > 0x85U)
      {
        resp[0] = 0xFFU;
        out = 1U;
        return true;
      }
      return HandleVendor(cmd, req, len, resp, cap, out);
    }

    if (cmd >= 0x80U && cmd <= 0x85U)
    {
      return HandleVendor(cmd, req, len, resp, cap, out);
    }

    // 跟踪 DAP_Connect / DAP_Disconnect，扩展命令要求先连接到 SWD 目标。
    if (cmd == 0x02U)
    {
      const uint8_t port = (len >= 2U) ? req[1] : 0U;
      connected_ = (port == 0U || port == 1U);
    }
    else if (cmd == 0x03U)
    {
      StopSampling();
      connected_ = false;
      swd_.InvalidateSelectCache();
    }
    return false;
  }

  bool HandleVendor(uint8_t cmd, const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
                    uint16_t& out)
  {
    switch (cmd)
    {
      case 0x80U:
        return CmdInfo(len, resp, cap, out);
      case 0x81U:
        return CmdRead(req, len, resp, cap, out);
      case 0x82U:
        return CmdWrite(req, len, resp, cap, out);
      case 0x83U:
        return CmdSubSet(req, len, resp, cap, out);
      case 0x84U:
        return CmdSubCtrl(req, len, resp, out);
      case 0x85U:
        return CmdSubRead(req, len, resp, cap, out);
      default:
        return false;
    }
  }

  // ------------------------------------------------------------------ 字节序工具

  static void Put16(uint8_t* p, uint16_t v)
  {
    p[0] = static_cast<uint8_t>(v);
    p[1] = static_cast<uint8_t>(v >> 8);
  }
  static void Put32(uint8_t* p, uint32_t v)
  {
    for (int i = 0; i < 4; ++i)
    {
      p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
  }
  static void Put48(uint8_t* p, uint64_t v)
  {
    for (int i = 0; i < 6; ++i)
    {
      p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
  }
  static void Put64(uint8_t* p, uint64_t v)
  {
    for (int i = 0; i < 8; ++i)
    {
      p[i] = static_cast<uint8_t>(v >> (8 * i));
    }
  }
  static uint16_t Get16(const uint8_t* p) { return static_cast<uint16_t>(p[0] | (p[1] << 8)); }
  static uint32_t Get32(const uint8_t* p)
  {
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
  }

  static bool Reply(uint8_t* resp, uint8_t status, uint16_t& out)
  {
    resp[1] = status;
    out = 2U;
    return true;
  }

  // ------------------------------------------------------------------ 命令

  bool CmdInfo(uint16_t len, uint8_t* resp, uint16_t cap, uint16_t& out)
  {
    if (cap < 26U)
    {
      out = 0U;
      return true;
    }
    resp[0] = 0x80U;
    if (len != 1U)
    {
      return Reply(resp, kStatusArg, out);
    }
    resp[1] = kStatusOk;
    resp[2] = kVersion;
    Put32(&resp[3], kFeatures);
    Put16(&resp[7], kMaxItems);
    resp[9] = kMaxDepth;
    Put32(&resp[10], kRingBytes);
    Put64(&resp[14], static_cast<uint64_t>(esp_timer_get_time()));
    Put32(&resp[22], access_us_);
    out = 26U;
    return true;
  }

  /// 解析一个条目；sampling 为 true 时条目后面还有 div 和 phase。
  static bool ParseItem(const uint8_t*& p, const uint8_t* end, Item& it, bool sampling,
                        uint16_t* div, uint16_t* phase)
  {
    if (static_cast<size_t>(end - p) < 9U)
    {
      return false;
    }
    it.ap = p[0];
    it.id = Get16(&p[1]);
    it.size = p[3];
    it.base = Get32(&p[4]);
    it.n = p[8];
    if (it.n > kMaxDepth)
    {
      return false;
    }
    p += 9;
    if (static_cast<size_t>(end - p) < static_cast<size_t>(it.n) * 2U + 2U)
    {
      return false;
    }
    for (uint8_t i = 0; i < it.n; ++i)
    {
      it.deref[i] = static_cast<int16_t>(Get16(p));
      p += 2;
    }
    it.final_off = static_cast<int16_t>(Get16(p));
    p += 2;
    if (sampling)
    {
      if (static_cast<size_t>(end - p) < 4U)
      {
        return false;
      }
      *div = Get16(p);
      *phase = Get16(p + 2);
      p += 4;
    }
    return true;
  }

  static bool SizeValid(uint8_t size) { return size == 1U || size == 2U || size == 4U || size == 8U; }

  bool CmdRead(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap, uint16_t& out)
  {
    resp[0] = 0x81U;
    if (len < 2U || req[1] == 0U)
    {
      return Reply(resp, kStatusArg, out);
    }
    const uint8_t count = req[1];
    if (count > kMaxItems)
    {
      return Reply(resp, kStatusResource, out);
    }
    Item items[kMaxItems];
    const uint8_t* p = req + 2;
    const uint8_t* end = req + len;
    uint32_t resp_size = 3U;
    for (uint8_t i = 0; i < count; ++i)
    {
      if (!ParseItem(p, end, items[i], false, nullptr, nullptr) || !SizeValid(items[i].size))
      {
        return Reply(resp, kStatusArg, out);
      }
      resp_size += 3U + items[i].size;
    }
    if (p != end)
    {
      return Reply(resp, kStatusArg, out);
    }
    if (resp_size > cap || resp_size > max_packet_)
    {
      return Reply(resp, kStatusResource, out);
    }
    if (!connected_)
    {
      return Reply(resp, kStatusNoTarget, out);
    }
    // 采样任务和本命令共用同一条 SWD 链路，要串行执行。
    if (xSemaphoreTake(swd_mutex_, pdMS_TO_TICKS(2000)) != pdTRUE)
    {
      return Reply(resp, kStatusState, out);
    }

    ResetApCache();
    resp[1] = kStatusOk;
    resp[2] = count;
    uint8_t* q = resp + 3;
    for (uint8_t i = 0; i < count; ++i)
    {
      uint8_t value[8] = {};
      const uint8_t st = ExecuteRead(items[i], value);
      Put16(q, items[i].id);
      q[2] = st;
      std::memcpy(q + 3, value, items[i].size);
      q += 3 + items[i].size;
    }
    swd_.InvalidateSelectCache();
    xSemaphoreGive(swd_mutex_);
    out = static_cast<uint16_t>(q - resp);
    return true;
  }

  bool CmdWrite(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap, uint16_t& out)
  {
    resp[0] = 0x82U;
    if (len < 2U || req[1] == 0U)
    {
      return Reply(resp, kStatusArg, out);
    }
    const uint8_t count = req[1];
    if (count > kMaxItems)
    {
      return Reply(resp, kStatusResource, out);
    }
    Item items[kMaxItems];
    uint8_t values[kMaxItems][8] = {};
    const uint8_t* p = req + 2;
    const uint8_t* end = req + len;
    for (uint8_t i = 0; i < count; ++i)
    {
      if (!ParseItem(p, end, items[i], false, nullptr, nullptr) || !SizeValid(items[i].size) ||
          static_cast<size_t>(end - p) < items[i].size)
      {
        return Reply(resp, kStatusArg, out);
      }
      std::memcpy(values[i], p, items[i].size);
      p += items[i].size;
    }
    if (p != end)
    {
      return Reply(resp, kStatusArg, out);
    }
    if (3U + 3U * count > cap || 3U + 3U * count > max_packet_)
    {
      return Reply(resp, kStatusResource, out);
    }
    if (!connected_)
    {
      return Reply(resp, kStatusNoTarget, out);
    }
    if (xSemaphoreTake(swd_mutex_, pdMS_TO_TICKS(2000)) != pdTRUE)
    {
      return Reply(resp, kStatusState, out);
    }

    ResetApCache();
    resp[1] = kStatusOk;
    resp[2] = count;
    uint8_t* q = resp + 3;
    for (uint8_t i = 0; i < count; ++i)
    {
      const uint8_t st = ExecuteWrite(items[i], values[i]);
      Put16(q, items[i].id);
      q[2] = st;
      q += 3;
    }
    swd_.InvalidateSelectCache();
    xSemaphoreGive(swd_mutex_);
    out = static_cast<uint16_t>(q - resp);
    return true;
  }

  bool CmdSubSet(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap, uint16_t& out)
  {
    if (cap < 8U)
    {
      out = 0U;
      return true;
    }
    resp[0] = 0x83U;
    if (active_.load(std::memory_order_acquire))
    {
      return Reply(resp, kStatusState, out);
    }
    if (len < 8U)
    {
      return Reply(resp, kStatusArg, out);
    }
    const uint32_t period = Get32(&req[1]);
    const uint8_t flags = req[5];
    const uint16_t count = Get16(&req[6]);
    if (period == 0U || (flags & 0xFEU) != 0U || count == 0U)
    {
      return Reply(resp, kStatusArg, out);
    }
    if (count > kMaxItems)
    {
      return Reply(resp, kStatusResource, out);
    }
    const uint8_t* p = req + 8;
    const uint8_t* end = req + len;
    SubItem items[kMaxItems];
    uint32_t max_frame = 10U;
    uint8_t aps_seen[kMaxAps];
    uint8_t ap_count = 0U;
    for (uint16_t i = 0; i < count; ++i)
    {
      Item it;
      uint16_t div = 0U;
      uint16_t phase = 0U;
      if (!ParseItem(p, end, it, true, &div, &phase) || !SizeValid(it.size) || div == 0U ||
          phase >= div)
      {
        return Reply(resp, kStatusArg, out);
      }
      bool found = false;
      for (uint8_t j = 0; j < ap_count; ++j)
      {
        found = found || (aps_seen[j] == it.ap);
      }
      if (!found)
      {
        if (ap_count >= kMaxAps)
        {
          return Reply(resp, kStatusResource, out);
        }
        aps_seen[ap_count++] = it.ap;
      }
      max_frame += it.size;
      items[i] = SubItem{it, div, phase};
    }
    if (p != end)
    {
      return Reply(resp, kStatusArg, out);
    }
    max_frame += (count + 7U) / 8U;
    if (!connected_)
    {
      return Reply(resp, kStatusNoTarget, out);
    }
    // 按当前 SWD 时钟实测：标定单次访问耗时（INFO 里报告），再把全部条目试读一遍，
    // 得到"所有条目都到期"时一帧的耗时。开机后第一次订阅也能得到准确的估计。
    uint32_t frame_us = 0U;
    if (!Calibrate(items[0].item.ap) || !MeasureFrame(items, count, frame_us))
    {
      return Reply(resp, kStatusNoTarget, out);
    }

    // 乘 1.6：采样时同一个核上的 DAP 线程还要处理 SUB_READ，会抢占采样任务。
    // 实测（8 个条目、试读约 265 µs）每帧实际要约 1.5 倍的时间，周期再短就开始跳拍。
    const uint32_t est = 10U + (frame_us * 8U) / 5U;
    if (max_frame + 2U > kRingBytes || est > period)
    {
      resp[1] = kStatusResource;
      Put16(&resp[2], static_cast<uint16_t>(max_frame));
      Put32(&resp[4], est);
      out = 8U;
      return true;
    }

    std::memcpy(sub_, items, sizeof(SubItem) * count);
    sub_count_ = count;
    period_us_ = period;
    stop_on_full_ = (flags & 1U) != 0U;
    ResetRingAndCounters();
    config_valid_ = true;
    resp[1] = kStatusOk;
    Put16(&resp[2], static_cast<uint16_t>(max_frame));
    Put32(&resp[4], est);
    out = 8U;
    return true;
  }

  bool CmdSubCtrl(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t& out)
  {
    resp[0] = 0x84U;
    if (len != 2U)
    {
      return Reply(resp, kStatusArg, out);
    }
    switch (req[1])
    {
      case 0U:
        StopSampling();
        return Reply(resp, kStatusOk, out);
      case 1U:
        if (active_.load(std::memory_order_acquire) || !config_valid_)
        {
          return Reply(resp, kStatusState, out);
        }
        if (!connected_ || !StartSampling())
        {
          return Reply(resp, kStatusNoTarget, out);
        }
        return Reply(resp, kStatusOk, out);
      case 2U:
        ResetRingAndCounters();
        return Reply(resp, kStatusOk, out);
      default:
        return Reply(resp, kStatusArg, out);
    }
  }

  bool CmdSubRead(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap, uint16_t& out)
  {
    if (cap < 24U)
    {
      out = 0U;
      return true;
    }
    resp[0] = 0x85U;
    if (len != 3U)
    {
      return Reply(resp, kStatusArg, out);
    }
    if (!config_valid_)
    {
      return Reply(resp, kStatusState, out);
    }
    const uint32_t want = Get16(req + 1);
    const uint32_t limit = (want < cap - 24U) ? want : cap - 24U;
    uint32_t got = 0U;
    uint16_t frames = 0U;

    // 环形缓冲里每帧前面有 2 字节长度；只取完整的帧。
    taskENTER_CRITICAL(&ring_mux_);
    uint32_t pos = ring_tail_;
    while (ring_used_ != 0U)
    {
      const uint16_t flen = RingFrameLen(pos);
      if (got + flen > limit)
      {
        break;
      }
      CopyFromRing((pos + 2U) & (kRingBytes - 1U), resp + 24U + got, flen);
      pos = (pos + 2U + flen) & (kRingBytes - 1U);
      got += flen;
      ++frames;
      ring_used_ -= 2U + flen;
      ring_payload_ -= flen;
    }
    ring_tail_ = pos;
    const uint32_t dropped = dropped_;
    const uint32_t skipped = skipped_;
    const uint32_t remaining = ring_payload_;
    taskEXIT_CRITICAL(&ring_mux_);

    resp[1] = kStatusOk;
    Put64(&resp[2], static_cast<uint64_t>(esp_timer_get_time()));
    Put32(&resp[10], remaining);
    Put32(&resp[14], dropped);
    Put32(&resp[18], skipped);
    Put16(&resp[22], frames);
    out = static_cast<uint16_t>(24U + got);
    return true;
  }

  // ------------------------------------------------------------------ SWD 访问

  static uint8_t SizeCode(uint8_t size) { return (size == 1U) ? 0U : (size == 2U) ? 1U : 2U; }

  static bool TxnOk(LibXR::ErrorCode ec, Ack ack)
  {
    return ec == LibXR::ErrorCode::OK && ack == Ack::OK;
  }

  void ResetApCache()
  {
    for (ApState& st : ap_)
    {
      st = ApState{};
    }
    ap_count_ = 0U;
  }

  /// 标定一次访问（写 TAR + 读 AP 寄存器，与读一个字的事务数相同）的耗时。
  /// 读的是 CSW，不访问目标内存，没有副作用。
  bool Calibrate(uint8_t ap)
  {
    constexpr uint32_t kRounds = 16U;
    if (xSemaphoreTake(swd_mutex_, pdMS_TO_TICKS(2000)) != pdTRUE)
    {
      return false;
    }
    Ack ack;
    uint32_t v = 0U;
    bool ok = TxnOk(swd_.SetSelectCached(static_cast<uint32_t>(ap) << 24, ack), ack);
    const int64_t t0 = esp_timer_get_time();
    for (uint32_t i = 0; ok && i < kRounds; ++i)
    {
      ok = TxnOk(swd_.ApWriteTxn(0x01U, 0U, ack), ack) && TxnOk(swd_.ApReadTxn(0x00U, v, ack), ack);
    }
    const int64_t t1 = esp_timer_get_time();
    if (!ok)
    {
      ClearSticky();
    }
    swd_.InvalidateSelectCache();
    xSemaphoreGive(swd_mutex_);
    if (ok)
    {
      const uint32_t us = static_cast<uint32_t>((t1 - t0 + kRounds - 1U) / kRounds);
      access_us_ = (us == 0U) ? 1U : us;
    }
    return ok;
  }

  /// 把全部条目按采样时的访问序列试读一遍，返回第二遍的耗时（第一遍包含读 CSW）。
  /// 试读和采样访问的是同一批地址，副作用相同。
  bool MeasureFrame(const SubItem* items, uint16_t count, uint32_t& us)
  {
    if (xSemaphoreTake(swd_mutex_, pdMS_TO_TICKS(2000)) != pdTRUE)
    {
      return false;
    }
    ResetApCache();
    bool ok = true;
    for (int pass = 0; ok && pass < 2; ++pass)
    {
      ApState ap{};
      ap.ap = 0xFFU;
      const int64_t t0 = esp_timer_get_time();
      for (uint16_t i = 0; i < count; ++i)
      {
        const Item& it = items[i].item;
        if (ap.ap != it.ap)
        {
          const ApState* st = EnsureAp(it.ap);
          if (st == nullptr)
          {
            ok = false;
            break;
          }
          ap = *st;
          ap.written = false;
        }
        uint8_t value[8];
        (void)ReadItem(ap, it, value);
      }
      us = static_cast<uint32_t>(esp_timer_get_time() - t0);
    }
    swd_.InvalidateSelectCache();
    xSemaphoreGive(swd_mutex_);
    return ok;
  }

  bool ReadCsw(uint8_t ap, uint32_t& csw)
  {
    Ack ack;
    if (!TxnOk(swd_.SetSelectCached(static_cast<uint32_t>(ap) << 24, ack), ack) ||
        !TxnOk(swd_.ApReadTxn(0x00U, csw, ack), ack))
    {
      ClearSticky();
      return false;
    }
    return true;
  }

  /// 本命令里第一次用到某个 AP 时读一次它的 CSW。
  ApState* EnsureAp(uint8_t ap)
  {
    for (size_t i = 0; i < ap_count_; ++i)
    {
      if (ap_[i].ap == ap)
      {
        return ap_[i].valid ? &ap_[i] : nullptr;
      }
    }
    // 表满时复用最后一格，代价是每次重新读 CSW。
    ApState* st = (ap_count_ >= kMaxAps) ? &ap_[kMaxAps - 1U] : &ap_[ap_count_++];
    st->ap = ap;
    st->valid = ReadCsw(ap, st->base_csw);
    st->written = false;
    return st->valid ? st : nullptr;
  }

  /// 写 CSW：保留原值里 Size（位 2:0）和 AddrInc（位 5:4）以外的位，关闭自增。
  bool SetSize(ApState& st, uint8_t size)
  {
    if (!st.valid)
    {
      return false;
    }
    const uint32_t csw = (st.base_csw & ~0x3FU) | SizeCode(size);
    if (st.written && st.written_csw == csw)
    {
      return true;
    }
    Ack ack;
    if (!TxnOk(swd_.ApWriteTxn(0x00U, csw, ack), ack))
    {
      st.written = false;
      ClearSticky();
      return false;
    }
    st.written = true;
    st.written_csw = csw;
    return true;
  }

  bool ReadWord(uint32_t addr, uint32_t& val)
  {
    Ack ack;
    if (!TxnOk(swd_.ApWriteTxn(0x01U, addr, ack), ack) ||
        !TxnOk(swd_.ApReadTxn(0x03U, val, ack), ack))
    {
      ClearSticky();
      return false;
    }
    return true;
  }

  /// 写是 posted 的，写完读一次 CTRL/STAT 看有没有粘滞错误。
  bool CheckCtrlStat()
  {
    Ack ack;
    uint32_t cs = 0U;
    if (!TxnOk(swd_.DpReadTxn(DpReadReg::CTRL_STAT, cs, ack), ack) ||
        (cs & ((1U << 1) | (1U << 5) | (1U << 7))) != 0U)  // STICKYORUN、STICKYERR、WDATAERR
    {
      ClearSticky();
      return false;
    }
    return true;
  }

  bool WriteWord(uint32_t addr, uint32_t val)
  {
    Ack ack;
    if (!TxnOk(swd_.ApWriteTxn(0x01U, addr, ack), ack) ||
        !TxnOk(swd_.ApWriteTxn(0x03U, val, ack), ack))
    {
      ClearSticky();
      return false;
    }
    return CheckCtrlStat();
  }

  /// 写 DP ABORT 清除 STKCMPCLR、STKERRCLR、WDERRCLR、ORUNERRCLR，否则后面的访问会连续失败。
  void ClearSticky()
  {
    Ack ack;
    (void)swd_.WriteAbortTxn(0x1EU, ack);
  }

  /// 沿指针链求出最后要访问的地址；失败时 st 给出条目状态。
  bool Resolve(const Item& it, uint32_t& addr, uint8_t& st)
  {
    uint32_t cur = it.base;
    for (uint8_t i = 0; i < it.n; ++i)
    {
      const uint32_t a = cur + static_cast<uint32_t>(static_cast<int32_t>(it.deref[i]));
      if ((a & 3U) != 0U)
      {
        st = static_cast<uint8_t>(kItemAlignChain + i);
        return false;
      }
      uint32_t p = 0U;
      if (!ReadWord(a, p))
      {
        st = static_cast<uint8_t>(kItemSwdChain + i);
        return false;
      }
      if (p == 0U)
      {
        st = static_cast<uint8_t>(kItemNull + i);
        return false;
      }
      cur = p;
    }
    const uint32_t a = cur + static_cast<uint32_t>(static_cast<int32_t>(it.final_off));
    const uint8_t align = (it.size == 8U) ? 4U : it.size;
    if ((a & (align - 1U)) != 0U)
    {
      st = kItemAlignLast;
      return false;
    }
    addr = a;
    st = kItemOk;
    return true;
  }

  /// 按 size 和地址取出值：1、2 字节在 DRW 的对应字节通道里，8 字节是两次 32 位读（不是原子的）。
  uint8_t ReadValue(ApState& ap, uint8_t size, uint32_t addr, uint8_t* out)
  {
    if (!SetSize(ap, (size == 8U) ? 4U : size))
    {
      return kItemSwdLast;
    }
    uint32_t v = 0U;
    if (!ReadWord(addr, v))
    {
      return kItemSwdLast;
    }
    if (size == 8U)
    {
      uint32_t v2 = 0U;
      if (!ReadWord(addr + 4U, v2))
      {
        return kItemSwdLast;
      }
      std::memcpy(out, &v, 4);
      std::memcpy(out + 4, &v2, 4);
      return kItemOk;
    }
    if (size == 4U)
    {
      std::memcpy(out, &v, 4);
      return kItemOk;
    }
    const uint32_t shifted = v >> ((addr & 3U) * 8U);
    if (size == 1U)
    {
      out[0] = static_cast<uint8_t>(shifted);
    }
    else
    {
      const uint16_t h = static_cast<uint16_t>(shifted);
      std::memcpy(out, &h, 2);
    }
    return kItemOk;
  }

  /// 读一个条目。解引用前 CSW 必须是 32 位访问：上一个条目可能把它改成了 1 或 2 字节。
  uint8_t ReadItem(ApState& ap, const Item& it, uint8_t* out)
  {
    if (it.n != 0U && !SetSize(ap, 4U))
    {
      return kItemSwdLast;
    }
    uint32_t addr = 0U;
    uint8_t st = kItemOk;
    if (!Resolve(it, addr, st))
    {
      return st;
    }
    return ReadValue(ap, it.size, addr, out);
  }

  uint8_t ExecuteRead(const Item& it, uint8_t* out)
  {
    ApState* ap = EnsureAp(it.ap);
    return (ap == nullptr) ? kItemSwdLast : ReadItem(*ap, it, out);
  }

  uint8_t ExecuteWrite(const Item& it, const uint8_t* value)
  {
    ApState* ap = EnsureAp(it.ap);
    if (ap == nullptr || (it.n != 0U && !SetSize(*ap, 4U)))
    {
      return kItemSwdLast;
    }
    uint32_t addr = 0U;
    uint8_t st = kItemOk;
    if (!Resolve(it, addr, st))
    {
      return st;
    }
    if (it.size == 8U)
    {
      uint32_t lo = 0U;
      uint32_t hi = 0U;
      std::memcpy(&lo, value, 4);
      std::memcpy(&hi, value + 4, 4);
      if (!SetSize(*ap, 4U) || !WriteWord(addr, lo) || !WriteWord(addr + 4U, hi))
      {
        return kItemSwdLast;
      }
      return kItemOk;
    }
    if (!SetSize(*ap, it.size))
    {
      return kItemSwdLast;
    }
    uint32_t word = 0U;
    const uint32_t lane = (addr & 3U) * 8U;
    if (it.size == 4U)
    {
      std::memcpy(&word, value, 4);
    }
    else if (it.size == 2U)
    {
      uint16_t h = 0U;
      std::memcpy(&h, value, 2);
      word = static_cast<uint32_t>(h) << lane;
    }
    else
    {
      word = static_cast<uint32_t>(value[0]) << lane;
    }
    return WriteWord(addr, word) ? kItemOk : kItemSwdLast;
  }

  // ------------------------------------------------------------------ 采样

  bool StartSampling()
  {
    if (xSemaphoreTake(swd_mutex_, pdMS_TO_TICKS(2000)) != pdTRUE)
    {
      return false;
    }
    // 开始时读一次各 AP 的 CSW，采样期间不再重读。
    bool ok = true;
    samp_ap_count_ = 0U;
    for (uint16_t i = 0; i < sub_count_; ++i)
    {
      const uint8_t ap = sub_[i].item.ap;
      if (FindSampCsw(ap) != nullptr)
      {
        continue;
      }
      ApCsw& dst = samp_csw_[samp_ap_count_++];
      dst.ap = ap;
      dst.valid = ReadCsw(ap, dst.base_csw);
      ok = ok && dst.valid;
    }
    swd_.InvalidateSelectCache();
    xSemaphoreGive(swd_mutex_);
    if (!ok)
    {
      return false;
    }

    ResetRingAndCounters();
    start_us_ = static_cast<uint64_t>(esp_timer_get_time());
    owner_ = transport_;
    last_serial_ms_.store(NowMs(), std::memory_order_release);
    usb_reset_base_ = (usb_reset_fn_ != nullptr) ? usb_reset_fn_(usb_reset_ctx_) : 0U;
    gen_.fetch_add(1, std::memory_order_acq_rel);
    active_.store(true, std::memory_order_release);
    xSemaphoreGive(wake_);
    return true;
  }

  void StopSampling() { active_.store(false, std::memory_order_release); }

  void ResetRingAndCounters()
  {
    taskENTER_CRITICAL(&ring_mux_);
    ring_head_ = 0U;
    ring_tail_ = 0U;
    ring_used_ = 0U;
    ring_payload_ = 0U;
    dropped_ = 0U;
    skipped_ = 0U;
    taskEXIT_CRITICAL(&ring_mux_);
  }

  static void TaskEntry(void* arg) { static_cast<DapVendor*>(arg)->TaskLoop(); }

  void TaskLoop()
  {
    for (;;)
    {
      if (!active_.load(std::memory_order_acquire))
      {
        xSemaphoreTake(wake_, pdMS_TO_TICKS(100));
        continue;
      }
      RunSampling(gen_.load(std::memory_order_acquire));
    }
  }

  /// 等到 deadline：剩余超过 3 ms 时让出 CPU，最后一段忙等，保证节拍精度。
  static void SleepUntil(uint64_t deadline_us, const std::atomic<bool>& active)
  {
    for (;;)
    {
      if (!active.load(std::memory_order_relaxed))
      {
        return;
      }
      const uint64_t now = static_cast<uint64_t>(esp_timer_get_time());
      if (now >= deadline_us)
      {
        return;
      }
      const uint64_t remain = deadline_us - now;
      if (remain > 3000U)
      {
        const uint32_t ms = static_cast<uint32_t>((remain - 2000U) / 1000U);
        vTaskDelay(pdMS_TO_TICKS(ms == 0U ? 1U : ms));
      }
    }
  }

  uint16_t RingFrameLen(uint32_t pos) const
  {
    return static_cast<uint16_t>(ring_[pos] | (ring_[(pos + 1U) & (kRingBytes - 1U)] << 8));
  }

  void CopyFromRing(uint32_t at, uint8_t* dst, uint32_t len) const
  {
    const uint32_t first = (at + len <= kRingBytes) ? len : kRingBytes - at;
    std::memcpy(dst, ring_ + at, first);
    std::memcpy(dst + first, ring_, len - first);
  }

  void CopyToRing(uint32_t at, const uint8_t* src, uint32_t len)
  {
    const uint32_t first = (at + len <= kRingBytes) ? len : kRingBytes - at;
    std::memcpy(ring_ + at, src, first);
    std::memcpy(ring_, src + first, len - first);
  }

  void PushFrame(const uint8_t* frame, uint16_t len)
  {
    taskENTER_CRITICAL(&ring_mux_);
    while (ring_used_ + len + 2U > kRingBytes)
    {
      if (stop_on_full_ || ring_used_ == 0U)
      {
        ++dropped_;
        taskEXIT_CRITICAL(&ring_mux_);
        return;
      }
      // 覆盖模式：丢掉最旧的帧。
      const uint16_t old = RingFrameLen(ring_tail_);
      ring_tail_ = (ring_tail_ + 2U + old) & (kRingBytes - 1U);
      ring_used_ -= 2U + old;
      ring_payload_ -= old;
      ++dropped_;
    }
    const uint8_t hdr[2] = {static_cast<uint8_t>(len), static_cast<uint8_t>(len >> 8)};
    CopyToRing(ring_head_, hdr, 2U);
    CopyToRing((ring_head_ + 2U) & (kRingBytes - 1U), frame, len);
    ring_head_ = (ring_head_ + 2U + len) & (kRingBytes - 1U);
    ring_used_ += 2U + len;
    ring_payload_ += len;
    taskEXIT_CRITICAL(&ring_mux_);
  }

  const ApCsw* FindSampCsw(uint8_t ap) const
  {
    for (uint8_t i = 0; i < samp_ap_count_; ++i)
    {
      if (samp_csw_[i].ap == ap)
      {
        return &samp_csw_[i];
      }
    }
    return nullptr;
  }

  /// 读一拍里到期的条目，失败的在 marks 里置位。
  void SampleTick(const uint16_t* order, uint16_t due, uint8_t* values, uint8_t* marks)
  {
    if (xSemaphoreTake(swd_mutex_, pdMS_TO_TICKS(1000)) != pdTRUE)
    {
      for (uint16_t i = 0; i < due; ++i)
      {
        marks[i >> 3] |= static_cast<uint8_t>(1U << (i & 7U));
      }
      return;
    }
    ApState ap{};
    ap.ap = 0xFFU;
    for (uint16_t i = 0; i < due; ++i)
    {
      const Item& it = sub_[order[i]].item;
      const ApCsw* csw = FindSampCsw(it.ap);
      if (ap.ap != it.ap)
      {
        ap.ap = it.ap;
        ap.base_csw = (csw != nullptr) ? csw->base_csw : 0U;
        ap.valid = (csw != nullptr) && csw->valid;
        ap.written = false;
      }
      if (ReadItem(ap, it, values) != kItemOk)
      {
        marks[i >> 3] |= static_cast<uint8_t>(1U << (i & 7U));
      }
      values += it.size;
    }
    swd_.InvalidateSelectCache();
    xSemaphoreGive(swd_mutex_);
  }

  /// 采样主循环：第 k 拍的目标时刻为 start + k × period，不累计漂移；来不及时跳过节拍，不补做。
  void RunSampling(uint32_t gen)
  {
    uint64_t k = 0U;
    const uint64_t start = start_us_;
    const uint32_t period = period_us_;
    const uint16_t count = sub_count_;
    uint8_t frame[kMaxFrameBytes];
    uint16_t order[kMaxItems];

    while (active_.load(std::memory_order_acquire) && gen_.load(std::memory_order_acquire) == gen)
    {
      SleepUntil(start + k * period, active_);
      if (!active_.load(std::memory_order_acquire) || gen_.load(std::memory_order_acquire) != gen)
      {
        break;
      }

      const uint64_t tick_us = static_cast<uint64_t>(esp_timer_get_time());
      uint16_t due = 0U;
      uint16_t values_size = 0U;
      for (uint16_t i = 0; i < count; ++i)
      {
        if ((k + sub_[i].phase) % sub_[i].div == 0U)
        {
          order[due++] = i;
          values_size += sub_[i].item.size;
        }
      }

      if (due != 0U)
      {
        // 帧：节拍序号 u32 + 时间戳 u48 + 状态位图 + 各到期条目的值。
        const uint16_t bitmap = static_cast<uint16_t>((due + 7U) / 8U);
        const uint16_t base = 10U + bitmap;
        Put32(frame, static_cast<uint32_t>(k));
        Put48(frame + 4, tick_us);
        std::memset(frame + 10, 0, bitmap + values_size);
        SampleTick(order, due, frame + base, frame + 10);
        PushFrame(frame, static_cast<uint16_t>(base + values_size));
      }

      const uint64_t done = static_cast<uint64_t>(esp_timer_get_time());
      ++k;
      if (done > start + k * period)
      {
        const uint64_t next_k = (done - start) / period + 1U;
        taskENTER_CRITICAL(&ring_mux_);
        skipped_ += static_cast<uint32_t>(next_k - k);
        taskEXIT_CRITICAL(&ring_mux_);
        k = next_k;
      }

      // 采样归发起它的传输所有：USB 发起的在总线复位后停止，串口发起的在租期过后停止。
      // TCP 发起的由 TCP 服务在客户端断开时调用 OnTransportClosed 停止。
      if (owner_ == Transport::USB && usb_reset_fn_ != nullptr &&
          usb_reset_fn_(usb_reset_ctx_) != usb_reset_base_)
      {
        OnTransportClosed();
        break;
      }
      if (owner_ == Transport::SERIAL &&
          NowMs() - last_serial_ms_.load(std::memory_order_acquire) > kSerialLeaseMs)
      {
        OnTransportClosed();
        break;
      }
    }
  }

  LibXR::Debug::Swd& swd_;
  uint16_t max_packet_;

  std::atomic<bool> active_{false};
  std::atomic<uint32_t> gen_{0};
  std::atomic<uint32_t> last_serial_ms_{0};
  Transport transport_ = Transport::USB;
  Transport owner_ = Transport::USB;
  bool connected_ = false;

  SemaphoreHandle_t swd_mutex_ = nullptr;
  SemaphoreHandle_t wake_ = nullptr;
  TaskHandle_t task_ = nullptr;

  ResetCountFn usb_reset_fn_ = nullptr;
  void* usb_reset_ctx_ = nullptr;
  uint32_t usb_reset_base_ = 0U;

  ApState ap_[kMaxAps];
  uint8_t ap_count_ = 0U;

  SubItem sub_[kMaxItems];
  uint16_t sub_count_ = 0U;
  uint32_t period_us_ = 0U;
  bool stop_on_full_ = false;
  bool config_valid_ = false;
  ApCsw samp_csw_[kMaxAps];
  uint8_t samp_ap_count_ = 0U;
  uint64_t start_us_ = 0U;
  uint32_t access_us_ = 0U;  // 标定的单次访问耗时，0 表示还没有标定

  portMUX_TYPE ring_mux_ = portMUX_INITIALIZER_UNLOCKED;
  uint8_t ring_[kRingBytes] = {};
  uint32_t ring_head_ = 0U;
  uint32_t ring_tail_ = 0U;
  uint32_t ring_used_ = 0U;
  uint32_t ring_payload_ = 0U;
  uint32_t dropped_ = 0U;
  uint32_t skipped_ = 0U;
};

}  // namespace XrDap
