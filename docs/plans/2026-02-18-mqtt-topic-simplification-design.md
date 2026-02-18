# MQTT Topic Simplification Design

## Problem

The current MQTT API has 25 topics (19 inbound command topics + 6 outbound status/response topics). This is unnecessarily complex for clients to integrate with and creates overhead on the broker with 19 individual subscriptions.

## Decision Summary

- Clean break: remove all old per-command topics, no backward compatibility
- Single command topic with JSON `"command"` field for dispatch
- Consolidated status topic merging motor, TMC2209, health, and wifi data
- TMC2209 register data always included in status (30s interval accommodates slow UART reads)
- MQTT only: HTTP REST and Serial interfaces unchanged

## New Topic Structure

| Topic | Direction | QoS | Retained | Purpose |
|-------|-----------|-----|----------|---------|
| `{base}/{id}/command` | Inbound | 1 | No | All commands via JSON `"command"` field |
| `{base}/{id}/status` | Outbound | 0 | Yes | Consolidated status (motor + TMC2209 + wifi + health) |
| `{base}/{id}/response` | Outbound | 1 | No | Command acks, errors, move-complete events |
| `{base}/{id}/status/online` | Outbound | 0 | Yes | LWT "online"/"offline" (MQTT protocol requirement) |

Removed: `status/motor`, `status/full`, `status/health`, and all 19 `command/*` topics.

## Command Payload Format

All commands published to `{base}/{id}/command`. The `"command"` field determines the action. Command names are case-insensitive.

### Commands with parameters

```json
{"command": "position", "position": 180.0, "request_id": "abc-123"}
{"command": "heading", "heading": 90.0, "request_id": "abc-123"}
{"command": "enable", "enable": true}
{"command": "speed", "speed": 2000.0}
{"command": "acceleration", "accel": 400.0}
{"command": "microsteps", "microsteps": 8}
{"command": "gearRatio", "ratio": 2.0}
{"command": "speedHz", "speedHz": 1000.0}
{"command": "dance", "danceType": "twist"}
{"command": "behavior", "behaviorType": "scanning"}
```

### Commands without parameters

```json
{"command": "runForward"}
{"command": "runBackward"}
{"command": "stop"}
{"command": "forceStop"}
{"command": "reset"}
{"command": "zero"}
{"command": "home"}
{"command": "stopDance"}
{"command": "stopBehavior"}
```

Notable: `stopMove` renamed to `stop`.

## Consolidated Status Payload

Published on state change + every 30 seconds. Merges previous `status`, `status/motor`, `status/full`, and `status/health`.

```json
{
  "status": "success",
  "timestamp": 1234567890,
  "uptime_ms": 1234567890,
  "free_heap": 245678,
  "motor": {
    "enabled": true,
    "running": false,
    "position": 1000,
    "positionDegrees": 45.5,
    "speedHz": 0.0,
    "microsteps": 8,
    "gearRatio": 2.0,
    "behaviorInProgress": false,
    "danceInProgress": false
  },
  "tmc2209": {
    "rmsCurrent": 800.0,
    "csActual": 16,
    "actualCurrent": 750.0,
    "irun": 16,
    "ihold": 8,
    "enabled": true,
    "spreadCycle": false,
    "pwmAutoscale": true,
    "blankTime": 24
  },
  "wifi": {
    "connected": true,
    "ip": "192.168.1.100"
  }
}
```

## Response Format

Unchanged from current implementation. Same format for command acks, errors, and move-complete events.

## Code Changes

### MQTTController.h

- Replace `commandTopicPrefix` buffer with `commandTopic` (exact topic, no prefix)
- Remove `MQTTCommand::Type` enum (dispatch from JSON, not topic path)
- Remove `topic` field from `MQTTCommand` struct (topic is always the same)
- Keep all 19 handler method signatures unchanged

### MQTTController.cpp

- `buildTopics()`: `commandTopicPrefix` becomes `commandTopic`
- `subscribeToCommands()`: single subscribe instead of 19
- `onMqttMessage()`: no topic parsing; just queue the payload
- `handleCommand()`: parse JSON first, read `doc["command"]`, lowercase, dispatch. Add `stop` as alias for `stopMove`
- `publishStatus()`: merge `publishFullStatus()` into this. Remove `status/motor` publish. Add `uptime_ms` and TMC2209 UART reads with task yields
- Delete `publishFullStatus()` entirely
- `mqttTask()`: remove `status/health` publish, remove `publishFullStatus()` call

### MQTT_API.md

- Full rewrite to reflect new 4-topic structure

### Unchanged

- All 19 handler method internals
- `publishResponse()` and `publishMoveCompleteResponse()`
- `MQTTConfig`, `begin()`, `restart()`, `setConfig()`
- State change detection and move-complete tracking
- FreeRTOS task architecture
