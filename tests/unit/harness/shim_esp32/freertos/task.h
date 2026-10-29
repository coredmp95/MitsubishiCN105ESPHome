#pragma once
#include "FreeRTOS.h"
typedef void* TaskHandle_t;
extern "C" { void vTaskDelay(TickType_t); TickType_t xTaskGetTickCount(void);
 BaseType_t xTaskNotifyGiveIndexed(TaskHandle_t, UBaseType_t); uint32_t ulTaskNotifyTakeIndexed(UBaseType_t, BaseType_t, TickType_t);
 void vTaskNotifyGiveIndexedFromISR(TaskHandle_t, UBaseType_t, BaseType_t*); }
extern "C" { TaskHandle_t xTaskGetCurrentTaskHandle(void); }
typedef void (*TaskFunction_t)(void*); typedef uint32_t StackType_t; typedef struct { int dummy; } StaticTask_t;
