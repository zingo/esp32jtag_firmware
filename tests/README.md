# Web API Integration Tests

`tests/test_web_api.py` runs live integration tests against the firmware's HTTPS
web server (`main/network/web_server.c`). It requires a flashed, booted device on
your network — it is not a build-time unit test.

## Prerequisites

- A device flashed with this firmware and connected to your network (Wi-Fi STA or AP mode)
- The `requests` library:

  ```bash
  pip install requests
  ```

  On this repo's build host the ESP-IDF Python env already has it:

  ```bash
  ~/.espressif/python_env/idf5.5_py3.12_env/bin/python -c "import requests; print(requests.__version__)"
  ```

## Find the device IP

The device prints its IP to the serial console during boot, e.g.:

```
I (3168) network-mngr: IP          : 192.168.134.200
```

Or check your router's DHCP client list.

## Run

```bash
python tests/test_web_api.py --host <DEVICE_IP>
```

Example (this setup):

```bash
~/.espressif/python_env/idf5.5_py3.12_env/bin/python tests/test_web_api.py --host 192.168.134.200
```

### Options

| Option | Default | Purpose |
|---|---|---|
| `--host` | *(required)* | Device IP address |
| `--port` | `443` | HTTPS port |
| `--username` | `admin` | HTTP basic auth username |
| `--password` | `admin` | HTTP basic auth password |

Examples:

```bash
python tests/test_web_api.py --host 192.168.134.200
python tests/test_web_api.py --host 192.168.134.200 --port 8443
python tests/test_web_api.py --host 192.168.134.200 --username foo --password bar
```

## What it tests

| Test | Endpoint | Notes |
|---|---|---|
| root | `GET /` | Serves web UI, requires auth |
| root requires auth | `GET /` | Expects 401 |
| version | `GET /api/version` | Firmware + git commit metadata |
| debug_status | `GET /api/debug_status` | Probe/target state |
| ota_status | `GET /api/ota_status` | Current + alternate partition |
| get_credentials | `GET /get_credentials` | Full config key set |
| la_status | `GET /la_status` | Logic analyzer capture flags |
| la_get_settings | `GET /la_get_settings` | LA config + channels |
| uart_debug | `GET /api/uart_debug` | UART state |
| set_credentials (same value) | `POST /set_credentials` | Echoes stored value, expects no reboot |
| set_credentials (invalid) | `POST /set_credentials` | Expects 400 on bad JSON |
| set_credentials (no auth) | `POST /set_credentials` | Expects 401 |
| API requires auth | several `GET`s | Expects 401 without credentials |
| unknown 404 | `GET /no/such/endpoint` | Expects 404 |

## Safety

- The `set_credentials` test **echoes the currently stored value** so the device
  does not reboot or change persistent state mid-suite.
- Destructive endpoints are intentionally **not** tested: factory reset
  (`/reset_to_factory`), OTA partition switch (`/switch_ota_partition`),
  reboot-to-bootloader (`/reboot_bootloader`), and OTA upload (`/ota_upload`).

## Console Tests

`tests/test_console.py` runs live integration tests against the USB Serial
JTAG console (`main/console_menu.c`). It requires a flashed, booted device
connected over USB Serial JTAG and, optionally, network access for the
persistence cross-check.

### Prerequisites

- `pyserial`:

  ```bash
  pip install pyserial
  ```

  On this repo's build host the ESP-IDF Python env already has it.

### Run

```bash
python tests/test_console.py --device /dev/ttyACM0
python tests/test_console.py --device /dev/ttyACM0 --host 192.168.134.200
```

The `--host` flag enables the settings persistence tests, which write a value
through the console and read it back via the web API (`/get_credentials`).
The original values are restored afterwards.

### Options

| Option | Default | Purpose |
|---|---|---|
| `--device` | *(required)* | USB Serial JTAG device (e.g. `/dev/ttyACM0`) |
| `--baud` | `115200` | Serial baud rate |
| `--host` | *(none)* | Device IP for web cross-check (optional) |
| `--username` | `admin` | HTTP basic auth username |
| `--password` | `admin` | HTTP basic auth password |

> **Line endings:** the console expects Enter as CR (`\r`). The tests always
> send `\r` and wait for unique output markers, so interleaved boot logs do not
> affect them.

### What it tests

| Test | Notes |
|---|---|
| prompt | REPL responds at `esp32jtag>` |
| help lists commands | All 26 registered console commands |
| show settings | All setting groups printed, including OTA partition status |
| no-arg commands show usage | 20 settings commands print current value + usage without changing state |
| help `<cmd>` / `<cmd> --help` | Per-command summary (description, usage, group) for all 26 commands; unknown command rejected |
| show includes OTA status | `show` prints Running/Alternate partitions; standalone `ota_status` is removed |
| settings roundtrips | 11 web-visible settings (porta–portd, vio, uart_*, usb_dap) written via console, verified via web API, restored |
| ota_url persistence | Writes via console, verifies via web API, restores original |
| console-only setters | wifi_mode/wifi_ssid/web_user/sreset write + getter + restore |
| factory_reset confirmation | Requires explicit `yes`, plain `factory_reset` only warns |
| reset_target guard | Rejected when Port B is not in SReset mode |
| portd guard | `portd_output`/`portd_freq` run in LA mode; restored to tristate/0 Hz afterwards |
| invalid args | Out-of-range values rejected for all settings commands (incl. `vio 2.0`, `log maybe`) |
| log toggle | `log off` silences ESP_LOG output, `log on` restores it |

### Safety

- Destructive commands are never executed: `reboot`, confirmed `factory_reset`,
  `ota_switch`, and `reset_target` when Port B is in SReset mode (the guard test
  asserts the error message instead).
- The Port D tests restore a neutral state (`portd_output 0 0`, `portd_freq 0`)
  even if they fail.
- The `log toggle` test always re-enables log output (`log on`) before passing,
  so a failure does not leave the device muted.
- All write tests restore the original values afterwards — via the web API where
  the setting is web-visible, otherwise via the console getter. `disableUsbDapCom`
  is preserved during the OTA URL restore because `/set_credentials` writes it
  unconditionally.
- Writes only touch NVS (they need a reboot to take effect), so the running
  firmware is never reconfigured during the run.

## Exit code

Returns `0` when all tests pass, `1` if any fail. Failures are printed to stderr.
