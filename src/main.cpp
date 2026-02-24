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

    Serial.println("\nSerial Commands:");
    Serial.println("  status           - Show current status (basic, no TMC)");
    Serial.println("  statusfull       - Show full status including TMC2209");
    Serial.println("  zero             - Zero stepper position");
    Serial.println("  log [off|error|warn|info|debug] - Set log level");
    if (httpServer != nullptr && httpServer->isConnected())
    {
        httpServer->printEndpoints();
    }
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
            // Process command
            if (strcmp(cmdBuffer, "status") == 0)
            {
                Serial.println("========================================");
                Serial.println("System Status (Basic)");
                Serial.println("========================================");
                
                // System Information
                Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
                Serial.printf("Uptime: %lu ms\n", millis());
                
                // WiFi Information
                if (httpServer != nullptr && httpServer->isConnected())
                {
                    Serial.println("WiFi: Connected");
                    Serial.printf("  SSID: %s\n", WiFi.SSID().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5)); // Yield to other tasks
                    Serial.printf("  IP Address: %s\n", httpServer->getIPAddress().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5));
                    Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5));
                    Serial.printf("  Subnet: %s\n", WiFi.subnetMask().toString().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5));
                    Serial.printf("  DNS: %s\n", WiFi.dnsIP().toString().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5));
                    Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
                    vTaskDelay(pdMS_TO_TICKS(5));
                    Serial.printf("  MAC: %s\n", WiFi.macAddress().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
                else
                {
                    Serial.println("WiFi: Disconnected");
                }
                
                // Stepper Motor Status
                Serial.printf("Stepper Position: %ld steps\n", stepperController.getStepperPosition());
                Serial.printf("Enabled: %s\n", stepperController.isEnabled() ? "Yes" : "No");
                Serial.printf("Running: %s\n", stepperController.isRunning() ? "Yes" : "No");
                Serial.printf("Microsteps: %u\n", stepperController.getMicrosteps());
                Serial.printf("Gear Ratio: %.2f:1 (stepper:turntable)\n", stepperController.getGearRatio());

                // Dance Status
                Serial.printf("Dance In Progress: %s\n", stepperController.isDanceInProgress() ? "Yes" : "No");
                
                // Queue Status
                Serial.printf("Motor Queue: %u pending\n", motorCommandQueue.getCount());
                Serial.printf("Serial Queue: %u pending\n", serialCommandQueue.getCount());
                
                Serial.println("========================================");
            }
            else if (strcmp(cmdBuffer, "zero") == 0)
            {
                // Send zero command via queue
                MotorCommand cmd;
                cmd.type = MotorCommandType::ZERO_POSITION;
                cmd.statusCallback = nullptr;
                cmd.statusContext = nullptr;

                if (motorCommandQueue.sendCommand(cmd, pdMS_TO_TICKS(100)))
                {
                    Serial.println("Position zero command queued");
                }
                else
                {
                    Serial.println("ERROR: Failed to queue zero command");
                }
            }
            else if (strcmp(cmdBuffer, "statusfull") == 0)
            {
                Serial.println("========================================");
                Serial.println("Full System Status (with TMC2209)");
                Serial.println("========================================");
                
                // System Information
                Serial.printf("Free Heap: %u bytes\n", ESP.getFreeHeap());
                Serial.printf("Uptime: %lu ms\n", millis());
                
                // WiFi Information
                if (httpServer != nullptr && httpServer->isConnected())
                {
                    Serial.println("WiFi: Connected");
                    Serial.printf("  SSID: %s\n", WiFi.SSID().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5)); // Yield to other tasks
                    Serial.printf("  IP Address: %s\n", httpServer->getIPAddress().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5));
                    Serial.printf("  Gateway: %s\n", WiFi.gatewayIP().toString().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5));
                    Serial.printf("  Subnet: %s\n", WiFi.subnetMask().toString().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5));
                    Serial.printf("  DNS: %s\n", WiFi.dnsIP().toString().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5));
                    Serial.printf("  RSSI: %d dBm\n", WiFi.RSSI());
                    vTaskDelay(pdMS_TO_TICKS(5));
                    Serial.printf("  MAC: %s\n", WiFi.macAddress().c_str());
                    vTaskDelay(pdMS_TO_TICKS(5));
                }
                else
                {
                    Serial.println("WiFi: Disconnected");
                }
                
                // Stepper Motor Status
                Serial.printf("Stepper Position: %ld steps\n", stepperController.getStepperPosition());
                Serial.printf("Enabled: %s\n", stepperController.isEnabled() ? "Yes" : "No");
                Serial.printf("Running: %s\n", stepperController.isRunning() ? "Yes" : "No");
                Serial.printf("Microsteps: %u\n", stepperController.getMicrosteps());
                Serial.printf("Gear Ratio: %.2f:1 (stepper:turntable)\n", stepperController.getGearRatio());
                
                // TMC2209 Driver Status (with delays between UART reads to prevent watchdog timeout)
                Serial.printf("TMC RMS Current: %u mA\n", stepperController.getTmcRmsCurrent());
                vTaskDelay(pdMS_TO_TICKS(10)); // Yield to other tasks
                Serial.printf("TMC CS Actual: %u\n", stepperController.getTmcCsActual());
                vTaskDelay(pdMS_TO_TICKS(10));
                Serial.printf("TMC Actual Current: %.2f mA\n", stepperController.getTmcActualCurrent());
                vTaskDelay(pdMS_TO_TICKS(10));
                Serial.printf("TMC IRUN: %u%%\n", stepperController.getTmcIrun());
                vTaskDelay(pdMS_TO_TICKS(10));
                Serial.printf("TMC IHOLD: %u%%\n", stepperController.getTmcIhold());
                vTaskDelay(pdMS_TO_TICKS(10));
                Serial.printf("TMC Enabled: %s\n", stepperController.getTmcEnabled() ? "Yes" : "No");
                vTaskDelay(pdMS_TO_TICKS(10));
                Serial.printf("TMC Mode: %s\n", stepperController.getTmcSpreadCycle() ? "SpreadCycle" : "StealthChop");
                vTaskDelay(pdMS_TO_TICKS(10));
                Serial.printf("TMC PWM Autoscale: %s\n", stepperController.getTmcPwmAutoscale() ? "Enabled" : "Disabled");
                vTaskDelay(pdMS_TO_TICKS(10));
                Serial.printf("TMC Blank Time: %u\n", stepperController.getTmcBlankTime());
                vTaskDelay(pdMS_TO_TICKS(10));

                // Dance Status
                Serial.printf("Dance In Progress: %s\n", stepperController.isDanceInProgress() ? "Yes" : "No");
                
                // MQTT configuration and connection status
                {
                    const SystemConfig& cfg = configManager.getConfig();
                    Serial.println("MQTT:");
                    Serial.printf("  Enabled: %s\n", cfg.mqttEnabled ? "Yes" : "No");
                    Serial.printf("  Connected: %s\n", mqttController.isConnected() ? "Yes" : "No");
                    if (cfg.mqttEnabled)
                    {
                        Serial.printf("  Broker: %s\n", cfg.mqttBroker);
                        Serial.printf("  Port: %u\n", cfg.mqttPort);
                        Serial.printf("  Device ID: %s\n", cfg.mqttDeviceId);
                        Serial.printf("  Base Topic: %s\n", cfg.mqttBaseTopic);
                    }
                }
                
                // Queue Status
                Serial.printf("Motor Queue: %u pending\n", motorCommandQueue.getCount());
                Serial.printf("Serial Queue: %u pending\n", serialCommandQueue.getCount());
                
                Serial.println("========================================");
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
            else
            {
                Serial.println("Unknown command. Available commands:");
                Serial.println("  status           - Show status (basic, no TMC)");
                Serial.println("  statusfull       - Show full status including TMC2209");
                Serial.println("  zero             - Zero stepper position");
                Serial.println("  log [off|error|warn|info|debug] - Set log level");
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
