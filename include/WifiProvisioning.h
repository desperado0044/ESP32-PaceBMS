#pragma once
#include <Arduino.h>

// Non-blocking WiFi connect + captive-portal provisioning. Everything here must never block for
// more than a few milliseconds per loop() call - the display/touch loop runs on the same core and
// would otherwise freeze for as long as WiFi is connecting or the setup portal is open.
namespace WifiManager {

// Kicks off connecting with saved credentials (if any) or starts the non-blocking captive portal
// immediately (if none saved yet). Returns right away either way.
void begin();

// Call every loop() tick. Services the saved-credentials connect attempt / the captive portal's
// DNS+web server (WiFiManager::process()) / a silent background reconnect once previously
// connected - never blocks, never re-opens the portal after a first successful connection.
void loop();

bool isConnected();

// Short status string for the display's top bar: "IP 192.168.1.42" once connected, "Setup
// 10.0.0.1" while the captive portal is open, "Verbinde..." while trying saved credentials.
String statusText();

}  // namespace WifiManager
