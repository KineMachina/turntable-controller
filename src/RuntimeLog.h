#pragma once

// Arduino-ESP32 compiles ESP_LOGx macros as direct log_printf calls with
// no runtime level check — only compile-time gating via CORE_DEBUG_LEVEL.
// The USE_ESP_IDF_LOG flag would enable real esp_log runtime control, but
// it breaks third-party libraries that call log_e() without defining TAG.
//
// This header re-defines ESP_LOGx (used by our code) to add a runtime
// level check, while leaving log_e/w/i/d unchanged (used by libraries).
// Include this instead of <esp_log.h> in project source files.

#include <esp_log.h>

extern esp_log_level_t runtimeLogLevel;

#undef ESP_LOGE
#undef ESP_LOGW
#undef ESP_LOGI
#undef ESP_LOGD
#undef ESP_LOGV

#define ESP_LOGE(tag, format, ...) do { if (runtimeLogLevel >= ESP_LOG_ERROR)   log_e("[%s] " format, tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGW(tag, format, ...) do { if (runtimeLogLevel >= ESP_LOG_WARN)    log_w("[%s] " format, tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGI(tag, format, ...) do { if (runtimeLogLevel >= ESP_LOG_INFO)    log_i("[%s] " format, tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGD(tag, format, ...) do { if (runtimeLogLevel >= ESP_LOG_DEBUG)   log_d("[%s] " format, tag, ##__VA_ARGS__); } while(0)
#define ESP_LOGV(tag, format, ...) do { if (runtimeLogLevel >= ESP_LOG_VERBOSE) log_v("[%s] " format, tag, ##__VA_ARGS__); } while(0)
