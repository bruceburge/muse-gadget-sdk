#pragma once
// Host mock of ESP-IDF nvs_flash.h
#include "esp_err.h"
#define NVS_DEFAULT_PART_NAME "nvs"
esp_err_t nvs_flash_init(void);
esp_err_t nvs_flash_init_partition(const char *part_name);
