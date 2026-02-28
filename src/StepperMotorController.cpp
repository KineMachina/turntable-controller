#include "StepperMotorController.h"
#include "ConfigurationManager.h"
#include <HardwareSerial.h>
#include "RuntimeLog.h"

static const char* TAG = "Stepper";

// TMC2209 defaults when no config is provided
static const uint8_t TMC_DEFAULT_TOFF = 5;
static const uint8_t TMC_DEFAULT_BLANK_TIME = 24;
static const uint16_t TMC_DEFAULT_RMS_CURRENT_MA = 1200;
static const bool TMC_DEFAULT_PWM_AUTOSCALE = true;
static const bool TMC_DEFAULT_SPREAD_CYCLE = false;
static const uint8_t TMC_DEFAULT_IRUN = 31;
static const uint8_t TMC_DEFAULT_IHOLD = 8;

// Static FastAccelStepperEngine instance (singleton)
static FastAccelStepperEngine stepperEngineInstance;

StepperMotorController::StepperMotorController(int step, int dir, int enable,
                                               float maxSpeed, float acceleration,
                                               int tmcUartRx, int tmcUartTx,
                                               float tmcRsense, uint8_t tmcAddress)
    : stepPin(step), dirPin(dir), enablePin(enable),
      tmcSerial(nullptr), tmcDriver(nullptr),
      tmcUartRx(tmcUartRx), tmcUartTx(tmcUartTx), tmcRsense(tmcRsense), tmcAddress(tmcAddress),
      maxSpeed(maxSpeed), acceleration(acceleration),
      microsteps(8),
      motorEnabled(false), lastMotorActiveTime(0), autoEnabled(false),
      debugLogging(false), lastDebugLogTime(0),
      stepperEngine(nullptr), stepper(nullptr),
      danceTaskHandle(nullptr), danceInProgress(false),
      behaviorTaskHandle(nullptr), behaviorInProgress(false),
      currentBehaviorType(BehaviorType::SCANNING) {
    if (tmcUartRx >= 0) {
        tmcSerial = new HardwareSerial(1);
        tmcDriver = new TMC2209Stepper(tmcSerial, tmcRsense, tmcAddress);
    }
}

StepperMotorController::~StepperMotorController() {
    delete tmcDriver;
    tmcDriver = nullptr;
    delete tmcSerial;
    tmcSerial = nullptr;
}

bool StepperMotorController::begin(const SystemConfig* config) {
    ESP_LOGI(TAG, "Initializing Stepper Motor Controller...");

    if (config != nullptr) {
        maxSpeed = config->motorMaxSpeed;
        acceleration = config->motorAcceleration;
        microsteps = config->motorMicrosteps;
        gearRatio = config->motorGearRatio;
    }

    // Configure pins (step, dir, enable)
    pinMode(stepPin, OUTPUT);
    pinMode(dirPin, OUTPUT);
    if (enablePin >= 0) {
        pinMode(enablePin, OUTPUT);
        digitalWrite(enablePin, HIGH);  // Start disabled (HIGH = disabled)
    }

    // Initialize TMC2209 if present (UART, driver, defaults/config)
    if (tmcDriver != nullptr && tmcSerial != nullptr) {
        ESP_LOGI(TAG, "Initializing TMC2209 via UART...");
        tmcSerial->begin(115200, SERIAL_8N1, tmcUartRx, tmcUartTx);
        delay(100);

        tmcDriver->begin();
        tmcDriver->pdn_disable(true);   // Enable UART control (disable PDN_UART analog function)
        tmcDriver->mstep_reg_select(true); // Use UART for microstep setting (not MS1/MS2 pins)
        tmcDriver->I_scale_analog(false);
        if (config != nullptr) {
            tmcDriver->blank_time(config->tmcBlankTime);
            tmcDriver->rms_current(config->tmcRmsCurrent);
            tmcDriver->pwm_autoscale(config->tmcPwmAutoscale);
            tmcDriver->pwm_autograd(true);
            tmcDriver->en_spreadCycle(config->tmcSpreadCycle);
            tmcDriver->irun(config->tmcIrun);
            tmcDriver->ihold(config->tmcIhold);
        } else {
            tmcDriver->blank_time(TMC_DEFAULT_BLANK_TIME);
            tmcDriver->rms_current(TMC_DEFAULT_RMS_CURRENT_MA);
            tmcDriver->pwm_autoscale(TMC_DEFAULT_PWM_AUTOSCALE);
            tmcDriver->pwm_autograd(true);
            tmcDriver->en_spreadCycle(TMC_DEFAULT_SPREAD_CYCLE);
            tmcDriver->irun(TMC_DEFAULT_IRUN);
            tmcDriver->ihold(TMC_DEFAULT_IHOLD);
        }
        // toff MUST be set last — rms_current() forces toff=5 if it's 0
        tmcDriver->toff(0);  // Start disabled; auto-enabled on first move
        tmcDriver->push();

        // Flush echo bytes then verify UART readback
        while (tmcSerial->available()) tmcSerial->read();

        // IOIN register (0x06) is the most reliable for testing UART readback
        uint32_t ioin = tmcDriver->IOIN();
        uint8_t version = (ioin >> 24) & 0xFF;
        ESP_LOGI(TAG, "TMC2209 IOIN: 0x%08X (version: 0x%02X, %s)",
                 ioin, version, version == 0x21 ? "OK" : "READBACK FAILED");

        float actualCurrent = (tmcDriver->cs_actual() / 32.0f) * tmcDriver->rms_current();
        ESP_LOGI(TAG, "TMC2209 initialized. Current set to: %u mA | CS_ACTUAL: %u (%.0f mA) | toff: %u",
                 tmcDriver->rms_current(), tmcDriver->cs_actual(), actualCurrent, tmcDriver->toff());
    }

    // Initialize FastAccelStepperEngine (static instance)
    stepperEngine = &stepperEngineInstance;
    stepperEngine->init();

    stepper = stepperEngine->stepperConnectToPin(stepPin);
    if (stepper == nullptr) {
        ESP_LOGE(TAG, "Failed to create FastAccelStepper instance");
        return false;
    }

    stepper->setDirectionPin(dirPin);
    stepper->setSpeedInHz(maxSpeed);
    stepper->setAcceleration(acceleration);
    stepper->setCurrentPosition(0);

    if (tmcDriver != nullptr) {
        tmcDriver->microsteps(microsteps);
        uint16_t actualMicrosteps = tmcDriver->microsteps();
        if (actualMicrosteps != microsteps) {
            ESP_LOGW(TAG, "Microstepping set to: %u (read back: %u WARNING: mismatch, %.2f deg per microstep)",
                     microsteps, actualMicrosteps, 360.0f / (200.0f * microsteps));
        } else {
            ESP_LOGI(TAG, "Microstepping set to: %u (read back: %u, %.2f deg per microstep)",
                     microsteps, actualMicrosteps, 360.0f / (200.0f * microsteps));
        }
    }

    ESP_LOGI(TAG, "Stepper motor initialized. Max Speed: %.0f steps/sec, Acceleration: %.0f steps/sec^2",
             maxSpeed, acceleration);
    ESP_LOGI(TAG, "Motor starts disabled — will auto-enable on first move, auto-disable after %lus idle",
             IDLE_DISABLE_MS / 1000);

    return true;
}

void StepperMotorController::autoEnableIfNeeded() {
    if (!isEnabled()) {
        enable(true);
        autoEnabled = true;
        ESP_LOGI(TAG, "Auto-enabled motor for move");
    }
    lastMotorActiveTime = millis();
}

void StepperMotorController::checkIdleDisable() {
    if (!autoEnabled || !isEnabled()) return;
    if (danceInProgress || behaviorInProgress) {
        lastMotorActiveTime = millis();
        return;
    }
    if (stepper != nullptr && stepper->isRunning()) {
        lastMotorActiveTime = millis();
        return;
    }
    if (millis() - lastMotorActiveTime >= IDLE_DISABLE_MS) {
        enable(false);
        autoEnabled = false;
        ESP_LOGI(TAG, "Auto-disabled motor after %lus idle", IDLE_DISABLE_MS / 1000);
    }
}

void StepperMotorController::update() {
    checkIdleDisable();
}

long StepperMotorController::getStepperPosition() const {
    if (stepper != nullptr) {
        return stepper->getPositionAfterCommandsCompleted();
    }
    return 0;
}

float StepperMotorController::getStepperPositionDegrees() const {
    long stepperSteps = getStepperPosition();
    // Convert steps to stepper degrees
    float stepperDegrees = stepsToDegrees((float)stepperSteps);
    // Convert stepper degrees to turntable degrees (divide by gear ratio)
    return stepperDegrees / gearRatio;
}

void StepperMotorController::moveTo(long position) {
    if (stepper != nullptr) {
        ESP_LOGD(TAG, "calling moveTo(%ld)", position);
        stepper->moveTo(position);
    }
}

void StepperMotorController::enable(bool en) {
    if (tmcDriver != nullptr) {
        tmcDriver->toff(en ? 5 : 0);
    }

    if (enablePin >= 0) {
        digitalWrite(enablePin, en ? LOW : HIGH);
    }

    motorEnabled = en;
    ESP_LOGI(TAG, "Motor %s", en ? "enabled" : "disabled");
}

bool StepperMotorController::isEnabled() const {
    return motorEnabled;
}

void StepperMotorController::setMaxSpeed(float speed) {
    maxSpeed = speed;
    if (stepper != nullptr) {
        stepper->setSpeedInHz(speed);  // FastAccelStepper uses setSpeedInHz
    }
}

void StepperMotorController::setAcceleration(float accel) {
    acceleration = accel;
    if (stepper != nullptr) {
        stepper->setAcceleration(accel);
    }
}

bool StepperMotorController::setMicrosteps(uint8_t microsteps) {
    // Valid microstepping values for TMC2209: 1, 2, 4, 8, 16, 32, 64, 128, 256
    // Check if value is a power of 2 and within valid range
    if (microsteps == 0 || microsteps > 256) {
        return false;
    }
    
    // Check if it's a power of 2
    if ((microsteps & (microsteps - 1)) != 0) {
        return false;  // Not a power of 2
    }
    
    uint8_t oldMicrosteps = this->microsteps;
    this->microsteps = microsteps;
    
    // Update TMC2209 driver if available
    if (tmcDriver != nullptr) {
        tmcDriver->microsteps(microsteps);
        // Read back to verify
        uint16_t actualMicrosteps = tmcDriver->microsteps();
        if (actualMicrosteps != microsteps) {
            ESP_LOGW(TAG, "Microstepping mismatch! Set %u but TMC2209 reports %u", microsteps, actualMicrosteps);
        }
    }
    
    return true;
}

void StepperMotorController::setGearRatio(float ratio) {
    if (ratio > 0.0f && ratio <= 100.0f) {
        gearRatio = ratio;
    }
}


void StepperMotorController::setDebugLogging(bool enable) {
    debugLogging = enable;
}

long StepperMotorController::degreesToSteps(float degrees) const {
    // Standard stepper: 200 steps per revolution (1.8° per step)
    // With microstepping: steps_per_revolution = 200 * microsteps
    // Note: degrees parameter is in stepper degrees
    const float STEPS_PER_STEPPER_REVOLUTION = 200.0f * microsteps;
    // Convert stepper degrees to steps
    return (long) ((degrees / 360.0f) * STEPS_PER_STEPPER_REVOLUTION);
}

float StepperMotorController::stepsToDegrees(long steps) const {
    // Standard stepper: 200 steps per revolution (1.8° per step)
    // With microstepping: steps_per_revolution = 200 * microsteps
    // Note: returns stepper degrees
    const float STEPS_PER_STEPPER_REVOLUTION = 200.0f * microsteps;
    // Convert steps to stepper degrees
    return (steps / STEPS_PER_STEPPER_REVOLUTION) * 360.0f;
}

void StepperMotorController::moveToDegrees(float degrees) {
    autoEnableIfNeeded();
    // Input degrees are in turntable degrees
    // Convert turntable degrees to stepper degrees (multiply by gear ratio)
    float stepperDegrees = degrees * gearRatio;
    // Convert stepper degrees to steps
    long steps = degreesToSteps(stepperDegrees);
    
    ESP_LOGD(TAG, "moveToDegrees: turntableDegrees=%.2f, gearRatio=%.2f, stepperDegrees=%.2f, microsteps=%u, steps=%ld",
             degrees, gearRatio, stepperDegrees, microsteps, steps);
     
    // Move to target position
    moveTo(steps);
}

bool StepperMotorController::moveToHeadingDegrees(float targetHeading) {
    if (stepper == nullptr) {
        return false;
    }

    autoEnableIfNeeded();

    // Normalize target heading to 0-360 range
    while (targetHeading < 0.0f) {
        targetHeading += 360.0f;
    }
    while (targetHeading >= 360.0f) {
        targetHeading -= 360.0f;
    }

    // Get current target position in steps and convert to heading
    // Uses getPositionAfterCommandsCompleted() so the reference is stable
    long currentSteps = getStepperPosition();
    float currentHeading = getStepperPositionDegrees();

    // Normalize current heading to 0-360 range
    while (currentHeading < 0.0f) {
        currentHeading += 360.0f;
    }
    while (currentHeading >= 360.0f) {
        currentHeading -= 360.0f;
    }

    // Calculate shortest path (considering wrap-around)
    float delta = targetHeading - currentHeading;
    if (delta > 180.0f) {
        delta -= 360.0f;
    } else if (delta < -180.0f) {
        delta += 360.0f;
    }

    ESP_LOGD(TAG, "moveToHeadingDegrees: currentHeading=%.2fdeg, targetHeading=%.2fdeg, delta=%.2fdeg",
             currentHeading, targetHeading, delta);

    // Convert relative degrees to stepper degrees (multiply by gear ratio)
    float stepperDegrees = delta * gearRatio;
    // Convert stepper degrees to steps
    long deltaSteps = degreesToSteps(stepperDegrees);

    // Compute absolute target position in steps
    long targetSteps = currentSteps + deltaSteps;

    ESP_LOGD(TAG, "moveToHeadingDegrees: stepperDegrees=%.2f, deltaSteps=%ld, targetSteps=%ld",
             stepperDegrees, deltaSteps, targetSteps);

    // Use absolute moveTo so the target is correct even if called mid-move.
    // move() would add steps relative to the current actual position, but our
    // delta was computed from getPositionAfterCommandsCompleted(), causing the
    // motor to overshoot or undershoot when interrupted.
    stepper->moveTo(targetSteps);
    return true;
}

void StepperMotorController::moveDegrees(float relativeDegrees) {
    if (stepper == nullptr) {
        return;
    }
    
    // Convert relative turntable degrees to stepper degrees (multiply by gear ratio)
    float stepperDegrees = relativeDegrees * gearRatio;
    // Convert stepper degrees to steps
    long steps = degreesToSteps(stepperDegrees);
    
    ESP_LOGD(TAG, "moveDegrees: relativeDegrees=%.2f, stepperDegrees=%.2f, steps=%ld",
             relativeDegrees, stepperDegrees, steps);
    
    // Use relative movement (move) instead of absolute (moveTo)
    stepper->move(steps);
}

void StepperMotorController::processCommandQueue(MotorCommandQueue* cmdQueue) {
    if (cmdQueue == nullptr) {
        return;
    }
    
    MotorCommand cmd;
    while (cmdQueue->receiveCommand(cmd, 0)) {  // Non-blocking, process all pending commands
        // Log command received and processed
        const char* cmdName = "UNKNOWN";
        bool shouldLog = true;
        
        switch (cmd.type) {
            case MotorCommandType::MOVE_TO: {
                cmdName = "MOVE_TO";
                // autoEnableIfNeeded() called inside moveToDegrees()
                moveToDegrees(cmd.data.position.value);
                ESP_LOGI(TAG, "Command: MOVE_TO -> %.2f deg", cmd.data.position.value);
                break;
            }
           
            case MotorCommandType::ENABLE:
                cmdName = "ENABLE";
                enable(cmd.data.enable.enable);
                autoEnabled = false;  // Explicit enable/disable overrides auto behavior
                ESP_LOGI(TAG, "Command: ENABLE -> %s", cmd.data.enable.enable ? "ON" : "OFF");
                break;
                
            case MotorCommandType::SET_SPEED:
                cmdName = "SET_SPEED";
                setMaxSpeed(cmd.data.speed.speed);
                ESP_LOGI(TAG, "Command: SET_SPEED -> %.1f steps/sec", cmd.data.speed.speed);
                break;
                
            case MotorCommandType::SET_ACCELERATION:
                cmdName = "SET_ACCELERATION";
                setAcceleration(cmd.data.acceleration.accel);
                ESP_LOGI(TAG, "Command: SET_ACCELERATION -> %.1f steps/sec^2", cmd.data.acceleration.accel);
                break;
                
            case MotorCommandType::SET_MICROSTEPS:
                cmdName = "SET_MICROSTEPS";
                setMicrosteps(cmd.data.microsteps.microsteps);
                ESP_LOGI(TAG, "Command: SET_MICROSTEPS -> %u", cmd.data.microsteps.microsteps);
                break;
        
            case MotorCommandType::HOME:
                cmdName = "HOME";
                autoEnableIfNeeded();
                home(0);
                ESP_LOGI(TAG, "Command: HOME");
                break;
                
            case MotorCommandType::GET_STATUS:
                cmdName = "GET_STATUS";
                // Status is handled via direct method calls, not through queue
                // This command type can be used for triggering status callbacks if needed
                if (cmd.statusCallback != nullptr) {
                    cmd.statusCallback(cmd.statusContext);
                }
                shouldLog = false;  // Don't log status requests
                break;
                
            default:
                ESP_LOGW(TAG, "Command: UNKNOWN (type=%d)", (int)cmd.type);
                break;
        }
    }
}

void StepperMotorController::setSpeedInHz(float speedHz) {
    if (stepper != nullptr) {
        stepper->setSpeedInHz(speedHz);
    }
}

void StepperMotorController::runForward() {
    if (stepper != nullptr) {
        autoEnableIfNeeded();
        stepper->runForward();
    }
}

void StepperMotorController::runBackward() {
    if (stepper != nullptr) {
        autoEnableIfNeeded();
        stepper->runBackward();
    }
}

void StepperMotorController::stopVelocity() {
    if (stepper != nullptr) {
        stepper->forceStop();
    }
}

void StepperMotorController::stopMove() {
    if (stepper != nullptr) {
        stepper->stopMove();
    }
}

bool StepperMotorController::resetEngine() {
    if (stepperEngine == nullptr) {
        ESP_LOGE(TAG, "Cannot reset engine - engine not initialized");
        return false;
    }
    
    ESP_LOGI(TAG, "Resetting FastAccelStepper engine...");
    
    // Stop any current movement
    stepper->forceStop();
    
    // Save current position before resetting
    long savedPosition = 0;
    if (stepper != nullptr) {
        savedPosition = stepper->getPositionAfterCommandsCompleted();
        stepper->forceStop();
    }
    
    // Reinitialize the engine
    stepperEngine->init();
    
    // Reconnect the stepper instance
    stepper = stepperEngine->stepperConnectToPin(stepPin);
    if (stepper == nullptr) {
        ESP_LOGE(TAG, "Failed to reconnect stepper after engine reset");
        return false;
    }
    
    // Reconfigure the stepper
    stepper->setDirectionPin(dirPin);
    stepper->setSpeedInHz(maxSpeed);
    stepper->setAcceleration(acceleration);
    stepper->setCurrentPosition(savedPosition);
    
    ESP_LOGI(TAG, "Engine reset complete");
    return true;
}

bool StepperMotorController::home(uint32_t timeoutMs) { 
    if (stepper == nullptr) {
        return false;
    }
    stepper->moveTo(0);
    return true;
}

bool StepperMotorController::isRunning() const {
    if (stepper == nullptr) {
        return false;
    }
    return stepper->isRunning();
}

bool StepperMotorController::startDance(DanceType danceType) {
    if (stepper == nullptr) {
        return false;
    }
    
    // Check if a dance is already in progress
    if (danceInProgress && danceTaskHandle != nullptr) {
        ESP_LOGW(TAG, "Dance already in progress, ignoring new request");
        return false;
    }
    
    autoEnableIfNeeded();

    // Store dance type and mark as in progress
    currentDanceType = danceType;
    danceInProgress = true;
    
    // Create task to run dance (non-blocking)
    xTaskCreatePinnedToCore(
        danceTaskWrapper,           // Task function
        "DanceTask",                // Task name
        4096,                       // Stack size
        this,                       // Parameter (this pointer)
        1,                          // Priority (low, below motor control)
        &danceTaskHandle,           // Task handle
        1                           // Core 1 (same as motor control)
    );
    
    ESP_LOGI(TAG, "Dance task started");
    return true;
}

void StepperMotorController::danceTaskWrapper(void* parameter) {
    StepperMotorController* controller = static_cast<StepperMotorController*>(parameter);
    controller->danceTask();
}

void StepperMotorController::danceTask() {
    ESP_LOGI(TAG, "Dance task running");
    bool success = performDance(currentDanceType);

    // Check if dance was stopped (danceInProgress will be false if stopDance() was called)
    bool wasStopped = !danceInProgress;

    // Mark dance as complete
    danceInProgress = false;
    danceTaskHandle = nullptr;

    if (wasStopped) {
        ESP_LOGI(TAG, "Dance task complete: Stopped by user");
    } else {
        ESP_LOGI(TAG, "Dance task complete: %s", success ? "Success" : "Failed");
    }
    
    // Delete this task
    vTaskDelete(nullptr);
}

bool StepperMotorController::stopDance() {
    if (!danceInProgress) {
        ESP_LOGW(TAG, "No dance in progress to stop");
        return false;
    }
    
    ESP_LOGI(TAG, "Stopping dance...");
    
    // Stop motor movement immediately
    if (stepper != nullptr) {
        stepper->forceStop();
    }
    
    // Mark dance as stopped (dance task will check this flag and exit)
    danceInProgress = false;
    
    // The dance task will check danceInProgress and exit gracefully
    // It will set danceTaskHandle to nullptr when it exits
    
    return true;
}

bool StepperMotorController::startBehavior(BehaviorType behaviorType) {
    if (stepper == nullptr) {
        return false;
    }
    
    if (behaviorInProgress && behaviorTaskHandle != nullptr) {
        ESP_LOGW(TAG, "Behavior already in progress, ignoring new request");
        return false;
    }
    
    autoEnableIfNeeded();

    currentBehaviorType = behaviorType;
    behaviorInProgress = true;

    xTaskCreatePinnedToCore(
        behaviorTaskWrapper,
        "BehaviorTask",
        4096,
        this,
        1,
        &behaviorTaskHandle,
        1  // Core 1
    );
    
    const char* behaviorNames[] = {
        "Scanning", "Sleeping", "Eating", "Alert", "Roaring",
        "Stalking", "Playing", "Resting", "Hunting", "Victory"
    };
    ESP_LOGI(TAG, "Behavior task started: %s", behaviorNames[static_cast<int>(behaviorType)]);
    return true;
}

bool StepperMotorController::stopBehavior() {
    if (!behaviorInProgress) {
        ESP_LOGW(TAG, "No behavior in progress to stop");
        return false;
    }
    
    ESP_LOGI(TAG, "Stopping behavior...");
    
    if (stepper != nullptr) {
        stepper->forceStop();
    }
    
    behaviorInProgress = false;
    return true;
}

void StepperMotorController::behaviorTaskWrapper(void* parameter) {
    StepperMotorController* controller = static_cast<StepperMotorController*>(parameter);
    controller->behaviorTask();
}

void StepperMotorController::behaviorTask() {
    const char* behaviorNames[] = {
        "Scanning", "Sleeping", "Eating", "Alert", "Roaring",
        "Stalking", "Playing", "Resting", "Hunting", "Victory"
    };
    ESP_LOGI(TAG, "Behavior task running: %s", behaviorNames[static_cast<int>(currentBehaviorType)]);
    bool success = performBehavior(currentBehaviorType);

    bool wasStopped = !behaviorInProgress;
    behaviorInProgress = false;
    behaviorTaskHandle = nullptr;

    if (wasStopped) {
        ESP_LOGI(TAG, "Behavior task complete: %s - Stopped by user", behaviorNames[static_cast<int>(currentBehaviorType)]);
    } else {
        ESP_LOGI(TAG, "Behavior task complete: %s - %s", behaviorNames[static_cast<int>(currentBehaviorType)], success ? "Success" : "Failed");
    }
    
    vTaskDelete(nullptr);
}

bool StepperMotorController::performDance(DanceType danceType) {
    if (stepper == nullptr) {
        return false;
    }
    
    switch (danceType) {
        case DanceType::TWIST: {
            ESP_LOGD(TAG, "Performing dance: Twist");
            // Chubby Checkers "Twist" - back and forth with increasing then decreasing arcs
            // Pattern: +45°, -135°, +225°, -315°, +135°, -225°, +135°, -45° (relative movements)
            float relativeMoves[] = {45.0f, -135.0f, 225.0f, -315.0f, 135.0f, -225.0f, 135.0f, -45.0f};
            int numMoves = sizeof(relativeMoves) / sizeof(relativeMoves[0]);
            
            for (int i = 0; i < numMoves; i++) {
                // Check if dance was stopped
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                moveDegrees(relativeMoves[i]);
                // Wait for movement to complete (with timeout - increased for slower speeds)
                // Use longer delays to feed watchdog more frequently
                unsigned long startTime = millis();
                while (stepper->isRunning() && danceInProgress && (millis() - startTime < 30000)) { // 30 seconds per move
                    vTaskDelay(pdMS_TO_TICKS(100)); // Longer delay to feed watchdog
                }
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                vTaskDelay(pdMS_TO_TICKS(200)); // Brief pause between moves
            }
            break;
        }
        
        case DanceType::SHAKE: {
            ESP_LOGD(TAG, "Performing dance: Shake");
            // Quick shake - rapid small back and forth movements (relative)
            for (int i = 0; i < 8; i++) {
                // Check if dance was stopped
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                float relativeAngle = (i % 2 == 0) ? 30.0f : -30.0f;
                moveDegrees(relativeAngle);
                unsigned long startTime = millis();
                while (stepper->isRunning() && danceInProgress && (millis() - startTime < 5000)) { // 5 seconds per move
                    vTaskDelay(pdMS_TO_TICKS(100)); // Longer delay to feed watchdog
                }
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                vTaskDelay(pdMS_TO_TICKS(50)); // Quick pause
            }
            break;
        }
        
        case DanceType::SPIN: {
            ESP_LOGD(TAG, "Performing dance: Spin");
            // Full rotations back and forth (relative movements)
            float relativeMoves[] = {360.0f, -720.0f, 720.0f, -360.0f};
            int numMoves = sizeof(relativeMoves) / sizeof(relativeMoves[0]);
            
            for (int i = 0; i < numMoves; i++) {
                // Check if dance was stopped
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                moveDegrees(relativeMoves[i]);
                unsigned long startTime = millis();
                while (stepper->isRunning() && danceInProgress && (millis() - startTime < 60000)) { // 60 seconds per move (for 720° at slow speeds)
                    vTaskDelay(pdMS_TO_TICKS(100)); // Longer delay to feed watchdog
                }
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                vTaskDelay(pdMS_TO_TICKS(300));
            }
            break;
        }
        
        case DanceType::WIGGLE: {
            ESP_LOGD(TAG, "Performing dance: Wiggle");
            // Small wiggles in place (relative movements)
            for (int i = 0; i < 12; i++) {
                // Check if dance was stopped
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                float relativeAngle = (i % 2 == 0) ? 15.0f : -15.0f;
                moveDegrees(relativeAngle);
                unsigned long startTime = millis();
                while (stepper->isRunning() && danceInProgress && (millis() - startTime < 3000)) { // 3 seconds per move
                    vTaskDelay(pdMS_TO_TICKS(100)); // Longer delay to feed watchdog
                }
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                vTaskDelay(pdMS_TO_TICKS(100));
            }
            break;
        }
        
        case DanceType::WATUSI: {
            ESP_LOGD(TAG, "Performing dance: Watusi");
            // Watusi - side-to-side alternating movements with increasing amplitude
            // Pattern: alternating left-right with increasing then decreasing angles
            float relativeMoves[] = {20.0f, -20.0f, 40.0f, -40.0f, 60.0f, -60.0f, 80.0f, -80.0f, 
                                     60.0f, -60.0f, 40.0f, -40.0f, 20.0f, -20.0f};
            int numMoves = sizeof(relativeMoves) / sizeof(relativeMoves[0]);
            
            for (int i = 0; i < numMoves; i++) {
                // Check if dance was stopped
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                moveDegrees(relativeMoves[i]);
                unsigned long startTime = millis();
                while (stepper->isRunning() && danceInProgress && (millis() - startTime < 5000)) { // 5 seconds per move
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                vTaskDelay(pdMS_TO_TICKS(150));
            }
            break;
        }
        
        case DanceType::PEPPERMINT_TWIST: {
            ESP_LOGD(TAG, "Performing dance: Peppermint Twist");
            // Peppermint Twist - rapid alternating twists back and forth
            // Pattern: rapid alternating movements with varying speeds
            for (int i = 0; i < 16; i++) {
                // Check if dance was stopped
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                // Alternating pattern with varying amplitudes
                float relativeAngle;
                if (i % 4 == 0) {
                    relativeAngle = 45.0f;  // Larger movement
                } else if (i % 4 == 1) {
                    relativeAngle = -45.0f;
                } else if (i % 4 == 2) {
                    relativeAngle = 30.0f;  // Medium movement
                } else {
                    relativeAngle = -30.0f;
                }
                
                moveDegrees(relativeAngle);
                unsigned long startTime = millis();
                while (stepper->isRunning() && danceInProgress && (millis() - startTime < 3000)) { // 3 seconds per move
                    vTaskDelay(pdMS_TO_TICKS(100));
                }
                if (!danceInProgress) {
                    ESP_LOGD(TAG, "Dance stopped by user");
                    return false;
                }
                vTaskDelay(pdMS_TO_TICKS(100)); // Quick transitions
            }
            break;
        }
    }
    
    ESP_LOGD(TAG, "Dance complete");
    return true;
}

bool StepperMotorController::performBehavior(BehaviorType behaviorType) {
    if (stepper == nullptr) {
        return false;
    }
    
    auto waitForMove = [&](unsigned long timeoutMs) -> bool {
        unsigned long startTime = millis();
        while (stepper->isRunning() && behaviorInProgress && (millis() - startTime < timeoutMs)) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
        return behaviorInProgress;
    };
    
    switch (behaviorType) {
        case BehaviorType::SCANNING: {
            ESP_LOGD(TAG, "Performing behavior: Scanning");
            setSpeedInHz(600.0f);
            for (int i = 0; i < 4; i++) {
                if (!behaviorInProgress) return false;
                moveDegrees(360.0f);
                if (!waitForMove(80000)) return false;
                vTaskDelay(pdMS_TO_TICKS(2000));
                if (!behaviorInProgress) return false;
                moveDegrees(-360.0f);
                if (!waitForMove(80000)) return false;
                vTaskDelay(pdMS_TO_TICKS(2000));
            }
            break;
        }
        
        case BehaviorType::SLEEPING: {
            ESP_LOGD(TAG, "Performing behavior: Sleeping");
            setSpeedInHz(250.0f);
            while (behaviorInProgress) {
                moveDegrees(6.0f);
                if (!waitForMove(10000)) return false;
                vTaskDelay(pdMS_TO_TICKS(5000));
                moveDegrees(-6.0f);
                if (!waitForMove(10000)) return false;
                vTaskDelay(pdMS_TO_TICKS(7000));
            }
            break;
        }
        
        case BehaviorType::EATING: {
            ESP_LOGD(TAG, "Performing behavior: Eating");
            setSpeedInHz(1000.0f);
            for (int i = 0; i < 10; i++) {
                if (!behaviorInProgress) return false;
                moveDegrees(25.0f);
                if (!waitForMove(5000)) return false;
                vTaskDelay(pdMS_TO_TICKS(500));
                moveDegrees(-25.0f);
                if (!waitForMove(5000)) return false;
                vTaskDelay(pdMS_TO_TICKS(1200));
            }
            break;
        }
        
        case BehaviorType::ALERT: {
            ESP_LOGD(TAG, "Performing behavior: Alert");
            setSpeedInHz(1800.0f);
            for (int i = 0; i < 20; i++) {
                if (!behaviorInProgress) return false;
                float angle = (i % 2 == 0) ? 55.0f : -55.0f;
                moveDegrees(angle);
                if (!waitForMove(4000)) return false;
                vTaskDelay(pdMS_TO_TICKS(250));
            }
            break;
        }
        
        case BehaviorType::ROARING: {
            ESP_LOGD(TAG, "Performing behavior: Roaring");
            setSpeedInHz(1300.0f);
            for (int r = 0; r < 2; r++) {
                if (!behaviorInProgress) return false;
                moveDegrees(180.0f);
                if (!waitForMove(20000)) return false;
                vTaskDelay(pdMS_TO_TICKS(400));
                moveDegrees(-180.0f);
                if (!waitForMove(20000)) return false;
                vTaskDelay(pdMS_TO_TICKS(400));
                moveDegrees(360.0f);
                if (!waitForMove(30000)) return false;
                vTaskDelay(pdMS_TO_TICKS(800));
            }
            break;
        }
        
        case BehaviorType::STALKING: {
            ESP_LOGD(TAG, "Performing behavior: Stalking");
            setSpeedInHz(600.0f);
            for (int i = 0; i < 8; i++) {
                if (!behaviorInProgress) return false;
                float angle = (i % 2 == 0) ? 40.0f : -40.0f;
                moveDegrees(angle);
                if (!waitForMove(20000)) return false;
                vTaskDelay(pdMS_TO_TICKS(3000));
            }
            break;
        }
        
        case BehaviorType::PLAYING: {
            ESP_LOGD(TAG, "Performing behavior: Playing");
            setSpeedInHz(1200.0f);
            float moves[] = {20, -30, 60, -15, 90, -45, 30, -60, 15, -20, 70, -35, 25, -55};
            int numMoves = sizeof(moves) / sizeof(moves[0]);
            for (int i = 0; i < numMoves; i++) {
                if (!behaviorInProgress) return false;
                moveDegrees(moves[i]);
                if (!waitForMove(15000)) return false;
                vTaskDelay(pdMS_TO_TICKS(400));
            }
            break;
        }
        
        case BehaviorType::RESTING: {
            ESP_LOGD(TAG, "Performing behavior: Resting");
            setSpeedInHz(300.0f);
            int cycle = 0;
            while (behaviorInProgress) {
                moveDegrees(6.0f);
                if (!waitForMove(8000)) return false;
                vTaskDelay(pdMS_TO_TICKS(9000));
                moveDegrees(-6.0f);
                if (!waitForMove(8000)) return false;
                vTaskDelay(pdMS_TO_TICKS(9000));
                cycle++;
                if (cycle % 3 == 0) {
                    moveDegrees(15.0f);
                    if (!waitForMove(8000)) return false;
                    vTaskDelay(pdMS_TO_TICKS(5000));
                    moveDegrees(-15.0f);
                    if (!waitForMove(8000)) return false;
                }
            }
            break;
        }
        
        case BehaviorType::HUNTING: {
            ESP_LOGD(TAG, "Performing behavior: Hunting");
            setSpeedInHz(900.0f);
            for (int i = 0; i < 5; i++) {
                if (!behaviorInProgress) return false;
                moveDegrees(110.0f);
                if (!waitForMove(20000)) return false;
                vTaskDelay(pdMS_TO_TICKS(700));
                moveDegrees(-110.0f);
                if (!waitForMove(20000)) return false;
                vTaskDelay(pdMS_TO_TICKS(700));
            }
            break;
        }
        
        case BehaviorType::VICTORY: {
            ESP_LOGD(TAG, "Performing behavior: Victory");
            setSpeedInHz(1700.0f);
            moveDegrees(360.0f);
            if (!waitForMove(30000)) return false;
            vTaskDelay(pdMS_TO_TICKS(500));
            moveDegrees(-360.0f);
            if (!waitForMove(30000)) return false;
            vTaskDelay(pdMS_TO_TICKS(500));
            for (int i = 0; i < 2; i++) {
                if (!behaviorInProgress) return false;
                moveDegrees(180.0f);
                if (!waitForMove(20000)) return false;
                vTaskDelay(pdMS_TO_TICKS(400));
                moveDegrees(-180.0f);
                if (!waitForMove(20000)) return false;
                vTaskDelay(pdMS_TO_TICKS(400));
            }
            moveDegrees(720.0f);
            if (!waitForMove(40000)) return false;
            vTaskDelay(pdMS_TO_TICKS(800));
            break;
        }
    }
    
    ESP_LOGD(TAG, "Behavior complete");
    return true;
}

uint16_t StepperMotorController::getTmcRmsCurrent() const {
    if (tmcDriver != nullptr) {
        return tmcDriver->rms_current();
    }
    return 0;
}

uint8_t StepperMotorController::getTmcCsActual() const {
    if (tmcDriver != nullptr) {
        return tmcDriver->cs_actual();
    }
    return 0;
}

float StepperMotorController::getTmcActualCurrent() const {
    if (tmcDriver != nullptr) {
        uint8_t csActual = tmcDriver->cs_actual();
        uint16_t rmsCurrent = tmcDriver->rms_current();
        return (csActual / 32.0f) * rmsCurrent;
    }
    return 0.0f;
}

uint8_t StepperMotorController::getTmcIrun() const {
    if (tmcDriver != nullptr) {
        return tmcDriver->irun();
    }
    return 0;
}

uint8_t StepperMotorController::getTmcIhold() const {
    if (tmcDriver != nullptr) {
        return tmcDriver->ihold();
    }
    return 0;
}

bool StepperMotorController::getTmcEnabled() const {
    if (tmcDriver != nullptr) {
        return tmcDriver->toff() > 0;
    }
    return false;
}

bool StepperMotorController::getTmcSpreadCycle() const {
    if (tmcDriver != nullptr) {
        return tmcDriver->en_spreadCycle();
    }
    return false;
}

bool StepperMotorController::getTmcPwmAutoscale() const {
    if (tmcDriver != nullptr) {
        return tmcDriver->pwm_autoscale();
    }
    return false;
}

uint8_t StepperMotorController::getTmcBlankTime() const {
    if (tmcDriver != nullptr) {
        return tmcDriver->blank_time();
    }
    return 0;
}

void StepperMotorController::setTmcRmsCurrent(uint16_t ma) {
    if (tmcDriver != nullptr) {
        tmcDriver->rms_current(ma);
    }
}

void StepperMotorController::setTmcIrun(uint8_t irun) {
    if (tmcDriver != nullptr) {
        tmcDriver->irun(irun);
    }
}

void StepperMotorController::setTmcIhold(uint8_t ihold) {
    if (tmcDriver != nullptr) {
        tmcDriver->ihold(ihold);
    }
}

void StepperMotorController::setTmcSpreadCycle(bool enabled) {
    if (tmcDriver != nullptr) {
        tmcDriver->en_spreadCycle(enabled);
    }
}

void StepperMotorController::setTmcPwmAutoscale(bool enabled) {
    if (tmcDriver != nullptr) {
        tmcDriver->pwm_autoscale(enabled);
    }
}

void StepperMotorController::setTmcBlankTime(uint8_t blankTime) {
    if (tmcDriver != nullptr) {
        tmcDriver->blank_time(blankTime);
    }
}
