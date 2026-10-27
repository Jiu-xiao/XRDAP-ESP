// SPDX-License-Identifier: Apache-2.0
#pragma once

// 极简测试框架：TEST 登记用例，CHECK 记录失败并继续 / Minimal test harness: TEST
// registers a case, CHECK records a failure and carries on.

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace Test
{
using Bytes = std::vector<uint8_t>;

struct Case
{
  const char* name;
  void (*fn)();
};

std::vector<Case>& Registry();
void Fail(const char* file, int line, const std::string& what);

inline std::string Hex(const Bytes& b)
{
  std::string s;
  char tmp[4];
  for (uint8_t v : b)
  {
    std::snprintf(tmp, sizeof(tmp), "%02X ", v);
    s += tmp;
  }
  return s;
}
}  // namespace Test

#define TEST(name)                                       \
  static void name();                                    \
  static const bool name##_registered =                  \
      (Test::Registry().push_back({#name, name}), true); \
  static void name()

#define CHECK(cond)                                       \
  do                                                      \
  {                                                       \
    if (!(cond))                                          \
    {                                                     \
      Test::Fail(__FILE__, __LINE__, "CHECK(" #cond ")"); \
    }                                                     \
  } while (0)

#define CHECK_BYTES(actual, expected)                                       \
  do                                                                        \
  {                                                                         \
    const Test::Bytes a_ = (actual), e_ = (expected);                       \
    if (a_ != e_)                                                           \
    {                                                                       \
      Test::Fail(__FILE__, __LINE__,                                        \
                 "got " + Test::Hex(a_) + "\n      want " + Test::Hex(e_)); \
    }                                                                       \
  } while (0)
