# Reproducible verification

These procedures distinguish host tests, a mock server, and real hardware/app tests.
The latter require provisioning and attached hardware; they have not been run yet.

## 1. Automated host checks

```sh
python tests/run_tests.py
python -m pip install -r tools/requirements.txt
python tests/test_tools.py
```

The C tests use actual firmware parser/model/config sources and ESP-IDF's cJSON.
The HTTP test starts a localhost fixture on an ephemeral port, sends real GETs,
and feeds response fragments to the native firmware parser. It checks four colors,
chunking, malformed/oversized/truncated responses, bad keys, invalid UUIDs,
unknown UUID -> 200/off, HTTP failures, redirects, a stalled fixture and recovery.
The freshness model is tested independently with monotonic simulated time, including
an update arriving just after a control task reads its clock. The allocation soak
checks cJSON allocations for 10,000 parse/config cycles; it does not prove ESP-IDF
HTTP/Wi-Fi heap behavior on hardware.

The tool test simulates the USB reply, checks hidden credential prompts/no printed
secrets, and uses the real QR library to create an SVG for an explicitly synthetic
UUID under `build/host-tests/`. It is not a production box label or phone scan test.

## 2. Firmware + mock backend on a trusted development LAN

Keep this fixture separate from the real backend (use port 8788):

```sh
python tools/mock_backend.py --host 0.0.0.0 --port 8788 --scenario off
```

Allow the development machine's firewall to accept the ESP32 on that port. This
fixture has a **public test-only key** `test-device-key`, synthetic UUID
`12345678-1234-5678-9abc-123456789abc`, and an unauthenticated test-control endpoint.
Do not expose it to the internet or use it for real tables/users.

Provision the board with your actual Wi-Fi, the machine's LAN URL on port 8788,
that synthetic UUID and test key. This is an explicit fixture-only test, not a real
database UUID assignment. Example:

```sh
python tools/provision.py provision --port COM9 --allow-http --base-url http://YOUR_LAN_IP:8788 --table-id 12345678-1234-5678-9abc-123456789abc
```

After restarting, open `idf.py -p COM9 monitor`. To change fixture states from
PowerShell on the server computer:

```powershell
Invoke-RestMethod -Uri http://127.0.0.1:8788/__scenario -Method Post -ContentType application/json -Body '{"scenario":"yellow"}'
```

Substitute a scenario from this checklist:

| Test | Expected physical observation |
| --- | --- |
| Reset with server/Wi-Fi absent | Conversation lamps never turn on; no-signal status after ~10 s |
| `off`, `green`, `yellow`, `red` | Dark, steady green, yellow breath, solid red; correct physical lamp position |
| Keep `yellow` for >30 s | Smooth continuous pulse, no phase restart at two-second polls |
| `fragmented`, `chunked` | Valid yellow; fragments do not cause flickering or partial updates |
| `malformed`, `unknown`, `oversized`, `truncated` after green | No successful freshness updates; lamps dark by 10 s from last valid response |
| `unauthorized`, `bad_request`, `not_found`, `redirect` | Immediate configuration fault/dark once response received; redirect destination never contacted |
| `server_error` after green | Retry with backoff; stale green cleared by watchdog |
| `stalled` after yellow | Networking request may wait; independent light task still clears stale yellow |
| Stop server / disable AP | No stale invitation beyond ~10 s, status LED blinks, no reboot needed |
| Restore server/AP, choose `green` | Automatic valid-state recovery after reconnect/backoff (up to 30 s between retries) |
| Change to an unrecognized valid UUID | Fixture and current real contract return 200/off, not a configuration error |
| Wrong device key | 401, lamps dark; key not present in logs |
| Invalid UUID during USB setup | Provisioning rejected; prior complete NVS configuration is retained |
| Disconnect OLED / fault its bus | Light/watchdog task continues independently |
| Press old buttons / turn old dial | No effect on light state, no local timer or buzzer |
| Reset while green | Boots dark; green returns only after a new valid response |

Use a scope or timestamped recording for the stale cutoff and yellow PWM/pulse
timing. No-signal uses its own GPIO23 LED, not a red/yellow/green invitation lamp.

For a real HTTPS staging endpoint, separately test a trusted certificate, invalid
certificate/hostname, unavailable NTP and recovery. Do not turn off certificate
verification. The localhost fixture is HTTP-only and does not cover ESP32 TLS.

### Hardware memory soak

Run valid polling for at least one hour, then alternate outages, malformed bodies,
401 and recovery. Record `polls_ok`, `heap_free`, `heap_min` from logs every 60
successful responses. After initial Wi-Fi/TLS/cJSON warmup, free heap should
stabilize across equivalent workload phases rather than fall each poll. A lifetime
minimum can decline on one larger handshake without proving a leak. Check for
reboots, task stack errors and failed recovery. This has not yet been performed.

## 3. Current app + actual backend/database

Provision again with the **real current table UUID**, its API server and an allowed
device key; do not leave fixture credentials/UUID in the box. Confirm the server's
`HW_DEVICE_KEYS` configuration. Its allowlist is currently global, not table-bound.
The expected mapping below is checked by observing the actual hardware endpoint;
the firmware deliberately does not duplicate these database rules.

| Action/state in app/backend | Expected next device state |
| --- | --- |
| No active claim | off |
| Claim table with default selection | yellow |
| Claimant selects green | green |
| Active conversation | red |
| End conversation (`ended_at` set), claim remains | Claimant's selected color |
| Leave/check out | off |
| Box disabled | off |
| Incognito claimant excluded from active claim | off |
| Outside configured operating hours | off |

Use the backend's testing-hours override if needed; do not change the ESP32's
timezone or add local schedule logic. Do not expect `step_away()` alone to change
the current light calculation, or expect `ends_at` alone to clear conversation red.

## 4. Identity / QR / NFC

Close the monitor and run:

```sh
python tools/provision.py info --port COM9
python tools/provision.py qr --port COM9 --output artifacts/table-qr.svg
```

Compare the read-back UUID, printed QR decode and optional passive tag's NDEF URI:
all must be `firstplace://table/<same UUID>`, matching the polled table. Scan with
the current app and verify the intended table opens. After changing the device's
table assignment, replace the printed label and reprogram the tag. No rotating
QR flow or PN532 tag emulation is part of this implementation.
