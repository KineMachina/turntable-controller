#ifndef MOTOR_COMMAND_QUEUE_H
#define MOTOR_COMMAND_QUEUE_H

#include "Arduino.h"
#include <FreeRTOS.h>
#include <queue.h>

/**
 * Motor Command Types
 * Enumeration of all possible motor commands
 */
enum class MotorCommandType {
    MOVE_TO,                // Move to target position in degrees
    ZERO_POSITION,          // Zero current position
    ENABLE,                 // Enable/disable motor
    SET_SPEED,              // Set maximum speed
    SET_ACCELERATION,       // Set acceleration
    SET_MICROSTEPS,         // Set microstepping
    HOME,                   // Perform homing operation
    GET_STATUS              // Request status update (response via callback/queue)
};

/**
 * Motor Command Structure
 * Contains command type and associated data
 */
struct MotorCommand {
    MotorCommandType type;
    union {
        struct {
            float value;
        } position;
        struct {
            long value;
        } positionSteps;
        struct {
            bool enable;
        } enable;
        struct {
            float speed;
        } speed;
        struct {
            float accel;
        } acceleration;
        struct {
            uint8_t microsteps;
        } microsteps;
    } data;
    
    // Optional callback for status requests (can be nullptr)
    void (*statusCallback)(void* context);
    void* statusContext;
};

/**
 * MotorCommandQueue - FreeRTOS queue wrapper for motor commands
 * Provides thread-safe command passing between tasks
 */
class MotorCommandQueue {
private:
    QueueHandle_t commandQueue;
    static const size_t QUEUE_SIZE = 20;  // Maximum pending commands
    
public:
    /**
     * Constructor
     */
    MotorCommandQueue();
    
    /**
     * Destructor
     */
    ~MotorCommandQueue();
    
    /**
     * Initialize the queue
     * @return true if successful, false otherwise
     */
    bool begin();
    
    /**
     * Send a command to the queue (non-blocking)
     * @param cmd Command to send
     * @param timeoutMs Timeout in milliseconds (0 = immediate, portMAX_DELAY = wait forever)
     * @return true if sent successfully, false if timeout or error
     */
    bool sendCommand(const MotorCommand& cmd, TickType_t timeoutMs = 0);
    
    /**
     * Receive a command from the queue (blocking)
     * @param cmd Output: Received command
     * @param timeoutMs Timeout in milliseconds (portMAX_DELAY = wait forever)
     * @return true if received successfully, false if timeout
     */
    bool receiveCommand(MotorCommand& cmd, TickType_t timeoutMs = portMAX_DELAY);
    
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

#endif // MOTOR_COMMAND_QUEUE_H
