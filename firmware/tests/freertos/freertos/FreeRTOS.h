// SPDX-License-Identifier: Apache-2.0
#pragma once

// 电脑端测试用的 FreeRTOS 类型：只有 SerialMux 用到的部分 / FreeRTOS types for the PC
// tests: only what SerialMux uses.

#include <cstdint>

using TickType_t = uint32_t;
using BaseType_t = int;
using UBaseType_t = unsigned;

#define pdPASS 1
#define portTICK_PERIOD_MS 1U
#define pdMS_TO_TICKS(ms) (ms)
