#include "SimulatedBms.h"
#include <Arduino.h>
#include <math.h>

namespace SimulatedBms {

namespace {
constexpr uint8_t kCellCount = 16;  // typical 48V/16S LiFePO4 pack
constexpr uint8_t kTempCount = 4;
constexpr uint8_t kPackCount = 3;   // exercises pack-switching/aggregate view in the UI

// Approximate single-cell LiFePO4 open-circuit-voltage curve: flat ~3.20-3.30V plateau through
// the 20-90% middle, steeper drop-off/rise near the empty/full ends - unlike a plain linear
// SOC->voltage mapping, which looks nothing like a real pack to anyone who's watched one.
float cellVoltageMvForSoc(float socPercent) {
    static const float kSocPoints[] = {0, 5, 10, 20, 30, 40, 50, 60, 70, 80, 90, 95, 100};
    static const float kMvPoints[] = {2500, 2900, 3000, 3090, 3150, 3180, 3200,
                                       3220, 3250, 3270, 3300, 3350, 3450};
    constexpr int n = sizeof(kSocPoints) / sizeof(kSocPoints[0]);

    float soc = constrain(socPercent, 0.0f, 100.0f);
    for (int i = 0; i < n - 1; i++) {
        if (soc <= kSocPoints[i + 1]) {
            float span = kSocPoints[i + 1] - kSocPoints[i];
            float frac = span > 0 ? (soc - kSocPoints[i]) / span : 0;
            return kMvPoints[i] + frac * (kMvPoints[i + 1] - kMvPoints[i]);
        }
    }
    return kMvPoints[n - 1];
}

void fillPack(PacePackAnalog& pack, PacePackWarn& warn, float t, float socOffset,
              float phaseShift) {
    // SOC drifts slowly between roughly 35% and 95% over a ~4 minute cycle; current follows the
    // same phase so charging (positive) lines up with a rising SOC and vice versa. Each pack gets
    // a slightly different phase/offset so they visibly don't move in lockstep.
    float phase = sinf((t + phaseShift) * 2.0f * PI / 240.0f);
    float socPercent = constrain(65.0f + socOffset + phase * 30.0f, 1.0f, 99.0f);
    float currentA = phase * 6.5f;

    pack.cellCount = kCellCount;
    float avgCellMv = cellVoltageMvForSoc(socPercent);
    uint16_t minMv = 0xFFFF, maxMv = 0;
    for (uint8_t i = 0; i < kCellCount; i++) {
        // Small deterministic per-cell/time jitter so it visibly "breathes" without being random
        // noise every redraw.
        float jitter = sinf((t + phaseShift) * 0.7f + i * 1.3f) * 12.0f + (i == 3 ? -35.0f : 0) +
                        (i == 9 ? 25.0f : 0);
        uint16_t mv = (uint16_t)(avgCellMv + jitter);
        pack.cellMillivolts[i] = mv;
        if (mv < minMv) minMv = mv;
        if (mv > maxMv) maxMv = mv;
    }
    pack.cellMaxDiffMv = maxMv - minMv;

    pack.tempCount = kTempCount;
    for (uint8_t i = 0; i < kTempCount; i++) {
        pack.temperaturesC[i] = 24.0f + i * 1.5f + sinf((t + phaseShift) * 0.3f + i) * 2.0f;
    }

    pack.packCurrentA = currentA;
    pack.packVoltageV = (avgCellMv * kCellCount) / 1000.0f;
    pack.designCapacityMah = 100000;
    pack.fullCapacityMah = 96000;
    pack.remainingCapacityMah = (uint32_t)(pack.fullCapacityMah * (socPercent / 100.0f));
    pack.cycles = 42;
    pack.socPercent = socPercent;
    pack.sohPercent = (pack.fullCapacityMah * 100.0f) / pack.designCapacityMah;

    warn.warnings = pack.cellMaxDiffMv > 60 ? "cell 4 below limit" : "";
    warn.protShortCircuit = false;
    warn.protDischargeCurrent = false;
    warn.protChargeCurrent = false;
    warn.fullyCharged = socPercent > 94.0f;
    warn.currentLimitOn = false;
    warn.chargeFetOn = true;
    warn.dischargeFetOn = true;
    warn.packIndicateOn = true;
    warn.reverseOn = false;
    warn.acInOn = currentA > 0;
    warn.heartOn = true;
    warn.balanceState1 = pack.cellMaxDiffMv > 60 ? 0b00001000 : 0;
    warn.balanceState2 = 0;
}

}  // namespace

void fillSimulatedSnapshot(PaceBmsSnapshot& snapshot) {
    float t = millis() / 1000.0f;

    snapshot.valid = true;
    snapshot.lastUpdateMs = millis();
    snapshot.bmsVersion = "SIM-1.0";
    snapshot.bmsSerial = "SIM0000000001";
    snapshot.packSerial = "SIMPACK000001";
    snapshot.packCount = kPackCount;
    for (uint8_t i = 0; i < kPackCount; i++) snapshot.packAddress[i] = i + 1;

    // Slightly different SOC offset/phase per pack so the aggregate view and pack-switching are
    // visibly meaningful to test, not just 3 identical copies.
    fillPack(snapshot.packs[0], snapshot.warn[0], t, 0.0f, 0.0f);
    fillPack(snapshot.packs[1], snapshot.warn[1], t, -8.0f, 35.0f);
    fillPack(snapshot.packs[2], snapshot.warn[2], t, 5.0f, 70.0f);

    uint32_t remainSum = 0, fullSum = 0, designSum = 0;
    for (uint8_t i = 0; i < kPackCount; i++) {
        remainSum += snapshot.packs[i].remainingCapacityMah;
        fullSum += snapshot.packs[i].fullCapacityMah;
        designSum += snapshot.packs[i].designCapacityMah;
    }
    snapshot.capacity.remainCapacityMah = remainSum;
    snapshot.capacity.fullCapacityMah = fullSum;
    snapshot.capacity.designCapacityMah = designSum;
    snapshot.capacity.socPercent = fullSum > 0 ? (remainSum * 100.0f) / fullSum : 0;
    snapshot.capacity.sohPercent = designSum > 0 ? (fullSum * 100.0f) / designSum : 0;
}

}  // namespace SimulatedBms
