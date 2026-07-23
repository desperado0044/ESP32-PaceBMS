#pragma once
#include <Arduino.h>
#include <Preferences.h>

// Persists WiFi + MQTT broker credentials in NVS so they survive restarts/reflashing. WiFi and
// MQTT are saved independently (saveWifi()/saveMqtt()) so changing one via the running web UI's
// Konfiguration tab never requires re-entering the other.
class CredentialsManager {
public:
    static CredentialsManager& instance();
    bool begin();

    String getWifiSsid();
    String getWifiPass();
    // Fallback WiFi, only ever used once already running normally and the primary network drops
    // (see WifiProvisioning.cpp) - never tried at boot, and never preferred over the primary once
    // it's reachable again.
    String getWifiSsid2();
    String getWifiPass2();
    String getMqttHost();
    int getMqttPort();
    String getMqttUser();
    String getMqttPass();
    // Single device identity, serving three purposes at once: mDNS name (reachable as
    // "<hostname>.local"), OTA HTTP Basic Auth username, and MQTT base topic prefix. One setting
    // instead of three separate ones so multiple devices on the same network/broker only need one
    // value changed per device to avoid colliding with each other.
    String getHostname();

    void saveWifi(const String& ssid, const String& pass);
    void saveWifi2(const String& ssid, const String& pass);
    void saveMqtt(const String& host, int port, const String& user, const String& pass);
    // Only letters/digits/hyphen/underscore, 1-32 chars, are accepted (this name doubles as an
    // mDNS hostname, OTA Basic-Auth username, and MQTT topic prefix - keeping to that common-
    // denominator character set avoids surprises in any of those three contexts). Returns false
    // and leaves the stored value unchanged if hostname fails validation.
    bool saveHostname(const String& hostname);

private:
    CredentialsManager() = default;
    Preferences prefs;
};
