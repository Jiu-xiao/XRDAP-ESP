// SPDX-License-Identifier: Apache-2.0
#include <cstring>

#include "test.hpp"

namespace Test
{
namespace
{
int failures = 0;
}  // namespace

std::vector<Case>& Registry()
{
  static std::vector<Case> cases;
  return cases;
}

void Fail(const char* file, int line, const std::string& what)
{
  std::printf("  %s:%d: %s\n", file, line, what.c_str());
  ++failures;
}
}  // namespace Test

// 参数为子串时只跑名字含它的用例 / With an argument, run only cases whose name contains
// it.
int main(int argc, char** argv)
{
  int failed_cases = 0;
  int run = 0;
  for (const Test::Case& c : Test::Registry())
  {
    if (argc > 1 && std::strstr(c.name, argv[1]) == nullptr)
    {
      continue;
    }
    const int before = Test::failures;
    c.fn();
    ++run;
    const bool ok = Test::failures == before;
    failed_cases += ok ? 0 : 1;
    std::printf("%s %s\n", ok ? "ok  " : "FAIL", c.name);
  }
  std::printf("%d cases, %d failed\n", run, failed_cases);
  return failed_cases == 0 ? 0 : 1;
}
