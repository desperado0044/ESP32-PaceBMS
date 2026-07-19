#include "WifiProvisioning.h"
#include <WiFi.h>
#include <WiFiManager.h>
#include "Config.h"
#include "CredentialsStorage.h"

namespace WifiManager {

namespace {

enum class State { ConnectingSaved, PortalActive, Settled };

State state = State::Settled;
unsigned long savedAttemptStartMs = 0;
unsigned long lastReconnectAttemptMs = 0;
bool everConnected = false;

::WiFiManager wm;
// WiFiManagerParameter keeps its own internal copy of the value, but the objects themselves must
// stay alive for as long as wm.process() might still reference them - i.e. for the whole time the
// portal can be open, not just inside startPortal().
WiFiManagerParameter* mqttHostParam = nullptr;
WiFiManagerParameter* mqttPortParam = nullptr;
WiFiManagerParameter* mqttUserParam = nullptr;
WiFiManagerParameter* mqttPassParam = nullptr;

void startPortal() {
    IPAddress apIp(WIFI_AP_IP_OCTETS[0], WIFI_AP_IP_OCTETS[1], WIFI_AP_IP_OCTETS[2],
                    WIFI_AP_IP_OCTETS[3]);
    Serial.printf("WiFi: opening setup portal '%s' at %s\n", WIFI_AP_NAME, apIp.toString().c_str());

    wm.setDebugOutput(false);
    wm.setConfigPortalBlocking(false);  // critical: must not freeze the display/touch loop
    wm.setAPStaticIPConfig(apIp, apIp, IPAddress(255, 255, 255, 0));

    // MQTT is collected on the same first-time portal page as WiFi (neither is guessable in
    // advance) - both can later be changed independently via the Konfiguration tab of the running
    // web UI, without needing to re-open this portal.
    String mqttPortStr = String(CredentialsManager::instance().getMqttPort());
    mqttHostParam = new WiFiManagerParameter("mqtt_host", "MQTT Broker",
                                              CredentialsManager::instance().getMqttHost().c_str(), 64);
    mqttPortParam = new WiFiManagerParameter("mqtt_port", "MQTT Port", mqttPortStr.c_str(), 6);
    mqttUserParam = new WiFiManagerParameter("mqtt_user", "MQTT User",
                                              CredentialsManager::instance().getMqttUser().c_str(), 32);
    mqttPassParam = new WiFiManagerParameter("mqtt_pass", "MQTT Passwort",
                                              CredentialsManager::instance().getMqttPass().c_str(), 32);
    wm.addParameter(mqttHostParam);
    wm.addParameter(mqttPortParam);
    wm.addParameter(mqttUserParam);
    wm.addParameter(mqttPassParam);

    // Non-blocking: returns immediately (almost certainly false, since nothing is connected yet).
    // The AP + captive portal keep running in the background, serviced by wm.process() in loop().
    if (strlen(WIFI_AP_PASSWORD) > 0) {
        wm.autoConnect(WIFI_AP_NAME, WIFI_AP_PASSWORD);
    } else {
        wm.autoConnect(WIFI_AP_NAME);
    }
    state = State::PortalActive;
}

void onPortalConnected() {
    CredentialsManager::instance().saveWifi(WiFi.SSID(), WiFi.psk());
    CredentialsManager::instance().saveMqtt(mqttHostParam->getValue(),
                                             String(mqttPortParam->getValue()).toInt(),
                                             mqttUserParam->getValue(), mqttPassParam->getValue());
    delete mqttHostParam;
    delete mqttPortParam;
    delete mqttUserParam;
    delete mqttPassParam;
    mqttHostParam = mqttPortParam = mqttUserParam = mqttPassParam = nullptr;
    Serial.println("WiFi: portal credentials saved");
}

}  // namespace

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    String savedSsid = CredentialsManager::instance().getWifiSsid();
    if (savedSsid.length() > 0) {
        Serial.printf("WiFi: connecting to %s (in background)\n", savedSsid.c_str());
        WiFi.begin(savedSsid.c_str(), CredentialsManager::instance().getWifiPass().c_str());
        state = State::ConnectingSaved;
        savedAttemptStartMs = millis();
    } else {
        startPortal();
    }
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (state == State::PortalActive) onPortalConnected();
        if (!everConnected) Serial.printf("WiFi: connected, IP %s\n", WiFi.localIP().toString().c_str());
        everConnected = true;
        state = State::Settled;
        return;
    }

    switch (state) {
        case State::ConnectingSaved:
            if (millis() - savedAttemptStartMs > WIFI_CONNECT_TIMEOUT_MS) {
                Serial.println("WiFi: saved credentials did not connect, opening setup portal");
                startPortal();
            }
            break;
        case State::PortalActive:
            wm.process();
            break;
        case State::Settled:
            // Was connected before and dropped - or the portal timed out without ever connecting.
            // Silent background reconnect only; never re-opens the portal on its own (that would
            // require unplugging/reflashing mid-operation).
            if (everConnected) {
                unsigned long now = millis();
                if (now - lastReconnectAttemptMs >= WIFI_RECONNECT_CHECK_MS) {
                    lastReconnectAttemptMs = now;
                    WiFi.reconnect();
                }
            }
            break;
    }
}

bool isConnected() { return WiFi.status() == WL_CONNECTED; }

String statusText() {
    if (WiFi.status() == WL_CONNECTED) return "IP " + WiFi.localIP().toString();
    if (state == State::PortalActive) {
        IPAddress apIp(WIFI_AP_IP_OCTETS[0], WIFI_AP_IP_OCTETS[1], WIFI_AP_IP_OCTETS[2],
                        WIFI_AP_IP_OCTETS[3]);
        return "Setup " + apIp.toString();
    }
    return "Verbinde...";
}

}  // namespace WifiManager
