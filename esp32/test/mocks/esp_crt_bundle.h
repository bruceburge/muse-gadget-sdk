#pragma once
// Host mock of ESP-IDF esp_crt_bundle.h
#include "esp_err.h"
esp_err_t esp_crt_bundle_attach(void *conf);
