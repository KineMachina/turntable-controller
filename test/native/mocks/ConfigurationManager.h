#ifndef CONFIGURATION_MANAGER_H
#define CONFIGURATION_MANAGER_H

#include "Arduino.h"
#include "Preferences.h"

struct SystemConfig {
    char wifiSSID[33];
    char wifiPassword[65];
    bool mqttEnabled;
    char mqttBroker[129];
    uint16_t mqttPort;
    char mqttUsername[65];
    char mqttPassword[65];
    char mqttDeviceId[65];
    char mqttBaseTopic[129];
    uint8_t mqttQosCommands;
    uint8_t mqttQosStatus;
    uint16_t mqttKeepalive;
    float motorMaxSpeed;
    float motorAcceleration;
    uint8_t motorMicrosteps;
    float motorGearRatio;
    uint16_t tmcRmsCurrent;
    uint8_t tmcIrun;
    uint8_t tmcIhold;
    bool tmcSpreadCycle;
    bool tmcPwmAutoscale;
    uint8_t tmcBlankTime;
    uint8_t configVersion;
};

class ConfigurationManager {
public:
    ConfigurationManager() {}
    ~ConfigurationManager() {}
    bool load() { return true; }
    bool save() { return true; }
    bool resetToDefaults() { return true; }
    const SystemConfig& getConfig() const { return _config; }
    bool setConfig(const SystemConfig&) { return true; }
    bool isLoaded() const { return true; }

    void setWifiSSID(const char*) {}
    void setWifiPassword(const char*) {}
    const char* getWifiSSID() const { return ""; }
    const char* getWifiPassword() const { return ""; }

    void setMqttEnabled(bool) {}
    void setMqttBroker(const char*) {}
    void setMqttPort(uint16_t) {}
    void setMqttUsername(const char*) {}
    void setMqttPassword(const char*) {}
    void setMqttDeviceId(const char*) {}
    void setMqttBaseTopic(const char*) {}
    void setMqttQosCommands(uint8_t) {}
    void setMqttQosStatus(uint8_t) {}
    void setMqttKeepalive(uint16_t) {}

    void setMotorMaxSpeed(float) {}
    void setMotorAcceleration(float) {}
    void setMotorMicrosteps(uint8_t) {}
    void setMotorGearRatio(float) {}

    void setTmcRmsCurrent(uint16_t) {}
    void setTmcIrun(uint8_t) {}
    void setTmcIhold(uint8_t) {}
    void setTmcSpreadCycle(bool) {}
    void setTmcPwmAutoscale(bool) {}
    void setTmcBlankTime(uint8_t) {}

private:
    SystemConfig _config = {};
};

#endif // CONFIGURATION_MANAGER_H
