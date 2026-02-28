# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Build Commands

This is a PlatformIO project targeting ESP32-S3 with the Arduino framework.

```bash
pio run                    # Build
pio run -t upload          # Build and flash via USB
pio device monitor         # Serial monitor (115200 baud)
pio run -t clean           # Clean build artifacts
pio run -t clean && pio run  # Full rebuild
```

```bash
pio test -e native              # Run all native (host) tests
pio test -e native -f native/test_mqtt_command_dispatch   # Run one suite
```

Tests use PlatformIO's native platform with Unity. Mock headers in `test/native/mocks/` shadow ESP32 dependencies so tests run on the host without hardware. When adding new features or modifying existing code, write tests. Place test suites in `test/native/test_<name>/test_main.cpp`. Each test file must include mock headers before `MQTTController.h` to set include guards (see existing tests for the pattern).

## Architecture

KineMachina Turntable Controller — ESP32-S3 stepper motor controller with TMC2209 driver and three control interfaces (HTTP REST API, MQTT, Serial). Uses FreeRTOS dual-core task architecture. GitHub: https://github.com/KineMachina/turntable-controller

### Task Layout (main.cpp)

| Task | Core | Priority | Purpose |
|------|------|----------|---------|
| MotorControlTask | 1 | 3 (high) | 100Hz motor control loop, processes command queue |
| HTTPServerTask | 0 | 2 | AsyncWebServer event handling |
| MQTTTask | 0 | 2 | MQTT connection, status publishing |
| SerialReadTask | 0 | 2 | Serial input buffering |
| SerialCommandTask | 0 | 1 | Serial command processing |
| DanceTask | 1 | 1 | Background dance sequences |
| BehaviorTask | 1 | 1 | Background behavior patterns |

Core 1 handles time-critical motor control. Core 0 handles network I/O.

### Command Flow

All three interfaces (HTTP, MQTT, Serial) enqueue commands into `MotorCommandQueue` (FreeRTOS queue, 20 items max). The motor control task on Core 1 dequeues and executes them. Velocity commands (runForward, runBackward, stop) bypass the queue for low latency.

### Source Modules

- **main.cpp** - Entry point, FreeRTOS task creation, pin definitions, initialization sequence
- **StepperMotorController** - Motor control: FastAccelStepper + TMC2209 (UART). Implements position/heading/velocity control modes and dance/behavior animations
- **HTTPServerController** - WiFi management, AsyncWebServer with 20+ REST endpoints, embedded HTML/AJAX web UI
- **MQTTController** - AsyncMqttClient, single command topic with JSON dispatch, consolidated status publishing (change-triggered + periodic 30s), move-complete notifications with request_id echo
- **ConfigurationManager** - Persistent config via ESP32 NVS/Preferences API. Stores WiFi, MQTT, motor, and TMC2209 settings
- **MotorCommandQueue** - FreeRTOS queue wrapper for thread-safe motor commands
- **SerialCommandQueue** - Serial command buffering and processing

### Logging

All diagnostic output uses `ESP_LOGx(TAG, format, ...)`. Never `Serial.print` for logging. The only exception: `Serial.print/println` in main.cpp for direct responses to serial commands (interactive terminal I/O).

**Setup per file:**
```cpp
#include "RuntimeLog.h"
static const char* TAG = "MyTag";
```

**IMPORTANT:** Always include `"RuntimeLog.h"`, NOT `<esp_log.h>`. `RuntimeLog.h` re-defines `ESP_LOGx` to use `log_printf()` directly with a runtime level check against the global `runtimeLogLevel` variable. This decouples project logging from `CORE_DEBUG_LEVEL`, so our code has full runtime control regardless of the compile-time setting. `CORE_DEBUG_LEVEL=2` (WARN) in `platformio.ini` silences third-party library `log_i()`/`log_d()` output at compile time (e.g., AsyncMqttClient publish spam). The `USE_ESP_IDF_LOG` build flag would fix this properly but breaks third-party libraries that use `log_e()` without defining `TAG`.

| Level | Macro | Use for |
|-------|-------|---------|
| ERROR | `ESP_LOGE` | Hardware failures, critical errors, init failures |
| WARN | `ESP_LOGW` | Non-fatal issues, degraded operation, missing optional components |
| INFO | `ESP_LOGI` | Normal events: startup, connections, commands, config changes |
| DEBUG | `ESP_LOGD` | Periodic: heartbeat, reconnects, status publishing, loop restarts |

**TAG registry:** Main, HTTP, MQTT, Stepper, Config, MotorQueue, SerialQueue, OLED

**Runtime control:** `log` serial command sets `runtimeLogLevel`: `log off`, `log error`, `log warn`, `log info` (default), `log debug`.

### Key Hardware Details

- **TMC2209**: UART single-wire bidirectional on PDN_UART (GPIO 17 TX, GPIO 18 RX). Supports StealthChop/SpreadCycle modes, microstepping 1-256
- **Stepper**: Step on GPIO 5, Dir on GPIO 6, Enable on GPIO 4
- **Watchdog**: 120s timeout in platformio.ini to accommodate long dance/behavior sequences

### API Documentation

- REST API: see `REST_API.md`
- MQTT API: see `MQTT_API.md` (single command topic: `{baseTopic}/{deviceId}/command`)
- Pin connections: see `pin-connections.md`
- Product spec: see `PRODUCT_SPEC.md`
