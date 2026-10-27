// SPDX-License-Identifier: Apache-2.0
#pragma once

// 命令行 heap 命令的 ESP 实现：内部 RAM 的空闲堆、开机以来的最低空闲堆，以及每个任务栈
// 的最高水位（剩余最少的字节数）。用来在硬件上确认堆和栈的余量（ESP32-S2 的内部 RAM 只有
// 320 KB）。
//
// ESP implementation of the console's heap command: the free internal heap, the minimum
// free heap since boot, and each task's stack high-water mark (the fewest bytes left).
// Used to confirm the heap and stack margins on hardware (the ESP32-S2 has only 320 KB of
// internal RAM).

#include "console_core.hpp"

namespace XrDap
{

/// 堆和任务栈的运行状态 / Runtime status of the heap and the task stacks.
class EspHeapReport final : public HeapReport
{
 public:
  using ExtraReport = void (*)(ConsoleWriteFn, void*, void*);
  void SetExtraReport(ExtraReport report, void* ctx)
  {
    extra_ = report;
    extra_ctx_ = ctx;
  }
  /// @note 在命令行任务里调用；栈上用约 1 KB 放任务列表 / Called in the console task;
  ///       uses about 1 KB of stack for the task list.
  void Write(ConsoleWriteFn write, void* ctx) override;

 private:
  ExtraReport extra_ = nullptr;
  void* extra_ctx_ = nullptr;
};

}  // namespace XrDap
