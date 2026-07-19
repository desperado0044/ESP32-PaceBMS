#pragma once

// NVS-persisted settings the user can flip at runtime (display System tab / web Konfiguration),
// as opposed to Config.h's compile-time constants. Falls back to the compile-time default the
// first time it's read (nothing saved yet).
namespace RuntimeSettings {

void begin();

bool simulateBmsData();
// Persists the new value; caller is expected to reboot afterwards (NetworkTask only reads this
// once at task start, same pattern as WiFi/MQTT credential changes).
void setSimulateBmsData(bool value);

}  // namespace RuntimeSettings
