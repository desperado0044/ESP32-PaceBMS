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

}  // namespace RuntimeSettings
