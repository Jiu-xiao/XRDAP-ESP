// SPDX-License-Identifier: Apache-2.0
#include "heap_report.hpp"

#include <cstdio>

#include "esp_heap_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

namespace XrDap
{
namespace
{
constexpr UBaseType_t kMaxTasks = 24U;
}  // namespace

void EspHeapReport::Write(ConsoleWriteFn write, void* ctx)
{
  char line[80];
  constexpr uint32_t kCaps = MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT;
  int n = snprintf(line, sizeof(line), "堆：空闲 %u 字节，开机以来最低 %u 字节\r\n",
                   static_cast<unsigned>(heap_caps_get_free_size(kCaps)),
                   static_cast<unsigned>(heap_caps_get_minimum_free_size(kCaps)));
  write(ctx, line, static_cast<size_t>(n));

  // ESP-IDF 的栈以字节为单位，最高水位即剩余最少的字节数 / ESP-IDF stacks are counted in
  // bytes, so the high-water mark is the fewest bytes left.
  TaskStatus_t tasks[kMaxTasks];
  const UBaseType_t count = uxTaskGetSystemState(tasks, kMaxTasks, nullptr);
  for (UBaseType_t i = 0; i < count; ++i)
  {
    n = snprintf(line, sizeof(line), "  %-16s 栈最少剩 %u 字节\r\n", tasks[i].pcTaskName,
                 static_cast<unsigned>(tasks[i].usStackHighWaterMark));
    write(ctx, line, static_cast<size_t>(n));
  }
  if (extra_ != nullptr)
  {
    extra_(write, ctx, extra_ctx_);
  }
}

}  // namespace XrDap
