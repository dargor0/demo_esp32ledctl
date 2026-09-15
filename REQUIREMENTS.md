# Requirements — ESP32-S3 Provisioning & Control Firmware

## 1. Overview

An ESP-IDF based application for the **ESP32-S3 DevKit** whose **primary
function is RGB LED control through a web interface**. The LED reflects the
device's connection state (provisioning, connecting, connected, error) using
distinct colors and blink patterns. Once connected, the web user may override
the LED with any custom color and blink rate (the *custom state*).

Supporting features make the LED controllable and the device usable on a LAN:
WiFi credentials are provisioned over Bluetooth Low Energy (BLE) and persisted
in Non-Volatile Storage (NVS), a local REST API / web UI is served over the
network, time is synchronized via NTP, and the device is reachable by an mDNS
local name. The on-board BOOT button provides quick recovery actions (revert
LED to the connected state, or factory-reset credentials).

- **Target chip:** ESP32-S3
- **Framework:** ESP-IDF v5.5.x
- **Project name:** `esp32ledcontrol`
- **External component (ESP Component Manager):**
  - `espressif/network_provisioning` `^1.2.4` — network provisioning (Wi-Fi/Thread)
    with BLE transport. For IDF < 6.0 it uses IDF's built-in `json`; the managed
    `espressif/cjson` is pulled only on IDF >= 6.0.
- **JSON:** ESP-IDF built-in `json` component (bundled cJSON). No external
  `espressif/cjson` dependency on IDF 5.x (would duplicate/conflict).
- **Provisioning API:** `network_provisioning/*` headers (scheme `BLE`), not the
  legacy `wifi_provisioning/*` API.

## 2. Functional Requirements

### FR-1 — BLE Provisioning
- The device shall start a BLE provisioning service on first boot or when no
  valid WiFi credentials are stored.
- The provisioning transport shall use the `espressif/network_provisioning`
  component (`network_prov_mgr`) with the **BLE** scheme
  (`network_provisioning/scheme_ble.h`).
- The device shall advertise using the **system name** (see FR-8), e.g.
  `PROV_<system_name>`; the BLE device/service name shall derive from it.
- The user shall supply SSID and password (Security 1 / plaintext for the MVP).
- On successful delivery of credentials, the device shall attempt to connect to
  the WiFi network.
- The device shall report provisioning status/errors back to the client
  (`success`, `invalid_credentials`, `network_not_found`, etc.).
- Provisioning service shall stop once credentials are successfully applied.

### FR-2 — Credential Persistence in NVS
- WiFi SSID and password shall be persisted in a dedicated NVS partition/namespace.
- On boot, the device shall read stored credentials and attempt auto-connect
  without requiring re-provisioning.
- The device shall expose a way to reset/erase stored credentials
  (see FR-7) and re-enter provisioning mode.
- Credentials shall never be printed to logs in plaintext.

### FR-3 — REST API / Web Interface
- The device shall run an HTTP server on port 80 (default) after WiFi connects.
- The server shall expose a REST API returning JSON, at minimum:
  - `GET /api/status` — device state, WiFi state, IP, uptime, free heap.
  - `GET /api/time` — current synchronized time (ISO-8601 + epoch, if synced).
  - `GET /api/led` — current RGB LED state: mode/state, color, blink rate.
  - `POST /api/led` — set a **custom** LED state while connected:
    `{ "r":0-255, "g":0-255, "b":0-255, "blink_ms":0-60000 }`
    where `blink_ms = 0` means solid. This transitions the LED into the
    *custom* state.
  - `POST /api/led/clear` — clear the custom override and revert the LED to the
    automatic *connected* state (green fixed).
  - `POST /api/wifi` — update WiFi credentials and reconnect.
  - `POST /api/restart` — restart the device.
  - `POST /api/reset` — erase stored credentials and restart in provisioning mode.
- The server shall serve an embedded web UI (single HTML page) for status
  display and LED control, including color picker and blink-rate input.
- The web UI HTML `<title>` and page heading shall use the **system name**
  (see FR-8).
- All endpoints shall return appropriate HTTP status codes and JSON error bodies.

### FR-4 — NTP Time Synchronization
- The device shall start SNTP after WiFi connectivity is established.
- Default NTP servers shall be configurable (e.g. `pool.ntp.org`).
- The timezone shall be configurable via POSIX TZ string (default UTC).
- The device shall expose sync status and current time through the REST API.
- The device shall handle re-synchronization after temporary network loss.

### FR-5 — mDNS Local Name
- The device shall advertise a hostname over mDNS once connected, derived from
  the **system name**: `<system_name>.local` (default `esp32ledctl.local`).
- The device shall advertise the HTTP service via mDNS (`_http._tcp`).
- The mDNS hostname/service instance name shall derive from `CONFIG_APP_SYSTEM_NAME`.
- mDNS shall start only after the network interface is up with a valid IP.

### FR-6 — RGB LED Control & State Machine (primary function)
- The firmware shall control the on-board addressable RGB LED (WS2812 via RMT)
  on supported ESP32-S3 DevKit boards.
- The RGB GPIO pin shall be configurable (default for common S3 DevKitC boards).
- The LED shall be driven by a **state machine** with the following states and
  default patterns:

  | State             | Color        | Pattern        | Trigger / meaning                          |
  |-------------------|--------------|----------------|--------------------------------------------|
  | **Provisioning**  | Blue         | Slow blink     | No stored credentials / BLE provisioning    |
  | **Connecting**    | Amber        | Fast blink     | Connecting (or reconnecting) to WiFi        |
  | **Connected**     | Green        | Fixed (solid)  | WiFi connected, no custom override          |
  | **Error**         | Red          | Fast blink     | Provisioning/WiFi failure                   |
  | **Custom**        | User-defined | User-defined   | Web user set color + blink while connected  |

- **Blink definitions (defaults, configurable):**
  - Slow blink: ~1 Hz (e.g. 500 ms on / 500 ms off).
  - Fast blink: ~5 Hz (e.g. 100 ms on / 100 ms off).
  - Fixed: continuously on.
- **State precedence:** while not connected, the automatic states
  (provisioning / connecting / error) take priority. The *custom* state can only
  be entered while **connected**, and overrides the fixed green *connected*
  state until cleared.
- **Custom state (web-controlled):**
  - The user may set any RGB color (0–255 per channel) and any blink rate
    (`blink_ms`, 0 = solid, up to a defined max) through the web interface.
  - The custom setting remains active until cleared via the API/web UI, a BOOT
    short press, a disconnect, or a reset.
  - On WiFi disconnect the LED shall leave the custom state and follow the
    automatic state machine again.
- The LED pin and default blink timings shall be configurable.

### FR-7 — BOOT Button
- The firmware shall read the on-board BOOT button (GPIO0) with debouncing.
- **Short press** (while connected, e.g. < 1 s): revert the LED to the automatic
  **connected** state (green fixed), clearing any *custom* override and leaving
  the device ready for a new LED change from the web interface.
- **Long press** (e.g. >= 5 s): factory reset — erase stored WiFi credentials
  from NVS, clear any custom LED state, and revert to the **provisioning**
  state (re-enter BLE provisioning).
- Button events shall be logged and reflected in the REST API status.
- Button handling shall not block other tasks (event-driven or dedicated task).

### FR-8 — System Name (Identity)
- The firmware shall define a single **system name** used consistently across
  all user-facing identities.
- Default value: `esp32ledctl`.
- It shall be configurable via `sdkconfig` / Kconfig option
  `CONFIG_APP_SYSTEM_NAME` (string).
- The system name shall be used for:
  1. **BLE provisioning** advertisement/device name (e.g. `PROV_<system_name>`).
  2. **mDNS** hostname (`<system_name>.local`).
  3. **Web server** HTML `<title>` and page heading.
- If the configured name is empty, the default `esp32ledctl` shall be used.

## 3. Non-Functional Requirements

- **NFR-1:** Modular component structure (separate components for provisioning,
  wifi, http, time, mdns, led, button).
- **NFR-2:** No secrets or credentials written to logs or source.
- **NFR-3:** Graceful handling of WiFi disconnects with automatic reconnect.
- **NFR-4:** Watchdog-safe; no long blocking delays in the main loop.
- **NFR-4b:** Task-to-core pinning shall be explicit: `prov_task` and
  `net_task` on core 0; `led_task` and `button_task` on core 1.
- **NFR-5:** Static memory usage kept reasonable; JSON serialized safely using
  the built-in `json` (cJSON) component with bounded buffers.
- **NFR-6:** Configurable via `menuconfig` (Kconfig) where practical.
- **NFR-7:** Code shall build with `idf.py build` and flash with `idf.py flash`.
- **NFR-8 (Testability):** Business/state logic shall be isolated behind
  interfaces so it can be unit-tested on the host (Linux target) without
  hardware; every component shall ship Unity unit tests.
- **NFR-9 (Coverage):** Host-unit-test line coverage shall be **>= 50% for each
  source file**; the check fails otherwise.
- **NFR-10 (Style):** Code shall conform to **clang-format GNU** style, pass
  **lizard** CCN limits, and carry **Doxygen** comments on public APIs.

## 4. Task Architecture & Component Breakdown

The application shall be structured around **four FreeRTOS tasks**, pinned to
cores as follows (dual-core ESP32-S3):

| Task           | Core | Responsibility                                                        |
|----------------|:----:|-----------------------------------------------------------------------|
| `prov_task`    |  0   | BLE provisioning: start/stop provisioning, capture WiFi credentials, write to NVS, report status |
| `net_task`     |  0   | Network stack: WiFi station connect/reconnect, SNTP time sync, mDNS, HTTP/REST server |
| `led_task`     |  1   | WS2812/RMT driver + LED state machine (automatic states + custom override), blink timing |
| `button_task`  |  1   | GPIO0 BOOT button debounce, short/long press detection, event dispatch |

- **Core 0:** `prov_task` and `net_task` (connectivity / radio-adjacent work).
- **Core 1:** `led_task` and `button_task` (real-time UI/feedback work),
  keeping LED timing responsive and independent of network activity.
- Tasks shall communicate via FreeRTOS primitives (queues, event groups,
  task notifications) and the ESP event loop; no shared state without
  synchronization.
- `led_task` shall run the blink timing loop (e.g. via `vTaskDelay`) and react
  to state-change commands without blocking on network or provisioning.
- `button_task` shall debounce in software and not block other tasks.
- Tasks shall be created with explicit stack sizes and priorities
  (configurable via Kconfig where practical).

### Supporting components (used by the tasks above)

| Component     | Used by            | Responsibility                                    |
|---------------|--------------------|---------------------------------------------------|
| `app_wifi`    | `net_task`         | Station init, connect, reconnect handling          |
| `provisioning`| `prov_task`        | `espressif/network_provisioning` manager + BLE scheme + event handling |
| `storage`     | `prov_task`/`net_task` | NVS init and credential read/write/erase helpers |
| `http_server` | `net_task`         | REST API + embedded web UI                         |
| `time_sync`   | `net_task`         | SNTP init and time queries                         |
| `mdns_service`| `net_task`         | mDNS hostname + HTTP service advertisement         |
| `led_rgb`     | `led_task`         | WS2812/RMT driver + LED state machine (auto + custom) |
| `button`      | `button_task`      | GPIO0 debounce, short/long press event dispatch    |

### External & built-in components

**External (declared in `main/idf_component.yml`, resolved by the Component Manager):**

| Component                        | Version   | Purpose                                   |
|----------------------------------|-----------|-------------------------------------------|
| `espressif/network_provisioning` | `^1.2.4`  | Wi-Fi/Thread provisioning over BLE/SoftAP |
| `espressif/mdns`                 | `^1.13.0` | mDNS (not bundled with ESP-IDF 5.5)       |

- Transitive dependencies pulled automatically: `espressif/protocomm`,
  `espressif/protobuf-c`, `espressif/bt`, `espressif/esp_timer`,
  `espressif/esp_wifi`, `espressif/openthread`, `espressif/lwip`.
- On **IDF < 6.0** `network_provisioning` uses IDF's built-in `json`; the managed
  `espressif/cjson` is only added for **IDF >= 6.0**. Therefore no explicit
  `espressif/cjson` is declared for this project.
- BLE transport requires `CONFIG_BT_ENABLED` with Bluedroid or Nimble, and
  `CONFIG_BT_BLE_42_FEATURES_SUPPORTED` for protocomm's legacy advertising API.
- The BLE + WiFi + provisioning image exceeds the default 1 MB app slot, so the
  project uses an 8 MB flash config and a custom partition table
  (`partitions.csv`, 3 MB `factory` partition).

**Built into ESP-IDF (no Component Manager entry needed):**

| Component                         | Purpose                                 |
|-----------------------------------|-----------------------------------------|
| `json` (cJSON)                    | REST API JSON serialization/parsing     |
| `nvs_flash`                       | Credential persistence                  |
| `esp_wifi` / `esp_netif` / `lwip` | WiFi station stack                      |
| `esp_http_server`                 | REST API + embedded web UI              |
| `esp_netif` / `lwip` SNTP         | NTP time sync                           |
| `esp_driver_rmt`                  | WS2812 addressable RGB LED              |
| `esp_driver_gpio`                 | BOOT button input                       |
| `esp_event` / `esp_timer`         | Event loop and timing                   |

## 5. Configuration (Kconfig) — Proposed Options

- `CONFIG_APP_SYSTEM_NAME` (string, default `esp32ledctl`) — used for BLE name,
  mDNS hostname and web UI title
- `CONFIG_APP_WIFI_SSID` / password (optional build-time defaults)
- `CONFIG_APP_NTP_SERVER` (default `pool.ntp.org`)
- `CONFIG_APP_TIMEZONE` (default `UTC0`)
- `CONFIG_APP_RGB_GPIO` (default per DevKit board)
- `CONFIG_APP_LED_SLOW_BLINK_MS` (default `500`)
- `CONFIG_APP_LED_FAST_BLINK_MS` (default `100`)
- `CONFIG_APP_LED_MAX_BLINK_MS` (default `60000`)
- `CONFIG_APP_BOOT_GPIO` (default `0`)
- `CONFIG_APP_BUTTON_SHORTPRESS_MS` (default `1000`)
- `CONFIG_APP_BUTTON_LONGPRESS_MS` (default `5000`)
- `CONFIG_APP_TASK_CORE_PROV` (default `0`)
- `CONFIG_APP_TASK_CORE_NET` (default `0`)
- `CONFIG_APP_TASK_CORE_LED` (default `1`)
- `CONFIG_APP_TASK_CORE_BUTTON` (default `1`)
- `CONFIG_APP_TASK_STACK_*` and `CONFIG_APP_TASK_PRIO_*` per task

## 6. Testing, Linting & Code Quality

### 6.1 Unit testing
- Every component (`led_rgb`, `button`, `storage`, `http_server`, `time_sync`,
  `mdns_service`, `app_wifi`, `provisioning`) shall ship **Unity** unit tests.
- Tests shall run on the **host (Linux target)** for pure logic, from a test app
  in the component's `test/` directory, driven by `pytest-embedded`
  (`idf.py --preview set-target linux` + `pytest`). Hardware-only paths may use
  target tests under `test_apps/`.
- Hardware-dependent logic (RMT/GPIO/I2C) shall be isolated behind an
  abstraction so the pure logic is host-testable, including:
  - LED state machine and blink timing math.
  - Button debounce and short/long-press event mapping.
  - REST JSON build/parse handlers.
  - NVS credential (de)serialization helpers.
  - System-name derivation and config validation.
- Tests shall be deterministic and require no network, BLE, or board hardware
  for the host suite.

### 6.2 Code coverage
- Coverage shall be collected with **gcov** (aggregated by **gcovr**) from a
  host (Linux target) build compiled with GCC coverage flags
  (`--coverage`, i.e. `-fprofile-arcs -ftest-coverage`), injected via
  `CMAKE_C_FLAGS`/`CMAKE_EXE_LINKER_FLAGS` or the script wrapper.
- **Minimum line coverage: 50% per source file** (`*.c` under each component
  `src/` and `main/`).
- The check shall **fail (non-zero exit)** if any single file is below 50%.
- Reports shall be written to `build/coverage/` in machine-readable form
  (gcovr JSON/CSV) and optionally HTML.

### 6.3 Linting & static analysis
- **clang-format (GNU):** a committed `.clang-format` with `BasedOnStyle: GNU`
  is authoritative. Check mode shall be `clang-format --dry-run --Werror` over
  all `*.c`/`*.h`; any formatting difference fails.
- **lizard (CCN):** `lizard -C 10` over `components/` and `main/`; any function
  with **cyclomatic complexity > 10** fails the check.
- **Doxygen comments:** all public headers/functions shall document `@brief`,
  `@param` and `@return` where applicable, with a committed `Doxyfile`; when
  `doxygen` is present it shall run with `WARN_AS_ERROR=YES`.

### 6.4 Check script (testing + linting only)
- A single script `scripts/check.sh` shall run **only** testing and linting —
  it shall never flash, erase, or open a monitor.
- It shall exit **non-zero** if any stage fails, printing a summary table.
- Selectable stages (default: all), e.g. `--lint`, `--test`, `--coverage`,
  `--all`:

  | # | Stage                         | Command (indicative)                                             |
  |---|-------------------------------|------------------------------------------------------------------|
  | 1 | clang-format check            | `clang-format --dry-run --Werror $(git ls-files '*.c' '*.h')`    |
  | 2 | lizard CCN check              | `lizard -C 10 -w components main`                                |
  | 3 | Doxygen comment/doc check     | `doxygen Doxyfile` (if available)                                |
  | 4 | Host unit tests + coverage    | Linux target build with `--coverage` flags, then run `pytest`     |
  | 5 | Per-file coverage threshold   | `gcovr --json` -> fail any file `< 50%`                          |
  | 6 | Summary                       | print pass/fail per stage; exit status                            |

- Required tooling: Espressif `clang-format` (19.x), `lizard` (1.23+),
  `gcov`/`gcovr` (7.x), Python `pytest-embedded`; `doxygen` optional.

## 7. Out of Scope (MVP)

- SoftAP fallback provisioning.
- OTA firmware update.
- HTTPS / TLS for the local API.
- Multi-user auth for the REST API.

## 8. Acceptance Criteria

1. Fresh device advertises over BLE and accepts credentials.
2. Credentials persist across reboot; device auto-connects.
3. LED shows **slow blue blink** while provisioning.
4. LED shows **amber fast blink** while connecting to WiFi.
5. LED shows **green fixed** once connected (no override).
6. LED shows **red fast blink** on error.
7. While connected, the web user can set any color and blink rate, and the LED
   follows the **custom** state.
8. HTTP REST API responds with valid JSON on all documented endpoints.
9. Time is synchronized from NTP and returned correctly by `/api/time`.
10. `esp32ledctl.local` resolves on the LAN and serves the web UI/API.
11. The configured system name (`CONFIG_APP_SYSTEM_NAME`, default `esp32ledctl`)
    appears in the BLE advertisement, the mDNS hostname, and the web page title.
12. BOOT short press clears the custom state and returns to green fixed.
13. BOOT long press erases credentials and returns the device to provisioning
    (slow blue blink).
14. The four tasks exist and are pinned to the specified cores: `prov_task` and
    `net_task` on core 0; `led_task` and `button_task` on core 1.
15. LED blink timing remains responsive while network/provisioning work is
    active (independent core).
16. Every component has host unit tests that run via the Linux target.
17. Per-file line coverage is **>= 50%** for all covered `*.c` files.
18. `scripts/check.sh` runs only tests + linting and exits non-zero on failure.
19. `clang-format --dry-run --Werror` (GNU) reports no differences.
20. `lizard -C 10` reports no function above CCN 10.
21. Public APIs carry Doxygen comments.

## 9. Implementation Workflow (Live Demo)

- This project is a **live demonstration of coding with AI assistance**; the
  implementation shall proceed in controlled, reviewable steps.
- The implementation shall be done **component by component** in dependency
  order (e.g. `storage` → `led_rgb` → `button` → `app_wifi`/`time_sync`/
  `mdns_service` → `provisioning` → `http_server` → `main` integration).
- At the end of **each component**, the assistant shall **stop**, explain what
  the component does (design, API, files, tests) **on screen only**, and
  **wait for an explicit `continue` instruction** before moving on.
- Explanations shall **not** be written into project files; they are shown in
  the session only.
- No component shall be started until the previous one has been acknowledged
  with `continue`.
