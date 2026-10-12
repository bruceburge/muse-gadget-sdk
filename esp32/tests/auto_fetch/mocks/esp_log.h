#pragma once
// Host mock of ESP-IDF esp_log.h: logging is a no-op in unit tests.
#include <stdarg.h>
typedef int (*vprintf_like_t)(const char *fmt, va_list args);
vprintf_like_t esp_log_set_vprintf(vprintf_like_t func);
#define ESP_LOGI(tag, fmt, ...) ((void)0)
#define ESP_LOGW(tag, fmt, ...) ((void)0)
#define ESP_LOGE(tag, fmt, ...) ((void)0)
#define ESP_LOGD(tag, fmt, ...) ((void)0)
#define ESP_LOGV(tag, fmt, ...) ((void)0)
