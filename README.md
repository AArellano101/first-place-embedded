# Third Place social-availability beacon

Standalone firmware for the **Freenove ESP32-WROVER-CAM**, target **esp32**, using
ESP-IDF **5.5.2**. The existing SSD1306 text renderer is retained. No Wi-Fi,
Supabase calls, credentials, camera driver, or session persistence is included.

**Disconnect the camera ribbon before powering this circuit.** These assignments
reuse camera pins. Power the development board through USB for the prototype.

## Wiring

Numbers below are GPIO numbers, not physical header positions. All assignments
and behavior constants are centralized in `main/beacon_config.h`.

| Function | GPIO | Wiring/configuration |
| --- | ---: | --- |
| Red LED | 25 | Active-high output; individual 330 ohm resistor |
| Yellow LED | 26 | Active-high output; individual 330 ohm resistor |
| Green LED | 27 | Active-high output; individual 330 ohm resistor |
| Button 1: Accept | 18 | Button to GND; internal pull-up, active low |
| Button 2: Skip | 19 | Button to GND; internal pull-up, active low |
| Button 3: Status | 32 | Button to GND; internal pull-up, active low |
| Button 4: Timer | 33 | Button to GND; internal pull-up, active low |
| Potentiometer wiper | 34 | ADC1 channel 6; 12-bit, 12 dB attenuation |
| Buzzer control | 13 | Passive: 2 kHz LEDC PWM, 50% duty; off is low |
| OLED SDA | 21 | I2C data; pull-up to 3.3 V |
| OLED SCL | 22 | I2C clock; pull-up to 3.3 V |

- Each LED: GPIO -> its own **330 ohm resistor** -> LED anode; cathode -> GND.
- Each button: assigned GPIO -> button -> GND. Internal pull-ups are enabled.
- Potentiometer: outer terminals -> **3.3 V and GND**, wiper -> GPIO34.
  **Never connect the wiper to 5 V.** Reversing the outer terminals reverses the dial.
- All peripherals share ground.
- OLED: SDA -> GPIO21, SCL -> GPIO22. Verify the module's power requirements;
  **I2C pull-ups must go to 3.3 V**, even if its supply input accepts another voltage.
  Most modules include pull-ups; if absent, fit suitable external pull-ups
  (4.7 kohm is a starting point for short prototype wiring). Internal weak pull-ups
  are enabled, but are not a substitute for suitable bus pull-ups.
- GPIO13 is a **control signal**, not a general buzzer power supply. Verify the
  buzzer type, voltage and current requirements before connecting it directly.
  Use a suitable transistor driver if required, with common ground and a defined
  off state (for example, a gate/base pull-down on an active-high driver). Add
  flyback protection if the load is inductive. Do not wire an unknown load directly
  to the ESP32. The firmware assumes a passive buzzer and active-high control;
  an active buzzer can instead use the configuration option below.
- GPIO6-11 and GPIO16-17 remain reserved for memory. GPIO1/3 remain the serial
  programming/logging console. GPIO0/2/5/12/15 are not used by application peripherals.

## Build and run

Open an ESP-IDF 5.5.2 terminal in this directory:

```sh
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p COM9 flash monitor
```

The existing project is already configured for esp32, so `set-target` is only
needed for a fresh checkout or a different previous target. It regenerates the
configuration. Change COM9 to your board's port. Ctrl+] exits the monitor.

Under **Third Place beacon** in `menuconfig`:

| Option | Default | Meaning |
| --- | --- | --- |
| Demo timer | Off | Off: 1-30 minutes in one-minute steps. On: 5-60 seconds in five-second steps. |
| Active buzzer | Off | Off: passive 2 kHz/50% PWM. On: steady active-high output during each beep. |
| Local mock | On | `invite` and `clear_invite` commands through the serial monitor. |
| OLED address | 0x3C | Configurable 7-bit address; only this address is probed. |
| ADC low/high | 100 / 3900 | Initial raw endpoint estimates; calibrate for the actual potentiometer/board. |
| ADC hysteresis | 20 counts | Deadband on each side of a duration boundary. |
| ADC log | Off | Idle raw/filtered samples once a second for endpoint calibration. |

Production defaults are in `sdkconfig.defaults`. A clean, separate demo build is:

```sh
idf.py -B build/demo -D SDKCONFIG=build/demo.sdkconfig -D "SDKCONFIG_DEFAULTS=sdkconfig.defaults;sdkconfig.demo.defaults" build
idf.py -B build/demo -D SDKCONFIG=build/demo.sdkconfig -p COM9 flash monitor
```

Defaults apply to new settings/configurations, not settings already saved in an
existing sdkconfig. Use `menuconfig` to change an existing build. Demo mode is
marked **DEMO** on the OLED and in the boot log. Reboot always starts RED/IDLE.

## Controls

Exactly one LED indicates the current availability at steady brightness:
**RED: Do not disturb**, **YELLOW: Tentative**, **GREEN: Open to talk**.
The dial controls duration, never LED brightness.

- **Status** cycles RED -> YELLOW -> GREEN -> RED.
- **Accept** changes YELLOW to GREEN. If an invitation is pending, it emits a local
  acceptance event containing the ID and clears it. With no pending invitation,
  no acceptance event is fabricated. It never starts the timer.
- **Skip** clears/reports a pending invitation without changing availability or
  timer. It never triggers an alarm. With no invitation, feedback says
  `No pending invite`. An already-running expiration alert is independent of Skip.
- **Timer** from IDLE latches the selected duration and starts. From RUNNING it
  cancels silently and returns to IDLE. From EXPIRED it acknowledges and silences
  any ongoing alert, returning to IDLE. A subsequent press starts another timer.

Each button is debounced for 30 ms and produces one action per press, including
when held. Simultaneous presses are handled in Accept, Skip, Status, Timer order.
If a deadline has already passed when a Timer press is processed, it acknowledges
expiration rather than cancelling the old countdown.

The idle dial is sampled every 20 ms, smoothed with a 1/8 exponential filter, then
quantized with hysteresis. Raw readings are clamped to configurable endpoints.
The firmware does **not** assume linear voltage measurement across 0-3.3 V. To
calibrate, enable ADC logging, keep the timer idle, move the dial to each end,
record settled raw readings, and set low/high accordingly. The high value must
exceed low; hysteresis must be less than half a duration bin. Invalid settings
fail at compile time. Defaults are estimates, not measurements of your hardware.
ADC failures preserve the last selection (the minimum duration at boot).

While RUNNING, the selected duration is locked and remaining time is computed
from an `esp_timer_get_time()` monotonic deadline, rounded up to whole seconds for
MM:SS display. Availability changes do not alter the deadline. On expiration,
state becomes EXPIRED and the buzzer sounds **three 200 ms beeps**, with **150 ms
silent gaps**, once. Output ends low. Alert phases and cancellation are evaluated
in the 10 ms control loop, subject to scheduler and GPIO/serial overhead; the timer
is not implemented as a decrementing delay loop. Availability is preserved.

## OLED assumptions and failure behavior

**SSD1306 128x64 at 0x3C is a configurable assumption, not identified hardware.**
The marking `QW24120601` has not confirmed the controller. Check the vendor's
module documentation. An I2C ACK confirms an address, not the chip type.

The screen shows availability, idle selected duration or remaining MM:SS,
`Time's up.` on expiration, temporary local feedback, and a pending-invite indicator.
It receives snapshots at 5 Hz and redraws only when displayed text changes.

- For address 0x3D or another address, change the OLED address in menuconfig.
- For upside-down text, change both mirror constants in `beacon_config.h`.
- For SH1106/SH1107 or another controller, replace the SSD1306 panel factory and
  controller-specific IO configuration in `oled.c` with a driver for that chip,
  adding its ESP-IDF component dependency if necessary. Changing only the address
  does not make the SSD1306 driver compatible. The eight-row UI targets 128x64;
  another resolution also requires adapting `beacon_ui.c` and `display.c`.
- If the OLED is absent at boot, the display task logs a warning and stops.
  Buttons, LEDs, dial, countdown and buzzer continue. A returned write failure
  disables display updates until reboot. I2C work runs only in the display task,
  so even a stalled bus cannot block the control loop. Reboot after reconnecting.

## Local mock and future app integration

The Expo/React Native/Supabase device protocol, authentication method and endpoints
have not been supplied. **There is no backend integration or backend confirmation.**
No privileged Supabase credentials are embedded. All state is volatile.

With the default local mock enabled, enter these commands in `idf.py monitor`
(press Enter to display the `local-mock>` prompt):

```text
invite demo-001
clear_invite
help
```

`invite demo-001` queues a local invitation. Press Accept and observe an `accept`
log with `invitation_id=demo-001`, then verify the pending indicator clears. Repeat
with `invite demo-002` and Skip. Pressing either button again produces no duplicate
acceptance/skip event. A new ID replaces any currently pending ID. `clear_invite`
clears without emitting acceptance/skip. Queue success means **enqueued locally**,
not accepted by a server or even processed by the control loop yet.

`backend_adapter.h` is the integration boundary:

- `backend_receive_invitation(id)` and `backend_clear_invitation()` are task-safe,
  nonblocking inbound hooks. IDs are 1-64 printable ASCII characters without
  whitespace (empty string clears). They copy data into an eight-entry queue.
  Oversize/invalid IDs fail instead of being truncated; a full queue returns an
  error for the producer to handle. Call only after adapter initialization, not
  from an ISR. The application processes at most eight updates per iteration.
- `backend_report_local()` receives availability, accept, skip, timer start,
  cancellation and expiration events. Accept/skip include the exact invitation
  ID. Timer events include the latched duration. Currently it only logs locally.
  A later transport should copy/enqueue these events and perform networking in
  its own task; never block the control loop waiting for Supabase or a response.
- The control loop exclusively owns `beacon_t`. Future network tasks must use
  the adapter queue, not mutate that state. Authentication, authorization,
  reconnection, acknowledgements, persistence/retries and server reconciliation
  remain future work. Do not interpret `Accepted locally` as a confirmed connection.

## Source layout

| File | Responsibility |
| --- | --- |
| `main/beacon_config.h`, `main/Kconfig.projbuild` | Fixed pins and configurable behavior |
| `main/beacon.c/.h` | Availability, invitations, monotonic timer, alert phases |
| `main/input_logic.c/.h` | Press-edge debounce, ADC smoothing/hysteresis |
| `main/hardware.c/.h` | GPIO, ADC1 and LEDC / active-buzzer output |
| `main/beacon_ui.c/.h` | Pure text formatting |
| `main/display.c/.h` | Isolated OLED task and latest-snapshot queue |
| `main/oled.c/.h` | Retained font/framebuffer and SSD1306 panel driver |
| `main/backend_adapter.c/.h` | Inbound queue, local reporting, serial mock |
| `main/main.c` | Single-owner 10 ms control loop |

The LEDs use break-before-make updates so two LEDs are never driven high together.
There may be a momentary all-off interval during a status change. The displayed
state and LEDs converge on the same state each control iteration.

## Validation

Run the actual platform-independent C logic with a host GCC:

```sh
python tests/run_tests.py
```

Set `CC` to a host GCC executable path if it is not on PATH. Tests build and execute
both production and demo configurations with warnings treated as errors. They
cover boot/reset, state cycling, invitation IDs and clearing/replacement, no
invented/duplicate events, acceptance from YELLOW, skip invariants, duration
latching, cancellation, monotonic deadlines beyond 32-bit uptime, one-shot beep
phases, acknowledgement during alert, button bounce/holds, dial range/stability,
and display text/temporary feedback. They do not simulate electrical hardware.

See `VALIDATION.md` for recorded build/test results and physical checks still needed.

The hardware APIs were checked against the installed ESP-IDF 5.5.2 headers and
examples; reference documentation: [ADC oneshot](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32/api-reference/peripherals/adc_oneshot.html)
and [LEDC](https://docs.espressif.com/projects/esp-idf/en/release-v5.5/esp32/api-reference/peripherals/ledc.html).

## Manual hardware checklist (not yet performed)

Use demo mode for short tests, then check production endpoints as well.

- [ ] Camera disconnected; wiring/current/power checks completed before USB power.
- [ ] Boot: only red LED on, buzzer low/off, timer IDLE; DEMO label matches build.
- [ ] Status: RED -> YELLOW -> GREEN -> RED, one change per press; hold/bounce do not repeat.
- [ ] Accept from YELLOW: GREEN, timer remains unchanged; no invented acceptance event.
- [ ] Inject invitation, Accept/Skip: correct ID reported once and pending flag cleared.
- [ ] Skip with/without invitation: no new buzz, no availability or timer change.
- [ ] Dial: stable full range 1-30 min (or 5-60 s), with no boundary flicker; calibrate endpoints.
- [ ] Start: selected duration locks; rotating the dial does not change active countdown.
- [ ] Cancel: countdown stops immediately after debounce, no alarm, timer IDLE.
- [ ] Expiration: one sequence of three 200 ms beeps with 150 ms gaps; final output low;
      `Time's up.` remains; availability unchanged. Verify timing with a scope if available.
- [ ] Buttons remain responsive during countdown and beeps; Timer silences and acknowledges;
      a separate next press starts again.
- [ ] Availability changes and local invitation updates leave countdown unaffected.
- [ ] Missing OLED at boot: LEDs/buttons/dial/timer/buzzer work; inspect offline log.
- [ ] OLED disconnected or bus fault during operation: controls continue; reset to reconnect.
- [ ] Reboot during a session: RED/IDLE, buzzer off, pending invitation cleared.
