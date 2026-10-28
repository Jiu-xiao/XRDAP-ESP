// SPDX-License-Identifier: Apache-2.0
#pragma once

// 测试用的假对象：Wi-Fi 设置、DAP 实例、内存字节流 / Test doubles: Wi-Fi settings, a DAP
// instance, an in-memory byte stream.

#include <cstring>
#include <string>

#include "test.hpp"
#include "wifi_settings.hpp"

namespace Test
{

/// 记录调用的 Wi-Fi 设置；Save 按 CredentialsValid 判断长度 / Wi-Fi settings that record
/// calls; Save checks lengths with CredentialsValid.
class FakeWifiSettings final : public XrDap::WifiSettings
{
 public:
  XrDap::WifiStatus status = {};
  bool storage_ok = true;
  std::string saved_ssid;
  std::string saved_password;
  int saves = 0;
  int clears = 0;
  XrDap::EraseSource cleared_by = XrDap::EraseSource::NONE;
  int restarts = 0;

  XrDap::WifiStatus Status() override { return status; }

  SaveResult Save(const char* ssid, const char* password) override
  {
    if (!XrDap::CredentialsValid(ssid, password))
    {
      return SaveResult::INVALID;
    }
    if (!storage_ok)
    {
      return SaveResult::STORAGE;
    }
    ++saves;
    saved_ssid = ssid;
    saved_password = password;
    return SaveResult::OK;
  }

  bool Clear(XrDap::EraseSource source) override
  {
    if (!storage_ok)
    {
      return false;
    }
    ++clears;
    cleared_by = source;
    return true;
  }

  void RestartLater() override { ++restarts; }
};

/// 假 DAP：回复 = 请求原样；0x07（DAP_TransferAbort）没有回复 / Fake DAP: the response
/// echoes the request; 0x07 (DAP_TransferAbort) has no response.
struct FakeDap
{
  std::vector<Bytes> requests;
  std::vector<Bytes> controls;

  /// 控制请求的应答:op、状态 0、再回显载荷 / A control answer: the op, status 0, then the
  /// payload echoed.
  int ExecuteControl(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
                     uint16_t& out_len)
  {
    controls.emplace_back(req, req + len);
    out_len = static_cast<uint16_t>(len + 1U < cap ? len + 1U : cap);
    resp[0] = req[0];
    resp[1] = 0U;
    if (out_len > 2U)
    {
      std::memcpy(resp + 2, req + 1, out_len - 2U);
    }
    return 0;
  }

  int ExecuteExternal(const uint8_t* req, uint16_t len, uint8_t* resp, uint16_t cap,
                      uint16_t& out_len)
  {
    requests.emplace_back(req, req + len);
    if (req[0] == 0x07U)
    {
      out_len = 0U;
      return 0;
    }
    out_len = len < cap ? len : cap;
    std::memcpy(resp, req, out_len);
    return 0;
  }
};

/// 内存字节流：从 in 读，写到 out；读完即视为对方关闭 / In-memory byte stream: reads
/// from in, writes to out; running out of input counts as the peer closing.
struct MemoryStream
{
  Bytes in;
  size_t pos = 0;
  Bytes out;

  bool ReadAll(uint8_t* buf, size_t n)
  {
    if (in.size() - pos < n)
    {
      return false;
    }
    std::memcpy(buf, in.data() + pos, n);
    pos += n;
    return true;
  }

  bool WriteAll(const uint8_t* buf, size_t n)
  {
    out.insert(out.end(), buf, buf + n);
    return true;
  }
};

inline void Append(Bytes& b, const Bytes& more)
{
  b.insert(b.end(), more.begin(), more.end());
}

inline Bytes Str(const char* s) { return Bytes(s, s + std::strlen(s)); }

}  // namespace Test
