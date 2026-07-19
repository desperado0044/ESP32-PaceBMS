#include "CredentialsStorage.h"
#include "Config.h"

CredentialsManager& CredentialsManager::instance() {
    static CredentialsManager manager;
    return manager;
}

bool CredentialsManager::begin() { return prefs.begin(CREDENTIALS_NVS_NAMESPACE, false); }

String CredentialsManager::getWifiSsid() { return prefs.getString("wifi_ssid", ""); }

String CredentialsManager::getWifiPass() { return prefs.getString("wifi_pass", ""); }

String CredentialsManager::getMqttHost() { return prefs.getString("mqtt_host", MQTT_DEFAULT_HOST); }

int CredentialsManager::getMqttPort() { return prefs.getInt("mqtt_port", MQTT_DEFAULT_PORT); }

String CredentialsManager::getMqttUser() { return prefs.getString("mqtt_user", MQTT_DEFAULT_USER); }

String CredentialsManager::getMqttPass() { return prefs.getString("mqtt_pass", MQTT_DEFAULT_PASS); }

void CredentialsManager::saveWifi(const String& ssid, const String& pass) {
    prefs.putString("wifi_ssid", ssid);
    prefs.putString("wifi_pass", pass);
}

void CredentialsManager::saveMqtt(const String& host, int port, const String& user,
                                   const String& pass) {
    prefs.putString("mqtt_host", host);
    prefs.putInt("mqtt_port", port);
    prefs.putString("mqtt_user", user);
    prefs.putString("mqtt_pass", pass);
}
