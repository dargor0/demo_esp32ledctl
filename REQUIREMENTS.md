# Requirements — ESP32-S3 LED Controller Firmware

## 1. Overview

An ESP-IDF based application for the **ESP32-S3 DevKit** whose **primary
function is RGB LED control through a web interface**. The LED reflects the
device's connection state (provisioning, connecting, connected, error) using
distinct colors and blink patterns. Once connected, the web user may override
the LED with any custom color and blink rate (the *custom state*). The driver
also supports **multiple addressable LEDs chained on one data line**, each
individually addressable and controllable (see FR-10).

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
  - `espressif/mdns` `^1.13.0` — mDNS (not bundled with ESP-IDF 5.5).
- **JSON:** ESP-IDF built-in `json` component (bundled cJSON). No external
  `espressif/cjson` dependency on IDF 5.x (would duplicate/conflict).
- **Provisioning API:** `network_provisioning/*` headers (scheme `BLE`), not the
  legacy `wifi_provisioning/*` API.

## 2. Functional Requirements

### FR-1 — BLE Provisioning
- The device shall start a BLE provisioning service on first boot or when no
  valid WiFi credentials are stored. (Disabled in static credentials mode,
  FR-11.)
- The provisioning transport shall use the `espressif/network_provisioning`
  component (`network_prov_mgr`) with the **BLE** scheme
  (`network_provisioning/scheme_ble.h`).
- The device shall advertise using the **system name** (see FR-8), e.g.
  `PROV_<system_name>`; the BLE device/service name shall derive from it.
- The user shall supply SSID and password. The MVP uses
  `NETWORK_PROV_SECURITY_1` (secure handshake) with a Proof-of-Possession
  supplied via Kconfig `CONFIG_APP_PROV_POP` (string, default `abcd1234`).
- On successful delivery of credentials, the device shall attempt to connect to
  the WiFi network.
- The device shall report provisioning status/errors back to the client
  (`success`, `invalid_credentials`, `network_not_found`, etc.).
- Provisioning service shall stop once credentials are successfully applied.
- Changing the WiFi network or password shall be performed **only** through BLE
  provisioning (or supplied at build time in static mode, FR-11); there is no
  API that writes new credentials.

### FR-2 — Credential Persistence in NVS
- WiFi SSID and password shall be persisted in a dedicated NVS partition/namespace
  (not used in static credentials mode, FR-11).
- On boot, the device shall read stored credentials and attempt auto-connect
  without requiring re-provisioning.
- The device shall expose ways to reset/erase stored credentials and re-enter
  provisioning mode: the `POST /api/wifi` endpoint and the BOOT long press
  (FR-7). (In static credentials mode both are disabled — see FR-11.)
- Credentials shall never be printed to logs in plaintext.

### FR-3 — REST API / Web Interface
- The device shall run an HTTP server on port 80 (default) after WiFi connects.
- The server shall expose a REST API returning JSON, at minimum:
  - `GET /api/status` — device state, WiFi state, IP, uptime, free heap,
    `led_count`, and `last_button_event`.
  - `GET /api/time` — current synchronized time (ISO-8601 + epoch, if synced).
  - `GET /api/led` — all LEDs:
    `{"count":N,"leds":[{"id":K,"state":"...","r":..,"g":..,"b":..,
    "blink_ms":..,"on":true}, ...]}`.
  - `GET /api/led/{id}` — a single LED object; `404 Not Found` if `{id}` is
    non-numeric, negative, or `>= LED_COUNT`.
  - `PUT /api/led/{id}` — set LED `{id}` to a **custom** state; body
    `{"r":0-255,"g":0-255,"b":0-255,"blink_ms":0..CONFIG_APP_LED_MAX_BLINK_MS}`
    (`0` = solid).
    `404` if `{id}` is invalid, `400 Bad Request` on an invalid body, and
    `409 Conflict` if the device is not connected.
  - `PUT /api/led` — set custom for **all** LEDs when the body is a single
    command object **without** `id`, or for a specific set when the body is an
    **array** `[{"id":K,...}, ...]`. Array ids are validated all-or-nothing: any
    invalid/out-of-range id → `400` and **no** LED changes; `409` if not
    connected. A single object that contains `id` on the collection path is
    rejected with `400`.
  - `DELETE /api/led/{id}` — clear LED `{id}` (idempotent `200 OK`); `404` if
    `{id}` is invalid.
  - `DELETE /api/led` — clear **all** LEDs (idempotent `200 OK`); an optional
    body `{"ids":[K,...]}` clears only those LEDs.
  - `POST /api/wifi` — **reset** the stored WiFi credentials: erase the SSID and
    password from NVS and return the device to BLE provisioning. It does **not**
    accept new credentials (network/password changes happen only through BLE
    provisioning). Returns `200 OK`.
  - `POST /api/reset` — restart the device.
- The server shall serve a web UI built from **separate source files** (HTML5,
  CSS and vanilla JavaScript) kept in a `www/` directory in the repository. The
  files shall be **embedded into the firmware at build time** (ESP-IDF
  `EMBED_FILES`/`EMBED_TXTFILES`) and served from those embedded blobs — they
  shall **not** be hardcoded as C string literals and shall **not** require a
  filesystem (no SPIFFS/LittleFS).
  - `GET /`          → `www/index.html`
  - `GET /style.css` → `www/style.css`
  - `GET /app.js`    → `www/app.js`
  - Unknown paths shall return `404`.
- The HTML `<title>` and page heading shall use the **system name** (see FR-8).
- The page shall contain the following sections:
  1. **LED status panel** — a textual description of the current LED state
     (e.g. "custom, red, 500 ms") and a **simulated LED**: a colored element
     that mirrors the real LED by showing the current color and blinking with
     the current `blink_ms` (steady when `blink_ms = 0`, dark during the
     off-phase). When `CONFIG_APP_LED_COUNT > 1`, one simulated LED shall be
     rendered **per ID** mirroring each real LED in the chain.
  2. **Device time panel** — the ESP's current date and time (from NTP) with a
     synchronization indicator; shows "not synchronized" before the clock is
     set.
  3. **LED control** — an `<input type="color">` and/or per-channel RGB inputs
     plus a blink-period control; an **LED target selector** (a specific ID or
     "all"); an **Apply** action issuing `PUT /api/led/{id}` (or `PUT /api/led`
     for all), and a **Clear** action issuing `DELETE /api/led/{id}` (or
     `DELETE /api/led` for all). The blink-period control shall accept `0`
     (solid) up to `CONFIG_APP_LED_MAX_BLINK_MS` — the **same cap** as the REST
     API/validator (a slider for the common range plus a numeric field for the
     full range is acceptable).
  4. **Refresh-rate control** — an `<input type="range">` slider from
     **500 ms (min)** to **5000 ms (max)**, default **500 ms**. Moving it shall
     change the polling interval immediately (and may be persisted in
     `localStorage`).
- The JavaScript (vanilla, no frameworks or build step) shall poll
  `GET /api/status`, `GET /api/led` and `GET /api/time` at the selected interval
  (default **500 ms**) and update all panels in place. Network/parse failures
  shall update a connection indicator without throwing.
- The simulated LED shall animate at the reported blink period (CSS animation or
  JS toggling) and be off during the blink off-phase.
- The page shall work offline (no external CDN/asset dependencies) and be
  usable on desktop and mobile browsers.
- All **API** endpoints (`/api/*`) shall return appropriate HTTP status codes
  and JSON error bodies. Static assets (`/`, `/style.css`, `/app.js`) shall be
  served with their correct content types; unknown paths return `404`.

### FR-4 — NTP Time Synchronization
- The device shall start SNTP after WiFi connectivity is established.
- The default NTP server shall be configurable (default `pool.ntp.org`).
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
- The RGB GPIO pin shall be configurable (default `48`, ESP32-S3-DevKitC-1 v1.1;
  `38` on v1.0 boards).
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
    (`blink_ms`, 0 = solid, up to `CONFIG_APP_LED_MAX_BLINK_MS`) through the web
    interface.
  - The custom setting remains active until cleared via the API/web UI, a BOOT
    short press, a disconnect, or a factory reset (FR-7).
  - On WiFi disconnect **all** LEDs shall leave the custom state and follow the
    automatic state machine again.
- The LED pin and default blink timings shall be configurable.
- The LED may be one of several **chained** addressable LEDs on the same data
  line; multiple-LED support (per-LED IDs, compile-time count, array API) is
  specified in **FR-10**. The on-board LED is **ID 0**.

### FR-7 — BOOT Button
- The firmware shall read the on-board BOOT button (GPIO0) with debouncing.
- **Short press** (while connected; any debounced press released before the
  long-press threshold): revert **all** LEDs to the automatic **connected** state
  (green fixed), clearing any *custom* override via `led_rgb_clear_custom_all()`,
  leaving the device ready for a new LED change from the web interface.
- **Long press** (e.g. >= 5 s): factory reset — erase stored WiFi credentials
  from NVS, clear the custom state on **all** LEDs, and revert to the
  **provisioning** state (re-enter BLE provisioning). This action is disabled
  (no-op) in static credentials mode (FR-11).
- Button events shall be logged and exposed through `/api/status` as
  `last_button_event` (`none`, `pressed`, `short_press`, `long_press`).
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

### FR-9 — Thread Safety & Concurrency
- Every component that exposes **mutable shared state** through public functions
  shall serialize access with a synchronization primitive (FreeRTOS
  `SemaphoreHandle_t`, typically a mutex created at init). Public functions that
  other components or tasks may call **asynchronously** shall be re-entrant-safe
  by taking this lock for the whole operation. This applies to **all** public
  mutating functions without exception, including convenience/broadcast variants
  such as `*_all`, not only the primitive per-item functions.
- In particular, any **read-modify-write** sequence shall be atomic with respect
  to other callers. This applies to state gates such as "custom is only allowed
  while connected": the check of the current state and the update of the LED
  state/color/blink shall happen under the same lock.
- Locks shall **not** be held across blocking calls (e.g. `esp_wifi_connect()`,
  `nvs_commit()`, `rmt_tx_wait_all_done()`, `network_prov_mgr_*`) or while
  invoking callbacks, to avoid deadlock and priority inversion; the state is
  updated under the lock and the blocking action is performed after releasing.
- **No nested cross-component locks.** When a component calls another
  component's public API it shall not already hold its own lock (or a documented
  strict lock order shall be defined and respected).
- A recursive mutex shall be used where a public call can re-enter (e.g. a
  callback invoked during an operation).
- For host unit tests the locking layer shall be a thin, swappable shim so the
  pure logic can be tested without FreeRTOS (no-op lock on the Linux target).
- Components whose public API is effectively immutable after start (e.g. the
  web assets) need no lock; the requirement applies only to mutable state.

#### Worked example — LED custom set vs. BOOT short press
`PUT /api/led/{id}` is handled in the HTTP server task and calls
`led_rgb_set_custom(id, ...)`; a BOOT short press runs in `button_task` on the
other core and calls `led_rgb_clear_custom_all()`. Without synchronization the
two read-modify-write sequences can interleave:

1. httpd reads `state == CONNECTED` → gate passes.
2. button sets `state = CONNECTED` (clear).
3. httpd writes `custom_color`/`blink_ms` and sets `state = CUSTOM`.

The final state is `CUSTOM` even though the user pressed Clear (the button
action is lost), or `led_task` may observe `state = CUSTOM` with a partially
written color. Guarding both public operations with the same recursive mutex
makes one complete before the other, so the **last command deterministically
wins** and the rendered frame is always consistent.

#### Per-component concurrency analysis

| Component      | Public entry points called by others | Shared mutable state | Callers (task / core) | Synchronization |
|----------------|--------------------------------------|----------------------|-----------------------|-----------------|
| `led_rgb`      | `set_state`, `set_custom`, `set_custom_many`, `set_custom_all`, `clear_custom`, `clear_custom_many`, `clear_custom_all`, `get_output`, `get_state`, `get_count`, `render`, `set_sink` | global `state`, per-LED `custom_active[]`/`custom_color[]`/`custom_blink_ms[]`, `sink` | `led_task` (1) renders; `net_task`/`prov_task` (0), `button_task` (1) and httpd task call setters | Single recursive mutex; mutators hold it for the whole op (array/broadcast `_all` all-or-nothing); render snapshots then unlocks before the RMT transmit |
| `button`       | `button_start` (init once), `button_get_last_event` | last-event field; driver context; SM is task-local | `button_task` sets; httpd reads `get_last_event` | Single owner for the SM; a small mutex/atomic guards the last-event read/write |
| `storage`      | `save_wifi`, `load_wifi`, `erase_wifi`, `has_wifi` | credential blobs (two-key sequence) + backend | provisioning event task (save); `net_task` (load/has); `button_task`/httpd (erase) | Mutex serializing save/load/erase/has (NVS handles are safe individually, our multi-step ops are not) |
| `app_wifi`     | `start`, `connect`, `get_state` | `state`, policy, callback, `started` | esp_event task (events); `net_task` (start/connect); httpd (`get_state`) | Mutex for policy/state; actions (`esp_wifi_connect`, timer) executed after unlock |
| `time_sync`    | `start`, `wait_synced`, `is_synced`, `now`, `get`, `get_local` | `synced`, `started` | `net_task` (start); SNTP callback task; httpd (reads) | Atomic/critical-section flags; `TZ` set once before use |
| `mdns_service` | `mdns_service_start` (init once) | none after start | `net_task` | No lock; name helpers are pure/re-entrant |
| `provisioning` | `start`, `reset`, `is_provisioned` | state machine, callback, `started` | `prov_task` (start); event task (handler); `button_task`/httpd (reset) | Mutex; not held across `network_prov_mgr_*` calls |
| `http_server`  | `http_server_start` (init once) | system-name/HTML buffers written once at start | handlers run in httpd task(s), possibly concurrent | Assets read-only after start; relies on callee locks for shared state |
| `main`         | — | `EventGroup` | all tasks | `EventGroup` API is thread-safe |

### FR-10 — Multiple LEDs Chained on One Channel
- The RGB driver shall support **chaining multiple WS2812 LEDs on a single RMT
  TX channel / single data GPIO**. LEDs are addressed by a zero-based **ID** in
  chain order: `0 .. LED_COUNT-1`. The **on-board LED is ID 0** (first in the
  chain, nearest the data GPIO); additional external chained LEDs are
  `1 .. LED_COUNT-1` in signal order. `CONFIG_APP_LED_COUNT` includes the
  on-board LED.
- The number of LEDs shall be fixed at **compile time** by the Kconfig option
  `CONFIG_APP_LED_COUNT` (default `1`). It is not configurable at runtime.
- All per-LED memory shall be **statically sized** from `CONFIG_APP_LED_COUNT`
  (no per-render dynamic allocation):
  - the RMT symbol buffer is `LED_COUNT * 24` symbols (`96 bytes` per LED, since
    each WS2812 pixel needs 24 symbols × 4 B). It shall be **static/global, not
    a stack local**, because it can be tens of KB (e.g. 600 LEDs ≈ 56 KB, which
    would overflow a task stack);
  - per-LED state arrays (custom-active flag, color, blink period).
  Memory usage therefore scales linearly with `CONFIG_APP_LED_COUNT` and must be
  documented in `menuconfig` help.
- The **automatic** connection state applies to **all** LEDs. Each LED may
  independently hold a **custom** override (color + blink) while connected, so a
  single chain can show, e.g., LED 0 green (connected) and LED 1 red slow blink.
- `led_rgb_render()` shall encode and transmit **all `LED_COUNT` pixels in one
  frame** on the single channel (GRB order, chain order). Frame duration is
  `LED_COUNT * 30 us`; the render tick (`CONFIG_APP_LED_TICK_MS`) must be at
  least the frame time (e.g. at 20 ms the practical chain limit is ~600 LEDs;
  `N_max ≈ CONFIG_APP_LED_TICK_MS * 1000 / 30`).
- `CONFIG_APP_LED_COUNT` shall be **capped at 1024** in Kconfig, and a
  **compile-time assertion** (`BUILD_ASSERT`) shall fail the build unless
  `LED_COUNT * 30 us <= CONFIG_APP_LED_TICK_MS` — i.e. a large chain requires a
  proportionally larger render tick. (Kconfig cannot express this arithmetic, so
  it is enforced at build time; the `range` provides a memory-sanity cap.)
- The MVP shall use the **non-DMA RMT copy encoder**, so the static symbol buffer
  remains `96 B/LED`. RMT DMA and streaming encoders are out of scope (see §7).
- `render()` shall wait for RMT completion with a fixed **100 ms** timeout
  (`rmt_tx_wait_all_done`); this must exceed the maximum frame time (~31 ms at
  the 1024-LED cap, so it has ~3x margin).
- Interface (supersedes the single-LED signatures; see "Interface impact"):
  - **Per-LED:**
    - `led_rgb_set_custom(id, color, blink_ms)` — only valid while connected;
      `id` must be `< LED_COUNT`.
    - `led_rgb_clear_custom(id)` — clear one LED.
    - `led_rgb_get_output(id, now_ms, out)` — resolve one LED;
      `led_rgb_get_state(id)`.
  - **Many (explicit list):**
    - `led_rgb_set_custom_many(const led_rgb_cmd_t *cmds, size_t count)` — apply
      an array of per-LED commands in one call.
    - `led_rgb_clear_custom_many(const uint16_t *ids, size_t count)` — clear the
      listed LEDs.
  - **All LEDs (broadcast — extra convenience API):**
    - `led_rgb_set_custom_all(color, blink_ms)` — apply the same custom color and
      blink period to **every** LED in one atomic call (equivalent to
      `set_custom_many` with all IDs, without building an array).
    - `led_rgb_clear_custom_all()` — clear custom on **every** LED in one atomic
      call.
  - **Global automatic state:**
    - `led_rgb_set_state(state)` — set the automatic connection state for all
      LEDs; clears any custom overrides.
  - **Whole chain:**
    - `led_rgb_get_count()`; `led_rgb_render(now_ms)` — resolve and transmit the
      whole chain.
- A command carries its target LED:
  ```c
  typedef struct {
      uint16_t id;            /* 0 .. LED_COUNT-1 */
      led_rgb_color_t color;  /* r,g,b 0-255 */
      uint32_t blink_ms;      /* 0 = solid */
  } led_rgb_cmd_t;
  ```
- **Every mutating** public entry point listed above (`set_state`, `set_custom`,
  `set_custom_many`, `set_custom_all`, `clear_custom`, `clear_custom_many`,
  `clear_custom_all`, `set_sink`) shall take the **same single recursive mutex**
  for the whole operation, including the broadcast `_all` convenience APIs; no
  mutating function is exempt (FR-9). Read-only entry points (`get_output`,
  `get_state`, `get_count`) take the lock only long enough to read a consistent
  snapshot. `render()` shall take the lock **only to snapshot** the per-LED
  arrays and then **release it before the blocking RMT transmit**, since FR-9
  forbids holding a lock across `rmt_tx_wait_all_done()`.
- **Multi-LED operations** — both the explicit `_many` arrays **and** the `_all`
  broadcasts — shall be **atomic with respect to other callers** and
  **all-or-nothing**: inputs are validated first; if any `id` is out of range or
  any field is invalid, the call returns `ESP_ERR_INVALID_ARG` and **no** LED
  changes. A concurrent single-LED call cannot interleave into the middle of a
  `_many`/`_all` update.
- Validation order shall be: (1) argument/range validation →
  `ESP_ERR_INVALID_ARG`; (2) connection-state gate → `ESP_ERR_INVALID_STATE`;
  (3) apply. On failure at step 1 or 2, **no** LED changes (all-or-nothing),
  including for `_many`/`_all`.
- **Any** set operation (`set_custom`, `_many`, `_all`) while **not connected**
  shall return `ESP_ERR_INVALID_STATE` and change no LED (the FR-6 gate).
- **Clear operations are idempotent**: `clear_custom`, `_many` and `_all` return
  `ESP_OK` when the addressed LEDs are already not in the custom state.

#### Interface impact (migration)
- This supersedes the single-LED API (`led_rgb_set_custom(color, blink_ms)`,
  `led_rgb_clear_custom()`, `led_rgb_get_output(now, out)`,
  `led_rgb_get_state()`). Existing callers migrate by passing `id` (use `0` for
  a single-LED build or the `_all` variants for global actions).
- The REST schema is **path-based** (see FR-3): `PUT`/`DELETE /api/led/{id}` for
  one LED, `PUT`/`DELETE /api/led` for all (or an array body for a specific
  set), and `GET /api/led` returns the collection. The schema is identical for
  `LED_COUNT = 1` and `> 1`.
- Host unit tests shall cover: per-ID set/clear, `_many` and `_all`
  all-or-nothing validation, out-of-range rejection, whole-chain render
  encoding, and (with the no-op lock shim) that broadcast calls apply
  consistently to every LED.

### FR-11 — Static Credentials Mode
- An optional **compile-time** mode shall allow providing fixed WiFi credentials
  via Kconfig instead of provisioning: `CONFIG_APP_WIFI_STATIC_CREDS` (bool,
  default `n`) with `CONFIG_APP_WIFI_SSID` and `CONFIG_APP_WIFI_PASSWORD`
  (strings).
- When static mode is **enabled**, the device connects directly with the
  configured credentials and **all reconfiguration code shall be disabled**:
  - the **BLE provisioning** component is not compiled/started (no `prov_task`
    work, no BLE advertising);
  - the **`POST /api/wifi`** credential-reset endpoint is not registered and
    returns `404`;
  - the **BOOT long-press** reconfiguration/factory-reset action is disabled (the
    long press becomes a no-op); only the short-press LED action remains;
  - NVS credential storage is not required.
- The build shall **fail** (compile-time assertion) if static mode is enabled
  with an empty `CONFIG_APP_WIFI_SSID`.
- When static mode is **disabled** (default), FR-1/FR-2 apply unchanged (BLE
  provisioning + NVS, resettable via `POST /api/wifi` and BOOT long press).

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
  host-compiled source file**; the check fails otherwise. (ESP-only files not
  built for the Linux target and the static web assets are excluded.)
- **NFR-10 (Style):** Code shall conform to **clang-format GNU** style, pass
  **lizard** CCN limits, and carry **Doxygen** comments on public APIs.
- **NFR-11 (Web assets):** The UI shall be plain HTML5/CSS/vanilla JS with no
  framework or build step, work offline (no CDN), and be small enough that the
  embedded assets add little to the firmware image (each file well under 64 KB).
  Static JS/CSS/HTML files are not subject to the C line-coverage gate.
- **NFR-12 (Polling load):** The default polling interval (500 ms) yields at
  most a few small requests per second; JSON responses are bounded and the
  refresh slider bounds the request rate to no more than 2 req/s per client.
- **NFR-13 (Concurrency):** All public functions touching mutable shared state
  shall be safe to call from different tasks/cores concurrently (see FR-9).
  Locking shall not be held across blocking calls or callbacks, and cross-
  component lock nesting shall be avoided. Host tests shall use a no-op lock
  shim so the pure logic stays testable.
- **NFR-14 (Static LED memory):** LED-related buffers (including the RMT symbol
  buffer) shall be **static/global**, sized from `CONFIG_APP_LED_COUNT` at
  compile time; the render path shall perform **no dynamic allocation** and
  shall not place the symbol buffer on the stack. Adding LEDs increases
  `.bss`/static memory linearly (`96 B/LED` symbol buffer + per-LED state).

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
- Tasks shall be created with explicit stack sizes and priorities. Stack sizes
  shall be Kconfig options (`CONFIG_APP_TASK_STACK_*`); priorities shall be fixed
  in code and documented.
- In **static-credentials mode** (FR-11) `prov_task` shall not be created and the
  BLE provisioning component shall not be linked, leaving three tasks.

### Supporting components (used by the tasks above)

| Component     | Used by            | Responsibility                                    |
|---------------|--------------------|---------------------------------------------------|
| `app_wifi`    | `net_task`         | Station init, connect, reconnect handling          |
| `provisioning`| `prov_task`        | `espressif/network_provisioning` manager + BLE scheme + event handling |
| `storage`     | `prov_task`/`net_task` | NVS init and credential read/write/erase helpers |
| `http_server` | `net_task`         | REST API + web UI (build-embedded assets) |
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
  (`partitions.csv`, 3 MB `factory` partition). No filesystem or extra data
  partition is required; web assets are linked into the application binary.

**Built into ESP-IDF (no Component Manager entry needed):**

| Component                         | Purpose                                 |
|-----------------------------------|-----------------------------------------|
| `json` (cJSON)                    | REST API JSON serialization/parsing     |
| `nvs_flash`                       | Credential persistence                  |
| `esp_wifi` / `esp_netif` / `lwip` | WiFi station stack                      |
| `esp_http_server`                 | REST API + serving build-embedded web assets |
| `esp_netif` / `lwip` SNTP         | NTP time sync                           |
| `esp_driver_rmt`                  | WS2812 addressable RGB LED              |
| `esp_driver_gpio`                 | BOOT button input                       |
| `esp_event` / `esp_timer`         | Event loop and timing                   |

## 5. Configuration (Kconfig) — Proposed Options

- `CONFIG_APP_SYSTEM_NAME` (string, default `esp32ledctl`) — used for BLE name,
  mDNS hostname and web UI title
- `CONFIG_APP_PROV_POP` (string, default `abcd1234`) — Proof-of-Possession for
  BLE provisioning (Security 1)
- `CONFIG_APP_WIFI_STATIC_CREDS` (bool, default `n`) — use fixed credentials and
  disable provisioning/reconfiguration (FR-11)
- `CONFIG_APP_WIFI_SSID` (string, default `""`) — static SSID (required when
  static mode is enabled)
- `CONFIG_APP_WIFI_PASSWORD` (string, default `""`) — static password
- `CONFIG_APP_HTTP_PORT` (int, default `80`) — HTTP server port
- `CONFIG_APP_NTP_SERVER` (string, default `pool.ntp.org`)
- `CONFIG_APP_TIMEZONE` (string, default `UTC0`)
- `CONFIG_APP_RGB_GPIO` (int, default `48`; 38 on DevKitC-1 v1.0)
- `CONFIG_APP_LED_COUNT` (int, default `1`, range `1..1024`) — number of chained
  WS2812 LEDs on the data line. Compile-time; statically sizes the RMT symbol
  buffer (`96 bytes` per LED) and the per-LED state arrays. A build-time
  assertion requires `LED_COUNT * 30 us <= CONFIG_APP_LED_TICK_MS` (see FR-10).
- `CONFIG_APP_LED_TICK_MS` (int, default `20`) — render tick; must be at least
  the frame time `CONFIG_APP_LED_COUNT * 30 us`.
- `CONFIG_APP_LED_SLOW_BLINK_MS` (default `500`)
- `CONFIG_APP_LED_FAST_BLINK_MS` (default `100`)
- `CONFIG_APP_LED_MAX_BLINK_MS` (default `60000`)
- `CONFIG_APP_BOOT_GPIO` (int, default `0`)
- `CONFIG_APP_BUTTON_DEBOUNCE_MS` (default `50`)
- `CONFIG_APP_BUTTON_LONGPRESS_MS` (default `5000`)
- `CONFIG_APP_TASK_CORE_PROV` / `_NET` / `_LED` / `_BUTTON` (defaults `0/0/1/1`)
- `CONFIG_APP_TASK_STACK_PROV` / `_NET` / `_LED` / `_BUTTON` (ints, defaults
  `4096 / 4096 / 3072 / 3072`) — task stack sizes. Task **priorities are fixed in
  code** (documented) and are not configurable.

These Kconfig values are the **single source of truth**: components shall
consume them rather than defining duplicate constants (in particular, one shared
maximum blink value for both the LED state machine and the REST validator).

## 6. Testing, Linting & Code Quality

### 6.1 Unit testing
- Every component (`led_rgb`, `button`, `storage`, `http_server`, `time_sync`,
  `mdns_service`, `app_wifi`, `provisioning`) shall ship **Unity** unit tests.
- Tests shall run on the **host (Linux target)** for pure logic, from a host test
  app in the component's `test_apps/host/` directory (`idf.py --preview
  set-target linux` + `idf.py build`), executed via the **Unity runner**
  (optionally wrapped by `pytest-embedded`). Hardware-only paths may use target
  tests under `test_apps/`.
- Hardware-dependent logic (RMT/GPIO) shall be isolated behind an
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
- **Minimum line coverage: 50% per host-compiled source file** (`*.c` at each
  component root and in `main/`).
- The check shall **fail (non-zero exit)** if any single file is below 50%.
- The check shall emit a machine-readable per-file report (gcovr JSON/CSV); an
  HTML report may additionally be generated.

### 6.3 Linting & static analysis
- **clang-format (GNU):** a committed `.clang-format` with `BasedOnStyle: GNU`
  is authoritative. Check mode shall be `clang-format --dry-run --Werror` over
  all `*.c`/`*.h`; any formatting difference fails.
- **lizard (CCN):** `lizard -C 10` over `components/` and `main/`; any function
  with **cyclomatic complexity > 10** fails the check.
- **Doxygen comments:** all public headers/functions shall document `@brief`,
  `@param` and `@return` where applicable. No `Doxyfile` or doxygen build is
  required, and none runs in CI.

### 6.4 Check script (testing + linting only)
- A single script `scripts/check.sh` shall run **only** testing and linting —
  it shall never flash, erase, or open a monitor.
- It shall exit **non-zero** if any stage fails, printing a summary table.
- Selectable stages (default: all), e.g. `--lint`, `--test`, `--coverage`,
  `--all`:

  | # | Stage                         | Command (indicative)                                             |
  |---|-------------------------------|------------------------------------------------------------------|
  | 1 | clang-format check            | `clang-format --dry-run --Werror <sources, excluding build/>`    |
  | 2 | lizard CCN check              | `lizard -C 10 -w components main`                                |
  | 3 | Host unit tests + coverage    | Linux target build with `--coverage` flags, then run test ELF    |
  | 4 | Per-file coverage threshold   | `gcovr --json` -> fail any file `< 50%`                          |
  | 5 | Summary                       | print pass/fail per stage; exit status                            |

- Required tooling: Espressif `clang-format` (19.x), `lizard` (1.23+),
  `gcov`/`gcovr` (7.x); `pytest-embedded` optional.

## 7. Out of Scope (MVP)

- SoftAP fallback provisioning.
- OTA firmware update.
- HTTPS / TLS for the local API.
- Multi-user auth for the REST API.
- RMT DMA and on-the-fly/streaming WS2812 encoding (non-DMA copy encoder only).

## 8. Acceptance Criteria

1. Fresh device advertises over BLE and accepts credentials.
2. Credentials persist across reboot; device auto-connects.
3. All LEDs show **slow blue blink** while provisioning.
4. All LEDs show **amber fast blink** while connecting to WiFi.
5. All LEDs show **green fixed** once connected (no override).
6. All LEDs show **red fast blink** on error.
7. While connected, the web user can set any color and blink rate, and the
   addressed LED(s) follow the **custom** state.
8. HTTP REST API responds with valid JSON on all documented endpoints.
9. Time is synchronized from NTP and returned correctly by `/api/time`.
10. `esp32ledctl.local` resolves on the LAN and serves the web UI/API.
11. The configured system name (`CONFIG_APP_SYSTEM_NAME`, default `esp32ledctl`)
    appears in the BLE advertisement, the mDNS hostname, and the web page title.
12. BOOT short press clears the custom state on all LEDs and returns them to
    green fixed.
13. BOOT long press erases credentials and returns all LEDs to provisioning
    (slow blue blink).
14. The four tasks exist and are pinned to the specified cores: `prov_task` and
    `net_task` on core 0; `led_task` and `button_task` on core 1.
15. LED blink timing remains responsive while network/provisioning work is
    active (independent core).
16. Every component has host unit tests that run via the Linux target.
17. Per-file line coverage is **>= 50%** for all host-compiled `*.c` files.
18. `scripts/check.sh` runs only tests + linting and exits non-zero on failure.
19. `clang-format --dry-run --Werror` (GNU) reports no differences.
20. `lizard -C 10` reports no function above CCN 10.
21. Public APIs carry Doxygen comments.
22. `GET /`, `/style.css` and `/app.js` are served from assets embedded at build
    time (from project files, not C string literals); unknown paths return 404.
23. The web page shows a textual LED state and a **simulated LED** that mirrors
    the real color and blink period.
24. The web page shows the ESP's current date/time and updates it while polling.
25. The refresh-rate slider ranges **500 ms–5000 ms** (default 500 ms) and
    changes the polling interval immediately.
26. The page polls `/api/status`, `/api/led` and `/api/time` at the selected
    interval without console errors.
27. Concurrent calls to a component's public API (e.g. `led_rgb_set_custom`
    from the HTTP server while `led_rgb_clear_custom_all` runs from
    `button_task`) are serialized: no torn state, the connected-state gate is
    evaluated atomically, and the last command deterministically wins.
28. A stress test issuing rapid `PUT /api/led` requests and BOOT short presses does not
    crash, deadlock, or leave the LED in an inconsistent state.
29. With `CONFIG_APP_LED_COUNT = N`, the firmware drives N chained LEDs in one
    frame and statically allocates `96 bytes × N` for the RMT symbol buffer
    (plus the per-LED state arrays); no runtime allocation per render.
30. `PUT /api/led` with an array of `{id,...}` commands sets several LEDs in one
    request; an out-of-range `id` rejects the whole array (`400`) and changes no
    LED.
31. `GET /api/led` returns `count` + `leds[]`; `GET /api/led/{id}` returns one
    LED or `404`; `DELETE /api/led/{id}` clears one and `DELETE /api/led` clears
    all (an optional `{"ids":[...]}` body clears only those).
32. The same path-based schema is used for `CONFIG_APP_LED_COUNT = 1` and > 1
    (`count = 1`, `/api/led/0` valid).
33. `led_rgb_set_custom_all(color, blink_ms)` / `PUT /api/led` with a single
    command object applies the same custom color and blink to **every** LED, and
    `led_rgb_clear_custom_all()` / `DELETE /api/led` clears every LED — both in
    one atomic call.
34. `CONFIG_APP_LED_COUNT` is limited to `1..1024`, and a configuration where
    `LED_COUNT * 30 us > CONFIG_APP_LED_TICK_MS` fails a compile-time assertion.
35. Set operations while not connected return `ESP_ERR_INVALID_STATE` and change
    no LED; clear operations are idempotent (`ESP_OK` when already not custom) —
    REST maps these to `409 Conflict` and `200 OK` respectively.
36. LED ID 0 is the on-board LED (first in the chain); external chained LEDs are
    IDs `1..LED_COUNT-1` in signal order.
37. `/api/status` reports `led_count` and `last_button_event`, the latter updated
    when the BOOT button produces an event.
38. `POST /api/wifi` erases the stored credentials and returns the device to BLE
    provisioning; there is no endpoint that writes new credentials.
39. With `CONFIG_APP_WIFI_STATIC_CREDS=y`, the device connects with the Kconfig
    SSID/password, no BLE provisioning runs, `POST /api/wifi` returns `404`, the
    BOOT long press is a no-op, and the build fails if the SSID is empty.
40. The web blink-period control is bounded by `CONFIG_APP_LED_MAX_BLINK_MS`,
    matching the REST validator and the state machine (single source of truth).
41. `POST /api/reset` restarts the device; there is no `/api/restart` endpoint.

## 9. Implementation Workflow

- This project is a **live demonstration of coding with AI assistance**.
- Implementation may proceed in one pass or in whatever grouping is convenient;
  there is **no requirement** to stop after each component or to wait for a
  `continue` instruction.
- Explanations shall **not** be written into project files; they are shown in the
  session only.
