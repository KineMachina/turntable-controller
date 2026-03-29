#ifndef ESP_LOG_H_MOCK
#define ESP_LOG_H_MOCK

typedef enum {
    ESP_LOG_NONE    = 0,
    ESP_LOG_ERROR   = 1,
    ESP_LOG_WARN    = 2,
    ESP_LOG_INFO    = 3,
    ESP_LOG_DEBUG   = 4,
    ESP_LOG_VERBOSE = 5
} esp_log_level_t;

// log_printf — no-op in native tests (used by RuntimeLog.h macros)
#define log_printf(format, ...) ((void)0)

// Default ESP_LOGx macros (RuntimeLog.h will override these)
#define ESP_LOGE(tag, format, ...) ((void)0)
#define ESP_LOGW(tag, format, ...) ((void)0)
#define ESP_LOGI(tag, format, ...) ((void)0)
#define ESP_LOGD(tag, format, ...) ((void)0)
#define ESP_LOGV(tag, format, ...) ((void)0)

// Provide storage for runtime log level (normally defined in main.cpp)
#ifdef __cplusplus
inline esp_log_level_t runtimeLogLevel = ESP_LOG_INFO;
#endif

#endif // ESP_LOG_H_MOCK
