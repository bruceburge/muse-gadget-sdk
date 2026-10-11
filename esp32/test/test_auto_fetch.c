// Host unit tests for the E1002 auto-fetch firmware.
//
// Strategy: the production translation unit ../main/auto_fetch.c is #included
// directly, so static functions (get_config_int, fnv_hex, do_dashboard_fetch,
// dashboard_fetch_done) are testable without changing production visibility.
// ESP-IDF / FreeRTOS APIs are replaced by the mocks in mocks/ (an in-memory
// fake NVS, a fake esp_http_client, and a stub image_fetch_start).
//
// These tests target the bug classes seen in review:
//   - NVS key length violations (ESP_ERR_NVS_KEY_TOO_LONG, silent)
//   - config errors swallowed by callers ("ok:true" when nothing was saved)
//   - missing TLS trust anchors on HTTPS fetches
//   - unchecked xTaskCreate
//   - atoi overflow / unclamped intervals in scheduler timing
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "minunit.h"
#include "nvs.h"              // NVS_KEY_NAME_MAX_SIZE for the key-length test
#include "esp_err.h"
#include "freertos/task.h"
#include "esp_http_client.h"
#include "image_fetch.h"
#include "mock_image_fetch_hooks.h"

// Declaration of the real HTTP GET helper (compiled from ../main/auto_fetch_http.c).
uint8_t *auto_fetch_http_get(const char *url, size_t *out_len);

// Pull in the production code under test (statics become visible here).
#include "../main/auto_fetch.c"

static void reset_all(void) {
    mock_nvs_reset();
    mock_http_reset();
    mock_image_fetch_reset();
    mock_task_reset();
}

// --- NVS key constraints ------------------------------------------------

MU_TEST(nvs_keys_within_limit) {
    // Regression test for the shipped "af_dashboard_url" (16 chars) bug:
    // ESP-IDF NVS keys are limited to NVS_KEY_NAME_MAX_SIZE-1 == 15 chars.
    mu_assert(strlen(KEY_DASHBOARD_URL) < NVS_KEY_NAME_MAX_SIZE);
    mu_assert(strlen(KEY_FETCH_HOURS) < NVS_KEY_NAME_MAX_SIZE);
    mu_assert(strlen(KEY_IMAGE_HASH) < NVS_KEY_NAME_MAX_SIZE);
    mu_assert(strlen(KEY_RESET_REASON) < NVS_KEY_NAME_MAX_SIZE);
    mu_assert(strlen(KEY_LAST_FETCH) < NVS_KEY_NAME_MAX_SIZE);
}

MU_TEST(config_layer_rejects_long_key) {
    // The config layer must surface ESP_ERR_NVS_KEY_TOO_LONG as false,
    // never silently pretend the write worked.
    mock_nvs_reset();
    mu_assert(config_set_str("0123456789abcdef", "v") == false);  // 16 chars
    mu_assert(config_set_str("0123456789abcde", "v") == true);    // 15 chars
    char buf[8];
    mu_assert(config_get_str("0123456789abcde", buf, sizeof(buf)) == true);
    mu_assert_str_eq(buf, "v");
}

// --- auto_fetch_configure error propagation ------------------------------

MU_TEST(configure_propagates_set_failure) {
    // CRITICAL-3: a failed config_set_str must make configure() return false.
    reset_all();
    mock_nvs_fail_set_str = 1;
    mu_assert(auto_fetch_configure("https://example.com/w.jpg", 1) == false);
    mock_nvs_fail_set_str = 0;
}

MU_TEST(configure_propagates_commit_failure) {
    reset_all();
    mock_nvs_fail_commit = 1;
    mu_assert(auto_fetch_configure("https://example.com/w.jpg", 1) == false);
    mock_nvs_fail_commit = 0;
}

MU_TEST(configure_noop_with_null_params) {
    // NULL url + 0 interval configures nothing; must still report success
    // (the app.c handler rejects empty calls with missing_param separately).
    reset_all();
    mu_assert(auto_fetch_configure(NULL, 0) == true);
}

MU_TEST(configure_stores_url_and_interval) {
    reset_all();
    mu_assert(auto_fetch_configure("https://example.com/w.jpg", 3) == true);
    char url[AF_URL_BUF_SIZE];
    char hrs[AF_HOURS_BUF_SIZE];
    mu_assert(config_get_str(KEY_DASHBOARD_URL, url, sizeof(url)) == true);
    mu_assert_str_eq(url, "https://example.com/w.jpg");
    mu_assert(config_get_str(KEY_FETCH_HOURS, hrs, sizeof(hrs)) == true);
    mu_assert_str_eq(hrs, "3");
}

MU_TEST(params_valid) {
    mu_assert(auto_fetch_params_valid(NULL, 0) == false);
    mu_assert(auto_fetch_params_valid("https://x", 0) == true);
    mu_assert(auto_fetch_params_valid(NULL, 2) == true);
    mu_assert(auto_fetch_params_valid("https://x", 2) == true);
}

// --- config int parsing ---------------------------------------------------

MU_TEST(get_config_int_safe) {
    // atoi() on out-of-range input is undefined behavior; get_config_int
    // must use strtol and fall back to the default on any bad input.
    mock_nvs_reset();
    config_set_str(KEY_FETCH_HOURS, "abc");
    mu_assert_int_eq(get_config_int(KEY_FETCH_HOURS, 3), 3);
    config_set_str(KEY_FETCH_HOURS, "");
    mu_assert_int_eq(get_config_int(KEY_FETCH_HOURS, 3), 3);
    config_set_str(KEY_FETCH_HOURS, "7");
    mu_assert_int_eq(get_config_int(KEY_FETCH_HOURS, 3), 7);
    config_set_str(KEY_FETCH_HOURS, "99999999999999999999999");  // ERANGE
    mu_assert_int_eq(get_config_int(KEY_FETCH_HOURS, 3), 3);
    config_set_str(KEY_FETCH_HOURS, "2147483647");  // INT_MAX, no UB
    mu_assert_int_eq(get_config_int(KEY_FETCH_HOURS, 3), 2147483647);
    mock_nvs_reset();
    mu_assert_int_eq(get_config_int(KEY_FETCH_HOURS, 3), 3);  // missing key
}

// --- scheduler timing ------------------------------------------------------

MU_TEST(sanitize_interval) {
    mu_assert_int_eq(auto_fetch_sanitize_interval(0), 1);
    mu_assert_int_eq(auto_fetch_sanitize_interval(-5), 1);
    mu_assert_int_eq(auto_fetch_sanitize_interval(1), 1);
    mu_assert_int_eq(auto_fetch_sanitize_interval(3), 3);
    mu_assert_int_eq(auto_fetch_sanitize_interval(168), 168);
    mu_assert_int_eq(auto_fetch_sanitize_interval(169), 168);
    mu_assert_int_eq(auto_fetch_sanitize_interval(INT_MAX), 168);
}

MU_TEST(tick_due) {
    // One tick == 60s. interval=1h -> due on the 60th tick, counter resets.
    int c = 0;
    for (int i = 0; i < 59; i++) mu_assert(auto_fetch_tick_due(&c, 1) == false);
    mu_assert_int_eq(c, 59);
    mu_assert(auto_fetch_tick_due(&c, 1) == true);
    mu_assert_int_eq(c, 0);

    // interval=3h -> due on the 180th tick.
    c = 0;
    for (int i = 0; i < 179; i++) mu_assert(auto_fetch_tick_due(&c, 3) == false);
    mu_assert(auto_fetch_tick_due(&c, 3) == true);
    mu_assert_int_eq(c, 0);

    // interval 0 sanitizes to 1.
    c = 0;
    for (int i = 0; i < 59; i++) mu_assert(auto_fetch_tick_due(&c, 0) == false);
    mu_assert(auto_fetch_tick_due(&c, 0) == true);

    // Huge intervals clamp to 168h (10080 ticks); no overflow to negative.
    c = 10079;
    mu_assert(auto_fetch_tick_due(&c, 1000000) == true);
    mu_assert_int_eq(c, 0);
    c = 10078;
    mu_assert(auto_fetch_tick_due(&c, INT_MAX) == false);
    mu_assert_int_eq(c, 10079);
}

// --- hashing -----------------------------------------------------------------

MU_TEST(fnv_hex_vectors) {
    // FNV-1a 64-bit reference vectors.
    char out[AF_HASH_HEX_SIZE];
    fnv_hex((const uint8_t *)"", 0, out);
    mu_assert_str_eq(out, "cbf29ce484222325");
    fnv_hex((const uint8_t *)"a", 1, out);
    mu_assert_str_eq(out, "af63dc4c8601ec8c");
    fnv_hex((const uint8_t *)"hello", 5, out);
    mu_assert_str_eq(out, "a430d84680aabd0b");
    const char *fox = "The quick brown fox jumps over the lazy dog";
    fnv_hex((const uint8_t *)fox, strlen(fox), out);
    mu_assert_str_eq(out, "f3f9b7f5e7e47110");
    mu_assert_int_eq((int)strlen(out), 16);  // fits AF_HASH_HEX_SIZE incl. NUL
}

// --- dashboard fetch flow ------------------------------------------------------

MU_TEST(dashboard_fetch_no_url) {
    reset_all();
    mu_assert(do_dashboard_fetch() == false);  // nothing configured
    mu_assert_int_eq(mock_image_fetch_start_count, 0);
}

MU_TEST(dashboard_fetch_changed_displays) {
    reset_all();
    config_set_str(KEY_DASHBOARD_URL, "https://example.com/w.jpg");
    mock_http_body = "hello";
    mock_image_fetch_start_result = true;
    mock_image_fetch_result_ok = true;
    mu_assert(do_dashboard_fetch() == true);
    mu_assert_int_eq(mock_image_fetch_start_count, 1);
    mu_assert_str_eq(mock_image_fetch_last_url, "https://example.com/w.jpg");
    // Hash of the displayed image must be stored for change detection.
    char want[AF_HASH_HEX_SIZE], got[64];
    fnv_hex((const uint8_t *)"hello", 5, want);
    mu_assert(config_get_str(KEY_IMAGE_HASH, got, sizeof(got)) == true);
    mu_assert_str_eq(got, want);
}

MU_TEST(dashboard_fetch_unchanged_skips) {
    reset_all();
    config_set_str(KEY_DASHBOARD_URL, "https://example.com/w.jpg");
    char want[AF_HASH_HEX_SIZE];
    fnv_hex((const uint8_t *)"hello", 5, want);
    config_set_str(KEY_IMAGE_HASH, want);  // pretend we already showed it
    mock_http_body = "hello";
    mu_assert(do_dashboard_fetch() == true);
    mu_assert_int_eq(mock_image_fetch_start_count, 0);  // skipped: unchanged
}

MU_TEST(dashboard_fetch_http_failure) {
    reset_all();
    config_set_str(KEY_DASHBOARD_URL, "https://example.com/w.jpg");
    mock_http_perform_result = ESP_FAIL;
    mu_assert(do_dashboard_fetch() == true);  // attempted; screen keeps old image
    mu_assert_int_eq(mock_image_fetch_start_count, 0);
    char got[64];
    mu_assert(config_get_str(KEY_IMAGE_HASH, got, sizeof(got)) == false);
}

MU_TEST(dashboard_fetch_start_failure_keeps_hash) {
    // If display fails to start, the old hash must be kept so the next
    // interval retries instead of believing the new image was shown.
    reset_all();
    config_set_str(KEY_DASHBOARD_URL, "https://example.com/w.jpg");
    mock_http_body = "hello";
    mock_image_fetch_start_result = false;
    mu_assert(do_dashboard_fetch() == true);
    mu_assert_int_eq(mock_image_fetch_start_count, 1);
    char got[64];
    mu_assert(config_get_str(KEY_IMAGE_HASH, got, sizeof(got)) == false);
}

MU_TEST(dashboard_fetch_done_callback) {
    reset_all();
    image_fetch_result_t r;
    r.ok = true; r.code = "ok"; r.message = "mock";
    char *h = strdup("abc123");
    dashboard_fetch_done(&r, h);  // callback frees h
    char got[64];
    mu_assert(config_get_str(KEY_IMAGE_HASH, got, sizeof(got)) == true);
    mu_assert_str_eq(got, "abc123");

    r.ok = false;
    char *h2 = strdup("deadbeef");
    dashboard_fetch_done(&r, h2);  // failed display: hash must not change
    mu_assert(config_get_str(KEY_IMAGE_HASH, got, sizeof(got)) == true);
    mu_assert_str_eq(got, "abc123");
}

// --- HTTP layer -----------------------------------------------------------------

MU_TEST(http_get_uses_cert_bundle) {
    // CRITICAL-2 regression: the pre-fetch GET must attach the TLS cert
    // bundle or every https:// fetch fails the handshake.
    reset_all();
    mock_http_body = "data123";
    size_t len = 0;
    uint8_t *p = auto_fetch_http_get("https://example.com/f.jpg", &len);
    mu_assert(p != NULL);
    mu_assert_int_eq((long long)len, 7);
    mu_assert(memcmp(p, "data123", 7) == 0);
    free(p);
    mu_assert_int_eq(mock_http_init_count, 1);
    mu_assert(mock_http_last_config.crt_bundle_attach != NULL);
}

MU_TEST(http_get_handles_http_error) {
    reset_all();
    mock_http_status_code = 500;
    mock_http_body = "x";
    size_t len = 0;
    mu_assert(auto_fetch_http_get("https://example.com/f.jpg", &len) == NULL);
}

// --- task creation ---------------------------------------------------------------

MU_TEST(init_task_create_failure_then_success) {
    // HIGH-1: if xTaskCreate fails, init must not mark itself initialized;
    // a later retry must still create the scheduler task.
    mock_nvs_reset();
    mock_task_reset();
    mock_task_create_result = pdFAIL;
    auto_fetch_init();
    mu_assert_int_eq(mock_task_create_count, 2);  // log_fwd + scheduler attempted
    mock_task_create_result = pdPASS;
    auto_fetch_init();
    mu_assert_int_eq(mock_task_create_count, 4);
    mu_assert_str_eq(mock_last_task_name, "auto_fetch");
}

// --- buffer sizes ------------------------------------------------------------------

MU_TEST(buffer_sizes_fit_worst_case) {
    char buf[AF_HOURS_BUF_SIZE];
    int n = snprintf(buf, sizeof(buf), "%d", INT_MAX);
    mu_assert(n > 0 && n < (int)sizeof(buf));
    mu_assert(buf[sizeof(buf) - 1] == '\0');

    char ts[AF_TS_BUF_SIZE];
    n = snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d", 9999, 12, 31, 23, 59);
    mu_assert(n > 0 && n < (int)sizeof(ts));

    char longurl[AF_URL_BUF_SIZE];
    memset(longurl, 'a', sizeof(longurl) - 1);
    longurl[sizeof(longurl) - 1] = '\0';
    char url[AF_URL_BUF_SIZE];
    n = snprintf(url, sizeof(url), "%s", longurl);
    mu_assert(n == (int)sizeof(url) - 1 && n < (int)sizeof(url));
}

int main(void) {
    printf("auto_fetch host unit tests\n");
    MU_RUN(nvs_keys_within_limit);
    MU_RUN(config_layer_rejects_long_key);
    MU_RUN(configure_propagates_set_failure);
    MU_RUN(configure_propagates_commit_failure);
    MU_RUN(configure_noop_with_null_params);
    MU_RUN(configure_stores_url_and_interval);
    MU_RUN(params_valid);
    MU_RUN(get_config_int_safe);
    MU_RUN(sanitize_interval);
    MU_RUN(tick_due);
    MU_RUN(fnv_hex_vectors);
    MU_RUN(dashboard_fetch_no_url);
    MU_RUN(dashboard_fetch_changed_displays);
    MU_RUN(dashboard_fetch_unchanged_skips);
    MU_RUN(dashboard_fetch_http_failure);
    MU_RUN(dashboard_fetch_start_failure_keeps_hash);
    MU_RUN(dashboard_fetch_done_callback);
    MU_RUN(http_get_uses_cert_bundle);
    MU_RUN(http_get_handles_http_error);
    MU_RUN(init_task_create_failure_then_success);
    MU_RUN(buffer_sizes_fit_worst_case);
    MU_SUMMARY();
}
