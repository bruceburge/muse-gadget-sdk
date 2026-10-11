// In-memory fake NVS for host unit tests.
//
// Replicates the ESP-IDF constraint that matters most here:
// NVS_KEY_NAME_MAX_SIZE (16) includes the NUL terminator, so keys longer
// than 15 characters fail with ESP_ERR_NVS_KEY_TOO_LONG.
#include "nvs.h"
#include "nvs_flash.h"
#include <string.h>

#define MOCK_NVS_MAX_ENTRIES 32
#define MOCK_NVS_MAX_KEY 64
#define MOCK_NVS_MAX_VAL 512

static struct {
    char key[MOCK_NVS_MAX_KEY];
    char val[MOCK_NVS_MAX_VAL];
    int used;
} s_store[MOCK_NVS_MAX_ENTRIES];

int mock_nvs_fail_open = 0;
int mock_nvs_fail_set_str = 0;
int mock_nvs_fail_commit = 0;

void mock_nvs_reset(void) {
    memset(s_store, 0, sizeof(s_store));
    mock_nvs_fail_open = 0;
    mock_nvs_fail_set_str = 0;
    mock_nvs_fail_commit = 0;
}

static int find_key(const char *key) {
    for (int i = 0; i < MOCK_NVS_MAX_ENTRIES; i++) {
        if (s_store[i].used && strcmp(s_store[i].key, key) == 0) return i;
    }
    return -1;
}

esp_err_t nvs_flash_init(void) { return ESP_OK; }
esp_err_t nvs_flash_init_partition(const char *part_name) {
    (void)part_name;
    return ESP_OK;
}

esp_err_t nvs_open(const char *ns, int open_mode, nvs_handle_t *out_handle) {
    (void)ns; (void)open_mode;
    if (mock_nvs_fail_open) return ESP_FAIL;
    *out_handle = 1;
    return ESP_OK;
}

void nvs_close(nvs_handle_t handle) { (void)handle; }

esp_err_t nvs_get_str(nvs_handle_t handle, const char *key, char *out_value, size_t *length) {
    (void)handle;
    int i = find_key(key);
    if (i < 0) return ESP_ERR_NVS_NOT_FOUND;
    size_t need = strlen(s_store[i].val) + 1;
    if (out_value == NULL) {
        *length = need;
        return ESP_OK;
    }
    if (*length < need) return ESP_ERR_NVS_INVALID_LENGTH;
    memcpy(out_value, s_store[i].val, need);
    *length = need;
    return ESP_OK;
}

esp_err_t nvs_set_str(nvs_handle_t handle, const char *key, const char *value) {
    (void)handle;
    if (mock_nvs_fail_set_str) return ESP_FAIL;
    if (strlen(key) >= NVS_KEY_NAME_MAX_SIZE) return ESP_ERR_NVS_KEY_TOO_LONG;
    int i = find_key(key);
    if (i < 0) {
        for (i = 0; i < MOCK_NVS_MAX_ENTRIES; i++) {
            if (!s_store[i].used) break;
        }
        if (i >= MOCK_NVS_MAX_ENTRIES) return ESP_FAIL;
        strncpy(s_store[i].key, key, sizeof(s_store[i].key) - 1);
        s_store[i].key[sizeof(s_store[i].key) - 1] = '\0';
        s_store[i].used = 1;
    }
    strncpy(s_store[i].val, value, sizeof(s_store[i].val) - 1);
    s_store[i].val[sizeof(s_store[i].val) - 1] = '\0';
    return ESP_OK;
}

esp_err_t nvs_commit(nvs_handle_t handle) {
    (void)handle;
    if (mock_nvs_fail_commit) return ESP_FAIL;
    return ESP_OK;
}

esp_err_t nvs_erase_key(nvs_handle_t handle, const char *key) {
    (void)handle;
    int i = find_key(key);
    if (i < 0) return ESP_ERR_NVS_NOT_FOUND;
    s_store[i].used = 0;
    return ESP_OK;
}
