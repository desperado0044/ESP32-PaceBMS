#include "NetworkTask.h"
#include <Arduino.h>
#include <ESPmDNS.h>
#include "Config.h"
#include "PaceBmsClient.h"
#include "PaceModbusClient.h"
#include "WifiProvisioning.h"
#include "MqttManager.h"
#include "WebUiServer.h"
#include "SnapshotStore.h"
#include "SimulatedBms.h"
#include "RuntimeSettings.h"
#include "BmsActivity.h"

namespace NetworkTask {

namespace {

void taskEntry(void* /*pvParameters*/) {
    WifiManager::begin();

    PaceBmsClient bmsClient(Serial2, BMS_RESPONSE_TIMEOUT_MS);
    PaceModbusClient modbusClient(Serial1, MODBUS_DE_RE_PIN, MODBUS_RESPONSE_TIMEOUT_MS);
    PaceBmsSnapshot workingSnapshot;  // mutated in place across polls, mirrored out via SnapshotStore
    unsigned long lastPollMs = 0;
    bool servicesStarted = false;
    int consecutiveFailures = 0;

    for (;;) {
        WifiManager::loop();

        // MQTT + the web server both need WiFi up first, and the web server must not bind port 80
        // while WiFiManager's own captive-portal server might still be using it - so both start
        // only once, right after the first successful connect.
        if (!servicesStarted && WifiManager::isConnected()) {
            MqttManager::begin();
            WebUiServer::begin();
            if (MDNS.begin(OTA_HOSTNAME)) {
                MDNS.addService("http", "tcp", WEB_SERVER_PORT);
                Serial.printf("mDNS: reachable as %s.local\n", OTA_HOSTNAME);
            }
            servicesStarted = true;
        }
        if (servicesStarted) {
            MqttManager::loop();
            WebUiServer::loop();
        }

        unsigned long now = millis();
        if (now - lastPollMs >= RuntimeSettings::bmsPollIntervalMs()) {
            lastPollMs = now;
            BmsActivity::markRequestSent();
            bool useModbus = RuntimeSettings::useModbus();
            bool pollOk;
            const char* pollErr = "";
            if (RuntimeSettings::simulateBmsData()) {
                SimulatedBms::fillSimulatedSnapshot(workingSnapshot);
                pollOk = true;
            } else if (useModbus) {
                pollOk = modbusClient.poll(workingSnapshot);
                pollErr = modbusClient.lastError().c_str();
            } else {
                pollOk = bmsClient.poll(workingSnapshot);
                pollErr = bmsClient.lastError().c_str();
            }

            if (pollOk) {
                BmsActivity::markResponseReceived();
                consecutiveFailures = 0;
                SnapshotStore::set(workingSnapshot);
                if (servicesStarted) MqttManager::publishSnapshot(workingSnapshot);
            } else {
                Serial.printf("BMS poll failed (%s): %s\n", useModbus ? "Modbus" : "RS232", pollErr);
                consecutiveFailures++;
                if (consecutiveFailures == BMS_ZERO_AFTER_CONSECUTIVE_FAILURES) {
                    // BMS unresponsive for a while now - this counts as a definitive "disconnected"
                    // reading, not a stale one, so valid/lastUpdateMs are refreshed like a normal
                    // successful poll instead of leaving the freshness dot stuck on old data too.
                    for (uint8_t i = 0; i < workingSnapshot.packCount; i++) {
                        workingSnapshot.packs[i] = PacePackAnalog();
                        workingSnapshot.warn[i] = PacePackWarn();
                    }
                    workingSnapshot.capacity = PaceCapacity();
                    workingSnapshot.valid = true;
                    workingSnapshot.lastUpdateMs = millis();
                    SnapshotStore::set(workingSnapshot);
                    if (servicesStarted) MqttManager::publishSnapshot(workingSnapshot);
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace

void start() {
    xTaskCreatePinnedToCore(taskEntry, "network", 8192, nullptr, 1, nullptr, 0);
}

}  // namespace NetworkTask
