// SPDX-License-Identifier: Apache-2.0
#include "test.hpp"
#include "timer_deadline.hpp"

TEST(TimerOnTimeAndSubperiodDelayKeepPhase)
{
  const auto a = XrDap::AdvanceTimerDeadline(1000U, 1000U, 1000U);
  CHECK(a.elapsed_ticks == 1U && a.late_us == 0U && a.next_alarm == 2000U);
  const auto b = XrDap::AdvanceTimerDeadline(2000U, 2999U, 1000U);
  CHECK(b.elapsed_ticks == 1U && b.late_us == 999U && b.next_alarm == 3000U);
}

TEST(TimerCoalescesWholePeriodsWithoutDrift)
{
  const auto a = XrDap::AdvanceTimerDeadline(1000U, 4250U, 1000U);
  CHECK(a.elapsed_ticks == 4U && a.late_us == 250U && a.next_alarm == 5000U);
  const auto b = XrDap::AdvanceTimerDeadline(a.next_alarm, 6000U, 1000U);
  CHECK(b.elapsed_ticks == 2U && b.late_us == 0U && b.next_alarm == 7000U);
}

TEST(TimerCounterPasses32BitMicroseconds)
{
  const uint64_t alarm = (1ULL << 32) + 1000U;
  const auto a = XrDap::AdvanceTimerDeadline(alarm, alarm + 10123U, 1000U);
  CHECK(a.elapsed_ticks == 11U && a.late_us == 123U);
  CHECK(a.next_alarm == alarm + 11000U);
}
