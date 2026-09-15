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

- **REST API + embedded web UI** for status, time, and LED control.
- **NTP** time synchronization with configurable POSIX timezone.
- **mDNS** hostname and `_http._tcp` service advertisement.
- **BOOT button**: short press clears the custom LED state; long press erases
  credentials and returns to provisioning (factory reset).
- Four FreeRTOS tasks pinned to cores: `prov_task`/`net_task` on core 0,
  `led_task`/`button_task` on core 1.

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
| `APP_RGB_GPIO` | `48` | RGB LED data GPIO |
| `APP_BOOT_GPIO` | `0` | BOOT button GPIO |
| `APP_HTTP_PORT` | `80` | HTTP server port |
| `APP_NTP_SERVER` | `pool.ntp.org` | NTP server |
| `APP_TIMEZONE` | `UTC0` | POSIX timezone |
| `APP_BUTTON_*` | | Debounce / long-press timing |
| `APP_TASK_CORE_*` | 0/0/1/1 | Task core pinning |

## REST API

| Method | Path | Description |
|---|---|---|
| GET  | `/` | Web UI (HTML) |
| GET  | `/api/status` | System / WiFi / IP / uptime / heap |
| GET  | `/api/time` | Sync state, epoch, ISO-8601 |
| GET  | `/api/led` | Current LED state, color, blink, on |
| POST | `/api/led` | Set custom LED: `{"r":0-255,"g":0-255,"b":0-255,"blink_ms":0-60000}` |
| POST | `/api/led/clear` | Clear custom state (back to green) |
| POST | `/api/restart` | Restart the device |
| POST | `/api/reset` | Erase credentials and restart |

## Testing and linting

Requires `clang-format`, `lizard`, `gcovr`, and the Linux host tools.

```sh
./scripts/check.sh            # lint + host tests + coverage (no flash)
./scripts/check.sh --lint     # clang-format (GNU) + lizard CCN <= 10
./scripts/check.sh --test --coverage
```

Host unit tests run on the ESP-IDF Linux target with gcov coverage, enforced at
**>= 50% per source file**. `tests/led_scan/` is a standalone RGB pin-scan
utility used to locate the LED data GPIO.

## License

Copyright (C) 2026 Oscar Diaz

This project is licensed under the **GNU General Public License v3.0** — see
[LICENSE](LICENSE).
