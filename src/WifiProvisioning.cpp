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
unsigned long lastPrimaryRecoveryMs = 0;
bool everConnected = false;
// false = currently on/attempting the primary network (always preferred), true = the fallback.
// Only ever becomes true once already running normally (Settled) and the primary drops - the
// initial boot connection in begin() only ever tries primary, unchanged for now.
bool usingSecondary = false;

::WiFiManager wm;
// WiFiManagerParameter keeps its own internal copy of the value, but the objects themselves must
// stay alive for as long as wm.process() might still reference them - i.e. for the whole time the
// portal can be open, not just inside startPortal().
WiFiManagerParameter* mqttHostParam = nullptr;
WiFiManagerParameter* mqttPortParam = nullptr;
WiFiManagerParameter* mqttUserParam = nullptr;
WiFiManagerParameter* mqttPassParam = nullptr;
WiFiManagerParameter* hostnameParam = nullptr;

bool hasSecondary() { return CredentialsManager::instance().getWifiSsid2().length() > 0; }

// Starts an async connection attempt to whichever network is requested and remembers which one
// it is (usingSecondary) - loop() checks WiFi.status() on later calls to see how it went.
void beginConnect(bool secondary) {
    usingSecondary = secondary;
    String ssid = secondary ? CredentialsManager::instance().getWifiSsid2()
                             : CredentialsManager::instance().getWifiSsid();
    String pass = secondary ? CredentialsManager::instance().getWifiPass2()
                             : CredentialsManager::instance().getWifiPass();
    Serial.printf("WiFi: verbinde zu %s (%s)\n", ssid.c_str(), secondary ? "Fallback" : "primaer");
    WiFi.begin(ssid.c_str(), pass.c_str());
    savedAttemptStartMs = millis();
}

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
    // Device name: mDNS hostname, OTA login, and MQTT topic prefix all in one - matters most for
    // multi-device setups (each needs a distinct value to avoid colliding on the same network/
    // broker), so it's collected here too rather than only reachable later via the Konfiguration
    // tab (which itself needs this same name to know which device you're even talking to).
    hostnameParam = new WiFiManagerParameter(
        "hostname", "Geraetename (mDNS/MQTT)", CredentialsManager::instance().getHostname().c_str(), 32,
        " pattern=\"[A-Za-z0-9_-]{1,32}\" title=\"Nur Buchstaben, Ziffern, - und _\" required");
    wm.addParameter(mqttHostParam);
    wm.addParameter(mqttPortParam);
    wm.addParameter(mqttUserParam);
    wm.addParameter(mqttPassParam);
    wm.addParameter(hostnameParam);

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
    // Explicit stop, not relying solely on WiFiManager's own internal bookkeeping - this also
    // fires when the self-heal background retry (see PortalActive handling below) reconnects
    // using the original credentials directly via WiFi.begin(), bypassing the portal's own form
    // submission entirely, so it needs its own guaranteed cleanup path.
    wm.stopConfigPortal();
    CredentialsManager::instance().saveWifi(WiFi.SSID(), WiFi.psk());
    CredentialsManager::instance().saveMqtt(mqttHostParam->getValue(),
                                             String(mqttPortParam->getValue()).toInt(),
                                             mqttUserParam->getValue(), mqttPassParam->getValue());
    if (!CredentialsManager::instance().saveHostname(hostnameParam->getValue())) {
        Serial.println("WiFi: invalid device name from portal ignored, keeping previous value");
    }
    delete mqttHostParam;
    delete mqttPortParam;
    delete mqttUserParam;
    delete mqttPassParam;
    delete hostnameParam;
    mqttHostParam = mqttPortParam = mqttUserParam = mqttPassParam = hostnameParam = nullptr;
    Serial.println("WiFi: portal credentials saved");
}

}  // namespace

void begin() {
    WiFi.mode(WIFI_STA);
    WiFi.setSleep(false);

    // Boot only ever tries the primary network - the fallback WiFi (below) is purely a
    // once-already-running safety net, not part of the initial setup flow yet.
    String savedSsid = CredentialsManager::instance().getWifiSsid();
    if (savedSsid.length() > 0) {
        beginConnect(false);
        state = State::ConnectingSaved;
    } else {
        startPortal();
    }
}

void loop() {
    if (WiFi.status() == WL_CONNECTED) {
        if (state == State::PortalActive) onPortalConnected();
        if (!everConnected) {
            Serial.printf("WiFi: connected, IP %s (%s)\n", WiFi.localIP().toString().c_str(),
                          usingSecondary ? "Fallback" : "primaer");
        }
        everConnected = true;
        state = State::Settled;

        // Connected via the fallback right now - periodically test if the preferred primary is
        // back. Single radio, so this briefly drops the working fallback connection to test;
        // kept infrequent (WIFI_PRIMARY_RECOVERY_MS) to limit disruption. If primary doesn't come
        // back within the usual timeout, the ConnectingSaved handling below reconnects to the
        // fallback again automatically (see there).
        if (usingSecondary) {
            unsigned long now = millis();
            if (now - lastPrimaryRecoveryMs >= WIFI_PRIMARY_RECOVERY_MS) {
                lastPrimaryRecoveryMs = now;
                Serial.println("WiFi: pruefe, ob primaeres Netz wieder erreichbar ist");
                beginConnect(false);
                state = State::ConnectingSaved;
            }
        }
        return;
    }

    switch (state) {
        case State::ConnectingSaved:
            if (millis() - savedAttemptStartMs > WIFI_CONNECT_TIMEOUT_MS) {
                if (!usingSecondary && hasSecondary()) {
                    Serial.println("WiFi: primaeres Netz nicht erreichbar, versuche Fallback-WLAN");
                    beginConnect(true);
                } else {
                    // Either no fallback is configured, or the fallback attempt just failed too.
                    // Safe to open the portal here even if this device was already running
                    // normally before (not just on the very first boot): PortalActive below keeps
                    // retrying the original primary credentials in the background and closes the
                    // portal again by itself the moment they work - this can never permanently
                    // strand a remote/no-physical-access device, only a genuinely wrong/changed
                    // password needs a human to actually use the portal.
                    Serial.println("WiFi: kein gespeichertes Netz erreichbar, oeffne Setup-Portal");
                    startPortal();
                }
            }
            break;
        case State::PortalActive: {
            wm.process();
            // Self-heal: keep periodically retrying the original primary credentials in the
            // background while the portal stays fully open/usable - if the primary network was
            // just temporarily unreachable (not genuinely wrong credentials), this reconnects and
            // the top-of-loop() WL_CONNECTED check closes the portal automatically (see
            // onPortalConnected()), with zero human intervention needed.
            unsigned long now = millis();
            if (now - lastReconnectAttemptMs >= WIFI_RECONNECT_CHECK_MS &&
                CredentialsManager::instance().getWifiSsid().length() > 0) {
                lastReconnectAttemptMs = now;
                beginConnect(false);
            }
            break;
        }
        case State::Settled:
            // Was connected before and dropped. Silent background reconnect, alternating between
            // primary and fallback (if configured) - eventually opens the (self-healing, see
            // above) portal if neither responds, same as a fresh boot would.
            if (everConnected) {
                unsigned long now = millis();
                if (now - lastReconnectAttemptMs >= WIFI_RECONNECT_CHECK_MS) {
                    lastReconnectAttemptMs = now;
                    beginConnect(usingSecondary);
                    state = State::ConnectingSaved;
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
