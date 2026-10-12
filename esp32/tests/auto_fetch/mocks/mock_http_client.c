// Host mock of esp_http_client: records the init config and synthesizes
// a single HTTP_EVENT_ON_DATA from mock_http_body on perform().
#include "esp_http_client.h"
#include <string.h>

esp_http_client_config_t mock_http_last_config;
int mock_http_init_count = 0;
esp_err_t mock_http_perform_result = ESP_OK;
int mock_http_status_code = 200;
const char *mock_http_body = NULL;

static http_event_handler_cb_t s_handler = NULL;
static void *s_user_data = NULL;

void mock_http_reset(void) {
    memset(&mock_http_last_config, 0, sizeof(mock_http_last_config));
    mock_http_init_count = 0;
    mock_http_perform_result = ESP_OK;
    mock_http_status_code = 200;
    mock_http_body = NULL;
    s_handler = NULL;
    s_user_data = NULL;
}

esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *config) {
    mock_http_init_count++;
    if (config) {
        mock_http_last_config = *config;
        s_handler = config->event_handler;
        s_user_data = config->user_data;
    }
    return (esp_http_client_handle_t)0x1234;
}

esp_err_t esp_http_client_perform(esp_http_client_handle_t client) {
    (void)client;
    if (mock_http_perform_result == ESP_OK && mock_http_body && s_handler) {
        esp_http_client_event_t evt;
        evt.event_id = HTTP_EVENT_ON_DATA;
        evt.data = mock_http_body;
        evt.data_len = (int)strlen(mock_http_body);
        evt.user_data = s_user_data;
        s_handler(&evt);
    }
    return mock_http_perform_result;
}

int esp_http_client_get_status_code(esp_http_client_handle_t client) {
    (void)client;
    return mock_http_status_code;
}

esp_err_t esp_http_client_cleanup(esp_http_client_handle_t client) {
    (void)client;
    return ESP_OK;
}

esp_err_t esp_http_client_set_header(esp_http_client_handle_t client,
                                     const char *key, const char *value) {
    (void)client; (void)key; (void)value;
    return ESP_OK;
}

esp_err_t esp_http_client_set_post_field(esp_http_client_handle_t client,
                                         const char *data, int len) {
    (void)client; (void)data; (void)len;
    return ESP_OK;
}
