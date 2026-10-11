#pragma once
// Scheduled dashboard auto-fetch for the E1002.
// Fetches a single dashboard image URL on an interval, redrawing only when
// the content changes (hash comparison preserves e-paper panel life).
// The server bakes alert state into the image; the device just displays it.

#ifdef __cplusplus
extern "C" {
#endif

// Initialize the auto-fetch subsystem. Starts SNTP and the scheduler task.
// Safe to call before WiFi is up; fetching waits for connectivity.
// Logs the boot reset reason to NVS for crash diagnosis.
void auto_fetch_init(void);

// Configure the fetch schedule. Any NULL parameter leaves that setting unchanged.
// dashboard_url: fixed URL for the dashboard image (e.g. https://bruceburge.com/e1002/weather.jpg)
// fetch_interval_hours: how often to check the URL (e.g. 3)
// Returns false if a supplied value could not be persisted.
bool auto_fetch_configure(const char *dashboard_url, int fetch_interval_hours);

// Testable helpers (also used by the scheduler and the command handler).
// Exposed for host unit tests; safe to call on device too.
int auto_fetch_sanitize_interval(int raw_hours);
bool auto_fetch_tick_due(int *tick_counter, int interval_hours);
bool auto_fetch_params_valid(const char *dashboard_url, int fetch_interval_hours);

#ifdef __cplusplus
}
#endif
