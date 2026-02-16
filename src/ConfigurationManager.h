#ifndef CONFIGURATION_MANAGER_H
#define CONFIGURATION_MANAGER_H

#include "Arduino.h"
#include <Preferences.h>

/**
 * System Configuration Structure
 * Contains all configurable settings for the motor controller system
 */
struct SystemConfig {
    // WiFi Settings
    char wifiSSID[33];        // Max 32 chars + null terminator
    char wifiPassword[65];    // Max 64 chars + null terminator
    
    // MQTT Settings
    bool mqttEnabled;
    char mqttBroker[129];     // Max 128 chars + null terminator
    uint16_t mqttPort;
    char mqttUsername[65];     // Max 64 chars + null terminator
    char mqttPassword[65];    // Max 64 chars + null terminator
    char mqttDeviceId[65];    // Max 64 chars + null terminator
    char mqttBaseTopic[129];  // Max 128 chars + null terminator
    uint8_t mqttQosCommands;
    uint8_t mqttQosStatus;
    uint16_t mqttKeepalive;
    
    // Motor Controller Settings
    float motorMaxSpeed;
    float motorAcceleration;
    uint8_t motorMicrosteps;
    float motorGearRatio;

    // TMC2209 Driver Settings
    uint16_t tmcRmsCurrent;
    uint8_t tmcIrun;
    uint8_t tmcIhold;
    bool tmcSpreadCycle;
    bool tmcPwmAutoscale;
    uint8_t tmcBlankTime;
    
    // Configuration version (for future migration support)
    uint8_t configVersion;
};

/**
 * ConfigurationManager - Manages persistent configuration storage
 * Uses ESP32 Preferences (NVS) for non-volatile storage
 */
class ConfigurationManager {
private:
    Preferences preferences;
    SystemConfig config;
    bool configLoaded;
    
    static const char* NVS_NAMESPACE;
    static const uint8_t CONFIG_VERSION = 1;
    
    // Default values
    void setDefaults();
    
    // Validation helpers
    bool validateConfig(const SystemConfig& cfg);
    
public:
    /**
     * Constructor
     */
    ConfigurationManager();
    
    /**
     * Destructor
     */
    ~ConfigurationManager();
    
    /**
     * Load configuration from NVS
     * @return true if successful, false if using defaults
     */
    bool load();
    
    /**
     * Save configuration to NVS
     * @return true if successful, false otherwise
     */
    bool save();
    
    /**
     * Reset configuration to default values and save
     * @return true if successful, false otherwise
     */
    bool resetToDefaults();
    
    /**
     * Get current configuration
     * @return Reference to current SystemConfig
     */
    const SystemConfig& getConfig() const { return config; }
    
    /**
     * Set configuration (does not save automatically)
     * @param cfg New configuration
     * @return true if valid and set, false otherwise
     */
    bool setConfig(const SystemConfig& cfg);
    
    /**
     * Check if configuration has been loaded
     * @return true if loaded, false if using defaults
     */
    bool isLoaded() const { return configLoaded; }
    
    // WiFi configuration setters/getters
    void setWifiSSID(const char* ssid);
    void setWifiPassword(const char* password);
    const char* getWifiSSID() const { return config.wifiSSID; }
    const char* getWifiPassword() const { return config.wifiPassword; }
    
    // MQTT configuration setters/getters
    void setMqttEnabled(bool enabled) { config.mqttEnabled = enabled; }
    void setMqttBroker(const char* broker);
    void setMqttPort(uint16_t port) { config.mqttPort = port; }
    void setMqttUsername(const char* username);
    void setMqttPassword(const char* password);
    void setMqttDeviceId(const char* deviceId);
    void setMqttBaseTopic(const char* baseTopic);
    void setMqttQosCommands(uint8_t qos) { config.mqttQosCommands = qos; }
    void setMqttQosStatus(uint8_t qos) { config.mqttQosStatus = qos; }
    void setMqttKeepalive(uint16_t keepalive) { config.mqttKeepalive = keepalive; }
    
    // Motor configuration setters/getters
    void setMotorMaxSpeed(float speed) { config.motorMaxSpeed = speed; }
    void setMotorAcceleration(float accel) { config.motorAcceleration = accel; }
    void setMotorMicrosteps(uint8_t microsteps) { config.motorMicrosteps = microsteps; }
    void setMotorGearRatio(float ratio) { config.motorGearRatio = ratio; }

    // TMC2209 configuration setters/getters
    void setTmcRmsCurrent(uint16_t current) { config.tmcRmsCurrent = current; }
    void setTmcIrun(uint8_t irun) { config.tmcIrun = irun; }
    void setTmcIhold(uint8_t ihold) { config.tmcIhold = ihold; }
    void setTmcSpreadCycle(bool enabled) { config.tmcSpreadCycle = enabled; }
    void setTmcPwmAutoscale(bool enabled) { config.tmcPwmAutoscale = enabled; }
    void setTmcBlankTime(uint8_t blankTime) { config.tmcBlankTime = blankTime; }
};

#endif // CONFIGURATION_MANAGER_H
