# E1002 Auto-Fetch Host Unit Tests

Host-based unit tests for the E1002 dashboard auto-fetch firmware
(`esp32/main/auto_fetch.c`, `config_store.c`, `auto_fetch_http.c`).
They run on Linux -- nothing here touches the ESP32.

## Why this exists

A series of bugs shipped in this code because ESP-IDF API constraints were
violated silently:

- an NVS key of 16 chars (`af_dashboard_url`) exceeded the 15-char
  `NVS_KEY_NAME_MAX_SIZE` limit, so `nvs_set_str` failed and the schedule
  was never persisted;
- `auto_fetch_configure()` ignored the `config_set_str()` failures and
  returned `true`, and the `display.auto_fetch` handler reported `ok:true`
  anyway -- the failure was invisible;
- the HTTPS pre-fetch in `auto_fetch_http.c` never attached the TLS cert
  bundle, so every `https://` fetch would fail the handshake;
- `xTaskCreate()` return values were ignored;
- `atoi()` on NVS/config strings (overflow is undefined behavior) and an
  unclamped `interval * 60` in the scheduler.

Every test below is a regression test for one of those classes.

## Running

On the Debian build box (`miniforumdeb13`):

```sh
cd ~/muse-gadget-sdk/esp32/test
make test     # static NVS-key scan + build + run (use this; also the CI entry)
make check    # only the static NVS-key source scan
make run      # only build + run the test binary
make clean    # remove the test binary
```

`make test` exits non-zero if the key scan fails or any test fails.

## How it works

- `test_auto_fetch.c` **#includes `../main/auto_fetch.c` directly**, so
  `static` functions (`get_config_int`, `fnv_hex`, `do_dashboard_fetch`,
  `dashboard_fetch_done`) are testable without changing production visibility.
- `mocks/` replaces ESP-IDF / FreeRTOS headers with host implementations:
  - `mock_nvs.c` -- in-memory fake NVS that **replicates the real
    15-char key limit** and supports failure injection
    (`mock_nvs_fail_set_str`, `mock_nvs_fail_commit`, `mock_nvs_fail_open`).
  - `mock_http_client.c` -- fake `esp_http_client`; records the init config
    (so tests can assert `crt_bundle_attach` is set) and synthesizes
    response bodies via `mock_http_body` / `mock_http_status_code` /
    `mock_http_perform_result`.
  - `mock_image_fetch.c` -- stub `image_fetch_start` using the **real**
    `../main/image_fetch.h`, so callback types always match; invokes the
    completion callback synchronously.
  - `mock_freertos.c`, `mock_esp.c` -- task/semaphore/log/sntp/reset-reason
    stubs with hooks (`mock_task_create_result`, ...).
- `check_nvs_keys.py` is a **build-time** scan: it parses every `#define`
  NVS key macro and every string literal passed to `config_set_str()` /
  `config_get_str()` / `config_erase_key()` / `config_key_lookup()` in
  `esp32/main/` and fails the build if any key exceeds 15 chars. This
  catches the original `af_dashboard_url` bug class in *any* file, even
  ones with no unit tests.
- `minunit.h` is a tiny assertion framework (no dependencies).

## What each test covers

| Test | Bug class |
|---|---|
| `nvs_keys_within_limit` | NVS key length (runtime, all 5 `af_` keys) |
| `config_layer_rejects_long_key` | `config_set_str` surfaces `KEY_TOO_LONG` as `false` |
| `configure_propagates_set_failure` | `auto_fetch_configure` returns `false` on write failure |
| `configure_propagates_commit_failure` | ... on commit failure |
| `configure_noop_with_null_params` | NULL/0 is a no-op success (handler rejects empties separately) |
| `configure_stores_url_and_interval` | values actually land in NVS |
| `params_valid` | `auto_fetch_params_valid` truth table |
| `get_config_int_safe` | `strtol` vs `atoi`: garbage/empty/overflow input -> default, no UB |
| `sanitize_interval` | interval clamped to [1, 168] |
| `tick_due` | scheduler fires on the 60th tick per hour; no overflow on huge input |
| `fnv_hex_vectors` | FNV-1a 64 reference vectors; output fits 17-byte buffer |
| `dashboard_fetch_no_url` | no URL configured -> no fetch attempted |
| `dashboard_fetch_changed_displays` | changed image -> `image_fetch_start` called, hash stored |
| `dashboard_fetch_unchanged_skips` | unchanged image -> display skipped (e-paper wear) |
| `dashboard_fetch_http_failure` | network failure -> attempted, nothing displayed |
| `dashboard_fetch_start_failure_keeps_hash` | display start failure -> old hash kept, retries next interval |
| `dashboard_fetch_done_callback` | callback stores hash only on success |
| `http_get_uses_cert_bundle` | TLS cert bundle attached (CRITICAL-2 regression) |
| `http_get_handles_http_error` | non-200 -> NULL |
| `init_task_create_failure_then_success` | failed `xTaskCreate` doesn't stick `s_initialized`; retry works |
| `buffer_sizes_fit_worst_case` | worst-case `snprintf` output fits `AF_*_BUF_SIZE` |

## Production changes made for testability (same behavior on device)

- `auto_fetch.c`: `get_config_int` uses `strtol` (no `atoi` UB);
  new non-static helpers `auto_fetch_sanitize_interval()`,
  `auto_fetch_tick_due()`, `auto_fetch_params_valid()`; scheduler uses the
  tick helper; `auto_fetch_configure()` propagates `config_set_str()`
  failures; `xTaskCreate()` results checked in `auto_fetch_init()` /
  `log_forward_init()`; named `AF_*_BUF_SIZE` constants for buffers.
- `auto_fetch.h`: declarations for the new helpers.
- `app.c`: `display.auto_fetch` handler uses `auto_fetch_params_valid()`
  (same `missing_param` behavior as before).

## Adding a new test

1. Add a `MU_TEST(my_test) { ... }` block in `test_auto_fetch.c` using
   `mu_assert`, `mu_assert_int_eq`, `mu_assert_str_eq`.
2. Register it in `main()` with `MU_RUN(my_test);`.
3. If the test needs new fake behavior, extend the matching mock in
   `mocks/` (keep mocks faithful to the real ESP-IDF constraint you are
   testing -- e.g. the NVS key-length limit).
4. Run `make test`.

## Notes / limits

- Timing: `get_local_time()` uses the real host clock; tests don't depend on it.
- The scheduler task itself is never run (infinite loop); its tick math is
  tested via `auto_fetch_tick_due()`.
- `check_nvs_keys.py` only resolves macro and literal keys; keys built at
  runtime (e.g. `snprintf`'d) are out of scope -- prefer string literals or
  `#define`s for NVS keys.
