# First Place Wi-Fi table light

ESP-IDF **5.5.2**, target **esp32**, Freenove **ESP32-WROVER-CAM**.
This implements the current backend contract: `off | green | yellow | red`.
It replaces local invitations, buttons, potentiometer selection, timers and
buzzer alerts. The previous standalone implementation is preserved under
`legacy/standalone/` and is not compiled into this firmware.

**The device is intentionally unprovisioned.** No real table UUID, API address,
Wi-Fi credentials or device key has been supplied. It boots with conversation
lamps off. Provision it over USB when those values are available.

## Wiring and light hardware

Disconnect the camera ribbon: these pins overlap the camera interface. Power the
board through USB. All external circuits share GND. Numbers are **GPIO labels**,
not physical header positions.

| Function | Default GPIO | Wiring / behavior |
| --- | ---: | --- |
| Red lamp, top, printed x symbol | 25 | GPIO -> individual 330 ohm resistor -> LED anode; cathode -> GND |
| Yellow lamp, middle, printed ? symbol | 26 | Same wiring; PWM breathing |
| Green lamp, bottom, printed checkmark | 27 | Same wiring; steady |
| Separate no-signal status LED | 23 | GPIO -> 330 ohm resistor -> LED anode; cathode -> GND; use a distinct color/label |
| Optional OLED SDA | 21 | SDA, with I2C pull-up to 3.3 V |
| Optional OLED SCL | 22 | SCL, with I2C pull-up to 3.3 V |
| Previous buzzer control | 13 | Held LOW; buzzer is unused and can be disconnected |

Previous buttons on 18/19/32/33 and the potentiometer on 34 are unused. They can
be disconnected. GPIO6-11 and 16-17 remain reserved for memory; GPIO1/3 remain
programming/logging; application outputs exclude strapping pins 0/2/5/12/15.

This driver supports three **separate low-current LED channels** by default.
Each needs its own resistor. Larger lamps/strips need appropriate power drivers,
a separate suitable supply and common ground; do not power them from GPIOs.

`idf.py menuconfig` -> **First Place table box** configures pin assignments,
brightness (default 50%), polarity and yellow period (default 2000 ms). The
separate status LED is active high; set its GPIO to **-1** if absent. Its blink
must be separate from the three conversation lamps. If neither this LED nor a
working OLED is present, loss still turns the lamps off, but looks like valid
`off` to a person.

An **analog RGB LED** option maps red to the red pin, green to the green pin, and
blue to the formerly-yellow pin. Thus its default channel-to-pin order is
R=25, G=27, B=26. Yellow combines R+G. Set active-low polarity for an appropriate
common-anode/driver arrangement. This is analog PWM, **not WS2812/addressable-strip
support**. Such hardware needs a replacement `box_led.c` driver; the network code
and logical RGB/state interface are independent. Invalid/duplicate/reserved pins
or collisions with an enabled OLED are rejected at startup before outputs are configured.

The OLED remains an **unconfirmed SSD1306 128x64 at 0x3C assumption**; the marking
`QW24120601` has not identified it. Verify its power requirements and ensure I2C
pull-ups go to 3.3 V. Change address/pins or disable it in menuconfig. Other
controllers need the matching panel factory/IO setup in `oled.c`. Missing/faulty
OLED hardware does not block the light task. Reboot after correcting its wiring.

## Build and flash

In an ESP-IDF 5.5.2 terminal at this repository:

```sh
idf.py set-target esp32
idf.py menuconfig
idf.py build
idf.py -p COM9 flash monitor
```

The existing project already targets esp32; `set-target` is only needed for a
fresh setup or different prior target and regenerates configuration. Change COM9
to the board's port. Ctrl+] exits the monitor. `sdkconfig.defaults` contains no
secrets and selects size optimization and the verified HTTPS certificate bundle.
Existing sdkconfig values take precedence over defaults; use menuconfig to change them.

Flash contains no provisioning defaults and never restores a previously displayed
color. Existing NVS provisioning survives normal firmware flashing. No firmware
update service or OTA protocol is implemented.

## Provisioning over USB

Obtain an actual **current database table UUID** and a device key accepted by the
server's `HW_DEVICE_KEYS` allowlist. Seed IDs are not assumed stable. Do not use
fixture IDs in a real deployment. The server currently allowlists keys globally,
not per table: setting a device UUID does not make it a server-enforced binding.

Install the local tools (use a virtual environment if preferred):

```sh
python -m pip install -r tools/requirements.txt
python tools/provision.py provision --port COM9
```

Close serial monitors first so the tool can open the port. It prompts locally for
SSID, Wi-Fi password, base URL, table UUID and device key. Password/key prompts
are hidden. They are not command-line arguments, echoed by firmware, or printed
in logs. Settings are validated, stored as one versioned NVS blob, and the board
restarts. Wi-Fi is 2.4 GHz. No settings are sent anywhere until the configured
Wi-Fi/server is used.

For local development, explicitly permit unencrypted HTTP:

```sh
python tools/provision.py provision --port COM9 --allow-http --base-url http://192.168.1.100:8787
```

Replace that IP with the server computer's LAN address. `localhost`/loopback is
rejected because it would refer to the ESP32. HTTP exposes the device key on the
LAN; use it only for development. **Use HTTPS for deployment.** HTTPS verifies
certificates/hostnames with the ESP-IDF trusted-root bundle; verification is never
skipped. SNTP (`pool.ntp.org`) supplies certificate-validation time. Until time
is available, HTTPS waits with lamps off/no-signal. Freshness always uses the
ESP32 monotonic clock, never SNTP or the server timestamp. For a private CA,
configure an appropriate custom certificate bundle in ESP-IDF; do not disable TLS checks.

Optional file-based provisioning:

1. Copy `provisioning.example.json` to ignored `provisioning.local.json`.
2. Replace every placeholder locally, including the actual database UUID.
3. Run `python tools/provision.py provision --port COM9 --config provisioning.local.json`.
4. Keep the file out of source control and remove it when no longer needed.

```sh
python tools/provision.py info --port COM9
python tools/provision.py erase --port COM9
```

`info` exposes only the stored table identity, not credentials. `erase` requires a
local confirmation and erases only the table-box namespace, then restarts dark.
The prototype's NVS is not encrypted; protect physical access. Production flash
and NVS encryption/security provisioning are separate deployment work. If NVS
initialization fails, the firmware stays dark and logs a storage fault rather
than silently erasing credentials.

The USB wire protocol is newline-terminated `INFO`, `ERASE`, or `PROVISION <json>`.
There is no serial echo/history or credential readback. Use the tool instead of
pasting secrets into a terminal/chat. The maximum command is 1536 bytes. Changing
provisioning restarts the device; no concurrent HTTP-client reconfiguration occurs.

## Exact backend protocol

One networking task owns/reuses a single ESP-IDF HTTP client and performs:

```http
GET <API_BASE_URL>/hw/tables/<TABLE_UUID>/state
X-Device-Key: <DEVICE_KEY>
Accept: application/json
```

No body, user bearer token, Auth0 login, registration, heartbeat or acknowledgement.
The expected response is:

```json
{"light":"yellow","at":"2026-11-08T12:00:00.000Z","pollMs":2000}
```

All four exact lowercase values are supported. The parser requires the current
response schema: recognized string `light`, ISO timestamp-shaped string `at`, and
numeric `pollMs: 2000`. The timestamp is metadata only. Legacy `lit`, `openToChat`,
`welcome`, and `full` payloads are rejected. Duplicate required fields, embedded
NULs, excessive JSON nesting and trailing garbage are also rejected.

Successful polling runs approximately every two seconds. Response fragments,
including decoded HTTP chunks, accumulate into a bounded **2048-byte** buffer.
Only a successful HTTP 200 with complete data and valid payload publishes a new
state. Truncated, oversized, invalid, failed and non-200 responses do not refresh
freshness. Redirects are disabled so the device key stays at the configured
server. Rejected connections are closed; subsequent requests reuse the handle.
The owning task cleans the handle up if setup fails; it owns it for normal device
lifetime. The separate light task never accesses that handle.

The request timeout is 3 seconds. Data/header callbacks also close oversized or
elapsed requests from their owning networking task; they do not free the handle
inside callbacks. DNS/TLS/library stalls cannot block the independent light task.
Transient failures retry with exponential backoff/jitter capped at 30 seconds;
success restores the two-second cadence. Wi-Fi reconnects indefinitely.

## Light and failure behavior

| Input / condition | Conversation lamps | Status / OLED |
| --- | --- | --- |
| Boot, no valid response yet | All off | Waiting for server, or Setup required |
| `off` | All off | Valid backend `off` |
| `green` | Steady green | Backend controlled |
| `yellow` | Slow yellow breath, default 2 s | Backend controlled |
| `red` | Solid red | Backend controlled |
| No valid response for ~10 s | All off | Separate status LED blinks 1 Hz; OLED NO SIGNAL |
| HTTP 400/401/403/404 or redirect | Immediately off | Configuration fault; status LED blinks |
| Next complete valid 200 | Returned state automatically | Fault/no-signal clears |

Identical yellow responses refresh freshness without restarting its animation.
The 50 Hz light task uses a 20 ms conservative margin at the ten-second cutoff.
It does not block on HTTP, display transactions or logs. The OLED has its own
low-priority task, receives snapshots at 5 Hz and only redraws changed text.

A malformed UUID is rejected during provisioning; a server HTTP 400 is still
handled as a configuration fault. A bad/missing device key produces 401. A wrong
route commonly produces 404. Backend/database 500 and network failures are
transient. **A syntactically valid nonexistent table UUID currently returns valid
200/off**, which is indistinguishable from an intentionally dark table.

The firmware does not compute claims, operating hours, incognito, away state,
conversation deadlines or safety actions. The backend decides all colors. In
particular, it does not invent a five-minute-away state or clear red based on a
conversation end time. Phone/server changes must be reflected by subsequent polling.

## Static QR and optional NFC

After provisioning, read back the device's UUID and generate the printed QR:

```sh
python tools/provision.py qr --port COM9 --output artifacts/table-qr.svg
```

The generator uses the **stored device UUID**, not an independent ID or seed.
It creates a printable SVG with a quiet border and a text companion containing:

```text
firstplace://table/<TABLE_UUID>
```

Print that QR on the box and scan-test it. For NFC, write the identical URI into
an NDEF URI record on a compatible passive tag. No NFC reader/UID-upload endpoint
is involved. PN532 tag emulation is not implemented or assumed compatible with
phones. There are no rotating kiosk codes. No real QR is generated yet because
this device has not been assigned a real UUID.

## Tests and source layout

```sh
python tests/run_tests.py
```

This compiles the actual C model, response accumulator/parser and provisioning
validation with host GCC plus cJSON from the installed ESP-IDF. Set `CC` and
`IDF_PATH` if needed. Host checks cover four states, pulse continuity, boot,
freshness during a stalled network task, recovery, configuration faults, bounded
fragment assembly, malformed/unknown/oversized/truncated responses, UUID/URL
validation and 10,000 parse/config cycles without retained parser allocations.
Localhost HTTP fixtures feed their bodies through the same native C parser.
They are not tests of ESP32 Wi-Fi/TLS or the live database/app.

| Source | Responsibility |
| --- | --- |
| `box_settings.h`, `Kconfig.projbuild` | Timing, bounds, board/LED settings |
| `box_config.c`, `box_provision.c` | Validation, identity, USB and NVS |
| `box_model.c` | Four colors, pulse, freshness and retry timing |
| `box_protocol.c` | Bounded accumulation and strict response validation |
| `box_network.c` | Wi-Fi, SNTP, verified HTTP(S), reconnect/poll task |
| `box_led.c` | Replaceable discrete/analog-PWM hardware driver |
| `box_display.c`, `oled.c` | Optional status display, retained text renderer |
| `main.c` | Startup and independent light-safety task |
| `tools/provision.py` | Local provisioning and matching QR/NFC URI |
| `tools/mock_backend.py` | Explicit test fixture, never actual backend logic |

See [TESTING.md](TESTING.md) for reproducible physical/app tests and
[VALIDATION.md](VALIDATION.md) for what actually ran.

The current contract was checked against the adjacent app checkout's
`server/src/routes/hw.ts`, `src/lib/conversations.ts`, `src/components/LightPole.tsx`
and `db/migrations/080_conversations.sql`. Those files were only read, not edited.
HTTP APIs were checked against the installed SDK and the
[ESP-IDF 5.5.2 HTTP client reference](https://docs.espressif.com/projects/esp-idf/en/v5.5.2/esp32/api-reference/protocols/esp_http_client.html).
