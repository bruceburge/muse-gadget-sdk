#pragma once
// Host mock of ESP-IDF esp_sntp.h
#define SNTP_OPMODE_POLL 1
void esp_sntp_setoperatingmode(int mode);
void esp_sntp_setservername(int idx, const char *name);
void esp_sntp_init(void);
