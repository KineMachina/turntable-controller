#include "MQTTController.h"
#include <FreeRTOS.h>
#include <task.h>
#include <ArduinoJson.h>

// Static instance pointer for callbacks
MQTTController* MQTTController::instance = nullptr;

MQTTController::MQTTController()
    : stepperController(nullptr), commandQueue(nullptr), configManager(nullptr),
      mqttTaskHandle(nullptr), mqttCommandQueue(nullptr),
      lastStatusPublishTime(0), pendingMoveComplete(false)
{
    // Initialize state tracking
    memset(&lastState, 0, sizeof(lastState));
    pendingMoveCommandType[0] = '\0';
    pendingMoveRequestId[0] = '\0';
    
    // Set instance pointer
    instance = this;
}

MQTTController::~MQTTController()
{
    if (mqttClient.connected())
    {
        mqttClient.disconnect();
    }
    
    if (mqttTaskHandle != nullptr)
    {
        vTaskDelete(mqttTaskHandle);
        mqttTaskHandle = nullptr;
    }
    
    if (mqttCommandQueue != nullptr)
    {
        vQueueDelete(mqttCommandQueue);
        mqttCommandQueue = nullptr;
    }
    
    if (instance == this)
    {
        instance = nullptr;
    }
}

void MQTTController::buildTopics()
{
    snprintf(commandTopic, sizeof(commandTopic), "%s/%s/command", config.baseTopic, config.deviceId);
    snprintf(statusTopicPrefix, sizeof(statusTopicPrefix), "%s/%s/status", config.baseTopic, config.deviceId);
    snprintf(responseTopic, sizeof(responseTopic), "%s/%s/response", config.baseTopic, config.deviceId);
    snprintf(onlineTopic, sizeof(onlineTopic), "%s/%s/status/online", config.baseTopic, config.deviceId);

    Serial.println("[MQTT] Topics configured:");
    Serial.printf("  Command: %s\n", commandTopic);
    Serial.printf("  Status: %s\n", statusTopicPrefix);
    Serial.printf("  Response: %s\n", responseTopic);
    Serial.printf("  Online: %s\n", onlineTopic);
}

void MQTTController::subscribeToCommands()
{
    if (!mqttClient.connected())
    {
        return;
    }

    uint16_t packetId = mqttClient.subscribe(commandTopic, config.qosCommands);
    Serial.printf("[MQTT] Subscribed to: %s (QoS %u, packet ID: %u)\n", commandTopic, config.qosCommands, packetId);
}

void MQTTController::publishStatus(bool force)
{
    if (!mqttClient.connected() || stepperController == nullptr)
    {
        return;
    }

    if (!force && !hasStateChanged())
    {
        return;
    }

    updateState();

    JsonDocument doc;
    doc["status"] = "success";
    doc["timestamp"] = millis();
    doc["uptime_ms"] = millis();
    doc["free_heap"] = ESP.getFreeHeap();

    // Motor status
    JsonObject motor = doc["motor"].to<JsonObject>();
    motor["enabled"] = stepperController->isEnabled();
    motor["running"] = stepperController->isRunning();
    motor["position"] = stepperController->getStepperPosition();
    motor["positionDegrees"] = stepperController->getStepperPositionDegrees();
    motor["speedHz"] = stepperController->getTargetSpeedHz();
    motor["microsteps"] = stepperController->getMicrosteps();
    motor["gearRatio"] = stepperController->getGearRatio();
    motor["behaviorInProgress"] = stepperController->isBehaviorInProgress();
    motor["danceInProgress"] = stepperController->isDanceInProgress();

    // TMC2209 (UART reads are slow - yield between each)
    vTaskDelay(pdMS_TO_TICKS(10));
    JsonObject tmc = doc["tmc2209"].to<JsonObject>();
    tmc["rmsCurrent"] = stepperController->getTmcRmsCurrent();
    vTaskDelay(pdMS_TO_TICKS(25));
    tmc["csActual"] = stepperController->getTmcCsActual();
    vTaskDelay(pdMS_TO_TICKS(25));
    tmc["actualCurrent"] = stepperController->getTmcActualCurrent();
    vTaskDelay(pdMS_TO_TICKS(25));
    tmc["irun"] = stepperController->getTmcIrun();
    vTaskDelay(pdMS_TO_TICKS(25));
    tmc["ihold"] = stepperController->getTmcIhold();
    vTaskDelay(pdMS_TO_TICKS(25));
    tmc["enabled"] = stepperController->getTmcEnabled();
    vTaskDelay(pdMS_TO_TICKS(25));
    tmc["spreadCycle"] = stepperController->getTmcSpreadCycle();
    vTaskDelay(pdMS_TO_TICKS(25));
    tmc["pwmAutoscale"] = stepperController->getTmcPwmAutoscale();
    vTaskDelay(pdMS_TO_TICKS(25));
    tmc["blankTime"] = stepperController->getTmcBlankTime();
    vTaskDelay(pdMS_TO_TICKS(25));

    // WiFi status
    JsonObject wifi = doc["wifi"].to<JsonObject>();
    wifi["connected"] = (WiFi.status() == WL_CONNECTED);
    if (WiFi.status() == WL_CONNECTED)
    {
        wifi["ip"] = WiFi.localIP().toString();
    }

    String statusStr;
    serializeJson(doc, statusStr);
    mqttClient.publish(statusTopicPrefix, config.qosStatus, true, statusStr.c_str(), statusStr.length());
}

void MQTTController::publishResponse(const char* command, bool success, const char* message, const char* error)
{
    if (!mqttClient.connected())
    {
        return;
    }
    
    JsonDocument doc;
    doc["status"] = success ? "success" : "error";
    doc["command"] = command;
    doc["executed"] = success;
    doc["message"] = message;
    doc["timestamp"] = millis();
    
    if (error != nullptr)
    {
        doc["error"] = error;
    }
    
    String responseStr;
    serializeJson(doc, responseStr);
    
    mqttClient.publish(responseTopic, config.qosCommands, false, responseStr.c_str(), responseStr.length());
}

void MQTTController::publishMoveCompleteResponse(const char* commandType, const char* requestId)
{
    if (!mqttClient.connected())
    {
        return;
    }
    JsonDocument doc;
    doc["status"] = "success";
    doc["command"] = commandType;
    doc["executed"] = true;
    doc["message"] = "Move complete";
    doc["event"] = "complete";
    doc["timestamp"] = millis();
    if (requestId != nullptr && requestId[0] != '\0')
    {
        doc["request_id"] = requestId;
    }
    String responseStr;
    serializeJson(doc, responseStr);
    mqttClient.publish(responseTopic, config.qosCommands, false, responseStr.c_str(), responseStr.length());
}

bool MQTTController::hasStateChanged()
{
    if (stepperController == nullptr)
    {
        return false;
    }
    
    bool enabled = stepperController->isEnabled();
    bool running = stepperController->isRunning();
    long position = stepperController->getStepperPosition();
    float positionDegrees = stepperController->getStepperPositionDegrees();
    float speedHz = stepperController->getTargetSpeedHz();
    uint8_t microsteps = stepperController->getMicrosteps();
    float gearRatio = stepperController->getGearRatio();
    bool behaviorInProgress = stepperController->isBehaviorInProgress();
    bool danceInProgress = stepperController->isDanceInProgress();

    return (enabled != lastState.enabled ||
            running != lastState.running ||
            position != lastState.position ||
            abs(positionDegrees - lastState.positionDegrees) > 0.1f ||
            abs(speedHz - lastState.speedHz) > 0.1f ||
            microsteps != lastState.microsteps ||
            abs(gearRatio - lastState.gearRatio) > 0.01f ||
            behaviorInProgress != lastState.behaviorInProgress ||
            danceInProgress != lastState.danceInProgress);
}

void MQTTController::updateState()
{
    if (stepperController == nullptr)
    {
        return;
    }
    
    lastState.enabled = stepperController->isEnabled();
    lastState.running = stepperController->isRunning();
    lastState.position = stepperController->getStepperPosition();
    lastState.positionDegrees = stepperController->getStepperPositionDegrees();
    lastState.speedHz = stepperController->getTargetSpeedHz();
    lastState.microsteps = stepperController->getMicrosteps();
    lastState.gearRatio = stepperController->getGearRatio();
    lastState.behaviorInProgress = stepperController->isBehaviorInProgress();
    lastState.danceInProgress = stepperController->isDanceInProgress();
}

void MQTTController::handleCommand(const char* payload, size_t len)
{
    // Parse JSON to extract command type
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);

    if (error)
    {
        Serial.printf("[MQTT] ERROR: Invalid JSON: %s\n", error.c_str());
        publishResponse("unknown", false, "Invalid JSON", error.c_str());
        return;
    }

    if (!doc["command"].is<const char*>() && !doc["command"].is<String>())
    {
        Serial.println("[MQTT] ERROR: Missing 'command' field");
        publishResponse("unknown", false, "Missing 'command' field", "Expected string");
        return;
    }

    String commandType = doc["command"].as<String>();
    commandType.toLowerCase();
    Serial.printf("[MQTT] Handling command: %s\n", commandType.c_str());

    if (commandType == "position")
    {
        handlePosition(payload, len);
    }
    else if (commandType == "heading")
    {
        handleHeading(payload, len);
    }
    else if (commandType == "enable")
    {
        handleEnable(payload, len);
    }
    else if (commandType == "speed")
    {
        handleSpeed(payload, len);
    }
    else if (commandType == "acceleration")
    {
        handleAcceleration(payload, len);
    }
    else if (commandType == "microsteps")
    {
        handleMicrosteps(payload, len);
    }
    else if (commandType == "gearratio")
    {
        handleGearRatio(payload, len);
    }
    else if (commandType == "speedhz")
    {
        handleSpeedHz(payload, len);
    }
    else if (commandType == "runforward")
    {
        handleRunForward(payload, len);
    }
    else if (commandType == "runbackward")
    {
        handleRunBackward(payload, len);
    }
    else if (commandType == "stop" || commandType == "stopmove")
    {
        handleStopMove(payload, len);
    }
    else if (commandType == "forcestop")
    {
        handleForceStop(payload, len);
    }
    else if (commandType == "reset")
    {
        handleReset(payload, len);
    }
    else if (commandType == "zero")
    {
        handleZero(payload, len);
    }
    else if (commandType == "home")
    {
        handleHome(payload, len);
    }
    else if (commandType == "dance")
    {
        handleDance(payload, len);
    }
    else if (commandType == "stopdance")
    {
        handleStopDance(payload, len);
    }
    else if (commandType == "behavior")
    {
        handleBehavior(payload, len);
    }
    else if (commandType == "stopbehavior")
    {
        handleStopBehavior(payload, len);
    }
    else
    {
        Serial.printf("[MQTT] ERROR: Unknown command: %s\n", commandType.c_str());
        publishResponse(commandType.c_str(), false, "Unknown command", "Command not recognized");
    }
}

void MQTTController::handlePosition(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    
    if (error)
    {
        publishResponse("position", false, "Invalid JSON", error.c_str());
        return;
    }
    
    if (!doc["position"].is<float>())
    {
        publishResponse("position", false, "Missing or invalid 'position' parameter", "Expected float");
        return;
    }
    
    float position = doc["position"].as<float>();
    
    // Send command via queue
    if (commandQueue != nullptr)
    {
        MotorCommand cmd;
        cmd.type = MotorCommandType::MOVE_TO;
        cmd.data.position.value = position;
        cmd.statusCallback = nullptr;
        cmd.statusContext = nullptr;
        
        if (commandQueue->sendCommand(cmd, pdMS_TO_TICKS(100)))
        {
            publishResponse("position", true, "Position command queued", nullptr);
            publishStatus(true); // Force status update
            pendingMoveComplete = true;
            strncpy(pendingMoveCommandType, "position", sizeof(pendingMoveCommandType) - 1);
            pendingMoveCommandType[sizeof(pendingMoveCommandType) - 1] = '\0';
            if (doc["request_id"].is<const char*>() || doc["request_id"].is<String>())
            {
                String rid = doc["request_id"].as<String>();
                strncpy(pendingMoveRequestId, rid.c_str(), sizeof(pendingMoveRequestId) - 1);
                pendingMoveRequestId[sizeof(pendingMoveRequestId) - 1] = '\0';
            }
            else
            {
                pendingMoveRequestId[0] = '\0';
            }
        }
        else
        {
            publishResponse("position", false, "Command queue full", nullptr);
        }
    }
    else
    {
        // Fallback to direct call
        stepperController->moveToDegrees(position);
        publishResponse("position", true, "Position set", nullptr);
        publishStatus(true);
        pendingMoveComplete = true;
        strncpy(pendingMoveCommandType, "position", sizeof(pendingMoveCommandType) - 1);
        pendingMoveCommandType[sizeof(pendingMoveCommandType) - 1] = '\0';
        if (doc["request_id"].is<const char*>() || doc["request_id"].is<String>())
        {
            String rid = doc["request_id"].as<String>();
            strncpy(pendingMoveRequestId, rid.c_str(), sizeof(pendingMoveRequestId) - 1);
            pendingMoveRequestId[sizeof(pendingMoveRequestId) - 1] = '\0';
        }
        else
        {
            pendingMoveRequestId[0] = '\0';
        }
    }
}

void MQTTController::handleHeading(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    
    if (error)
    {
        publishResponse("heading", false, "Invalid JSON", error.c_str());
        return;
    }
    
    if (!doc["heading"].is<float>() && !doc["heading"].is<int>())
    {
        publishResponse("heading", false, "Missing or invalid 'heading' parameter", "Expected float (0-360)");
        return;
    }
    
    float heading = doc["heading"].as<float>();
    
    // Direct call (uses moveToHeadingDegrees)
    bool success = stepperController->moveToHeadingDegrees(heading);
    
    if (success)
    {
        publishResponse("heading", true, "Moving to heading", nullptr);
        publishStatus(true);
        pendingMoveComplete = true;
        strncpy(pendingMoveCommandType, "heading", sizeof(pendingMoveCommandType) - 1);
        pendingMoveCommandType[sizeof(pendingMoveCommandType) - 1] = '\0';
        if (doc["request_id"].is<const char*>() || doc["request_id"].is<String>())
        {
            String rid = doc["request_id"].as<String>();
            strncpy(pendingMoveRequestId, rid.c_str(), sizeof(pendingMoveRequestId) - 1);
            pendingMoveRequestId[sizeof(pendingMoveRequestId) - 1] = '\0';
        }
        else
        {
            pendingMoveRequestId[0] = '\0';
        }
    }
    else
    {
        publishResponse("heading", false, "Failed to move to heading", "Stepper not available");
    }
}

void MQTTController::handleEnable(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    
    if (error)
    {
        publishResponse("enable", false, "Invalid JSON", error.c_str());
        return;
    }
    
    if (!doc["enable"].is<bool>())
    {
        publishResponse("enable", false, "Missing or invalid 'enable' parameter", "Expected bool");
        return;
    }
    
    bool enable = doc["enable"].as<bool>();
    
    // Send command via queue
    if (commandQueue != nullptr)
    {
        MotorCommand cmd;
        cmd.type = MotorCommandType::ENABLE;
        cmd.data.enable.enable = enable;
        cmd.statusCallback = nullptr;
        cmd.statusContext = nullptr;
        
        if (commandQueue->sendCommand(cmd, pdMS_TO_TICKS(100)))
        {
            publishResponse("enable", true, enable ? "Motor enabled" : "Motor disabled", nullptr);
            publishStatus(true);
        }
        else
        {
            publishResponse("enable", false, "Command queue full", nullptr);
        }
    }
    else
    {
        // Fallback to direct call
        stepperController->enable(enable);
        publishResponse("enable", true, enable ? "Motor enabled" : "Motor disabled", nullptr);
        publishStatus(true);
    }
}

void MQTTController::handleSpeed(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    
    if (error)
    {
        publishResponse("speed", false, "Invalid JSON", error.c_str());
        return;
    }
    
    if (!doc["speed"].is<float>())
    {
        publishResponse("speed", false, "Missing or invalid 'speed' parameter", "Expected float > 0");
        return;
    }
    
    float speed = doc["speed"].as<float>();
    
    if (speed <= 0)
    {
        publishResponse("speed", false, "Speed must be greater than 0", nullptr);
        return;
    }
    
    // Direct call (settings)
    stepperController->setMaxSpeed(speed);
    publishResponse("speed", true, "Max speed set", nullptr);
    publishStatus(true);
}

void MQTTController::handleAcceleration(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    
    if (error)
    {
        publishResponse("acceleration", false, "Invalid JSON", error.c_str());
        return;
    }
    
    if (!doc["accel"].is<float>())
    {
        publishResponse("acceleration", false, "Missing or invalid 'accel' parameter", "Expected float > 0");
        return;
    }
    
    float accel = doc["accel"].as<float>();
    
    if (accel <= 0)
    {
        publishResponse("acceleration", false, "Acceleration must be greater than 0", nullptr);
        return;
    }
    
    // Direct call (settings)
    stepperController->setAcceleration(accel);
    publishResponse("acceleration", true, "Acceleration set", nullptr);
    publishStatus(true);
}

void MQTTController::handleMicrosteps(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    
    if (error)
    {
        publishResponse("microsteps", false, "Invalid JSON", error.c_str());
        return;
    }
    
    if (!doc["microsteps"].is<int>())
    {
        publishResponse("microsteps", false, "Missing or invalid 'microsteps' parameter", "Expected int");
        return;
    }
    
    int microsteps = doc["microsteps"].as<int>();
    
    // Direct call (settings)
    if (stepperController->setMicrosteps(microsteps))
    {
        publishResponse("microsteps", true, "Microstepping set", nullptr);
        publishStatus(true);
    }
    else
    {
        publishResponse("microsteps", false, "Invalid microstepping value", "Must be 1, 2, 4, 8, 16, 32, 64, 128, or 256");
    }
}

void MQTTController::handleGearRatio(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    
    if (error)
    {
        publishResponse("gearratio", false, "Invalid JSON", error.c_str());
        return;
    }
    
    if (!doc["ratio"].is<float>())
    {
        publishResponse("gearratio", false, "Missing or invalid 'ratio' parameter", "Expected float");
        return;
    }
    
    float ratio = doc["ratio"].as<float>();
    
    if (ratio <= 0.0f || ratio > 100.0f)
    {
        publishResponse("gearratio", false, "Invalid gear ratio value", "Must be between 0.1 and 100.0");
        return;
    }
    
    // Direct call (settings)
    stepperController->setGearRatio(ratio);
    publishResponse("gearratio", true, "Gear ratio set", nullptr);
    publishStatus(true);
}

void MQTTController::handleSpeedHz(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    
    if (error)
    {
        publishResponse("speedHz", false, "Invalid JSON", error.c_str());
        return;
    }
    
    if (!doc["speedHz"].is<float>())
    {
        publishResponse("speedHz", false, "Missing or invalid 'speedHz' parameter", "Expected float >= 0");
        return;
    }
    
    float speedHz = doc["speedHz"].as<float>();
    
    if (speedHz < 0)
    {
        publishResponse("speedHz", false, "Speed must be >= 0 Hz", nullptr);
        return;
    }
    
    // Direct call (immediate action)
    stepperController->setSpeedInHz(speedHz);
    publishResponse("speedHz", true, "Speed set", nullptr);
    publishStatus(true);
}

void MQTTController::handleRunForward(const char* payload, size_t len)
{
    // Direct call (immediate action)
    stepperController->runForward();
    publishResponse("runForward", true, "Forward rotation started", nullptr);
    publishStatus(true);
}

void MQTTController::handleRunBackward(const char* payload, size_t len)
{
    // Direct call (immediate action)
    stepperController->runBackward();
    publishResponse("runBackward", true, "Backward rotation started", nullptr);
    publishStatus(true);
}

void MQTTController::handleStopMove(const char* payload, size_t len)
{
    pendingMoveComplete = false;
    pendingMoveCommandType[0] = '\0';
    pendingMoveRequestId[0] = '\0';
    // Direct call (immediate action)
    stepperController->stopMove();
    publishResponse("stopMove", true, "Move stopped", nullptr);
    publishStatus(true);
}

void MQTTController::handleForceStop(const char* payload, size_t len)
{
    pendingMoveComplete = false;
    pendingMoveCommandType[0] = '\0';
    pendingMoveRequestId[0] = '\0';
    // Direct call (immediate action)
    stepperController->stopVelocity();
    publishResponse("forceStop", true, "Force stop executed", nullptr);
    publishStatus(true);
}

void MQTTController::handleReset(const char* payload, size_t len)
{
    // Direct call (immediate)
    bool success = stepperController->resetEngine();
    
    if (success)
    {
        publishResponse("reset", true, "Engine reset successfully", nullptr);
        publishStatus(true);
    }
    else
    {
        publishResponse("reset", false, "Failed to reset engine", nullptr);
    }
}

void MQTTController::handleZero(const char* payload, size_t len)
{
    // Send command via queue
    if (commandQueue != nullptr)
    {
        MotorCommand cmd;
        cmd.type = MotorCommandType::ZERO_POSITION;
        cmd.statusCallback = nullptr;
        cmd.statusContext = nullptr;
        
        if (commandQueue->sendCommand(cmd, pdMS_TO_TICKS(100)))
        {
            publishResponse("zero", true, "Zero position command queued", nullptr);
            publishStatus(true);
        }
        else
        {
            publishResponse("zero", false, "Command queue full", nullptr);
        }
    }
    else
    {
        publishResponse("zero", false, "Command queue not available", nullptr);
    }
}

void MQTTController::handleHome(const char* payload, size_t len)
{
    // Send command via queue
    if (commandQueue != nullptr)
    {
        MotorCommand cmd;
        cmd.type = MotorCommandType::HOME;
        cmd.statusCallback = nullptr;
        cmd.statusContext = nullptr;
        
        if (commandQueue->sendCommand(cmd, pdMS_TO_TICKS(100)))
        {
            publishResponse("home", true, "Home command queued", nullptr);
            publishStatus(true);
        }
        else
        {
            publishResponse("home", false, "Command queue full", nullptr);
        }
    }
    else
    {
        publishResponse("home", false, "Command queue not available", nullptr);
    }
}

void MQTTController::handleDance(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    
    if (error)
    {
        publishResponse("dance", false, "Invalid JSON", error.c_str());
        return;
    }
    
    if (!doc["danceType"].is<const char*>() && !doc["danceType"].is<String>())
    {
        publishResponse("dance", false, "Missing or invalid 'danceType' parameter", "Expected string");
        return;
    }
    
    String danceTypeStr = doc["danceType"].as<String>();
    danceTypeStr.toLowerCase();
    
    StepperMotorController::DanceType danceType;
    if (danceTypeStr == "twist")
    {
        danceType = StepperMotorController::DanceType::TWIST;
    }
    else if (danceTypeStr == "shake")
    {
        danceType = StepperMotorController::DanceType::SHAKE;
    }
    else if (danceTypeStr == "spin")
    {
        danceType = StepperMotorController::DanceType::SPIN;
    }
    else if (danceTypeStr == "wiggle")
    {
        danceType = StepperMotorController::DanceType::WIGGLE;
    }
    else if (danceTypeStr == "watusi")
    {
        danceType = StepperMotorController::DanceType::WATUSI;
    }
    else if (danceTypeStr == "pepperminttwist" || danceTypeStr == "peppermint_twist")
    {
        danceType = StepperMotorController::DanceType::PEPPERMINT_TWIST;
    }
    else
    {
        publishResponse("dance", false, "Invalid danceType", "Must be 'twist', 'shake', 'spin', 'wiggle', 'watusi', or 'peppermintTwist'");
        return;
    }
    
    // Direct call (background task)
    bool success = stepperController->startDance(danceType);
    
    if (success)
    {
        publishResponse("dance", true, "Dance started", nullptr);
        publishStatus(true);
    }
    else
    {
        publishResponse("dance", false, "Dance failed to start", "Dance already in progress or stepper unavailable");
    }
}

void MQTTController::handleStopDance(const char* payload, size_t len)
{
    // Direct call (background task)
    bool success = stepperController->stopDance();
    
    if (success)
    {
        publishResponse("stopDance", true, "Dance stopped", nullptr);
        publishStatus(true);
    }
    else
    {
        publishResponse("stopDance", false, "No dance in progress", nullptr);
    }
}

void MQTTController::handleBehavior(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    
    if (error)
    {
        publishResponse("behavior", false, "Invalid JSON", error.c_str());
        return;
    }
    
    if (!doc["behaviorType"].is<const char*>() && !doc["behaviorType"].is<String>())
    {
        publishResponse("behavior", false, "Missing or invalid 'behaviorType' parameter", "Expected string");
        return;
    }
    
    String behaviorTypeStr = doc["behaviorType"].as<String>();
    behaviorTypeStr.toLowerCase();
    
    StepperMotorController::BehaviorType behaviorType;
    if (behaviorTypeStr == "scanning")
    {
        behaviorType = StepperMotorController::BehaviorType::SCANNING;
    }
    else if (behaviorTypeStr == "sleeping")
    {
        behaviorType = StepperMotorController::BehaviorType::SLEEPING;
    }
    else if (behaviorTypeStr == "eating")
    {
        behaviorType = StepperMotorController::BehaviorType::EATING;
    }
    else if (behaviorTypeStr == "alert")
    {
        behaviorType = StepperMotorController::BehaviorType::ALERT;
    }
    else if (behaviorTypeStr == "roaring")
    {
        behaviorType = StepperMotorController::BehaviorType::ROARING;
    }
    else if (behaviorTypeStr == "stalking")
    {
        behaviorType = StepperMotorController::BehaviorType::STALKING;
    }
    else if (behaviorTypeStr == "playing")
    {
        behaviorType = StepperMotorController::BehaviorType::PLAYING;
    }
    else if (behaviorTypeStr == "resting")
    {
        behaviorType = StepperMotorController::BehaviorType::RESTING;
    }
    else if (behaviorTypeStr == "hunting")
    {
        behaviorType = StepperMotorController::BehaviorType::HUNTING;
    }
    else if (behaviorTypeStr == "victory")
    {
        behaviorType = StepperMotorController::BehaviorType::VICTORY;
    }
    else
    {
        publishResponse("behavior", false, "Unknown behaviorType", nullptr);
        return;
    }
    
    // Direct call (background task)
    bool started = stepperController->startBehavior(behaviorType);
    
    if (started)
    {
        publishResponse("behavior", true, "Behavior started", nullptr);
        publishStatus(true);
    }
    else
    {
        publishResponse("behavior", false, "Behavior already running or stepper unavailable", nullptr);
    }
}

void MQTTController::handleStopBehavior(const char* payload, size_t len)
{
    // Direct call (background task)
    bool success = stepperController->stopBehavior();
    
    if (success)
    {
        publishResponse("stopBehavior", true, "Behavior stopped", nullptr);
        publishStatus(true);
    }
    else
    {
        publishResponse("stopBehavior", false, "No behavior in progress", nullptr);
    }
}

// Static callback wrappers
void MQTTController::onMqttConnect(bool sessionPresent)
{
    if (instance == nullptr)
    {
        return;
    }
    
    Serial.println("[MQTT] ========================================");
    Serial.println("[MQTT] CONNECTED TO BROKER");
    Serial.printf("[MQTT] Session present: %s\n", sessionPresent ? "Yes (resumed)" : "No (new session)");
    Serial.printf("[MQTT] Broker: %s:%u\n", instance->config.broker, instance->config.port);
    Serial.printf("[MQTT] Device ID: %s\n", instance->config.deviceId);
    Serial.printf("[MQTT] Keepalive: %u seconds\n", instance->config.keepalive);
    Serial.println("[MQTT] ========================================");
    
    // Publish online status (LWT)
    uint16_t onlinePacketId = instance->mqttClient.publish(instance->onlineTopic, instance->config.qosStatus, true, "online", 6);
    Serial.printf("[MQTT] Published online status to %s (packet ID: %u, retained)\n", instance->onlineTopic, onlinePacketId);
    
    // Subscribe to commands
    instance->subscribeToCommands();
    
    // Publish initial status and full status
    Serial.println("[MQTT] Publishing initial status...");
    instance->publishStatus(true);
    Serial.println("[MQTT] Initial status published");
}

void MQTTController::onMqttDisconnect(AsyncMqttClientDisconnectReason reason)
{
    if (instance == nullptr)
    {
        return;
    }
    
    Serial.println("[MQTT] ========================================");
    Serial.println("[MQTT] DISCONNECTED FROM BROKER");
    Serial.print("[MQTT] Reason code: ");
    Serial.print((int)reason);
    Serial.print(" (");
    
    // Human-readable disconnect reasons
    switch (reason)
    {
        case AsyncMqttClientDisconnectReason::TCP_DISCONNECTED:
            Serial.print("TCP_DISCONNECTED - Network connection lost");
            break;
        case AsyncMqttClientDisconnectReason::MQTT_UNACCEPTABLE_PROTOCOL_VERSION:
            Serial.print("MQTT_UNACCEPTABLE_PROTOCOL_VERSION - Protocol version mismatch");
            break;
        case AsyncMqttClientDisconnectReason::MQTT_IDENTIFIER_REJECTED:
            Serial.print("MQTT_IDENTIFIER_REJECTED - Client ID rejected by broker");
            break;
        case AsyncMqttClientDisconnectReason::MQTT_SERVER_UNAVAILABLE:
            Serial.print("MQTT_SERVER_UNAVAILABLE - Broker unavailable");
            break;
        case AsyncMqttClientDisconnectReason::MQTT_MALFORMED_CREDENTIALS:
            Serial.print("MQTT_MALFORMED_CREDENTIALS - Invalid username/password");
            break;
        case AsyncMqttClientDisconnectReason::MQTT_NOT_AUTHORIZED:
            Serial.print("MQTT_NOT_AUTHORIZED - Authentication failed");
            break;
        case AsyncMqttClientDisconnectReason::ESP8266_NOT_ENOUGH_SPACE:
            Serial.print("ESP8266_NOT_ENOUGH_SPACE - Not enough space (ESP8266 specific)");
            break;
        case AsyncMqttClientDisconnectReason::TLS_BAD_FINGERPRINT:
            Serial.print("TLS_BAD_FINGERPRINT - TLS fingerprint mismatch");
            break;
        default:
            Serial.print("UNKNOWN");
            break;
    }
    Serial.println(")");
    
    if (instance->config.broker != nullptr)
    {
        Serial.printf("[MQTT] Broker was: %s:%u\n", instance->config.broker, instance->config.port);
    }
    Serial.println("[MQTT] ========================================");
}

void MQTTController::onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)
{
    if (instance == nullptr)
    {
        return;
    }

    if (index == 0)
    {
        Serial.printf("[MQTT] Message received - Topic: %s, QoS: %u, Retain: %s, Total size: %zu bytes\n",
                     topic, properties.qos, properties.retain ? "Yes" : "No", total);
    }

    // Handle multi-part messages
    static char fullPayload[512];
    static size_t fullPayloadLen = 0;

    if (index == 0)
    {
        fullPayloadLen = 0;
    }

    size_t copyLen = len;
    if (fullPayloadLen + copyLen >= sizeof(fullPayload))
    {
        copyLen = sizeof(fullPayload) - fullPayloadLen - 1;
    }

    memcpy(fullPayload + fullPayloadLen, payload, copyLen);
    fullPayloadLen += copyLen;

    if (index + len >= total)
    {
        fullPayload[fullPayloadLen] = '\0';

        MQTTCommand cmd;
        strncpy(cmd.payload, fullPayload, sizeof(cmd.payload) - 1);
        cmd.payload[sizeof(cmd.payload) - 1] = '\0';
        cmd.payloadLen = fullPayloadLen;

        Serial.printf("[MQTT] Queuing command - Payload: %.*s\n", (int)fullPayloadLen, fullPayload);

        if (xQueueSend(instance->mqttCommandQueue, &cmd, 0) != pdTRUE)
        {
            Serial.println("[MQTT] ERROR: Command queue full, dropping message");
        }
        else
        {
            Serial.println("[MQTT] Command queued successfully");
        }

        fullPayloadLen = 0;
    }
}

void MQTTController::onMqttPublish(uint16_t packetId)
{
    // Acknowledgment received - publish confirmed by broker
    // Note: packetId 0 means QoS 0 (no acknowledgment), so we only log QoS 1/2 publishes
    if (packetId > 0)
    {
        Serial.printf("[MQTT] Publish confirmed (packet ID: %u)\n", packetId);
    }
}

// FreeRTOS task
void MQTTController::mqttTaskWrapper(void* parameter)
{
    MQTTController* controller = static_cast<MQTTController*>(parameter);
    if (controller != nullptr)
    {
        controller->mqttTask();
    }
    vTaskDelete(nullptr);
}

void MQTTController::mqttTask()
{
    Serial.println("[MQTT] Task started");

    MQTTCommand cmd;
    const TickType_t updateInterval = pdMS_TO_TICKS(100);
    TickType_t lastStatusCheck = xTaskGetTickCount();

    while (true)
    {
        // Process commands from queue
        if (xQueueReceive(mqttCommandQueue, &cmd, pdMS_TO_TICKS(100)) == pdTRUE)
        {
            Serial.printf("[MQTT] Processing queued command - Payload: %.*s\n",
                         (int)cmd.payloadLen, cmd.payload);
            handleCommand(cmd.payload, cmd.payloadLen);
        }

        // Move complete: when a heading/position move was started and motor has stopped
        if (pendingMoveComplete && stepperController != nullptr && !stepperController->isRunning())
        {
            publishMoveCompleteResponse(
                pendingMoveCommandType[0] != '\0' ? pendingMoveCommandType : "move",
                pendingMoveRequestId[0] != '\0' ? pendingMoveRequestId : nullptr);
            pendingMoveComplete = false;
            pendingMoveCommandType[0] = '\0';
            pendingMoveRequestId[0] = '\0';
        }

        // Periodic status publishing
        TickType_t currentTick = xTaskGetTickCount();
        if ((currentTick - lastStatusCheck) >= pdMS_TO_TICKS(STATUS_PUBLISH_INTERVAL_MS))
        {
            if (mqttClient.connected())
            {
                publishStatus(true);
            }

            lastStatusCheck = currentTick;
        }

        vTaskDelay(updateInterval);
    }
}

bool MQTTController::begin(StepperMotorController* stepperCtrl, MotorCommandQueue* cmdQueue, const MQTTConfig* cfg, ConfigurationManager* configMgr)
{
    stepperController = stepperCtrl;
    commandQueue = cmdQueue;
    configManager = configMgr;
    
    if (cfg != nullptr)
    {
        config = *cfg;
    }
    
    Serial.println("[MQTT] ========================================");
    Serial.println("[MQTT] Initializing MQTT Controller...");
    
    if (!config.enabled)
    {
        Serial.println("[MQTT] MQTT disabled in configuration - aborting initialization");
        Serial.println("[MQTT] ========================================");
        return false;
    }
    
    // Check WiFi connection
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[MQTT] ERROR: WiFi not connected, cannot initialize MQTT");
        Serial.printf("[MQTT] WiFi status: %d\n", WiFi.status());
        Serial.println("[MQTT] ========================================");
        return false;
    }
    
    Serial.printf("[MQTT] WiFi connected: %s\n", WiFi.localIP().toString().c_str());
    
    // Build topic strings
    buildTopics();
    
    // Create command queue
    mqttCommandQueue = xQueueCreate(MQTT_QUEUE_SIZE, sizeof(MQTTCommand));
    if (mqttCommandQueue == nullptr)
    {
        Serial.println("[MQTT] ERROR: Failed to create command queue");
        Serial.println("[MQTT] ========================================");
        return false;
    }
    Serial.printf("[MQTT] Command queue created (size: %zu)\n", MQTT_QUEUE_SIZE);
    
    // Setup MQTT client callbacks
    Serial.println("[MQTT] Registering callbacks...");
    mqttClient.onConnect(onMqttConnect);
    mqttClient.onDisconnect(onMqttDisconnect);
    mqttClient.onMessage(onMqttMessage);
    mqttClient.onPublish(onMqttPublish);
    
    // Configure MQTT client
    Serial.println("[MQTT] Configuring client...");
    mqttClient.setServer(config.broker, config.port);
    mqttClient.setKeepAlive(config.keepalive);
    Serial.printf("[MQTT] Server: %s:%u\n", config.broker, config.port);
    Serial.printf("[MQTT] Keepalive: %u seconds\n", config.keepalive);
    
    if (strlen(config.username) > 0)
    {
        mqttClient.setCredentials(config.username, config.password);
        Serial.printf("[MQTT] Authentication: Username='%s' (password set)\n", config.username);
    }
    else
    {
        Serial.println("[MQTT] Authentication: None (anonymous)");
    }
    
    // Set LWT (Last Will and Testament)
    mqttClient.setWill(onlineTopic, config.qosStatus, true, "offline", 7);
    Serial.printf("[MQTT] LWT configured: %s (QoS %u, retained)\n", onlineTopic, config.qosStatus);
    
    // Connect to broker
    Serial.println("[MQTT] ========================================");
    Serial.print("[MQTT] Attempting connection to broker: ");
    Serial.print(config.broker);
    Serial.print(":");
    Serial.println(config.port);
    Serial.println("[MQTT] Waiting for connection callback...");
    
    mqttClient.connect();
    
    // Create FreeRTOS task for command processing
    xTaskCreatePinnedToCore(
        mqttTaskWrapper,
        "MQTTController",
        4096, // Stack size
        this,
        2, // Priority (medium)
        &mqttTaskHandle,
        0 // Core 0
    );
    
    if (mqttTaskHandle == nullptr)
    {
        Serial.println("[MQTT] ERROR: Failed to create MQTT task");
        Serial.println("[MQTT] ========================================");
        return false;
    }
    
    Serial.printf("[MQTT] MQTT task created (handle: %p, stack: 4096 bytes, priority: 2, core: 0)\n", mqttTaskHandle);
    Serial.println("[MQTT] Controller initialization complete");
    Serial.println("[MQTT] Note: Connection status will be reported in connect/disconnect callbacks");
    Serial.println("[MQTT] ========================================");
    return true;
}

void MQTTController::update()
{
    // Connection management is handled by AsyncMqttClient
    // Status publishing is handled by the task
    // This method can be used for additional periodic updates if needed
}

bool MQTTController::isConnected() const
{
    return mqttClient.connected();
}

void MQTTController::setConfig(const MQTTConfig& cfg)
{
    config = cfg;
    buildTopics();
    
    // Save configuration to ConfigurationManager if available
    if (configManager != nullptr)
    {
        configManager->setMqttEnabled(config.enabled);
        configManager->setMqttBroker(config.broker);
        configManager->setMqttPort(config.port);
        configManager->setMqttUsername(config.username);
        configManager->setMqttPassword(config.password);
        configManager->setMqttDeviceId(config.deviceId);
        configManager->setMqttBaseTopic(config.baseTopic);
        configManager->setMqttQosCommands(config.qosCommands);
        configManager->setMqttQosStatus(config.qosStatus);
        configManager->setMqttKeepalive(config.keepalive);
        configManager->save();
    }
    
    // Reconnect if currently connected
    if (mqttClient.connected())
    {
        mqttClient.disconnect();
        mqttClient.setServer(config.broker, config.port);
        mqttClient.setKeepAlive(config.keepalive);
        
        if (strlen(config.username) > 0)
        {
            mqttClient.setCredentials(config.username, config.password);
        }
        
        mqttClient.setWill(onlineTopic, config.qosStatus, true, "offline", 7);
        mqttClient.connect();
    }
}

bool MQTTController::restart()
{
    if (!config.enabled)
    {
        Serial.println("[MQTT] Cannot restart - MQTT disabled in configuration");
        // If task exists but MQTT is disabled, disconnect
        if (mqttTaskHandle != nullptr && mqttClient.connected())
        {
            mqttClient.disconnect();
        }
        return false;
    }
    
    if (WiFi.status() != WL_CONNECTED)
    {
        Serial.println("[MQTT] Cannot restart - WiFi not connected");
        return false;
    }
    
    // If MQTT was never initialized (begin() never called successfully), we can't restart
    // User needs to reboot for initial setup
    if (mqttTaskHandle == nullptr)
    {
        Serial.println("[MQTT] Cannot restart - MQTT was not previously initialized. Please reboot device.");
        return false;
    }
    
    Serial.println("[MQTT] ========================================");
    Serial.println("[MQTT] Restarting MQTT connection...");
    
    // Disconnect if currently connected
    if (mqttClient.connected())
    {
        Serial.println("[MQTT] Disconnecting existing connection...");
        mqttClient.disconnect();
        // Wait a bit for disconnect to complete
        delay(500);
        Serial.println("[MQTT] Disconnect complete");
    }
    else
    {
        Serial.println("[MQTT] No existing connection to disconnect");
    }
    
    // Rebuild topics in case config changed
    Serial.println("[MQTT] Rebuilding topics...");
    buildTopics();
    
    // Reconfigure client
    Serial.println("[MQTT] Reconfiguring client...");
    mqttClient.setServer(config.broker, config.port);
    mqttClient.setKeepAlive(config.keepalive);
    Serial.printf("[MQTT] Server: %s:%u\n", config.broker, config.port);
    Serial.printf("[MQTT] Keepalive: %u seconds\n", config.keepalive);
    
    if (strlen(config.username) > 0)
    {
        mqttClient.setCredentials(config.username, config.password);
        Serial.printf("[MQTT] Authentication: Username='%s' (password set)\n", config.username);
    }
    else
    {
        mqttClient.setCredentials("", "");
        Serial.println("[MQTT] Authentication: None (anonymous)");
    }
    
    mqttClient.setWill(onlineTopic, config.qosStatus, true, "offline", 7);
    Serial.printf("[MQTT] LWT configured: %s (QoS %u, retained)\n", onlineTopic, config.qosStatus);
    
    // Connect
    Serial.println("[MQTT] ========================================");
    Serial.print("[MQTT] Attempting connection to broker: ");
    Serial.print(config.broker);
    Serial.print(":");
    Serial.println(config.port);
    Serial.println("[MQTT] Waiting for connection callback...");
    
    mqttClient.connect();
    
    return true;
}
