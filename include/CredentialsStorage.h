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
    String getMqttHost();
    int getMqttPort();
    String getMqttUser();
    String getMqttPass();

    void saveWifi(const String& ssid, const String& pass);
    void saveMqtt(const String& host, int port, const String& user, const String& pass);

private:
    CredentialsManager() = default;
    Preferences prefs;
};
