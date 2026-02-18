#ifndef FREERTOS_H_MOCK
#define FREERTOS_H_MOCK

#include <cstdint>
#include <cstddef>

typedef uint32_t TickType_t;
typedef void* TaskHandle_t;
typedef void* QueueHandle_t;
typedef int BaseType_t;
typedef unsigned int UBaseType_t;

#define pdTRUE 1
#define pdFALSE 0
#define pdPASS pdTRUE
#define pdFAIL pdFALSE
#define portMAX_DELAY 0xFFFFFFFF

#define pdMS_TO_TICKS(ms) ((TickType_t)(ms))

#endif // FREERTOS_H_MOCK
