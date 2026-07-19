#include "BmsDisplayUi.h"
#include "DisplayHardware.h"
#include "TouchCalibration.h"
#include "WifiProvisioning.h"
#include "RuntimeSettings.h"
#include "Config.h"
#include <WiFi.h>
#include <math.h>

namespace BmsDisplayUi {

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
}

constexpr uint16_t COLOR_BG = rgb565(12, 13, 18);
constexpr uint16_t COLOR_CARD = rgb565(26, 28, 36);
constexpr uint16_t COLOR_CARD_ALT = rgb565(20, 21, 28);
constexpr uint16_t COLOR_ACCENT = rgb565(0, 200, 150);
constexpr uint16_t COLOR_ACCENT_DIM = rgb565(0, 90, 75);
constexpr uint16_t COLOR_AMBER = rgb565(235, 180, 40);
constexpr uint16_t COLOR_WARN = rgb565(255, 99, 71);
constexpr uint16_t COLOR_WARN_DIM = rgb565(80, 40, 36);
constexpr uint16_t COLOR_OK = rgb565(90, 210, 130);
constexpr uint16_t COLOR_TEXT = 0xFFFF;
constexpr uint16_t COLOR_TEXT_DIM = rgb565(140, 145, 160);
constexpr uint16_t COLOR_DIVIDER = rgb565(42, 44, 52);

constexpr int TOP_BAR_H = 24;
constexpr int TAB_BAR_H = 32;
constexpr int CONTENT_TOP = TOP_BAR_H;
constexpr int CONTENT_BOTTOM = SCREEN_HEIGHT - TAB_BAR_H;
constexpr int CONTENT_H = CONTENT_BOTTOM - CONTENT_TOP;
constexpr int TAB_COUNT = 4;
constexpr int TAB_W = SCREEN_WIDTH / TAB_COUNT;

// Reserved strip at the top of Overview/Cells/Status for the pack indicator ("Gesamt" / "Pack X
// von N" + swipe-hint chevrons) - System has no per-pack concept and doesn't use it.
constexpr int PACK_BAR_H = 16;
constexpr int PAGE_TOP = CONTENT_TOP + PACK_BAR_H;
constexpr int PAGE_H = CONTENT_BOTTOM - PAGE_TOP;

constexpr int SWIPE_THRESHOLD_PX = 32;
constexpr int SWIPE_MAX_VERTICAL_PX = 60;

enum class Page { Overview = 0, Cells = 1, Status = 2, System = 3 };

Page currentPage = Page::Overview;
bool lastTouchedState = false;
unsigned long lastDrawMs = 0;
unsigned long lastTopBarMs = 0;
unsigned long lastSeenUpdateMs = 0;
bool everDrawn = false;

// -1 = "Gesamt" (aggregate across all packs), 0..packCount-1 = a single pack. Shared across
// Overview/Cells/Status so picking a pack on one carries over to the others.
int selectedPack = -1;
bool swipeTracking = false;
bool swipeConsumed = false;
int16_t swipeStartX = 0;
int16_t swipeStartY = 0;

// Bounds of the tappable "Simulation an/aus" row on the System tab, set each time it's drawn.
int simRowY = 0;
int simRowH = 0;

// Bounds of the "Neustart" button on the System tab - placed top-right, away from the Simulation
// row at the bottom, so the two touch targets can't be mixed up.
int rebootBtnX = 0, rebootBtnY = 0, rebootBtnW = 0, rebootBtnH = 0;

// ---- low-level drawing helpers -----------------------------------------------------------

void drawWrappedText(const String& text, int x, int y, int maxWidth, int lineHeight,
                      uint16_t color, uint8_t font) {
    tft.setTextFont(font);
    tft.setTextColor(color, COLOR_BG);
    tft.setTextDatum(TL_DATUM);

    int start = 0;
    int line = 0;
    int len = text.length();
    while (start < len) {
        int end = start;
        int lastSpace = -1;
        int cursor = start;
        while (cursor < len) {
            if (text[cursor] == ' ') lastSpace = cursor;
            String candidate = text.substring(start, cursor + 1);
            if (tft.textWidth(candidate) > maxWidth && cursor > start) {
                end = (lastSpace > start) ? lastSpace : cursor;
                break;
            }
            end = cursor + 1;
            cursor++;
        }
        String lineText = text.substring(start, end);
        lineText.trim();
        tft.drawString(lineText, x, y + line * lineHeight);
        start = end;
        while (start < len && text[start] == ' ') start++;
        line++;
    }
}

void drawBatteryIcon(int x, int y, int w, int h, float socPercent) {
    int nubW = w / 8;
    int nubH = h / 2;
    int bodyW = w - nubW - 2;

    tft.drawRoundRect(x, y, bodyW, h, 4, COLOR_TEXT_DIM);
    tft.fillRoundRect(x + bodyW + 2, y + (h - nubH) / 2, nubW, nubH, 2, COLOR_TEXT_DIM);

    int pad = 3;
    float clamped = socPercent < 0 ? 0 : (socPercent > 100 ? 100 : socPercent);
    int fillW = (int)((bodyW - 2 * pad) * (clamped / 100.0f));
    uint16_t fillColor = clamped > 60 ? COLOR_OK : (clamped > 25 ? COLOR_AMBER : COLOR_WARN);
    if (fillW > 0) tft.fillRoundRect(x + pad, y + pad, fillW, h - 2 * pad, 2, fillColor);
}

// Small triangular current-direction indicator: up = charging (current > 0), down = discharging.
void drawCurrentArrow(int cx, int cy, float currentA) {
    if (fabsf(currentA) < 0.05f) return;
    uint16_t color = currentA > 0 ? COLOR_OK : COLOR_AMBER;
    if (currentA > 0) {
        tft.fillTriangle(cx, cy - 6, cx - 6, cy + 5, cx + 6, cy + 5, color);
    } else {
        tft.fillTriangle(cx, cy + 6, cx - 6, cy - 5, cx + 6, cy - 5, color);
    }
}

void drawStatRow(int x, int y, int w, int h, const char* label, const String& value,
                  bool divider, uint8_t valueFont = 4) {
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    tft.drawString(label, x, y + h / 2 - 8);

    tft.setTextDatum(MR_DATUM);
    tft.setTextFont(valueFont);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString(value, x + w, y + h / 2 + (valueFont >= 4 ? 2 : 0));

    if (divider) tft.drawFastHLine(x, y + h - 1, w, COLOR_DIVIDER);
    tft.setTextDatum(TL_DATUM);
}

void drawTabIcon(Page page, int cx, int cy, uint16_t color) {
    switch (page) {
        case Page::Overview:
            tft.drawRoundRect(cx - 8, cy - 5, 14, 10, 2, color);
            tft.fillRect(cx + 6, cy - 2, 2, 4, color);
            tft.fillRect(cx - 6, cy - 3, 10, 6, color);
            break;
        case Page::Cells:
            tft.fillRect(cx - 7, cy - 2, 3, 7, color);
            tft.fillRect(cx - 2, cy - 6, 3, 11, color);
            tft.fillRect(cx + 3, cy - 4, 3, 9, color);
            break;
        case Page::Status:
            tft.fillTriangle(cx, cy - 7, cx - 7, cy + 5, cx + 7, cy + 5, color);
            tft.fillRect(cx - 1, cy - 4, 2, 5, COLOR_BG);
            tft.fillRect(cx - 1, cy + 2, 2, 2, COLOR_BG);
            break;
        case Page::System:
            tft.drawCircle(cx, cy, 7, color);
            tft.drawCircle(cx, cy, 2, color);
            for (int a = 0; a < 360; a += 45) {
                float rad = a * 3.14159f / 180.0f;
                int x1 = cx + (int)(cosf(rad) * 8);
                int y1 = cy + (int)(sinf(rad) * 8);
                int x2 = cx + (int)(cosf(rad) * 11);
                int y2 = cy + (int)(sinf(rad) * 11);
                tft.drawLine(x1, y1, x2, y2, color);
            }
            break;
    }
}

// ---- page drawing --------------------------------------------------------------------------

// Redraws only the top bar (title, WiFi/IP status, freshness dot+age) - deliberately cheap so it
// can run on a fast heartbeat without the full-screen clear+redraw flash a whole-page redraw would
// cause every couple of seconds.
void drawTopBar(const PaceBmsSnapshot& snapshot) {
    tft.fillRect(0, 0, SCREEN_WIDTH, TOP_BAR_H, COLOR_CARD_ALT);
    tft.setTextDatum(ML_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_TEXT, COLOR_CARD_ALT);
    tft.drawString("PACE BMS", 8, TOP_BAR_H / 2);

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_ALT);
    tft.drawString(WifiManager::statusText(), SCREEN_WIDTH / 2, 3);

    unsigned long ageMs = snapshot.valid ? (millis() - snapshot.lastUpdateMs) : 0xFFFFFFFF;
    bool fresh = snapshot.valid && ageMs < (BMS_POLL_INTERVAL_MS * 3);
    uint16_t dotColor = fresh ? COLOR_OK : COLOR_WARN;

    String ageText = snapshot.valid ? (String(ageMs / 1000) + "s") : "--";
    tft.setTextDatum(MR_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_ALT);
    tft.drawString(ageText, SCREEN_WIDTH - 14, TOP_BAR_H / 2);
    tft.fillCircle(SCREEN_WIDTH - 6, TOP_BAR_H / 2, 4, dotColor);
    tft.setTextDatum(TL_DATUM);
}

void drawTabBar() {
    tft.fillRect(0, SCREEN_HEIGHT - TAB_BAR_H, SCREEN_WIDTH, TAB_BAR_H, COLOR_CARD_ALT);
    const char* labels[TAB_COUNT] = {"Uebersicht", "Zellen", "Status", "System"};
    for (int i = 0; i < TAB_COUNT; i++) {
        int x = i * TAB_W;
        int y = SCREEN_HEIGHT - TAB_BAR_H;
        bool active = (int)currentPage == i;
        uint16_t color = active ? COLOR_ACCENT : COLOR_TEXT_DIM;
        if (active) tft.drawFastHLine(x + 6, y, TAB_W - 12, COLOR_ACCENT);
        drawTabIcon((Page)i, x + TAB_W / 2, y + 11, color);
        tft.setTextDatum(TC_DATUM);
        tft.setTextFont(1);
        tft.setTextColor(color, COLOR_CARD_ALT);
        tft.drawString(labels[i], x + TAB_W / 2, y + 21);
    }
    tft.setTextDatum(TL_DATUM);
}

void drawEmptyState(const char* text) {
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    tft.drawString(text, SCREEN_WIDTH / 2, CONTENT_TOP + CONTENT_H / 2);
    tft.setTextDatum(TL_DATUM);
}

// Cycles selectedPack by dir (+1/-1) through Gesamt(-1) -> Pack 0 -> ... -> Pack packCount-1 ->
// back to Gesamt, wrapping in both directions.
void advancePack(int dir, uint8_t packCount) {
    int total = (int)packCount + 1;  // +1 slot for "Gesamt"
    int current = selectedPack + 1;  // shift so Gesamt=0, pack i -> i+1
    current = ((current + dir) % total + total) % total;
    selectedPack = current - 1;
}

void drawPackBar(uint8_t packCount) {
    String label = selectedPack < 0 ? "Gesamt"
                                     : "Pack " + String(selectedPack + 1) + " von " + String(packCount);
    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    tft.drawString(label, SCREEN_WIDTH / 2, CONTENT_TOP + 4);

    // Small chevrons hinting that this bar (and the page below it) responds to a horizontal swipe.
    int cy = CONTENT_TOP + PACK_BAR_H / 2;
    tft.drawLine(SCREEN_WIDTH / 2 - 46, cy - 4, SCREEN_WIDTH / 2 - 50, cy, COLOR_TEXT_DIM);
    tft.drawLine(SCREEN_WIDTH / 2 - 50, cy, SCREEN_WIDTH / 2 - 46, cy + 4, COLOR_TEXT_DIM);
    tft.drawLine(SCREEN_WIDTH / 2 + 46, cy - 4, SCREEN_WIDTH / 2 + 50, cy, COLOR_TEXT_DIM);
    tft.drawLine(SCREEN_WIDTH / 2 + 50, cy, SCREEN_WIDTH / 2 + 46, cy + 4, COLOR_TEXT_DIM);
    tft.setTextDatum(TL_DATUM);
}

void drawOverview(const PaceBmsSnapshot& snapshot) {
    tft.fillRect(0, CONTENT_TOP, SCREEN_WIDTH, CONTENT_H, COLOR_BG);
    if (snapshot.packCount == 0) {
        drawEmptyState("Keine Paketdaten");
        return;
    }
    drawPackBar(snapshot.packCount);

    bool aggregate = selectedPack < 0 || selectedPack >= snapshot.packCount;
    PacePackAnalog agg;
    String warnText;
    float energyKwh = 0;

    if (aggregate) {
        float voltageSum = 0, currentSum = 0;
        uint32_t remainSum = 0, fullSum = 0, designSum = 0;
        for (uint8_t i = 0; i < snapshot.packCount; i++) {
            const PacePackAnalog& p = snapshot.packs[i];
            voltageSum += p.packVoltageV;
            currentSum += p.packCurrentA;
            remainSum += p.remainingCapacityMah;
            fullSum += p.fullCapacityMah;
            designSum += p.designCapacityMah;
            if (snapshot.warn[i].warnings.length() > 0) {
                if (warnText.length() > 0) warnText += " | ";
                warnText += "Pack " + String(i + 1) + ": " + snapshot.warn[i].warnings;
            }
        }
        agg.packVoltageV = snapshot.packCount > 0 ? voltageSum / snapshot.packCount : 0;
        agg.packCurrentA = currentSum;
        agg.remainingCapacityMah = remainSum;
        agg.fullCapacityMah = fullSum;
        agg.socPercent = fullSum > 0 ? (remainSum * 100.0f) / fullSum : 0;
        agg.sohPercent = designSum > 0 ? (fullSum * 100.0f) / designSum : 0;
    }

    const PacePackAnalog& pack = aggregate ? agg : snapshot.packs[selectedPack];
    if (!aggregate) warnText = snapshot.warn[selectedPack].warnings;
    energyKwh = pack.packVoltageV * (pack.remainingCapacityMah / 1000.0f) / 1000.0f;  // V * Ah / 1000

    bool hasWarning = warnText.length() > 0;
    int bannerH = hasWarning ? 22 : 0;
    int mainH = PAGE_H - bannerH;

    // Left column: battery icon + big SOC readout.
    int leftW = 128;
    int iconY = PAGE_TOP + 14;
    drawBatteryIcon(12, iconY, leftW - 24, 40, pack.socPercent);

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(7);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    String socInt = String((int)(pack.socPercent + 0.5f));
    tft.drawString(socInt, 12 + (leftW - 24) / 2 - 8, iconY + 54);
    tft.setTextFont(4);
    tft.setTextDatum(TL_DATUM);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    tft.drawString("%", 12 + (leftW - 24) / 2 + (socInt.length() >= 2 ? 44 : 26), iconY + 60);

    drawCurrentArrow(leftW - 4, iconY + 20, pack.packCurrentA);

    // Right column: stat rows. Aggregate has no meaningful "Zyklen" (cycle counts don't sum
    // across packs), so it gets one fewer row than an individual pack's view.
    int rightX = leftW + 4;
    int rightW = SCREEN_WIDTH - rightX - 8;
    int rowCount = aggregate ? 4 : 5;
    int rowH = mainH / rowCount;
    char buf[24];
    int row = 0;

    snprintf(buf, sizeof(buf), "%.2f V", pack.packVoltageV);
    drawStatRow(rightX, PAGE_TOP + row * rowH, rightW, rowH, aggregate ? "Bus-Spannung" : "Spannung",
                buf, true);
    row++;

    snprintf(buf, sizeof(buf), "%+.2f A", pack.packCurrentA);
    drawStatRow(rightX, PAGE_TOP + row * rowH, rightW, rowH, "Strom", buf, true);
    row++;

    snprintf(buf, sizeof(buf), "%.0f %%", pack.sohPercent);
    drawStatRow(rightX, PAGE_TOP + row * rowH, rightW, rowH, "SOH", buf, true);
    row++;

    if (!aggregate) {
        snprintf(buf, sizeof(buf), "%u", pack.cycles);
        drawStatRow(rightX, PAGE_TOP + row * rowH, rightW, rowH, "Zyklen", buf, true);
        row++;
    }

    snprintf(buf, sizeof(buf), "%.2f kWh", energyKwh);
    drawStatRow(rightX, PAGE_TOP + row * rowH, rightW, rowH, "Energie", buf, false);

    if (hasWarning) {
        int by = CONTENT_BOTTOM - bannerH;
        tft.fillRect(0, by, SCREEN_WIDTH, bannerH, COLOR_WARN_DIM);
        tft.setTextDatum(ML_DATUM);
        tft.setTextFont(2);
        tft.setTextColor(COLOR_WARN, COLOR_WARN_DIM);
        String msg = warnText;
        if (msg.length() > 40) msg = msg.substring(0, 37) + "...";
        tft.drawString(msg, 6, by + bannerH / 2);
        tft.setTextDatum(TL_DATUM);
    }
}

void drawCells(const PaceBmsSnapshot& snapshot) {
    tft.fillRect(0, CONTENT_TOP, SCREEN_WIDTH, CONTENT_H, COLOR_BG);
    if (snapshot.packCount == 0) {
        drawEmptyState("Keine Zellendaten");
        return;
    }
    drawPackBar(snapshot.packCount);
    uint8_t idx = (selectedPack >= 0 && selectedPack < snapshot.packCount) ? selectedPack : 0;
    const PacePackAnalog& pack = snapshot.packs[idx];
    if (pack.cellCount == 0) {
        drawEmptyState("Keine Zellendaten");
        return;
    }

    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    char header[48];
    snprintf(header, sizeof(header), "%u Zellen  -  Diff %u mV", pack.cellCount, pack.cellMaxDiffMv);
    tft.drawString(header, 6, PAGE_TOP + 4);

    uint16_t minMv = pack.cellMillivolts[0];
    uint16_t maxMv = pack.cellMillivolts[0];
    for (int i = 1; i < pack.cellCount; i++) {
        if (pack.cellMillivolts[i] < minMv) minMv = pack.cellMillivolts[i];
        if (pack.cellMillivolts[i] > maxMv) maxMv = pack.cellMillivolts[i];
    }

    int gridTop = PAGE_TOP + 22;
    int gridH = CONTENT_BOTTOM - gridTop - 4;
    int cols = 4;
    int rows = (pack.cellCount + cols - 1) / cols;
    int cellW = SCREEN_WIDTH / cols;
    int cellH = gridH / rows;

    for (int i = 0; i < pack.cellCount; i++) {
        int col = i % cols;
        int row = i / cols;
        int x = col * cellW + 3;
        int y = gridTop + row * cellH + 2;
        int w = cellW - 6;
        int h = cellH - 4;

        uint16_t mv = pack.cellMillivolts[i];
        uint16_t cardColor = COLOR_CARD;
        uint16_t accentColor = COLOR_TEXT_DIM;
        if (mv == maxMv && maxMv != minMv) accentColor = COLOR_ACCENT;
        if (mv == minMv && maxMv != minMv) accentColor = COLOR_WARN;

        tft.fillRoundRect(x, y, w, h, 3, cardColor);
        tft.drawRoundRect(x, y, w, h, 3, accentColor);

        tft.setTextDatum(TC_DATUM);
        tft.setTextFont(1);
        tft.setTextColor(COLOR_TEXT_DIM, cardColor);
        tft.drawString("Z" + String(i + 1), x + w / 2, y + 3);

        tft.setTextFont(2);
        tft.setTextColor(COLOR_TEXT, cardColor);
        tft.drawString(String(mv), x + w / 2, y + h / 2 + 1);
    }
    tft.setTextDatum(TL_DATUM);
}

void drawPill(int x, int y, int w, int h, const char* label, bool active) {
    uint16_t bg = active ? COLOR_ACCENT_DIM : COLOR_CARD;
    uint16_t fg = active ? COLOR_OK : COLOR_TEXT_DIM;
    tft.fillRoundRect(x, y, w, h, 4, bg);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(fg, bg);
    tft.drawString(label, x + w / 2, y + h / 2);
    tft.setTextDatum(TL_DATUM);
}

void drawStatus(const PaceBmsSnapshot& snapshot) {
    tft.fillRect(0, CONTENT_TOP, SCREEN_WIDTH, CONTENT_H, COLOR_BG);
    if (snapshot.packCount == 0) {
        drawEmptyState("Keine Statusdaten");
        return;
    }
    drawPackBar(snapshot.packCount);
    uint8_t idx = (selectedPack >= 0 && selectedPack < snapshot.packCount) ? selectedPack : 0;
    const PacePackWarn& warn = snapshot.warn[idx];
    const PacePackAnalog& pack = snapshot.packs[idx];

    int y = PAGE_TOP + 4;
    if (warn.warnings.length() > 0) {
        drawWrappedText(warn.warnings, 6, y, SCREEN_WIDTH - 12, 14, COLOR_WARN, 2);
        y += 32;
    } else {
        tft.setTextFont(2);
        tft.setTextColor(COLOR_OK, COLOR_BG);
        tft.drawString("Keine Warnungen", 6, y);
        y += 18;
    }

    tft.drawFastHLine(0, y, SCREEN_WIDTH, COLOR_DIVIDER);
    y += 6;

    int pillW = (SCREEN_WIDTH - 18) / 3;
    int pillH = 26;
    drawPill(6, y, pillW, pillH, "Laden", warn.chargeFetOn);
    drawPill(6 + pillW + 3, y, pillW, pillH, "Entladen", warn.dischargeFetOn);
    drawPill(6 + 2 * (pillW + 3), y, pillW, pillH, "Netzteil", warn.acInOn);
    y += pillH + 4;
    drawPill(6, y, pillW, pillH, "I-Limit", warn.currentLimitOn);
    drawPill(6 + pillW + 3, y, pillW, pillH, "Balancing",
             warn.balanceState1 != 0 || warn.balanceState2 != 0);
    drawPill(6 + 2 * (pillW + 3), y, pillW, pillH, "Vollgeladen", warn.fullyCharged);
    y += pillH + 8;

    tft.setTextFont(2);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    char buf[64];
    snprintf(buf, sizeof(buf), "Rest %lu mAh   Voll %lu mAh   Design %lu mAh",
             (unsigned long)pack.remainingCapacityMah, (unsigned long)pack.fullCapacityMah,
             (unsigned long)pack.designCapacityMah);
    tft.drawString(buf, 6, y);
}

void drawSystemInfo() {
    tft.fillRect(0, CONTENT_TOP, SCREEN_WIDTH, CONTENT_H, COLOR_BG);

    tft.setTextDatum(TC_DATUM);
    tft.setTextFont(4);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("System", SCREEN_WIDTH / 2, CONTENT_TOP + 6);

    // Top-right, well away from the "Simulation" row at the bottom of this page - two distinct
    // touch targets that are easy to tell apart, so one can't be hit by accident going for the other.
    rebootBtnW = 64;
    rebootBtnH = 20;
    rebootBtnX = SCREEN_WIDTH - rebootBtnW - 8;
    rebootBtnY = CONTENT_TOP + 2;
    tft.fillRoundRect(rebootBtnX, rebootBtnY, rebootBtnW, rebootBtnH, 4, COLOR_WARN_DIM);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(1);
    tft.setTextColor(COLOR_WARN, COLOR_WARN_DIM);
    tft.drawString("Neustart", rebootBtnX + rebootBtnW / 2, rebootBtnY + rebootBtnH / 2);
    tft.setTextDatum(TC_DATUM);

    unsigned long upSec = millis() / 1000;
    unsigned long upH = upSec / 3600;
    unsigned long upM = (upSec % 3600) / 60;
    unsigned long upS = upSec % 60;

    int x = 12;
    int y = CONTENT_TOP + 34;
    int w = SCREEN_WIDTH - 24;
    int rowH = 20;
    char buf[32];

    snprintf(buf, sizeof(buf), "%luh %02lum %02lus", upH, upM, upS);
    drawStatRow(x, y, w, rowH, "Laufzeit", buf, true, 2);
    y += rowH;

    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "%d dBm", WiFi.RSSI());
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    drawStatRow(x, y, w, rowH, "WLAN-Signal", buf, true, 2);
    y += rowH;

    snprintf(buf, sizeof(buf), "%u KB", (unsigned)(ESP.getFreeHeap() / 1024));
    drawStatRow(x, y, w, rowH, "Freier Speicher", buf, true, 2);
    y += rowH;

    snprintf(buf, sizeof(buf), "%s Rev %d", ESP.getChipModel(), ESP.getChipRevision());
    drawStatRow(x, y, w, rowH, "Chip", buf, true, 2);
    y += rowH;

    snprintf(buf, sizeof(buf), "%u MHz", ESP.getCpuFreqMHz());
    drawStatRow(x, y, w, rowH, "CPU-Takt", buf, true, 2);
    y += rowH;

    snprintf(buf, sizeof(buf), "%u MB", (unsigned)(ESP.getFlashChipSize() / (1024 * 1024)));
    drawStatRow(x, y, w, rowH, "Flash", buf, true, 2);
    y += rowH;

    // Tappable row - background tint marks it as interactive, unlike the read-only rows above.
    simRowY = y;
    simRowH = rowH;
    bool simOn = RuntimeSettings::simulateBmsData();
    tft.fillRect(x, y, w, rowH, COLOR_CARD);
    drawStatRow(x, y, w, rowH, "Simulation (tippen)", simOn ? "AN" : "AUS", false, 2);
}

void draw(const PaceBmsSnapshot& snapshot) {
    drawTopBar(snapshot);
    switch (currentPage) {
        case Page::Overview: drawOverview(snapshot); break;
        case Page::Cells: drawCells(snapshot); break;
        case Page::Status: drawStatus(snapshot); break;
        case Page::System: drawSystemInfo(); break;
    }
    drawTabBar();
}

// Returns true if handling the touch requires a redraw.
bool handleTouch(int x, int y) {
    if (y >= SCREEN_HEIGHT - TAB_BAR_H) {
        int tab = x / TAB_W;
        if (tab < 0) tab = 0;
        if (tab >= TAB_COUNT) tab = TAB_COUNT - 1;
        Page newPage = (Page)tab;
        if (newPage != currentPage) {
            currentPage = newPage;
            return true;
        }
        return false;
    }

    if (currentPage == Page::System) {
        if (rebootBtnW > 0 && x >= rebootBtnX && x < rebootBtnX + rebootBtnW && y >= rebootBtnY &&
            y < rebootBtnY + rebootBtnH) {
            tft.fillScreen(COLOR_BG);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(COLOR_TEXT, COLOR_BG);
            tft.setTextFont(2);
            tft.drawString("Neustart...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
            tft.setTextDatum(TL_DATUM);
            delay(500);
            ESP.restart();
        }

        if (simRowH > 0 && y >= simRowY && y < simRowY + simRowH) {
            bool newValue = !RuntimeSettings::simulateBmsData();
            RuntimeSettings::setSimulateBmsData(newValue);
            tft.fillScreen(COLOR_BG);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(COLOR_TEXT, COLOR_BG);
            tft.setTextFont(2);
            tft.drawString(String("Simulation ") + (newValue ? "AN" : "AUS") + " - Neustart...",
                            SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
            tft.setTextDatum(TL_DATUM);
            delay(800);
            ESP.restart();
        }
    }

    return false;
}

// Horizontal swipe anywhere in the content area of Overview/Cells/Status switches the selected
// pack (Gesamt <-> Pack 1 <-> Pack 2 <-> ...). Tracked continuously while touched (not just on
// release) so it feels immediate; a vertical drift past SWIPE_MAX_VERTICAL_PX cancels tracking so
// an accidental diagonal drag doesn't also fire a pack change. Returns true if a swipe just fired.
bool handleSwipeTracking(bool touchedNow, bool risingEdge, int16_t tx, int16_t ty,
                          uint8_t packCount) {
    if (!touchedNow) {
        swipeTracking = false;
        return false;
    }

    bool swipeable = currentPage == Page::Overview || currentPage == Page::Cells ||
                      currentPage == Page::Status;
    bool inContentArea = ty < SCREEN_HEIGHT - TAB_BAR_H;

    if (risingEdge) {
        swipeTracking = swipeable && inContentArea;
        swipeConsumed = false;
        swipeStartX = tx;
        swipeStartY = ty;
        return false;
    }

    if (!swipeTracking || swipeConsumed) return false;

    int dx = tx - swipeStartX;
    if (abs((int)ty - (int)swipeStartY) > SWIPE_MAX_VERTICAL_PX) {
        swipeTracking = false;  // too much vertical drift - probably not a horizontal swipe
        return false;
    }

    if (dx <= -SWIPE_THRESHOLD_PX) {
        advancePack(+1, packCount);
        swipeConsumed = true;
        return true;
    }
    if (dx >= SWIPE_THRESHOLD_PX) {
        advancePack(-1, packCount);
        swipeConsumed = true;
        return true;
    }
    return false;
}

}  // namespace

void begin() {
    tft.fillScreen(COLOR_BG);
}

void update(const PaceBmsSnapshot& snapshot) {
    bool rawTouched = ts.touched();
    bool calibrationTriggered = TouchCalibrationRoutine::instance().pollLocalTrigger(rawTouched);

    int16_t tx = 0, ty = 0;
    bool touchedNow = getCalibratedTouch(tx, ty);
    bool risingEdge = touchedNow && !lastTouchedState;
    lastTouchedState = touchedNow;

    bool needRedraw = calibrationTriggered || !everDrawn;
    if (risingEdge) needRedraw = handleTouch(tx, ty) || needRedraw;
    if (handleSwipeTracking(touchedNow, risingEdge, tx, ty, snapshot.packCount)) needRedraw = true;
    if (snapshot.lastUpdateMs != lastSeenUpdateMs) {
        lastSeenUpdateMs = snapshot.lastUpdateMs;
        needRedraw = true;
    }
    unsigned long now = millis();

    if (needRedraw) {
        draw(snapshot);
        lastDrawMs = now;
        lastTopBarMs = now;
        everDrawn = true;
    } else if (now - lastTopBarMs > 1000) {
        // Keeps the age readout / WiFi status / freshness dot live without the full-screen
        // clear+redraw a whole-page refresh would cause (that was visible as a flash every couple
        // of seconds) - the top bar alone is a small, cheap redraw.
        drawTopBar(snapshot);
        lastTopBarMs = now;
    }
}

}  // namespace BmsDisplayUi
