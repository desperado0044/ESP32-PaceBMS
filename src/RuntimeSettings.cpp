#include "RuntimeSettings.h"
#include <Preferences.h>
#include "Config.h"

namespace RuntimeSettings {

namespace {
Preferences prefs;
}  // namespace

void begin() { prefs.begin(RUNTIME_SETTINGS_NVS_NAMESPACE, false); }

bool simulateBmsData() { return prefs.getBool("sim_bms", SIMULATE_BMS_DATA); }

void setSimulateBmsData(bool value) { prefs.putBool("sim_bms", value); }

bool useModbus() { return prefs.getBool("use_modbus", false); }

void setUseModbus(bool value) { prefs.putBool("use_modbus", value); }

// Default: bit 1 set = address 1 only (bit N = address N now, not bit (N-1) as in the earlier
// 1-15-only scheme - reinterpreting an already-saved mask from before this change would shift
// every selected address down by one, but this is still prerelease software under active testing).
uint16_t modbusPackAddressMask() { return prefs.getUShort("mb_addr_mask", 0x0002); }

void setModbusPackAddressMask(uint16_t mask) { prefs.putUShort("mb_addr_mask", mask); }

unsigned long bmsPollIntervalMs() { return prefs.getULong("poll_ms", BMS_POLL_INTERVAL_MS); }

void setBmsPollIntervalMs(unsigned long ms) { prefs.putULong("poll_ms", ms); }

}  // namespace RuntimeSettings
