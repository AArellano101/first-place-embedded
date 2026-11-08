# Table-box validation record

Date: **2026-11-08 UTC**. Installed SDK: **ESP-IDF 5.5.2**. Target: **esp32**.
This record describes the current Wi-Fi table-light implementation. The previous
standalone beacon results are archived under `legacy/standalone/VALIDATION.md`.

## Completed

| Verification | Result / scope |
| --- | --- |
| ESP32 firmware build | PASS: compilation, linking, binary generation and partition-size check; `build/meow.bin` |
| Native C firmware tests | PASS with GCC C11, `-Wall -Wextra -Werror -pedantic` |
| Localhost HTTP fixture tests | PASS: real host HTTP requests feed decoded response fragments into the actual C firmware parser |
| Parser/config allocation soak | PASS: 10,000 success/failure/config cycles, zero retained cJSON allocations after each cycle |
| Provisioning tool test | PASS: simulated USB response, hidden password/key prompts, no printed test credentials |
| QR tool test | PASS: real qrcode 8.2 library creates a white-background SVG and matching URI text for a synthetic read-back UUID |
| Python syntax checks | PASS for provisioning, mock backend and test runners |
| Git whitespace check | PASS |

The C checks cover all four colors, boot-dark behavior, smooth yellow intensity,
unchanged pulse phase on repeated yellow, stale-state cutoff, faults/recovery,
response/control-tick timing, 64-bit uptime, bounded retry timing, arbitrary
fragment boundaries, complete/incomplete transfers, oversized/NUL/deeply nested
or malformed JSON, duplicate fields, unknown/legacy colors, malformed numeric
spellings, UUID/base-URL/device-key validation and table URI identity.

The HTTP fixtures cover off/green/yellow/red, fragmented/chunked transfer,
malformed/unknown/oversized/truncated bodies, 400/401/404/500/redirect handling,
wrong key, malformed UUID, unknown valid UUID returning off, a stalled request
and recovery. They use a host HTTP client plus the firmware parser, **not** the
ESP32 Wi-Fi radio or ESP-IDF HTTP client's runtime. The independent freshness
behavior during network stalls is verified at the model level with simulated
monotonic time.

The active build is three discrete PWM lamps, 50% brightness, 2-second yellow
cycle, separate status LED GPIO23, optional SSD1306 enabled, size optimization,
and HTTPS certificate bundle enabled. Firmware contains no real provisioning.
The final image is `0xe40e0` bytes, with 11% of the existing app partition free.
The analog-RGB/active-low configuration is available but has not been physically
verified. No real table QR has been created; the generated test QR is explicitly
synthetic and lives under `build/host-tests/`, not a production label.

Build log: `build/table-box-build.log`. Build execution used process-local SDK Git
safe-directory settings and disabled the pre-existing compiler cache, with cache
temporary files in `build/ccache-tmp`. No global Git settings were changed.
Tool-test dependencies were installed only under `build/tool-deps` (pyserial 3.5,
qrcode 8.2); a permission difference required running that tool test in the same
context as the installer. USB was simulated and no ESP32 settings were changed.

## Still requires real hardware / service verification

**Not flashed, not provisioned, not physically verified.** The user requested that
configuration remain open until the actual Wi-Fi/API/table UUID/device key exist.

- GPIO wiring, LED polarity/channel order, PWM brightness/pulse, separate status
  indicator, camera disconnection and OLED controller/power/address.
- USB/NVS provisioning and power-cycle persistence on the actual board.
- Real Wi-Fi reconnection and ESP-IDF HTTP/TLS behavior, trusted/untrusted
  certificates, DNS/NTP failure, network stalls and physical ten-second cutoff.
- Long-running ESP-IDF/Wi-Fi/TLS heap behavior; parser allocation tests alone do
  not establish absence of a device-side networking leak.
- Current app/database scenarios: claim -> yellow, selected green -> green,
  conversation -> red, conversation end -> selected color, leave -> off, and
  disabled/incognito/closed-hours -> off.
- Phone scanning of a printed QR and optional passive NFC tag using the assigned
  real UUID. PN532 emulation is not implemented.

The adjacent backend/app source was inspected, but no backend service, app flow
or database migration was run or modified. See `TESTING.md` for reproducible
procedures and expected results for every outstanding acceptance test.
