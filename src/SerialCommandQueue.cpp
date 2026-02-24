#include "SerialCommandQueue.h"
#include "RuntimeLog.h"

static const char* TAG = "SerialQueue";

SerialCommandQueue::SerialCommandQueue() : commandQueue(nullptr) {
}

SerialCommandQueue::~SerialCommandQueue() {
    if (commandQueue != nullptr) {
        vQueueDelete(commandQueue);
        commandQueue = nullptr;
    }
}

bool SerialCommandQueue::begin() {
    if (commandQueue != nullptr) {
        return true;  // Already initialized
    }
    
    commandQueue = xQueueCreate(QUEUE_SIZE, MAX_CMD_LENGTH);
    if (commandQueue == nullptr) {
        ESP_LOGE(TAG, "Failed to create queue");
        return false;
    }
    
    ESP_LOGI(TAG, "Queue created successfully");
    return true;
}

bool SerialCommandQueue::sendCommand(const char* cmd, TickType_t timeoutMs) {
    if (commandQueue == nullptr) {
        ESP_LOGE(TAG, "Queue not initialized");
        return false;
    }
    
    if (cmd == nullptr || strlen(cmd) == 0) {
        return false;  // Empty command
    }
    
    if (strlen(cmd) >= MAX_CMD_LENGTH) {
        ESP_LOGW(TAG, "Command too long, truncating");
    }
    
    BaseType_t result = xQueueSend(commandQueue, cmd, timeoutMs);
    if (result != pdTRUE) {
        ESP_LOGW(TAG, "Failed to send command - queue may be full");
        return false;
    }
    
    return true;
}

bool SerialCommandQueue::receiveCommand(char* buffer, size_t bufferSize, TickType_t timeoutMs) {
    if (commandQueue == nullptr || buffer == nullptr || bufferSize < MAX_CMD_LENGTH) {
        return false;
    }
    
    BaseType_t result = xQueueReceive(commandQueue, buffer, timeoutMs);
    if (result == pdTRUE) {
        // Ensure null termination
        buffer[MAX_CMD_LENGTH - 1] = '\0';
        return true;
    }
    
    return false;
}

bool SerialCommandQueue::isEmpty() const {
    if (commandQueue == nullptr) {
        return true;
    }
    return (uxQueueMessagesWaiting(commandQueue) == 0);
}

UBaseType_t SerialCommandQueue::getCount() const {
    if (commandQueue == nullptr) {
        return 0;
    }
    return uxQueueMessagesWaiting(commandQueue);
}

void SerialCommandQueue::clear() {
    if (commandQueue == nullptr) {
        return;
    }
    
    char buffer[MAX_CMD_LENGTH];
    while (xQueueReceive(commandQueue, buffer, 0) == pdTRUE) {
        // Discard commands
    }
}
