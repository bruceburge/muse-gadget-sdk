# Host unit tests for the E1002 auto-fetch firmware.
#
# These tests run on Linux (not on the ESP32). ESP-IDF / FreeRTOS APIs are
# replaced by the mocks in mocks/, and the production translation unit
# ../main/auto_fetch.c is #included directly so static functions are testable.
#
# Usage:
#   make test    # static NVS-key check + build + run (use this in CI)
#   make check   # only the static NVS-key source scan
#   make run     # only build + run the test binary
#   make clean
CC      ?= gcc
MAINDIR  = ../../main
MOCKDIR  = mocks
CFLAGS   = -std=c11 -D_GNU_SOURCE -Wall -Wextra -g -I$(MOCKDIR) -I$(MAINDIR)
SRCS     = test_auto_fetch.c \
           $(MOCKDIR)/mock_esp.c \
           $(MOCKDIR)/mock_freertos.c \
           $(MOCKDIR)/mock_nvs.c \
           $(MOCKDIR)/mock_http_client.c \
           $(MOCKDIR)/mock_image_fetch.c \
           $(MAINDIR)/config_store.c \
           $(MAINDIR)/auto_fetch_http.c
BIN      = run_tests

.PHONY: test check run clean

test: check $(BIN)
	./$(BIN)

check:
	python3 check_nvs_keys.py $(MAINDIR)

run: $(BIN)
	./$(BIN)

$(BIN): $(SRCS) minunit.h
	$(CC) $(CFLAGS) -o $@ $(SRCS)

clean:
	rm -f $(BIN)
