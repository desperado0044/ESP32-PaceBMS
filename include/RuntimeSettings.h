#pragma once
#include <cstdint>

// NVS-persisted settings the user can flip at runtime (display System tab / web Konfiguration),
// as opposed to Config.h's compile-time constants. Falls back to the compile-time default the
// first time it's read (nothing saved yet).
namespace RuntimeSettings {

void begin();

bool simulateBmsData();
// Persists the new value; caller is expected to reboot afterwards (NetworkTask only reads this
// once at task start, same pattern as WiFi/MQTT credential changes).
void setSimulateBmsData(bool value);

// Which BMS transport NetworkTask polls: false = RS232/ASCII (PaceBmsClient, the default), true =
// Modbus RTU/RS485 (PaceModbusClient). Same "persist + caller reboots" pattern as above.
bool useModbus();
void setUseModbus(bool value);

// Which Modbus slave addresses (0-15, per the official PACE dip-switch table) are actually
// installed, as a bitmask: bit N set = address N is queried. Set via the web UI's
// "Modbus-Konfiguration" section. Default: only address 1, so an upgrade from the earlier
// single-pack-only Modbus build keeps working unchanged until reconfigured. Address 0's behavior
// is not clearly documented (see README) - included for completeness, not confirmed working.
uint16_t modbusPackAddressMask();
void setModbusPackAddressMask(uint16_t mask);

// How often NetworkTask polls the BMS, in milliseconds. Unlike the settings above, NetworkTask
// re-reads this every loop iteration rather than caching it once at task start, so a change here
// takes effect on its very next poll check (well under a second) - no reboot needed.
unsigned long bmsPollIntervalMs();
void setBmsPollIntervalMs(unsigned long ms);

// RS232 only: whether PaceBmsClient builds the "last raw poll bytes" hex dump shown on the web
// UI's Diagnose page (see PaceBmsClient::lastRawHex()/lastAnalogRawHex()). Off by default - the
// hex string is rebuilt via repeated heap-allocating String concatenation on every single poll
// cycle, which is fine to enable temporarily while actually looking at the Diagnose page but not
// worth paying continuously (heap fragmentation risk on a device that otherwise runs for weeks).
// Re-read every loop iteration like bmsPollIntervalMs - no reboot needed to flip it.
bool rawCaptureEnabled();
void setRawCaptureEnabled(bool value);

}  // namespace RuntimeSettings
