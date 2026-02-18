# MQTT Topic Simplification Implementation Plan

> **For Claude:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Consolidate 25 MQTT topics down to 4 by using a single command topic with JSON dispatch and a unified status topic.

**Architecture:** Replace per-command topic subscriptions with a single `{base}/{id}/command` topic. The JSON payload's `"command"` field drives dispatch. Merge `status`, `status/motor`, `status/full`, and `status/health` into one `status` topic. Keep `response` and `status/online` (LWT) unchanged.

**Tech Stack:** ESP32-S3, Arduino framework, PlatformIO, AsyncMqttClient, ArduinoJson, FreeRTOS

**Design doc:** `docs/plans/2026-02-18-mqtt-topic-simplification-design.md`

---

### Task 1: Simplify MQTTCommand struct in header

**Files:**
- Modify: `src/MQTTController.h:35-61` (MQTTCommand struct)
- Modify: `src/MQTTController.h:79` (commandTopicPrefix -> commandTopic)

**Step 1: Edit MQTTCommand struct**

Remove the `Type` enum and `topic` field. The struct only needs payload data now.

Replace lines 35-61 of `src/MQTTController.h`:
```cpp
/**
 * MQTT Command Structure
 * Used for queuing commands from MQTT callbacks
 */
struct MQTTCommand {
    char payload[512];
    size_t payloadLen;
};
```

**Step 2: Rename commandTopicPrefix to commandTopic**

In `src/MQTTController.h` line 79, change:
```cpp
    char commandTopicPrefix[128];
```
to:
```cpp
    char commandTopic[128];
```

**Step 3: Remove publishFullStatus declaration**

In `src/MQTTController.h` line 118, remove:
```cpp
    void publishFullStatus();
```

**Step 4: Update handleCommand signature**

In `src/MQTTController.h` line 125, change:
```cpp
    void handleCommand(MQTTCommand& cmd);
```
to:
```cpp
    void handleCommand(const char* payload, size_t len);
```

**Step 5: Build to verify header compiles**

Run: `pio run`
Expected: Build errors in .cpp (expected — we haven't updated it yet). Verify no header syntax errors.

**Step 6: Commit**

```bash
git add src/MQTTController.h
git commit -m "refactor(mqtt): simplify MQTTCommand struct and header for single topic"
```

---

### Task 2: Update buildTopics and subscribeToCommands

**Files:**
- Modify: `src/MQTTController.cpp:48-90` (buildTopics + subscribeToCommands)

**Step 1: Update buildTopics**

Replace `buildTopics()` (lines 48-63) with:
```cpp
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
```

**Step 2: Replace subscribeToCommands with single subscribe**

Replace `subscribeToCommands()` (lines 65-90) with:
```cpp
void MQTTController::subscribeToCommands()
{
    if (!mqttClient.connected())
    {
        return;
    }

    uint16_t packetId = mqttClient.subscribe(commandTopic, config.qosCommands);
    Serial.printf("[MQTT] Subscribed to: %s (QoS %u, packet ID: %u)\n", commandTopic, config.qosCommands, packetId);
}
```

**Step 3: Commit**

```bash
git add src/MQTTController.cpp
git commit -m "refactor(mqtt): single command topic subscription"
```

---

### Task 3: Update handleCommand for JSON-based dispatch

**Files:**
- Modify: `src/MQTTController.cpp:312-428` (handleCommand)

**Step 1: Rewrite handleCommand**

Replace `handleCommand(MQTTCommand& cmd)` (lines 312-428) with:
```cpp
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

    // Route to appropriate handler (payload still contains full JSON for handlers to re-parse)
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
```

Note: `"stop"` is accepted as alias for `"stopmove"`. Both route to `handleStopMove`.

**Step 2: Commit**

```bash
git add src/MQTTController.cpp
git commit -m "refactor(mqtt): JSON-based command dispatch from 'command' field"
```

---

### Task 4: Update onMqttMessage to queue payload only

**Files:**
- Modify: `src/MQTTController.cpp:1118-1182` (onMqttMessage)

**Step 1: Simplify onMqttMessage**

Replace `onMqttMessage` (lines 1118-1182). No longer copies topic into the queue item:
```cpp
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
```

**Step 2: Commit**

```bash
git add src/MQTTController.cpp
git commit -m "refactor(mqtt): simplify message callback to queue payload only"
```

---

### Task 5: Update mqttTask and consolidate publishStatus

**Files:**
- Modify: `src/MQTTController.cpp:92-217` (publishStatus + delete publishFullStatus)
- Modify: `src/MQTTController.cpp:1205-1265` (mqttTask)

**Step 1: Replace publishStatus with consolidated version**

Replace `publishStatus` (lines 92-159) with this version that includes TMC2209 data and removes the `status/motor` sub-publish:
```cpp
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
```

**Step 2: Delete publishFullStatus entirely**

Remove lines 161-217 (`publishFullStatus` method).

**Step 3: Simplify mqttTask**

Replace `mqttTask` (lines 1205-1265) with:
```cpp
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
```

**Step 4: Update onMqttConnect — remove publishFullStatus call**

In `onMqttConnect` (around line 1058-1062), change:
```cpp
    instance->publishStatus(true);
    instance->publishFullStatus();
    Serial.println("[MQTT] Initial status published");
```
to:
```cpp
    instance->publishStatus(true);
    Serial.println("[MQTT] Initial status published");
```

**Step 5: Build to verify**

Run: `pio run`
Expected: Successful build with no errors.

**Step 6: Commit**

```bash
git add src/MQTTController.h src/MQTTController.cpp
git commit -m "refactor(mqtt): consolidate status publishing, remove health/motor/full sub-topics"
```

---

### Task 6: Update MQTT_API.md documentation

**Files:**
- Rewrite: `MQTT_API.md`

**Step 1: Rewrite MQTT_API.md**

Replace the entire file with documentation reflecting the new 4-topic structure. Include:
- Topic structure table (4 topics)
- Command payload format with all 19 commands (JSON examples)
- Consolidated status payload schema
- Response format (unchanged)
- Move-complete response (unchanged)
- mosquitto_pub/sub examples using the new single topic
- Python and Node.js client examples updated for single topic
- Error handling section

Key points to document:
- `"command"` field is required in every payload
- Command names are case-insensitive
- `"stop"` is accepted as alias for `"stopMove"`
- Status now always includes `tmc2209` and `uptime_ms`
- `status/motor`, `status/full`, `status/health` no longer exist

**Step 2: Commit**

```bash
git add MQTT_API.md
git commit -m "docs(mqtt): rewrite MQTT_API.md for simplified 4-topic structure"
```

---

### Task 7: Update CLAUDE.md architecture section

**Files:**
- Modify: `CLAUDE.md`

**Step 1: Update MQTT references in CLAUDE.md**

In the Architecture section, update the MQTTController description to mention simplified topic structure. No need to change the task table or command flow — those are still accurate.

**Step 2: Commit**

```bash
git add CLAUDE.md
git commit -m "docs: update CLAUDE.md MQTT architecture notes"
```

---

### Task 8: Final build verification

**Step 1: Clean build**

Run: `pio run -t clean && pio run`
Expected: Successful build with no warnings related to MQTT changes.

**Step 2: Verify no leftover references**

Search for removed topic patterns:
- `commandTopicPrefix` — should not appear in .h or .cpp
- `publishFullStatus` — should not appear anywhere
- `status/motor` — should not appear in .cpp (ok in docs for "removed" notes)
- `status/health` — should not appear in .cpp
- `status/full` — should not appear in .cpp
- `CMD_POSITION` — should not appear (enum removed)
- `cmd.topic` — should not appear in MQTTController (field removed)
