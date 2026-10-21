// SPDX-License-Identifier: Apache-2.0
#pragma once

// 命令行（console_core）和 0x86（wifi_command）看到的 Wi-Fi 设置：读状态、保存、清除、
// 安排重启。这里只有接口和规则，不依赖 ESP-IDF，电脑上的测试用假实现；固件里的实现在
// wifi_sta.cpp。
//
// The Wi-Fi settings as the console (console_core) and 0x86 (wifi_command) see them:
// read the status, save, clear, schedule the restart. Only the interface and its rules
// live here, without ESP-IDF, so PC tests can use a fake; the firmware implementation is
// in wifi_sta.cpp.

#include <cstdint>
#include <cstring>

namespace XrDap
{

/// 热点名最长字节数 / Longest SSID in bytes.
constexpr size_t kSsidMax = 32U;
/// WPA 密码最短字节数 / Shortest WPA password in bytes.
constexpr size_t kPasswordMin = 8U;
/// WPA 密码最长字节数 / Longest WPA password in bytes.
constexpr size_t kPasswordMax = 63U;

/// @brief 热点参数长度是否合法：热点名 1 到 32 字节，密码为空（开放网络）或 8 到 63 字节
///        / Whether the credential lengths are valid: SSID 1 to 32 bytes, password empty
///        (open network) or 8 to 63 bytes.
inline bool CredentialsValid(const char* ssid, const char* password)
{
  const size_t ssid_len = strlen(ssid);
  const size_t pw_len = strlen(password);
  return ssid_len >= 1U && ssid_len <= kSsidMax &&
         (pw_len == 0U || (pw_len >= kPasswordMin && pw_len <= kPasswordMax));
}

/// Wi-Fi 连接阶段 / Wi-Fi link stage.
enum class WifiLink : uint8_t
{
  OFF,      ///< 没有热点参数，Wi-Fi 未启动 / No credentials, Wi-Fi not started.
  JOINING,  ///< 已启动，还没拿到 IP / Started, no IP yet.
  UP,       ///< 已拿到 IP / IP obtained.
};

/// 是什么清除了已保存的热点参数，存在 NVS 里，跨重启保留；数值即 0x86 状态里的字节 /
/// What erased the saved credentials; kept in NVS across restarts. The values are the
/// byte in the 0x86 status.
enum class EraseSource : uint8_t
{
  NONE = 0,            ///< 没有清除过，或记录丢失 / Never erased, or no record.
  BUTTON = 1,          ///< 板上按键长按 / Board button long press.
  CONSOLE = 2,         ///< 命令行 wifi clear / Console wifi clear.
  VENDOR_COMMAND = 3,  ///< 0x86 清除 / 0x86 clear.
  STORAGE_REPAIR = 4,  ///< NVS 损坏，擦除重建 / NVS damaged, erased and rebuilt.
};

/// 上次复位的原因；数值即 0x86 状态里的字节 / Cause of the last reset; the values are
/// the byte in the 0x86 status.
enum class ResetReason : uint8_t
{
  UNKNOWN = 0,   ///< 未知 / Unknown.
  POWER_ON = 1,  ///< 上电 / Power-on.
  SOFTWARE = 2,  ///< 软件重启（改设置后）/ Software restart (after a setting change).
  WATCHDOG = 3,  ///< 看门狗 / Watchdog.
  PANIC = 4,     ///< 异常 / Panic.
  BROWNOUT = 5,  ///< 欠压 / Brown-out.
  OTHER = 6,     ///< 其他（外部复位脚、深睡、USB/JTAG 等）/ Other (reset pin, deep
                 ///< sleep, USB/JTAG...).
};

/// Wi-Fi 状态快照 / Wi-Fi status snapshot.
struct WifiStatus
{
  /// Wi-Fi 是否已启动（有热点参数才会启动）/ Wi-Fi started (only with saved
  /// credentials).
  bool started;
  /// 是否已拿到 IP / An IP has been obtained.
  bool connected;
  /// 已保存的热点名，没有则为空串 / Saved SSID; empty if none.
  char ssid[kSsidMax + 1U];
  /// 已拿到的 IPv4 地址，网络字节序 / Obtained IPv4 address, network byte order.
  uint8_t ip[4];
  /// 信号强度，未连接时为 0 / Signal strength; 0 when not connected.
  int8_t rssi;
  /// 本次启动时 NVS 损坏，已擦除重建 / NVS was damaged at this boot and was erased and
  /// rebuilt.
  bool storage_repaired;
  /// 上次清除热点参数的来源 / What last erased the credentials.
  EraseSource last_erase;
  /// 上次复位的原因 / Cause of the last reset.
  ResetReason reset_reason;
};

/// Wi-Fi 设置 / Wi-Fi settings.
/// @note 实现的每个方法都必须可在任意任务调用（命令行任务、各传输的 DAP 执行任务），不能
///       在中断里调用 / Every method of an implementation must be callable from any task
///       (the console task, each transport's DAP task), not from an ISR.
class WifiSettings
{
 public:
  /// 保存结果 / Save result.
  enum class SaveResult : uint8_t
  {
    OK,       ///< 已写入 / Written.
    INVALID,  ///< 长度不合法，见 CredentialsValid / Invalid length, see CredentialsValid.
    STORAGE,  ///< 存储读写失败 / Storage read or write failed.
  };

  /// @brief 读当前状态 / Read the current status.
  virtual WifiStatus Status() = 0;

  /// @brief 保存热点参数，重启后生效 / Save the credentials; they take effect after a
  ///        restart.
  virtual SaveResult Save(const char* ssid, const char* password) = 0;

  /// @brief 清除已保存的热点参数并记下来源；本来就没有也算成功 / Erase the saved
  ///        credentials and record the source; succeeds even if none exist.
  /// @param source 谁要求清除 / Who asked for the erase.
  /// @return 存储读写失败时返回 false / False when the storage access failed.
  virtual bool Clear(EraseSource source) = 0;

  /// @brief 约 0.5 s 后重启，让新设置生效；立即返回 / Restart in about 0.5 s so the new
  ///        setting takes effect; returns at once.
  virtual void RestartLater() = 0;

 protected:
  ~WifiSettings() = default;
};

}  // namespace XrDap
