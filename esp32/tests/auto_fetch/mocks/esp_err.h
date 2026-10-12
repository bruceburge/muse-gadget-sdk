#pragma once
// Host mock of ESP-IDF esp_err.h
typedef int esp_err_t;
#define ESP_OK   0
#define ESP_FAIL 0x101
const char *esp_err_to_name(esp_err_t e);
#define ESP_ERROR_CHECK(x) ((void)(x))
