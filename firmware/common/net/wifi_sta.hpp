// SPDX-License-Identifier: Apache-2.0
#pragma once

// Wi-Fi STA 连接，以及热点参数在 NVS 中的存取。
//
// Wi-Fi STA connection and NVS storage of the access-point credentials.

#include <cstdint>

#include "esp_err.h"
#include "wifi_settings.hpp"

namespace XrDap
{
/// @brief 初始化默认 NVS 分区（热点参数，以及 Wi-Fi 的 PHY 校准数据都在里面）。分区已满
///        或格式版本不符时擦除重建，在状态里报告 storage_repaired，清除来源记为
///        STORAGE_REPAIR / Initialise the default NVS partition (it holds the credentials
///        and the Wi-Fi PHY calibration). When the partition is full or has a newer
///        format version it is erased and rebuilt; the status then reports
///        storage_repaired and the erase source STORAGE_REPAIR.
/// @return ESP_OK，或 nvs_flash 的错误；出错时保存、清除和读取热点参数都会失败 / ESP_OK,
///         or an nvs_flash error; on error saving, clearing and reading the credentials
///         fail.
/// @note app_main 在启动命令行、0x86 和状态灯之前调用一次 / app_main calls this once
///       before it starts the console, 0x86 and the status light.
esp_err_t WifiStaInitStorage();

/// @brief 以 STA 模式连接 Wi-Fi，热点参数从 NVS 读取（命名空间 "rmlink"，键 "wifi"）/
///        Connect in STA mode using credentials from NVS (namespace "rmlink", key
///        "wifi").
/// @param timeout_ms 等待拿到 IP 的时间 / Time to wait for an IP.
/// @return ESP_OK：已连接 / connected.
/// @return ESP_ERR_TIMEOUT：协议栈已启动但未连上，后台继续重连 / Stack started but not
///         connected; reconnecting in the background.
/// @return 其他 / Other：无可用热点参数，Wi-Fi 未启动 / No usable credentials, Wi-Fi not
///         started.
/// @note 在 WifiStaInitStorage 之后调用一次，阻塞调用任务至多 timeout_ms；协议栈初始化
///       失败时中止（ESP_ERROR_CHECK）/ Call once after WifiStaInitStorage; blocks the
///       calling task for up to timeout_ms. Aborts (ESP_ERROR_CHECK) if the stack cannot
///       be initialised.
esp_err_t WifiStaStart(uint32_t timeout_ms);

/// @brief 命令行和 0x86 用的 Wi-Fi 设置，热点参数存在 NVS / Wi-Fi settings for the
///        console and 0x86; the credentials live in NVS.
/// @note 返回的对象一直有效，方法可在任意任务调用 / The object stays valid; its methods
///       are callable from any task.
WifiSettings& WifiStaSettings();

/// 读取连接阶段；不读 NVS，适合周期性查询，可在任意任务调用 / Read the link stage; does
/// not touch NVS, so it is cheap to poll; callable from any task.
WifiLink WifiStaGetLink();
}  // namespace XrDap
