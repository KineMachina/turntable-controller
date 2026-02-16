#ifndef MQTT_CONTROLLER_H
#define MQTT_CONTROLLER_H

#include "Arduino.h"
#include <AsyncMqttClient.h>
#include <WiFi.h>
#include "StepperMotorController.h"
#include "MotorCommandQueue.h"
#include "ConfigurationManager.h"
#include <FreeRTOS.h>
#include <queue.h>
#include <ArduinoJson.h>

/**
 * MQTT Configuration Structure
 * Configuration is now loaded from ConfigurationManager (NVS)
 */
struct MQTTConfig {
    bool enabled = false;
    const char* broker = "mqtt.broker.local";
    uint16_t port = 1883;
    const char* username = "";
    const char* password = "";
    const char* deviceId = "turntable_001";
    const char* baseTopic = "kinemachina/turntable";
    uint8_t qosCommands = 1;
    uint8_t qosStatus = 0;
    uint16_t keepalive = 60;
};

/**
 * MQTT Command Structure
 * Used for queuing commands from MQTT callbacks
 */
struct MQTTCommand {
    enum Type {
        CMD_POSITION,
        CMD_HEADING,
        CMD_ENABLE,
        CMD_SPEED,
        CMD_ACCELERATION,
        CMD_MICROSTEPS,
        CMD_GEAR_RATIO,
        CMD_SPEED_HZ,
        CMD_RUN_FORWARD,
        CMD_RUN_BACKWARD,
        CMD_STOP_MOVE,
        CMD_FORCE_STOP,
        CMD_RESET,
        CMD_ZERO,
        CMD_HOME,
        CMD_DANCE,
        CMD_STOP_DANCE,
        CMD_BEHAVIOR,
        CMD_STOP_BEHAVIOR
    };
    Type type;
    char topic[256];
    char payload[512];
    size_t payloadLen;
};

/**
 * MQTTController - Manages MQTT connection and command processing
 * Follows the same pattern as SFX controller for consistency
 */
class MQTTController {
private:
    // MQTT client
    AsyncMqttClient mqttClient;
    MQTTConfig config;
    
    // Controller references
    StepperMotorController* stepperController;
    MotorCommandQueue* commandQueue;
    ConfigurationManager* configManager;
    
    // Topic buffers
    char commandTopicPrefix[128];
    char statusTopicPrefix[128];
    char responseTopic[128];
    char onlineTopic[128];
    
    // FreeRTOS task and queue
    TaskHandle_t mqttTaskHandle;
    QueueHandle_t mqttCommandQueue;
    static const size_t MQTT_QUEUE_SIZE = 20;
    
    // Static instance pointer for callbacks
    static MQTTController* instance;
    
    // State tracking for change detection
    struct {
        bool enabled;
        bool running;
        long position;
        float positionDegrees;
        float speedHz;
        uint8_t microsteps;
        float gearRatio;
        bool behaviorInProgress;
        bool danceInProgress;
    } lastState;
    
    // Status publishing
    unsigned long lastStatusPublishTime;
    static const unsigned long STATUS_PUBLISH_INTERVAL_MS = 30000; // 30 seconds
    
    // Move-complete tracking: set when heading/position move starts, cleared when move finishes or stopMove/forceStop
    bool pendingMoveComplete;
    char pendingMoveCommandType[24];  // "heading" or "position"
    char pendingMoveRequestId[64];    // optional request_id from incoming JSON
    
    // Internal methods
    void buildTopics();
    void subscribeToCommands();
    void publishStatus(bool force = false);
    void publishFullStatus();
    void publishResponse(const char* command, bool success, const char* message, const char* error = nullptr);
    void publishMoveCompleteResponse(const char* commandType, const char* requestId = nullptr);
    bool hasStateChanged();
    void updateState();
    
    // Command handlers
    void handleCommand(MQTTCommand& cmd);
    void handlePosition(const char* payload, size_t len);
    void handleHeading(const char* payload, size_t len);
    void handleEnable(const char* payload, size_t len);
    void handleSpeed(const char* payload, size_t len);
    void handleAcceleration(const char* payload, size_t len);
    void handleMicrosteps(const char* payload, size_t len);
    void handleGearRatio(const char* payload, size_t len);
    void handleSpeedHz(const char* payload, size_t len);
    void handleRunForward(const char* payload, size_t len);
    void handleRunBackward(const char* payload, size_t len);
    void handleStopMove(const char* payload, size_t len);
    void handleForceStop(const char* payload, size_t len);
    void handleReset(const char* payload, size_t len);
    void handleZero(const char* payload, size_t len);
    void handleHome(const char* payload, size_t len);
    void handleDance(const char* payload, size_t len);
    void handleStopDance(const char* payload, size_t len);
    void handleBehavior(const char* payload, size_t len);
    void handleStopBehavior(const char* payload, size_t len);
    
    // MQTT callbacks (static wrappers)
    static void onMqttConnect(bool sessionPresent);
    static void onMqttDisconnect(AsyncMqttClientDisconnectReason reason);
    static void onMqttMessage(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total);
    static void onMqttPublish(uint16_t packetId);
    
    // FreeRTOS task
    static void mqttTaskWrapper(void* parameter);
    void mqttTask();
    
public:
    /**
     * Constructor
     */
    MQTTController();
    
    /**
     * Destructor
     */
    ~MQTTController();
    
    /**
     * Initialize MQTT controller
     * @param stepperCtrl Reference to StepperMotorController
     * @param cmdQueue Reference to MotorCommandQueue
     * @param cfg Optional MQTT configuration (uses default if nullptr)
     * @param configMgr Optional reference to ConfigurationManager for saving settings
     * @return true if successful, false otherwise
     */
    bool begin(StepperMotorController* stepperCtrl, MotorCommandQueue* cmdQueue, const MQTTConfig* cfg = nullptr, ConfigurationManager* configMgr = nullptr);
    
    /**
     * Update MQTT controller (call from loop or task)
     * Handles status publishing and connection management
     */
    void update();
    
    /**
     * Check if MQTT is connected
     * @return true if connected, false otherwise
     */
    bool isConnected() const;
    
    /**
     * Get MQTT configuration
     * @return Reference to current configuration
     */
    const MQTTConfig& getConfig() const { return config; }
    
    /**
     * Set MQTT configuration
     * @param cfg New configuration
     */
    void setConfig(const MQTTConfig& cfg);
    
    /**
     * Restart MQTT connection (disconnect and reconnect)
     * Useful after configuration changes
     * @return true if restart initiated, false if MQTT disabled or not initialized
     */
    bool restart();
};

#endif // MQTT_CONTROLLER_H
