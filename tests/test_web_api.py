"""
Integration tests for the ESP32-JTAG firmware web API.

Tests the HTTPS web server as implemented in main/network/web_server.c.
Run against a live, flashed device.

Requires:
  - requests  (pip install requests)
  - A running device with HTTPS on port 443

Run with --help to see all options and examples.

Safety:
  Destructive endpoints (factory reset, OTA partition switch, reboot
  bootloader, OTA upload) are intentionally NOT tested.
"""

import argparse
import json
import sys
import time
import requests
import urllib3

urllib3.disable_warnings(urllib3.exceptions.InsecureRequestWarning)

EXPECTED_CREDENTIAL_KEYS = (
    "pacfg", "pbcfg", "pccfg", "pdcfg", "targetVoltage", "swMcu",
    "ssid_st", "ssid_ap", "ssid", "password", "wifiMode", "otaUrl",
    "mcuInterface", "uartPortSel", "uartBaud", "uartDataBits",
    "uartStopBits", "uartParity", "disableUsbDapCom",
)


def make_parser():
    p = argparse.ArgumentParser(
        description="Integration tests for the ESP32-JTAG firmware web API.",
        epilog="Examples:\n"
        "  %(prog)s --host 192.168.1.100\n"
        "  %(prog)s --host 192.168.1.100 --port 8080\n"
        "  %(prog)s --host 192.168.1.100 --username foo --password bar",
        formatter_class=argparse.RawDescriptionHelpFormatter,
    )
    p.add_argument("--host", required=True, help="Device IP address")
    p.add_argument("--port", type=int, default=443, help="HTTPS port (default: 443)")
    p.add_argument("--username", default="admin", help="HTTP basic auth username (factory default: admin)")
    p.add_argument("--password", default="admin", help="HTTP basic auth password (factory default: admin)")
    return p


def url(args, path):
    return f"https://{args.host}:{args.port}{path}"


def get_json(args, path, auth=None, **kw):
    r = requests.get(url(args, path), auth=auth, verify=False, timeout=10, **kw)
    assert r.status_code == 200, f"{path}: expected 200, got {r.status_code}"
    data = r.json()
    assert isinstance(data, dict), f"{path}: expected JSON object, got {type(data)}"
    return data


def test_root(args):
    """Root endpoint serves the web UI when authenticated."""
    r = requests.get(url(args, "/"), auth=(args.username, args.password), verify=False, timeout=5)
    assert r.status_code == 200, f"root: {r.status_code}"
    assert "<html" in r.text.lower() or "<!doctype html" in r.text.lower(), \
        f"root: expected HTML, got: {r.text[:80]!r}"
    print(f"[PASS] root: served {len(r.text)} bytes of HTML")


def test_root_requires_auth(args):
    """Root endpoint without auth should return 401."""
    r = requests.get(url(args, "/"), verify=False, timeout=5)
    assert r.status_code == 401, f"root no auth: expected 401, got {r.status_code}"
    print("[PASS] root requires auth: 401")


def test_version(args):
    """Version endpoint returns firmware metadata."""
    data = get_json(args, "/api/version", auth=(args.username, args.password))
    for key in ("project_name", "firmware_version", "hardware_version",
                "idf_version", "main_git_commit", "blackmagic_git_commit"):
        assert key in data, f"version missing '{key}': {data}"
    print(f"[PASS] version: {data.get('project_name')} "
          f"{data.get('firmware_version')} (commit {data.get('main_git_commit')})")


def test_debug_status(args):
    """Debug status returns probe state."""
    data = get_json(args, "/api/debug_status", auth=(args.username, args.password))
    for key in ("connected", "state", "port_c_active"):
        assert key in data, f"debug_status missing '{key}': {data}"
    assert data["port_c_active"] in (True, False), f"debug_status port_c_active: {data}"
    print(f"[PASS] debug_status: connected={data['connected']} state={data['state']}")


def test_ota_status(args):
    """OTA status returns current and alternate partition info."""
    data = get_json(args, "/api/ota_status", auth=(args.username, args.password))
    assert "current" in data, f"ota_status missing 'current': {data}"
    cur = data["current"]
    assert "label" in cur, f"ota_status.current missing label: {data}"
    assert "has_alternate" in data, f"ota_status missing 'has_alternate': {data}"
    print(f"[PASS] ota_status: current={cur.get('label')} "
          f"v{cur.get('version')} alternate={data['has_alternate']}")


def test_get_credentials(args):
    """GET credentials returns all configuration keys."""
    data = get_json(args, "/get_credentials", auth=(args.username, args.password))
    missing = [k for k in EXPECTED_CREDENTIAL_KEYS if k not in data]
    assert not missing, f"get_credentials missing keys: {missing}"
    assert data["mcuInterface"] == "GPIO", f"get_credentials mcuInterface: {data}"
    print(f"[PASS] get_credentials: {len(data)} keys "
          f"(pccfg={data['pccfg']}, uartBaud={data['uartBaud']})")


def test_la_status(args):
    """Logic analyzer status returns capture flags."""
    data = get_json(args, "/la_status", auth=(args.username, args.password))
    for key in ("triggered", "all_captured", "wr_addr_stop_position", "trigger_position"):
        assert key in data, f"la_status missing '{key}': {data}"
    print(f"[PASS] la_status: triggered={data['triggered']} "
          f"all_captured={data['all_captured']}")


def test_la_get_settings(args):
    """Logic analyzer settings returns config."""
    data = get_json(args, "/la_get_settings", auth=(args.username, args.password))
    for key in ("sampleRate", "triggerEnabled", "triggerPosition", "channels"):
        assert key in data, f"la_get_settings missing '{key}': {data}"
    assert isinstance(data["channels"], list), f"la_get_settings channels: {data}"
    print(f"[PASS] la_get_settings: sampleRate={data['sampleRate']} "
          f"channels={len(data['channels'])}")


def test_uart_debug(args):
    """UART debug endpoint returns UART state JSON."""
    data = get_json(args, "/api/uart_debug", auth=(args.username, args.password))
    for key in ("status", "uart_port_num", "uart_port_sel", "total_rx_messages"):
        assert key in data, f"uart_debug missing '{key}': {data}"
    print(f"[PASS] uart_debug: status={data.get('status')} "
          f"port={data.get('uart_port_num')} rx_msgs={data.get('total_rx_messages')}")


def test_set_credentials_same_value(args):
    """Posting the current disableUsbDapCom value should NOT reboot.

    Note: the handler marks any change to disableUsbDapCom (or any UART
    setting) as reboot_required, so this test echoes the value currently
    stored on the device to avoid a reboot mid-suite.
    """
    creds = get_json(args, "/get_credentials", auth=(args.username, args.password))
    current_dap = creds["disableUsbDapCom"]
    r = requests.post(
        url(args, "/set_credentials"),
        auth=(args.username, args.password),
        json={"disableUsbDapCom": current_dap},
        verify=False,
        timeout=5,
    )
    assert r.status_code == 200, f"set_credentials: {r.status_code} {r.text[:200]}"
    assert "Settings saved" in r.text, f"set_credentials: expected 'Settings saved', got {r.text[:200]!r}"
    print(f"[PASS] set_credentials (echo disableUsbDapCom={current_dap}): Settings saved")


def test_set_credentials_invalid(args):
    """Send invalid JSON and expect 400."""
    r = requests.post(
        url(args, "/set_credentials"),
        auth=(args.username, args.password),
        data="not json",
        headers={"Content-Type": "application/json"},
        verify=False,
        timeout=5,
    )
    assert r.status_code == 400, f"set_credentials invalid: expected 400, got {r.status_code}"
    print("[PASS] set_credentials invalid JSON: 400")


def test_set_credentials_no_auth(args):
    """Request without auth should return 401."""
    r = requests.post(
        url(args, "/set_credentials"),
        json={"otaUrl": ""},
        verify=False,
        timeout=5,
    )
    assert r.status_code == 401, f"set_credentials no auth: expected 401, got {r.status_code}"
    print("[PASS] set_credentials no auth: 401")


def test_api_requires_auth(args):
    """API endpoints without auth should return 401."""
    for path in ("/api/version", "/api/ota_status", "/get_credentials", "/la_status"):
        r = requests.get(url(args, path), verify=False, timeout=5)
        assert r.status_code == 401, f"{path} no auth: expected 401, got {r.status_code}"
    print("[PASS] API endpoints require auth: 401")


def test_unknown_404(args):
    """Unknown path should return 404."""
    r = requests.get(url(args, "/no/such/endpoint"), auth=(args.username, args.password),
                     verify=False, timeout=5)
    assert r.status_code == 404, f"unknown path: expected 404, got {r.status_code}"
    print("[PASS] unknown endpoint: 404")


def test_help_page(args):
    """Help page is served without authentication."""
    r = requests.get(url(args, "/help"), verify=False, timeout=5)
    assert r.status_code == 200, f"help: {r.status_code}"
    assert "<html" in r.text.lower(), f"help: expected HTML, got: {r.text[:80]!r}"
    print("[PASS] help page (no auth): served HTML")


def test_static_assets(args):
    """Static assets are served without authentication."""
    r = requests.get(url(args, "/ez32.svg"), verify=False, timeout=5)
    assert r.status_code == 200, f"ez32.svg: {r.status_code}"
    assert "image/svg" in r.headers.get("Content-Type", ""), \
        f"ez32.svg: expected image/svg+xml, got {r.headers.get('Content-Type')!r}"
    r = requests.get(url(args, "/favicon.ico"), verify=False, timeout=5)
    assert r.status_code == 200, f"favicon.ico: {r.status_code}"
    assert len(r.content) > 0, "favicon.ico: empty body"
    print(f"[PASS] static assets (no auth): ez32.svg, favicon.ico ({len(r.content)} bytes)")


def test_logic_analyzer_page(args):
    """Logic analyzer page requires auth."""
    r = requests.get(url(args, "/logic_analyzer.html"), verify=False, timeout=5)
    assert r.status_code == 401, f"logic_analyzer.html no auth: expected 401, got {r.status_code}"
    r = requests.get(url(args, "/logic_analyzer.html"), auth=(args.username, args.password),
                     verify=False, timeout=5)
    assert r.status_code == 200, f"logic_analyzer.html auth: expected 200, got {r.status_code}"
    assert "<html" in r.text.lower(), "logic_analyzer.html: expected HTML"
    print("[PASS] logic_analyzer.html: 401 no auth, 200 with auth")


def test_log_error(args):
    """Client-side error logging endpoint accepts a message."""
    r = requests.post(
        url(args, "/log_error"),
        json={"message": "integration test error"},
        verify=False,
        timeout=5,
    )
    assert r.status_code == 200, f"log_error: {r.status_code} {r.text[:200]}"
    assert "Error logged" in r.text, f"log_error: {r.text[:200]!r}"
    print("[PASS] log_error: accepted client error")


def test_test_status(args):
    """Self-test status endpoint returns JSON (public)."""
    r = requests.get(url(args, "/test/status"), verify=False, timeout=5)
    assert r.status_code == 200, f"test/status: {r.status_code}"
    data = r.json()
    assert "status" in data and "test" in data, f"test/status: {data}"
    print(f"[PASS] test/status (no auth): {data['status']}")


def test_test_result(args):
    """Self-test result endpoint returns JSON (public)."""
    r = requests.get(url(args, "/test/result"), verify=False, timeout=5)
    assert r.status_code == 200, f"test/result: {r.status_code}"
    assert r.json() is not None, "test/result: expected JSON"
    print("[PASS] test/result (no auth): JSON")


def test_sreset_config(args):
    """SRESET config accepts a valid pulse width and rejects bad ones."""
    r = requests.post(
        url(args, "/api/sreset_config"),
        auth=(args.username, args.password),
        json={"polarity": 1, "pulse_ms": 100},
        verify=False,
        timeout=5,
    )
    assert r.status_code == 200, f"sreset_config: {r.status_code} {r.text[:200]}"
    data = r.json()
    assert data.get("status") == "ok", f"sreset_config: {data}"
    assert data.get("pulse_ms") == 100, f"sreset_config: {data}"

    r = requests.post(
        url(args, "/api/sreset_config"),
        auth=(args.username, args.password),
        json={"polarity": 1, "pulse_ms": 99999},
        verify=False,
        timeout=5,
    )
    assert r.status_code == 400, f"sreset_config bad pulse: expected 400, got {r.status_code}"
    print("[PASS] sreset_config: 200 ok (pulse_ms=100), 400 on invalid pulse_ms")


def test_portd_freq(args):
    """Port D frequency endpoint accepts 0 (stop) and rejects bad values."""
    r = requests.post(
        url(args, "/api/portd_freq"),
        auth=(args.username, args.password),
        json={"freq_hz": 0},
        verify=False,
        timeout=5,
    )
    assert r.status_code == 200, f"portd_freq: {r.status_code} {r.text[:200]}"
    data = r.json()
    assert data.get("status") == "ok", f"portd_freq: {data}"

    r = requests.post(
        url(args, "/api/portd_freq"),
        auth=(args.username, args.password),
        json={"freq_hz": 123},
        verify=False,
        timeout=5,
    )
    assert r.status_code == 400, f"portd_freq bad: expected 400, got {r.status_code}"
    print("[PASS] portd_freq: 200 ok (freq_hz=0), 400 on invalid freq_hz")


def test_wrong_method_405(args):
    """Wrong HTTP method on a known path should return 405."""
    r = requests.post(url(args, "/api/version"), auth=(args.username, args.password),
                      verify=False, timeout=5)
    assert r.status_code == 405, f"POST /api/version: expected 405, got {r.status_code}"
    r = requests.get(url(args, "/set_credentials"), auth=(args.username, args.password),
                     verify=False, timeout=5)
    assert r.status_code == 405, f"GET /set_credentials: expected 405, got {r.status_code}"
    print("[PASS] wrong method: 405")


def test_wrong_credentials(args):
    """Bad credentials should return 401."""
    r = requests.get(url(args, "/api/ota_status"), auth=("admin", "wrongpassword"),
                     verify=False, timeout=5)
    assert r.status_code == 401, f"wrong creds: expected 401, got {r.status_code}"
    print("[PASS] wrong credentials: 401")


def main():
    parser = make_parser()
    args = parser.parse_args()

    # Grouped so failures are easy to locate. Order within each group matters:
    # auth-dependent reads come before the auth-negative checks that follow them.
    groups = [
        ("UI pages", [
            ("root", test_root),
            ("root requires auth", test_root_requires_auth),
            ("help page (no auth)", test_help_page),
            ("static assets (no auth)", test_static_assets),
            ("logic_analyzer.html", test_logic_analyzer_page),
        ]),
        ("Read-only API", [
            ("version", test_version),
            ("debug_status", test_debug_status),
            ("ota_status", test_ota_status),
            ("get_credentials", test_get_credentials),
            ("uart_debug", test_uart_debug),
            ("la_status", test_la_status),
            ("la_get_settings", test_la_get_settings),
        ]),
        ("Settings write (safe values)", [
            ("set_credentials (same value)", test_set_credentials_same_value),
            ("sreset_config", test_sreset_config),
            ("portd_freq", test_portd_freq),
        ]),
        ("Error handling", [
            ("set_credentials (invalid JSON)", test_set_credentials_invalid),
            ("wrong method 405", test_wrong_method_405),
            ("unknown 404", test_unknown_404),
        ]),
        ("Authentication", [
            ("API requires auth", test_api_requires_auth),
            ("set_credentials (no auth)", test_set_credentials_no_auth),
            ("wrong credentials", test_wrong_credentials),
        ]),
        ("Public utility endpoints", [
            ("log_error", test_log_error),
            ("test/status", test_test_status),
            ("test/result", test_test_result),
        ]),
    ]

    passed = 0
    failed = 0
    for group_name, tests in groups:
        print(f"\n== {group_name} ==")
        for name, fn in tests:
            try:
                fn(args)
                passed += 1
            except Exception as e:
                print(f"[FAIL] {name}: {e}", file=sys.stderr)
                failed += 1

    print(f"\n{'='*40}\nResults: {passed} passed, {failed} failed")
    return 0 if failed == 0 else 1


if __name__ == "__main__":
    sys.exit(main())
