#ifndef ASYNC_MQTT_CLIENT_H_MOCK
#define ASYNC_MQTT_CLIENT_H_MOCK

#include <functional>
#include <vector>
#include <string>
#include <cstdint>
#include <cstring>

#include "Arduino.h"
#include "AsyncMqttClient/Callbacks.hpp"
#include "AsyncMqttClient/DisconnectReasons.hpp"
#include "AsyncMqttClient/MessageProperties.hpp"

// Record of a publish call for test assertions
struct MockMqttPublish {
    std::string topic;
    uint8_t qos;
    bool retain;
    std::string payload;
};

class AsyncMqttClient {
public:
    AsyncMqttClient() : _connected(false), _nextPacketId(1) {}
    ~AsyncMqttClient() {}

    // Configuration (no-ops for mock)
    AsyncMqttClient& setKeepAlive(uint16_t) { return *this; }
    AsyncMqttClient& setClientId(const char*) { return *this; }
    AsyncMqttClient& setCleanSession(bool) { return *this; }
    AsyncMqttClient& setMaxTopicLength(uint16_t) { return *this; }
    AsyncMqttClient& setCredentials(const char*, const char* = nullptr) { return *this; }
    AsyncMqttClient& setWill(const char*, uint8_t, bool, const char* = nullptr, size_t = 0) { return *this; }
    AsyncMqttClient& setServer(IPAddress, uint16_t) { return *this; }
    AsyncMqttClient& setServer(const char*, uint16_t) { return *this; }

    // Callback registration (no-ops)
    AsyncMqttClient& onConnect(AsyncMqttClientInternals::OnConnectUserCallback) { return *this; }
    AsyncMqttClient& onDisconnect(AsyncMqttClientInternals::OnDisconnectUserCallback) { return *this; }
    AsyncMqttClient& onSubscribe(AsyncMqttClientInternals::OnSubscribeUserCallback) { return *this; }
    AsyncMqttClient& onUnsubscribe(AsyncMqttClientInternals::OnUnsubscribeUserCallback) { return *this; }
    AsyncMqttClient& onMessage(AsyncMqttClientInternals::OnMessageUserCallback) { return *this; }
    AsyncMqttClient& onPublish(AsyncMqttClientInternals::OnPublishUserCallback) { return *this; }

    // Connection
    bool connected() const { return _connected; }
    void connect() {}
    void disconnect(bool = false) { _connected = false; }

    // Subscribe/unsubscribe
    uint16_t subscribe(const char*, uint8_t) { return _nextPacketId++; }
    uint16_t unsubscribe(const char*) { return _nextPacketId++; }

    // Publish - captures to vector for test inspection
    uint16_t publish(const char* topic, uint8_t qos, bool retain, const char* payload = nullptr, size_t length = 0, bool = false, uint16_t = 0) {
        MockMqttPublish pub;
        pub.topic = topic ? topic : "";
        pub.qos = qos;
        pub.retain = retain;
        if (payload && length > 0) {
            pub.payload = std::string(payload, length);
        } else if (payload) {
            pub.payload = payload;
        }
        _publishes.push_back(pub);
        return _nextPacketId++;
    }

    bool clearQueue() { return true; }

    // Test helpers
    void _setConnected(bool c) { _connected = c; }
    void _clearPublishes() { _publishes.clear(); }
    const std::vector<MockMqttPublish>& _getPublishes() const { return _publishes; }

private:
    bool _connected;
    uint16_t _nextPacketId;
    std::vector<MockMqttPublish> _publishes;
};

#endif // ASYNC_MQTT_CLIENT_H_MOCK
