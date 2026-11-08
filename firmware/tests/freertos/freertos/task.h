// SPDX-License-Identifier: Apache-2.0
#pragma once

// 电脑端测试用的 FreeRTOS 任务接口；实现在 test_serial_mux.cpp / FreeRTOS task interface
// for the PC tests; implemented in test_serial_mux.cpp.

#include "freertos/FreeRTOS.h"

BaseType_t xTaskCreatePinnedToCore(void (*entry)(void*), const char* name, uint32_t stack,
                                   void* arg, UBaseType_t priority, void* handle,
                                   BaseType_t core);
TickType_t xTaskGetTickCount();
void vTaskDelay(TickType_t ticks);
