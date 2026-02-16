# KineMachina Turntable Controller - Product Specification

## Product Overview

The KineMachina Turntable Controller is a high-performance, feature-rich motor control system designed for precise turntable and rotational control applications. Built on the ESP32-S3 platform, it combines advanced stepper motor control, web-based management, and MQTT integration in a single, integrated solution.

### Target Applications

- **Animatronic Displays**: Turntable control for animatronic figures and displays
- **Robotic Platforms**: Precise rotational control for robotic systems
- **Automated Displays**: Rotating displays and exhibits
- **Camera Gimbals**: Pan/tilt control systems
- **Industrial Automation**: Precise positioning applications
- **Interactive Installations**: Museum exhibits and interactive displays

## Key Features

### 1. High-Performance Motor Control

- **TMC2209 Stepper Driver Integration**
  - UART-based configuration and monitoring
  - Microstepping support (1-256 steps)
  - StealthChop mode for quiet operation
  - SpreadCycle mode for higher torque
  - Configurable current settings (RMS, running, holding)
  - Real-time driver status monitoring

- **FastAccelStepper Library**
  - High-frequency step generation (up to 2000+ steps/sec)
  - Smooth acceleration/deceleration profiles
  - Configurable motion parameters
  - Non-blocking operation

### 2. Position Management

- Absolute position control (0-360 degrees)
- Relative heading control with shortest path calculation
- Gear ratio compensation for turntable applications

### 3. Multiple Control Interfaces

- **Web-Based Interface**
  - Modern, responsive HTML5 interface
  - Real-time status monitoring
  - Visual controls for all functions
  - Configuration management
  - No additional software required

- **REST API**
  - Complete JSON-based API
  - Standard HTTP methods (GET, POST)
  - Comprehensive status endpoints
  - Integration with external systems
  - See [REST_API.md](REST_API.md)

- **MQTT Integration**
  - Publish/subscribe architecture
  - Command/response pattern
  - Automatic status publishing
  - Move-complete notification: second response when heading/position move finishes (`event: "complete"`, optional `request_id` echo)
  - Online/offline status (LWT)
  - Configurable QoS levels
  - See [MQTT_API.md](MQTT_API.md)

- **Serial Interface**
  - Command-line interface
  - Status queries
  - Debugging and diagnostics

### 4. Advanced Motion Features

- **Dance Sequences**
  - Pre-programmed rotation patterns
  - 6 dance types: Twist, Shake, Spin, Wiggle, Watusi, Peppermint Twist
  - Non-blocking execution
  - Can be stopped mid-sequence

- **Behavior Patterns**
  - Thematic movement sequences
  - 10 behavior types: Scanning, Sleeping, Eating, Alert, Roaring, Stalking, Playing, Resting, Hunting, Victory
  - Realistic motion profiles
  - Background task execution

### 5. Configuration Management

- **Persistent Storage**
  - EEPROM (NVS) storage for all settings
  - Automatic save on configuration changes
  - Configuration loaded on boot
  - Version management for future migrations

- **Configurable Parameters**
  - WiFi credentials
  - MQTT broker settings
  - Motor parameters (speed, acceleration, microsteps, gear ratio)
  - TMC2209 driver settings

### 6. System Architecture

- **FreeRTOS-Based**
  - Multi-core task execution
  - Priority-based scheduling
  - Thread-safe command queues
  - Non-blocking operations

- **Task Organization**
  - Motor Control Task (Core 1, High Priority)
  - HTTP Server Task (Core 0, Medium Priority)
  - MQTT Task (Core 0, Medium Priority)
  - Serial Command Task (Core 0, Low Priority)
  - Dance/Behavior Tasks (Core 1, Low Priority)

## Technical Specifications

### Hardware Requirements

**Controller Board:**
- ESP32-S3 DevKit C-1 (or compatible)
- Dual-core processor (240MHz)
- WiFi 802.11 b/g/n (2.4GHz)
- USB-C for programming and power

**Motor Driver:**
- TMC2209 Stepper Driver
- UART interface capability
- Compatible with 24V motor power supply

**Motor:**
- NEMA stepper motor (compatible with TMC2209)
- 1.8° step angle (200 steps/revolution)
- Current rating compatible with TMC2209

### Pin Assignments

| Function | ESP32-S3 Pin | Description |
|----------|--------------|-------------|
| Step | GPIO 5 | Step signal to TMC2209 |
| Direction | GPIO 6 | Direction signal to TMC2209 |
| Enable | GPIO 4 | Enable signal (optional) |
| TMC UART TX | GPIO 17 | UART TX to TMC2209 PDN_UART |
| TMC UART RX | GPIO 18 | UART RX from TMC2209 PDN_UART |

### Performance Specifications

**Motion Performance:**
- Maximum Speed: Up to 2000 steps/second (configurable)
- Acceleration: Up to 400 steps/second² (configurable)
- Position Accuracy: Dependent on microstepping configuration
- Update Rate: 100Hz (10ms motor control loop)

**Communication:**
- WiFi: 802.11 b/g/n (2.4GHz only)
- HTTP Server: AsyncWebServer (non-blocking)
- MQTT: AsyncMqttClient (non-blocking)
- Serial: 115200 baud

**System Resources:**
- FreeRTOS Tasks: 5+ concurrent tasks
- Command Queue Size: 20 commands
- Watchdog Timeout: 120 seconds
- Stack Sizes: 2KB-8KB per task

### Default Configuration

**Motor Settings:**
- Max Speed: 2000 steps/sec
- Acceleration: 400 steps/sec²
- Microstepping: 8
- Gear Ratio: 1.0 (configurable)

**TMC2209 Settings:**
- RMS Current: 1200mA
- Running Current (irun): 31 (100% of RMS)
- Holding Current (ihold): 31 (100% of running)
- Mode: StealthChop (quiet operation)
- PWM Autoscale: Enabled
- Blank Time: 24

**Network Settings:**
- WiFi: Configured via web UI or defaults
- HTTP Port: 80
- MQTT: Disabled by default (enable via web UI)
- MQTT Port: 1883 (default)
- MQTT QoS Commands: 1
- MQTT QoS Status: 0
- MQTT Keepalive: 60 seconds

## Functional Capabilities

### Position Control

1. **Absolute Position Control**
   - Move to specific turntable angle (0-360 degrees)
   - Accounts for gear ratio

2. **Relative Heading Control**
   - Move to target heading using shortest path
   - Automatically calculates optimal rotation direction
   - Wraps around 0/360 degrees correctly

3. **Zero/Home Operations**
   - Zero stepper position
   - Home to position 0

### Velocity Control

1. **Continuous Rotation**
   - Forward rotation at configurable speed
   - Backward rotation at configurable speed
   - Speed control in Hz (steps per second)

2. **Stop Operations**
   - Graceful stop (decelerates)
   - Force stop (immediate stop)

### Configuration Control

1. **Motor Parameters**
   - Maximum speed adjustment
   - Acceleration adjustment
   - Microstepping configuration
   - Gear ratio setting

2. **Driver Settings**
   - Current limits
   - Operating mode (StealthChop/SpreadCycle)
   - PWM settings

### Entertainment Features

1. **Dance Sequences**
   - 6 pre-programmed dance patterns
   - Non-blocking execution
   - Interruptible

2. **Behavior Patterns**
   - 10 thematic behaviors
   - Realistic motion profiles
   - Background execution

## Software Architecture

### Core Components

1. **StepperMotorController**
   - Motor control logic
   - Position management
   - Dance/behavior execution

2. **HTTPServerController**
   - Web interface
   - REST API endpoints
   - Configuration management

3. **MQTTController**
   - MQTT client management
   - Command processing
   - Status publishing

4. **ConfigurationManager**
   - EEPROM/NVS storage
   - Configuration persistence
   - Default value management

5. **Command Queues**
   - MotorCommandQueue: Thread-safe motor commands
   - SerialCommandQueue: Serial command processing

### Communication Protocols

1. **HTTP/REST**
   - JSON request/response format
   - Standard HTTP methods
   - AsyncWebServer implementation

2. **MQTT**
   - JSON payload format
   - Topic-based routing
   - Publish/subscribe pattern

3. **Serial**
   - Text-based commands
   - Status reporting
   - Debug output

## User Interfaces

### Web Interface

- **Status Display**
  - Real-time motor status
  - System information
  - TMC2209 settings

- **Control Panels**
  - Position control
  - Heading control
  - Velocity control
  - Motor enable/disable
  - Configuration settings

- **Entertainment Controls**
  - Dance selection buttons
  - Behavior selection buttons
  - Stop controls

- **Configuration**
  - MQTT settings
  - Motor parameters
  - System settings

### API Interfaces

- **REST API**: 20+ endpoints for complete control
- **MQTT API**: 19 command topics + status topics
- **Serial API**: Command-line interface

## Configuration and Setup

### Initial Setup

1. **Hardware Assembly**
   - Connect TMC2209 to ESP32-S3
   - Connect stepper motor
   - Power supply connection

2. **Software Installation**
   - Install PlatformIO
   - Clone repository
   - Build and upload firmware

3. **Network Configuration**
   - Connect to WiFi (via web UI or defaults)
   - Configure MQTT (optional, via web UI)
   - Access web interface

4. **Calibration**
   - Configure gear ratio
   - Test position accuracy

### Configuration Methods

1. **Web UI**: Primary method for all settings
2. **REST API**: Programmatic configuration
3. **MQTT**: Remote configuration
4. **EEPROM**: Persistent storage (automatic)

## Use Cases

### Animatronic Displays

- **Primary Use Case**: Turntable control for animatronic figures and displays
- **Requirements**: Precise positioning, smooth motion, entertainment features
- **Features Used**: Position control, dance sequences, behavior patterns

### Robotic Platforms

- **Use Case**: Pan/tilt control for robotic systems
- **Requirements**: High accuracy, remote control
- **Features Used**: Position control, MQTT integration, configuration persistence

### Automated Displays

- **Use Case**: Rotating displays and exhibits
- **Requirements**: Smooth motion, programmable sequences, remote control
- **Features Used**: Position control, behavior patterns, web interface

### Camera Gimbals

- **Use Case**: Pan/tilt control for camera systems
- **Requirements**: Precise positioning, smooth motion
- **Features Used**: Position control, heading control, velocity control

## Limitations and Considerations

### Hardware Limitations

- **WiFi**: 2.4GHz only (no 5GHz support)
- **Power**: Requires adequate power supply for motor
- **UART**: TMC2209 uses single-wire bidirectional UART

### Software Limitations

- **Command Queue**: Limited to 20 queued commands
- **Watchdog**: 120-second timeout (may affect very long sequences)
- **Memory**: Limited by ESP32-S3 RAM
- **Concurrent Operations**: Only one dance or behavior at a time

### Operational Considerations

- **MQTT**: Must be enabled via web UI before use
- **Configuration**: Changes require reboot for some settings (first-time MQTT setup)

## Future Enhancements

### Potential Features

- PID control for position accuracy
- Trajectory planning
- Multi-motor support
- OTA (Over-The-Air) updates
- WebSocket support for real-time updates
- Advanced diagnostics and logging
- Configuration import/export
- Preset configurations
- Scheduled operations

### Performance Improvements

- Higher update rates
- Lower latency command processing
- Optimized memory usage
- Enhanced error recovery

## Documentation

- **README.md**: Getting started guide
- **REST_API.md**: Complete REST API documentation
- **MQTT_API.md**: Complete MQTT API documentation
- **PRODUCT_SPEC.md**: This document

## Support and Maintenance

### Troubleshooting

- Serial monitor for debugging
- Status endpoints for diagnostics
- Web UI for configuration verification
- MQTT status topics for remote monitoring

### Updates

- Firmware updates via PlatformIO
- Configuration preserved across updates
- Version management in EEPROM

## Compliance and Standards

- **WiFi**: IEEE 802.11 b/g/n
- **MQTT**: MQTT 3.1.1 protocol
- **HTTP**: HTTP/1.1
- **JSON**: RFC 7159
- **I2C**: Standard I2C protocol
- **UART**: Standard UART protocol

## Version History

- **v1.0**: Initial release with REST API, web interface, MQTT support, and persistent configuration
