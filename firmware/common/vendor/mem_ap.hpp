// SPDX-License-Identifier: Apache-2.0
#pragma once

// 经 SWD 访问 MEM-AP：沿指针链读写目标内存。自己管理 SELECT、CSW、TAR，不走 DAP 的传输
// 流水线。命令线程和采样任务共用一条 SWD 链路，访问前要持有 Lock；Lock 释放时让 SWD 的
// SELECT 缓存失效，上位机须把 SELECT、CSW、TAR 视为未知。
//
// MEM-AP access over SWD: reads and writes target memory along pointer chains. It manages
// SELECT, CSW and TAR itself, bypassing the DAP transfer pipeline. The command thread and
// the sampling task share one SWD link and must hold a Lock while accessing it; releasing
// the Lock invalidates the SWD SELECT cache, so the host must treat SELECT, CSW and TAR
// as unknown.

#include <cstdint>

#include "debug/swd.hpp"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "vendor_protocol.hpp"

namespace XrDap::Vendor
{

/// 共享 SWD 链路上的 MEM-AP 访问 / MEM-AP access on a shared SWD link.
class MemAp
{
 public:
  /// 一个 AP 的 CSW 状态 / CSW state of one AP.
  struct ApState
  {
    /// AP 号；0xFF 表示还没有选 AP / AP index; 0xFF before any AP is chosen.
    uint8_t ap = 0xFF;
    /// 读出的 CSW 原值，保留 Size、AddrInc 以外的位 / CSW as read; bits other than Size
    /// and AddrInc are preserved.
    uint32_t base_csw = 0;
    /// base_csw 读成功 / base_csw was read successfully.
    bool valid = false;
    /// 最近一次写入的 CSW，用来省掉重复写 / Last CSW written, to skip repeated writes.
    uint32_t written_csw = 0;
    /// written_csw 有效 / written_csw is valid.
    bool written = false;
  };

  /// 持有 SWD 链路的作用域锁 / Scoped lock on the SWD link.
  class Lock
  {
   public:
    /// @brief 最多等 timeout_ms 取得链路 / Take the link, waiting up to timeout_ms.
    Lock(MemAp& mem, uint32_t timeout_ms);
    /// 释放链路并让 SELECT 缓存失效 / Release the link and invalidate the SELECT cache.
    ~Lock();
    Lock(const Lock&) = delete;
    Lock& operator=(const Lock&) = delete;
    /// 是否取得了链路 / Whether the link was taken.
    explicit operator bool() const { return held_; }

   private:
    MemAp& mem_;
    bool held_;
  };

  /// @param swd SWD 实现，生命周期需长于本对象 / SWD implementation; must outlive this
  ///        object.
  explicit MemAp(LibXR::Debug::Swd& swd) : swd_(swd) {}

  /// 创建互斥量，在使用 Lock 之前调用一次 / Create the mutex; call once before any Lock.
  void Begin() { mutex_ = xSemaphoreCreateMutex(); }

  /// @brief 让 SWD 的 SELECT 缓存失效；目标断开或换了传输时调用，不需要 Lock /
  ///        Invalidate the SWD SELECT cache; call when the target disconnects or the
  ///        transport changes. No Lock needed.
  void ForgetSelect() { swd_.InvalidateSelectCache(); }

  // 以下方法都要求调用方持有 Lock / Every method below requires the caller to hold a
  // Lock.

  /// 清空命令用的 AP 缓存，每条命令开始时调用 / Clear the per-command AP cache; call at
  /// the start of every command.
  void ResetCache();
  /// @brief 本命令里第一次用到某个 AP 时读一次它的 CSW / Read an AP's CSW the first time
  ///        a command uses it.
  /// @return 读不到 CSW 时返回 nullptr；缓存满时复用最后一格 / nullptr when the CSW
  ///         cannot be read; the last slot is reused when the cache is full.
  ApState* EnsureAp(uint8_t ap);
  /// 读一个 AP 的 CSW，失败时清粘滞错误再试一次 / Read an AP's CSW; on failure clear the
  /// sticky errors and retry once.
  bool ReadCsw(uint8_t ap, uint32_t& csw);
  /// 读一个条目，返回条目状态 / Read one item; returns the item status.
  uint8_t Read(const Item& it, uint8_t* out);
  /// 写一个条目，返回条目状态 / Write one item; returns the item status.
  uint8_t Write(const Item& it, const uint8_t* value);
  /// 用给定的 AP 状态读一个条目（采样用），返回条目状态 / Read one item with a given AP
  /// state (used by sampling); returns the item status.
  uint8_t ReadItem(ApState& ap, const Item& it, uint8_t* out);
  /// @brief 标定一次访问（写 TAR + 读 AP 寄存器）的耗时；读的是 CSW，没有副作用 /
  ///        Calibrate one access (TAR write + AP register read); reads CSW, no side
  ///        effects.
  /// @param us 成功时为单次访问耗时（微秒，至少 1）/ On success the per-access time in
  ///        microseconds, at least 1.
  bool Calibrate(uint8_t ap, uint32_t& us);

 private:
  using Ack = LibXR::Debug::SwdProtocol::Ack;

  bool SetSize(ApState& st, uint8_t size);
  bool ReadWord(uint32_t addr, uint32_t& val);
  bool WriteWord(uint32_t addr, uint32_t val);
  bool CheckCtrlStat();
  void ClearSticky();
  bool Resolve(const Item& it, uint32_t& addr, uint8_t& st);
  uint8_t ReadValue(ApState& ap, uint8_t size, uint32_t addr, uint8_t* out);

  LibXR::Debug::Swd& swd_;
  SemaphoreHandle_t mutex_ = nullptr;
  ApState ap_[kMaxAps];
  uint8_t ap_count_ = 0U;
};

}  // namespace XrDap::Vendor
