#pragma once
#include "BmsData.h"

// Dev/demo helper, gated by Config.h's SIMULATE_BMS_DATA - fills in a made-up but plausible and
// slowly drifting single-pack snapshot so the display/web UI can be previewed without a real BMS.
namespace SimulatedBms {

void fillSimulatedSnapshot(PaceBmsSnapshot& snapshot);

}  // namespace SimulatedBms
