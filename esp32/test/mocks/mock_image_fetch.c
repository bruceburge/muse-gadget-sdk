// Host mock of image_fetch_start: records the call and synchronously
// invokes the completion callback so do_dashboard_fetch() can be tested.
// Uses the REAL ../main/image_fetch.h so callback types always match.
#include "image_fetch.h"
#include <string.h>

int mock_image_fetch_start_count = 0;
char mock_image_fetch_last_url[512] = {0};
bool mock_image_fetch_start_result = true;
bool mock_image_fetch_result_ok = true;

void mock_image_fetch_reset(void) {
    mock_image_fetch_start_count = 0;
    mock_image_fetch_last_url[0] = '\0';
    mock_image_fetch_start_result = true;
    mock_image_fetch_result_ok = true;
}

// Test-hook declarations (kept here; the real header has no hook decls).
// They are also declared in test_auto_fetch.c via the mock header path;
// to avoid divergence, tests include this file's declarations through
// "mock_image_fetch_hooks.h".
bool image_fetch_start(const char *url, int row, image_fetch_done_cb done,
                       void *user, const char **code, const char **message) {
    (void)row;
    mock_image_fetch_start_count++;
    if (url) {
        strncpy(mock_image_fetch_last_url, url, sizeof(mock_image_fetch_last_url) - 1);
        mock_image_fetch_last_url[sizeof(mock_image_fetch_last_url) - 1] = '\0';
    }
    if (!mock_image_fetch_start_result) {
        if (code) *code = "busy";
        if (message) *message = "mock start failure";
        return false;
    }
    if (done) {
        image_fetch_result_t r;
        memset(&r, 0, sizeof(r));
        r.ok = mock_image_fetch_result_ok;
        r.code = "ok";
        r.message = "mock";
        done(&r, user);
    }
    return true;
}
