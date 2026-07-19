#include "MqttManager.h"
#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include "Config.h"
#include "CredentialsStorage.h"

namespace MqttManager {

namespace {
WiFiClient wifiClient;
PubSubClient client(wifiClient);

// PubSubClient::setServer() only stores the const char* pointer it's given, not a copy - this
// must stay alive for as long as the client uses it, so the host string is kept here rather than
// as a temporary in begin().
String mqttHostBuf;

bool discoveryPublished = false;
unsigned long lastReconnectAttemptMs = 0;
constexpr unsigned long kReconnectIntervalMs = 5000;

String availabilityTopic() { return String(MQTT_BASE_TOPIC) + "/availability"; }

String topic(const String& suffix) { return String(MQTT_BASE_TOPIC) + "/" + suffix; }

void publish(const String& suffix, const String& value, bool retain = false) {
    client.publish(topic(suffix).c_str(), value.c_str(), retain);
}

void publish(const String& suffix, float value, bool retain = false) {
    publish(suffix, String(value, 2), retain);
}

void publish(const String& suffix, long value, bool retain = false) {
    publish(suffix, String(value), retain);
}

void publishSensorDiscovery(const String& deviceId, const String& uniqueSuffix,
                             const String& name, const String& stateTopic,
                             const char* unit) {
    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = "pacebms_" + deviceId + "_" + uniqueSuffix;
    doc["state_topic"] = stateTopic;
    doc["availability_topic"] = availabilityTopic();
    if (unit) doc["unit_of_measurement"] = unit;
    JsonObject device = doc["device"].to<JsonObject>();
    device["manufacturer"] = "PACE";
    device["model"] = "PACE BMS";
    device["identifiers"] = "pacebms_" + deviceId;
    device["name"] = "PACE BMS";

    String payload;
    serializeJson(doc, payload);
    String discTopic = String(MQTT_HA_DISCOVERY_TOPIC) + "/sensor/pacebms-" + deviceId + "/" +
                        uniqueSuffix + "/config";
    client.publish(discTopic.c_str(), payload.c_str(), true);
}

void publishBinarySensorDiscovery(const String& deviceId, const String& uniqueSuffix,
                                   const String& name, const String& stateTopic) {
    JsonDocument doc;
    doc["name"] = name;
    doc["unique_id"] = "pacebms_" + deviceId + "_" + uniqueSuffix;
    doc["state_topic"] = stateTopic;
    doc["availability_topic"] = availabilityTopic();
    doc["payload_on"] = "1";
    doc["payload_off"] = "0";
    JsonObject device = doc["device"].to<JsonObject>();
    device["manufacturer"] = "PACE";
    device["model"] = "PACE BMS";
    device["identifiers"] = "pacebms_" + deviceId;
    device["name"] = "PACE BMS";

    String payload;
    serializeJson(doc, payload);
    String discTopic = String(MQTT_HA_DISCOVERY_TOPIC) + "/binary_sensor/pacebms-" + deviceId +
                        "/" + uniqueSuffix + "/config";
    client.publish(discTopic.c_str(), payload.c_str(), true);
}

void publishDiscovery(const PaceBmsSnapshot& snapshot) {
    if (!MQTT_HA_DISCOVERY_ENABLED) return;
    String deviceId = snapshot.bmsSerial.length() > 0 ? snapshot.bmsSerial : String(MQTT_CLIENT_ID);

    for (uint8_t p = 1; p <= snapshot.packCount; p++) {
        String packPrefix = "pack_" + String(p);
        const PacePackAnalog& pack = snapshot.packs[p - 1];

        for (uint8_t i = 0; i < pack.cellCount; i++) {
            String suffix = packPrefix + "_v_cell_" + String(i + 1);
            String stateTopic = topic(packPrefix + "/v_cells/cell_" + String(i + 1));
            publishSensorDiscovery(deviceId, suffix,
                                    "Pack " + String(p) + " Cell " + String(i + 1) + " Voltage",
                                    stateTopic, "mV");
        }
        for (uint8_t i = 0; i < pack.tempCount; i++) {
            String suffix = packPrefix + "_temp_" + String(i + 1);
            String stateTopic = topic(packPrefix + "/temps/temp_" + String(i + 1));
            publishSensorDiscovery(deviceId, suffix,
                                    "Pack " + String(p) + " Temperature " + String(i + 1),
                                    stateTopic, "°C");
        }

        publishSensorDiscovery(deviceId, packPrefix + "_i_pack", "Pack " + String(p) + " Current",
                                topic(packPrefix + "/i_pack"), "A");
        publishSensorDiscovery(deviceId, packPrefix + "_v_pack", "Pack " + String(p) + " Voltage",
                                topic(packPrefix + "/v_pack"), "V");
        publishSensorDiscovery(deviceId, packPrefix + "_i_remain_cap",
                                "Pack " + String(p) + " Remaining Capacity",
                                topic(packPrefix + "/i_remain_cap"), "mAh");
        publishSensorDiscovery(deviceId, packPrefix + "_i_full_cap",
                                "Pack " + String(p) + " Full Capacity",
                                topic(packPrefix + "/i_full_cap"), "mAh");
        publishSensorDiscovery(deviceId, packPrefix + "_i_design_cap",
                                "Pack " + String(p) + " Design Capacity",
                                topic(packPrefix + "/i_design_cap"), "mAh");
        publishSensorDiscovery(deviceId, packPrefix + "_soc", "Pack " + String(p) + " State of Charge",
                                topic(packPrefix + "/soc"), "%");
        publishSensorDiscovery(deviceId, packPrefix + "_soh", "Pack " + String(p) + " State of Health",
                                topic(packPrefix + "/soh"), "%");
        publishSensorDiscovery(deviceId, packPrefix + "_cycles", "Pack " + String(p) + " Cycles",
                                topic(packPrefix + "/cycles"), nullptr);
        publishSensorDiscovery(deviceId, packPrefix + "_cells_max_diff",
                                "Pack " + String(p) + " Cell Max Voltage Diff",
                                topic(packPrefix + "/cells_max_diff_calc"), "mV");
        publishSensorDiscovery(deviceId, packPrefix + "_warnings", "Pack " + String(p) + " Warnings",
                                topic(packPrefix + "/warnings"), nullptr);
        publishSensorDiscovery(deviceId, packPrefix + "_balancing1", "Pack " + String(p) + " Balancing 1",
                                topic(packPrefix + "/balancing1"), nullptr);
        publishSensorDiscovery(deviceId, packPrefix + "_balancing2", "Pack " + String(p) + " Balancing 2",
                                topic(packPrefix + "/balancing2"), nullptr);

        publishBinarySensorDiscovery(deviceId, packPrefix + "_prot_short_circuit",
                                      "Pack " + String(p) + " Protection Short Circuit",
                                      topic(packPrefix + "/prot_short_circuit"));
        publishBinarySensorDiscovery(deviceId, packPrefix + "_prot_discharge_current",
                                      "Pack " + String(p) + " Protection Discharge Current",
                                      topic(packPrefix + "/prot_discharge_current"));
        publishBinarySensorDiscovery(deviceId, packPrefix + "_prot_charge_current",
                                      "Pack " + String(p) + " Protection Charge Current",
                                      topic(packPrefix + "/prot_charge_current"));
        publishBinarySensorDiscovery(deviceId, packPrefix + "_current_limit",
                                      "Pack " + String(p) + " Current Limit",
                                      topic(packPrefix + "/current_limit"));
        publishBinarySensorDiscovery(deviceId, packPrefix + "_charge_fet",
                                      "Pack " + String(p) + " Charge FET",
                                      topic(packPrefix + "/charge_fet"));
        publishBinarySensorDiscovery(deviceId, packPrefix + "_discharge_fet",
                                      "Pack " + String(p) + " Discharge FET",
                                      topic(packPrefix + "/discharge_fet"));
        publishBinarySensorDiscovery(deviceId, packPrefix + "_pack_indicate",
                                      "Pack " + String(p) + " Pack Indicate",
                                      topic(packPrefix + "/pack_indicate"));
        publishBinarySensorDiscovery(deviceId, packPrefix + "_reverse", "Pack " + String(p) + " Reverse",
                                      topic(packPrefix + "/reverse"));
        publishBinarySensorDiscovery(deviceId, packPrefix + "_ac_in", "Pack " + String(p) + " AC In",
                                      topic(packPrefix + "/ac_in"));
        publishBinarySensorDiscovery(deviceId, packPrefix + "_heart", "Pack " + String(p) + " Heart",
                                      topic(packPrefix + "/heart"));
        publishBinarySensorDiscovery(deviceId, packPrefix + "_fully", "Pack " + String(p) + " Fully Charged",
                                      topic(packPrefix + "/fully"));
    }

    publishSensorDiscovery(deviceId, "pack_remain_cap", "Pack Remaining Capacity",
                            topic("pack_remain_cap"), "mAh");
    publishSensorDiscovery(deviceId, "pack_full_cap", "Pack Full Capacity", topic("pack_full_cap"),
                            "mAh");
    publishSensorDiscovery(deviceId, "pack_design_cap", "Pack Design Capacity",
                            topic("pack_design_cap"), "mAh");
    publishSensorDiscovery(deviceId, "pack_soc", "Pack State of Charge", topic("pack_soc"), "%");
    publishSensorDiscovery(deviceId, "pack_soh", "Pack State of Health", topic("pack_soh"), "%");
}

bool reconnect() {
    String clientId = String(MQTT_CLIENT_ID) + "-" + String((uint32_t)ESP.getEfuseMac(), HEX);
    String user = CredentialsManager::instance().getMqttUser();
    String pass = CredentialsManager::instance().getMqttPass();
    bool ok;
    if (user.length() > 0) {
        ok = client.connect(clientId.c_str(), user.c_str(), pass.c_str(), availabilityTopic().c_str(),
                             0, true, "offline");
    } else {
        ok = client.connect(clientId.c_str(), nullptr, nullptr, availabilityTopic().c_str(), 0, true,
                             "offline");
    }
    if (ok) {
        Serial.println("MQTT: connected");
        client.publish(availabilityTopic().c_str(), "online", true);
        discoveryPublished = false;
    }
    return ok;
}

}  // namespace

void begin() {
    mqttHostBuf = CredentialsManager::instance().getMqttHost();
    client.setServer(mqttHostBuf.c_str(), CredentialsManager::instance().getMqttPort());
    client.setBufferSize(1024);
}

void loop() {
    if (WiFi.status() != WL_CONNECTED) return;
    if (!client.connected()) {
        unsigned long now = millis();
        if (now - lastReconnectAttemptMs < kReconnectIntervalMs) return;
        lastReconnectAttemptMs = now;
        reconnect();
        return;
    }
    client.loop();
}

bool isConnected() { return client.connected(); }

void publishAvailability(bool online) {
    if (!client.connected()) return;
    client.publish(availabilityTopic().c_str(), online ? "online" : "offline", true);
}

void publishSnapshot(const PaceBmsSnapshot& snapshot) {
    if (!client.connected() || !snapshot.valid) return;

    if (snapshot.bmsVersion.length() > 0) publish("bms_version", snapshot.bmsVersion, true);
    if (snapshot.bmsSerial.length() > 0) publish("bms_sn", snapshot.bmsSerial, true);
    if (snapshot.packSerial.length() > 0) publish("pack_sn", snapshot.packSerial, true);

    for (uint8_t p = 1; p <= snapshot.packCount; p++) {
        String packPrefix = "pack_" + String(p);
        const PacePackAnalog& pack = snapshot.packs[p - 1];
        const PacePackWarn& warn = snapshot.warn[p - 1];

        for (uint8_t i = 0; i < pack.cellCount; i++) {
            publish(packPrefix + "/v_cells/cell_" + String(i + 1), (long)pack.cellMillivolts[i]);
        }
        for (uint8_t i = 0; i < pack.tempCount; i++) {
            publish(packPrefix + "/temps/temp_" + String(i + 1), pack.temperaturesC[i]);
        }
        publish(packPrefix + "/cells_max_diff_calc", (long)pack.cellMaxDiffMv);
        publish(packPrefix + "/i_pack", pack.packCurrentA);
        publish(packPrefix + "/v_pack", pack.packVoltageV);
        publish(packPrefix + "/i_remain_cap", (long)pack.remainingCapacityMah);
        publish(packPrefix + "/i_full_cap", (long)pack.fullCapacityMah);
        publish(packPrefix + "/i_design_cap", (long)pack.designCapacityMah);
        publish(packPrefix + "/soc", pack.socPercent);
        publish(packPrefix + "/soh", pack.sohPercent);
        publish(packPrefix + "/cycles", (long)pack.cycles);

        publish(packPrefix + "/warnings", warn.warnings);
        publish(packPrefix + "/balancing1", String(warn.balanceState1, BIN));
        publish(packPrefix + "/balancing2", String(warn.balanceState2, BIN));
        publish(packPrefix + "/prot_short_circuit", warn.protShortCircuit ? "1" : "0");
        publish(packPrefix + "/prot_discharge_current", warn.protDischargeCurrent ? "1" : "0");
        publish(packPrefix + "/prot_charge_current", warn.protChargeCurrent ? "1" : "0");
        publish(packPrefix + "/current_limit", warn.currentLimitOn ? "1" : "0");
        publish(packPrefix + "/charge_fet", warn.chargeFetOn ? "1" : "0");
        publish(packPrefix + "/discharge_fet", warn.dischargeFetOn ? "1" : "0");
        publish(packPrefix + "/pack_indicate", warn.packIndicateOn ? "1" : "0");
        publish(packPrefix + "/reverse", warn.reverseOn ? "1" : "0");
        publish(packPrefix + "/ac_in", warn.acInOn ? "1" : "0");
        publish(packPrefix + "/heart", warn.heartOn ? "1" : "0");
        publish(packPrefix + "/fully", warn.fullyCharged ? "1" : "0");
    }

    publish("pack_remain_cap", (long)snapshot.capacity.remainCapacityMah);
    publish("pack_full_cap", (long)snapshot.capacity.fullCapacityMah);
    publish("pack_design_cap", (long)snapshot.capacity.designCapacityMah);
    publish("pack_soc", snapshot.capacity.socPercent);
    publish("pack_soh", snapshot.capacity.sohPercent);

    if (!discoveryPublished) {
        publishDiscovery(snapshot);
        discoveryPublished = true;
    }
}

}  // namespace MqttManager
