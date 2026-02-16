#ifndef OLED_DISPLAY_CONTROLLER_H
#define OLED_DISPLAY_CONTROLLER_H

#include "Arduino.h"
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

class StepperMotorController;
class HTTPServerController;
class MQTTController;
class ConfigurationManager;

class OLEDDisplayController {
private:
    static const int SCREEN_WIDTH = 128;
    static const int SCREEN_HEIGHT = 64;
    static const int OLED_ADDRESS = 0x3C;
    static const int SDA_PIN = 10;
    static const int SCL_PIN = 11;

    Adafruit_SSD1306 display;

    StepperMotorController* stepperController;
    HTTPServerController* httpServer;
    MQTTController* mqttController;
    ConfigurationManager* configManager;

    bool initialized;

public:
    OLEDDisplayController();

    bool begin(StepperMotorController* stepperCtrl,
               HTTPServerController* httpSrv,
               MQTTController* mqttCtrl,
               ConfigurationManager* configMgr);

    void update();
};

#endif // OLED_DISPLAY_CONTROLLER_H
