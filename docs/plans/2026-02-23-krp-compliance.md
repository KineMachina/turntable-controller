# KRP v1.0 Compliance Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Refactor MQTTController to speak KRP v1.0 natively — birth sequence, KRP topic structure, KRP command/response/status schemas — while keeping all existing motor control infrastructure untouched.

**Architecture:** In-place refactor of MQTTController (topic builder, connect handler, command dispatch, response/status publishers). StepperMotorController, MotorCommandQueue, HTTPServerController, ConfigurationManager, serial — all unchanged. Python client updated in parallel.

**Tech Stack:** ESP32-S3 C++ (PlatformIO), AsyncMqttClient, ArduinoJson, FreeRTOS. Python (paho-mqtt).

**Design doc:** `docs/plans/2026-02-23-krp-compliance-design.md`

---

### Task 1: Update topic builder and config struct

**Files:**
- Modify: `src/MQTTController.h`
- Modify: `src/MQTTController.cpp`

**Step 1: Update MQTTConfig struct**

In `src/MQTTController.h`, change `MQTTConfig`:
- Remove `baseTopic` field (KRP prefix is always `"krp"`)
- Change `deviceId` default to `"turntable-001"`
- Add `deviceName` field (default `"Turntable A"`)

```cpp
struct MQTTConfig {
    bool enabled = false;
    const char* broker = "mqtt.broker.local";
    uint16_t port = 1883;
    const char* username = "";
    const char* password = "";
    const char* deviceId = "turntable-001";
    const char* deviceName = "Turntable A";
    uint8_t qosCommands = 1;
    uint8_t qosStatus = 0;
    uint16_t keepalive = 60;
};
```

**Step 2: Add new topic buffers and birth method declarations**

In `src/MQTTController.h`, replace the topic buffer declarations and add birth methods:

```cpp
    // Topic buffers
    char stateTopic[128];       // krp/{deviceId}/$state
    char nameTopic[128];        // krp/{deviceId}/$name
    char capabilitiesTopic[128];// krp/{deviceId}/$capabilities
    char commandTopic[128];     // krp/{deviceId}/command
    char responseTopic[128];    // krp/{deviceId}/response
    char statusTopic[128];      // krp/{deviceId}/status
```

Remove `statusTopicPrefix` and `onlineTopic` buffers.

Add to private methods:

```cpp
    void publishBirthMessages();
```

**Step 3: Rewrite buildTopics()**

In `src/MQTTController.cpp`, replace `buildTopics()`:

```cpp
void MQTTController::buildTopics()
{
    snprintf(stateTopic, sizeof(stateTopic), "krp/%s/$state", config.deviceId);
    snprintf(nameTopic, sizeof(nameTopic), "krp/%s/$name", config.deviceId);
    snprintf(capabilitiesTopic, sizeof(capabilitiesTopic), "krp/%s/$capabilities", config.deviceId);
    snprintf(commandTopic, sizeof(commandTopic), "krp/%s/command", config.deviceId);
    snprintf(responseTopic, sizeof(responseTopic), "krp/%s/response", config.deviceId);
    snprintf(statusTopic, sizeof(statusTopic), "krp/%s/status", config.deviceId);

    Serial.println("[MQTT] KRP topics configured:");
    Serial.printf("  State: %s\n", stateTopic);
    Serial.printf("  Name: %s\n", nameTopic);
    Serial.printf("  Capabilities: %s\n", capabilitiesTopic);
    Serial.printf("  Command: %s\n", commandTopic);
    Serial.printf("  Response: %s\n", responseTopic);
    Serial.printf("  Status: %s\n", statusTopic);
}
```

**Step 4: Update all references to old topic names**

In `src/MQTTController.cpp`:
- `statusTopicPrefix` → `statusTopic` (in `publishStatus()`)
- `onlineTopic` → `stateTopic` (in `onMqttConnect()`, `begin()`, `setConfig()`, `restart()`)

**Step 5: Update begin() LWT**

Change the LWT from `onlineTopic` to `stateTopic` with payload `"offline"`:

```cpp
mqttClient.setWill(stateTopic, 1, true, "offline", 7);
```

Note QoS 1 for LWT (KRP spec recommends QoS 1 for `$state`).

**Step 6: Commit**

```bash
git add src/MQTTController.h src/MQTTController.cpp
git commit -m "refactor: update MQTT topics to KRP v1.0 structure"
```

---

### Task 2: Implement birth sequence

**Files:**
- Modify: `src/MQTTController.cpp`

**Step 1: Implement publishBirthMessages()**

Add after `buildTopics()` in `src/MQTTController.cpp`:

```cpp
void MQTTController::publishBirthMessages()
{
    if (!mqttClient.connected()) return;

    // 1. $state → "online"
    mqttClient.publish(stateTopic, 1, true, "online", 6);
    Serial.printf("[MQTT] Published $state: online -> %s\n", stateTopic);

    // 2. $name
    mqttClient.publish(nameTopic, 1, true, config.deviceName, strlen(config.deviceName));
    Serial.printf("[MQTT] Published $name: %s -> %s\n", config.deviceName, nameTopic);

    // 3. $capabilities
    JsonDocument doc;
    doc["device_id"] = config.deviceId;
    doc["device_type"] = "turntable";
    doc["name"] = config.deviceName;
    doc["platform"] = "esp32s3";
    doc["protocol_version"] = "1.0";

    JsonObject caps = doc["capabilities"].to<JsonObject>();
    JsonObject motion = caps["motion"].to<JsonObject>();
    JsonArray joints = motion["joints"].to<JsonArray>();
    JsonObject turntable = joints.add<JsonObject>();
    turntable["name"] = "turntable";
    turntable["type"] = "stepper";
    turntable["continuous"] = true;
    turntable["home"] = 0;

    JsonArray behaviors = caps["behaviors"].to<JsonArray>();
    // Behavior types
    const char* behaviorNames[] = {
        "scanning", "sleeping", "eating", "alert", "roaring",
        "stalking", "playing", "resting", "hunting", "victory"
    };
    for (const auto& name : behaviorNames) behaviors.add(name);
    // Dance types (exposed as behaviors)
    const char* danceNames[] = {
        "twist", "shake", "spin", "wiggle", "watusi", "peppermint_twist"
    };
    for (const auto& name : danceNames) behaviors.add(name);

    String capsStr;
    serializeJson(doc, capsStr);
    mqttClient.publish(capabilitiesTopic, 1, true, capsStr.c_str(), capsStr.length());
    Serial.printf("[MQTT] Published $capabilities (%u bytes) -> %s\n", capsStr.length(), capabilitiesTopic);
}
```

**Step 2: Update onMqttConnect() to call birth sequence**

Replace the body of `onMqttConnect()` after the connection log block:

```cpp
    // Publish KRP birth messages ($state, $name, $capabilities)
    instance->publishBirthMessages();

    // Subscribe to commands
    instance->subscribeToCommands();

    // Transition to "ready" and publish initial status
    instance->mqttClient.publish(instance->stateTopic, 1, true, "ready", 5);
    Serial.printf("[MQTT] Published $state: ready -> %s\n", instance->stateTopic);
    instance->publishStatus(true);
```

Remove the old `onlinePacketId` publish line.

**Step 3: Commit**

```bash
git add src/MQTTController.cpp
git commit -m "feat: add KRP birth sequence ($state, $name, $capabilities)"
```

---

### Task 3: Rewrite command dispatch for KRP

**Files:**
- Modify: `src/MQTTController.cpp`

**Step 1: Add KRP `move` command handler**

Add a new method `handleMove()` that dispatches to heading or position based on fields:

```cpp
void MQTTController::handleMove(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    if (error)
    {
        publishResponse("move", false, "Invalid JSON");
        return;
    }

    // KRP move requires "joint" field
    if (!doc["joint"].is<const char*>() && !doc["joint"].is<String>())
    {
        publishResponse("move", false, "Missing 'joint' field");
        return;
    }

    String joint = doc["joint"].as<String>();
    if (joint != "turntable")
    {
        // Silently ignore commands for joints we don't have (KRP spec)
        return;
    }

    // Dispatch based on heading vs position (angle is alias for position on stepper)
    if (doc["heading"].is<float>() || doc["heading"].is<int>())
    {
        handleHeading(payload, len);
    }
    else if (doc["position"].is<float>() || doc["position"].is<int>() ||
             doc["angle"].is<float>() || doc["angle"].is<int>())
    {
        handlePosition(payload, len);
    }
    else
    {
        publishResponse("move", false, "Missing 'heading' or 'position' field");
    }
}
```

Declare `void handleMove(const char* payload, size_t len);` in `MQTTController.h` private section.

**Step 2: Rewrite handleCommand() dispatch**

Replace the `handleCommand()` method's if-else chain. Add `move` at the top, merge `dance`/`stopdance`/`stopbehavior` into unified `behavior` handling:

```cpp
void MQTTController::handleCommand(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);

    if (error)
    {
        Serial.printf("[MQTT] ERROR: Invalid JSON: %s\n", error.c_str());
        publishResponse("unknown", false, "Invalid JSON");
        return;
    }

    if (!doc["command"].is<const char*>() && !doc["command"].is<String>())
    {
        Serial.println("[MQTT] ERROR: Missing 'command' field");
        publishResponse("unknown", false, "Missing 'command' field");
        return;
    }

    String commandType = doc["command"].as<String>();
    commandType.toLowerCase();
    Serial.printf("[MQTT] Handling command: %s\n", commandType.c_str());

    // --- KRP standard commands ---
    if (commandType == "move")
    {
        handleMove(payload, len);
    }
    else if (commandType == "home")
    {
        handleHome(payload, len);
    }
    else if (commandType == "behavior")
    {
        handleBehaviorUnified(payload, len);
    }
    // --- Device-specific extensions ---
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
    // --- Legacy aliases (keep for backwards compat during transition) ---
    else if (commandType == "heading")
    {
        handleHeading(payload, len);
    }
    else if (commandType == "position")
    {
        handlePosition(payload, len);
    }
    else
    {
        Serial.printf("[MQTT] ERROR: Unknown command: %s\n", commandType.c_str());
        publishResponse(commandType.c_str(), false, "Unknown command");
    }
}
```

**Step 3: Add unified behavior handler**

Add `handleBehaviorUnified()` which handles both behaviors and dances via `name` field, plus `stop`:

```cpp
void MQTTController::handleBehaviorUnified(const char* payload, size_t len)
{
    JsonDocument doc;
    DeserializationError error = deserializeJson(doc, payload, len);
    if (error)
    {
        publishResponse("behavior", false, "Invalid JSON");
        return;
    }

    // Stop current behavior/dance
    if (doc["stop"].is<bool>() && doc["stop"].as<bool>())
    {
        bool stoppedBehavior = stepperController->stopBehavior();
        bool stoppedDance = stepperController->stopDance();
        if (stoppedBehavior || stoppedDance)
        {
            publishResponse("behavior", true, nullptr);
        }
        else
        {
            publishResponse("behavior", false, "No behavior in progress");
        }
        publishStatus(true);
        return;
    }

    if (!doc["name"].is<const char*>() && !doc["name"].is<String>())
    {
        publishResponse("behavior", false, "Missing 'name' field");
        return;
    }

    String name = doc["name"].as<String>();
    name.toLowerCase();

    // Try as dance first
    StepperMotorController::DanceType danceType;
    bool isDance = true;
    if (name == "twist") danceType = StepperMotorController::DanceType::TWIST;
    else if (name == "shake") danceType = StepperMotorController::DanceType::SHAKE;
    else if (name == "spin") danceType = StepperMotorController::DanceType::SPIN;
    else if (name == "wiggle") danceType = StepperMotorController::DanceType::WIGGLE;
    else if (name == "watusi") danceType = StepperMotorController::DanceType::WATUSI;
    else if (name == "peppermint_twist" || name == "pepperminttwist")
        danceType = StepperMotorController::DanceType::PEPPERMINT_TWIST;
    else isDance = false;

    if (isDance)
    {
        bool started = stepperController->startDance(danceType);
        if (started)
        {
            publishResponse("behavior", true, nullptr);
            publishStatus(true);
        }
        else
        {
            publishResponse("behavior", false, "Dance failed to start");
        }
        return;
    }

    // Try as behavior
    StepperMotorController::BehaviorType behaviorType;
    if (name == "scanning") behaviorType = StepperMotorController::BehaviorType::SCANNING;
    else if (name == "sleeping") behaviorType = StepperMotorController::BehaviorType::SLEEPING;
    else if (name == "eating") behaviorType = StepperMotorController::BehaviorType::EATING;
    else if (name == "alert") behaviorType = StepperMotorController::BehaviorType::ALERT;
    else if (name == "roaring") behaviorType = StepperMotorController::BehaviorType::ROARING;
    else if (name == "stalking") behaviorType = StepperMotorController::BehaviorType::STALKING;
    else if (name == "playing") behaviorType = StepperMotorController::BehaviorType::PLAYING;
    else if (name == "resting") behaviorType = StepperMotorController::BehaviorType::RESTING;
    else if (name == "hunting") behaviorType = StepperMotorController::BehaviorType::HUNTING;
    else if (name == "victory") behaviorType = StepperMotorController::BehaviorType::VICTORY;
    else
    {
        publishResponse("behavior", false, "Unknown behavior name");
        return;
    }

    bool started = stepperController->startBehavior(behaviorType);
    if (started)
    {
        publishResponse("behavior", true, nullptr);
        publishStatus(true);
    }
    else
    {
        publishResponse("behavior", false, "Behavior failed to start");
    }
}
```

Declare `void handleBehaviorUnified(const char* payload, size_t len);` and `void handleMove(const char* payload, size_t len);` in `MQTTController.h`.

**Step 4: Remove old handleDance(), handleStopDance(), handleBehavior(), handleStopBehavior()**

Delete these four methods from `.cpp` and their declarations from `.h`. The unified handler replaces them.

**Step 5: Commit**

```bash
git add src/MQTTController.h src/MQTTController.cpp
git commit -m "feat: KRP command dispatch (move, behavior unified, legacy aliases)"
```

---

### Task 4: Rewrite response and status publishers

**Files:**
- Modify: `src/MQTTController.cpp`

**Step 1: Rewrite publishResponse() for KRP schema**

```cpp
void MQTTController::publishResponse(const char* command, bool success, const char* message, const char* requestId)
{
    if (!mqttClient.connected()) return;

    JsonDocument doc;
    doc["status"] = success ? "ok" : "error";
    doc["command"] = command;
    if (requestId != nullptr && requestId[0] != '\0')
    {
        doc["request_id"] = requestId;
    }
    if (!success && message != nullptr)
    {
        doc["message"] = message;
    }
    doc["timestamp"] = millis();

    String responseStr;
    serializeJson(doc, responseStr);
    mqttClient.publish(responseTopic, config.qosCommands, false, responseStr.c_str(), responseStr.length());
}
```

Update the declaration in `.h`:

```cpp
void publishResponse(const char* command, bool success, const char* message = nullptr, const char* requestId = nullptr);
```

**Step 2: Rewrite publishMoveCompleteResponse() for KRP schema**

```cpp
void MQTTController::publishMoveCompleteResponse(const char* commandType, const char* requestId)
{
    if (!mqttClient.connected()) return;

    JsonDocument doc;
    doc["status"] = "ok";
    doc["command"] = commandType;
    doc["event"] = "complete";
    if (requestId != nullptr && requestId[0] != '\0')
    {
        doc["request_id"] = requestId;
    }
    doc["timestamp"] = millis();

    String responseStr;
    serializeJson(doc, responseStr);
    mqttClient.publish(responseTopic, config.qosCommands, false, responseStr.c_str(), responseStr.length());
}
```

**Step 3: Update all publishResponse() call sites**

Every handler currently calls `publishResponse("cmd", true/false, "message", "error")`. The new signature is `publishResponse("cmd", success, message, requestId)`.

For each handler:
- Success calls: change to `publishResponse("cmd", true, nullptr)` (no message on success per KRP)
- Error calls: change to `publishResponse("cmd", false, "error message")`
- Where a handler has access to `request_id` from the parsed JSON, pass it as the 4th arg

Example for `handleHeading()` — after the existing JSON parse, extract request_id:

```cpp
    const char* requestId = nullptr;
    char ridBuf[64] = {0};
    if (doc["request_id"].is<const char*>() || doc["request_id"].is<String>())
    {
        String rid = doc["request_id"].as<String>();
        strncpy(ridBuf, rid.c_str(), sizeof(ridBuf) - 1);
        requestId = ridBuf;
    }
```

Then pass `requestId` to all `publishResponse()` calls in that handler. Apply this pattern to: `handleHeading`, `handlePosition`, `handleMove`, `handleHome`, `handleBehaviorUnified`. The device-specific handlers (enable, speed, etc.) can omit request_id for now.

**Step 4: Rewrite publishStatus() for KRP schema**

```cpp
void MQTTController::publishStatus(bool force)
{
    if (!mqttClient.connected() || stepperController == nullptr) return;
    if (!force && !hasStateChanged()) return;

    updateState();

    JsonDocument doc;
    JsonObject joints = doc["joints"].to<JsonObject>();
    joints["turntable"] = stepperController->getStepperPositionDegrees();
    doc["uptime_ms"] = millis();
    doc["timestamp"] = millis();

    String statusStr;
    serializeJson(doc, statusStr);
    mqttClient.publish(statusTopic, config.qosStatus, true, statusStr.c_str(), statusStr.length());
}
```

**Step 5: Commit**

```bash
git add src/MQTTController.h src/MQTTController.cpp
git commit -m "feat: KRP response and status schemas"
```

---

### Task 5: Update setConfig() and restart()

**Files:**
- Modify: `src/MQTTController.cpp`

**Step 1: Update setConfig()**

Remove `configManager->setMqttBaseTopic()` call (baseTopic removed from config). Add `configManager->setMqttDeviceName()` if ConfigurationManager supports it, otherwise skip. Update LWT to use `stateTopic`:

```cpp
mqttClient.setWill(stateTopic, 1, true, "offline", 7);
```

**Step 2: Update restart()**

Same LWT change. Also publish `"offline"` to `stateTopic` before disconnect (graceful shutdown per KRP):

```cpp
if (mqttClient.connected())
{
    // Graceful KRP shutdown
    mqttClient.publish(stateTopic, 1, true, "offline", 7);
    Serial.println("[MQTT] Published $state: offline (graceful shutdown)");
    mqttClient.disconnect();
    delay(500);
}
```

**Step 3: Commit**

```bash
git add src/MQTTController.cpp
git commit -m "refactor: update setConfig/restart for KRP topics"
```

---

### Task 6: Update native tests

**Files:**
- Modify: `test/native/test_mqtt_command_dispatch/test_main.cpp`
- Modify: `test/native/test_mqtt_param_validation/test_main.cpp`
- Modify: `test/native/test_mqtt_state_tracking/test_main.cpp`

**Step 1: Update setUp() in all three test files**

Replace:
```cpp
    ctrl.config.baseTopic = "test";
    ctrl.config.deviceId = "t1";
```

With:
```cpp
    ctrl.config.deviceId = "t1";
```

(baseTopic removed from config; topics are now `krp/t1/...`)

**Step 2: Update response success/error checks in all test files**

Replace `"status":"success"` with `"status":"ok"` in `lastResponseSuccess()`:

```cpp
static bool lastResponseSuccess() {
    std::string payload = lastResponsePayload();
    return payload.find("\"status\":\"ok\"") != std::string::npos;
}
```

`lastResponseError()` stays the same (`"status":"error"`).

**Step 3: Update command dispatch tests**

In `test_mqtt_command_dispatch/test_main.cpp`:

Replace `test_dispatch_heading`:
```cpp
void test_dispatch_heading(void) {
    sendCommand(R"({"command":"move","joint":"turntable","heading":180.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("moveToHeadingDegrees"));
}
```

Replace `test_dispatch_position`:
```cpp
void test_dispatch_position(void) {
    sendCommand(R"({"command":"move","joint":"turntable","position":90.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_EQUAL(1, (int)mockQueue.getCommands().size());
    TEST_ASSERT_EQUAL(MotorCommandType::MOVE_TO, mockQueue.getCommands()[0].type);
}
```

Replace `test_dispatch_dance`:
```cpp
void test_dispatch_dance(void) {
    sendCommand(R"({"command":"behavior","name":"twist"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("startDance"));
}
```

Replace `test_dispatch_stopdance`:
```cpp
void test_dispatch_stopdance(void) {
    sendCommand(R"({"command":"behavior","stop":true})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    // stopBehavior or stopDance called
}
```

Replace `test_dispatch_behavior`:
```cpp
void test_dispatch_behavior(void) {
    sendCommand(R"({"command":"behavior","name":"scanning"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("startBehavior"));
}
```

Replace `test_dispatch_stopbehavior`:
```cpp
void test_dispatch_stopbehavior(void) {
    sendCommand(R"({"command":"behavior","stop":true})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
}
```

Add new test for `move` with missing joint:
```cpp
void test_dispatch_move_missing_joint(void) {
    sendCommand(R"({"command":"move","heading":90.0})");
    TEST_ASSERT_TRUE(lastResponseError());
}
```

Add test for `move` with unknown joint (silently ignored per KRP):
```cpp
void test_dispatch_move_unknown_joint(void) {
    ctrl.mqttClient._clearPublishes();
    sendCommand(R"({"command":"move","joint":"arm","heading":90.0})");
    // Should be silently ignored - no response published
    auto& pubs = ctrl.mqttClient._getPublishes();
    bool hasResponse = false;
    for (const auto& p : pubs) {
        if (p.topic.find("/response") != std::string::npos) hasResponse = true;
    }
    TEST_ASSERT_FALSE(hasResponse);
}
```

Add legacy alias tests:
```cpp
void test_dispatch_legacy_heading(void) {
    sendCommand(R"({"command":"heading","heading":180.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(mockStepper.hasCall("moveToHeadingDegrees"));
}

void test_dispatch_legacy_position(void) {
    sendCommand(R"({"command":"position","position":90.0})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
}
```

Update `main()` to register new tests and remove duplicates from old dispatch names.

**Step 4: Update param validation tests**

In `test_mqtt_param_validation/test_main.cpp`:

Replace dance tests with unified behavior tests:
```cpp
void test_behavior_missing_name(void) {
    sendCommand(R"({"command":"behavior"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_behavior_invalid_name(void) {
    sendCommand(R"({"command":"behavior","name":"moonwalk"})");
    TEST_ASSERT_TRUE(lastResponseError());
}

void test_behavior_all_dance_types(void) {
    const char* types[] = {"twist", "shake", "spin", "wiggle", "watusi", "peppermint_twist"};
    for (auto& dt : types) {
        setUp();
        char json[128];
        snprintf(json, sizeof(json), R"({"command":"behavior","name":"%s"})", dt);
        sendCommand(json);
        TEST_ASSERT_TRUE_MESSAGE(lastResponseSuccess(), dt);
    }
}

void test_behavior_all_behavior_types(void) {
    const char* types[] = {"scanning", "sleeping", "eating", "alert", "roaring",
                           "stalking", "playing", "resting", "hunting", "victory"};
    for (auto& bt : types) {
        setUp();
        char json[128];
        snprintf(json, sizeof(json), R"({"command":"behavior","name":"%s"})", bt);
        sendCommand(json);
        TEST_ASSERT_TRUE_MESSAGE(lastResponseSuccess(), bt);
    }
}

void test_behavior_stop(void) {
    sendCommand(R"({"command":"behavior","stop":true})");
    // OK even if nothing running (stopBehavior returns false, stopDance returns false)
    // Response depends on implementation - just verify no crash
}
```

Update request_id tests to use `move` command:
```cpp
void test_move_heading_request_id_stored(void) {
    sendCommand(R"({"command":"move","joint":"turntable","heading":270.0,"request_id":"xyz-789"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(ctrl.pendingMoveComplete);
    TEST_ASSERT_EQUAL_STRING("xyz-789", ctrl.pendingMoveRequestId);
}

void test_move_position_request_id_stored(void) {
    sendCommand(R"({"command":"move","joint":"turntable","position":45.0,"request_id":"abc-123"})");
    TEST_ASSERT_TRUE(lastResponseSuccess());
    TEST_ASSERT_TRUE(ctrl.pendingMoveComplete);
    TEST_ASSERT_EQUAL_STRING("abc-123", ctrl.pendingMoveRequestId);
}
```

**Step 5: Update state tracking tests**

In `test_mqtt_state_tracking/test_main.cpp`, update move-complete tests to use `move` command:

```cpp
void test_stopmove_clears_pending_move(void) {
    sendCommand(R"({"command":"move","joint":"turntable","position":90.0,"request_id":"r1"})");
    TEST_ASSERT_TRUE(ctrl.pendingMoveComplete);
    ctrl.mqttClient._clearPublishes();
    sendCommand(R"({"command":"stopmove"})");
    TEST_ASSERT_FALSE(ctrl.pendingMoveComplete);
}

void test_forcestop_clears_pending_move(void) {
    sendCommand(R"({"command":"move","joint":"turntable","heading":180.0,"request_id":"r2"})");
    TEST_ASSERT_TRUE(ctrl.pendingMoveComplete);
    ctrl.mqttClient._clearPublishes();
    sendCommand(R"({"command":"forcestop"})");
    TEST_ASSERT_FALSE(ctrl.pendingMoveComplete);
}
```

**Step 6: Run tests**

```bash
pio test -e native
```

Expected: all tests pass.

**Step 7: Commit**

```bash
git add test/
git commit -m "test: update native tests for KRP command/response format"
```

---

### Task 7: Update Python motor client

**Files:**
- Modify: `kinemachina-arena/kinemachina_core/clients/mqtt_motor.py`

**Step 1: Update constructor**

Change default `base_topic` to `"krp"` and `device_id` to `"turntable-001"`. Remove `topic_status_online` (replaced by `$state` topic). Add `$state` subscription:

```python
def __init__(self, broker: str, port: int = 1883, base_topic: str = "krp",
             device_id: str = "turntable-001", username: str = None, password: str = None,
             status_callback: Opt[Callable[[dict], None]] = None, qos_commands: int = 1):
```

Update topic construction:
```python
        self.topic_command = f"{base_topic}/{device_id}/command"
        self.topic_response = f"{base_topic}/{device_id}/response"
        self.topic_status = f"{base_topic}/{device_id}/status"
        self.topic_state = f"{base_topic}/{device_id}/$state"
```

Remove `self.topic_status_online`.

**Step 2: Update _on_connect() subscriptions**

Replace `topic_status_online` subscription with `topic_state`:

```python
            client.subscribe(self.topic_state, qos=1)
```

**Step 3: Update move_to_heading() command format**

```python
    def move_to_heading(self, heading: float, wait_response: bool = False,
                        wait_for_complete: bool = False) -> Opt[dict]:
        payload = {"command": "move", "joint": "turntable", "heading": float(heading)}
        wait = wait_response or wait_for_complete
        result = self._send_request(
            payload, wait_response=wait, wait_for_move_complete=wait_for_complete
        )
        return result if wait else (result is not None)
```

**Step 4: Update move_to_position() command format**

```python
    def move_to_position(self, position: float, wait_response: bool = False) -> Opt[dict]:
        payload = {"command": "move", "joint": "turntable", "position": float(position)}
        result = self._send_request(payload, wait_response=wait_response)
        return result if wait_response else (result is not None)
```

**Step 5: Update home() command format**

```python
    def home(self, wait_response: bool = False) -> Opt[dict]:
        payload = {"command": "home"}
        result = self._send_request(payload, wait_response=wait_response)
        return result if wait_response else (result is not None)
```

**Step 6: Merge start_dance/stop_dance into behavior interface**

Replace `start_dance()` and `stop_dance()` with behavior-based versions:

```python
    def start_dance(self, dance_type: str) -> bool:
        """Start a dance sequence (exposed as KRP behavior)."""
        payload = {"command": "behavior", "name": str(dance_type)}
        result = self._send_request(payload, wait_response=False)
        return result is not None

    def stop_dance(self) -> bool:
        """Stop the currently running dance/behavior."""
        result = self._send_request({"command": "behavior", "stop": True}, wait_response=False)
        return result is not None
```

Similarly update `start_behavior()` and `stop_behavior()`:

```python
    def start_behavior(self, behavior_type: str) -> bool:
        payload = {"command": "behavior", "name": str(behavior_type)}
        result = self._send_request(payload, wait_response=False)
        return result is not None

    def stop_behavior(self) -> bool:
        result = self._send_request({"command": "behavior", "stop": True}, wait_response=False)
        return result is not None
```

**Step 7: Update _on_message() response parsing**

In the response handler, update the success check:

```python
                    msg_text = response_data.get("message") or ""
                    evt = response_data.get("event") or ""
                    if msg_text == "Move complete" or evt == "complete":
```

This already works — the `evt == "complete"` check matches the new KRP format. No change needed here.

**Step 8: Update send() angle-to-heading conversion**

The `send()` method's heading path should use the new `move` format. Since it calls `self.move_to_heading()` which we already updated, no change needed.

**Step 9: Commit**

```bash
cd /Users/thomasdavidson/projects/kinemachina/kinemachina-arena
git add kinemachina_core/clients/mqtt_motor.py
git commit -m "feat: update mqtt_motor.py for KRP v1.0 topic/command format"
```

---

### Task 8: Update MQTT_API.md

**Files:**
- Modify: `MQTT_API.md`

**Step 1: Rewrite MQTT_API.md**

Replace the entire file with KRP v1.0 documentation covering:
- Topic structure (`krp/turntable-001/...`)
- Birth messages (`$state`, `$name`, `$capabilities`)
- KRP commands (`move`, `home`, `behavior`)
- Device-specific extensions (`enable`, `speed`, `runForward`, etc.)
- Response schema (`status: "ok"/"error"`, `request_id`, `event: "complete"`)
- Status schema (`joints`, `uptime_ms`, `timestamp`)
- Legacy aliases (`heading`, `position` still accepted)

**Step 2: Commit**

```bash
git add MQTT_API.md
git commit -m "docs: rewrite MQTT_API.md for KRP v1.0"
```

---

### Task 9: Final verification

**Step 1: Run native tests**

```bash
cd /Users/thomasdavidson/projects/kinemachina/turntable-controller
pio test -e native
```

Expected: all tests pass.

**Step 2: Run PlatformIO build**

```bash
pio run
```

Expected: compiles without errors.

**Step 3: Run Python tests (if any)**

```bash
cd /Users/thomasdavidson/projects/kinemachina/kinemachina-arena
pytest tests/ -x -q 2>&1 | head -30
```

Check for any failures related to mqtt_motor imports/usage.
