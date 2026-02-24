#include "MotorCommandQueue.h"
#include "RuntimeLog.h"

static const char* TAG = "MotorQueue";

MotorCommandQueue::MotorCommandQueue() : commandQueue(nullptr) {
}

MotorCommandQueue::~MotorCommandQueue() {
    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
    }
}

bool MotorCommandQueue::begin() {
    if (commandQueue != nullptr) {
        return true;  // Already initialized
    }
    
    commandQueue = xQueueCreate(QUEUE_SIZE, sizeof(MotorCommand));
    if (commandQueue == nullptr) {
        ESP_LOGE(TAG, "Failed to create queue");
        return false;
    }
    
    ESP_LOGI(TAG, "Queue created successfully");
    return true;
}

bool MotorCommandQueue::sendCommand(const MotorCommand& cmd, TickType_t timeoutMs) {
    if (commandQueue == nullptr) {
        ESP_LOGE(TAG, "Queue not initialized");
        return false;
    }
    
    BaseType_t result = xQueueSend(commandQueue, &cmd, timeoutMs);
    if (result != pdTRUE) {
        ESP_LOGW(TAG, "Failed to send command (type=%d) - queue may be full", (int)cmd.type);
        return false;
    }
    
    return true;
}

bool MotorCommandQueue::receiveCommand(MotorCommand& cmd, TickType_t timeoutMs) {
    if (commandQueue == nullptr) {
        return false;
    }
    
    BaseType_t result = xQueueReceive(commandQueue, &cmd, timeoutMs);
    return (result == pdTRUE);
}

bool MotorCommandQueue::isEmpty() const {
    if (commandQueue == nullptr) {
        return true;
    }
    return (uxQueueMessagesWaiting(commandQueue) == 0);
}

UBaseType_t MotorCommandQueue::getCount() const {
    if (commandQueue == nullptr) {
        return 0;
    }
    return uxQueueMessagesWaiting(commandQueue);
}

void MotorCommandQueue::clear() {
    if (commandQueue == nullptr) {
        return;
    }
    
    MotorCommand cmd;
    while (xQueueReceive(commandQueue, &cmd, 0) == pdTRUE) {
        // Discard commands
    }
}
