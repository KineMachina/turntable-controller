# REST API Documentation

Complete API reference for the KineMachina Turntable Controller.

## Base URL

All endpoints are available at:
```
http://<esp32-ip-address>
```

Replace `<esp32-ip-address>` with the IP address assigned to your ESP32-S3 device (visible in serial monitor on startup).

## Content Type

All POST requests must include:
```
Content-Type: application/json
```

All responses are returned as:
```
Content-Type: application/json
```

## Response Format

### Success Response
```json
{
  "status": "ok",
  "message": "Operation completed successfully",
  ...additional fields...
}
```

### Error Response
```json
{
  "status": "error",
  "message": "Error description"
}
```

## Endpoints

### System Status

#### GET /status
Get overall system status including WiFi connection and stepper motor status.

**Response:**
```json
{
  "status": "ok",
  "wifiConnected": true,
  "ipAddress": "192.168.1.100",
  "stepperEnabled": true,
  "stepperPositionDegrees": 45.5
}
```

---

### Web Interface

#### GET /
Returns the web-based control interface (HTML page).

---

### Position Control

#### POST /stepper/position
Move to target position in degrees.

**Request Body:**
```json
{
  "position": 90.0
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Target position set to 90.00°",
  "targetPositionDegrees": 90.0,
  "currentPositionDegrees": 45.5
}
```

#### GET /stepper/position
Get current and target position in degrees.

**Response:**
```json
{
  "status": "ok",
  "currentPositionDegrees": 45.5,
  "targetPositionDegrees": 90.0
}
```

---

### Heading Control

#### POST /stepper/heading
Move to target heading in degrees using shortest path (relative movement).

**Request Body:**
```json
{
  "heading": 180.0
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Moving to heading 180.00° (shortest path)",
  "targetHeading": 180.0,
  "currentHeading": 45.5
}
```

**Notes:**
- Calculates shortest angular path (handles 360° wrap-around)
- Uses relative movement (`stepper->move()`) instead of absolute positioning
- Heading is normalized to 0-360° range
- Example: Moving from 350° to 10° will move +20° (not -340°)

---

#### POST /stepper/stopMove
Stop the current move operation (passthrough to stepper->stopMove).

**Request Body:**
```json
{}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Move stopped"
}
```

---

#### GET /stepper/positionStatus
Get combined motor position status.

**Response:**
```json
{
  "status": "ok",
  "stepperPosition": 1000,
  "turntablePositionDegrees": 90.0,
  "gearRatio": 2.0
}
```

**Notes:**
- `stepperPosition` is always included (in steps)
- `turntablePositionDegrees` is calculated from stepper position divided by gear ratio
- `gearRatio` shows the current gear ratio setting

---

### Turntable Position

#### GET /stepper/turntablePosition
Get the turntable position in degrees, accounting for gear ratio.

**Response:**
```json
{
  "status": "ok",
  "turntablePositionDegrees": 45.5
}
```

**Notes:**
- Returns the turntable position calculated from stepper position divided by gear ratio

---

### Gear Ratio

#### POST /stepper/gearratio
Set the gear ratio between stepper motor and turntable.

**Request Body:**
```json
{
  "ratio": 2.0
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Gear ratio set to 2.00:1",
  "gearRatio": 2.0
}
```

**Notes:**
- Gear ratio defines how many stepper rotations equal one turntable rotation
- Default: 1.0 (1:1 ratio)
- Used in position calculations to convert between stepper degrees and turntable degrees

#### GET /stepper/gearratio
Get current gear ratio.

**Response:**
```json
{
  "status": "ok",
  "gearRatio": 2.0
}
```

---

### Dance Effects

#### GET /stepper/dance
Get list of available dance types.

**Response:**
```json
{
  "status": "ok",
  "dances": [
    {
      "id": "twist",
      "name": "Twist",
      "description": "Chubby Checkers 'Twist' pattern with increasing/decreasing arcs"
    },
    {
      "id": "shake",
      "name": "Shake",
      "description": "Quick shake with rapid back and forth movements"
    },
    {
      "id": "spin",
      "name": "Spin",
      "description": "Full rotations back and forth"
    },
    {
      "id": "wiggle",
      "name": "Wiggle",
      "description": "Small wiggles in place"
    }
  ]
}
```

**Notes:**
- Returns all available dance types with their IDs, names, and descriptions
- Use the `id` field when calling the POST endpoint to start a dance

---

#### POST /stepper/dance
Trigger a pre-programmed dance sequence with rotation patterns.

**Request Body:**
```json
{
  "danceType": "twist"
}
```

**Valid dance types:**
- `"twist"` - Chubby Checkers "Twist" pattern with increasing/decreasing arcs
- `"shake"` - Quick shake with rapid back and forth movements
- `"spin"` - Full rotations back and forth
- `"wiggle"` - Small wiggles in place

**Response:**
```json
{
  "status": "ok",
  "message": "Dance started: twist",
  "danceType": "twist"
}
```

**Notes:**
- Dance sequences run in a separate FreeRTOS task (non-blocking)
- The HTTP response returns immediately with "Dance started" message
- Dance execution continues in background without blocking the HTTP server
- Uses relative movements (`stepper->move()`) from current position
- Dance patterns (all relative movements):
  - **Twist**: +45°, -135°, +225°, -315°, +135°, -225°, +135°, -45° (8 moves)
  - **Shake**: Rapid ±30° movements (8 times, alternating)
  - **Spin**: +360°, -720°, +720°, -360° (4 moves, full rotations)
  - **Wiggle**: Small ±15° movements (12 times, alternating)
- Only one dance can run at a time (subsequent requests are ignored if dance is in progress)

---

### Behaviors

#### GET /stepper/behavior
Get list of available behavior types.

**Response:**
```json
{
  "status": "ok",
  "behaviors": [
    {
      "id": "scanning",
      "name": "Scanning",
      "description": "Slow 360° sweeps scanning for enemies"
    },
    {
      "id": "sleeping",
      "name": "Sleeping",
      "description": "Minimal subtle movements while resting"
    },
    {
      "id": "eating",
      "name": "Eating",
      "description": "Rhythmic chewing-like motions"
    },
    {
      "id": "alert",
      "name": "Alert",
      "description": "Rapid, jerky scanning when threatened"
    },
    {
      "id": "roaring",
      "name": "Roaring",
      "description": "Large intimidating sweeps and spins"
    },
    {
      "id": "stalking",
      "name": "Stalking",
      "description": "Slow deliberate movements with freezes"
    },
    {
      "id": "playing",
      "name": "Playing",
      "description": "Erratic playful movements"
    },
    {
      "id": "resting",
      "name": "Resting",
      "description": "Idle with occasional minor adjustments"
    },
    {
      "id": "hunting",
      "name": "Hunting",
      "description": "Focused sector scanning"
    },
    {
      "id": "victory",
      "name": "Victory",
      "description": "Celebratory spins and sweeps"
    }
  ]
}
```

**Notes:**
- Returns all available behavior types with their IDs, names, and descriptions
- Use the `id` field when calling the POST endpoint to start a behavior

---

#### POST /stepper/behavior
Trigger a pre-programmed behavior sequence with thematic rotation patterns.

**Request Body:**
```json
{
  "behaviorType": "scanning"
}
```

**Valid behavior types:**
- `"scanning"` - Slow 360° sweeps scanning for enemies (60-90 seconds)
- `"sleeping"` - Minimal subtle movements while resting (continuous until stopped)
- `"eating"` - Rhythmic chewing-like motions (30-45 seconds)
- `"alert"` - Rapid, jerky scanning when threatened (20-30 seconds)
- `"roaring"` - Large intimidating sweeps and spins (40-60 seconds)
- `"stalking"` - Slow deliberate movements with freezes (60-90 seconds)
- `"playing"` - Erratic playful movements (45-60 seconds)
- `"resting"` - Idle with occasional minor adjustments (continuous until stopped)
- `"hunting"` - Focused sector scanning (50-70 seconds)
- `"victory"` - Celebratory spins and sweeps (30-45 seconds)

**Response:**
```json
{
  "status": "ok",
  "message": "Behavior started",
  "behaviorType": "scanning"
}
```

**Notes:**
- Behavior sequences run in a separate FreeRTOS task (non-blocking)
- The HTTP response returns immediately with "Behavior started" message
- Behavior execution continues in background without blocking the HTTP server
- Uses relative movements (`stepper->move()`) from current position
- Each behavior sets appropriate speed for its movement pattern:
  - Very Slow (200-400 Hz): Sleeping, Resting
  - Slow (400-800 Hz): Scanning, Stalking
  - Medium (800-1200 Hz): Eating, Hunting
  - Medium-Fast (1000-1500 Hz): Roaring
  - Fast (1500-2000 Hz): Alert, Victory, Playing
- Only one behavior can run at a time (subsequent requests are ignored if behavior is in progress)
- Some behaviors (Sleeping, Resting) run continuously until stopped

---

#### POST /stepper/stopBehavior
Stop the currently running behavior sequence.

**Request Body:**
```json
{}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Behavior stopped"
}
```

**Notes:**
- Stops the currently running behavior immediately
- Returns error if no behavior is in progress

---

### Motor Control

#### POST /stepper/enable
Enable or disable the stepper motor.

**Request Body:**
```json
{
  "enable": true
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Motor enabled",
  "enabled": true
}
```

---

### Speed & Acceleration

#### POST /stepper/speed
Set maximum speed in steps per second.

**Request Body:**
```json
{
  "speed": 2000.0
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Speed set to 2000.00 steps/sec",
  "speed": 2000.0
}
```

**Notes:**
- Default: 2000 steps/sec
- Configurable via API

#### POST /stepper/acceleration
Set acceleration in steps per second squared.

**Request Body:**
```json
{
  "accel": 400.0
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Acceleration set to 400.00 steps/sec²",
  "accel": 400.0
}
```

**Notes:**
- Default: 400 steps/sec²
- Configurable via API

---

### Velocity Control

#### POST /stepper/runForward
Start continuous forward rotation (passthrough to stepper->runForward).

**Request Body:**
```json
{}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Forward rotation started",
  "direction": "forward",
  "speedHz": 0.0
}
```

#### POST /stepper/runBackward
Start continuous backward rotation (passthrough to stepper->runBackward).

**Request Body:**
```json
{}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Backward rotation started",
  "direction": "backward",
  "speedHz": 0.0
}
```

#### POST /stepper/stopMove
Stop the current move operation (passthrough to stepper->stopMove).

**Request Body:**
```json
{}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Move stopped"
}
```

#### POST /stepper/forceStop
Force stop the motor immediately (passthrough to stepper->forceStop).

**Request Body:**
```json
{}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Force stop executed"
}
```

---

---

### Microstepping

#### POST /stepper/microsteps
Set microstepping value (1, 2, 4, 8, 16, 32, 64, 128, or 256).

**Request Body:**
```json
{
  "microsteps": 8
}
```

**Response:**
```json
{
  "status": "ok",
  "message": "Microsteps set to 8",
  "microsteps": 8
}
```

**Notes:**
- Default: 8 microsteps
- Must be a power of 2
- TMC2209 read-back verification is performed to detect mismatches

#### GET /stepper/microsteps
Get current microstepping value.

**Response:**
```json
{
  "status": "ok",
  "microsteps": 8
}
```

---

---

### Engine Reset

#### POST /stepper/reset
Reset the FastAccelStepper engine. This reinitializes the stepper motor control engine.

**Request Body:**
```json
{}
```

**Response:**
```json
{
  "status": "ok",
  "message": "FastAccelStepper engine reset successfully"
}
```

---

### Status

#### GET /stepper/status
Get comprehensive stepper motor status.

**Response:**
```json
{
  "status": "ok",
  "currentPositionDegrees": 45.5,
  "enabled": true,
  "isRunning": true,
  "microsteps": 8,
  "speedHz": 0.0,
  "gearRatio": 2.0,
  "behaviorInProgress": false,
  "tmc2209": {
    "rmsCurrent": 1200,
    "csActual": 31,
    "actualCurrent": 1200,
    "irun": 31,
    "ihold": 31,
    "enabled": true,
    "spreadCycle": false,
    "pwmAutoscale": true,
    "blankTime": 24
  }
}
```

**Notes:**
- `speedHz` always returns 0.0 (speed tracking removed)
- `tmc2209` object contains TMC2209 driver settings:
  - `rmsCurrent`: RMS current setting in milliamps
  - `csActual`: CS_ACTUAL register value (0-31)
  - `actualCurrent`: Calculated actual current in milliamps
  - `irun`: Running current setting (0-31, where 31 = 100% of RMS)
  - `ihold`: Holding current setting (0-31, where 31 = 100% of running current)
  - `enabled`: Driver enable status (toff > 0)
  - `spreadCycle`: Mode (false = StealthChop, true = SpreadCycle)
  - `pwmAutoscale`: PWM autoscale status
  - `blankTime`: Blank time setting

---

### Motor Running Status

#### GET /stepper/running
Check if the motor is currently running (moving).

**Response:**
```json
{
  "status": "ok",
  "isRunning": true
}
```

**Notes:**
- Returns `true` if the motor is currently moving (either in position mode moving to a target, or in velocity mode continuously rotating)
- Returns `false` if the motor is stopped
- This is a fast, non-blocking check that uses the FastAccelStepper `isRunning()` method

---

## HTTP Status Codes

- `200 OK` - Request successful
- `400 Bad Request` - Invalid request (missing parameters, invalid JSON, etc.)
- `405 Method Not Allowed` - HTTP method not supported for this endpoint
- `500 Internal Server Error` - Server error during processing
- `503 Service Unavailable` - Command queue full or resource unavailable
- `404 Not Found` - Endpoint not found

---

## Example Usage

### Python Example

```python
import requests

ESP32_IP = "192.168.1.100"
BASE_URL = f"http://{ESP32_IP}"

# Set target position
response = requests.post(
    f"{BASE_URL}/stepper/position",
    json={"position": 90.0},
    headers={"Content-Type": "application/json"}
)
print(response.json())

# Move to heading using shortest path
response = requests.post(
    f"{BASE_URL}/stepper/heading",
    json={"heading": 180.0},
    headers={"Content-Type": "application/json"}
)
print(response.json())

# Set speed in Hz and start forward rotation
response = requests.post(
    f"{BASE_URL}/stepper/speedHz",
    json={"speedHz": 2000.0},
    headers={"Content-Type": "application/json"}
)
print(response.json())

response = requests.post(
    f"{BASE_URL}/stepper/runForward",
    json={},
    headers={"Content-Type": "application/json"}
)
print(response.json())

# Get status (includes TMC2209 settings)
response = requests.get(f"{BASE_URL}/stepper/status")
print(response.json())

# Get available dance types
response = requests.get(f"{BASE_URL}/stepper/dance")
dances = response.json()
print("Available dances:", dances["dances"])

# Trigger a dance sequence (non-blocking, returns immediately)
response = requests.post(
    f"{BASE_URL}/stepper/dance",
    json={"danceType": "twist"},
    headers={"Content-Type": "application/json"}
)
print(response.json())

# Get available behavior types
response = requests.get(f"{BASE_URL}/stepper/behavior")
behaviors = response.json()
print("Available behaviors:", behaviors["behaviors"])

# Trigger a behavior sequence (non-blocking, returns immediately)
response = requests.post(
    f"{BASE_URL}/stepper/behavior",
    json={"behaviorType": "scanning"},
    headers={"Content-Type": "application/json"}
)
print(response.json())
```

### cURL Example

```bash
# Move to position 45 degrees
curl -X POST http://192.168.1.100/stepper/position \
  -H "Content-Type: application/json" \
  -d '{"position": 45.0}'

# Move to heading using shortest path
curl -X POST http://192.168.1.100/stepper/heading \
  -H "Content-Type: application/json" \
  -d '{"heading": 180.0}'

# Set speed to 2000 Hz
curl -X POST http://192.168.1.100/stepper/speedHz \
  -H "Content-Type: application/json" \
  -d '{"speedHz": 2000.0}'

# Start forward rotation
curl -X POST http://192.168.1.100/stepper/runForward \
  -H "Content-Type: application/json" \
  -d '{}'

# Get status (includes TMC2209 settings)
curl http://192.168.1.100/stepper/status

# Get available dance types
curl http://192.168.1.100/stepper/dance

# Trigger a dance sequence (non-blocking)
curl -X POST http://192.168.1.100/stepper/dance \
  -H "Content-Type: application/json" \
  -d '{"danceType": "shake"}'

# Get available behavior types
curl http://192.168.1.100/stepper/behavior

# Trigger a behavior sequence (non-blocking)
curl -X POST http://192.168.1.100/stepper/behavior \
  -H "Content-Type: application/json" \
  -d '{"behaviorType": "scanning"}'
```

### JavaScript/Node.js Example

```javascript
const axios = require('axios');

const ESP32_IP = '192.168.1.100';
const BASE_URL = `http://${ESP32_IP}`;

// Set position
async function setPosition(degrees) {
  const response = await axios.post(
    `${BASE_URL}/stepper/position`,
    { position: degrees },
    { headers: { 'Content-Type': 'application/json' } }
  );
  console.log(response.data);
}

// Move to heading using shortest path
async function moveToHeading(heading) {
  const response = await axios.post(
    `${BASE_URL}/stepper/heading`,
    { heading },
    { headers: { 'Content-Type': 'application/json' } }
  );
  console.log(response.data);
}

// Set speed and start rotation
async function startForward(speedHz) {
  await axios.post(
    `${BASE_URL}/stepper/speedHz`,
    { speedHz },
    { headers: { 'Content-Type': 'application/json' } }
  );
  
  await axios.post(
    `${BASE_URL}/stepper/runForward`,
    {},
    { headers: { 'Content-Type': 'application/json' } }
  );
}

// Get status (includes TMC2209 settings)
async function getStatus() {
  const response = await axios.get(`${BASE_URL}/stepper/status`);
  console.log(response.data);
  if (response.data.tmc2209) {
    console.log('TMC2209 Settings:', response.data.tmc2209);
  }
}

// Get available dance types
async function getAvailableDances() {
  const response = await axios.get(`${BASE_URL}/stepper/dance`);
  console.log('Available dances:', response.data.dances);
  return response.data.dances;
}

// Trigger dance (non-blocking, returns immediately)
async function performDance(danceType) {
  const response = await axios.post(
    `${BASE_URL}/stepper/dance`,
    { danceType },
    { headers: { 'Content-Type': 'application/json' } }
  );
  console.log(response.data);
}

// Usage
setPosition(90);
moveToHeading(180);
startForward(2000);
getStatus();

// Get available dances and trigger one
getAvailableDances().then(dances => {
  if (dances.length > 0) {
    performDance(dances[0].id); // Use first available dance
  }
});

// Get available behaviors and trigger one
async function getAvailableBehaviors() {
  const response = await axios.get(`${BASE_URL}/stepper/behavior`);
  console.log('Available behaviors:', response.data.behaviors);
  return response.data.behaviors;
}

async function performBehavior(behaviorType) {
  const response = await axios.post(
    `${BASE_URL}/stepper/behavior`,
    { behaviorType },
    { headers: { 'Content-Type': 'application/json' } }
  );
  console.log(response.data);
}

getAvailableBehaviors().then(behaviors => {
  if (behaviors.length > 0) {
    performBehavior(behaviors[0].id); // Use first available behavior
  }
});
```

---

## Notes

1. **Position Control**: The `/stepper/position` endpoint uses `moveToDegrees()` to move to the target position. The position is specified in degrees and is automatically converted to stepper steps.

2. **Heading Control**: The `/stepper/heading` endpoint uses `moveToHeadingDegrees()` to move to a target heading using the shortest path. It calculates the relative shortest angle, then uses relative movement (`stepper->move()`) instead of absolute positioning.

3. **Velocity Control**: Velocity control endpoints (`runForward`, `runBackward`, `stopMove`, `forceStop`) are direct passthroughs to the FastAccelStepper library. These methods execute immediately without state tracking.

4. **Immediate Execution**: Some endpoints (like `runForward`, `runBackward`, `stopMove`, `forceStop`) execute immediately without queuing for faster response times.

5. **JSON Validation**: All POST requests with JSON bodies are validated. Missing `Content-Type: application/json` header or invalid JSON will return a 400 error.

6. **Command Queue**: Most position and configuration commands are queued via FreeRTOS for thread-safe execution. Direct control commands execute immediately for responsiveness.

7. **Gear Ratio**: The gear ratio defines the relationship between stepper motor rotations and turntable rotations. It's used to convert between stepper degrees and turntable degrees in position calculations. Default: 1.0 (1:1 ratio).

8. **Dance Effects**: Dance sequences are pre-programmed rotation patterns that use relative movements. The HTTP request returns immediately with "Dance started" - the dance executes in a separate FreeRTOS task to avoid blocking the HTTP server and prevent watchdog timeouts. Only one dance can run at a time.

9. **Behaviors**: Behavior sequences are thematic movement patterns. Like dances, they use relative movements and run in a separate FreeRTOS task. Each behavior sets appropriate speed for its movement pattern (ranging from very slow 200-400 Hz for sleeping/resting to fast 1500-2000 Hz for alert/victory). Some behaviors (Sleeping, Resting) run continuously until stopped, while others have fixed durations. Only one behavior can run at a time.

10. **TMC2209 Settings**: All TMC2209 driver settings (current, mode, PWM autoscale, etc.) are reported in the `/stepper/status` endpoint under the `tmc2209` object. Default settings: 1200mA RMS current, 100% running current (irun=31), 100% holding current (ihold=31), StealthChop mode.

11. **Default Values**:
    - Max Speed: 2000 steps/sec
    - Acceleration: 400 steps/sec²
    - Microstepping: 8
    - Gear Ratio: 1.0
    - TMC2209 Current: 1200mA

12. **Removed Features**: 
    - PID control has been completely removed
    - Current/torque control (setCurrent, setRunningCurrent, setHoldingCurrent, setSpreadCycle) removed
    - Home and zero position endpoints removed
    - Auto-home at startup removed
    - Velocity mode state tracking removed (methods are now passthroughs)
