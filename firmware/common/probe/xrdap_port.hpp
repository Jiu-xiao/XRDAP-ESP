// SPDX-License-Identifier: Apache-2.0
#pragma once

// XRDAP-Core 的平台部分：采样帧缓冲的大小。
//
// Platform part of XRDAP-Core: the size of the sample frame buffer.

#include <cstdint>

#include "sdkconfig.h"

namespace XrDap::Port
{
/// 采样帧缓冲字节数，2 的幂。ESP32-S2 的内部 RAM 只有 320 KB，取 Core 默认的 4 KB；别的
/// ESP 芯片 RAM 够大，取 16 KB / Sample frame buffer size, a power of two. The ESP32-S2
/// has only 320 KB of internal RAM and keeps Core's default 4 KB; the other ESP chips
/// have enough RAM for 16 KB.
#if CONFIG_IDF_TARGET_ESP32S2
inline constexpr uint32_t kRingBytes = 4096U;
#else
inline constexpr uint32_t kRingBytes = 16384U;
#endif

}  // namespace XrDap::Port
