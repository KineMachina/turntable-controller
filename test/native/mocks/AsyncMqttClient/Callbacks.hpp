#pragma once

#include <functional>
#include "DisconnectReasons.hpp"
#include "MessageProperties.hpp"
#include "Errors.hpp"

namespace AsyncMqttClientInternals {
typedef std::function<void(bool sessionPresent)> OnConnectUserCallback;
typedef std::function<void(AsyncMqttClientDisconnectReason reason)> OnDisconnectUserCallback;
typedef std::function<void(uint16_t packetId, uint8_t qos)> OnSubscribeUserCallback;
typedef std::function<void(uint16_t packetId)> OnUnsubscribeUserCallback;
typedef std::function<void(char* topic, char* payload, AsyncMqttClientMessageProperties properties, size_t len, size_t index, size_t total)> OnMessageUserCallback;
typedef std::function<void(uint16_t packetId)> OnPublishUserCallback;
typedef std::function<void(uint16_t packetId, AsyncMqttClientError error)> OnErrorUserCallback;
}  // namespace AsyncMqttClientInternals
