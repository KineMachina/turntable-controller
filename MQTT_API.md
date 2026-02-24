# MQTT API Documentation (KRP v1.0)

## Overview

The turntable controller implements the KineMachina Robot Protocol (KRP) v1.0 over MQTT. It publishes device discovery messages on connect and accepts commands via a single JSON-dispatch topic.

## Topic Structure

All topics use a fixed `krp` prefix:

```
krp/{deviceId}/...
```

**Default Device ID:** `turntable-001`

| Topic | Direction | QoS | Retained | Purpose |
|-------|-----------|-----|----------|---------|
| `krp/{deviceId}/$state` | Outbound | 1 | Yes | Device lifecycle (`online`, `ready`, `offline`) |
| `krp/{deviceId}/$name` | Outbound | 1 | Yes | Human-readable name |
| `krp/{deviceId}/$capabilities` | Outbound | 1 | Yes | JSON capability manifest |
| `krp/{deviceId}/command` | Inbound | 1 | No | All commands via JSON `"command"` field |
| `krp/{deviceId}/response` | Outbound | 1 | No | Command acks, errors, move-complete |
| `krp/{deviceId}/status` | Outbound | 0 | Yes | Device status (change-triggered + 30s heartbeat) |

## Birth Sequence

On MQTT connect, the device publishes three retained messages:

1. **`$state`** → `"online"` (plain string)
2. **`$name`** → `"Turntable A"` (plain string, configurable via NVS)
3. **`$capabilities`** → JSON manifest:

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
      "scanning", "sleeping", "eating", "alert", "roaring",
      "stalking", "playing", "resting", "hunting", "victory",
      "twist", "shake", "spin", "wiggle", "watusi", "peppermint_twist"
    ]
  }
}
```

After subscribing and publishing initial status, `$state` transitions to `"ready"`.

**LWT (Last Will and Testament):** `$state` → `"offline"` on unexpected disconnect.

**Graceful shutdown:** `$state` → `"offline"` published before disconnect.

## Commands

All commands are published to `krp/{deviceId}/command`. Payloads must be valid JSON with a `"command"` field. Command names are **case-insensitive**.

### KRP Standard Commands

#### move

Move to a heading or position on a named joint.

```json
{"command": "move", "joint": "turntable", "heading": 180.0, "request_id": "req_001"}
```

```json
{"command": "move", "joint": "turntable", "position": 90.0}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `joint` | string | Yes | Joint name (must be `"turntable"`) |
| `heading` | float | One of heading/position/angle | Target heading (0-360, shortest path) |
| `position` | float | One of heading/position/angle | Absolute position in degrees |
| `angle` | float | One of heading/position/angle | Alias for `position` |
| `request_id` | string | No | Echoed in response and move-complete |

Unknown joints are silently ignored per KRP spec.

```bash
mosquitto_pub -h broker -t "krp/turntable-001/command" \
  -m '{"command":"move","joint":"turntable","heading":180.0}'
```

#### home

Return to position 0.

```json
{"command": "home"}
```

#### behavior

Start or stop a behavior/dance sequence.

**Start a behavior:**
```json
{"command": "behavior", "name": "scanning"}
```

**Start a dance:**
```json
{"command": "behavior", "name": "twist"}
```

**Stop current behavior/dance:**
```json
{"command": "behavior", "stop": true}
```

| Field | Type | Required | Description |
|-------|------|----------|-------------|
| `name` | string | Yes (unless stop) | Behavior or dance name (case-insensitive) |
| `stop` | bool | No | Set `true` to stop current behavior/dance |

**Available behaviors:** scanning, sleeping, eating, alert, roaring, stalking, playing, resting, hunting, victory

**Available dances:** twist, shake, spin, wiggle, watusi, peppermint_twist (or pepperminttwist)

### Device-Specific Extensions

| Command | Fields | Description |
|---------|--------|-------------|
| `enable` | `enable: true/false` | Enable/disable motor driver |
| `speed` | `speed: 2000.0` | Max speed (steps/sec, > 0) |
| `acceleration` | `accel: 400.0` | Acceleration (steps/sec^2, > 0) |
| `speedHz` | `speedHz: 1000.0` | Speed for velocity mode (Hz, >= 0) |
| `microsteps` | `microsteps: 16` | TMC2209 microstepping (power of 2: 1-256) |
| `gearRatio` | `ratio: 2.0` | Gear ratio (0.1-100.0) |
| `runForward` | (none) | Start continuous forward rotation |
| `runBackward` | (none) | Start continuous backward rotation |
| `stop` | (none) | Stop movement (decelerate). Alias: `stopMove` |
| `forceStop` | (none) | Emergency stop (immediate) |
| `reset` | (none) | Reset stepper engine |
| `zero` | (none) | Set current position as 0 |

### Legacy Aliases

For backward compatibility, `heading` and `position` are accepted as top-level commands:

```json
{"command": "heading", "heading": 180.0}
{"command": "position", "position": 90.0}
```

These bypass the `joint` field requirement and dispatch directly.

## Response Format

Published to `krp/{deviceId}/response` (not retained).

**Success:**
```json
{"status": "ok", "command": "move", "timestamp": 12345}
```

**Success with request_id:**
```json
{"status": "ok", "command": "move", "request_id": "req_001", "timestamp": 12345}
```

**Error:**
```json
{"status": "error", "command": "move", "message": "Missing 'joint' field", "timestamp": 12345}
```

| Field | Type | Present | Description |
|-------|------|---------|-------------|
| `status` | string | Always | `"ok"` or `"error"` |
| `command` | string | Always | Command that was processed |
| `request_id` | string | When provided | Echoed from command payload |
| `message` | string | Errors only | Error description |
| `timestamp` | number | Always | `millis()` since boot |

### Move-Complete Response

For `move` (heading/position) commands, a second response is published when the motor finishes:

```json
{"status": "ok", "command": "heading", "event": "complete", "request_id": "req_001", "timestamp": 12350}
```

- `event: "complete"` distinguishes this from the initial ack
- If a new move starts before completion, the pending completion is overwritten
- `stop` or `forceStop` clears the pending completion (no event published)

## Status Format

Published to `krp/{deviceId}/status` (retained). Change-detection + 30-second heartbeat.

```json
{
  "joints": {"turntable": 180.0},
  "uptime_ms": 360000,
  "timestamp": 360000
}
```

| Field | Type | Description |
|-------|------|-------------|
| `joints.turntable` | float | Current position in degrees |
| `uptime_ms` | number | Milliseconds since boot |
| `timestamp` | number | Same as uptime_ms (no RTC) |

Detailed motor/TMC2209/WiFi diagnostics are available via the HTTP REST API only.

## Examples

### CLI Workflow

```bash
# Check device state
mosquitto_sub -h broker -t "krp/turntable-001/$state"

# Subscribe to status and responses
mosquitto_sub -h broker -t "krp/turntable-001/status" &
mosquitto_sub -h broker -t "krp/turntable-001/response" &

# Enable motor
mosquitto_pub -h broker -t "krp/turntable-001/command" \
  -m '{"command":"enable","enable":true}'

# Move to heading
mosquitto_pub -h broker -t "krp/turntable-001/command" \
  -m '{"command":"move","joint":"turntable","heading":180.0,"request_id":"r1"}'

# Start a behavior
mosquitto_pub -h broker -t "krp/turntable-001/command" \
  -m '{"command":"behavior","name":"scanning"}'

# Stop behavior
mosquitto_pub -h broker -t "krp/turntable-001/command" \
  -m '{"command":"behavior","stop":true}'

# Start a dance
mosquitto_pub -h broker -t "krp/turntable-001/command" \
  -m '{"command":"behavior","name":"twist"}'
```

### Python Example

```python
from kinemachina_core.clients.mqtt_motor import MqttMotorCommandSender

motor = MqttMotorCommandSender(broker="rpi-5.local", device_id="turntable-001")

# Move to heading (fire-and-forget)
motor.move_to_heading(180.0)

# Move to heading and wait for completion
result = motor.move_to_heading(90.0, wait_for_complete=True)

# Start/stop behavior
motor.start_behavior("scanning")
motor.stop_behavior()

# Start/stop dance
motor.start_dance("twist")
motor.stop_dance()
```

## Configuration

MQTT settings are configured via the web UI or NVS (ESP32 Preferences):

- **Broker**: `mqtt.broker.local`
- **Port**: `1883`
- **Device ID**: `turntable-001`
- **Device Name**: `Turntable A`
- **Enabled**: `false` (must be enabled via web UI)
