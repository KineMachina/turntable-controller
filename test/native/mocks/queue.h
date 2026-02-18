#ifndef QUEUE_H_MOCK
#define QUEUE_H_MOCK

#include "FreeRTOS.h"

inline QueueHandle_t xQueueCreate(UBaseType_t, UBaseType_t) { return (QueueHandle_t)1; }
inline void vQueueDelete(QueueHandle_t) {}
inline BaseType_t xQueueSend(QueueHandle_t, const void*, TickType_t) { return pdTRUE; }
inline BaseType_t xQueueReceive(QueueHandle_t, void*, TickType_t) { return pdFALSE; }

#endif // QUEUE_H_MOCK
