#include "CredentialsStorage.h"
#include "Config.h"

CredentialsManager& CredentialsManager::instance() {
    static CredentialsManager manager;
    return manager;
}

bool CredentialsManager::begin() { return prefs.begin(CREDENTIALS_NVS_NAMESPACE, false); }

String CredentialsManager::getWifiSsid() { return prefs.getString("wifi_ssid", ""); }

String CredentialsManager::getWifiPass() { return prefs.getString("wifi_pass", ""); }

String CredentialsManager::getWifiSsid2() { return prefs.getString("wifi_ssid2", ""); }

String CredentialsManager::getWifiPass2() { return prefs.getString("wifi_pass2", ""); }

String CredentialsManager::getMqttHost() { return prefs.getString("mqtt_host", MQTT_DEFAULT_HOST); }

int CredentialsManager::getMqttPort() { return prefs.getInt("mqtt_port", MQTT_DEFAULT_PORT); }

String CredentialsManager::getMqttUser() { return prefs.getString("mqtt_user", MQTT_DEFAULT_USER); }

String CredentialsManager::getMqttPass() { return prefs.getString("mqtt_pass", MQTT_DEFAULT_PASS); }

String CredentialsManager::getHostname() { return prefs.getString("hostname", OTA_HOSTNAME); }

void CredentialsManager::saveWifi(const String& ssid, const String& pass) {
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", pass);
}

void CredentialsManager::saveWifi2(const String& ssid, const String& pass) {
    prefs.putString("wifi_ssid2", ssid);
    prefs.putString("wifi_pass2", pass);
}

void CredentialsManager::saveMqtt(const String& host, int port, const String& user,
                                   const String& pass) {
    prefs.putString("mqtt_host", host);
    prefs.putInt("mqtt_port", port);
    prefs.putString("mqtt_user", user);
    prefs.putString("mqtt_pass", pass);
}

bool CredentialsManager::saveHostname(const String& hostname) {
    if (hostname.length() == 0 || hostname.length() > 32) return false;
    for (size_t i = 0; i < hostname.length(); i++) {
        char c = hostname[i];
        if (!isalnum((unsigned char)c) && c != '-' && c != '_') return false;
    }
    prefs.putString("hostname", hostname);
    return true;
}
