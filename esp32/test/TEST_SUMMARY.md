# Auto-Fetch Test Summary

**21 host unit tests** in `esp32/test/`, all passing. Run with `make test`.

## What They Catch

These are regression tests for bugs that shipped:

| Bug | Test |
|-----|------|
| NVS key `af_dashboard_url` (16 chars) exceeded 15-char limit, config silently never saved | `nvs_keys_within_limit`, `config_layer_rejects_long_key`, plus build-time `check_nvs_keys.py` scan |
| `auto_fetch_configure()` ignored config failures, returned `true` anyway | `configure_propagates_set_failure`, `configure_propagates_commit_failure` |
| HTTPS pre-fetch missing TLS cert bundle | `http_get_uses_cert_bundle` |
| `xTaskCreate()` return ignored | `init_task_create_failure_then_success` |
| `atoi()` overflow undefined behavior | `get_config_int_safe` |
| Unclamped interval causing scheduler overflow | `sanitize_interval`, `tick_due` |

## Full Test List

**NVS & Config:**
- `nvs_keys_within_limit` - all 5 `af_` keys fit 15-char limit
- `config_layer_rejects_long_key` - long keys surface as `false`
- `configure_propagates_set_failure` - write failures propagate
- `configure_propagates_commit_failure` - commit failures propagate
- `configure_noop_with_null_params` - NULL params are safe no-op
- `configure_stores_url_and_interval` - values actually persist
- `params_valid` - parameter validation truth table

**Scheduler & Timing:**
- `get_config_int_safe` - garbage/empty/overflow input handled, no UB
- `sanitize_interval` - clamped to [1, 168] hours
- `tick_due` - fires on 60th tick per hour, no overflow

**Fetch Logic:**
- `fnv_hex_vectors` - FNV-1a hash correctness
- `dashboard_fetch_no_url` - no URL means no fetch
- `dashboard_fetch_changed_displays` - changed image displays, hash stored
- `dashboard_fetch_unchanged_skips` - unchanged skips display (e-paper wear)
- `dashboard_fetch_http_failure` - network failure handled
- `dashboard_fetch_start_failure_keeps_hash` - old hash kept on failure
- `dashboard_fetch_done_callback` - callback stores hash only on success

**HTTP & Tasks:**
- `http_get_uses_cert_bundle` - TLS cert bundle attached
- `http_get_handles_http_error` - non-200 returns NULL
- `init_task_create_failure_then_success` - task failure doesn't stick
- `buffer_sizes_fit_worst_case` - snprintf buffers fit worst case

## Running

```sh
cd esp32/test
make test   # key scan + build + run (CI entry point)
```

## Adding Tests

1. Add `MU_TEST(my_test)` block in `test_auto_fetch.c`
2. Register with `MU_RUN(my_test)` in `main()`
3. Extend mocks in `mocks/` if needed
4. Run `make test`
