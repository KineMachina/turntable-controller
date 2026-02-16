#include "ConfigurationManager.h"
#include <string.h>

const char* ConfigurationManager::NVS_NAMESPACE = "motor_config";

ConfigurationManager::ConfigurationManager() : configLoaded(false) {
    setDefaults();
}

ConfigurationManager::~ConfigurationManager() {
    preferences.end();
}

void ConfigurationManager::setDefaults() {
    // WiFi defaults
    strncpy(config.wifiSSID, "", sizeof(config.wifiSSID) - 1);
    config.wifiSSID[sizeof(config.wifiSSID) - 1] = '\0';
    strncpy(config.wifiPassword, "", sizeof(config.wifiPassword) - 1);
    config.wifiPassword[sizeof(config.wifiPassword) - 1] = '\0';
    
    // MQTT defaults
    config.mqttEnabled = false;
    strncpy(config.mqttBroker, "mqtt.broker.local", sizeof(config.mqttBroker) - 1);
    config.mqttBroker[sizeof(config.mqttBroker) - 1] = '\0';
    config.mqttPort = 1883;
    config.mqttUsername[0] = '\0';
    config.mqttPassword[0] = '\0';
    strncpy(config.mqttDeviceId, "turntable_001", sizeof(config.mqttDeviceId) - 1);
    config.mqttDeviceId[sizeof(config.mqttDeviceId) - 1] = '\0';
    strncpy(config.mqttBaseTopic, "kinemachina/turntable", sizeof(config.mqttBaseTopic) - 1);
    config.mqttBaseTopic[sizeof(config.mqttBaseTopic) - 1] = '\0';
    config.mqttQosCommands = 1;
    config.mqttQosStatus = 0;
    config.mqttKeepalive = 60;
    
    // Motor controller defaults
    config.motorMaxSpeed = 2000.0f;
    config.motorAcceleration = 400.0f;
    config.motorMicrosteps = 8;
    config.motorGearRatio = 1.0f;

    // TMC2209 defaults
    config.tmcRmsCurrent = 1200;
    config.tmcIrun = 31;
    config.tmcIhold = 31;
    config.tmcSpreadCycle = false;
    config.tmcPwmAutoscale = true;
    config.tmcBlankTime = 24;
    
    // Configuration version
    config.configVersion = CONFIG_VERSION;
}

bool ConfigurationManager::validateConfig(const SystemConfig& cfg) {
    // Validate WiFi SSID
    if (strlen(cfg.wifiSSID) == 0 || strlen(cfg.wifiSSID) > 32) {
        return false;
    }
    
    // Validate WiFi password length
    if (strlen(cfg.wifiPassword) > 64) {
        return false;
    }
    
    // Validate MQTT broker
    if (cfg.mqttEnabled && (strlen(cfg.mqttBroker) == 0 || strlen(cfg.mqttBroker) > 128)) {
        return false;
    }
    
    // Validate MQTT port
    if (cfg.mqttPort == 0 || cfg.mqttPort > 65535) {
        return false;
    }
    
    // Validate motor settings
    if (cfg.motorMaxSpeed <= 0 || cfg.motorMaxSpeed > 10000) {
        return false;
    }
    if (cfg.motorAcceleration <= 0 || cfg.motorAcceleration > 10000) {
        return false;
    }
    // Validate microsteps (must be power of 2: 1, 2, 4, 8, 16, 32, 64, 128, 256)
    if (cfg.motorMicrosteps == 0 || cfg.motorMicrosteps > 256) {
        return false;
    }
    if ((cfg.motorMicrosteps & (cfg.motorMicrosteps - 1)) != 0) {
        return false; // Not a power of 2
    }
    if (cfg.motorGearRatio <= 0 || cfg.motorGearRatio > 100) {
        return false;
    }

    // Validate TMC2209 settings
    if (cfg.tmcRmsCurrent == 0 || cfg.tmcRmsCurrent > 3000) {
        return false;
    }
    if (cfg.tmcIrun > 31) {
        return false;
    }
    if (cfg.tmcIhold > 31) {
        return false;
    }
    if (cfg.tmcBlankTime > 255) {
        return false;
    }
    
    return true;
}

bool ConfigurationManager::load() {
    if (!preferences.begin(NVS_NAMESPACE, true)) { // Read-only mode
        Serial.println("[Config] Failed to open NVS namespace (read-only)");
        configLoaded = false;
        return false;
    }
    
    // Check if configuration exists
    if (!preferences.isKey("config_version")) {
        Serial.println("[Config] No saved configuration found, using defaults");
        preferences.end();
        configLoaded = false;
        return false;
    }
    
    // Load configuration version
    uint8_t savedVersion = preferences.getUChar("config_version", 0);
    if (savedVersion != CONFIG_VERSION) {
        Serial.printf("[Config] Version mismatch: saved=%d, current=%d. Using defaults.\n", 
                     savedVersion, CONFIG_VERSION);
        preferences.end();
        configLoaded = false;
        return false;
    }
    
    // Load WiFi settings
    if (preferences.isKey("wifi_ssid")) {
        preferences.getString("wifi_ssid", config.wifiSSID, sizeof(config.wifiSSID));
    } else {
        strncpy(config.wifiSSID, "", sizeof(config.wifiSSID) - 1);
        config.wifiSSID[sizeof(config.wifiSSID) - 1] = '\0';
    }
    if (preferences.isKey("wifi_password")) {
        preferences.getString("wifi_password", config.wifiPassword, sizeof(config.wifiPassword));
    } else {
        strncpy(config.wifiPassword, "", sizeof(config.wifiPassword) - 1);
        config.wifiPassword[sizeof(config.wifiPassword) - 1] = '\0';
    }
    
    // Load MQTT settings
    config.mqttEnabled = preferences.getBool("mqtt_enabled", false);
    if (preferences.isKey("mqtt_broker")) {
        preferences.getString("mqtt_broker", config.mqttBroker, sizeof(config.mqttBroker));
    } else {
        strncpy(config.mqttBroker, "mqtt.broker.local", sizeof(config.mqttBroker) - 1);
        config.mqttBroker[sizeof(config.mqttBroker) - 1] = '\0';
    }
    config.mqttPort = preferences.getUShort("mqtt_port", 1883);
    if (preferences.isKey("mqtt_username")) {
        preferences.getString("mqtt_username", config.mqttUsername, sizeof(config.mqttUsername));
    } else {
        config.mqttUsername[0] = '\0';
    }
    if (preferences.isKey("mqtt_password")) {
        preferences.getString("mqtt_password", config.mqttPassword, sizeof(config.mqttPassword));
    } else {
        config.mqttPassword[0] = '\0';
    }
    if (preferences.isKey("mqtt_device_id")) {
        preferences.getString("mqtt_device_id", config.mqttDeviceId, sizeof(config.mqttDeviceId));
    } else {
        strncpy(config.mqttDeviceId, "turntable_001", sizeof(config.mqttDeviceId) - 1);
        config.mqttDeviceId[sizeof(config.mqttDeviceId) - 1] = '\0';
    }
    if (preferences.isKey("mqtt_base_topic")) {
        preferences.getString("mqtt_base_topic", config.mqttBaseTopic, sizeof(config.mqttBaseTopic));
    } else {
        strncpy(config.mqttBaseTopic, "kinemachina/turntable", sizeof(config.mqttBaseTopic) - 1);
        config.mqttBaseTopic[sizeof(config.mqttBaseTopic) - 1] = '\0';
    }
    config.mqttQosCommands = preferences.getUChar("mqtt_qos_cmds", 1);
    config.mqttQosStatus = preferences.getUChar("mqtt_qos_status", 0);
    config.mqttKeepalive = preferences.getUShort("mqtt_keepalive", 60);
    
    // Load motor controller settings
    config.motorMaxSpeed = preferences.getFloat("motor_max_speed", 2000.0f);
    config.motorAcceleration = preferences.getFloat("motor_acceleration", 400.0f);
    config.motorMicrosteps = preferences.getUChar("motor_microsteps", 8);
    config.motorGearRatio = preferences.getFloat("motor_gear_ratio", 1.0f);

    // Load TMC2209 settings
    config.tmcRmsCurrent = preferences.getUShort("tmc_rms_current", 1200);
    config.tmcIrun = preferences.getUChar("tmc_irun", 31);
    config.tmcIhold = preferences.getUChar("tmc_ihold", 31);
    config.tmcSpreadCycle = preferences.getBool("tmc_spread_cycle", false);
    config.tmcPwmAutoscale = preferences.getBool("tmc_pwm_autoscale", true);
    config.tmcBlankTime = preferences.getUChar("tmc_blank_time", 24);
    
    config.configVersion = CONFIG_VERSION;
    
    preferences.end();
    
    // Validate loaded configuration
    if (!validateConfig(config)) {
        Serial.println("[Config] Loaded configuration failed validation, using defaults");
        setDefaults();
        configLoaded = false;
        return false;
    }
    
    Serial.println("[Config] Configuration loaded successfully from NVS");
    configLoaded = true;
    return true;
}

bool ConfigurationManager::save() {
    if (!preferences.begin(NVS_NAMESPACE, false)) { // Read-write mode
        Serial.println("[Config] Failed to open NVS namespace (read-write)");
        return false;
    }
    
    // Validate before saving
    if (!validateConfig(config)) {
        Serial.println("[Config] Configuration validation failed, not saving");
        preferences.end();
        return false;
    }
    
    // Save configuration version
    preferences.putUChar("config_version", CONFIG_VERSION);
    
    // Save WiFi settings
    preferences.putString("wifi_ssid", config.wifiSSID);
    preferences.putString("wifi_password", config.wifiPassword);
    
    // Save MQTT settings
    preferences.putBool("mqtt_enabled", config.mqttEnabled);
    preferences.putString("mqtt_broker", config.mqttBroker);
    preferences.putUShort("mqtt_port", config.mqttPort);
    preferences.putString("mqtt_username", config.mqttUsername);
    preferences.putString("mqtt_password", config.mqttPassword);
    preferences.putString("mqtt_device_id", config.mqttDeviceId);
    preferences.putString("mqtt_base_topic", config.mqttBaseTopic);
    preferences.putUChar("mqtt_qos_cmds", config.mqttQosCommands);
    preferences.putUChar("mqtt_qos_status", config.mqttQosStatus);
    preferences.putUShort("mqtt_keepalive", config.mqttKeepalive);
    
    // Save motor controller settings
    preferences.putFloat("motor_max_speed", config.motorMaxSpeed);
    preferences.putFloat("motor_acceleration", config.motorAcceleration);
    preferences.putUChar("motor_microsteps", config.motorMicrosteps);
    preferences.putFloat("motor_gear_ratio", config.motorGearRatio);

    // Save TMC2209 settings
    preferences.putUShort("tmc_rms_current", config.tmcRmsCurrent);
    preferences.putUChar("tmc_irun", config.tmcIrun);
    preferences.putUChar("tmc_ihold", config.tmcIhold);
    preferences.putBool("tmc_spread_cycle", config.tmcSpreadCycle);
    preferences.putBool("tmc_pwm_autoscale", config.tmcPwmAutoscale);
    preferences.putUChar("tmc_blank_time", config.tmcBlankTime);
    
    preferences.end();
    
    Serial.println("[Config] Configuration saved successfully to NVS");
    configLoaded = true;
    return true;
}

bool ConfigurationManager::resetToDefaults() {
    setDefaults();
    return save();
}

bool ConfigurationManager::setConfig(const SystemConfig& cfg) {
    if (!validateConfig(cfg)) {
        return false;
    }
    config = cfg;
    config.configVersion = CONFIG_VERSION;
    return true;
}

void ConfigurationManager::setWifiSSID(const char* ssid) {
    if (ssid != nullptr) {
        strncpy(config.wifiSSID, ssid, sizeof(config.wifiSSID) - 1);
        config.wifiSSID[sizeof(config.wifiSSID) - 1] = '\0';
    }
}

void ConfigurationManager::setWifiPassword(const char* password) {
    if (password != nullptr) {
        strncpy(config.wifiPassword, password, sizeof(config.wifiPassword) - 1);
        config.wifiPassword[sizeof(config.wifiPassword) - 1] = '\0';
    }
}

void ConfigurationManager::setMqttBroker(const char* broker) {
    if (broker != nullptr) {
        strncpy(config.mqttBroker, broker, sizeof(config.mqttBroker) - 1);
        config.mqttBroker[sizeof(config.mqttBroker) - 1] = '\0';
    }
}

void ConfigurationManager::setMqttUsername(const char* username) {
    if (username != nullptr) {
        strncpy(config.mqttUsername, username, sizeof(config.mqttUsername) - 1);
        config.mqttUsername[sizeof(config.mqttUsername) - 1] = '\0';
    }
}

void ConfigurationManager::setMqttPassword(const char* password) {
    if (password != nullptr) {
        strncpy(config.mqttPassword, password, sizeof(config.mqttPassword) - 1);
        config.mqttPassword[sizeof(config.mqttPassword) - 1] = '\0';
    }
}

void ConfigurationManager::setMqttDeviceId(const char* deviceId) {
    if (deviceId != nullptr) {
        strncpy(config.mqttDeviceId, deviceId, sizeof(config.mqttDeviceId) - 1);
        config.mqttDeviceId[sizeof(config.mqttDeviceId) - 1] = '\0';
    }
}

void ConfigurationManager::setMqttBaseTopic(const char* baseTopic) {
    if (baseTopic != nullptr) {
        strncpy(config.mqttBaseTopic, baseTopic, sizeof(config.mqttBaseTopic) - 1);
        config.mqttBaseTopic[sizeof(config.mqttBaseTopic) - 1] = '\0';
    }
}
