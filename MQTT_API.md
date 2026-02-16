# MQTT API Documentation

## Overview

The KineMachina Turntable Controller exposes a comprehensive MQTT API for remote control and monitoring. All commands use JSON payloads and publish responses to a dedicated response topic.

## Topic Structure

All topics follow this pattern:
```
{baseTopic}/{deviceId}/{category}/{command}
```

**Default Configuration:**
- Base Topic: `kinemachina/turntable`
- Device ID: `turntable_001`

**Topic Categories:**

1. **Commands**: `{baseTopic}/{deviceId}/command/{commandName}`
   - Subscribe to these topics to send commands
   - QoS: Configurable (default: 1)

2. **Status**: `{baseTopic}/{deviceId}/status`
   - Published automatically when state changes or periodically
   - QoS: Configurable (default: 0)

3. **Motor Status**: `{baseTopic}/{deviceId}/status/motor`
   - Motor-specific status updates
   - QoS: Configurable (default: 0)

4. **Online Status**: `{baseTopic}/{deviceId}/status/online`
   - Last Will and Testament (LWT) topic
   - Published as "online" when connected, "offline" on disconnect
   - Retained: true
   - QoS: Configurable (default: 0)

5. **Response**: `{baseTopic}/{deviceId}/response`
   - Command execution responses
   - QoS: Configurable (default: 0)

## Configuration

MQTT settings can be configured via the web UI or EEPROM. Default values:

- **Broker**: `mqtt.broker.local`
- **Port**: `1883`
- **Username**: (empty, optional)
- **Password**: (empty, optional)
- **Device ID**: `turntable_001`
- **Base Topic**: `kinemachina/turntable`
- **QoS Commands**: `1`
- **QoS Status**: `0`
- **Keepalive**: `60` seconds
- **Enabled**: `false` (must be enabled via web UI)

## Commands

All commands are published to: `{baseTopic}/{deviceId}/command/{commandName}`

Payloads must be valid JSON. Empty payloads `{}` are accepted for commands that don't require parameters.

### Position Control

#### Move to Position (Absolute)

**Topic**: `{baseTopic}/{deviceId}/command/position`

**Payload**:
```json
{
  "position": 180.0,
  "request_id": "optional-client-id"
}
```

**Description**: Move to an absolute turntable position in degrees (0-360). When the move finishes, a second response is published to the response topic with `message: "Move complete"` and `event: "complete"` (see [Move-complete response](#move-complete-response)).

**Parameters**:
- `position` (float, required): Target position in degrees
- `request_id` (string, optional): Client-supplied ID echoed in the move-complete response for correlation

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/position" -m '{"position": 180.0}'
```

#### Move to Heading (Shortest Path)

**Topic**: `{baseTopic}/{deviceId}/command/heading`

**Payload**:
```json
{
  "heading": 180.0,
  "request_id": "optional-client-id"
}
```

**Description**: Move to a target heading using the shortest path. Computes shortest angle from current position. When the move finishes, a second response is published to the response topic with `message: "Move complete"` and `event: "complete"` (see [Move-complete response](#move-complete-response)).

**Parameters**:
- `heading` (float, required): Target heading in degrees (0-360)
- `request_id` (string, optional): Client-supplied ID echoed in the move-complete response for correlation

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/heading" -m '{"heading": 180.0}'
```

#### Zero Position

**Topic**: `{baseTopic}/{deviceId}/command/zero`

**Payload**:
```json
{}
```

**Description**: Zero the stepper position (sets current position to 0).

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/zero" -m '{}'
```

#### Home

**Topic**: `{baseTopic}/{deviceId}/command/home`

**Payload**:
```json
{}
```

**Description**: Home the motor (move to position 0).

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/home" -m '{}'
```

### Velocity Control

#### Run Forward

**Topic**: `{baseTopic}/{deviceId}/command/runForward`

**Payload**:
```json
{}
```

**Description**: Start continuous forward rotation at current speed setting.

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/runForward" -m '{}'
```

#### Run Backward

**Topic**: `{baseTopic}/{deviceId}/command/runBackward`

**Payload**:
```json
{}
```

**Description**: Start continuous backward rotation at current speed setting.

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/runBackward" -m '{}'
```

#### Stop Move

**Topic**: `{baseTopic}/{deviceId}/command/stopMove`

**Payload**:
```json
{}
```

**Description**: Stop current movement (decelerates to stop).

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/stopMove" -m '{}'
```

#### Force Stop

**Topic**: `{baseTopic}/{deviceId}/command/forceStop`

**Payload**:
```json
{}
```

**Description**: Immediately stop all movement (emergency stop).

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/forceStop" -m '{}'
```

### Motor Control

#### Enable/Disable Motor

**Topic**: `{baseTopic}/{deviceId}/command/enable`

**Payload**:
```json
{
  "enable": true
}
```

**Description**: Enable or disable the motor driver.

**Parameters**:
- `enable` (bool, required): `true` to enable, `false` to disable

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/enable" -m '{"enable": true}'
```

#### Reset Engine

**Topic**: `{baseTopic}/{deviceId}/command/reset`

**Payload**:
```json
{}
```

**Description**: Reset the FastAccelStepper engine. Use if motor control becomes unresponsive.

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/reset" -m '{}'
```

### Configuration Commands

#### Set Max Speed

**Topic**: `{baseTopic}/{deviceId}/command/speed`

**Payload**:
```json
{
  "speed": 2000.0
}
```

**Description**: Set maximum speed for position moves.

**Parameters**:
- `speed` (float, required): Maximum speed in steps/second (must be > 0)

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/speed" -m '{"speed": 2000.0}'
```

#### Set Acceleration

**Topic**: `{baseTopic}/{deviceId}/command/acceleration`

**Payload**:
```json
{
  "accel": 400.0
}
```

**Description**: Set acceleration for position moves.

**Parameters**:
- `accel` (float, required): Acceleration in steps/second² (must be > 0)

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/acceleration" -m '{"accel": 400.0}'
```

#### Set Microstepping

**Topic**: `{baseTopic}/{deviceId}/command/microsteps`

**Payload**:
```json
{
  "microsteps": 8
}
```

**Description**: Set microstepping value for TMC2209 driver.

**Parameters**:
- `microsteps` (int, required): Microstepping value (must be power of 2: 1, 2, 4, 8, 16, 32, 64, 128, or 256)

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/microsteps" -m '{"microsteps": 8}'
```

#### Set Gear Ratio

**Topic**: `{baseTopic}/{deviceId}/command/gearratio`

**Payload**:
```json
{
  "ratio": 2.0
}
```

**Description**: Set gear ratio (stepper rotations : turntable rotations).

**Parameters**:
- `ratio` (float, required): Gear ratio (must be between 0.1 and 100.0)

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/gearratio" -m '{"ratio": 2.0}'
```

#### Set Speed (Hz)

**Topic**: `{baseTopic}/{deviceId}/command/speedHz`

**Payload**:
```json
{
  "speedHz": 1000.0
}
```

**Description**: Set speed for velocity mode (continuous rotation).

**Parameters**:
- `speedHz` (float, required): Speed in Hz (steps per second, must be >= 0)

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/speedHz" -m '{"speedHz": 1000.0}'
```

### Dance Commands

#### Start Dance

**Topic**: `{baseTopic}/{deviceId}/command/dance`

**Payload**:
```json
{
  "danceType": "twist"
}
```

**Description**: Start a dance sequence. Dance runs in background task and can be stopped.

**Parameters**:
- `danceType` (string, required): Dance type (case-insensitive)
  - `"twist"` - Chubby Checkers "Twist" - back and forth with increasing arcs
  - `"shake"` - Quick shake - small rapid back and forth
  - `"spin"` - Full rotations back and forth
  - `"wiggle"` - Small wiggles in place
  - `"watusi"` - Watusi - side-to-side alternating movements
  - `"pepperminttwist"` or `"peppermint_twist"` - Rapid alternating twists

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/dance" -m '{"danceType": "twist"}'
```

#### Stop Dance

**Topic**: `{baseTopic}/{deviceId}/command/stopDance`

**Payload**:
```json
{}
```

**Description**: Stop the currently running dance sequence.

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/stopDance" -m '{}'
```

### Behavior Commands

#### Start Behavior

**Topic**: `{baseTopic}/{deviceId}/command/behavior`

**Payload**:
```json
{
  "behaviorType": "scanning"
}
```

**Description**: Start a behavior sequence. Behavior runs in background task and can be stopped.

**Parameters**:
- `behaviorType` (string, required): Behavior type (case-insensitive)
  - `"scanning"` - Scanning behavior
  - `"sleeping"` - Sleeping behavior
  - `"eating"` - Eating behavior
  - `"alert"` - Alert behavior
  - `"roaring"` - Roaring behavior
  - `"stalking"` - Stalking behavior
  - `"playing"` - Playing behavior
  - `"resting"` - Resting behavior
  - `"hunting"` - Hunting behavior
  - `"victory"` - Victory behavior

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/behavior" -m '{"behaviorType": "scanning"}'
```

#### Stop Behavior

**Topic**: `{baseTopic}/{deviceId}/command/stopBehavior`

**Payload**:
```json
{}
```

**Description**: Stop the currently running behavior sequence.

**Example**:
```bash
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/stopBehavior" -m '{}'
```

## Response Format

All commands publish a response to: `{baseTopic}/{deviceId}/response`

**Success Response** (immediate, when command is accepted):
```json
{
  "status": "success",
  "command": "position",
  "executed": true,
  "message": "Position command queued",
  "timestamp": 1234567890
}
```

**Error Response**:
```json
{
  "status": "error",
  "command": "position",
  "executed": false,
  "message": "Missing or invalid 'position' parameter",
  "error": "Expected float",
  "timestamp": 1234567890
}
```

**Fields**:
- `status` (string): `"success"` or `"error"`
- `command` (string): Name of the command that was executed
- `executed` (bool): `true` if command was executed successfully, `false` otherwise
- `message` (string): Human-readable message
- `error` (string, optional): Error details (only present on error)
- `timestamp` (number): Timestamp in milliseconds since boot

### Move-complete response

For **heading** and **position** commands, the firmware publishes a **second** response on the same response topic when the move finishes (motor has stopped). Clients can wait for this message instead of polling HTTP to know when a move is done.

**Move-complete response** (published when the move finishes):
```json
{
  "status": "success",
  "command": "heading",
  "executed": true,
  "message": "Move complete",
  "event": "complete",
  "timestamp": 1234567895,
  "request_id": "optional-client-id"
}
```

**Fields** (in addition to the standard response fields):
- `message`: Always `"Move complete"` for this event
- `event`: `"complete"` — use this to distinguish “move started” from “move complete” without parsing the message string
- `request_id` (string, optional): Echoed from the command payload if the client included it; use for correlating the completion with a specific command (e.g. when using `wait_response=True`)

**Behavior**:
- One move-complete response is published per move when the motor stops. If a new heading/position command is sent while a move is running, the pending completion is overwritten; the single move-complete published when the motor stops corresponds to the most recently started move.
- If the move is stopped externally (**stopMove** or **forceStop**), no move-complete response is published (the pending flag is cleared).

## Status Messages

### Full Status

**Topic**: `{baseTopic}/{deviceId}/status`

**Published**: Automatically when state changes or periodically (every 30 seconds)

**Payload**:
```json
{
  "status": "success",
  "timestamp": 1234567890,
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
  "wifi": {
    "connected": true,
    "ip": "192.168.1.100"
  }
}
```

**Motor Fields**:
- `enabled` (bool): Motor driver enabled state
- `running` (bool): Motor is currently moving
- `position` (number): Stepper position in steps
- `positionDegrees` (float): Stepper position in degrees
- `speedHz` (float): Current target speed in Hz (always 0, tracking removed)
- `microsteps` (number): Current microstepping setting
- `gearRatio` (float): Current gear ratio
- `behaviorInProgress` (bool): Behavior sequence is running
- `danceInProgress` (bool): Dance sequence is running

**WiFi Fields**:
- `connected` (bool): WiFi connection status
- `ip` (string, if connected): IP address

### Motor Status

**Topic**: `{baseTopic}/{deviceId}/status/motor`

**Published**: Motor-specific status updates (subset of full status)

**Payload**: Same as `motor` object from full status

### Full Status (TMC2209 + magnet)

**Topic**: `{baseTopic}/{deviceId}/status/full`

**Published**: Automatically when state changes or periodically (every 30 seconds), and on MQTT connect

**Payload**: Extended status including TMC2209 driver registers and detailed magnet status (same data as HTTP `GET /stepper/status?full=true`):
- All fields from main status (enabled, isRunning, microsteps, gearRatio, speedHz, stepperPosition, behaviorInProgress)
- `tmc2209` (object): rmsCurrent, csActual, actualCurrent, irun, ihold, enabled, spreadCycle, pwmAutoscale, blankTime
- `wifi`: connected, ip

### Online Status

**Topic**: `{baseTopic}/{deviceId}/status/online`

**Published**: 
- `"online"` when MQTT connects (retained)
- `"offline"` when MQTT disconnects (retained, via LWT)

**Payload**: String `"online"` or `"offline"`

## Examples

### Complete Workflow Example

```bash
# 1. Check if device is online
mosquitto_sub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/status/online"

# 2. Subscribe to status updates
mosquitto_sub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/status"

# 3. Subscribe to responses
mosquitto_sub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/response"

# 4. Enable motor
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/enable" -m '{"enable": true}'

# 5. Move to 180 degrees
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/position" -m '{"position": 180.0}'

# 6. Start a dance
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/dance" -m '{"danceType": "twist"}'

# 7. Stop the dance
mosquitto_pub -h mqtt.broker.local -t "kinemachina/turntable/turntable_001/command/stopDance" -m '{}'
```

### Python Example

```python
import paho.mqtt.client as mqtt
import json

BROKER = "mqtt.broker.local"
BASE_TOPIC = "kinemachina/turntable"
DEVICE_ID = "turntable_001"

def on_connect(client, userdata, flags, rc):
    print(f"Connected with result code {rc}")
    # Subscribe to responses and status
    client.subscribe(f"{BASE_TOPIC}/{DEVICE_ID}/response")
    client.subscribe(f"{BASE_TOPIC}/{DEVICE_ID}/status")

def on_message(client, userdata, msg):
    payload = json.loads(msg.payload.decode())
    print(f"Received on {msg.topic}: {payload}")

client = mqtt.Client()
client.on_connect = on_connect
client.on_message = on_message
client.connect(BROKER, 1883, 60)
client.loop_start()

# Move to 180 degrees
command_topic = f"{BASE_TOPIC}/{DEVICE_ID}/command/position"
payload = json.dumps({"position": 180.0})
client.publish(command_topic, payload)

# Start dance
command_topic = f"{BASE_TOPIC}/{DEVICE_ID}/command/dance"
payload = json.dumps({"danceType": "twist"})
client.publish(command_topic, payload)

# Keep running
client.loop_forever()
```

### Python Example: Wait for Move Complete

To wait for a heading/position move to finish without polling HTTP, include a `request_id` and subscribe to the response topic; when you receive a response with `event == "complete"` and the same `request_id`, the move is done:

```python
import paho.mqtt.client as mqtt
import json
import time
import uuid

BROKER = "mqtt.broker.local"
BASE_TOPIC = "kinemachina/turntable"
DEVICE_ID = "turntable_001"

move_complete = False
request_id = str(uuid.uuid4())

def on_message(client, userdata, msg):
    global move_complete
    payload = json.loads(msg.payload.decode())
    if payload.get("event") == "complete" and payload.get("request_id") == request_id:
        move_complete = True

client = mqtt.Client()
client.on_message = on_message
client.connect(BROKER, 1883, 60)
client.subscribe(f"{BASE_TOPIC}/{DEVICE_ID}/response")
client.loop_start()

client.publish(
    f"{BASE_TOPIC}/{DEVICE_ID}/command/heading",
    json.dumps({"heading": 90.0, "request_id": request_id})
)
# Wait for move complete (in practice add a timeout)
while not move_complete:
    time.sleep(0.1)
print("Move complete for request_id:", request_id)
```

### Node.js Example

```javascript
const mqtt = require('mqtt');

const BROKER = 'mqtt://mqtt.broker.local';
const BASE_TOPIC = 'kinemachina/turntable';
const DEVICE_ID = 'turntable_001';

const client = mqtt.connect(BROKER);

client.on('connect', () => {
  console.log('Connected to MQTT broker');
  
  // Subscribe to responses and status
  client.subscribe(`${BASE_TOPIC}/${DEVICE_ID}/response`);
  client.subscribe(`${BASE_TOPIC}/${DEVICE_ID}/status`);
  
  // Move to 180 degrees
  const commandTopic = `${BASE_TOPIC}/${DEVICE_ID}/command/position`;
  client.publish(commandTopic, JSON.stringify({ position: 180.0 }));
  
  // Start dance
  const danceTopic = `${BASE_TOPIC}/${DEVICE_ID}/command/dance`;
  client.publish(danceTopic, JSON.stringify({ danceType: 'twist' }));
});

client.on('message', (topic, message) => {
  const payload = JSON.parse(message.toString());
  console.log(`Received on ${topic}:`, payload);
});
```

## Error Handling

### Common Errors

1. **Invalid JSON**: Payload is not valid JSON
   ```json
   {
     "status": "error",
     "command": "position",
     "executed": false,
     "message": "Invalid JSON",
     "error": "Unexpected token"
   }
   ```

2. **Missing Parameter**: Required parameter is missing
   ```json
   {
     "status": "error",
     "command": "position",
     "executed": false,
     "message": "Missing or invalid 'position' parameter",
     "error": "Expected float"
   }
   ```

3. **Invalid Value**: Parameter value is out of range
   ```json
   {
     "status": "error",
     "command": "microsteps",
     "executed": false,
     "message": "Invalid microstepping value",
     "error": "Must be 1, 2, 4, 8, 16, 32, 64, 128, or 256"
   }
   ```

4. **Command Queue Full**: Too many commands queued
   ```json
   {
     "status": "error",
     "command": "position",
     "executed": false,
     "message": "Command queue full"
   }
   ```

5. **Already Running**: Dance or behavior already in progress
   ```json
   {
     "status": "error",
     "command": "dance",
     "executed": false,
     "message": "Dance failed to start",
     "error": "Dance already in progress or stepper unavailable"
   }
   ```

## Best Practices

1. **Always Subscribe to Response Topic**: Monitor responses to verify command execution
2. **Subscribe to Status Topic**: Keep track of motor state
3. **Check Online Status**: Verify device is connected before sending commands
4. **Handle Errors**: Check `executed` field in responses
5. **Use Appropriate QoS**: 
   - QoS 1 for commands (ensures delivery)
   - QoS 0 for status (lower overhead, acceptable to miss updates)
6. **Monitor Queue**: If you get "Command queue full" errors, reduce command frequency
7. **Stop Before New Command**: Stop current movement before starting a new one if needed
8. **Wait for Move Complete**: For heading/position commands, wait for the second response (`event: "complete"` or `message: "Move complete"`) instead of polling HTTP; include `request_id` in the command payload to correlate the completion with your command

## Topic Summary

| Topic Pattern | Direction | Description |
|--------------|-----------|-------------|
| `{baseTopic}/{deviceId}/command/*` | Publish → | Send commands to device |
| `{baseTopic}/{deviceId}/response` | Subscribe ← | Command execution responses |
| `{baseTopic}/{deviceId}/status` | Subscribe ← | Full system status |
| `{baseTopic}/{deviceId}/status/motor` | Subscribe ← | Motor-specific status |
| `{baseTopic}/{deviceId}/status/online` | Subscribe ← | Online/offline status (LWT) |

## Configuration via Web UI

MQTT settings can be configured via the web interface:
1. Navigate to `http://{device-ip}/`
2. Scroll to "MQTT Configuration" section
3. Configure broker, port, credentials, topics, etc.
4. Check "Enable MQTT" checkbox
5. Click "Save MQTT Configuration"
6. MQTT will automatically restart with new settings

Settings are persisted to EEPROM and will be restored on reboot.
