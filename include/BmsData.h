#pragma once
#include <Arduino.h>

constexpr uint8_t PACE_MAX_PACKS = 16;  // Modbus dip-switch addresses 0-15, 16 possible slots
constexpr uint8_t PACE_MAX_CELLS = 24;
constexpr uint8_t PACE_MAX_TEMPS = 8;

struct PacePackAnalog {
    uint8_t cellCount = 0;
    uint16_t cellMillivolts[PACE_MAX_CELLS] = {0};
    uint16_t cellMaxDiffMv = 0;
    uint8_t tempCount = 0;
    float temperaturesC[PACE_MAX_TEMPS] = {0};
    float packCurrentA = 0;
    float packVoltageV = 0;
    uint32_t remainingCapacityMah = 0;
    uint32_t fullCapacityMah = 0;
    uint32_t designCapacityMah = 0;
    uint16_t cycles = 0;
    float socPercent = 0;
    float sohPercent = 0;
};

struct PacePackWarn {
    String warnings;  // human readable, comma separated
    bool protShortCircuit = false;
    bool protDischargeCurrent = false;
    bool protChargeCurrent = false;
    bool fullyCharged = false;
    bool currentLimitOn = false;
    bool chargeFetOn = false;
    bool dischargeFetOn = false;
    bool packIndicateOn = false;
    bool reverseOn = false;
    bool acInOn = false;
    bool heartOn = false;
    uint8_t balanceState1 = 0;
    uint8_t balanceState2 = 0;
};

struct PaceCapacity {
    uint32_t remainCapacityMah = 0;
    uint32_t fullCapacityMah = 0;
    uint32_t designCapacityMah = 0;
    float socPercent = 0;
    float sohPercent = 0;
};

struct PaceBmsSnapshot {
    bool valid = false;
    unsigned long lastUpdateMs = 0;
    String bmsVersion;
    String bmsSerial;
    String packSerial;

    uint8_t packCount = 0;
    // 1-based identifier shown to the user for packs[i]/warn[i] - for RS232 this is just the
    // sequential slot number (i+1, RS232 has no separate physical address); for Modbus it's the
    // actual configured RS485 slave address (1-15, may have gaps if addresses aren't contiguous).
    uint8_t packAddress[PACE_MAX_PACKS] = {0};
    PacePackAnalog packs[PACE_MAX_PACKS];
    PacePackWarn warn[PACE_MAX_PACKS];
    PaceCapacity capacity;

    // Diagnostic only: the transport client's own lastError() from the most recent poll attempt,
    // whatever it did - stashed here (not just logged to Serial) so it's visible over the network
    // via /api/system without needing a USB/serial connection to the board. Empty string means the
    // most recent attempt had nothing to report.
    String lastPollError;

    // RS232 only: raw bytes seen during the most recent poll (success or failure), as a hex
    // string - Modbus already gets a genuine passive bus-sniff (see BusSniff), but RS232 is a
    // direct point-to-point link with no independent traffic to sniff, so this is the only way to
    // see "what did the BMS just send" over the network. Stays empty for Modbus/simulation, and
    // also empty while RuntimeSettings::rawCaptureEnabled() is off (the default). poll() requests
    // warn info last each cycle, so this ends up holding that response specifically - see
    // lastAnalogRawHex below for the other (and, for the SOC/voltage data, more interesting) one.
    String lastRawHex;
    String lastAnalogRawHex;

    // Communication diagnostics, all surfaced via /api/system's "Kommunikation" section.
    int consecutiveFailures = 0;
    // Only meaningful for Modbus (indexed like packAddress[]/packs[] - position in the configured
    // address list, not the address value itself); stays all-zero for RS232, which has no
    // per-pack addressing to track separately from the single overall poll result.
    uint8_t packFailCount[PACE_MAX_PACKS] = {0};
};
