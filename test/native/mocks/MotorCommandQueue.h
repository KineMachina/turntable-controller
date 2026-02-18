#ifndef MOTOR_COMMAND_QUEUE_H
#define MOTOR_COMMAND_QUEUE_H

#include "Arduino.h"
#include "FreeRTOS.h"
#include "queue.h"
#include <vector>

enum class MotorCommandType {
    MOVE_TO,
    ZERO_POSITION,
    ENABLE,
    SET_SPEED,
    SET_ACCELERATION,
    SET_MICROSTEPS,
    HOME,
    GET_STATUS
};

struct MotorCommand {
    MotorCommandType type;
    union {
        struct { float value; } position;
        struct { long value; } positionSteps;
        struct { bool enable; } enable;
        struct { float speed; } speed;
        struct { float accel; } acceleration;
        struct { uint8_t microsteps; } microsteps;
    } data;
    void (*statusCallback)(void* context);
    void* statusContext;
};

class MotorCommandQueue {
public:
    MotorCommandQueue() : _failSend(false) {}
    ~MotorCommandQueue() {}

    bool begin() { return true; }

    bool sendCommand(const MotorCommand& cmd, TickType_t = 0) {
        if (_failSend) return false;
        _commands.push_back(cmd);
        return true;
    }

    bool receiveCommand(MotorCommand&, TickType_t = portMAX_DELAY) { return false; }
    bool isEmpty() const { return _commands.empty(); }
    UBaseType_t getCount() const { return (UBaseType_t)_commands.size(); }
    void clear() { _commands.clear(); }

    // Test helpers
    void clearCommands() { _commands.clear(); _failSend = false; }
    const std::vector<MotorCommand>& getCommands() const { return _commands; }
    void setFailSend(bool fail) { _failSend = fail; }

private:
    std::vector<MotorCommand> _commands;
    bool _failSend;
};

#endif // MOTOR_COMMAND_QUEUE_H
