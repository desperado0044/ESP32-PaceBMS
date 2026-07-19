#pragma once

// Everything that can block or take a while - WiFi connect/captive portal, MQTT, the async web
// server, and BMS UART polling (readFrame() blocks up to BMS_RESPONSE_TIMEOUT_MS waiting for a
// response, which is routinely hit in full when no BMS is attached/responding) - runs in this
// FreeRTOS task, pinned to Core 0. The display/touch loop (Core 1, default Arduino loop()) never
// calls any of it directly, so it stays responsive regardless of what the network side is doing.
namespace NetworkTask {

// Spawns the task. Call once from setup(), after Serial2 (the BMS UART) and the credential/
// calibration NVS namespaces have been initialized.
void start();

}  // namespace NetworkTask
