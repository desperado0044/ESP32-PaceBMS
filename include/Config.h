#pragma once

// Hardware wiring (ESP32 NodeMCU + MAX3232 RS232 transceiver, see README for the full pinout).
// NOT GPIO16/17: those are used by the TFT (see below), reusing the Haustuerklingel-Projekt's
// display test rig. RX34 is input-only (fine for RX-only use), TX32 is a free output-capable pin.
constexpr int BMS_UART_RX_PIN = 34;  // ESP32 RX2 <- MAX3232 R1OUT
constexpr int BMS_UART_TX_PIN = 32;  // ESP32 TX2 -> MAX3232 T1IN
constexpr unsigned long BMS_UART_BAUD = 9600;
constexpr unsigned long BMS_RESPONSE_TIMEOUT_MS = 500;

// ---------------------------------------------------------------------------------------------
// Alternative transport: PACE's Modbus RTU protocol over RS485 (a different physical port on the
// BMS than the RS232 one above - see README). Uses ESP32's third hardware UART (UART1; UART0 is
// Serial/USB-debug, UART2 is the RS232 BMS link) through a MAX485-style transceiver. This module's
// DE and RE pins are tied together to one GPIO (common on cheap breakout boards) - HIGH to
// transmit, LOW to receive, since half-duplex RS485 never does both at once anyway.
constexpr int MODBUS_UART_TX_PIN = 26;
constexpr int MODBUS_UART_RX_PIN = 25;
constexpr int MODBUS_DE_RE_PIN = 13;
constexpr unsigned long MODBUS_UART_BAUD = 9600;
constexpr unsigned long MODBUS_RESPONSE_TIMEOUT_MS = 500;

// ---------------------------------------------------------------------------------------------
// Display (TFT_eSPI, ILI9341, HSPI) + Touch (XPT2046, VSPI) - same physical rig and pin mapping
// as the Haustuerklingel project (see that repo's src/config.h / platformio.ini for the full
// rationale). TFT_MOSI/SCLK/CS/DC/RST are set via platformio.ini build_flags, duplicated here
// only as a comment: TFT_MOSI=4 TFT_SCLK=14 TFT_CS=17 TFT_DC=21 TFT_RST=16.
constexpr int SCREEN_WIDTH = 320;
constexpr int SCREEN_HEIGHT = 240;
constexpr int TFT_ROTATION = 1;  // landscape, panel mounted rotated 180 degrees

constexpr int TFT_BACKLIGHT_PIN = 22;
constexpr int TFT_BACKLIGHT_LEDC_CHANNEL = 0;
constexpr int TFT_BACKLIGHT_LEDC_FREQ_HZ = 5000;
constexpr int TFT_BACKLIGHT_LEDC_RESOLUTION_BITS = 8;
constexpr uint8_t TFT_BACKLIGHT_DEFAULT = 220;

constexpr int TOUCH_CS_PIN = 27;
constexpr int TOUCH_IRQ_PIN = 33;

// Fallback raw ADC calibration range, valid until the 4-corner routine below replaces it via NVS.
constexpr uint16_t TOUCH_DEFAULT_MINX = 300;
constexpr uint16_t TOUCH_DEFAULT_MAXX = 3800;
constexpr uint16_t TOUCH_DEFAULT_MINY = 300;
constexpr uint16_t TOUCH_DEFAULT_MAXY = 3800;

// Local calibration trigger: holding a single continuous touch anywhere on the screen for this
// long starts the 4-corner routine, independent of WiFi/MQTT. A held-touch duration was chosen
// over e.g. "N taps within a time window" because normal swipe/tab-tap use was hitting a tap-
// counting trigger by accident.
constexpr unsigned long CALIBRATION_LOCAL_TRIGGER_HOLD_MS = 10000UL;

constexpr const char* CALIBRATION_NVS_NAMESPACE = "pacebms_calib";

// Factory reset: holding the ESP32 board's BOOT/FLASH button (GPIO0, active low) this long clears
// saved WiFi/MQTT credentials and touch calibration and reboots into the captive portal. Uses a
// physical button rather than the touchscreen deliberately - it must still work even if the
// screen isn't calibrated/usable, and it must not collide with the touch-hold calibration gesture.
constexpr int FACTORY_RESET_BUTTON_PIN = 0;
constexpr unsigned long FACTORY_RESET_HOLD_MS = 8000UL;

// How often the BMS is polled for a full data set (version+serial once, then
// analog/capacity/warning data every cycle) - matches the upstream default.
constexpr unsigned long BMS_POLL_INTERVAL_MS = 5000;

// After this many consecutive failed poll cycles (~this many * BMS_POLL_INTERVAL_MS), all known
// pack slots are zeroed out instead of continuing to show the last (now stale) readings forever.
// Debounced rather than triggered on the first failure, so a single missed/corrupted frame
// doesn't flash every value to 0.
constexpr int BMS_ZERO_AFTER_CONSECUTIVE_FAILURES = 3;

// Dev/demo aid: when true, NetworkTask feeds a made-up (but slowly drifting, "live"-looking)
// single-pack snapshot instead of actually polling the BMS UART - lets the display/web UI be
// previewed and iterated on without real BMS hardware attached. This is only the seed default for
// RuntimeSettings::simulateBmsData() - once toggled via the System tab (display or web), the NVS
// value takes over and this constant is no longer consulted.
constexpr bool SIMULATE_BMS_DATA = false;
constexpr const char* RUNTIME_SETTINGS_NVS_NAMESPACE = "pacebms_rtset";

constexpr const char* MQTT_BASE_TOPIC = "pacebms";
constexpr const char* MQTT_HA_DISCOVERY_TOPIC = "homeassistant";
constexpr const char* MQTT_CLIENT_ID = "esp32-pacebms";
constexpr bool MQTT_HA_DISCOVERY_ENABLED = true;
// Only used until the Konfiguration tab / captive portal saves a real broker address.
constexpr const char* MQTT_DEFAULT_HOST = "";
constexpr int MQTT_DEFAULT_PORT = 1883;
constexpr const char* MQTT_DEFAULT_USER = "";
constexpr const char* MQTT_DEFAULT_PASS = "";

constexpr uint16_t WEB_SERVER_PORT = 80;

// OTA firmware updates via ElegantOTA on the existing web server, under /update, protected with
// HTTP Basic Auth. Change OTA_PASSWORD before a real deployment - this is a compiled-in default,
// same as the Haustuerklingel project's OTA setup.
constexpr const char* OTA_HOSTNAME = "PaceBMS";
constexpr const char* OTA_PASSWORD = "1234567890";

// ---------------------------------------------------------------------------------------------
// WiFi provisioning (WiFiManager). No credentials are compiled in - on first boot (or whenever
// no working WiFi is saved) the device opens a captive portal AP at a fixed IP, where WiFi *and*
// MQTT are entered once; both can later be changed independently, without WiFi, via the running
// web UI's Konfiguration tab (see WebUiServer.cpp).
constexpr const char* WIFI_AP_NAME = "PaceBMS-Setup";
constexpr const char* WIFI_AP_PASSWORD = "";  // open AP; set a password here to require one
constexpr uint8_t WIFI_AP_IP_OCTETS[4] = {10, 0, 0, 1};
constexpr unsigned long WIFI_CONNECT_TIMEOUT_MS = 15000UL;
constexpr unsigned long WIFI_RECONNECT_CHECK_MS = 5000UL;
constexpr const char* CREDENTIALS_NVS_NAMESPACE = "pacebms_cred";
