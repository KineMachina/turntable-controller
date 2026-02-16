#ifndef SERIAL_COMMAND_QUEUE_H
#define SERIAL_COMMAND_QUEUE_H

#include "Arduino.h"
#include <FreeRTOS.h>
#include <queue.h>

/**
 * SerialCommandQueue - FreeRTOS queue wrapper for serial commands
 * Provides thread-safe command buffering from serial input
 */
class SerialCommandQueue {
public:
    static const size_t MAX_CMD_LENGTH = 64;  // Maximum command length
    
private:
    QueueHandle_t commandQueue;
    static const size_t QUEUE_SIZE = 10;  // Maximum pending commands
    
public:
    /**
     * Constructor
     */
    SerialCommandQueue();
    
    /**
     * Destructor
     */
    ~SerialCommandQueue();
    
    /**
     * Initialize the queue
     * @return true if successful, false otherwise
     */
    bool begin();
    
    /**
     * Send a command to the queue (non-blocking)
     * @param cmd Command string (will be copied)
     * @param timeoutMs Timeout in milliseconds (0 = immediate, portMAX_DELAY = wait forever)
     * @return true if sent successfully, false if timeout or error
     */
    bool sendCommand(const char* cmd, TickType_t timeoutMs = 0);
    
    /**
     * Receive a command from the queue (blocking)
     * @param buffer Output buffer (must be at least MAX_CMD_LENGTH bytes)
     * @param bufferSize Size of output buffer
     * @param timeoutMs Timeout in milliseconds (portMAX_DELAY = wait forever)
     * @return true if received successfully, false if timeout
     */
    bool receiveCommand(char* buffer, size_t bufferSize, TickType_t timeoutMs = portMAX_DELAY);
    
    /**
     * Check if queue is empty
     * @return true if empty, false otherwise
     */
    bool isEmpty() const;
    
    /**
     * Get number of items in queue
     * @return Number of items
     */
    UBaseType_t getCount() const;
    
    /**
     * Clear all pending commands
     */
    void clear();
};

#endif // SERIAL_COMMAND_QUEUE_H
