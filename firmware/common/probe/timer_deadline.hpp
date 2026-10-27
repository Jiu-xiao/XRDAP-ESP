// SPDX-License-Identifier: Apache-2.0
#pragma once

#include <cstdint>

namespace XrDap
{
struct TimerDeadline
{
  uint32_t elapsed_ticks;
  uint32_t late_us;  // Lateness relative to the latest elapsed tick.
  uint64_t next_alarm;
};

// Free-running GPTimer at 1 MHz, period > 0, now >= alarm. Hardware auto-reload
// must be disabled: otherwise the counter loses the number of elapsed periods.
inline TimerDeadline AdvanceTimerDeadline(uint64_t alarm, uint64_t now, uint32_t period)
{
  const uint64_t delay = now - alarm;
  const uint64_t elapsed = 1U + delay / period;
  return {static_cast<uint32_t>(elapsed), static_cast<uint32_t>(delay % period),
          alarm + elapsed * period};
}
}  // namespace XrDap
