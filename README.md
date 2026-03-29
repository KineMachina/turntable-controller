# KineMachina Turntable Controller

A high-performance stepper motor controller for ESP32-S3 featuring TMC2209 driver control, web-based control interface, MQTT integration, and persistent configuration storage.

## Features

- **TMC2209 Stepper Driver Control**
  - UART-based configuration and control
  - Microstepping configuration (1-256 steps)
  - Basic motor enable/disable control

- **FastAccelStepper Library**
  - High-performance stepper motor control
  - Configurable acceleration and maximum speed
  - Smooth motion profiles
  - Direct passthrough to library methods

- **Web-Based Control Interface**
  - AsyncWebServer with non-blocking JSON REST API
  - Pure HTML/AJAX interface (no server-side template rendering)
  - Real-time status monitoring via REST API polling
  - Position control in degrees (absolute)
  - Heading control with shortest path calculation (relative movement)
  - Velocity control with speed (Hz) and direction control
  - Motor configuration controls
  - TMC2209 settings monitoring (current, mode, etc.)
  - MQTT configuration interface
  - Dance effects with pre-programmed rotation sequences (Twist, Shake, Spin, Wiggle, Watusi, Peppermint Twist)
  - Behaviors with thematic movement patterns (Scanning, Sleeping, Eating, Alert, Roaring, Stalking, Playing, Resting, Hunting, Victory)
  - Dance and behavior sequences run in separate FreeRTOS task (non-blocking)

- **MQTT Integration**
  - Full MQTT command/control API
  - Publish/subscribe architecture
  - Automatic status publishing on state changes
  - Command response feedback
  - **Move-complete notification**: a second response is published when a heading or position move finishes (message `"Move complete"`, optional `event: "complete"` and `request_id` echo for client matching)
  - Online/offline status (LWT)
  - Configurable QoS levels
  - See [MQTT_API.md](MQTT_API.md) for complete documentation

- **Persistent Configuration Storage**
  - EEPROM (NVS) storage for all settings
  - WiFi credentials persistence
  - MQTT configuration persistence
  - Motor settings persistence (speed, acceleration, microsteps, gear ratio)
  - TMC2209 settings persistence
  - Automatic configuration save on changes
  - Configuration loaded on boot

- **FreeRTOS Task-Based Architecture**
  - Concurrent execution of motor control, HTTP server, and serial command processing
  - Thread-safe command queues for motor and serial commands
  - Non-blocking HTTP server for improved responsiveness

## Hardware Requirements

- **ESP32-S3 DevKit C-1** (or compatible ESP32-S3 board)
- **TMC2209 Stepper Driver** with UART capability
- **Stepper Motor** (compatible with TMC2209)
- **Power Supply** (24V recommended for motor)
## Pin Connections

### TMC2209 Stepper Driver
| ESP32-S3 Pin | TMC2209 Connection | Description |
|--------------|-------------------|-------------|
| GPIO 5       | STEP               | Step signal |
| GPIO 6       | DIR                | Direction signal |
| GPIO 4       | EN                 | Enable pin (optional) |
| GPIO 17      | PDN_UART (TX)      | UART TX to TMC2209 |
| GPIO 18      | PDN_UART (RX)      | UART RX from TMC2209 |
| GND          | GND                | Ground |
| 5V/3.3V      | VIO                | Logic power (check TMC2209 requirements) |

**Note:** TMC2209 UART uses a single-wire bidirectional interface. Connect both RX and TX to the PDN_UART pin on the TMC2209.

## Installation

### Prerequisites

- [PlatformIO](https://platformio.org/) installed
- USB cable for programming and serial monitoring

### Setup Steps

1. **Clone the repository**
   ```bash
   git clone https://github.com/KineMachina/turntable-controller.git
   cd turntable-controller
   ```

2. **Configure WiFi credentials**
   
   **Option A: Via Web UI (Recommended)**
   - After first boot, connect to the device's WiFi access point (if available) or use serial commands
   - Access the web interface and configure WiFi in the MQTT Configuration section
   - Settings are saved to EEPROM automatically
   
   **Option B: Via Code (First Time Setup)**
   - Edit `src/main.cpp` and update the default values in `ConfigurationManager::setDefaults()`
   - Or configure via web UI after initial connection

3. **Adjust pin definitions** (if needed)
   Edit `src/main.cpp` to match your wiring:
   ```cpp
   #define STEP_PIN         5
   #define DIR_PIN          6
   #define ENABLE_PIN       4
   #define TMC_UART_RX      18
   #define TMC_UART_TX      17
   ```

4. **Configure TMC2209 settings**
   ```cpp
   #define TMC_RSENSE       0.11f  // Your sense resistor value
   #define TMC_CURRENT_MA   1200   // Initial current setting (1200mA for higher torque)
   ```

5. **Build and upload**
   ```bash
   pio run -t upload
   ```

6. **Monitor serial output**
   ```bash
   pio device monitor
   ```

## Configuration

### Persistent Configuration Storage

All configuration is stored in EEPROM (NVS) and persists across reboots:

- **WiFi Settings**: SSID and password
- **MQTT Settings**: Broker, port, credentials, topics, QoS, keepalive
- **Motor Settings**: Max speed, acceleration, microsteps, gear ratio
- **TMC2209 Settings**: RMS current, irun, ihold, spreadCycle mode, PWM autoscale, blank time

**Configuration Methods:**
1. **Web UI**: Access `http://<device-ip>/` and use the MQTT Configuration section
2. **REST API**: Use `/mqtt/config` endpoint (see REST_API.md)
3. **MQTT**: Configure via MQTT commands (see MQTT_API.md)
4. **Serial**: Configuration can be viewed via serial commands

**Default Values:**
- WiFi: Configured via web UI or defaults in code
- MQTT: Disabled by default (enable via web UI)
- Max Speed: 2000 steps/sec
- Acceleration: 400 steps/sec²
- Microsteps: 8
- Gear Ratio: 1.0
- TMC2209 Current: 1200mA
- TMC2209 irun/ihold: 31 (100%)

### Motor Parameters

- **Max Speed**: Configurable via API/web UI (default: 2000 steps/sec)
- **Acceleration**: Configurable via API/web UI (default: 400 steps/sec²)
- **Microsteps**: Configurable via API/web UI (default: 8)
- **Gear Ratio**: Configurable via API/web UI (default: 1.0)

### TMC2209 Configuration

The TMC2209 is configured during initialization and settings persist:
- **Microstepping**: Set via API/web UI (default: 8 microsteps)
- **RMS Current**: Configurable via web UI (default: 1200mA)
- **Running Current (irun)**: Configurable via web UI (default: 31 = 100%)
- **Holding Current (ihold)**: Configurable via web UI (default: 31 = 100%)
- **Mode**: StealthChop (quiet) or SpreadCycle (higher torque)
- **PWM Autoscale**: Enabled for StealthChop mode
- All TMC2209 settings are reported in the status API and web UI

## Web Interface

Once connected to WiFi, access the web interface at:
```
http://<esp32-ip-address>
```

The interface provides:
- **Position Control**: Move to target angle in degrees (absolute positioning)
- **Heading Control**: Move to target heading using shortest path (relative movement)
- **Velocity Control**: Set speed in Hz, run forward/backward, stop
- **Motor Control**: Enable/disable motor, reset engine
- **Speed & Acceleration**: Configure max speed and acceleration
- **Microstepping**: Configure microstepping (1-256)
- **Gear Ratio**: Configure gear ratio between stepper and turntable
  - **Dance Effects**: Trigger pre-programmed dance sequences (Twist, Shake, Spin, Wiggle, Watusi, Peppermint Twist)
  - **Behaviors**: Trigger thematic behavior patterns (Scanning, Sleeping, Eating, Alert, Roaring, Stalking, Playing, Resting, Hunting, Victory)
  - **Real-time Status Display**:
  - Free heap memory
  - Stepper position
  - TMC2209 settings (current, mode, PWM autoscale, etc.)
- **Auto-refresh**: Status updates every 2 seconds via AJAX

## API Documentation

### REST API

Complete REST API documentation is available in [REST_API.md](REST_API.md).

**Quick Reference:**
- **Position Control**: Move to target position in degrees (absolute)
- **Heading Control**: Move to target heading using shortest path (relative)
- **Velocity Control**: Set speed in Hz, run forward/backward, stop move, force stop
- **Motor Control**: Enable/disable motor, reset engine
- **Speed & Acceleration**: Configure max speed and acceleration
- **Microstepping**: Configure microstepping (1-256)
- **Gear Ratio**: Configure gear ratio between stepper and turntable
- **MQTT Configuration**: Get/set MQTT settings
- **Dance Effects**: Trigger pre-programmed dance sequences (Twist, Shake, Spin, Wiggle, Watusi, Peppermint Twist) - runs in separate task
- **Behaviors**: Trigger thematic behavior patterns (Scanning, Sleeping, Eating, Alert, Roaring, Stalking, Playing, Resting, Hunting, Victory) - runs in separate task
- **Status**: Get comprehensive system and motor status including TMC2209 settings

### MQTT API

Complete MQTT API documentation is available in [MQTT_API.md](MQTT_API.md).

**Features:**
- **Command Topics**: `{baseTopic}/{deviceId}/command/{commandName}`
- **Status Topics**: `{baseTopic}/{deviceId}/status` and `{baseTopic}/{deviceId}/status/motor`
- **Response Topic**: `{baseTopic}/{deviceId}/response` (immediate response + optional "Move complete" when heading/position move finishes)
- **Online Status**: `{baseTopic}/{deviceId}/status/online` (LWT)
- **All Commands**: Same functionality as REST API
- **Move-complete notification**: For `heading` and `position` commands, a second response is published when the move finishes (`message: "Move complete"`, `event: "complete"`; optional `request_id` echoed from the command payload)
- **Automatic Status Publishing**: Published on state changes and periodically
- **Configurable QoS**: Separate QoS for commands and status

**Default Topics** (configurable):
- Base Topic: `kinemachina/turntable`
- Device ID: `turntable_001`
- Command: `kinemachina/turntable/turntable_001/command/{command}`
- Status: `kinemachina/turntable/turntable_001/status`

### Example: Velocity Control

**Set Speed in Hz**
```http
POST /stepper/speedHz
Content-Type: application/json

{
  "speedHz": 1000.0
}
```

**Start Forward Rotation**
```http
POST /stepper/runForward
Content-Type: application/json

{}
```

**Start Backward Rotation**
```http
POST /stepper/runBackward
Content-Type: application/json

{}
```

See [REST_API.md](REST_API.md) for complete API documentation with all endpoints, request/response formats, and code examples.

## Example Usage

See [REST_API.md](REST_API.md) for comprehensive examples in Python, cURL, and JavaScript.

### Quick Example: Position and Heading Control

```python
import requests

ESP32_IP = "192.168.1.100"
BASE_URL = f"http://{ESP32_IP}"

# Move to absolute position (90 degrees)
requests.post(
    f"{BASE_URL}/stepper/position",
    json={"position": 90.0},
    headers={"Content-Type": "application/json"}
)

# Move to heading using shortest path (relative movement)
requests.post(
    f"{BASE_URL}/stepper/heading",
    json={"heading": 180.0},
    headers={"Content-Type": "application/json"}
)

# Trigger a dance sequence (non-blocking)
requests.post(
    f"{BASE_URL}/stepper/dance",
    json={"danceType": "twist"},
    headers={"Content-Type": "application/json"}
)

# Trigger a behavior (non-blocking)
requests.post(
    f"{BASE_URL}/stepper/behavior",
    json={"behaviorType": "scanning"},
    headers={"Content-Type": "application/json"}
)

# Get comprehensive status (includes TMC2209 settings)
response = requests.get(f"{BASE_URL}/stepper/status")
print(response.json())
```

## Troubleshooting

### Motor Not Moving

1. **Check TMC2209 UART connection**
   - Verify RX/TX pins are connected to PDN_UART
   - Check serial monitor for TMC2209 initialization messages
   - Verify CS_ACTUAL register is being read

2. **Check enable pin**
   - Verify enable pin is connected (or using UART enable)
   - Check if motor is enabled via API: `GET /stepper/status`

3. **Check current settings**
   - Verify current is set appropriately for your motor (default: 1200mA)
   - Check actual current via TMC2209 settings in status API
   - Verify holding current is set (default: 100% for maximum holding torque)

### WiFi Connection Issues

1. **Verify credentials**
   - Check SSID and password in `main.cpp`
   - Ensure 2.4GHz network (ESP32 doesn't support 5GHz)

2. **Check serial output**
   - Monitor serial for connection status
   - Look for IP address assignment

### Compilation Errors

1. **Update PlatformIO**
   ```bash
   pio upgrade
   ```

2. **Clean build**
   ```bash
   pio run -t clean
   pio run
   ```

## Project Structure

```
turntable-controller/
├── platformio.ini          # PlatformIO configuration
├── README.md               # This file
├── REST_API.md             # Complete REST API documentation
├── MQTT_API.md             # Complete MQTT API documentation
├── src/
│   ├── main.cpp            # Main application code (FreeRTOS tasks)
│   ├── StepperMotorController.h/cpp  # Stepper motor control logic
│   ├── HTTPServerController.h/cpp   # HTTP server and REST API (AsyncWebServer)
│   ├── MQTTController.h/cpp         # MQTT client and command processing
│   ├── ConfigurationManager.h/cpp    # Persistent configuration (EEPROM/NVS)
│   ├── MotorCommandQueue.h/cpp      # FreeRTOS command queue
│   └── SerialCommandQueue.h/cpp     # Serial command processing
```

## Architecture

The system uses a FreeRTOS-based task architecture:

- **Motor Control Task** (Core 1, High Priority): Processes motor commands from queue, updates stepper position
- **HTTP Server Task** (Core 0, Medium Priority): Handles HTTP requests using AsyncWebServer (non-blocking)
- **Serial Command Task** (Core 0, Low Priority): Processes serial commands from queue
- **Dance Task** (Core 1, Low Priority): Executes dance sequences in background (non-blocking)
- **Behavior Task** (Core 1, Low Priority): Executes behavior patterns in background (non-blocking)

Commands are queued via FreeRTOS queues for thread-safe execution. Velocity control commands (runForward, runBackward, stopMove, forceStop) execute immediately for low latency. Dance and behavior sequences run in separate tasks to avoid blocking the HTTP server and prevent watchdog timeouts.

**Watchdog Configuration**: Task watchdog timeout is set to 120 seconds (via `CONFIG_ESP_TASK_WDT_TIMEOUT_S=120` in `platformio.ini`) to accommodate long-running operations like dance sequences.

## Dependencies

- **Arduino Framework** (via PlatformIO)
- **TMCStepper Library** (v0.7.3)
- **FastAccelStepper Library** (v0.33.9)
- **ArduinoJson** (v7.0.0)
- **ESPAsyncWebServer** (v3.0.0)
- **AsyncTCP** (v1.1.1)
- **AsyncMqttClient** (v0.9.0)

## License

This project is licensed under the MIT License - see the [LICENSE](LICENSE) file for details.

## Contributing

Contributions are welcome! Please see [CONTRIBUTING.md](CONTRIBUTING.md) for guidelines.

## Acknowledgments

- FastAccelStepper library by gin66
- TMCStepper library by teemuatlut
