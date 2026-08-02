#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <stdarg.h>
#include <unistd.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_console.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

#include "storage.h"
#include "port_cfg.h"
#include "descriptors.h"
#include "types.h"
#include "esp32jtag_common.h"

static const char *TAG = "CONSOLE";

/* ---------------------------- log mute control --------------------------- */
/* Swapping the esp_log vprintf hook silences every ESP_LOGx call without
 * touching the console's own printf/prompt output (which goes through the
 * stdout VFS). This keeps the console readable while boot/status logs are
 * interleaved on the same USB Serial JTAG port. */

static vprintf_like_t g_saved_vprintf = NULL;
static bool g_log_muted = false;

static int muted_vprintf(const char *format, va_list args)
{
    (void)format;
    (void)args;
    return 0;
}

static void set_log_muted(bool mute)
{
    if (mute && !g_log_muted) {
        g_saved_vprintf = esp_log_set_vprintf(muted_vprintf);
        g_log_muted = true;
        printf("  Log output disabled.\n");
    } else if (!mute && g_log_muted) {
        esp_log_set_vprintf(g_saved_vprintf ? g_saved_vprintf : &vprintf);
        g_log_muted = false;
        printf("  Log output enabled.\n");
    } else {
        printf("  Log output already %s.\n", g_log_muted ? "disabled" : "enabled");
    }
}

/* ----------------------------- storage helpers --------------------------- */

static const char *read_storage_into(char *buf, size_t size, const char *key, const char *def)
{
    char *val = NULL;
    if (storage_alloc_and_read(key, &val) == ESP_OK && val) {
        snprintf(buf, size, "%s", val);
        free(val);
        return buf;
    }
    return def;
}

static const char *read_storage(const char *key, const char *def)
{
    static char buf[128];
    return read_storage_into(buf, sizeof(buf), key, def);
}

static void write_storage(const char *key, const char *value)
{
    storage_handle_t h;
    if (storage_open_session(&h) == ESP_OK) {
        storage_write_session(h, key, value, strlen(value));
        storage_close_session(h);
    }
}

/* ------------------------------ settings set ---------------------------- */

static void set_port_a(uint8_t v)
{
    if (v == 1) {
        printf("  Value 1 not supported, forcing to 0 (Logic Analyzer).\n");
        v = 0;
    }
    if (v == gbl_pa_cfg) { printf("  Port A already %d (%s).\n", v, get_port_a_description_int(v)); return; }
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", v);
    write_storage(PORT_A_CFG_KEY, buf);
    gbl_pa_cfg = v;
    printf("  Port A set to %d (%s). Reboot required.\n", v, get_port_a_description_int(v));
}

static void set_port_b(uint8_t v)
{
    if (v == gbl_pb_cfg) { printf("  Port B already %d (%s).\n", v, get_port_b_description_int(v)); return; }
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", v);
    write_storage(PORT_B_CFG_KEY, buf);
    gbl_pb_cfg = v;
    printf("  Port B set to %d (%s). Reboot required.\n", v, get_port_b_description_int(v));
}

static void set_port_c(uint8_t v)
{
    if (v == gbl_pc_cfg) { printf("  Port C already %d (%s).\n", v, get_port_c_description_int(v)); return; }
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", v);
    write_storage(PORT_C_CFG_KEY, buf);
    gbl_pc_cfg = v;
    printf("  Port C set to %d (%s). Reboot required.\n", v, get_port_c_description_int(v));
}

static void set_port_d(uint8_t v)
{
    if (v == gbl_pd_cfg) { printf("  Port D already %d (%s).\n", v, get_port_d_description_int(v)); return; }
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", v);
    write_storage(PORT_D_CFG_KEY, buf);
    gbl_pd_cfg = v;
    printf("  Port D set to %d (%s). Reboot required.\n", v, get_port_d_description_int(v));
}

static void set_target_voltage(uint8_t v)
{
    if (v == gbl_vio_idx) { printf("  Target voltage already %d (%s).\n", v,
                                   get_target_voltage_description((const char[]){'0' + v, 0})); return; }
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", v);
    write_storage(TARGET_VOLTAGE_KEY, buf);
    gbl_vio_idx = v;
    printf("  Target voltage set to %d (%s). Reboot required.\n", v,
           get_target_voltage_description(buf));
}

static void set_wifi_mode(const char *mode)
{
    const char *cur = read_storage(WIFI_MODE_KEY, "AP");
    char m[4];
    if (strcasecmp(mode, "AP") == 0)      strcpy(m, "AP");
    else if (strcasecmp(mode, "SM") == 0) strcpy(m, "SM");
    else { printf("  Invalid mode '%s'. Use AP or SM.\n", mode); return; }
    if (strcmp(m, cur) == 0) { printf("  WiFi mode already %s.\n", m); return; }
    write_storage(WIFI_MODE_KEY, m);
    printf("  WiFi mode set to %s. Reboot required.\n", m);
}

static void set_wifi_ssid(const char *ssid)
{
    const char *mode = read_storage(WIFI_MODE_KEY, "AP");
    const char *key = (strcmp(mode, "AP") == 0) ? WIFI_AP_SSID_KEY : WIFI_SSID_KEY;
    write_storage(key, ssid);
    printf("  SSID '%s' saved (%s mode, key %s). Reboot required.\n", ssid, mode, key);
}

static void set_wifi_pass(const char *pass)
{
    const char *mode = read_storage(WIFI_MODE_KEY, "AP");
    const char *key = (strcmp(mode, "AP") == 0) ? WIFI_AP_PASS_KEY : WIFI_PASS_KEY;
    write_storage(key, pass);
    printf("  Password saved (%s mode, key %s). Reboot required.\n", mode, key);
}

static void set_ota_url(const char *url)
{
    write_storage(OTA_URL_KEY, url);
    printf("  OTA URL set to '%s'.\n", url);
}

static void set_web_user(const char *user)
{
    write_storage(WEB_USER_KEY, user);
    printf("  Web username set to '%s'.\n", user);
}

static void set_web_pass(const char *pass)
{
    write_storage(WEB_PASS_KEY, pass);
    printf("  Web password set.\n");
}

static void set_uart_baud(uint32_t v)
{
    char buf[16];
    snprintf(buf, sizeof(buf), "%lu", (unsigned long)v);
    if (strcmp(buf, read_storage(UART_BAUD_KEY, "115200")) == 0) { printf("  UART baud already %lu.\n", (unsigned long)v); return; }
    write_storage(UART_BAUD_KEY, buf);
    printf("  UART baud set to %lu. Reboot required.\n", (unsigned long)v);
}

static void set_uart_dbits(uint8_t v)
{
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", v);
    if (strcmp(buf, read_storage(UART_DATA_BITS_KEY, "8")) == 0) { printf("  UART data bits already %d.\n", v); return; }
    write_storage(UART_DATA_BITS_KEY, buf);
    printf("  UART data bits set to %d. Reboot required.\n", v);
}

static void set_uart_sbits(const char *v)
{
    if (strcmp(v, read_storage(UART_STOP_BITS_KEY, "1")) == 0) { printf("  UART stop bits already %s.\n", v); return; }
    write_storage(UART_STOP_BITS_KEY, v);
    printf("  UART stop bits set to %s. Reboot required.\n", v);
}

static void set_uart_parity(const char *v)
{
    if (strcmp(v, read_storage(UART_PARITY_KEY, "n")) == 0) { printf("  UART parity already %s.\n", v); return; }
    write_storage(UART_PARITY_KEY, v);
    printf("  UART parity set to %s. Reboot required.\n", v);
}

static void set_uart_psel(uint8_t v)
{
    char buf[4];
    snprintf(buf, sizeof(buf), "%d", v);
    if (strcmp(buf, read_storage(UART_PORT_SEL_KEY, "1")) == 0) { printf("  UART port select already %d (%s).\n", v, v ? "Web" : "USB"); return; }
    write_storage(UART_PORT_SEL_KEY, buf);
    printf("  UART port select set to %d (%s). Reboot required.\n", v, v ? "Web" : "USB");
}

static void set_usb_dap(uint8_t disabled)
{
    const char *val = disabled ? "1" : "0";
    if (strcmp(val, read_storage(DISABLE_USB_DAP_KEY, "1")) == 0) { printf("  USB CMSIS-DAP already %s.\n", disabled ? "disabled" : "enabled"); return; }
    write_storage(DISABLE_USB_DAP_KEY, val);
    printf("  USB CMSIS-DAP %s. Reboot required.\n", disabled ? "disabled" : "enabled");
}

/* ------------------------------- actions --------------------------------- */

static void action_reset_target(void)
{
    if (gbl_pb_cfg != PB_UART_SRESET_VTARGET) {
        printf("  Error: Port B must be configured as Vtarget+UART+SReset.\n");
        return;
    }
    set_sreset(true);
    vTaskDelay(pdMS_TO_TICKS(gbl_sreset_pulse_ms));
    set_sreset(false);
    printf("  Reset pulse sent (%lu ms, %s)\n",
           (unsigned long)gbl_sreset_pulse_ms,
           gbl_sreset_polarity ? "active LOW" : "active HIGH");
}

static void action_sreset_config(uint8_t polarity, uint32_t pulse_ms)
{
    gbl_sreset_polarity = polarity;
    gbl_sreset_pulse_ms = pulse_ms;
    set_sreset(false);
    printf("  SRESET: polarity=%u pulse_ms=%lu\n", gbl_sreset_polarity,
           (unsigned long)gbl_sreset_pulse_ms);
}

static void action_portd_output(uint8_t mode, uint8_t value)
{
    if (gbl_pd_cfg != PD_LOGICANALYZER) {
        printf("  Error: Port D must be configured as Logic Analyzer (not XVC).\n");
        return;
    }
    set_portd_output(mode, value);
    static const uint8_t mode_to_display[] = {0, 2, 3, 4};
    gbl_pd_display_cfg = (mode < 4) ? mode_to_display[mode] : 0;
    printf("  Port D output: mode=%d value=%d\n", mode, value);
}

static void action_portd_freq(uint32_t freq)
{
    if (gbl_pd_cfg != PD_LOGICANALYZER) {
        printf("  Error: Port D must be configured as Logic Analyzer (not XVC).\n");
        return;
    }
    set_portd_freq(freq);
    printf("  Port D freq: %lu Hz\n", (unsigned long)freq);
}

static void action_ota_status(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    printf("  Running: %s\n", running ? running->label : "unknown");
    if (running) {
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(running, &desc) == ESP_OK) {
            printf("    version: %s (%s %s)\n", desc.version, desc.date, desc.time);
        }
    }
    printf("  Alternate: %s\n", next ? next->label : "(none)");
    if (next) {
        esp_app_desc_t desc;
        if (esp_ota_get_partition_description(next, &desc) == ESP_OK) {
            printf("    version: %s (%s %s)\n", desc.version, desc.date, desc.time);
        } else {
            printf("    version: (empty)\n");
        }
    }
}

static void action_switch_ota(void)
{
    const esp_partition_t *running = esp_ota_get_running_partition();
    const esp_partition_t *next = esp_ota_get_next_update_partition(NULL);
    if (!running || !next) {
        printf("  Cannot get OTA partitions.\n");
        return;
    }
    if (running == next) {
        printf("  No alternate OTA partition available.\n");
        return;
    }
    printf("  Switching OTA partition: %s -> %s. Rebooting...\n",
           running->label, next->label);
    esp_err_t err = esp_ota_set_boot_partition(next);
    if (err != ESP_OK) {
        printf("  Failed to set boot partition.\n");
        return;
    }
    vTaskDelay(2000 / portTICK_PERIOD_MS);
    esp_restart();
}

static void action_factory_reset(void)
{
    printf("  Erasing all settings...\n");
    storage_erase_key(PORT_A_CFG_KEY);
    storage_erase_key(PORT_B_CFG_KEY);
    storage_erase_key(PORT_C_CFG_KEY);
    storage_erase_key(PORT_D_CFG_KEY);
    storage_erase_key(TARGET_VOLTAGE_KEY);
    storage_erase_key(SW_MCU_KEY);
    storage_erase_key(WIFI_MODE_KEY);
    storage_erase_key(WIFI_SSID_KEY);
    storage_erase_key(WIFI_PASS_KEY);
    storage_erase_key(WIFI_AP_SSID_KEY);
    storage_erase_key(WIFI_AP_PASS_KEY);
    storage_erase_key(OTA_URL_KEY);
    storage_erase_key(MCU_INTERFACE_KEY);
    storage_erase_key(WEB_USER_KEY);
    storage_erase_key(WEB_PASS_KEY);
    storage_erase_key(UART_BAUD_KEY);
    storage_erase_key(UART_DATA_BITS_KEY);
    storage_erase_key(UART_STOP_BITS_KEY);
    storage_erase_key(UART_PARITY_KEY);
    storage_erase_key(UART_PORT_SEL_KEY);
    storage_erase_key(DISABLE_USB_DAP_KEY);
    printf("  Factory reset complete. Rebooting...\n");
    vTaskDelay(3000 / portTICK_PERIOD_MS);
    esp_restart();
}

/* ------------------------------ display ---------------------------------- */

static void show_all_settings(void)
{
    char buf[4];
    printf("\n--- Current Settings ---\n");
    printf("  Port A      : %d (%s)\n", gbl_pa_cfg, get_port_a_description_int(gbl_pa_cfg));
    printf("  Port B      : %d (%s)\n", gbl_pb_cfg, get_port_b_description_int(gbl_pb_cfg));
    printf("  Port C      : %d (%s)\n", gbl_pc_cfg, get_port_c_description_int(gbl_pc_cfg));
    printf("  Port D      : %d (%s)\n", gbl_pd_cfg, get_port_d_description_int(gbl_pd_cfg));
    snprintf(buf, sizeof(buf), "%d", gbl_vio_idx);
    printf("  Target IO V : %d (%s)\n", gbl_vio_idx, get_target_voltage_description(buf));
    printf("  SW MCU      : %s\n", read_storage(SW_MCU_KEY, "0"));
    const char *mode = read_storage(WIFI_MODE_KEY, "AP");
    printf("  WiFi mode   : %s (%s)\n", mode, get_wifi_mode_description(mode));
    printf("  WiFi SSID   : %s\n", read_storage(WIFI_SSID_KEY, "(none)"));
    printf("  WiFi pass   : %s\n", (storage_is_key_exist(WIFI_PASS_KEY)) ? "********" : "(none)");
    printf("  AP SSID     : %s\n", read_storage(WIFI_AP_SSID_KEY, "(none)"));
    printf("  AP pass     : %s\n", (storage_is_key_exist(WIFI_AP_PASS_KEY)) ? "********" : "(none)");
    printf("  OTA URL     : %s\n", read_storage(OTA_URL_KEY, "(none)"));
    printf("  Web user    : %s\n", read_storage(WEB_USER_KEY, "admin"));
    printf("  Web pass    : %s\n", (storage_is_key_exist(WEB_PASS_KEY)) ? "********" : "(none)");
    printf("  UART baud   : %s\n", read_storage(UART_BAUD_KEY, "115200"));
    printf("  UART dbits  : %s\n", read_storage(UART_DATA_BITS_KEY, "8"));
    printf("  UART sbits  : %s\n", read_storage(UART_STOP_BITS_KEY, "1"));
    printf("  UART parity : %s\n", read_storage(UART_PARITY_KEY, "n"));
    char psel_buf[8];
    const char *psel = read_storage_into(psel_buf, sizeof(psel_buf), UART_PORT_SEL_KEY, "1");
    printf("  UART port   : %s (%s)\n", psel, (strcmp(psel, "0") == 0) ? "USB" : "Web");
    printf("  USB DAP     : %s\n", (strcmp(read_storage(DISABLE_USB_DAP_KEY, "1"), "1") == 0) ? "disabled" : "enabled");
    printf("  SRESET      : polarity=%u pulse=%lums\n",
           gbl_sreset_polarity, (unsigned long)gbl_sreset_pulse_ms);
    printf("\n--- OTA ---\n");
    action_ota_status();
    printf("--------------------------\n");
}

/* ----------------------------- arg helpers ------------------------------- */

static int arg_int(const char *s, int min, int max, int *out)
{
    char *end = NULL;
    long v = strtol(s, &end, 10);
    if (end == s || *end != '\0' || v < min || v > max) return -1;
    *out = (int)v;
    return 0;
}

/* Parse a target IO voltage string ("3.3", "2.5v", "1.8V", ...) into the
 * stored index (0-4). Only voltage values are accepted. */
static int target_voltage_to_index(const char *s, int *out)
{
    struct { const char *label; int idx; } volts[] = {
        { "3.3", 0 }, { "2.5", 1 }, { "1.8", 2 }, { "1.5", 3 }, { "1.2", 4 },
    };
    char buf[8];
    strncpy(buf, s, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = '\0';
    size_t len = strlen(buf);
    if (len && (buf[len - 1] == 'v' || buf[len - 1] == 'V')) {
        buf[len - 1] = '\0';
    }
    for (size_t i = 0; i < sizeof(volts) / sizeof(volts[0]); i++) {
        if (strcmp(buf, volts[i].label) == 0) {
            *out = volts[i].idx;
            return 0;
        }
    }
    return -1;
}

static const char *target_voltage_index_to_label(uint8_t idx)
{
    switch (idx) {
    case 0: return "3.3V";
    case 1: return "2.5V";
    case 2: return "1.8V";
    case 3: return "1.5V";
    case 4: return "1.2V";
    }
    return "?";
}

static void print_port_a_usage(void)
{
    printf("  Usage: porta <0=Logic Analyzer|2=BMP SWD|3=BMP JTAG>\n");
    printf("  Current: %d (%s)\n", gbl_pa_cfg, get_port_a_description_int(gbl_pa_cfg));
}

static void print_port_b_usage(void)
{
    printf("  Usage: portb <0=Logic Analyzer|1=Vtarget+UART+SReset>\n");
    printf("  Current: %d (%s)\n", gbl_pb_cfg, get_port_b_description_int(gbl_pb_cfg));
}

static void print_port_c_usage(void)
{
    printf("  Usage: portc <0=Logic Analyzer|1=BMP SWD/JTAG|2=FPGA JTAG CFG|3=FPGA SPI CFG>\n");
    printf("  Current: %d (%s)\n", gbl_pc_cfg, get_port_c_description_int(gbl_pc_cfg));
}

static void print_port_d_usage(void)
{
    printf("  Usage: portd <0=Logic Analyzer|1=FPGA XVC|2=FPGA JTAG GPIO|3=FPGA SPI GPIO>\n");
    printf("  Current: %d (%s)\n", gbl_pd_cfg, get_port_d_description_int(gbl_pd_cfg));
}

/* ------------------------------ commands --------------------------------- */

static void show_command_help(const char *name);

#define CMD_HELP_IF_REQUESTED(cmdname) \
    do { \
        if (argc >= 2 && (strcmp(argv[1], "--help") == 0 || strcmp(argv[1], "-h") == 0)) { \
            show_command_help(cmdname); \
            return 0; \
        } \
    } while (0)

static int cmd_porta(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("porta");
    int v;
    if (argc < 2 || arg_int(argv[1], 0, 3, &v) != 0) { print_port_a_usage(); return 0; }
    set_port_a((uint8_t)v);
    return 0;
}

static int cmd_portb(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("portb");
    int v;
    if (argc < 2 || arg_int(argv[1], 0, 1, &v) != 0) { print_port_b_usage(); return 0; }
    set_port_b((uint8_t)v);
    return 0;
}

static int cmd_portc(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("portc");
    int v;
    if (argc < 2 || arg_int(argv[1], 0, 3, &v) != 0) { print_port_c_usage(); return 0; }
    set_port_c((uint8_t)v);
    return 0;
}

static int cmd_portd(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("portd");
    int v;
    if (argc < 2 || arg_int(argv[1], 0, 3, &v) != 0) { print_port_d_usage(); return 0; }
    set_port_d((uint8_t)v);
    return 0;
}

static int cmd_vio(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("vio");
    int v;
    if (argc < 2) {
        printf("  Usage: vio <3.3|2.5|1.8|1.5|1.2>  (optionally 'v' suffix, e.g. 'vio 1.8v')\n");
        printf("  Current: %s\n", target_voltage_index_to_label(gbl_vio_idx));
        return 0;
    }
    if (target_voltage_to_index(argv[1], &v) != 0) {
        printf("  Invalid voltage '%s'. Use 3.3, 2.5, 1.8, 1.5 or 1.2.\n", argv[1]);
        return 0;
    }
    set_target_voltage((uint8_t)v);
    return 0;
}

static int cmd_wifi_mode(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("wifi_mode");
    if (argc < 2) {
        const char *cur = read_storage(WIFI_MODE_KEY, "AP");
        printf("  Usage: wifi_mode <AP|SM>\n");
        printf("  Current: %s (%s)\n", cur, get_wifi_mode_description(cur));
        return 0;
    }
    set_wifi_mode(argv[1]);
    return 0;
}

static int cmd_wifi_ssid(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("wifi_ssid");
    if (argc < 2) {
        char mode_buf[8], val_buf[128];
        const char *mode = read_storage_into(mode_buf, sizeof(mode_buf), WIFI_MODE_KEY, "AP");
        const char *key = (strcmp(mode, "AP") == 0) ? WIFI_AP_SSID_KEY : WIFI_SSID_KEY;
        const char *ssid = read_storage_into(val_buf, sizeof(val_buf), key, "(none)");
        printf("  Usage: wifi_ssid <ssid>\n");
        printf("  Current (%s mode): %s\n", mode, ssid);
        return 0;
    }
    set_wifi_ssid(argv[1]);
    return 0;
}

static int cmd_wifi_pass(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("wifi_pass");
    if (argc < 2) {
        const char *mode = read_storage(WIFI_MODE_KEY, "AP");
        printf("  Usage: wifi_pass <password>\n");
        printf("  Current (%s mode): ********\n", mode);
        return 0;
    }
    set_wifi_pass(argv[1]);
    return 0;
}

static int cmd_ota_url(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("ota_url");
    if (argc < 2) {
        printf("  Usage: ota_url <url>\n");
        printf("  Current: %s\n", read_storage(OTA_URL_KEY, "(none)"));
        return 0;
    }
    set_ota_url(argv[1]);
    return 0;
}

static int cmd_web_user(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("web_user");
    if (argc < 2) {
        printf("  Usage: web_user <username>\n");
        printf("  Current: %s\n", read_storage(WEB_USER_KEY, "admin"));
        return 0;
    }
    set_web_user(argv[1]);
    return 0;
}

static int cmd_web_pass(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("web_pass");
    if (argc < 2) {
        printf("  Usage: web_pass <password>\n");
        printf("  Current: ********\n");
        return 0;
    }
    set_web_pass(argv[1]);
    return 0;
}

static int cmd_uart_baud(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("uart_baud");
    int v;
    if (argc < 2 || arg_int(argv[1], 300, 3000000, &v) != 0) {
        printf("  Usage: uart_baud <300-3000000>\n");
        printf("  Current: %s\n", read_storage(UART_BAUD_KEY, "115200"));
        return 0;
    }
    set_uart_baud((uint32_t)v);
    return 0;
}

static int cmd_uart_dbits(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("uart_dbits");
    int v;
    if (argc < 2 || arg_int(argv[1], 5, 8, &v) != 0) {
        printf("  Usage: uart_dbits <5-8>\n");
        printf("  Current: %s\n", read_storage(UART_DATA_BITS_KEY, "8"));
        return 0;
    }
    set_uart_dbits((uint8_t)v);
    return 0;
}

static int cmd_uart_sbits(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("uart_sbits");
    if (argc < 2) {
        printf("  Usage: uart_sbits <1|1.5|2>\n");
        printf("  Current: %s\n", read_storage(UART_STOP_BITS_KEY, "1"));
        return 0;
    }
    if (strcmp(argv[1], "1") != 0 && strcmp(argv[1], "1.5") != 0 && strcmp(argv[1], "2") != 0) {
        printf("  Invalid stop bits '%s'. Use 1, 1.5 or 2.\n", argv[1]);
        return 0;
    }
    set_uart_sbits(argv[1]);
    return 0;
}

static int cmd_uart_parity(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("uart_parity");
    if (argc < 2) {
        printf("  Usage: uart_parity <n|e|o>\n");
        printf("  Current: %s\n", read_storage(UART_PARITY_KEY, "n"));
        return 0;
    }
    if (strcmp(argv[1], "n") != 0 && strcmp(argv[1], "e") != 0 && strcmp(argv[1], "o") != 0) {
        printf("  Invalid parity '%s'. Use n, e or o.\n", argv[1]);
        return 0;
    }
    set_uart_parity(argv[1]);
    return 0;
}

static int cmd_uart_psel(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("uart_psel");
    int v;
    if (argc < 2 || arg_int(argv[1], 0, 1, &v) != 0) {
        char psel_buf[8];
        const char *cur = read_storage_into(psel_buf, sizeof(psel_buf), UART_PORT_SEL_KEY, "1");
        printf("  Usage: uart_psel <0=USB|1=Web>\n");
        printf("  Current: %s (%s)\n", cur, (strcmp(cur, "0") == 0) ? "USB" : "Web");
        return 0;
    }
    set_uart_psel((uint8_t)v);
    return 0;
}

static int cmd_usb_dap(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("usb_dap");
    int v;
    if (argc < 2 || arg_int(argv[1], 0, 1, &v) != 0) {
        printf("  Usage: usb_dap <0=enabled|1=disabled>\n");
        printf("  Current: %s\n", (strcmp(read_storage(DISABLE_USB_DAP_KEY, "1"), "1") == 0) ? "disabled" : "enabled");
        return 0;
    }
    set_usb_dap((uint8_t)v);
    return 0;
}

static int cmd_sreset(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("sreset");
    if (argc < 2) {
        printf("  Usage: sreset <polarity 0=HIGH|1=LOW> [pulse_ms 1-5000]\n");
        printf("  Current: polarity=%u pulse=%lums\n",
               gbl_sreset_polarity, (unsigned long)gbl_sreset_pulse_ms);
        return 0;
    }
    int pol, ms = (int)gbl_sreset_pulse_ms;
    if (arg_int(argv[1], 0, 1, &pol) != 0) {
        printf("  Invalid polarity. Use 0 or 1.\n");
        return 0;
    }
    if (argc >= 3 && arg_int(argv[2], 1, 5000, &ms) != 0) {
        printf("  Invalid pulse width. Use 1-5000 ms.\n");
        return 0;
    }
    action_sreset_config((uint8_t)pol, (uint32_t)ms);
    return 0;
}

static int cmd_portd_output(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("portd_output");
    int mode, value;
    if (argc < 3 || arg_int(argv[1], 0, 3, &mode) != 0 || arg_int(argv[2], 0, 15, &value) != 0) {
        printf("  Usage: portd_output <mode> <value 0-15>\n"
               "  Modes: 0 = tristate (LA input, default)\n"
               "         1 = counter_lo (drive 132 MHz counter bits [3:0])\n"
               "         2 = counter_hi (drive 132 MHz counter bits [7:4])\n"
               "         3 = gpio (drive pins with <value> 0-15)\n"
               "  Requires Port D in Logic Analyzer mode.\n");
        return 0;
    }
    action_portd_output((uint8_t)mode, (uint8_t)value);
    return 0;
}

static int cmd_portd_freq(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("portd_freq");
    int freq;
    if (argc < 2 || arg_int(argv[1], 0, 1000, &freq) != 0) {
        printf("  Usage: portd_freq <0|125|250|500|1000>\n");
        return 0;
    }
    if (freq != 0 && freq != 125 && freq != 250 && freq != 500 && freq != 1000) {
        printf("  Invalid frequency. Use 0, 125, 250, 500 or 1000.\n");
        return 0;
    }
    action_portd_freq((uint32_t)freq);
    return 0;
}

static int cmd_ota_switch(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("ota_switch");
    action_switch_ota();
    return 0;
}

static int cmd_reset_target(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("reset_target");
    action_reset_target();
    return 0;
}

static int cmd_show(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("show");
    show_all_settings();
    return 0;
}

static int cmd_reboot(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("reboot");
    printf("Rebooting...\n");
    vTaskDelay(1000 / portTICK_PERIOD_MS);
    esp_restart();
    return 0;
}

static int cmd_factory_reset(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("factory_reset");
    if (argc < 2 || (strcmp(argv[1], "yes") != 0 && strcmp(argv[1], "y") != 0)) {
        printf("  Danger: erases all settings and reboots.\n");
        printf("  Type 'factory_reset yes' to confirm.\n");
        return 0;
    }
    action_factory_reset();
    return 0;
}

static int cmd_log(int argc, char **argv)
{
    CMD_HELP_IF_REQUESTED("log");
    if (argc > 1) {
        if (strcmp(argv[1], "on") == 0)      set_log_muted(false);
        else if (strcmp(argv[1], "off") == 0) set_log_muted(true);
        else printf("  Usage: log [on|off]\n");
    } else {
        set_log_muted(!g_log_muted);
    }
    return 0;
}

/* ------------------------------ init ------------------------------------ */

typedef enum {
    CMD_GROUP_INFO,
    CMD_GROUP_CONFIG,
    CMD_GROUP_ACTIONS,
} cmd_group_t;

typedef struct {
    cmd_group_t group;
    const char *name;
    const char *desc;
    const char *usage;
    esp_console_cmd_func_t func;
} console_cmd_entry_t;

static const console_cmd_entry_t s_cmd_entries[] = {
    /* ---- Info ---- */
    { CMD_GROUP_INFO, "show", "Show all current settings (incl. OTA status)", NULL, cmd_show },
    { CMD_GROUP_INFO, "log", "Toggle log output on/off (or: log on | log off)", "log [on|off]", cmd_log },

    /* ---- Configuration ---- */
    { CMD_GROUP_CONFIG, "porta", "Set Port A config (0=LA, 2=BMP SWD, 3=BMP JTAG); no args = show", "porta <0|2|3>", cmd_porta },
    { CMD_GROUP_CONFIG, "portb", "Set Port B config (0=LA, 1=Vtarget+UART+SReset); no args = show", "portb <0|1>", cmd_portb },
    { CMD_GROUP_CONFIG, "portc", "Set Port C config (0=LA, 1=BMP SWD/JTAG, 2=FPGA JTAG CFG, 3=FPGA SPI CFG); no args = show", "portc <0-3>", cmd_portc },
    { CMD_GROUP_CONFIG, "portd", "Set Port D config (0=LA, 1=XVC, 2=FPGA JTAG GPIO, 3=FPGA SPI GPIO); no args = show", "portd <0-3>", cmd_portd },
    { CMD_GROUP_CONFIG, "portd_output", "Set Port D output: 0=tristate, 1=counter_lo, 2=counter_hi, 3=gpio", "portd_output <mode> <value 0-15>", cmd_portd_output },
    { CMD_GROUP_CONFIG, "portd_freq", "Set Port D frequency (0|125|250|500|1000)", "portd_freq <0|125|250|500|1000>", cmd_portd_freq },
    { CMD_GROUP_CONFIG, "vio", "Set target IO voltage (3.3/2.5/1.8/1.5/1.2 V); no args = show", "vio <3.3|2.5|1.8|1.5|1.2>", cmd_vio },
    { CMD_GROUP_CONFIG, "uart_baud", "Set UART baud rate (300-3000000); no args = show", "uart_baud <300-3000000>", cmd_uart_baud },
    { CMD_GROUP_CONFIG, "uart_dbits", "Set UART data bits (5-8); no args = show", "uart_dbits <5-8>", cmd_uart_dbits },
    { CMD_GROUP_CONFIG, "uart_sbits", "Set UART stop bits (1|1.5|2); no args = show", "uart_sbits <1|1.5|2>", cmd_uart_sbits },
    { CMD_GROUP_CONFIG, "uart_parity", "Set UART parity (n|e|o); no args = show", "uart_parity <n|e|o>", cmd_uart_parity },
    { CMD_GROUP_CONFIG, "uart_psel", "Set UART port select (0=USB, 1=Web); no args = show", "uart_psel <0|1>", cmd_uart_psel },
    { CMD_GROUP_CONFIG, "usb_dap", "Set USB CMSIS-DAP (0=enabled, 1=disabled); no args = show", "usb_dap <0|1>", cmd_usb_dap },
    { CMD_GROUP_CONFIG, "sreset", "Set SRESET polarity [pulse_ms]; no args = show", "sreset <0|1> [pulse_ms 1-5000]", cmd_sreset },

    /* ---- Device configuration (network / credentials) ---- */
    { CMD_GROUP_CONFIG, "wifi_mode", "Set WiFi mode (AP|SM); no args = show", "wifi_mode <AP|SM>", cmd_wifi_mode },
    { CMD_GROUP_CONFIG, "wifi_ssid", "Set WiFi SSID; no args = show", "wifi_ssid <ssid>", cmd_wifi_ssid },
    { CMD_GROUP_CONFIG, "wifi_pass", "Set WiFi password; no args = show", "wifi_pass <password>", cmd_wifi_pass },
    { CMD_GROUP_CONFIG, "ota_url", "Set OTA URL; no args = show", "ota_url <url>", cmd_ota_url },
    { CMD_GROUP_CONFIG, "web_user", "Set web username; no args = show", "web_user <username>", cmd_web_user },
    { CMD_GROUP_CONFIG, "web_pass", "Set web password; no args = show", "web_pass <password>", cmd_web_pass },

    /* ---- Actions: reset / reboot ---- */
    { CMD_GROUP_ACTIONS, "reset_target", "Send an SRESET pulse to the target", NULL, cmd_reset_target },
    { CMD_GROUP_ACTIONS, "ota_switch", "Switch OTA partition and reboot", NULL, cmd_ota_switch },
    { CMD_GROUP_ACTIONS, "factory_reset", "Erase all settings and reboot (add 'yes' to confirm)", "factory_reset yes", cmd_factory_reset },
    { CMD_GROUP_ACTIONS, "reboot", "Reboot the device", NULL, cmd_reboot },
};

static const char *cmd_group_title(cmd_group_t group)
{
    switch (group) {
    case CMD_GROUP_INFO:
        return "Info";
    case CMD_GROUP_CONFIG:
        return "Configuration";
    case CMD_GROUP_ACTIONS:
        return "Actions (reset / reboot)";
    }
    return "";
}

static void show_command_help(const char *name)
{
    for (size_t i = 0; i < sizeof(s_cmd_entries) / sizeof(s_cmd_entries[0]); i++) {
        if (strcmp(s_cmd_entries[i].name, name) == 0) {
            const console_cmd_entry_t *e = &s_cmd_entries[i];
            printf("\n%s: %s\n", e->name, e->desc);
            if (e->usage) {
                printf("  Usage: %s\n", e->usage);
            } else {
                printf("  Usage: %s\n", e->name);
            }
            printf("  Group: %s\n", cmd_group_title(e->group));
            if (e->group == CMD_GROUP_CONFIG) {
                printf("  Try 'help %s' for this summary; run with no args to show current value.\n", e->name);
            } else {
                printf("  Try 'help %s' for this summary.\n", e->name);
            }
            return;
        }
    }
    printf("  Unknown command '%s'. Type 'help' for the full list.\n", name);
}

static int cmd_help(int argc, char **argv)
{
    if (argc >= 2) {
        show_command_help(argv[1]);
        return 0;
    }
    cmd_group_t last_group = -1;
    for (size_t i = 0; i < sizeof(s_cmd_entries) / sizeof(s_cmd_entries[0]); i++) {
        if (s_cmd_entries[i].group != last_group) {
            printf("\n%s:\n", cmd_group_title(s_cmd_entries[i].group));
            last_group = s_cmd_entries[i].group;
        }
        printf("  %-16s %s\n", s_cmd_entries[i].name, s_cmd_entries[i].desc);
    }
    printf("\n  Use 'help <command>' or '<command> --help' for details on a specific command.\n");
    return 0;
}

void console_menu_init(void)
{
    esp_console_repl_config_t repl_config = ESP_CONSOLE_REPL_CONFIG_DEFAULT();
    repl_config.max_cmdline_length = 256;
    repl_config.prompt = "esp32jtag>";

    esp_console_dev_usb_serial_jtag_config_t hw_config =
        ESP_CONSOLE_DEV_USB_SERIAL_JTAG_CONFIG_DEFAULT();

    esp_console_repl_t *repl = NULL;
    esp_err_t ret = esp_console_new_repl_usb_serial_jtag(&hw_config, &repl_config, &repl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to create console REPL: %s", esp_err_to_name(ret));
        return;
    }

    for (size_t i = 0; i < sizeof(s_cmd_entries) / sizeof(s_cmd_entries[0]); i++) {
        esp_console_cmd_t cmd = {
            .command = s_cmd_entries[i].name,
            .help = s_cmd_entries[i].desc,
            .func = s_cmd_entries[i].func,
        };
        esp_console_cmd_register(&cmd);
    }
    esp_console_deregister_help_command();
    esp_console_cmd_register(&(esp_console_cmd_t) {
        .command = "help",
        .help = "Print a one-line summary of all registered commands",
        .func = cmd_help,
    });

    ret = esp_console_start_repl(repl);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to start console REPL: %s", esp_err_to_name(ret));
        return;
    }

    ESP_LOGI(TAG, "Console ready. Type 'help' for the command list.");
}
