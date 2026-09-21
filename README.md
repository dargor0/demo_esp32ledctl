# esp32ledcontrol

An ESP-IDF application for the **ESP32-S3 DevKit** whose primary function is
controlling the on-board addressable RGB LED, both automatically (by connection
state) and from a web interface. WiFi credentials are captured over **BLE
provisioning** and stored in NVS; once connected the device exposes a **REST
API / web UI**, synchronizes time via **NTP**, and is reachable through
**mDNS** as `<system-name>.local`.

## Features

- **BLE provisioning** (`espressif/network_provisioning`) storing WiFi
  credentials in NVS.
- **WiFi station** with automatic reconnection and exponential backoff.
- **RGB LED state machine** (WS2812 over RMT):

  | State        | Color        | Pattern      |
  |--------------|--------------|--------------|
  | Provisioning | Blue         | Slow blink   |
  | Connecting   | Amber        | Fast blink   |
  | Connected    | Green        | Solid        |
  | Error        | Red          | Fast blink   |
  | Custom       | User-defined | User-defined |

- **REST API + embedded web UI** (separate `www/` HTML/CSS/JS files linked into
  the firmware) for status, time, and LED control. The page renders a simulated
  LED per LED, the device date/time, and a refresh-rate slider (500 ms–5 s).
- **Multiple chained WS2812 LEDs** on one data line, addressed by ID (0 =
  on-board), each independently custom-colorable via a per-ID or array API
  (`CONFIG_APP_LED_COUNT`, 1–1024).
- **Thread-safe public APIs**: components with shared mutable state serialize
  access with mutexes (FR-9).
- **NTP** time synchronization with configurable POSIX timezone.
- **mDNS** hostname and `_http._tcp` service advertisement.
- **BOOT button**: short press clears the custom LED state; long press erases
  credentials and returns to provisioning (factory reset). Disabled in static
  credentials mode.
- **Static credentials mode** (`CONFIG_APP_WIFI_STATIC_CREDS`): connect with
  fixed credentials and disable all reconfiguration (no BLE provisioning, no
  `/api/wifi`, no BOOT factory reset).
- Four FreeRTOS tasks pinned to cores: `prov_task`/`net_task` on core 0,
  `led_task`/`button_task` on core 1 (three tasks in static mode).

## Components

| Component      | Responsibility |
|----------------|----------------|
| `led_rgb`      | WS2812/RMT driver + pure LED state machine |
| `button`       | BOOT debounce + short/long press classification |
| `storage`      | NVS credential persistence (pluggable backend) |
| `app_wifi`     | WiFi station lifecycle + reconnect policy |
| `time_sync`    | SNTP + ISO-8601 formatting |
| `mdns_service` | mDNS hostname + `_http._tcp` |
| `provisioning` | BLE provisioning (network_provisioning) |
| `http_server`  | REST API + web UI (cJSON) |
| `main`         | Task wiring and integration |

Each component isolates its logic behind a hardware-independent seam so it can
be unit-tested on the host (Linux target).

## Hardware

- Target: **ESP32-S3** (dual core), 8 MB flash.
- On-board addressable RGB LED (WS2812), data GPIO configurable
  (`CONFIG_APP_RGB_GPIO`, default **48**; 38 on DevKitC-1 v1.0).
- BOOT button on GPIO0.

## Requirements

- ESP-IDF **v5.5.x**
- Managed components (resolved by the Component Manager):
  - `espressif/network_provisioning ^1.2.4`
  - `espressif/mdns ^1.13.0`
- Built-in `json` (cJSON) is used for the REST API.

## Build and flash

```sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyACM0 flash monitor
```

## Configuration

Run `idf.py menuconfig` → **Application Configuration**:

| Option | Default | Description |
|---|---|---|
| `APP_SYSTEM_NAME` | `esp32ledctl` | BLE name, mDNS host, web title |
| `APP_PROV_POP` | `abcd1234` | BLE provisioning proof-of-possession |
| `APP_WIFI_STATIC_CREDS` | `n` | Fixed credentials; disables provisioning & reset |
| `APP_WIFI_SSID` / `APP_WIFI_PASSWORD` | `""` | Static credentials (when enabled) |
| `APP_RGB_GPIO` | `48` | RGB LED data GPIO |
| `APP_LED_COUNT` | `1` | Number of chained WS2812 LEDs (1–1024) |
| `APP_LED_TICK_MS` | `20` | LED render tick (>= `LED_COUNT * 30 us`) |
| `APP_LED_MAX_BLINK_MS` | `60000` | Max custom blink period |
| `APP_BOOT_GPIO` | `0` | BOOT button GPIO |
| `APP_HTTP_PORT` | `80` | HTTP server port |
| `APP_NTP_SERVER` | `pool.ntp.org` | NTP server |
| `APP_TIMEZONE` | `UTC0` | POSIX timezone |
| `APP_BUTTON_*` | | Debounce / long-press timing |
| `APP_TASK_CORE_*` | 0/0/1/1 | Task core pinning |
| `APP_TASK_STACK_*` | 4096/4096/3072/3072 | Task stack sizes (priorities fixed in code) |

## REST API

The LED API is path-based. LED 0 is the on-board LED; chained LEDs ascend in
signal order (`GET /api/led` returns the collection).

| Method | Path | Description |
|---|---|---|
| GET  | `/` | Web UI (`www/index.html`) |
| GET  | `/style.css`, `/app.js` | Web UI assets |
| GET  | `/api/status` | State, WiFi, IP, uptime, heap, `led_count`, `last_button_event` |
| GET  | `/api/time` | Sync state, epoch, ISO-8601 |
| GET  | `/api/led` | `{"count":N,"leds":[{"id","state","r","g","b","blink_ms","on"},...]}` |
| GET  | `/api/led/{id}` | One LED (404 if invalid) |
| PUT  | `/api/led/{id}` | Set one LED: `{"r":0-255,"g":0-255,"b":0-255,"blink_ms":0-..}` (404/400/409) |
| PUT  | `/api/led` | Set **all** (single object) or a **list** (`[{"id":..,...}, ...]`) |
| DELETE | `/api/led/{id}` | Clear one LED (idempotent 200) |
| DELETE | `/api/led` | Clear all; optional body `{"ids":[..]}` clears only those |
| POST | `/api/wifi` | Erase stored credentials and re-enter provisioning (404 in static mode) |
| POST | `/api/reset` | Restart the device |

`POST` rejects a single `PUT /api/led` body that contains an `id` (the collection
path *is* "all"). Set operations return `409 Conflict` when not connected and
change no LED on invalid input (all-or-nothing).

## Testing and linting

Requires `clang-format`, `lizard`, `gcovr`, and the Linux host tools.

```sh
./scripts/check.sh            # lint + host tests + coverage (no flash)
./scripts/check.sh --lint     # clang-format (GNU) + lizard CCN <= 10
./scripts/check.sh --test --coverage
```

Host unit tests run on the ESP-IDF Linux target with gcov coverage, enforced at
**>= 50% per host-compiled source file**. `tests/led_scan/` is a standalone RGB
pin-scan utility used to locate the LED data GPIO.

## License

Copyright (C) 2026 Oscar Diaz

This project is licensed under the **GNU General Public License v3.0** — see
[LICENSE](LICENSE).
