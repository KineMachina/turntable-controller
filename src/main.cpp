#include "Arduino.h"
#include "StepperMotorController.h"
#include "HTTPServerController.h"
#include "MotorCommandQueue.h"
#include "SerialCommandQueue.h"
#include "MQTTController.h"
#include "ConfigurationManager.h"
#include "OLEDDisplayController.h"
#include <FreeRTOS.h>
#include <task.h>
#include <string.h>
#include "RuntimeLog.h"

static const char* TAG = "Main";

// Runtime log level — checked by ESP_LOGx macros via RuntimeLog.h
esp_log_level_t runtimeLogLevel = ESP_LOG_INFO;

// Stepper motor pins (adjust to your wiring)
#define STEP_PIN 5
#define DIR_PIN 6
#define ENABLE_PIN 4
#define TMC_UART_RX 18     // TMC2209 UART RX (PDN_UART)
#define TMC_UART_TX 17     // TMC2209 UART TX (PDN_UART)
#define TMC_RSENSE 0.11f   // TMC2209 sense resistor (ohms)

// WiFi Configuration (defaults - will be overridden by loaded config)
#define HTTP_PORT 80

// Configuration Manager
ConfigurationManager configManager;

// Controller instances
StepperMotorController stepperController(STEP_PIN, DIR_PIN, ENABLE_PIN,
                                         2000.0f, 400.0f,
                                         TMC_UART_RX, TMC_UART_TX, TMC_RSENSE, 0);

// HTTPServerController will be configured after loading config
HTTPServerController* httpServer = nullptr;
MotorCommandQueue motorCommandQueue;
SerialCommandQueue serialCommandQueue;
MQTTController mqttController;
OLEDDisplayController oledDisplay;

// FreeRTOS Task Handles
TaskHandle_t motorControlTaskHandle = nullptr;
TaskHandle_t httpServerTaskHandle = nullptr;
TaskHandle_t serialReadTaskHandle = nullptr;    // Reads from Serial and queues commands
TaskHandle_t serialCommandTaskHandle = nullptr; // Processes queued commands
TaskHandle_t oledDisplayTaskHandle = nullptr;

// Forward declarations for task functions
void motorControlTask(void *parameter);
void httpServerTask(void *parameter);
void serialReadTask(void *parameter);    // Reads serial input and queues commands
void serialCommandTask(void *parameter); // Processes queued commands
void oledDisplayTask(void *parameter);

// Task Stack Sizes
#define MOTOR_CONTROL_TASK_STACK_SIZE 4096
#define HTTP_SERVER_TASK_STACK_SIZE 8192
#define SERIAL_READ_TASK_STACK_SIZE 2048    // Task that reads serial input
#define SERIAL_COMMAND_TASK_STACK_SIZE 4096 // Task that processes commands
#define OLED_DISPLAY_TASK_STACK_SIZE 4096

// Task Priorities
#define MOTOR_CONTROL_TASK_PRIORITY 3  // High priority for motor control
#define HTTP_SERVER_TASK_PRIORITY 2    // Medium priority for HTTP
#define SERIAL_READ_TASK_PRIORITY 2    // Medium priority for serial reading
#define SERIAL_COMMAND_TASK_PRIORITY 1 // Low priority for serial command processing
#define OLED_DISPLAY_TASK_PRIORITY 1   // Low priority for display updates

void setup()
{
    Serial.begin(115200);
    Serial.setTxBufferSize(1024);
    delay(1000);

    // Note: Task watchdog timeout is configured via build flag CONFIG_ESP_TASK_WDT_TIMEOUT_S=120
    // in platformio.ini. This gives HTTP handlers and dance sequences more time to complete.

    ESP_LOGI(TAG, "=== ESP32-S3 Stepper Motor Controller ===");
    ESP_LOGI(TAG, "TMC2209 Stepper Motor Controller");

    // Load configuration from NVS
    ESP_LOGI(TAG, "Loading configuration from NVS...");
    bool configLoaded = configManager.load();
    if (configLoaded) {
        ESP_LOGI(TAG, "Configuration loaded successfully");
    } else {
        ESP_LOGI(TAG, "Using default configuration (no saved config found)");
    }
    const SystemConfig& config = configManager.getConfig();

    ESP_LOGI(TAG, "Initializing Stepper Motor Controller...");
    if (!stepperController.begin(&config))
    {
        ESP_LOGE(TAG, "Stepper motor initialization failed. Continuing without stepper.");
    }

    // Enable debug logging for stepper
    stepperController.setDebugLogging(true);
    ESP_LOGI(TAG, "Stepper and PID debug logging enabled");

    // Initialize command queue
    if (!motorCommandQueue.begin())
    {
        ESP_LOGE(TAG, "Failed to initialize motor command queue");
    }
    else
    {
        ESP_LOGI(TAG, "Motor command queue initialized");
    }

    // Initialize HTTP Server (includes WiFi) - use config values
    httpServer = new HTTPServerController(config.wifiSSID, config.wifiPassword, HTTP_PORT);
    httpServer->setDeviceId(config.mqttDeviceId);
    if (httpServer->begin(&stepperController, &motorCommandQueue, &configManager, &mqttController))
    {
        ESP_LOGI(TAG, "Web interface available at: http://%s", httpServer->getIPAddress().c_str());
        httpServer->printEndpoints();
    }
    else
    {
        ESP_LOGW(TAG, "HTTP server initialization failed - continuing without network control");
        ESP_LOGI(TAG, "Serial commands still available");
    }

    // Initialize MQTT Controller (requires WiFi) - use config values
    if (httpServer->isConnected())
    {
        // Configure MQTT from saved configuration
        MQTTConfig mqttConfig;
        mqttConfig.enabled = config.mqttEnabled;
        mqttConfig.broker = config.mqttBroker;
        mqttConfig.port = config.mqttPort;
        mqttConfig.username = config.mqttUsername;
        mqttConfig.password = config.mqttPassword;
        mqttConfig.deviceId = config.mqttDeviceId;
        mqttConfig.baseTopic = config.mqttBaseTopic;
        mqttConfig.qosCommands = config.mqttQosCommands;
        mqttConfig.qosStatus = config.mqttQosStatus;
        mqttConfig.keepalive = config.mqttKeepalive;
        
        if (mqttController.begin(&stepperController, &motorCommandQueue, &mqttConfig, &configManager))
        {
            ESP_LOGI(TAG, "MQTT controller initialized");
        }
        else
        {
            ESP_LOGW(TAG, "MQTT controller initialization failed or disabled - continuing without MQTT");
        }
    }
    else
    {
        ESP_LOGW(TAG, "MQTT controller skipped - WiFi not connected");
    }

    // Initialize OLED display
    if (oledDisplay.begin(&stepperController, httpServer, &mqttController, &configManager))
    {
        ESP_LOGI(TAG, "OLED display initialized");
    }
    else
    {
        ESP_LOGW(TAG, "OLED display initialization failed - continuing without display");
    }

    // Create FreeRTOS tasks
    ESP_LOGI(TAG, "Creating FreeRTOS tasks...");

    // Motor Control Task - High priority for real-time control
    xTaskCreatePinnedToCore(
        motorControlTask,              // Task function
        "MotorControl",                // Task name
        MOTOR_CONTROL_TASK_STACK_SIZE, // Stack size
        nullptr,                       // Parameters
        MOTOR_CONTROL_TASK_PRIORITY,   // Priority
        &motorControlTaskHandle,       // Task handle
        1                              // Core 1 (dedicated to motor control)
    );
    ESP_LOGI(TAG, "Motor control task created on Core 1");

    // HTTP Server Task - Medium priority
    xTaskCreatePinnedToCore(
        httpServerTask,              // Task function
        "HTTPServer",                // Task name
        HTTP_SERVER_TASK_STACK_SIZE, // Stack size
        nullptr,                     // Parameters
        HTTP_SERVER_TASK_PRIORITY,   // Priority
        &httpServerTaskHandle,       // Task handle
        0                            // Core 0
    );
    ESP_LOGI(TAG, "HTTP server task created on Core 0");

    // Initialize serial command queue
    if (!serialCommandQueue.begin())
    {
        ESP_LOGE(TAG, "Failed to initialize serial command queue");
    }
    else
    {
        ESP_LOGI(TAG, "Serial command queue initialized");
    }

    // Serial Read Task - Medium priority (reads from Serial and queues commands)
    xTaskCreatePinnedToCore(
        serialReadTask,              // Task function
        "SerialRead",                // Task name
        SERIAL_READ_TASK_STACK_SIZE, // Stack size
        nullptr,                     // Parameters
        SERIAL_READ_TASK_PRIORITY,   // Priority
        &serialReadTaskHandle,       // Task handle
        0                            // Core 0
    );
    ESP_LOGI(TAG, "Serial read task created on Core 0");

    // Serial Command Task - Low priority (processes queued commands)
    xTaskCreatePinnedToCore(
        serialCommandTask,              // Task function
        "SerialCommand",                // Task name
        SERIAL_COMMAND_TASK_STACK_SIZE, // Stack size
        nullptr,                        // Parameters
        SERIAL_COMMAND_TASK_PRIORITY,   // Priority
        &serialCommandTaskHandle,       // Task handle
        0                               // Core 0
    );
    ESP_LOGI(TAG, "Serial command task created on Core 0");

    // OLED Display Task - Low priority
    xTaskCreatePinnedToCore(
        oledDisplayTask,              // Task function
        "OLEDDisplay",                // Task name
        OLED_DISPLAY_TASK_STACK_SIZE, // Stack size
        nullptr,                      // Parameters
        OLED_DISPLAY_TASK_PRIORITY,   // Priority
        &oledDisplayTaskHandle,       // Task handle
        0                             // Core 0
    );
    ESP_LOGI(TAG, "OLED display task created on Core 0");
    ESP_LOGI(TAG, "All tasks created successfully");

    Serial.println("\nType 'help' for serial commands.");
    Serial.println();
}

// FreeRTOS Task Functions

/**
 * Motor Control Task
 * Runs on Core 1 with high priority
 * Processes command queue and updates motor control loop
 */
void motorControlTask(void *parameter)
{
    ESP_LOGI(TAG, "MotorControlTask started on Core 1");

    const TickType_t updateInterval = pdMS_TO_TICKS(10); // 10ms = 100Hz update rate
    TickType_t lastWakeTime = xTaskGetTickCount();

    while (true)
    {
        // Process pending commands from queue
        stepperController.processCommandQueue(&motorCommandQueue);

        // Update motor control loop
        stepperController.update();

        // Wait for next cycle (100Hz = 10ms interval)
        vTaskDelayUntil(&lastWakeTime, updateInterval);
    }
}

/**
 * HTTP Server Task
 * Runs on Core 0 with medium priority
 * Handles incoming HTTP requests
 */
void httpServerTask(void *parameter)
{
    ESP_LOGI(TAG, "HTTPServerTask started on Core 0");

    const TickType_t updateInterval = pdMS_TO_TICKS(10); // 10ms update interval

    while (true)
    {
        // AsyncWebServer handles requests automatically, no update() needed
        // Yield to other tasks
        vTaskDelay(updateInterval);
    }
}

/**
 * Serial Read Task
 * Runs on Core 0 with medium priority
 * Reads from Serial and queues commands (non-blocking, efficient)
 */
void serialReadTask(void *parameter)
{
    ESP_LOGI(TAG, "SerialReadTask started on Core 0");

    const TickType_t readInterval = pdMS_TO_TICKS(50); // Check every 50ms
    char cmdBuffer[SerialCommandQueue::MAX_CMD_LENGTH];
    int cmdLen = 0;

    while (true)
    {
        // Read characters from Serial
        while (Serial.available() && cmdLen < (SerialCommandQueue::MAX_CMD_LENGTH - 1))
        {
            char c = Serial.read();

            if (c == '\n' || c == '\r')
            {
                // End of command
                if (cmdLen > 0)
                {
                    cmdBuffer[cmdLen] = '\0';

                    // Trim whitespace from end
                    while (cmdLen > 0 && (cmdBuffer[cmdLen - 1] == ' ' || cmdBuffer[cmdLen - 1] == '\t'))
                    {
                        cmdBuffer[--cmdLen] = '\0';
                    }

                    // Convert to lowercase
                    for (int i = 0; i < cmdLen; i++)
                    {
                        if (cmdBuffer[i] >= 'A' && cmdBuffer[i] <= 'Z')
                        {
                            cmdBuffer[i] = cmdBuffer[i] + 32;
                        }
                    }

                    // Queue the command
                    if (cmdLen > 0)
                    {
                        serialCommandQueue.sendCommand(cmdBuffer, 0);
                    }

                    cmdLen = 0; // Reset for next command
                }
            }
            else if (c >= 32 && c < 127)
            { // Printable ASCII only
                // Add character to buffer
                cmdBuffer[cmdLen++] = c;
            }
            // Ignore other characters (control chars, etc.)
        }

        // Yield to other tasks
        vTaskDelay(readInterval);
    }
}

/**
 * Serial Command Task
 * Runs on Core 0 with low priority
 * Processes queued serial commands (blocking wait on queue)
 */
void serialCommandTask(void *parameter)
{
    ESP_LOGI(TAG, "SerialCommandTask started on Core 0");

    const TickType_t heartbeatInterval = pdMS_TO_TICKS(10000); // 10 seconds
    TickType_t lastHeartbeat = xTaskGetTickCount();
    char cmdBuffer[SerialCommandQueue::MAX_CMD_LENGTH];

    while (true)
    {
        // Wait for command from queue (blocking, efficient)
        if (serialCommandQueue.receiveCommand(cmdBuffer, sizeof(cmdBuffer), heartbeatInterval))
        {
            // Helper lambda for queuing motor commands
            auto queueMotorCmd = [](MotorCommand& cmd) -> bool {
                cmd.statusCallback = nullptr;
                cmd.statusContext = nullptr;
                return motorCommandQueue.sendCommand(cmd, pdMS_TO_TICKS(100));
            };

            // Process command
            // --- Status commands ---
            if (strcmp(cmdBuffer, "status") == 0)
            {
                Serial.println("========================================");
                Serial.printf("Free Heap: %u bytes | Uptime: %lu ms\n", ESP.getFreeHeap(), millis());
                if (httpServer != nullptr && httpServer->isConnected())
                {
                    Serial.printf("WiFi: %s  IP: %s  RSSI: %d dBm\n",
                        WiFi.SSID().c_str(), httpServer->getIPAddress().c_str(), WiFi.RSSI());
                }
                else
                {
                    Serial.println("WiFi: Disconnected");
                }
                Serial.flush();
                vTaskDelay(pdMS_TO_TICKS(20));
                Serial.printf("Position: %.2f deg (%ld steps)  Heading: %.2f deg\n",
                    stepperController.getStepperPositionDegrees(),
                    stepperController.getStepperPosition(),
                    stepperController.getStepperPositionDegrees());
                Serial.printf("Enabled: %s  Running: %s  Microsteps: %u  Gear: %.2f:1\n",
                    stepperController.isEnabled() ? "Yes" : "No",
                    stepperController.isRunning() ? "Yes" : "No",
                    stepperController.getMicrosteps(),
                    stepperController.getGearRatio());
                Serial.printf("Dance: %s  Behavior: %s\n",
                    stepperController.isDanceInProgress() ? "Yes" : "No",
                    stepperController.isBehaviorInProgress() ? "Yes" : "No");
                {
                    const SystemConfig& cfg = configManager.getConfig();
                    Serial.printf("MQTT: %s (%s)  Device: %s\n",
                        cfg.mqttEnabled ? "Enabled" : "Disabled",
                        mqttController.isConnected() ? "Connected" : "Disconnected",
                        cfg.mqttDeviceId);
                }
                Serial.println("========================================");
                Serial.flush();
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            else if (strcmp(cmdBuffer, "statusfull") == 0)
            {
                Serial.println("========================================");
                Serial.println("Full System Status");
                Serial.println("========================================");
                Serial.printf("Free Heap: %u bytes | Uptime: %lu ms\n", ESP.getFreeHeap(), millis());
                Serial.flush();
                vTaskDelay(pdMS_TO_TICKS(20));

                // WiFi
                if (httpServer != nullptr && httpServer->isConnected())
                {
                    Serial.printf("WiFi: %s  IP: %s\n", WiFi.SSID().c_str(), httpServer->getIPAddress().c_str());
                    Serial.printf("  Gateway: %s  Subnet: %s\n",
                        WiFi.gatewayIP().toString().c_str(), WiFi.subnetMask().toString().c_str());
                    Serial.printf("  DNS: %s  RSSI: %d dBm  MAC: %s\n",
                        WiFi.dnsIP().toString().c_str(), WiFi.RSSI(), WiFi.macAddress().c_str());
                }
                else
                {
                    Serial.println("WiFi: Disconnected");
                }
                Serial.flush();
                vTaskDelay(pdMS_TO_TICKS(20));

                // Motor
                Serial.printf("Position: %.2f deg (%ld steps)\n",
                    stepperController.getStepperPositionDegrees(),
                    stepperController.getStepperPosition());
                Serial.printf("Enabled: %s  Running: %s  Microsteps: %u  Gear: %.2f:1\n",
                    stepperController.isEnabled() ? "Yes" : "No",
                    stepperController.isRunning() ? "Yes" : "No",
                    stepperController.getMicrosteps(),
                    stepperController.getGearRatio());
                Serial.flush();
                vTaskDelay(pdMS_TO_TICKS(20));

                // TMC2209
                Serial.printf("TMC RMS Current: %u mA  Actual: %.2f mA\n",
                    stepperController.getTmcRmsCurrent(), stepperController.getTmcActualCurrent());
                Serial.flush();
                vTaskDelay(pdMS_TO_TICKS(20));
                Serial.printf("TMC IRUN: %u  IHOLD: %u  CS Actual: %u\n",
                    stepperController.getTmcIrun(), stepperController.getTmcIhold(),
                    stepperController.getTmcCsActual());
                Serial.flush();
                vTaskDelay(pdMS_TO_TICKS(20));
                Serial.printf("TMC Mode: %s  PWM Autoscale: %s  Blank Time: %u\n",
                    stepperController.getTmcSpreadCycle() ? "SpreadCycle" : "StealthChop",
                    stepperController.getTmcPwmAutoscale() ? "On" : "Off",
                    stepperController.getTmcBlankTime());
                Serial.flush();
                vTaskDelay(pdMS_TO_TICKS(20));

                // Dance/Behavior
                Serial.printf("Dance: %s  Behavior: %s\n",
                    stepperController.isDanceInProgress() ? "Yes" : "No",
                    stepperController.isBehaviorInProgress() ? "Yes" : "No");

                // MQTT
                {
                    const SystemConfig& cfg = configManager.getConfig();
                    Serial.printf("MQTT: %s (%s)\n",
                        cfg.mqttEnabled ? "Enabled" : "Disabled",
                        mqttController.isConnected() ? "Connected" : "Disconnected");
                    Serial.printf("  Broker: %s:%u  Device: %s\n",
                        cfg.mqttBroker, cfg.mqttPort, cfg.mqttDeviceId);
                }

                Serial.printf("Motor Queue: %u  Serial Queue: %u\n",
                    motorCommandQueue.getCount(), serialCommandQueue.getCount());
                Serial.println("========================================");
                Serial.flush();
                vTaskDelay(pdMS_TO_TICKS(20));
            }
            // --- Motion commands ---
            else if (strncmp(cmdBuffer, "position ", 9) == 0)
            {
                float deg = atof(cmdBuffer + 9);
                MotorCommand cmd;
                cmd.type = MotorCommandType::MOVE_TO;
                cmd.data.position.value = deg;
                if (queueMotorCmd(cmd))
                    Serial.printf("Moving to %.2f deg\n", deg);
                else
                    Serial.println("ERROR: Queue full");
            }
            else if (strncmp(cmdBuffer, "heading ", 8) == 0)
            {
                float deg = atof(cmdBuffer + 8);
                stepperController.moveToHeadingDegrees(deg);
                Serial.printf("Heading to %.2f deg (shortest path)\n", deg);
            }
            else if (strcmp(cmdBuffer, "home") == 0)
            {
                MotorCommand cmd;
                cmd.type = MotorCommandType::HOME;
                if (queueMotorCmd(cmd))
                    Serial.println("Homing to 0 deg");
                else
                    Serial.println("ERROR: Queue full");
            }
            else if (strcmp(cmdBuffer, "zero") == 0)
            {
                MotorCommand cmd;
                cmd.type = MotorCommandType::ZERO_POSITION;
                if (queueMotorCmd(cmd))
                    Serial.println("Position zeroed");
                else
                    Serial.println("ERROR: Queue full");
            }
            else if (strcmp(cmdBuffer, "forward") == 0)
            {
                stepperController.runForward();
                Serial.println("Running forward");
            }
            else if (strcmp(cmdBuffer, "backward") == 0)
            {
                stepperController.runBackward();
                Serial.println("Running backward");
            }
            else if (strcmp(cmdBuffer, "stop") == 0)
            {
                stepperController.stopMove();
                Serial.println("Stopping (decelerate)");
            }
            else if (strcmp(cmdBuffer, "forcestop") == 0)
            {
                stepperController.stopMove();
                stepperController.enable(false);
                Serial.println("Emergency stop — motor disabled");
            }
            // --- Motor config commands ---
            else if (strncmp(cmdBuffer, "speed ", 6) == 0)
            {
                float val = atof(cmdBuffer + 6);
                if (val > 0) {
                    MotorCommand cmd;
                    cmd.type = MotorCommandType::SET_SPEED;
                    cmd.data.speed.speed = val;
                    if (queueMotorCmd(cmd))
                        Serial.printf("Speed set to %.0f steps/sec\n", val);
                    else
                        Serial.println("ERROR: Queue full");
                } else {
                    Serial.println("ERROR: Speed must be > 0");
                }
            }
            else if (strncmp(cmdBuffer, "accel ", 6) == 0)
            {
                float val = atof(cmdBuffer + 6);
                if (val > 0) {
                    MotorCommand cmd;
                    cmd.type = MotorCommandType::SET_ACCELERATION;
                    cmd.data.acceleration.accel = val;
                    if (queueMotorCmd(cmd))
                        Serial.printf("Acceleration set to %.0f steps/sec^2\n", val);
                    else
                        Serial.println("ERROR: Queue full");
                } else {
                    Serial.println("ERROR: Acceleration must be > 0");
                }
            }
            else if (strncmp(cmdBuffer, "microsteps ", 11) == 0)
            {
                int val = atoi(cmdBuffer + 11);
                if (val >= 1 && val <= 256 && (val & (val - 1)) == 0) {
                    MotorCommand cmd;
                    cmd.type = MotorCommandType::SET_MICROSTEPS;
                    cmd.data.microsteps.microsteps = (uint8_t)val;
                    if (queueMotorCmd(cmd))
                        Serial.printf("Microsteps set to %d\n", val);
                    else
                        Serial.println("ERROR: Queue full");
                } else {
                    Serial.println("ERROR: Microsteps must be power of 2 (1-256)");
                }
            }
            else if (strncmp(cmdBuffer, "gearratio ", 10) == 0)
            {
                float val = atof(cmdBuffer + 10);
                if (val >= 0.1f && val <= 100.0f) {
                    stepperController.setGearRatio(val);
                    Serial.printf("Gear ratio set to %.2f:1\n", val);
                } else {
                    Serial.println("ERROR: Gear ratio must be 0.1-100.0");
                }
            }
            else if (strncmp(cmdBuffer, "speedhz ", 8) == 0)
            {
                float val = atof(cmdBuffer + 8);
                stepperController.setSpeedInHz(val);
                Serial.printf("Speed set to %.0f Hz\n", val);
            }
            else if (strcmp(cmdBuffer, "enable") == 0)
            {
                MotorCommand cmd;
                cmd.type = MotorCommandType::ENABLE;
                cmd.data.enable.enable = true;
                if (queueMotorCmd(cmd))
                    Serial.println("Motor enabled");
                else
                    Serial.println("ERROR: Queue full");
            }
            else if (strcmp(cmdBuffer, "disable") == 0)
            {
                MotorCommand cmd;
                cmd.type = MotorCommandType::ENABLE;
                cmd.data.enable.enable = false;
                if (queueMotorCmd(cmd))
                    Serial.println("Motor disabled");
                else
                    Serial.println("ERROR: Queue full");
            }
            // --- Dance commands ---
            else if (strcmp(cmdBuffer, "dance list") == 0)
            {
                Serial.println("Dances: twist, shake, spin, wiggle, watusi, peppermint_twist");
            }
            else if (strcmp(cmdBuffer, "dance stop") == 0)
            {
                if (stepperController.stopDance())
                    Serial.println("Dance stopped");
                else
                    Serial.println("No dance in progress");
            }
            else if (strncmp(cmdBuffer, "dance ", 6) == 0)
            {
                const char* name = cmdBuffer + 6;
                StepperMotorController::DanceType type;
                bool valid = true;
                if (strcmp(name, "twist") == 0) type = StepperMotorController::DanceType::TWIST;
                else if (strcmp(name, "shake") == 0) type = StepperMotorController::DanceType::SHAKE;
                else if (strcmp(name, "spin") == 0) type = StepperMotorController::DanceType::SPIN;
                else if (strcmp(name, "wiggle") == 0) type = StepperMotorController::DanceType::WIGGLE;
                else if (strcmp(name, "watusi") == 0) type = StepperMotorController::DanceType::WATUSI;
                else if (strcmp(name, "peppermint_twist") == 0) type = StepperMotorController::DanceType::PEPPERMINT_TWIST;
                else { valid = false; Serial.println("Unknown dance. Use 'dance list' to see options."); }
                if (valid) {
                    if (stepperController.startDance(type))
                        Serial.printf("Dance '%s' started\n", name);
                    else
                        Serial.println("ERROR: Could not start dance (already dancing?)");
                }
            }
            // --- Behavior commands ---
            else if (strcmp(cmdBuffer, "behavior list") == 0)
            {
                Serial.println("Behaviors: scanning, sleeping, eating, alert, roaring, stalking, playing, resting, hunting, victory");
            }
            else if (strcmp(cmdBuffer, "behavior stop") == 0)
            {
                if (stepperController.stopBehavior())
                    Serial.println("Behavior stopped");
                else
                    Serial.println("No behavior in progress");
            }
            else if (strncmp(cmdBuffer, "behavior ", 9) == 0)
            {
                const char* name = cmdBuffer + 9;
                StepperMotorController::BehaviorType type;
                bool valid = true;
                if (strcmp(name, "scanning") == 0) type = StepperMotorController::BehaviorType::SCANNING;
                else if (strcmp(name, "sleeping") == 0) type = StepperMotorController::BehaviorType::SLEEPING;
                else if (strcmp(name, "eating") == 0) type = StepperMotorController::BehaviorType::EATING;
                else if (strcmp(name, "alert") == 0) type = StepperMotorController::BehaviorType::ALERT;
                else if (strcmp(name, "roaring") == 0) type = StepperMotorController::BehaviorType::ROARING;
                else if (strcmp(name, "stalking") == 0) type = StepperMotorController::BehaviorType::STALKING;
                else if (strcmp(name, "playing") == 0) type = StepperMotorController::BehaviorType::PLAYING;
                else if (strcmp(name, "resting") == 0) type = StepperMotorController::BehaviorType::RESTING;
                else if (strcmp(name, "hunting") == 0) type = StepperMotorController::BehaviorType::HUNTING;
                else if (strcmp(name, "victory") == 0) type = StepperMotorController::BehaviorType::VICTORY;
                else { valid = false; Serial.println("Unknown behavior. Use 'behavior list' to see options."); }
                if (valid) {
                    if (stepperController.startBehavior(type))
                        Serial.printf("Behavior '%s' started\n", name);
                    else
                        Serial.println("ERROR: Could not start behavior (already running?)");
                }
            }
            // --- Network config ---
            else if (strcmp(cmdBuffer, "wifi") == 0)
            {
                const SystemConfig& cfg = configManager.getConfig();
                Serial.printf("WiFi SSID: %s\n", cfg.wifiSSID);
                if (httpServer != nullptr && httpServer->isConnected())
                    Serial.printf("Status: Connected  IP: %s  RSSI: %d dBm\n",
                        httpServer->getIPAddress().c_str(), WiFi.RSSI());
                else
                    Serial.println("Status: Disconnected");
            }
            else if (strncmp(cmdBuffer, "wifi ", 5) == 0)
            {
                // Parse: wifi <ssid> <password>
                char ssid[33] = {0};
                char pass[65] = {0};
                const char* args = cmdBuffer + 5;
                const char* space = strchr(args, ' ');
                if (space) {
                    size_t ssidLen = space - args;
                    if (ssidLen < sizeof(ssid)) {
                        strncpy(ssid, args, ssidLen);
                        strncpy(pass, space + 1, sizeof(pass) - 1);
                        configManager.setWifiSSID(ssid);
                        configManager.setWifiPassword(pass);
                        configManager.save();
                        Serial.printf("WiFi set to '%s'. Reboot to apply.\n", ssid);
                    }
                } else {
                    Serial.println("Usage: wifi <ssid> <password>");
                }
            }
            else if (strcmp(cmdBuffer, "mqtt") == 0)
            {
                const SystemConfig& cfg = configManager.getConfig();
                Serial.printf("MQTT: %s (%s)\n",
                    cfg.mqttEnabled ? "Enabled" : "Disabled",
                    mqttController.isConnected() ? "Connected" : "Disconnected");
                Serial.printf("  Broker: %s:%u\n", cfg.mqttBroker, cfg.mqttPort);
                Serial.printf("  Device ID: %s\n", cfg.mqttDeviceId);
                Serial.printf("  Base Topic: %s\n", cfg.mqttBaseTopic);
            }
            else if (strcmp(cmdBuffer, "mqtt enable") == 0)
            {
                configManager.setMqttEnabled(true);
                configManager.save();
                Serial.println("MQTT enabled. Reboot to apply.");
            }
            else if (strcmp(cmdBuffer, "mqtt disable") == 0)
            {
                configManager.setMqttEnabled(false);
                configManager.save();
                Serial.println("MQTT disabled. Reboot to apply.");
            }
            else if (strncmp(cmdBuffer, "mqtt broker ", 12) == 0)
            {
                configManager.setMqttBroker(cmdBuffer + 12);
                configManager.save();
                Serial.printf("MQTT broker set to '%s'. Reboot to apply.\n", cmdBuffer + 12);
            }
            else if (strncmp(cmdBuffer, "mqtt port ", 10) == 0)
            {
                uint16_t port = (uint16_t)atoi(cmdBuffer + 10);
                configManager.setMqttPort(port);
                configManager.save();
                Serial.printf("MQTT port set to %u. Reboot to apply.\n", port);
            }
            else if (strncmp(cmdBuffer, "mqtt id ", 8) == 0)
            {
                configManager.setMqttDeviceId(cmdBuffer + 8);
                configManager.save();
                Serial.printf("MQTT device ID set to '%s'. Reboot to apply.\n", cmdBuffer + 8);
            }
            // --- System commands ---
            else if (strcmp(cmdBuffer, "save") == 0)
            {
                if (configManager.save())
                    Serial.println("Configuration saved to NVS");
                else
                    Serial.println("ERROR: Failed to save configuration");
            }
            else if (strcmp(cmdBuffer, "reboot") == 0)
            {
                Serial.println("Rebooting...");
                delay(100);
                ESP.restart();
            }
            // --- Logging level control ---
            else if (strcmp(cmdBuffer, "log off") == 0 || strcmp(cmdBuffer, "log none") == 0)
            {
                runtimeLogLevel = ESP_LOG_NONE;
                Serial.println("Logging disabled.");
            }
            else if (strcmp(cmdBuffer, "log error") == 0)
            {
                runtimeLogLevel = ESP_LOG_ERROR;
                Serial.println("Log level: ERROR");
            }
            else if (strcmp(cmdBuffer, "log warn") == 0)
            {
                runtimeLogLevel = ESP_LOG_WARN;
                Serial.println("Log level: WARN");
            }
            else if (strcmp(cmdBuffer, "log info") == 0 || strcmp(cmdBuffer, "log on") == 0)
            {
                runtimeLogLevel = ESP_LOG_INFO;
                Serial.println("Log level: INFO");
            }
            else if (strcmp(cmdBuffer, "log debug") == 0)
            {
                runtimeLogLevel = ESP_LOG_DEBUG;
                Serial.println("Log level: DEBUG");
            }
            else if (strcmp(cmdBuffer, "log") == 0)
            {
                const char* levelStr = "UNKNOWN";
                switch (runtimeLogLevel) {
                    case ESP_LOG_NONE:    levelStr = "OFF"; break;
                    case ESP_LOG_ERROR:   levelStr = "ERROR"; break;
                    case ESP_LOG_WARN:    levelStr = "WARN"; break;
                    case ESP_LOG_INFO:    levelStr = "INFO"; break;
                    case ESP_LOG_DEBUG:   levelStr = "DEBUG"; break;
                    case ESP_LOG_VERBOSE: levelStr = "VERBOSE"; break;
                }
                Serial.printf("Log level: %s\n", levelStr);
            }
            // --- Help ---
            else if (strcmp(cmdBuffer, "help") == 0)
            {
                static const char* const helpSections[] = {
                    "=== Motion ===\n"
                    "  position <deg>   Move to absolute position\n"
                    "  heading <deg>    Move to heading (shortest path)\n"
                    "  home             Return to 0 deg\n"
                    "  zero             Set current position as 0\n"
                    "  forward          Continuous forward rotation\n"
                    "  backward         Continuous backward rotation\n"
                    "  stop             Stop (decelerate)\n"
                    "  forcestop        Emergency stop + disable\n",

                    "=== Config ===\n"
                    "  speed <sps>      Max speed (steps/sec)\n"
                    "  accel <sps2>     Acceleration (steps/sec^2)\n"
                    "  microsteps <n>   Microstepping (1-256, power of 2)\n"
                    "  gearratio <r>    Gear ratio (0.1-100.0)\n"
                    "  speedhz <hz>     Velocity mode speed (Hz)\n"
                    "  enable / disable Motor driver on/off\n",

                    "=== Dance / Behavior ===\n"
                    "  dance <type>     Start dance (see 'dance list')\n"
                    "  dance stop/list  Stop or list dances\n"
                    "  behavior <type>  Start behavior (see 'behavior list')\n"
                    "  behavior stop/list\n",

                    "=== Network ===\n"
                    "  wifi             Show WiFi status\n"
                    "  wifi <s> <p>     Set WiFi SSID + password\n"
                    "  mqtt             Show MQTT config\n"
                    "  mqtt enable/disable/broker/port/id\n",

                    "=== System ===\n"
                    "  status           Quick status\n"
                    "  statusfull       Full status with TMC2209\n"
                    "  save             Save config to NVS\n"
                    "  reboot           Restart device\n"
                    "  log [off|error|warn|info|debug]\n",
                };
                for (int i = 0; i < 5; i++) {
                    Serial.print(helpSections[i]);
                    Serial.flush();
                    vTaskDelay(pdMS_TO_TICKS(20));
                }
            }
            else
            {
                Serial.printf("Unknown command: '%s'. Type 'help' for commands.\n", cmdBuffer);
            }
        }

        // Heartbeat status every 10 seconds (when queue is empty)
        TickType_t currentTick = xTaskGetTickCount();
        if ((currentTick - lastHeartbeat) >= heartbeatInterval)
        {
            ESP_LOGD(TAG, "Heap: %u bytes | Uptime: %lu ms | Stepper pos: %ld | Enabled: %s | Running: %s | Dance: %s | Motor queue: %u | Serial queue: %u",
                ESP.getFreeHeap(), millis(),
                stepperController.getStepperPosition(),
                stepperController.isEnabled() ? "Yes" : "No",
                stepperController.isRunning() ? "Yes" : "No",
                stepperController.isDanceInProgress() ? "Yes" : "No",
                motorCommandQueue.getCount(),
                serialCommandQueue.getCount());

            lastHeartbeat = currentTick;
        }
    }
}

/**
 * OLED Display Task
 * Runs on Core 0 with low priority
 * Updates display every 500ms
 */
void oledDisplayTask(void *parameter)
{
    ESP_LOGI(TAG, "OLEDDisplayTask started on Core 0");

    const TickType_t updateInterval = pdMS_TO_TICKS(500);

    while (true)
    {
        oledDisplay.update();
        vTaskDelay(updateInterval);
    }
}

void loop()
{
    // Main loop is now empty - all work is done in FreeRTOS tasks
    // This function must exist for Arduino framework, but tasks handle everything
    vTaskDelay(pdMS_TO_TICKS(1000)); // Sleep for 1 second
}
