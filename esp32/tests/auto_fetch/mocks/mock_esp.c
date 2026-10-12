// Host mock implementations for misc ESP-IDF APIs.
#include "esp_err.h"
#include <stddef.h>
#include "esp_log.h"
#include "esp_system.h"
#include "esp_sntp.h"
#include "esp_crt_bundle.h"

const char *esp_err_to_name(esp_err_t e) {
    switch (e) {
        case ESP_OK: return "ESP_OK";
        case ESP_FAIL: return "ESP_FAIL";
        case 0x1102: return "ESP_ERR_NVS_NOT_FOUND";
        case 0x110f: return "ESP_ERR_NVS_KEY_TOO_LONG";
        default: return "UNKNOWN";
    }
}

static vprintf_like_t s_vprintf = NULL;
vprintf_like_t esp_log_set_vprintf(vprintf_like_t func) {
    vprintf_like_t old = s_vprintf;
    s_vprintf = func;
    return old;
}

esp_reset_reason_t esp_reset_reason(void) {
    return ESP_RST_POWERON;
}

void esp_sntp_setoperatingmode(int mode) { (void)mode; }
void esp_sntp_setservername(int idx, const char *name) { (void)idx; (void)name; }
void esp_sntp_init(void) {}

esp_err_t esp_crt_bundle_attach(void *conf) { (void)conf; return ESP_OK; }
