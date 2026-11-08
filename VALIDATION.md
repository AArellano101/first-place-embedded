# Validation record

Date: 2026-11-08 (UTC). Target: classic `esp32`, installed ESP-IDF **5.5.2**.

## Executed successfully

| Check | Result |
| --- | --- |
| Host C tests, production timing | PASS |
| Host C tests, demo timing | PASS |
| ESP32 production build: passive buzzer, local mock enabled | PASS; `build/meow.bin`, size `0x445d0`, 73% of app partition free |
| ESP32 alternate build: demo timing, active buzzer, mock disabled | PASS; `build/validation/meow.bin`, size `0x35490`, 79% of app partition free |

Both firmware builds completed linking, binary generation and partition-size checks.
No compiler warnings or errors were reported. The main `sdkconfig` remains in
production mode with the passive buzzer and mock console enabled. The alternate
build has its own `build/validation.sdkconfig` and does not replace the main image.

Host checks compile the actual `beacon.c`, `input_logic.c`, and `beacon_ui.c` with
GCC, `-std=c11 -Wall -Wextra -Werror -pedantic`, then execute assertions in both
duration configurations. Coverage includes state cycling, accept/skip invariants,
exact invitation IDs, rejection of invalid IDs, clearing/replacement, no duplicate
events, timer locking/cancellation/expiration, 64-bit deadlines, exact logical
beep windows, acknowledgement mid-alert, button bounce and holds, dial clamping,
smoothing/hysteresis, and UI countdown/feedback rendering.

Reproduce the host checks with `python tests/run_tests.py`. Build production with
`idf.py build` using the main configuration. For a fresh alternate configuration:

```sh
idf.py -B build/validation -D SDKCONFIG=build/validation.sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;tests/sdkconfig.validation.defaults" build
```

The executed alternate build used an equivalent defaults file in `build/` with
the same three settings. A tracked copy is supplied under `tests/` for reuse.
Build logs are `build/beacon-build.log` and `build/beacon-validation-build.log`.
The sandbox build used a process-local Git safe-directory override for the
installed SDK, disabled the existing compiler cache, and placed cache temporary
files under `build/ccache-tmp`. No global Git/cache settings were changed.

## Not verified on hardware

**Firmware was not flashed and physical tests were not performed.** The README
contains the full manual checklist. Physical verification is still required for:

- GPIO wiring, LED polarity, disconnected camera, button bounce and response time.
- Potentiometer endpoints, ADC noise and usable selection range on this board.
- Buzzer type/current/driver wiring, audible frequency, real pulse timing and off state.
- OLED controller, resolution/address, orientation, missing-device behavior and bus faults.
- Serial mock command interaction with the attached device.

`QW24120601` has not identified the OLED controller. SSD1306 128x64/0x3C remains
an assumption. The hardware checklist has not been marked as passed based on
compilation or host logic tests. No backend connectivity has been implemented or
tested; event reports and mock invitations are local only.
