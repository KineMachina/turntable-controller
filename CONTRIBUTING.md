# Contributing to KineMachina Turntable Controller

Thanks for your interest in contributing! Whether you're reporting a bug, suggesting a feature, or submitting code, your help is welcome and appreciated.

## Getting Started

### Prerequisites

- [PlatformIO](https://platformio.org/) installed (CLI or IDE extension)
- ESP32-S3 DevKitC-1 board
- TMC2209 stepper driver and a stepper motor

If you're new to PlatformIO, the [VS Code extension](https://platformio.org/install/ide?install=vscode) is the easiest way to get up and running.

## Building and Testing

Build the firmware:

```bash
pio run
```

Flash to your board via USB:

```bash
pio run -t upload
```

Open the serial monitor (115200 baud):

```bash
pio device monitor
```

### WiFi Configuration

No WiFi credentials are hardcoded in the firmware. After flashing, configure your WiFi credentials using one of these methods:

- **Web UI** -- Connect to the access point the device creates on first boot, then enter your network details through the configuration page.
- **Serial interface** -- Send WiFi configuration commands over the serial connection.

## Submitting Issues

Good bug reports help us fix problems faster. When opening an issue, please include:

- **Hardware setup** -- Which ESP32-S3 board, TMC2209 module, stepper motor model, and wiring details.
- **Serial monitor output** -- Copy and paste the relevant serial output at 115200 baud. This almost always contains useful diagnostic information.
- **Steps to reproduce** -- What commands or actions triggered the problem, in what order.
- **Expected vs. actual behavior** -- What you expected to happen and what happened instead.

For feature requests, describe the use case and how you envision it working.

## Submitting Pull Requests

1. **Fork the repo** and create a feature branch from `main` (e.g., `feature/add-stall-detection` or `fix/mqtt-reconnect`).
2. **Test on actual hardware** before submitting. This is an embedded project and behavior can differ significantly from what compiles cleanly. If you don't have the exact hardware, mention that in the PR description.
3. **Describe what changed and why** in the PR description. A sentence or two about the motivation goes a long way.
4. **Keep PRs focused** -- one feature or fix per PR. Smaller, focused PRs are easier to review and merge.

## Code Style

Follow the conventions already used in the codebase:

- **PascalCase** for class names (e.g., `StepperMotorController`, `ConfigurationManager`)
- **camelCase** for methods and variables (e.g., `moveToPosition`, `currentSpeed`)
- **FreeRTOS task naming** -- task functions are named descriptively with a `Task` suffix (e.g., `MotorControlTask`, `MQTTTask`)
- When in doubt, match the patterns you see in surrounding code

## License

This project is licensed under the [MIT License](LICENSE). By submitting a pull request, you agree that your contributions will be licensed under the same terms.
