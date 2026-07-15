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

## Exit code

Returns `0` when all tests pass, `1` if any fail. Failures are printed to stderr.
