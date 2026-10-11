#pragma once
// Host mock of ESP-IDF esp_http_client.h (subset used by auto_fetch_http.c).
// The mock records the config passed to esp_http_client_init so tests can
// assert on it (e.g. that crt_bundle_attach is set for https).
#include "esp_err.h"
#include <stddef.h>
#include <stdint.h>

typedef void *esp_http_client_handle_t;

typedef struct {
    int event_id;
    const char *data;
    int data_len;
    void *user_data;
} esp_http_client_event_t;

#define HTTP_EVENT_ON_DATA 1

typedef esp_err_t (*http_event_handler_cb_t)(esp_http_client_event_t *evt);

typedef struct {
    const char *url;
    http_event_handler_cb_t event_handler;
    void *user_data;
    int timeout_ms;
    esp_err_t (*crt_bundle_attach)(void *conf);
    int buffer_size;
    int method;
} esp_http_client_config_t;

#define HTTP_METHOD_POST 2

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config);
esp_err_t esp_http_client_perform(esp_http_client_handle_t client);
int esp_http_client_get_status_code(esp_http_client_handle_t client);
esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client);
esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                     const char *key, const char *value);
esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client,
                                         const char *data, int len);

// Test hooks (implemented in mock_http_client.c)
extern esp_http_client_config_t mock_http_last_config;
extern int mock_http_init_count;
extern esp_err_t mock_http_perform_result;
extern int mock_http_status_code;
extern const char *mock_http_body;  // bytes delivered via HTTP_EVENT_ON_DATA
void mock_http_reset(void);
