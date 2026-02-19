# KineMachina Turntable MQTT API Migration Guide

This document describes the breaking changes to the turntable controller's MQTT API. Use it to update any client code that communicates with the turntable over MQTT.

## Summary of Changes

The MQTT API was simplified from a multi-topic command structure to a single-topic JSON dispatch model. The number of topics was reduced from many to 4. Status publishing was consolidated into a single retained topic.

## 1. Topic Structure Changes

### Before (old)

Commands were sent to **per-command topics**:
```
{baseTopic}/{deviceId}/command/{commandName}
```

Status was split across multiple sub-topics:
```
{baseTopic}/{deviceId}/status          # general status
{baseTopic}/{deviceId}/status/motor    # motor-specific status
{baseTopic}/{deviceId}/status/online   # LWT
```

### After (new)

**4 topics total:**

| Topic | Direction | QoS | Retained | Purpose |
|-------|-----------|-----|----------|---------|
| `{baseTopic}/{deviceId}/command` | Inbound | 1 | No | **Single topic for ALL commands** |
| `{baseTopic}/{deviceId}/status` | Outbound | 0 | Yes | Consolidated device status |
| `{baseTopic}/{deviceId}/response` | Outbound | 1 | No | Command acks, errors, move-complete events |
| `{baseTopic}/{deviceId}/status/online` | Outbound | 0 | Yes | LWT online/offline |

Default base topic: `kinemachina/turntable`, default device ID: `turntable_001`.

### What to change

- **Remove all per-command topic subscriptions/publishes** (e.g. `.../command/position`, `.../command/heading`, `.../command/enable`)
- **Publish all commands to a single topic**: `{baseTopic}/{deviceId}/command`
- **Remove subscriptions to `status/motor`** — motor data is now included in the consolidated `status` topic
- **Subscribe to `response` topic** (QoS changed from 0 to 1)

## 2. Command Payload Changes

### Before (old)

Each command had its own topic. The payload contained only the parameters:

```
Topic:   .../command/position
Payload: {"position": 180.0}
```

```
Topic:   .../command/enable
Payload: {"enable": true}
```

Commands without parameters used an empty JSON object:
```
Topic:   .../command/zero
Payload: {}
```

### After (new)

All commands go to a **single topic** with a `"command"` field that determines the action:

```
Topic:   .../command
Payload: {"command": "position", "position": 180.0}
```

```
Topic:   .../command
Payload: {"command": "enable", "enable": true}
```

```
Topic:   .../command
Payload: {"command": "zero"}
```

### What to change

- **Add `"command": "<name>"` to every JSON payload**
- **Publish all payloads to the single command topic** instead of per-command topics
- Command names are **case-insensitive** (`"POSITION"`, `"Position"`, `"position"` all work)

## 3. Complete Command Reference

Every command and its required payload fields:

| Command | Required Fields | Example Payload |
|---------|----------------|-----------------|
| `position` | `position` (float) | `{"command":"position","position":180.0}` |
| `heading` | `heading` (float) | `{"command":"heading","heading":90.0}` |
| `zero` | — | `{"command":"zero"}` |
| `home` | — | `{"command":"home"}` |
| `runForward` | — | `{"command":"runForward"}` |
| `runBackward` | — | `{"command":"runBackward"}` |
| `stop` | — | `{"command":"stop"}` |
| `forceStop` | — | `{"command":"forceStop"}` |
| `enable` | `enable` (bool) | `{"command":"enable","enable":true}` |
| `reset` | — | `{"command":"reset"}` |
| `speed` | `speed` (float, >0) | `{"command":"speed","speed":2000.0}` |
| `acceleration` | `accel` (float, >0) | `{"command":"acceleration","accel":400.0}` |
| `microsteps` | `microsteps` (int) | `{"command":"microsteps","microsteps":16}` |
| `gearRatio` | `ratio` (float, 0.1-100) | `{"command":"gearRatio","ratio":2.0}` |
| `speedHz` | `speedHz` (float, >=0) | `{"command":"speedHz","speedHz":1000.0}` |
| `dance` | `danceType` (string) | `{"command":"dance","danceType":"twist"}` |
| `stopDance` | — | `{"command":"stopDance"}` |
| `behavior` | `behaviorType` (string) | `{"command":"behavior","behaviorType":"scanning"}` |
| `stopBehavior` | — | `{"command":"stopBehavior"}` |

**Aliases:** `"stopMove"` is accepted as an alias for `"stop"`.

**Optional field:** `position` and `heading` commands accept an optional `"request_id"` (string) that is echoed back in the move-complete response.

**Dance types:** `twist`, `shake`, `spin`, `wiggle`, `watusi`, `pepperminttwist` (or `peppermint_twist`)

**Behavior types:** `scanning`, `sleeping`, `eating`, `alert`, `roaring`, `stalking`, `playing`, `resting`, `hunting`, `victory`

## 4. Status Topic Changes

### Before (old)

Motor status was published to a **separate** `status/motor` sub-topic.

### After (new)

There is **no** `status/motor` topic. All motor data is in the consolidated `status` topic. The payload structure:

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
    "rmsCurrent": 800,
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

**Published:** on state change (with change-detection thresholds) or periodically every 30 seconds. Retained.

### What to change

- **Unsubscribe from `status/motor`** — it no longer exists
- **Read motor data from `status` topic** under the `motor` object
- TMC2209 driver data is under `tmc2209`, WiFi under `wifi`

## 5. Response Topic Changes

### Before (old)

Response topic used QoS 0.

### After (new)

Response topic uses **QoS 1**. The response payload format is unchanged:

```json
{
  "status": "success",
  "command": "position",
  "executed": true,
  "message": "Position command queued",
  "timestamp": 1234567890
}
```

**New feature — move-complete events:** For `position` and `heading` commands, a second response is published when the move finishes:

```json
{
  "status": "success",
  "command": "heading",
  "executed": true,
  "message": "Move complete",
  "event": "complete",
  "timestamp": 1234567895,
  "request_id": "your-id-here"
}
```

Use `"event": "complete"` to distinguish move-started acks from move-complete notifications. The `request_id` field is echoed from the original command if provided.

If the move is cancelled by `stop` or `forceStop`, no move-complete event is published.

## 6. Migration Checklist

Use this checklist when refactoring client code:

- [ ] Replace all per-command topic publishes (`.../command/position`, `.../command/enable`, etc.) with publishes to the single `.../command` topic
- [ ] Add `"command": "<name>"` field to every command JSON payload
- [ ] For parameterless commands, change `{}` to `{"command": "<name>"}`
- [ ] Remove subscriptions to `{baseTopic}/{deviceId}/status/motor`
- [ ] Read motor status from the `motor` object within the `{baseTopic}/{deviceId}/status` payload
- [ ] Update response topic subscription to QoS 1
- [ ] (Optional) Use `request_id` and `event: "complete"` for move-complete tracking instead of polling
- [ ] Verify all topic string constants/variables are updated
- [ ] Test: send a command and verify a response appears on the response topic
