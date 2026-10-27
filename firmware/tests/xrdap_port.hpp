// SPDX-License-Identifier: Apache-2.0
#pragma once

// 电脑端测试用的 XRDAP-Core 平台头：单线程，锁为空操作 / XRDAP-Core platform header for
// the PC tests: single-threaded, the lock does nothing.

#include <cstdint>

namespace XrDap::Vendor
{
class LoopSampling;
}  // namespace XrDap::Vendor

namespace XrDap::Session
{
class MultiTransport;
}  // namespace XrDap::Session

namespace XrDap::Port
{
/// 主机测试按单线程运行 / The host tests run single-threaded.
using SamplerPolicy = Vendor::LoopSampling;

/// 传输策略：测试按多传输 / Transport policy: the tests use several transports.
using TransportPolicy = Session::MultiTransport;


inline constexpr uint32_t kRingBytes = 4096U;

class SpinLock
{
 public:
  void Lock() {}
  void Unlock() {}
};

}  // namespace XrDap::Port
