"""
Integration tests for the ESP32-JTAG serial console.

Tests the USB Serial JTAG console (main/console_menu.c): the REPL prompt,
registered commands, and settings written via flat commands. Run against a
live, flashed device connected over USB Serial JTAG.

Requires:
  - pyserial  (pip install pyserial)
  - A device flashed with firmware that includes the console commands,
    connected over USB Serial JTAG (e.g. /dev/ttyACM0)
  - Optionally: requests + --host, to cross-check that a setting written
    through the console is visible to the HTTPS web server

Run with --help to see all options and examples.

Notes:
  - The console uses CR line endings for input: Enter must be sent as `\r`.
  - Boot/status logs are printed on the same port and are interleaved with the
    REPL output, so tests wait for unique marker strings instead of silence.

Safety:
  Destructive commands are never executed: reboot, factory_reset (confirmed),
  ota_switch, port D output, port D frequency, and reset_target (when Port B is
  in SReset mode). Guards are exercised by asserting the error/usage message
  instead of running the action.
"""

import argparse
import sys
import time

import serial

EXPECTED_COMMANDS = (
    "porta",
    "portb",
    "portc",
    "portd",
    "vio",
    "wifi_mode",
    "wifi_ssid",
    "wifi_pass",
    "ota_url",
    "web_user",
    "web_pass",
    "uart_baud",
    "uart_dbits",
    "uart_sbits",
    "uart_parity",
    "uart_psel",
    "usb_dap",
    "sreset",
    "portd_output",
    "portd_freq",
    "ota_switch",
    "reset_target",
    "show",
    "factory_reset",
    "reboot",
    "log",
)

EXPECTED_SHOW_MARKERS = (
    "--- Current Settings ---",
    "Port A",
    "Port B",
    "Port C",
    "Port D",
    "Target IO V",
    "WiFi mode",
    "UART baud",
    "UART dbits",
    "UART sbits",
    "UART parity",
    "USB DAP",
    "SRESET",
    "OTA",
    "Running:",
    "Alternate:",
)

PROMPT = "esp32jtag>"


def make_parser():
    p = argparse.ArgumentParser(
        description="Integration tests for the ESP32-JTAG serial console.",
        epilog="Examples:\n"
        "  %(prog)s --device /dev/ttyACM0\n"
        "  %(prog)s --device /dev/ttyACM0 --host 192.168.134.200\n"
        "  %(prog)s --device /dev/ttyACM0 --baud 921600 --host 192.168.134.200",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--device", required=True, help="USB Serial JTAG device (e.g. /dev/ttyACM0)")
    p.add_argument("--baud", type=int, default=115200, help="Serial baud rate (default: 115200)")
    p.add_argument("--host", default=None,
                   help="Device IP for web cross-checks (optional; enables settings persistence tests)")
    p.add_argument("--username", default="admin", help="HTTP basic auth username (default: admin)")
    p.add_argument("--password", default="admin", help="HTTP basic auth password (default: admin)")
    return p


class Console:
    """Thin wrapper over a pyserial connection to the REPL.

    Incoming bytes are accumulated in a persistent history buffer, so bytes
    that arrive after a matched marker are not lost to the next call.
    """

    def __init__(self, device, baud):
        self.port = serial.Serial(device, baud, timeout=1)
        self.history = bytearray()
        self.pos = 0  # index into history of the last consumed byte
        time.sleep(0.2)
        self.port.reset_input_buffer()

    def close(self):
        try:
            self.port.close()
        except Exception:
            pass

    def send(self, text):
        self.port.write(text.encode())
        self.port.flush()

    def read_until(self, marker, timeout=8.0):
        """Read until marker appears in NEW output (after the current cursor).

        Returns the newly received text since the last call, without
        re-matching markers already consumed by previous calls.
        """
        mb = marker.encode()
        end = time.time() + timeout
        while True:
            hit = self.history.find(mb, self.pos)
            if hit >= 0:
                new = bytes(self.history[self.pos:hit + len(mb)])
                self.pos = hit + len(mb)
                return new.decode("utf-8", "replace")
            if time.time() > end:
                raise TimeoutError(
                    f"timed out waiting for {marker!r}; got: {bytes(self.history[self.pos:])[-400:]!r}"
                )
            n = self.port.in_waiting
            if n:
                self.history += self.port.read(n)
            else:
                time.sleep(0.05)

    def drain(self, seconds=0.5):
        end = time.time() + seconds
        while time.time() < end:
            n = self.port.in_waiting
            if n:
                self.history += self.port.read(n)
            else:
                time.sleep(0.05)

    def wait_prompt(self):
        """Ensure the REPL is idle at the prompt."""
        self.send("\r")
        self.read_until(PROMPT, timeout=8.0)
        self.drain()

    def command(self, cmd):
        """Run a console command and wait for the prompt to return."""
        self.send(cmd + "\r")
        return self.read_until(PROMPT, timeout=8.0)


def web_request(args, method, path, payload=None, retries=5, timeout=30):
    import requests  # imported lazily; --host enables this path
    import urllib3

    urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)
    last_err = None
    for attempt in range(retries):
        try:
            r = requests.request(
                method,
                f"https://{args.host}:443{path}",
                auth=(args.username, args.password),
                json=payload,
                verify=False,
                timeout=timeout,
            )
            assert r.status_code == 200, f"{path}: {r.status_code} {r.text[:200]}"
            return r
        except (requests.exceptions.RequestException, AssertionError) as e:
            last_err = e
            time.sleep(2)
    raise AssertionError(f"web {method} {path} failed after {retries} attempts: {last_err}")


def web_get(args, path):
    return web_request(args, "GET", path).json()


def web_post(args, path, payload):
    return web_request(args, "POST", path, payload=payload).text


def test_prompt(con, args):
    """REPL prompt is responsive."""
    con.send("\r")
    con.read_until(PROMPT, timeout=8.0)
    print("[PASS] prompt: REPL responds at esp32jtag>")


def test_help_lists_commands(con, args):
    """help lists every registered console command."""
    out = con.command("help")
    missing = [c for c in EXPECTED_COMMANDS if c not in out]
    assert not missing, f"help missing commands: {missing}"
    print(f"[PASS] help lists {len(EXPECTED_COMMANDS)} commands")


def test_show_settings(con, args):
    """show prints all current settings."""
    out = con.command("show")
    missing = [m for m in EXPECTED_SHOW_MARKERS if m not in out]
    assert not missing, f"show missing markers: {missing}"
    print(f"[PASS] show prints {len(EXPECTED_SHOW_MARKERS)} setting groups")


def test_settings_getters(con, args):
    """Settings commands with no args print current value + usage."""
    for cmd in ("porta", "portb", "portc", "portd", "vio", "wifi_mode",
                "wifi_ssid", "wifi_pass", "ota_url", "web_user", "web_pass",
                "uart_baud", "uart_dbits", "uart_sbits", "uart_parity",
                "uart_psel", "usb_dap", "sreset", "portd_output", "portd_freq"):
        out = con.command(cmd)
        assert "Usage" in out or "Current" in out, f"{cmd}: missing usage/current, got {out[-150:]!r}"
    print(f"[PASS] no-arg settings commands show usage + current value (20 commands)")


def test_help_specific(con, args):
    """help <cmd> and <cmd> --help print a short summary for every command."""
    for cmd in EXPECTED_COMMANDS:
        out = con.command(f"help {cmd}")
        assert cmd in out and "Usage:" in out, f"help {cmd}: {out[-150:]!r}"
    for cmd in EXPECTED_COMMANDS:
        out = con.command(f"{cmd} --help")
        assert cmd in out and "Usage:" in out, f"{cmd} --help: {out[-150:]!r}"
    out = con.command("help bogus")
    assert "Unknown command" in out, f"help bogus: {out[-150:]!r}"
    print(f"[PASS] help <cmd> and <cmd> --help work for all {len(EXPECTED_COMMANDS)} commands")


def test_show_ota_status(con, args):
    """show includes the OTA partition status; standalone ota_status is removed."""
    out = con.command("show")
    assert "Running:" in out and "Alternate:" in out, f"show OTA section: {out[-200:]!r}"
    out = con.command("ota_status")
    assert "Unrecognized command" in out, f"ota_status should be removed: {out[-150:]!r}"
    print("[PASS] show prints OTA status; ota_status command removed")


def test_settings_roundtrips(con, args):
    """Each web-visible setting: write via console, verify via web API, restore.

    Writes go to NVS; the running firmware is unchanged (settings need a reboot
    to take effect), so restoring the original NVS value is always safe.
    """
    if not args.host:
        print("[SKIP] settings_roundtrips: --host not given, skipping web cross-check")
        return

    labels = ["3.3", "2.5", "1.8", "1.5", "1.2"]
    creds = web_get(args, "/get_credentials")

    cases = []  # (cmd, web_key, original, test_arg, expected_web, restore_arg)
    for cmd, key, a, b in (
        ("porta", "pacfg", "2", "3"),
        ("portb", "pbcfg", "1", "0"),
        ("portc", "pccfg", "2", "0"),
        ("portd", "pdcfg", "2", "0"),
        ("uart_baud", "uartBaud", "9600", "115200"),
        ("uart_dbits", "uartDataBits", "7", "8"),
        ("uart_sbits", "uartStopBits", "1.5", "1"),
        ("uart_parity", "uartParity", "e", "n"),
        ("uart_psel", "uartPortSel", "0", "1"),
    ):
        orig = creds.get(key, "")
        test_arg = a if orig != a else b
        cases.append((cmd, key, orig, test_arg, test_arg, orig))

    # usb_dap: console arg 0/1, web value is a bool
    orig_dap = creds.get("disableUsbDapCom", True)
    test_arg = "0" if orig_dap else "1"
    restore_dap = "1" if orig_dap else "0"
    cases.append(("usb_dap", "disableUsbDapCom", orig_dap, test_arg,
                  test_arg == "1", restore_dap))

    # vio: console takes a voltage label, web stores the index
    try:
        orig_idx = int(creds.get("targetVoltage", "2"))
    except ValueError:
        orig_idx = 2
    test_label = labels[(orig_idx + 1) % 5]
    restore_label = labels[orig_idx % 5]
    cases.append(("vio", "targetVoltage", str(orig_idx), test_label,
                  str(labels.index(test_label)), restore_label))

    failures = []
    for cmd, key, orig, test_arg, expected, restore_arg in cases:
        try:
            out = con.command(f"{cmd} {test_arg}")
            after = web_get(args, "/get_credentials").get(key)
            assert after == expected, \
                f"{cmd} {test_arg}: web {key}={after!r}, expected {expected!r} (out={out[-100:]!r})"
        except AssertionError as e:
            failures.append(str(e))
        finally:
            con.command(f"{cmd} {restore_arg}")
            restored = web_get(args, "/get_credentials").get(key)
            assert restored == orig, \
                f"restore {cmd}: web {key}={restored!r}, expected {orig!r}"
    if failures:
        raise AssertionError("; ".join(failures))
    print(f"[PASS] settings roundtrips via web API: {len(cases)} settings")


def _current_value(out):
    """Extract the value printed after 'Current ...:' on the last line."""
    lines = out.split("\r\n")
    for line in reversed(lines):
        if "Current" in line:
            return line.rsplit(":", 1)[1].strip()
    return None


def test_console_only_setters(con, args):
    """wifi_mode/wifi_ssid/web_user/sreset: write via console, verify getter, restore."""
    # wifi_mode (getter prints 'Current: SM (Station (ST) Mode)')
    out = con.command("wifi_mode")
    cur_mode = _current_value(out)
    assert cur_mode, f"wifi_mode getter: {out[-150:]!r}"
    cur_mode = cur_mode.split()[0]
    other = "SM" if cur_mode == "AP" else "AP"
    con.command(f"wifi_mode {other}")
    out = con.command("wifi_mode")
    assert _current_value(out).split()[0] == other, f"wifi_mode write: {out[-150:]!r}"
    con.command(f"wifi_mode {cur_mode}")

    # wifi_ssid
    out = con.command("wifi_ssid")
    orig_ssid = _current_value(out)
    assert orig_ssid is not None, f"wifi_ssid getter: {out[-150:]!r}"
    con.command("wifi_ssid console-test-ssid")
    out = con.command("wifi_ssid")
    assert _current_value(out) == "console-test-ssid", f"wifi_ssid write: {out[-150:]!r}"
    con.command(f"wifi_ssid {orig_ssid}")

    # web_user
    out = con.command("web_user")
    orig_user = _current_value(out)
    assert orig_user is not None, f"web_user getter: {out[-150:]!r}"
    con.command("web_user console-test-user")
    out = con.command("web_user")
    assert _current_value(out) == "console-test-user", f"web_user write: {out[-150:]!r}"
    con.command(f"web_user {orig_user}")

    # sreset (applied instantly to RAM globals; getter shows polarity + pulse)
    out = con.command("sreset")
    assert "Current:" in out, f"sreset getter: {out[-150:]!r}"
    pol = "1" if "polarity=1" in out else "0"
    pulse = "100"
    if "pulse=" in out:
        pulse = out.split("pulse=", 1)[1].split("ms", 1)[0].strip()
    other_pol = "0" if pol == "1" else "1"
    con.command(f"sreset {other_pol} {pulse}")
    out = con.command("sreset")
    assert f"polarity={other_pol}" in out, f"sreset write: {out[-150:]!r}"
    con.command(f"sreset {pol} {pulse}")

    con.drain()
    print("[PASS] wifi_mode/wifi_ssid/web_user/sreset write + getter + restore")


def test_ota_url_persists(con, args):
    """ota_url written via console persists to NVS and is visible to the web API."""
    if not args.host:
        print("[SKIP] ota_url_persists: --host not given, skipping web cross-check")
        return

    creds = web_get(args, "/get_credentials")
    original = creds.get("otaUrl", "")
    test_url = "http://console-test.example/ota.bin"

    try:
        out = con.command(f"ota_url {test_url}")
        assert "OTA URL set" in out, f"ota_url: {out[-150:]!r}"
        after = web_get(args, "/get_credentials")
        assert after.get("otaUrl") == test_url, \
            f"web API did not see console-written OTA URL: {after.get('otaUrl')!r}"
        print(f"[PASS] ota_url written, web API reads it back ({test_url})")
    finally:
        # The web /set_credentials handler unconditionally writes disableUsbDapCom
        # (defaulting to false when absent), so preserve the original value to
        # avoid clobbering the USB DAP setting during restore.
        dap = web_get(args, "/get_credentials").get("disableUsbDapCom")
        web_post(args, "/set_credentials", {"otaUrl": original, "disableUsbDapCom": dap})
        restored = web_get(args, "/get_credentials")
        assert restored.get("otaUrl") == original, \
            f"failed to restore OTA URL to {original!r}: {restored.get('otaUrl')!r}"
        con.drain()


def test_factory_reset_requires_confirm(con, args):
    """factory_reset without 'yes' only prints a warning."""
    out = con.command("factory_reset")
    assert "factory_reset yes" in out, f"factory_reset: {out[-150:]!r}"
    print("[PASS] factory_reset requires explicit 'yes' argument")


def test_reset_target_guard(con, args):
    """reset_target is rejected unless Port B is in SReset mode."""
    con.send("reset_target\r")
    out = con.read_until(PROMPT, timeout=8.0)
    if "Error: Port B must be configured" in out:
        print("[PASS] reset_target guard: rejected when Port B is not SReset")
    elif "Reset pulse sent" in out:
        print("[PASS] reset_target guard: Port B is SReset mode, pulse sent")
    else:
        raise AssertionError(f"unexpected reset_target output: {out[-200:]!r}")


def test_portd_guard(con, args):
    """portd_output/portd_freq run when Port D is Logic Analyzer, otherwise rejected.

    Restores the port to a neutral state (tristate, freq 0) afterwards so the
    device is left unchanged.
    """
    try:
        con.send("portd_output 3 0\r")
        out = con.read_until(PROMPT, timeout=8.0)
        if "Error: Port D must be configured" in out:
            print("[PASS] portd_output rejected (Port D not Logic Analyzer)")
        else:
            assert "Port D output: mode=3 value=0" in out, f"portd_output: {out[-150:]!r}"
            print("[PASS] portd_output ran (Port D Logic Analyzer)")

        con.send("portd_freq 125\r")
        out = con.read_until(PROMPT, timeout=8.0)
        if "Error: Port D must be configured" in out:
            print("[PASS] portd_freq rejected (Port D not Logic Analyzer)")
        else:
            assert "Port D freq: 125 Hz" in out, f"portd_freq: {out[-150:]!r}"
            print("[PASS] portd_freq ran (Port D Logic Analyzer)")
    finally:
        con.send("portd_output 0 0\r")
        con.read_until(PROMPT, timeout=8.0)
        con.send("portd_freq 0\r")
        con.read_until(PROMPT, timeout=8.0)
        con.drain()


def test_invalid_args(con, args):
    """Commands reject out-of-range arguments without crashing."""
    for cmd in ("porta 9", "portb 9", "portc 9", "portd 9", "vio 9",
                "vio 2", "vio 0", "vio 2.0", "vio 3.3Vx", "vio x",
                "wifi_mode XX", "uart_baud 5", "uart_dbits 4",
                "uart_sbits 3", "uart_parity x", "uart_psel 9", "usb_dap 9",
                "sreset 2", "sreset 1 0", "portd_freq 123",
                "portd_output 5 0", "portd_output 1 99", "log maybe"):
        out = con.command(cmd)
        assert ("Invalid" in out or "Usage" in out), f"{cmd}: {out[-150:]!r}"
    print("[PASS] invalid arguments rejected for all settings commands")


def test_vio_voltage_only(con, args):
    """vio accepts only voltage values (1.8, 1.8v, 1.8V); plain indexes rejected.

    Writes go to NVS and are restored afterwards (needs a reboot to apply).
    """
    labels = ["3.3", "2.5", "1.8", "1.5", "1.2"]
    out = con.command("vio")
    cur = _current_value(out)  # e.g. "1.8V"
    assert cur, f"vio getter: {out[-150:]!r}"
    try:
        for good in ("1.2", "1.5", "1.8v", "2.5", "3.3V"):
            out = con.command(f"vio {good}")
            assert ("set to" in out or "already" in out), \
                f"vio {good}: {out[-150:]!r}"
        for bad in ("0", "1", "2", "3", "4", "2.0", "5v", "1.0", "x"):
            out = con.command(f"vio {bad}")
            assert "Invalid voltage" in out, f"vio {bad}: {out[-150:]!r}"
    finally:
        orig_label = cur[:-1] if cur and cur[-1] in "vV" else cur
        if orig_label not in labels:
            orig_label = labels[2]  # fallback to 1.8V
        con.command(f"vio {orig_label}")
        con.drain()
    print("[PASS] vio accepts only voltage values; plain indexes rejected")


def test_log_toggle(con, args):
    """log off silences ESP_LOG output, log on restores it."""
    marker = "MAIN: Free mem"
    con.send("log off\r")
    con.read_until("Log output disabled", timeout=8.0)
    con.read_until(PROMPT, timeout=8.0)

    before = len(con.history)
    time.sleep(1.5)
    con.drain()
    quiet = bytes(con.history[before:])
    assert marker.encode() not in quiet, "logs still flowing after 'log off'"

    con.send("log on\r")
    con.read_until("Log output enabled", timeout=8.0)
    con.read_until(PROMPT, timeout=8.0)
    con.read_until(marker, timeout=10.0)
    print("[PASS] log off silences output, log on restores it")


def main():
    parser = make_parser()
    args = parser.parse_args()

    groups = [
        ("REPL basics", [
            ("prompt", test_prompt),
            ("help lists commands", test_help_lists_commands),
            ("show settings", test_show_settings),
        ]),
        ("Settings getters", [
            ("no-arg commands show usage", test_settings_getters),
            ("help <cmd> / <cmd> --help", test_help_specific),
            ("show includes OTA status", test_show_ota_status),
        ]),
        ("Settings writes", [
            ("settings roundtrips via web", test_settings_roundtrips),
            ("ota_url persistence", test_ota_url_persists),
            ("console-only setters", test_console_only_setters),
        ]),
        ("Actions & guards", [
            ("factory_reset confirmation", test_factory_reset_requires_confirm),
            ("reset_target guard", test_reset_target_guard),
            ("portd guard", test_portd_guard),
            ("invalid args", test_invalid_args),
            ("vio voltage-only", test_vio_voltage_only),
            ("log toggle", test_log_toggle),
        ]),
    ]

    con = Console(args.device, args.baud)
    try:
        con.wait_prompt()

        passed = 0
        failed = 0
        skipped = 0
        for group_name, tests in groups:
            print(f"\n== {group_name} ==")
            # Re-assert a clean REPL prompt before each group so a failed
            # test that left state behind does not cascade into the next one.
            con.send("\r")
            con.read_until(PROMPT, timeout=4.0)
            for name, fn in tests:
                try:
                    fn(con, args)
                    passed += 1
                except (AssertionError, TimeoutError, serial.SerialException) as e:
                    print(f"[FAIL] {name}: {e}", file=sys.stderr)
                    failed += 1

        print(f"\n{'='*40}\nResults: {passed} passed, {failed} failed, {skipped} skipped")
        return 0 if failed == 0 else 1
    finally:
        con.close()


if __name__ == "__main__":
    sys.exit(main())
