#pragma once
#include "BmsData.h"

namespace MqttManager {

void begin();
// Call from loop(); maintains the MQTT connection and processes the client's internal state.
void loop();
bool isConnected();

// Publishes the current snapshot's data topics, and (once per BMS identity / periodically)
// the Home Assistant discovery configs.
void publishSnapshot(const PaceBmsSnapshot& snapshot);

void publishAvailability(bool online);

}  // namespace MqttManager
