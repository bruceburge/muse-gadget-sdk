// HTTP GET helper for auto_fetch: fetches a URL fully into memory.
#include <string.h>
#include <stdlib.h>
#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "auto_fetch_http";

typedef struct {
    uint8_t *data;
    size_t len;
    size_t cap;
} fetch_buf_t;

static esp_err_t http_event(esp_http_client_event_t *evt) {
    fetch_buf_t *fb = evt->user_data;
    if (evt->event_id == HTTP_EVENT_ON_DATA && evt->data_len > 0) {
        size_t need = fb->len + evt->data_len;
        if (need > fb->cap) {
            size_t ncap = fb->cap ? fb->cap * 2 : 8192;
            while (ncap < need) ncap *= 2;
            uint8_t *nd = realloc(fb->data, ncap);
            if (!nd) return ESP_FAIL;
            fb->data = nd;
            fb->cap = ncap;
        }
        memcpy(fb->data + fb->len, evt->data, evt->data_len);
        fb->len += evt->data_len;
    }
    return ESP_OK;
}

uint8_t *auto_fetch_http_get(const char *url, size_t *out_len) {
    fetch_buf_t fb = {0};
    esp_http_client_config_t cfg = {
        .url = url,
        .event_handler = http_event,
        .user_data = &fb,
        .timeout_ms = 30000,
        .buffer_size = 4096,
    };
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return NULL;
    esp_err_t err = esp_http_client_perform(c);
    int status = esp_http_client_get_status_code(c);
    esp_http_client_cleanup(c);
    if (err != ESP_OK || status != 200) {
        ESP_LOGW(TAG, "GET %s failed: err=%d status=%d", url, err, status);
        free(fb.data);
        return NULL;
    }
    *out_len = fb.len;
    return fb.data;
}
