#include "OLEDDisplayController.h"
#include "StepperMotorController.h"
#include "HTTPServerController.h"
#include "MQTTController.h"
#include "ConfigurationManager.h"

OLEDDisplayController::OLEDDisplayController()
    : display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1),
      stepperController(nullptr),
      httpServer(nullptr),
      mqttController(nullptr),
      configManager(nullptr),
      initialized(false)
{
}

bool OLEDDisplayController::begin(StepperMotorController* stepperCtrl,
                                   HTTPServerController* httpSrv,
                                   MQTTController* mqttCtrl,
                                   ConfigurationManager* configMgr)
{
    stepperController = stepperCtrl;
    httpServer = httpSrv;
    mqttController = mqttCtrl;
    configManager = configMgr;

    Wire.begin(SDA_PIN, SCL_PIN);

    if (!display.begin(SSD1306_SWITCHCAPVCC, OLED_ADDRESS)) {
        Serial.println("ERROR: SSD1306 OLED init failed");
        return false;
    }

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);
    display.println("OLED Init OK");
    display.display();

    initialized = true;
    Serial.println("OLED display initialized");
    return true;
}

void OLEDDisplayController::update()
{
    if (!initialized) return;

    display.clearDisplay();
    display.setTextSize(1);
    display.setTextColor(SSD1306_WHITE);
    display.setCursor(0, 0);

    // Line 1: IP address
    if (httpServer != nullptr && httpServer->isConnected()) {
        display.print("IP:");
        display.println(httpServer->getIPAddress());
    } else {
        display.println("IP: --");
    }

    // Line 2: WiFi status
    if (httpServer != nullptr && httpServer->isConnected()) {
        display.println("WiFi: Connected");
    } else {
        display.println("WiFi: Disconnected");
    }

    // Line 3: MQTT broker
    if (configManager != nullptr) {
        const SystemConfig& cfg = configManager->getConfig();
        display.print("MQTT:");
        display.println(cfg.mqttBroker);
    } else {
        display.println("MQTT: --");
    }

    // Line 4: Device ID
    if (configManager != nullptr) {
        const SystemConfig& cfg = configManager->getConfig();
        display.print("ID:");
        display.println(cfg.mqttDeviceId);
    } else {
        display.println("ID: --");
    }

    // Line 5: Stepper position (steps and degrees)
    if (stepperController != nullptr) {
        long pos = stepperController->getStepperPosition();
        float deg = stepperController->getStepperPositionDegrees();
        char buf[22];
        snprintf(buf, sizeof(buf), "Pos:%ld (%.1f%c)", pos, deg, 0xF8);
        display.println(buf);
    } else {
        display.println("Pos: --");
    }

    display.display();
}
