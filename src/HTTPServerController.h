#ifndef HTTP_SERVER_CONTROLLER_H
#define HTTP_SERVER_CONTROLLER_H

#include "Arduino.h"
#include <ESPAsyncWebServer.h>
#include "WiFi.h"
#include "StepperMotorController.h"
#include "MotorCommandQueue.h"
#include "ConfigurationManager.h"
#include "MQTTController.h"
#include <FreeRTOS.h>
#include <semphr.h>
#include <ArduinoJson.h>

/**
 * JSON Request Structures
 * Used for type-safe JSON deserialization
 */
struct PositionRequest {
    float position;
};

struct EnableRequest {
    bool enable;
};

struct SpeedRequest {
    float speed;
};

struct AccelerationRequest {
    float accel;
};

struct MicrostepsRequest {
    int microsteps;
};

struct GearRatioRequest {
    float ratio;
};

struct SpeedHzRequest {
    float speedHz;
};

struct DanceRequest {
    String danceType;  // "twist", "shake", "spin", "wiggle"
};

struct HomeRequest {
    float position;
    bool home;
};

struct HeadingRequest {
    float heading;
};

/**
 * Error Response Structure
 * Used for standardized error JSON responses
 */
struct ErrorResponse {
    const char* status = "error";
    String message;
};

/**
 * HTTPServerController - Manages WiFi connection and HTTP server
 * Provides REST API endpoints for controlling audio and stepper motor
 */
class HTTPServerController {
private:
    // WiFi configuration
    const char* wifiSSID;
    const char* wifiPassword;
    int httpPort;
    char deviceId[32];
    
    // Web server
    AsyncWebServer* server;
    
    // Controller references
    StepperMotorController* stepperController;
    MotorCommandQueue* commandQueue;
    ConfigurationManager* configManager;
    MQTTController* mqttController;
    
    // Internal methods
    bool initWiFi();
    void setupRoutes();
    void logRequest(AsyncWebServerRequest* request, const char* endpoint);  // Log HTTP request details
    bool getJsonBody(AsyncWebServerRequest* request, String& body, String& errorMsg);  // Extract and validate JSON body from request
    void sendErrorResponse(AsyncWebServerRequest* request, int code, const String& message);  // Send standardized error response
    String processTemplate(const String& var);  // Template processor for HTML template engine
    
    // HTTP handler methods (take AsyncWebServerRequest* parameter)
    void handleRoot(AsyncWebServerRequest* request);
    void handleStatus(AsyncWebServerRequest* request);
    void handleMoveTo(AsyncWebServerRequest* request);
    void handleStepperEnable(AsyncWebServerRequest* request);
    void handleStepperSpeed(AsyncWebServerRequest* request);
    void handleStepperAcceleration(AsyncWebServerRequest* request);
    void handleStepperMicrosteps(AsyncWebServerRequest* request);
    void handleStepperGearRatio(AsyncWebServerRequest* request);
    void handleStepperSpeedHz(AsyncWebServerRequest* request);
    void handleStepperRunForward(AsyncWebServerRequest* request);
    void handleStepperRunBackward(AsyncWebServerRequest* request);
    void handleStepperStopMove(AsyncWebServerRequest* request);
    void handleStepperForceStop(AsyncWebServerRequest* request);
    void handleStepperReset(AsyncWebServerRequest* request);
    void handleStepperStatus(AsyncWebServerRequest* request);
    void handleStepperPositionStatus(AsyncWebServerRequest* request);
    void handleStepperTurntablePosition(AsyncWebServerRequest* request);
    void handleStepperHeading(AsyncWebServerRequest* request);
    void handleStepperDance(AsyncWebServerRequest* request);
    void handleStepperBehavior(AsyncWebServerRequest* request);
    void handleStepperStopBehavior(AsyncWebServerRequest* request);
    void handleStepperStopDance(AsyncWebServerRequest* request);
    void handleStepperRunning(AsyncWebServerRequest* request);
    void handleMqttConfig(AsyncWebServerRequest* request);
    void handleMotorConfig(AsyncWebServerRequest* request);
    void handleNotFound(AsyncWebServerRequest* request);
    
public:
    /**
     * Constructor
     * @param ssid WiFi SSID
     * @param password WiFi password
     * @param port HTTP server port (default: 80)
     */
    HTTPServerController(const char* ssid, const char* password, int port = 80);

    /**
     * Destructor
     */
    ~HTTPServerController();

    /**
     * Set device ID for mDNS hostname and WiFi identification.
     * Must be called before begin().
     */
    void setDeviceId(const char* id);
    
    /**
     * Initialize WiFi and HTTP server
     * @param stepperCtrl Reference to StepperMotorController
     * @param cmdQueue Reference to MotorCommandQueue for sending commands
     * @param configMgr Optional reference to ConfigurationManager for saving settings
     * @param mqttCtrl Optional reference to MQTTController for restarting after config changes
     * @return true if successful, false otherwise
     */
    bool begin(StepperMotorController* stepperCtrl, MotorCommandQueue* cmdQueue, ConfigurationManager* configMgr = nullptr, MQTTController* mqttCtrl = nullptr);
    
    
    /**
     * Check if WiFi is connected
     * @return true if connected, false otherwise
     */
    bool isConnected() const;
    
    /**
     * Get WiFi IP address
     * @return IP address as String, empty if not connected
     */
    String getIPAddress() const;
    
    /**
     * Print API endpoint information to Serial
     */
    void printEndpoints() const;
};

#endif // HTTP_SERVER_CONTROLLER_H
