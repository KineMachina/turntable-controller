#ifndef STEPPER_MOTOR_CONTROLLER_H
#define STEPPER_MOTOR_CONTROLLER_H

#include "Arduino.h"
#include "MotorCommandQueue.h"
#include <FastAccelStepper.h>
#include <TMCStepper.h>
#include <FreeRTOS.h>
#include <task.h>

class HardwareSerial;
struct SystemConfig;

/**
 * StepperMotorController - Manages TMC2209 stepper motor
 * Handles motor initialization and position management (open-loop).
 * Owns TMC2209 UART and driver when TMC pins are provided.
 */
class StepperMotorController {
private:
    // Motor pins
    int stepPin;
    int dirPin;
    int enablePin;

    // TMC2209 (owned when tmcUartRx >= 0)
    class HardwareSerial* tmcSerial;
    TMC2209Stepper* tmcDriver;
    int tmcUartRx;
    int tmcUartTx;
    float tmcRsense;
    uint8_t tmcAddress;
    
    // Stepper motor objects
    FastAccelStepperEngine* stepperEngine;
    FastAccelStepper* stepper;

    // Motor configuration
    float maxSpeed;
    float acceleration;
    uint8_t microsteps;  // Microstepping setting (1, 2, 4, 8, 16, 32, 64, 128, 256)

    float gearRatio = 1.0f;
     
    // Motor enable state (TMCStepper shadow register for toff is unreliable)
    bool motorEnabled;

    // Auto-enable/disable
    unsigned long lastMotorActiveTime;  // millis() when motor last had work
    static const unsigned long IDLE_DISABLE_MS = 5000;  // Disable after 5s idle
    bool autoEnabled;  // Track if we auto-enabled (vs explicit user enable)

    void autoEnableIfNeeded();
    void checkIdleDisable();

    // Debug logging
    bool debugLogging;
    unsigned long lastDebugLogTime;
    static const unsigned long DEBUG_LOG_INTERVAL_MS = 100;  // Log every 100ms (10Hz)
    
    // Dance task
    TaskHandle_t danceTaskHandle;
    bool danceInProgress;
    static void danceTaskWrapper(void* parameter);
    void danceTask();
    
public:
    /**
     * Dance types for dance effects
     */
    enum class DanceType {
        TWIST,              // Chubby Checkers "Twist" - back and forth with increasing arcs
        SHAKE,              // Quick shake - small rapid back and forth
        SPIN,               // Full rotations back and forth
        WIGGLE,             // Small wiggles in place
        WATUSI,             // Watusi - side-to-side alternating movements
        PEPPERMINT_TWIST    // Peppermint Twist - rapid alternating twists
    };

    /**
     * Behavior types for precanned behaviors
     */
    enum class BehaviorType {
        SCANNING,
        SLEEPING,
        EATING,
        ALERT,
        ROARING,
        STALKING,
        PLAYING,
        RESTING,
        HUNTING,
        VICTORY
    };
    
private:
    DanceType currentDanceType;
    
    // Behavior task
    TaskHandle_t behaviorTaskHandle;
    bool behaviorInProgress;
    static void behaviorTaskWrapper(void* parameter);
    void behaviorTask();
    BehaviorType currentBehaviorType;
    
public:
    /**
     * Constructor
     * @param step Step pin number
     * @param dir Direction pin number
     * @param enable Enable pin number (-1 if not used)
     * @param maxSpeed Maximum speed in steps/second
     * @param acceleration Acceleration in steps/second^2
     * @param tmcUartRx TMC2209 UART RX pin (-1 to disable TMC)
     * @param tmcUartTx TMC2209 UART TX pin
     * @param tmcRsense TMC2209 sense resistor in ohms (e.g. 0.11f)
     * @param tmcAddress TMC2209 UART address (0 when MS1/MS2 = LOW)
     */
    StepperMotorController(int step, int dir, int enable,
                          float maxSpeed = 1000.0f,
                          float acceleration = 500.0f,
                          int tmcUartRx = -1, int tmcUartTx = -1,
                          float tmcRsense = 0.11f, uint8_t tmcAddress = 0);
    
    /**
     * Destructor
     */
    ~StepperMotorController();

    /**
     * Initialize stepper motor hardware (pins, TMC2209 if present, FastAccelStepper, config).
     * Applies motor and TMC settings from config when non-null.
     * @param config Optional system config; when null, keeps constructor defaults
     * @return true if successful, false otherwise
     */
    bool begin(const SystemConfig* config = nullptr);
    
    void update();
    
    /**
     * Get stepper position in steps
     * @return Current stepper position in steps
     */
    long getStepperPosition() const;
    
    /**
     * Get stepper position in turntable degrees
     * Converts stepper steps to stepper degrees, then divides by gear ratio to get turntable degrees
     * @return Current stepper position in turntable degrees
     */
    float getStepperPositionDegrees() const;
    
    /**
     * Set stepper position (passthrough to stepper->moveTo)
     * @param position Position to move to in steps
     */
    void moveTo(long position);
    
    /**
     * Enable/disable motor
     * @param enable true to enable, false to disable
     */
    void enable(bool enable);
    
    /**
     * Check if motor is enabled
     * @return true if enabled, false otherwise
     */
    bool isEnabled() const;
    
    /**
     * Set maximum speed
     * @param speed Maximum speed in steps/second
     */
    void setMaxSpeed(float speed);
    
    /**
     * Set acceleration
     * @param accel Acceleration in steps/second^2
     */
    void setAcceleration(float accel);
    
    /**
     * Set microstepping
     * @param microsteps Microstepping value (1, 2, 4, 8, 16, 32, 64, 128, or 256)
     * @return true if successful, false if invalid value
     */
    bool setMicrosteps(uint8_t microsteps);
    
    /**
     * Get current microstepping setting
     * @return Current microstepping value
     */
    uint8_t getMicrosteps() const { return microsteps; }
    
    /**
     * Set gear ratio (stepper rotations : turntable rotations)
     * @param ratio Gear ratio (e.g., 2.0 means stepper rotates 2x for turntable 1x)
     */
    void setGearRatio(float ratio);
    
    /**
     * Get current gear ratio
     * @return Current gear ratio
     */
    float getGearRatio() const { return gearRatio; }
   
    /**
     * Convert degrees to steps
     * @param degrees Angle in degrees
     * @return Equivalent number of steps
     */
    long degreesToSteps(float degrees) const;
    
    /**
     * Convert steps to degrees
     * @param steps Number of steps
     * @return Equivalent angle in degrees
     */
    float stepsToDegrees(long steps) const;
    
    /**
     * Move to target position in degrees (absolute)
     * @param degrees Target angle in degrees
     */
    void moveToDegrees(float degrees);
    
    /**
     * Move to target heading in degrees using shortest path (relative movement)
     * Uses stepper position to determine current heading and computes shortest angle
     * @param targetHeading Target heading in degrees (0-360)
     * @return true if successful, false if stepper not available
     */
    bool moveToHeadingDegrees(float targetHeading);
    
    /**
     * Move relative distance in degrees (uses stepper->move for relative movement)
     * @param relativeDegrees Relative movement in turntable degrees
     */
    void moveDegrees(float relativeDegrees);
     
    /**
     * Get FastAccelStepper instance (for advanced control)
     * @return Pointer to FastAccelStepper instance
     */
    FastAccelStepper* getStepper() { return stepper; }
    
    /**
     * Enable/disable debug logging
     * @param enable True to enable logging, false to disable
     */
    void setDebugLogging(bool enable);
    
    /**
     * Process commands from queue
     * Call this from a task to process pending motor commands
     * @param cmdQueue Pointer to MotorCommandQueue instance
     */
    void processCommandQueue(MotorCommandQueue* cmdQueue);
    
    /**
     * Set speed (passthrough to stepper->setSpeedInHz)
     * @param speedHz Speed in Hz (steps per second)
     */
    void setSpeedInHz(float speedHz);
    
    /**
     * Start forward rotation (passthrough to stepper->runForward)
     */
    void runForward();
    
    /**
     * Start backward rotation (passthrough to stepper->runBackward)
     */
    void runBackward();
    
    /**
     * Stop velocity mode (passthrough to stepper->forceStop)
     */
    void stopVelocity();
    
    /**
     * Stop move (passthrough to stepper->stopMove)
     */
    void stopMove();
    /**
     * Get current target speed in Hz (always returns 0 - speed tracking removed)
     * @return 0 (speed tracking removed)
     */
    float getTargetSpeedHz() const { return 0.0f; }
    
   
    /**
     * Perform homing operation - move to home position (step 0)
     * @param timeoutMs Maximum time to wait for homing to complete (0 = no timeout)
     * @return true if homing successful, false if stepper not available
     */
    bool home(uint32_t timeoutMs = 30000);
    
    /**
     * Reset the FastAccelStepper engine (calls engine.init())
     * This reinitializes the stepper engine and reconnects the stepper instance
     * @return true if reset successful, false otherwise
     */
    bool resetEngine();
 
    /**
     * Check if the motor is currently running (moving)
     * @return true if motor is currently moving, false if stopped
     */
    bool isRunning() const;
    
    /**
     * Start a dance sequence in a separate task (non-blocking)
     * @param danceType Type of dance to perform
     * @return true if dance task started successfully
     */
    bool startDance(DanceType danceType);
    
    /**
     * Stop the currently running dance sequence
     * @return true if dance was stopped, false if no dance was running
     */
    bool stopDance();
    
    /**
     * Check if a dance is currently in progress
     * @return true if dance is running, false otherwise
     */
    bool isDanceInProgress() const { return danceInProgress; }
    
    /**
     * Start a behavior sequence in a separate task (non-blocking)
     * @param behaviorType Type of behavior to perform
     * @return true if behavior task started successfully
     */
    bool startBehavior(BehaviorType behaviorType);
    
    /**
     * Stop the currently running behavior sequence
     * @return true if behavior was stopped, false if no behavior was running
     */
    bool stopBehavior();
    
    /**
     * Check if a behavior is currently in progress
     * @return true if behavior is running, false otherwise
     */
    bool isBehaviorInProgress() const { return behaviorInProgress; }
    
    /**
     * Perform a behavior sequence (internal, called from behavior task)
     * @param behaviorType Type of behavior to perform
     * @return true if behavior completed successfully
     */
    bool performBehavior(BehaviorType behaviorType);
    
    /**
     * Perform a dance sequence (internal, called from dance task)
     * @param danceType Type of dance to perform
     * @return true if dance completed successfully
     */
    bool performDance(DanceType danceType);
    
    /**
     * Get TMC2209 RMS current setting
     * @return RMS current in milliamps, or 0 if driver not available
     */
    uint16_t getTmcRmsCurrent() const;
    
    /**
     * Get TMC2209 actual current scale (CS_ACTUAL)
     * @return CS_ACTUAL value (0-31), or 0 if driver not available
     */
    uint8_t getTmcCsActual() const;
    
    /**
     * Get TMC2209 actual current in milliamps
     * @return Actual current in milliamps, or 0 if driver not available
     */
    float getTmcActualCurrent() const;
    
    /**
     * Get TMC2209 running current setting (irun)
     * @return irun value (0-31), or 0 if driver not available
     */
    uint8_t getTmcIrun() const;
    
    /**
     * Get TMC2209 holding current setting (ihold)
     * @return ihold value (0-31), or 0 if driver not available
     */
    uint8_t getTmcIhold() const;
    
    /**
     * Get TMC2209 driver enable status (toff)
     * @return true if enabled (toff > 0), false if disabled or driver not available
     */
    bool getTmcEnabled() const;
    
    /**
     * Get TMC2209 spreadCycle mode status
     * @return true if spreadCycle enabled, false if stealthChop or driver not available
     */
    bool getTmcSpreadCycle() const;
    
    /**
     * Get TMC2209 PWM autoscale status
     * @return true if PWM autoscale enabled, false if disabled or driver not available
     */
    bool getTmcPwmAutoscale() const;
    
    /**
     * Get TMC2209 blank time setting
     * @return Blank time value, or 0 if driver not available
     */
    uint8_t getTmcBlankTime() const;

    /**
     * Set TMC2209 RMS current (runtime). No effect if no TMC driver.
     * @param ma RMS current in milliamps
     */
    void setTmcRmsCurrent(uint16_t ma);

    /**
     * Set TMC2209 run current scale (0-31). No effect if no TMC driver.
     */
    void setTmcIrun(uint8_t irun);

    /**
     * Set TMC2209 hold current scale (0-31). No effect if no TMC driver.
     */
    void setTmcIhold(uint8_t ihold);

    /**
     * Set TMC2209 spreadCycle mode (true = SpreadCycle, false = StealthChop). No effect if no TMC driver.
     */
    void setTmcSpreadCycle(bool enabled);

    /**
     * Set TMC2209 PWM autoscale. No effect if no TMC driver.
     */
    void setTmcPwmAutoscale(bool enabled);

    /**
     * Set TMC2209 blank time (0-15). No effect if no TMC driver.
     */
    void setTmcBlankTime(uint8_t blankTime);
};

#endif // STEPPER_MOTOR_CONTROLLER_H
