#!/usr/bin/env python3
"""Build-time check: every NVS key used in esp32/main must fit ESP-IDF's
NVS_KEY_NAME_MAX_SIZE (16 bytes *including* the NUL terminator), i.e. at most
15 characters. A key that is too long makes nvs_set_str() fail with
ESP_ERR_NVS_KEY_TOO_LONG, silently dropping config (this exact bug shipped
in auto_fetch.c as "af_dashboard_url", 16 chars).

Scans:
  1. #define macros whose name contains KEY with a string value.
  2. String literals passed directly to config_set_str/get_str/erase_key/
     key_lookup().

Exits non-zero on any violation so `make test` fails.
"""
import pathlib
import re
import sys

MAX_LEN = 15  # NVS_KEY_NAME_MAX_SIZE (16) minus NUL

main_dir = pathlib.Path(sys.argv[1] if len(sys.argv) > 1 else "../main")
if not main_dir.is_dir():
    print(f"check_nvs_keys.py: not a directory: {main_dir}")
    sys.exit(2)

macro_re = re.compile(r'#define\s+([A-Za-z_][A-Za-z0-9_]*)\s+"([^"]+)"')
# (macro names are filtered for "KEY" below; the regex above must not
# require KEY at a fixed position -- names like KEY_FOO start with it)
use_re = re.compile(
    r'config_(?:set_str|get_str|erase_key|key_lookup)\s*\(\s*([A-Za-z_][A-Za-z0-9_]*|"[^"]*")'
)

macros = {}  # name -> (value, file, line)
violations = []

files = sorted(main_dir.glob("*.h")) + sorted(main_dir.glob("*.c")) + \
        sorted(main_dir.glob("*.cpp"))
for f in files:
    try:
        lines = f.read_text(errors="replace").splitlines()
    except OSError:
        continue
    for i, line in enumerate(lines, 1):
        m = macro_re.search(line)
        if m:
            name, val = m.group(1), m.group(2)
            if "KEY" not in name:
                continue
            macros[name] = (val, str(f), i)
            if len(val) > MAX_LEN:
                violations.append(
                    f"{f}:{i}: NVS key macro {name} = {val!r} "
                    f"is {len(val)} chars (max {MAX_LEN})"
                )

for f in files:
    try:
        lines = f.read_text(errors="replace").splitlines()
    except OSError:
        continue
    for i, line in enumerate(lines, 1):
        for m in use_re.finditer(line):
            arg = m.group(1)
            if arg.startswith('"'):
                key = arg[1:-1]
                if len(key) > MAX_LEN:
                    violations.append(
                        f"{f}:{i}: NVS key literal {arg} is {len(key)} chars "
                        f"(max {MAX_LEN})"
                    )
            # macro args were already validated at their #define site

if violations:
    print("NVS key length violations (max 15 chars):")
    for v in violations:
        print("  " + v)
    sys.exit(1)

print(f"OK: {len(macros)} NVS key macros checked, all <={MAX_LEN} chars")
