#pragma once
// Host mock of ESP-IDF esp_efuse.h (only what config_store.c references;
// the encrypted-NVS branch is compiled out without CONFIG_NVS_ENCRYPTION).
#include "esp_err.h"
