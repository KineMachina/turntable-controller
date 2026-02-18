#ifndef STEPPER_MOTOR_CONTROLLER_H
#define STEPPER_MOTOR_CONTROLLER_H

#include "Arduino.h"
#include <vector>
#include <string>
#include <cstdint>

// Forward declares to match production header
class FastAccelStepper;
struct SystemConfig;

class StepperMotorController {
public:
    enum class DanceType {
        TWIST, SHAKE, SPIN, WIGGLE, WATUSI, PEPPERMINT_TWIST
    };

    enum class BehaviorType {
        SCANNING, SLEEPING, EATING, ALERT, ROARING,
        STALKING, PLAYING, RESTING, HUNTING, VICTORY
    };

    // Call record for test assertions
    struct Call {
        std::string method;
        float floatArg = 0;
        int intArg = 0;
        bool boolArg = false;
    };

    StepperMotorController(int = 0, int = 0, int = 0,
                          float = 1000.0f, float = 500.0f,
                          int = -1, int = -1,
                          float = 0.11f, uint8_t = 0) {}
    ~StepperMotorController() {}

    bool begin(const SystemConfig* = nullptr) { return true; }
    void update() {}

    // Getters - configurable return values
    long getStepperPosition() const { return _position; }
    float getStepperPositionDegrees() const { return _positionDegrees; }
    bool isEnabled() const { return _enabled; }
    bool isRunning() const { return _running; }
    float getTargetSpeedHz() const { return _speedHz; }
    uint8_t getMicrosteps() const { return _microsteps; }
    float getGearRatio() const { return _gearRatio; }
    bool isDanceInProgress() const { return _danceInProgress; }
    bool isBehaviorInProgress() const { return _behaviorInProgress; }

    // TMC getters
    uint16_t getTmcRmsCurrent() const { return 800; }
    uint8_t getTmcCsActual() const { return 16; }
    float getTmcActualCurrent() const { return 400.0f; }
    uint8_t getTmcIrun() const { return 16; }
    uint8_t getTmcIhold() const { return 8; }
    bool getTmcEnabled() const { return true; }
    bool getTmcSpreadCycle() const { return false; }
    bool getTmcPwmAutoscale() const { return true; }
    uint8_t getTmcBlankTime() const { return 2; }

    // Actions - record calls
    void moveTo(long pos) { _calls.push_back({"moveTo", (float)pos}); }
    void moveToDegrees(float deg) { _calls.push_back({"moveToDegrees", deg}); }
    bool moveToHeadingDegrees(float heading) {
        _calls.push_back({"moveToHeadingDegrees", heading});
        return _moveToHeadingResult;
    }
    void moveDegrees(float deg) { _calls.push_back({"moveDegrees", deg}); }
    void enable(bool en) { _calls.push_back({"enable", 0, 0, en}); }
    void setMaxSpeed(float spd) { _calls.push_back({"setMaxSpeed", spd}); }
    void setAcceleration(float acc) { _calls.push_back({"setAcceleration", acc}); }
    bool setMicrosteps(uint8_t ms) {
        _calls.push_back({"setMicrosteps", 0, ms});
        // Valid values: 1, 2, 4, 8, 16, 32, 64, 128, 256
        return (ms == 1 || ms == 2 || ms == 4 || ms == 8 || ms == 16 ||
                ms == 32 || ms == 64 || ms == 128 || ms == 0 /* 256 wraps to 0 in uint8_t */);
    }
    void setGearRatio(float r) { _calls.push_back({"setGearRatio", r}); }
    void setSpeedInHz(float hz) { _calls.push_back({"setSpeedInHz", hz}); }
    void runForward() { _calls.push_back({"runForward"}); }
    void runBackward() { _calls.push_back({"runBackward"}); }
    void stopMove() { _calls.push_back({"stopMove"}); }
    void stopVelocity() { _calls.push_back({"stopVelocity"}); }
    bool resetEngine() { _calls.push_back({"resetEngine"}); return _resetResult; }
    bool home(uint32_t = 30000) { _calls.push_back({"home"}); return true; }
    long degreesToSteps(float deg) const { return (long)(deg * 10); }
    float stepsToDegrees(long steps) const { return steps / 10.0f; }
    void setDebugLogging(bool) {}
    void processCommandQueue(void*) {}
    FastAccelStepper* getStepper() { return nullptr; }

    bool startDance(DanceType dt) {
        _calls.push_back({"startDance", (float)(int)dt});
        return _startDanceResult;
    }
    bool stopDance() {
        _calls.push_back({"stopDance"});
        return _stopDanceResult;
    }
    bool startBehavior(BehaviorType bt) {
        _calls.push_back({"startBehavior", (float)(int)bt});
        return _startBehaviorResult;
    }
    bool stopBehavior() {
        _calls.push_back({"stopBehavior"});
        return _stopBehaviorResult;
    }
    bool performDance(DanceType) { return true; }
    bool performBehavior(BehaviorType) { return true; }

    // TMC setters (no-ops)
    void setTmcRmsCurrent(uint16_t) {}
    void setTmcIrun(uint8_t) {}
    void setTmcIhold(uint8_t) {}
    void setTmcSpreadCycle(bool) {}
    void setTmcPwmAutoscale(bool) {}
    void setTmcBlankTime(uint8_t) {}

    // Test helpers
    void clearCalls() {
        _calls.clear();
        _moveToHeadingResult = true;
        _resetResult = true;
        _startDanceResult = true;
        _stopDanceResult = true;
        _startBehaviorResult = true;
        _stopBehaviorResult = true;
    }
    const std::vector<Call>& getCalls() const { return _calls; }
    bool hasCall(const std::string& method) const {
        for (auto& c : _calls) if (c.method == method) return true;
        return false;
    }

    // Configurable return values
    bool _moveToHeadingResult = true;
    bool _resetResult = true;
    bool _startDanceResult = true;
    bool _stopDanceResult = true;
    bool _startBehaviorResult = true;
    bool _stopBehaviorResult = true;

    // Configurable state
    long _position = 0;
    float _positionDegrees = 0.0f;
    bool _enabled = true;
    bool _running = false;
    float _speedHz = 0.0f;
    uint8_t _microsteps = 16;
    float _gearRatio = 1.0f;
    bool _danceInProgress = false;
    bool _behaviorInProgress = false;

private:
    std::vector<Call> _calls;
};

#endif // STEPPER_MOTOR_CONTROLLER_H
