#pragma once
// Test-hook declarations for the image_fetch_start host mock.
// (The mock itself implements the real ../main/image_fetch.h API.)
#include <stdbool.h>

extern int mock_image_fetch_start_count;
extern char mock_image_fetch_last_url[512];
extern bool mock_image_fetch_start_result;  // false: start fails
extern bool mock_image_fetch_result_ok;     // result delivered to callback
void mock_image_fetch_reset(void);
