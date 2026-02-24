# Design: KRP v1.0 Compliance for Turntable Controller

**Date:** 2026-02-23
**Platform:** ESP32-S3, C++, PlatformIO
**Approach:** Refactor MQTTController in-place (Approach A)

## Summary

Redesign the turntable controller's MQTT interface to comply with KRP v1.0. The firmware keeps its existing C++ infrastructure (FreeRTOS tasks, StepperMotorController, MotorCommandQueue, HTTP server, serial). Only the MQTT topic structure and message formats change. The arena-side Python client (`mqtt_motor.py`) is updated to match.

## Topic Structure

### Current
```
kinemachina/turntable/turntable_001/command       → all commands
kinemachina/turntable/turntable_001/response      → ack + move-complete
kinemachina/turntable/turntable_001/status         → consolidated motor state
kinemachina/turntable/turntable_001/status/online  → LWT presence
```

### KRP v1.0
```
krp/turntable-001/$state          → "online" / "ready" / "offline" (retained, LWT)
krp/turntable-001/$name           → "Turntable A" (retained)
krp/turntable-001/$capabilities   → JSON manifest (retained)
krp/turntable-001/command         → all commands (single topic, JSON dispatch)
krp/turntable-001/response        → KRP response schema
krp/turntable-001/status          → KRP status schema (retained)
```

Base topic is fixed `"krp"` — no longer configurable. Device ID changes to `turntable-001`.

## Birth Sequence

On MQTT connect, publish three retained messages:

1. **`$state`** → `"online"` (plain string). After init → `"ready"`. LWT → `"offline"`.
2. **`$name`** → `"Turntable A"` (plain string, configurable via NVS).
3. **`$capabilities`** → retained JSON:

```json
{
  "device_id": "turntable-001",
  "device_type": "turntable",
  "name": "Turntable A",
  "platform": "esp32s3",
  "protocol_version": "1.0",
  "capabilities": {
    "motion": {
      "joints": [
        {"name": "turntable", "type": "stepper", "continuous": true, "home": 0}
      ]
    },
    "behaviors": [
      "scanning", "sleeping", "alert", "roaring",
      "stalking", "playing", "resting", "hunting", "victory",
      "twist", "shake", "spin", "wiggle", "watusi", "peppermint_twist"
    ]
  }
}
```

Dances and behaviors are merged into a single `behaviors` list. Internally the firmware maps dance names to `DanceType` and behavior names to `BehaviorType`.

On graceful disconnect: publish `"offline"` to `$state`.

## Command Mapping

### KRP standard commands

| KRP Command | Fields | Maps to |
|-------------|--------|---------|
| `move` | `joint: "turntable"`, `heading: 180` | `handleHeading()` |
| `move` | `joint: "turntable"`, `position: 90.0` | `handlePosition()` |
| `home` | (none) | `handleHome()` |
| `behavior` | `name: "scanning"` | `handleBehavior()` |
| `behavior` | `name: "twist"` | `handleDance()` |
| `behavior` | `stop: true` | `handleStopBehavior()` + `handleStopDance()` |

### Device-specific extensions

| Command | Fields | Notes |
|---------|--------|-------|
| `enable` | `enable: true/false` | Motor enable/disable |
| `speed` | `speed: 1000.0` | Max speed |
| `acceleration` | `accel: 500.0` | Acceleration |
| `speedHz` | `speedHz: 200.0` | Speed in Hz |
| `runForward` | (none) | Continuous rotation |
| `runBackward` | (none) | Continuous rotation |
| `stop` | (none) | Stop movement |
| `forceStop` | (none) | Emergency stop |
| `reset` | (none) | Engine reset |
| `zero` | (none) | Zero position counter |
| `microsteps` | `microsteps: 16` | TMC2209 microstepping |
| `gearRatio` | `ratio: 5.18` | Gear ratio |

Removed: `dance` and `stopDance` as separate commands (absorbed into `behavior`).

## Response Format

Published to `krp/turntable-001/response` (not retained).

```json
{"status": "ok", "command": "move", "request_id": "req_001", "timestamp": 12345}
```

- `"success"` → `"ok"`
- Remove `"executed"` field
- `"request_id"` echoed only when provided
- `"event": "complete"` for async move/behavior completion
- `"message"` only on errors
- Timestamp: `millis()` (no RTC)

## Status Format

Published to `krp/turntable-001/status` (retained). Change-detection + 30s heartbeat.

```json
{
  "joints": {"turntable": 180.0},
  "uptime_ms": 360000,
  "timestamp": 12345
}
```

Detailed motor/TMC2209/wifi diagnostics move to HTTP API only.

## Python Client Update (`mqtt_motor.py`)

- Topics: `krp/{device_id}/command`, `krp/{device_id}/response`, `krp/{device_id}/status`
- `move_to_heading()` → `{"command": "move", "joint": "turntable", "heading": 180}`
- `move_to_position()` → `{"command": "move", "joint": "turntable", "position": 90.0}`
- Response: check `"status": "ok"` instead of `"success"`
- Status: read `status["joints"]["turntable"]` instead of `status["motor"]["positionDegrees"]`
- Constructor: accept `device_id` (default `"turntable-001"`), build topics as `krp/{device_id}/...`

## Files Changed

| File | Change |
|------|--------|
| `turntable-controller/src/MQTTController.h` | New topic buffers, add birth methods, update config struct |
| `turntable-controller/src/MQTTController.cpp` | Rewrite topics, connect handler, command dispatch, status/response format |
| `turntable-controller/MQTT_API.md` | Rewrite for KRP v1.0 |
| `kinemachina-arena/kinemachina_core/clients/mqtt_motor.py` | Update topics, command/response/status formats |
| `kinemachina-arena/tests/test_mqtt_motor.py` | Update test expectations |
| `turntable-controller/test/native/test_mqtt_command_dispatch/test_main.cpp` | Update for KRP command format |

## Not Changed

StepperMotorController, MotorCommandQueue, HTTPServerController, ConfigurationManager, SerialCommandQueue, main.cpp — all untouched.
