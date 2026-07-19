#include <Arduino.h>
#include "Config.h"
#include "DisplayHardware.h"
#include "StorageCalibration.h"
#include "CredentialsStorage.h"
#include "BmsDisplayUi.h"
#include "SnapshotStore.h"
#include "NetworkTask.h"
#include "FactoryReset.h"
#include "RuntimeSettings.h"

// Two independent halves, split across cores so a stalled BMS UART read or a WiFi/MQTT hiccup
// never freezes the touch display:
//  - Core 1 (this file's setup()/loop(), Arduino's default core): display + touch only.
//  - Core 0 (NetworkTask): WiFi/captive portal, MQTT, the web server, and BMS polling - all of
//    which can block for anywhere from tens of ms to (with no BMS attached/responding) the full
//    BMS_RESPONSE_TIMEOUT_MS per command.
// The two sides only talk to each other through SnapshotStore's mutex-protected copy.

namespace {
HardwareSerial& bmsSerial = Serial2;
HardwareSerial& modbusSerial = Serial1;
}  // namespace

void setup() {
    Serial.begin(115200);
    bmsSerial.begin(BMS_UART_BAUD, SERIAL_8N1, BMS_UART_RX_PIN, BMS_UART_TX_PIN);

    // Modbus/RS485 UART + DE/RE control pin - initialized unconditionally (cheap, harmless if the
    // Modbus transport is never selected via RuntimeSettings::useModbus()).
    modbusSerial.begin(MODBUS_UART_BAUD, SERIAL_8N1, MODBUS_UART_RX_PIN, MODBUS_UART_TX_PIN);
    pinMode(MODBUS_DE_RE_PIN, OUTPUT);
    digitalWrite(MODBUS_DE_RE_PIN, LOW);  // receive mode by default

    CalibrationManager::instance().begin();
    CredentialsManager::instance().begin();
    RuntimeSettings::begin();
    SnapshotStore::begin();
    DisplayHardware::begin();
    BmsDisplayUi::begin();
    FactoryReset::begin();

    NetworkTask::start();

    Serial.println("PACE BMS monitor ready");
}

void loop() {
    FactoryReset::checkButton();
    BmsDisplayUi::update(SnapshotStore::get());
    delay(5);
}
