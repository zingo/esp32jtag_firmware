# Multi-Function Debug Tool for AI-Driven Embedded System Development

ESP32JTAG Firmware is a powerful all-in-one debug and development platform built on the ESP32-S3. It integrates JTAG/SWD debugging, a 16-channel logic analyzer, FPGA programming, XVC server support, signal generation, and a browser-based configuration interface into a single compact device, accessible over Wi-Fi or USB.

What makes ESP32JTAG especially distinctive is its ability to work seamlessly with AI-driven embedded development workflows. It is designed to pair naturally with [AEL (AI Embedded Lab)](https://github.com/EZ32Inc/ai-embedded-lab), enabling AI-assisted coding, flashing, signal capture, measurement, verification, and iterative debugging on real hardware.

| Feature | Description |
|---|---|
| JTAG / SWD Debugging | Debug, program, and control supported targets |
| 16-Channel Logic Analyzer | Capture and analyze digital waveforms |
| FPGA Programming | Program supported FPGA devices |
| XVC Server | Enable remote JTAG access via Xilinx Virtual Cable |
| Signal Generation | Provide digital stimulus signals for testing and validation |
| Web-Based Interface | Configure and operate the device from a browser |
| Serial Console | Configure everything from the USB Serial JTAG terminal |
| AI-Driven Workflow Support | Integrates with AEL for closed-loop embedded development |


| | |
|---|---|
| **Source code** | <https://github.com/EZ32Inc/esp32jtag_firmware> |
| **Releases** | <https://github.com/EZ32Inc/esp32jtag_firmware/releases> |
| **Recommended dev tool** | [AEL (AI Embedded Lab)](https://github.com/EZ32Inc/ai-embedded-lab) |
| **Build environment** | ESP-IDF v5.5.4 or newer |

---

## Features

- **JTAG / SWD Debugger**
  - [BlackMagic Probe](https://black-magic.org/) (BMP) — native GDB server
  - [CMSIS-DAP](https://arm-software.github.io/CMSIS_5/DAP/html/index.html) over USB ([CherryDAP](https://github.com/cherry-embedded/CherryDAP))
  - [XVC](https://docs.xilinx.com/r/en-US/ug908-vivado-programming-debugging/Virtual-Cable) (Vivado Virtual Cable) for FPGA development

- **16-Channel Logic Analyzer**
  - 264 MHz default sample rate (configurable down to 0.518 MHz)
  - Configurable per-channel triggers (rising, falling, crossing, high, low)
  - 128 KB PSRAM capture buffer
  - Web-based viewer

- **FPGA Support (ICE40UP5K)**
  - Automatic bitstream loading on boot
  - Port multiplexing for all debug interfaces
  - SPI and GPIO configuration modes

- **Network**
  - WiFi AP mode (standalone hotspot) or STA mode (connects to existing network)
  - WiFi provisioning
  - OTA firmware updates over HTTPS
  - OTA partition management — view OTA status, switch between `ota_0` / `ota_1` from the web UI
  - WebSocket bridge for UART traffic

- **Web Interface**
  - HTTPS web server with embedded TLS certificate
  - Basic-auth protected configuration pages
  - Live debug probe status panel (`GET /api/debug_status`)
  - Logic analyzer capture and visualization
  - Debugger target / RTOS / interface selection
  - Port A/B/C/D mode configuration
  - SRESET polarity (active HIGH / active LOW) and pulse width (1–5000 ms) configuration — applied without reboot via `POST /api/sreset_config`
  - Reboot into ROM bootloader mode from the web UI (`POST /reboot_bootloader`)

- **Display (optional LCD)**
  - Main status screen with WiFi / debug connection status
  - Pinout screen showing current per-port wiring (wire colors, pin functions, VIO voltage)
  - Cycle between screens with the SW1 / SW2 buttons

- **Signal Generation (Port D)**
  - Provides signal stimulus to the target system via Port D pins
  - FPGA internal free-running counter output: bits [3:0] or [7:4] at 132 MHz
  - Software square wave: 125 / 250 / 500 / 1000 Hz on all four Port D pins (~50% duty)
  - Useful for clock injection, loopback self-test, and functional excitation of target circuits
  - Applied instantly via web UI — no reboot required

- **UART / USB**
  - USB CDC or WebSocket-based UART bridge
  - Configurable baud rate, data bits, stop bits, parity

- **Serial Console (USB Serial JTAG)**
  - Stateless REPL command line on the same USB port that carries boot logs
  - Flat commands mirroring the web UI — every setting and action is one command, ideal for scripting
  - `help` lists all commands; each setting command takes its value as an argument (no args shows current value)
  - Use `log` to toggle ESP32JTAG Firmware serial log on/off, turning it temporary off makes the consol more usable.

---

## Hardware

| Item | Value |
|---|---|
| MCU | ESP32-S3 |
| Flash | 16 MB |
| PSRAM | Required (logic analyzer buffer) |
| FPGA | Lattice ICE40UP5K |
| Display | Optional LCD (SPI3) |

### Pin Map

| Signal | GPIO |
|---|---|
| UART TX | 43 |
| UART RX | 44 |
| SPI CLK | 38 |
| SPI MOSI | 14 |
| SPI MISO | 39 |
| SPI CS0 (manual) | 21 |
| SPI CS1 | 13 |
| SPI CS2 | 11 |
| SPI CS3 | 5 |
| SWDIO | 41 |
| SWDIO RD/nWR | 45 |
| Button SW1 | 0 |
| Button SW2 | 48 |

---

## Port Modes

The device has four logical ports (A–D). Each port is configured independently via the web UI or NVS storage.

| Port | Available Modes |
|---|---|
| **A** | Logic Analyzer · BMP SWD · BMP JTAG |
| **B** | Logic Analyzer · UART + Reset + Target Voltage |
| **C** | Logic Analyzer · BMP SWD/JTAG · FPGA JTAG config · FPGA SPI config |
| **D** | Logic Analyzer · FPGA XVC · Signal Generation (Counter Lo/Hi @ 132 MHz, GPIO Direct 125/250/500/1000 Hz) |

---

## Getting Started

### Current Release

The current firmware release is **v0.2.1**. It improves Logic Analyzer browser
responsiveness, restores channel trigger indicators after a page refresh,
corrects low-rate waveform timing, and makes SReset safe across device restarts
and available to Black Magic GDB reset commands. See
[`RELEASE_NOTES_v0.2.1.md`](RELEASE_NOTES_v0.2.1.md) for details.

### Prerequisites

- [ESP-IDF v5.5.4 or newer](https://docs.espressif.com/projects/esp-idf/en/stable/esp32s3/get-started/)
- Python 3.8+
- Git
- Recommended: [AEL (AI Embedded Lab)](https://github.com/EZ32Inc/ai-embedded-lab) for automated board bring-up and validation

### Clone

```bash
git clone --recursive https://github.com/EZ32Inc/esp32jtag_firmware.git
cd esp32jtag_firmware
```

### Choose a Board

This project uses a Kconfig board profile. Use separate build directories so each board keeps its own configuration.

For the EZ32 board:

```bash
. $IDF_PATH/export.sh
idf.py -B build_board_esp32jtag menuconfig
idf.py -B build_board_esp32jtag build
```

In `menuconfig`, set:
- `OpenOCD-on-ESP32 Configuration` -> `AEL board profile` -> `ESP32JTAG (ESP32-S3)`

For a generic ESP32-S3 DevKit:

```bash
. $IDF_PATH/export.sh
idf.py -B build_board_esp32s3_devkit menuconfig
idf.py -B build_board_esp32s3_devkit build
```

In `menuconfig`, set:
- `OpenOCD-on-ESP32 Configuration` -> `AEL board profile` -> `Generic ESP32-S3 DevKit`

The board choices come from `main/Kconfig.projbuild`, and the pin mappings and feature flags are defined in `components/platform_include/board_profile.h`.

### Build

```bash
. $IDF_PATH/export.sh
idf.py build
```

After a successful build two versioned release binaries are automatically generated in the `build/` directory:

| File | Purpose |
|---|---|
| `esp32jtag_v<VER>_<DATE>_<GIT>_ota.bin` | OTA wireless update (primary method) |
| `esp32jtag_v<VER>_<DATE>_<GIT>_full.bin` | Full merged flash image (factory / mass programming) |

Example filenames:
```
esp32jtag_v0.2.1_20260719_120000_abcdef0_ota.bin
esp32jtag_v0.2.1_20260719_120000_abcdef0_full.bin
```

The filename encodes version, UTC build timestamp, and git commit so you can always tell which is newer.

### Firmware Update — OTA (primary, no USB cable required)

1. Open `https://<device-ip>/` and select the **Firmware Update** tab.
2. Select the `_ota.bin` file and click **Upload & Update**.
3. The device reboots automatically into the new firmware.

OTA updates use two partitions (`ota_0` / `ota_1`). After an update, you can:
- View the running and alternate partition from the web UI (`/api/ota_status`).
- Roll back to the previous firmware by clicking **Switch to Alternate Partition** (`/switch_ota_partition`) — useful if a new build misbehaves.

### Reboot to Bootloader (reflash mode)

From the web UI you can reboot the device into ROM bootloader mode (`/reboot_bootloader`). This is useful when the firmware is not booting properly, when the serial port is unavailable (CMSIS-DAP USB mode), or when flashing via esptool instead of OTA.

After the reboot, enter download mode by holding **BOOT0** (SW1) while powering up, or run:
```bash
esptool.py --chip esp32s3 -p /dev/ttyUSB0 --before default-reset run
```
then flash with esptool as described below.

### Firmware Update — USB flash (factory / first-time programming)

```bash
# Using the full merged binary (single write from offset 0x0)
esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \
  --before default_reset --after hard_reset \
  write_flash 0x0 esp32jtag_v<VER>_<DATE>_<GIT>_full.bin
```

Or using idf.py during development:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

### Recovery Mode (firmware crash / cannot flash normally)

If the firmware has crashed or is stuck in a boot loop and normal flashing fails, force the device into ROM download mode using the **BOOT0** button (SW1, GPIO0):

1. **Power off** the device (disconnect USB).
2. **Hold** the **BOOT0** button (SW1) and keep it held.
3. **Power on** (reconnect USB) while still holding BOOT0.
4. **Release** BOOT0 after about one second.

The device is now in ROM download mode — the LCD shows no output. Flash using the `_full.bin` file:

```bash
esptool.py --chip esp32s3 -p /dev/ttyUSB0 -b 460800 \
  --before no_reset --after hard_reset \
  write_flash 0x0 esp32jtag_v<VER>_<DATE>_<GIT>_full.bin
```

> Use `_full.bin` only — the `_ota.bin` file cannot be used in recovery mode because the web interface does not start.

---

## First Boot

On first boot the device starts in **WiFi AP mode**.

| Setting | Default |
|---|---|
| SSID | `esp32jtag` |
| Password | `esp32jtag` |
| Web UI | `https://192.168.4.1` |
| Web username | `admin` |
| Web password | `admin` |

> The device uses a self-signed TLS certificate. Accept the browser security warning on first visit.

---

## Web Interface

After connecting to the AP (or the local network in STA mode), open a browser and navigate to the device IP.

| Path | Description |
|---|---|
| `/` | Main dashboard — port configuration, debugger settings, debug probe status |
| `/loganalyzer` | Logic analyzer capture and waveform viewer |
| `/api/debug_status` | Live debug probe connection state |
| `/help` | User guide |
| `/credentials` | Change web UI username and password |
| `/ota_upload` | Upload new firmware |
| `/reset_to_factory` | Erase all NVS settings and reboot |
| `/reboot_bootloader` | Reboot into ROM bootloader (reflash) mode |
| `/switch_ota_partition` | Switch to the alternate OTA partition and reboot |
| `/api/ota_status` | OTA partition status (running + alternate labels) |

### Debugger Configuration

From the main dashboard you can select:

- **Target** — target chip configuration file
- **Interface** — JTAG or SWD
- **RTOS** — FreeRTOS or none (for RTOS-aware stack unwinding)
- **Dual Core** — enable SMP debugging
- **Flash Support** — enable flash programming
- **Debug Level** — verbosity (0–4)

Press **Run** to save and reboot with the new configuration.

### Target Reset (Port B)

When Port B is configured as *Vtarget + UART + SReset*, the **Reset Target** button (and `POST /api/reset_target`) sends a configurable reset pulse on Port B pin 3.

Two settings can be changed on the fly via the **Apply SRESET Settings, Port D Settings and Target IO Voltage** button (no reboot needed):

| Setting | Default | Description |
|---|---|---|
| **SRESET Polarity** | Negative (active LOW) | *Negative*: idle HIGH, pulse LOW. *Positive*: idle LOW, pulse HIGH. |
| **Pulse Width** | 100 ms | Duration of the reset pulse in milliseconds (1–5000). |

The settings are applied immediately via `POST /api/sreset_config` and survive until the device reboots.

---

## Serial Console (USB Serial JTAG)

The firmware provides an ESP-IDF console REPL on the same USB Serial JTAG port
that carries the boot/log output, so a device without network access (or without
the web UI) can still be fully configured from a terminal.

The console is a flat, stateless command set — every setting is one command with
its value as an argument, so it is easy to script and safe to use non-interactively.

Connect a terminal emulator to the USB Serial JTAG device:

```bash
idf.py monitor                      # if the ESP-IDF environment is set up
python -m serial.tools.miniterm /dev/ttyACM0 115200 --eol CR
```

> **Line endings:** the console expects Enter to be sent as CR (`\r`). If input
> appears to be ignored, check that your terminal sends CR (not LF) on Enter.
> Boot and status logs are interleaved with the console output by design; use
> `log off` to mute them.

### Commands

`help` lists every command. Setting commands invoked without a value print the
current value plus usage (they never change state with no arguments).

| Command | Description |
|---|---|
| **Info** | |
| `show` | Print all current settings, including OTA partition status |
| `log [off\|on]` | Mute/restore interleaved log output; no argument toggles |
| **Configuration** | |
| `porta <mode>` | Port A mode: `0` Logic Analyzer, `2` BMP SWD, `3` BMP JTAG |
| `portb <mode>` | Port B mode: `0` Logic Analyzer, `1` Vtarget + UART + SReset |
| `portc <mode>` | Port C mode: `0` LA, `1` BMP SWD/JTAG, `2` FPGA JTAG CFG, `3` FPGA SPI CFG |
| `portd <mode>` | Port D mode: `0` LA, `1` XVC, `2` FPGA JTAG GPIO, `3` FPGA SPI GPIO |
| `portd_output <mode> <value 0-15>` | Set Port D pin drive. Mode: `0` tristate (LA input, default), `1` counter_lo (132 MHz counter bits [3:0]), `2` counter_hi (counter bits [7:4]), `3` gpio (drive pins with `value`). Requires Port D in Logic Analyzer mode |
| `portd_freq <0\|125\|250\|500\|1000>` | Set Port D square-wave frequency in Hz (0 = off) |
| `vio <3.3\|2.5\|1.8\|1.5\|1.2>` | Target IO voltage (`v` suffix optional, e.g. `vio 1.8v`) |
| `uart_baud <300-3000000>` | UART baud rate |
| `uart_dbits <5-8>` | UART data bits |
| `uart_sbits <1\|1.5\|2>` | UART stop bits |
| `uart_parity <n\|e\|o>` | UART parity (none/even/odd) |
| `uart_psel <0\|1>` | UART port select: `0` USB, `1` Web |
| `usb_dap <0\|1>` | `1` disables the USB CMSIS-DAP interface |
| `sreset <polarity> [pulse_ms]` | SRESET polarity (`0` high, `1` low) and optional pulse width in ms |
| `wifi_mode <AP\|SM>` | WiFi mode: AP (access point) or SM (station/managed) |
| `wifi_ssid <name>` | WiFi SSID |
| `wifi_pass <password>` | WiFi password |
| `ota_url <url>` | OTA update URL |
| `web_user <name>` / `web_pass <password>` | Web UI basic-auth credentials |
| **Actions (reset / reboot)** | |
| `reset_target` | Send an SRESET pulse to the target (requires Port B in SReset mode) |
| `ota_switch` | Switch to the alternate OTA partition and reboot |
| `factory_reset yes` | Erase all settings and reboot (requires explicit `yes`) |
| `reboot` | Reboot the device |

Settings that require a reboot to take effect print a `[!] A reboot is required`
notice after the change.

Guards are enforced: `reset_target` and the Port D actions refuse to run unless
the relevant port is in the required mode, and destructive actions
(`factory_reset`, `ota_switch`) require confirmation or warn before proceeding.

---

## Debugging

### BlackMagic Probe (BMP)

BMP exposes a native GDB server directly on the device.

```bash
arm-none-eabi-gdb firmware.elf
(gdb) target extended-remote /dev/ttyACM0
(gdb) monitor swdp_scan
(gdb) attach 1
```

### CMSIS-DAP (USB)

When connected via USB, the device appears as a CMSIS-DAP interface compatible with pyOCD and most IDEs (VS Code Cortex-Debug, Keil, IAR).

When Port C is configured for SWD/JTAG, only one of the two debug paths is active at a time — the BMP GDB server or the USB CMSIS-DAP interface (mutual exclusion). Disable the USB DAP via the **Disable USB DAP** option on the dashboard when you want to use the built-in BMP GDB server instead.

### XVC (Vivado Virtual Cable)

For FPGA development, configure Port D to **XVC** mode. Then connect Vivado to `<device-ip>:2542`.

---

## Logic Analyzer

1. Open `https://<device-ip>/loganalyzer`
2. Configure sample rate and channel triggers
3. Click **Start Capture** (triggered) or **Instant Capture**
4. The waveform appears in the browser once capture completes

**Trigger types:** rising edge · falling edge · crossing · high level · low level

**Buffer:** 128 KB at up to 264 MHz → ~500 µs full-speed capture window

---

## FPGA

The ICE40UP5K FPGA is the signal-routing backbone of the device. It handles:

- Logic analyzer signal capture (16 channels)
- Protocol multiplexing across ports A–D
- Reset signal generation
- JTAG/SWD signal path selection

The bitstream (`main/ice40up5k/bitstream.bin`) is embedded in the firmware and loaded automatically at startup. To update the bitstream, replace the file and rebuild.

---

## Partition Table

| Name | Type | Offset | Size |
|---|---|---|---|
| NVS | data/nvs | 0x9000 | 24 KB |
| OTA data | data/ota | 0xf000 | 8 KB |
| PHY init | data/phy | 0x11000 | 4 KB |
| factory | app/factory | 0x20000 | 5 MB |
| ota_0 | app/ota_0 | — | 2 MB |
| ota_1 | app/ota_1 | — | 2 MB |
| storage (FAT) | data/fat | — | 528 KB |

---

## NVS Keys Reference

| Key | Description |
|---|---|
| `pa_cfg` … `pd_cfg` | Port A–D mode |
| `target_voltage` | Target IO voltage index (`0`=3.3V … `4`=1.2V) |
| `sw_mcu` | Software MCU mode (`0`/`1`) |
| `mcu_if` | MCU interface (fixed to `GPIO`) |
| `ssid` / `pass` | WiFi STA (SM mode) credentials |
| `ap_ssid` / `ap_pass` | WiFi AP credentials |
| `wifi_mode` | WiFi mode: `AP` or `SM` |
| `ota_url_key` | OTA update URL |
| `web_user` / `web_pass` | Web UI basic-auth credentials |
| `uart_baud` | UART baud rate |
| `uart_dbits` | UART data bits |
| `uart_sbits` | UART stop bits |
| `uart_parity` | UART parity (`n`/`e`/`o`) |
| `uart_psel` | UART port (`0`=USB, `1`=Web) |
| `dis_usb_dap` | Disable USB CMSIS-DAP interface (`1`=disabled, `0`=enabled) |

---

## Build Configuration

Key `sdkconfig` options:

| Option | Value |
|---|---|
| Target | `esp32s3` |
| Flash size | 16 MB |
| Flash frequency | 80 MHz |
| CPU frequency | 240 MHz |
| SPIRAM | Enabled (80 MHz) |
| Main task stack | 32 KB |
| Watchdog timeout | 15 s |
| UI | Enabled via `CONFIG_UI_ENABLE` |

---

## Project Structure

```
esp32jtag/
├── main/
│   ├── main.c                  # Application entry point, LCD screens, button handling
│   ├── console_menu.c          # USB Serial JTAG console REPL + flat commands
│   ├── console_menu.h          # console_menu_init() declaration
│   ├── types.h                 # Shared type definitions and NVS keys
│   ├── esp32jtag_common.h      # Pin definitions and port enums
│   ├── storage.c               # NVS read/write helpers
│   ├── ice40up5k/
│   │   ├── ice.c               # FPGA SPI driver
│   │   └── bitstream.bin       # ICE40UP5K bitstream (embedded)
│   ├── network/
│   │   ├── web_server.c        # HTTP/HTTPS handlers (config, debug status, OTA, port D)
│   │   ├── network_mngr.c      # WiFi AP/STA management
│   │   ├── network_mngr_ota.c  # OTA update handler
│   │   ├── uart_websocket.c    # WebSocket ↔ UART bridge
│   │   └── descriptors.c       # Port configuration descriptors
│   └── web/                    # Embedded HTML assets
├── components/
│   ├── blackmagic_esp32/       # BlackMagic Probe (submodule)
│   ├── CherryDAP/              # CMSIS-DAP over USB
│   ├── lcd/                    # LCD driver + fonts (GUI_Paint / LCD_Driver)
│   └── platform_include/       # POSIX shims for host headers
├── partitions/ota_2mb.csv      # 16MB flash, factory + 2× OTA partitions
├── CMakeLists.txt
└── sdkconfig.defaults
```

---

## License

Apache License 2.0 — see [LICENSE](LICENSE) for details.

Third-party components retain their own licenses:
- **BlackMagic Probe** — GPL-3.0
- **CherryUSB / CherryDAP** — Apache-2.0
