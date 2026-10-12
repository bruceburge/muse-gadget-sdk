#pragma once
// Host mock of ESP-IDF nvs.h backed by an in-memory fake (mock_nvs.c).
// Replicates the real key-length constraint: NVS_KEY_NAME_MAX_SIZE is 16
// *including* the NUL, so keys are limited to 15 characters.
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef uint32_t nvs_handle_t;

#define NVS_KEY_NAME_MAX_SIZE 16
#define NVS_READONLY  1
#define NVS_READWRITE 2

#define ESP_ERR_NVS_NOT_FOUND        0x1102
#define ESP_ERR_NVS_INVALID_LENGTH   0x1104
#define ESP_ERR_NVS_KEY_TOO_LONG     0x110f
#define ESP_ERR_NVS_NO_FREE_PAGES    0x1101
#define ESP_ERR_NVS_NEW_VERSION_FOUND 0x110b

esp_err_t nvs_open(const char *ns, int open_mode, nvs_handle_t *out_handle);
void nvs_close(nvs_handle_t handle);
esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length);
esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value);
esp_err_t nvs_commit(nvs_handle_t handle);
esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key);

// Test hooks (implemented in mock_nvs.c)
extern int mock_nvs_fail_open;      // non-zero: nvs_open returns ESP_FAIL
extern int mock_nvs_fail_set_str;   // non-zero: nvs_set_str returns ESP_FAIL
extern int mock_nvs_fail_commit;    // non-zero: nvs_commit returns ESP_FAIL
void mock_nvs_reset(void);
