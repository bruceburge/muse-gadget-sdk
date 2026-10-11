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
bool auto_fetch_configure(const char *dashboard_url, int fetch_interval_hours);

// Returns current configuration as a JSON string (caller frees).
char *auto_fetch_status_json(void);

#ifdef __cplusplus
}
#endif
