#pragma once
#include <cstdint>

struct AsyncMqttClientMessageProperties {
  uint8_t qos;
  bool dup;
  bool retain;
};
