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
#include "ModbusSniff.h"
#include "CredentialsStorage.h"

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
            String hostname = CredentialsManager::instance().getHostname();
            if (MDNS.begin(hostname.c_str())) {
                MDNS.addService("http", "tcp", WEB_SERVER_PORT);
                Serial.printf("mDNS: reachable as %s.local\n", hostname.c_str());
            }
            servicesStarted = true;
        }
        if (servicesStarted) {
            MqttManager::loop();
            WebUiServer::loop();
        }

        ModbusSniff::collectIfActive(Serial1);

        unsigned long now = millis();
        if (!ModbusSniff::active() && now - lastPollMs >= RuntimeSettings::bmsPollIntervalMs()) {
            lastPollMs = now;
            BmsActivity::markRequestSent();
            bool useModbus = RuntimeSettings::useModbus();
            bool pollOk;
            String pollErr;
            if (RuntimeSettings::simulateBmsData()) {
                SimulatedBms::fillSimulatedSnapshot(workingSnapshot);
                pollOk = true;
            } else if (useModbus) {
                pollOk = modbusClient.poll(workingSnapshot);
                pollErr = modbusClient.lastError();
                memcpy(workingSnapshot.packFailCount, modbusClient.failCounts(),
                       sizeof(workingSnapshot.packFailCount));
            } else {
                pollOk = bmsClient.poll(workingSnapshot);
                pollErr = bmsClient.lastError();
            }
            // Stashed regardless of pollOk - Modbus in particular can report overall success (at
            // least one configured address answered) while still having something to report for
            // another one, and this is the only way to see it without a USB/serial connection.
            workingSnapshot.lastPollError = pollErr;

            bool publishNow = false;
            if (pollOk) {
                BmsActivity::markResponseReceived();
                consecutiveFailures = 0;
                publishNow = true;
            } else {
                Serial.printf("BMS poll failed (%s): %s\n", useModbus ? "Modbus" : "RS232",
                              pollErr.c_str());
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
                    publishNow = true;
                }
            }
            // Communication diagnostics (consecutiveFailures/packFailCount/lastPollError) are kept
            // fresh in the store every cycle, independent of the success/debounce logic above, so
            // the web UI's Kommunikation section reflects what just happened, not last cycle's
            // stale state, on every attempt, not just successes/complete-disconnect debounces.
            workingSnapshot.consecutiveFailures = consecutiveFailures;
            SnapshotStore::set(workingSnapshot);
            if (publishNow && servicesStarted) MqttManager::publishSnapshot(workingSnapshot);
        }

        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

}  // namespace

void start() {
    xTaskCreatePinnedToCore(taskEntry, "network", 8192, nullptr, 1, nullptr, 0);
}

}  // namespace NetworkTask
