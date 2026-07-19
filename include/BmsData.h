#pragma once
#include <Arduino.h>

constexpr uint8_t PACE_MAX_PACKS = 4;
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
    PacePackAnalog packs[PACE_MAX_PACKS];
    PacePackWarn warn[PACE_MAX_PACKS];
    PaceCapacity capacity;
};
