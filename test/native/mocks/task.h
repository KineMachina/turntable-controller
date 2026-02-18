#ifndef TASK_H_MOCK
#define TASK_H_MOCK

#include "FreeRTOS.h"

inline void vTaskDelay(TickType_t) {}
inline void vTaskDelete(TaskHandle_t) {}
inline TickType_t xTaskGetTickCount() { return 0; }

inline BaseType_t xTaskCreatePinnedToCore(
    void (*)(void*), const char*, uint32_t, void*,
    UBaseType_t, TaskHandle_t*, BaseType_t)
{
    return pdPASS;
}

#endif // TASK_H_MOCK
