#include "auto_fetch.h"
#include "config_store.h"
#include "image_fetch.h"

#include <string.h>
#include <stdlib.h>
#include <time.h>
#include <sys/time.h>
#include <limits.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "esp_sntp.h"
#include "esp_netif.h"
#include "esp_system.h"
#include "esp_http_client.h"
#include "esp_crt_bundle.h"
#include "cJSON.h"

static const char *TAG = "auto_fetch";

// Config keys (NVS, survive reboot)
#define KEY_DASHBOARD_URL "af_url"
#define KEY_FETCH_HOURS   "af_fetch_hours"   // string int
#define KEY_IMAGE_HASH    "af_image_hash"    // hex FNV-1a of last displayed image
#define KEY_RESET_REASON  "af_reset_reason"  // reset reason string from last boot
#define KEY_LAST_FETCH    "af_last_fetch"    // "YYYY-MM-DD HH:MM" of last attempt

// Defaults
#define DEFAULT_FETCH_HOURS 3

// Chicago timezone (CST/CDT with DST rules)
#define CHICAGO_TZ "CST6CDT,M3.2.0,M11.1.0"

// Buffer sizes: single source of truth. Host unit tests assert worst-case
// formatted output fits; do not shrink without updating esp32/test/.
#define AF_URL_BUF_SIZE   512
#define AF_TS_BUF_SIZE    64
#define AF_HOURS_BUF_SIZE 16
#define AF_HASH_HEX_SIZE  17

// NVS keys must stay under NVS_KEY_NAME_MAX_SIZE (16 incl. NUL on ESP-IDF).
// esp32/test/check_nvs_keys.py enforces this at build time.

// Forward declarations
static void log_forward_init(void);

static bool s_initialized = false;
static bool s_sntp_started = false;

static void init_sntp(void) {
    if (s_sntp_started) return;
    s_sntp_started = true;
    ESP_LOGI(TAG, "Initializing SNTP");
    esp_sntp_setoperatingmode(SNTP_OPMODE_POLL);
    esp_sntp_setservername(0, "pool.ntp.org");
    esp_sntp_setservername(1, "time.google.com");
    esp_sntp_init();
    setenv("TZ", CHICAGO_TZ, 1);
    tzset();
}

static bool get_local_time(struct tm *out) {
    time_t now = 0;
    time(&now);
    // Not synced yet if before 2020
    if (now < 1577836800) return false;
    localtime_r(&now, out);
    return true;
}

static int get_config_int(const char *key, int dflt) {
    char buf[AF_HOURS_BUF_SIZE];
    if (!config_get_str(key, buf, sizeof(buf))) {
        return dflt;
    }
    // strtol, not atoi: atoi on out-of-range input is undefined behavior.
    char *end = NULL;
    long v = strtol(buf, &end, 10);
    if (end == buf || *end != '\0' || v < INT_MIN || v > INT_MAX) {
        return dflt;
    }
    return (int)v;
}

// Map esp_reset_reason_t to a short string for NVS logging.
static const char *reset_reason_str(esp_reset_reason_t r) {
    switch (r) {
        case ESP_RST_UNKNOWN:   return "unknown";
        case ESP_RST_POWERON:    return "poweron";
        case ESP_RST_EXT:        return "ext_pin";
        case ESP_RST_SW:         return "software";
        case ESP_RST_PANIC:      return "panic";
        case ESP_RST_INT_WDT:    return "int_wdt";
        case ESP_RST_TASK_WDT:   return "task_wdt";
        case ESP_RST_WDT:        return "wdt";
        case ESP_RST_DEEPSLEEP:   return "deepsleep";
        case ESP_RST_BROWNOUT:   return "brownout";
        case ESP_RST_SDIO:       return "sdio";
        default:                 return "other";
    }
}

// Record the boot reset reason to NVS so a later status query can report
// why the device rebooted (panic, watchdog, brownout, etc.).
static void log_reset_reason(void) {
    esp_reset_reason_t r = esp_reset_reason();
    const char *s = reset_reason_str(r);
    ESP_LOGI(TAG, "Boot reset reason: %s", s);
    config_set_str(KEY_RESET_REASON, s);
}

// FNV-1a 64-bit hash as hex (for change detection, not security)
static void fnv_hex(const uint8_t *data, size_t len, char out_hex[AF_HASH_HEX_SIZE]) {
    uint64_t h = 14695981039346656037ULL;
    for (size_t i = 0; i < len; i++) {
        h ^= data[i];
        h *= 1099511628211ULL;
    }
    sprintf(out_hex, "%016llx", (unsigned long long)h);
}

// Fetch URL into memory, return buffer (caller frees) or NULL.
static uint8_t *fetch_url(const char *url, size_t *out_len) {
    extern uint8_t *auto_fetch_http_get(const char *url, size_t *out_len);
    return auto_fetch_http_get(url, out_len);
}

// Completion callback for dashboard fetches. Stores the image hash only
// after successful display, so a failed render retries next interval.
static void dashboard_fetch_done(const image_fetch_result_t *result, void *user) {
    char *hash = (char *)user;
    if (result && result->ok) {
        config_set_str(KEY_IMAGE_HASH, hash);
        ESP_LOGI(TAG, "Dashboard displayed, hash stored");
    } else {
        ESP_LOGW(TAG, "Dashboard display failed: %s %s",
                 result && result->code ? result->code : "?",
                 result && result->message ? result->message : "?");
    }
    free(hash);
}

// Fetch the dashboard, display it only if the content changed.
// Returns true if a fetch was attempted (regardless of outcome).
static bool do_dashboard_fetch(bool force) {
    char url[AF_URL_BUF_SIZE];
    if (!config_get_str(KEY_DASHBOARD_URL, url, sizeof(url))) {
        ESP_LOGI(TAG, "Dashboard fetch skipped: no URL configured");
        return false;
    }

    // Record attempt time
    struct tm tm_now;
    if (get_local_time(&tm_now)) {
        char ts[AF_TS_BUF_SIZE];
        snprintf(ts, sizeof(ts), "%04d-%02d-%02d %02d:%02d",
                 tm_now.tm_year + 1900, tm_now.tm_mon + 1, tm_now.tm_mday,
                 tm_now.tm_hour, tm_now.tm_min);
        config_set_str(KEY_LAST_FETCH, ts);
    }

    ESP_LOGI(TAG, "Fetching dashboard: %s", url);
    size_t len = 0;
    uint8_t *data = fetch_url(url, &len);
    if (!data || len == 0) {
        ESP_LOGW(TAG, "Dashboard fetch failed (network or non-200)");
        free(data);
        return true;  // attempted, will retry next interval; screen keeps old image
    }

    char hash[AF_HASH_HEX_SIZE];
    fnv_hex(data, len, hash);
    free(data);

    if (!force) {
        char last_hash[40];
        if (config_get_str(KEY_IMAGE_HASH, last_hash, sizeof(last_hash)) &&
            strcmp(last_hash, hash) == 0) {
            ESP_LOGI(TAG, "Dashboard unchanged, skipping display");
            return true;
        }
    }

    ESP_LOGI(TAG, "Dashboard changed, displaying");
    const char *code = NULL, *msg = NULL;
    // Pass a heap copy of the hash via user pointer; the callback stores it
    // only after successful display, and frees the copy. If start fails,
    // keep the old hash so the next interval retries.
    char *hash_copy = strdup(hash);
    if (!hash_copy) {
        ESP_LOGW(TAG, "Dashboard display failed: out_of_memory (hash copy)");
        return true;
    }
    if (!image_fetch_start(url, IMAGE_FETCH_DEFAULT_ROW, dashboard_fetch_done,
                           hash_copy, &code, &msg)) {
        ESP_LOGW(TAG, "Dashboard display failed to start: %s %s",
                 code ? code : "?", msg ? msg : "?");
        free(hash_copy);
    }
    return true;
}

// Clamp a raw interval to the supported range [1, 168] hours (hourly..weekly).
// Non-static so host unit tests can cover it.
int auto_fetch_sanitize_interval(int raw_hours) {
    if (raw_hours < 1) return 1;
    if (raw_hours > 168) return 168;
    return raw_hours;
}

// Scheduler tick bookkeeping. Call once per 60-second tick; returns true when
// a fetch is due and resets the counter. Non-static so host unit tests cover it.
bool auto_fetch_tick_due(int *tick_counter, int interval_hours) {
    int interval = auto_fetch_sanitize_interval(interval_hours);
    (*tick_counter)++;
    if (*tick_counter >= interval * 60) {
        *tick_counter = 0;
        return true;
    }
    return false;
}

// True when at least one fetch parameter was supplied. Non-static for tests.
bool auto_fetch_params_valid(const char *dashboard_url, int fetch_interval_hours) {
    return dashboard_url != NULL || fetch_interval_hours != 0;
}

static void scheduler_task(void *arg) {
    (void)arg;
    ESP_LOGI(TAG, "Auto-fetch scheduler started");
    // Wait for network stack before starting SNTP
    vTaskDelay(pdMS_TO_TICKS(10000));
    init_sntp();

    // Fetch immediately on first sync (recovers quickly after a reboot),
    // then on the configured interval.
    bool first_fetch_done = false;
    int tick_counter = 0;

    while (1) {
        // Check every 60 seconds
        vTaskDelay(pdMS_TO_TICKS(60000));

        struct tm tm_now;
        if (!get_local_time(&tm_now)) {
            continue;  // NTP not synced yet
        }

        int interval = get_config_int(KEY_FETCH_HOURS, DEFAULT_FETCH_HOURS);

        if (!first_fetch_done) {
            // Only mark first fetch done if a URL is configured.
            // Otherwise retry in 60s until the user configures one.
            char url_check[AF_URL_BUF_SIZE];
            if (config_get_str(KEY_DASHBOARD_URL, url_check, sizeof(url_check))) {
                do_dashboard_fetch(true);
                first_fetch_done = true;
                tick_counter = 0;
            }
            continue;
        }

        if (auto_fetch_tick_due(&tick_counter, interval)) {
            do_dashboard_fetch(false);
        }
    }
}

void auto_fetch_init(void) {
    if (s_initialized) return;
    log_reset_reason();
    log_forward_init();
    if (xTaskCreate(scheduler_task, "auto_fetch", 8192, NULL, 3, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create auto-fetch scheduler task");
        return;
    }
    s_initialized = true;
    ESP_LOGI(TAG, "Auto-fetch initialized");
}

bool auto_fetch_configure(const char *dashboard_url, int fetch_interval_hours) {
    // Propagate config failures: callers (and the user) must not be told
    // "ok" when nothing was persisted (see CRITICAL-3 review).
    bool ok = true;
    if (dashboard_url) {
        if (!config_set_str(KEY_DASHBOARD_URL, dashboard_url)) ok = false;
    }
    if (fetch_interval_hours > 0) {
        char buf[AF_HOURS_BUF_SIZE];
        snprintf(buf, sizeof(buf), "%d", fetch_interval_hours);
        if (!config_set_str(KEY_FETCH_HOURS, buf)) ok = false;
    }
    return ok;
}

// ---- Remote log forwarding (temporary diagnostics) ----
#define LOG_SERVER_URL "https://bruceburge.com/e1002/log-receiver.php"
#define LOG_BUF_SIZE 8192
#define LOG_SEND_INTERVAL_MS 300000  // 5 minutes

static char s_log_buf[LOG_BUF_SIZE];
static size_t s_log_len = 0;
static SemaphoreHandle_t s_log_mutex = NULL;
static vprintf_like_t s_orig_vprintf = NULL;

static int log_capture_vprintf(const char *fmt, va_list args) {
    // Call original first so USB serial still works
    int ret = 0;
    if (s_orig_vprintf) {
        va_list args_copy;
        va_copy(args_copy, args);
        ret = s_orig_vprintf(fmt, args_copy);
        va_end(args_copy);
    }
    // Also capture into ring buffer (skip heartbeat spam)
    if (s_log_mutex && xSemaphoreTake(s_log_mutex, 0) == pdTRUE) {
        char tmp[256];
        int n = vsnprintf(tmp, sizeof(tmp), fmt, args);
        if (n > 0 && strstr(tmp, "link.heartbeat") == NULL) {
            // Strip ANSI color codes for cleaner server logs
            // Simple approach: copy printable chars, skip ESC sequences
            for (int i = 0; i < n && tmp[i]; i++) {
                if (tmp[i] == '\x1b') {
                    // Skip until 'm'
                    while (tmp[i] && tmp[i] != 'm') i++;
                    continue;
                }
                if (s_log_len < LOG_BUF_SIZE - 1) {
                    s_log_buf[s_log_len++] = tmp[i];
                }
            }
            // If buffer full, drop oldest by shifting (simple, infrequent)
            if (s_log_len >= LOG_BUF_SIZE - 256) {
                size_t drop = 2048;
                memmove(s_log_buf, s_log_buf + drop, s_log_len - drop);
                s_log_len -= drop;
            }
        }
        xSemaphoreGive(s_log_mutex);
    }
    return ret;
}

static void log_sender_task(void *arg) {
    (void)arg;
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(LOG_SEND_INTERVAL_MS));
        if (s_log_len == 0) continue;
        if (!s_log_mutex) continue;

        // Copy buffer under lock
        char *copy = NULL;
        size_t copy_len = 0;
        if (xSemaphoreTake(s_log_mutex, pdMS_TO_TICKS(1000)) == pdTRUE) {
            copy_len = s_log_len;
            copy = malloc(copy_len + 1);
            if (copy) {
                memcpy(copy, s_log_buf, copy_len);
                copy[copy_len] = '\0';
                s_log_len = 0;
            }
            xSemaphoreGive(s_log_mutex);
        }
        if (!copy) continue;

        // POST to server using image_fetch's HTTP client if available,
        // otherwise use esp_http_client directly
        esp_http_client_config_t config = {
            .url = LOG_SERVER_URL,
            .method = HTTP_METHOD_POST,
            .crt_bundle_attach = esp_crt_bundle_attach,
            .timeout_ms = 15000,
        };
        esp_http_client_handle_t client = esp_http_client_init(&config);
        if (client) {
            esp_http_client_set_header(client, "Content-Type", "text/plain");
            esp_http_client_set_post_field(client, copy, copy_len);
            esp_err_t err = esp_http_client_perform(client);
            if (err != ESP_OK) {
                ESP_LOGW(TAG, "Log forward failed: %s", esp_err_to_name(err));
            }
            esp_http_client_cleanup(client);
        }
        free(copy);
    }
}

static void log_forward_init(void) {
    s_log_mutex = xSemaphoreCreateMutex();
    if (!s_log_mutex) return;
    if (xTaskCreate(log_sender_task, "log_fwd", 8192, NULL, 2, NULL) != pdPASS) {
        ESP_LOGE(TAG, "Failed to create log sender task");
        vSemaphoreDelete(s_log_mutex);
        s_log_mutex = NULL;
        return;
    }
    s_orig_vprintf = esp_log_set_vprintf(log_capture_vprintf);
    ESP_LOGI(TAG, "Remote log forwarding initialized");
}
