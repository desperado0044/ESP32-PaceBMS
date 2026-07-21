#include "BmsDisplayUi.h"
#include "DisplayHardware.h"
#include "TouchCalibration.h"
#include "WifiProvisioning.h"
#include "RuntimeSettings.h"
#include "BmsActivity.h"
#include "Config.h"
#include "CredentialsStorage.h"
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
// The six accent/semantic colors below are tuned against TFT_eSPI's actual 8bpp sprite encode/
// decode round-trip (see DISPLAY_USE_SPRITE_BUFFER, Config.h) - not a naive "top 3/3/2 bits" RGB332
// assumption, which turned out wrong: its 2-bit blue channel is a small non-linear lookup table
// {0,11,21,31} with only 4 possible outputs (0/90/173/255 once expanded to 8-bit), and a mid-range
// blue input (e.g. 64-176) can round back up to a much brighter value than expected, washing out
// any colour that isn't itself blue-dominant. Verified by simulating the actual encode/decode bit
// math for every candidate rather than by inspection. Each colour below deliberately lands its blue
// component in the *bottom* bucket (0-48, decodes to 0) if it has no business being blue at all
// (AMBER, WARN_DIM, ACCENT_DIM, OK), or the *top* bucket (192-255, decodes to 255) if it's supposed
// to read as a saturated teal/cyan (ACCENT) - the two middle buckets are what caused the washed-out
// look and are deliberately avoided everywhere.
constexpr uint16_t COLOR_ACCENT = rgb565(0, 192, 224);
constexpr uint16_t COLOR_ACCENT_DIM = rgb565(0, 96, 0);
constexpr uint16_t COLOR_AMBER = rgb565(224, 192, 0);
constexpr uint16_t COLOR_WARN = rgb565(255, 96, 64);
constexpr uint16_t COLOR_WARN_DIM = rgb565(64, 32, 0);
constexpr uint16_t COLOR_OK = rgb565(0, 224, 32);
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

// Modbus pack-address config sub-screen (opened from the System tab) - a rarely-used setup screen,
// not a main navigation tab, so it's a full-screen overlay that bypasses the tab bar entirely
// rather than taking up a permanent 5th tab slot.
constexpr int MB_GRID_COLS = 5;
constexpr int MB_CHIP_W = 54;
constexpr int MB_CHIP_H = 32;
constexpr int MB_GAP_X = 8;
constexpr int MB_GAP_Y = 8;
constexpr int MB_GRID_TOP = 40;
constexpr int MB_ADDR_COUNT = 16;  // dip-switch addresses 0-15
constexpr int MB_GRID_LEFT =
    (SCREEN_WIDTH - (MB_GRID_COLS * MB_CHIP_W + (MB_GRID_COLS - 1) * MB_GAP_X)) / 2;
constexpr int MB_BACK_X = 4, MB_BACK_Y = 4, MB_BACK_W = 70, MB_BACK_H = 22;
constexpr int MB_SAVE_W = 200, MB_SAVE_H = 30;
constexpr int MB_SAVE_X = (SCREEN_WIDTH - MB_SAVE_W) / 2;
constexpr int MB_SAVE_Y = SCREEN_HEIGHT - MB_SAVE_H - 8;

enum class Page { Overview = 0, Cells = 1, Status = 2, System = 3 };

Page currentPage = Page::Overview;
bool lastTouchedState = false;
unsigned long lastDrawMs = 0;
unsigned long lastActivityDotMs = 0;
unsigned long lastSeenUpdateMs = 0;
bool everDrawn = false;

// Off-screen frame buffer (see DISPLAY_USE_SPRITE_BUFFER in Config.h): a full redraw is composed
// here first and pushed to the display in one shot, instead of clearing the real screen and
// redrawing element-by-element directly on it (which left a visible blank flash between the two
// steps). Sized to the whole screen, not just the content area, so every existing coordinate below
// (all already expressed in absolute screen space) works unchanged whether gfx is the sprite or
// the real tft. Falls back to drawing straight on tft if the allocation fails at boot.
TFT_eSprite frameSprite = TFT_eSprite(&tft);
bool spriteReady = false;

// -1 = "Gesamt" (aggregate across all packs), 0..packCount-1 = a single pack. Shared across
// Overview/Cells/Status so picking a pack on one carries over to the others.
int selectedPack = -1;
bool swipeTracking = false;
bool swipeConsumed = false;
int16_t swipeStartX = 0;
int16_t swipeStartY = 0;

// Bounds of the System tab's three config buttons (BMS-Anschluss, Modbus-Konfig, Simulation),
// sitting side by side in one row - set each time the tab is drawn.
int protocolRowX = 0, protocolRowY = 0, protocolRowW = 0, protocolRowH = 0;
int modbusBtnRowX = 0, modbusBtnRowY = 0, modbusBtnRowW = 0, modbusBtnRowH = 0;
int simRowX = 0, simRowY = 0, simRowW = 0, simRowH = 0;

// Bounds of the "Neustart" button on the System tab - placed top-right, away from the config
// button row below, so the two touch targets can't be mixed up.
int rebootBtnX = 0, rebootBtnY = 0, rebootBtnW = 0, rebootBtnH = 0;

// Modbus pack-address config overlay: open/closed, and the bitmask being edited (only written to
// RuntimeSettings - and only then triggers a reboot - once "Speichern" is tapped, so ticking 15
// boxes doesn't mean 15 reboots).
bool modbusConfigOpen = false;
uint16_t modbusConfigEditMask = 0;

void modbusChipBounds(int index, int& cx, int& cy, int& cw, int& ch) {
    int col = index % MB_GRID_COLS;
    int row = index / MB_GRID_COLS;
    cx = MB_GRID_LEFT + col * (MB_CHIP_W + MB_GAP_X);
    cy = MB_GRID_TOP + row * (MB_CHIP_H + MB_GAP_Y);
    cw = MB_CHIP_W;
    ch = MB_CHIP_H;
}

// Hit-testing uses the full column/row pitch (chip + gap), not just the visually drawn chip
// rectangle - on a small resistive touchscreen the gap between chips is dead space that only makes
// a dense grid harder to hit, not clearer to read, so a touch anywhere between two chips resolves
// to whichever one it's closer to instead of missing both. Returns -1 for no hit.
int modbusChipIndexAt(int x, int y) {
    constexpr int colPitch = MB_CHIP_W + MB_GAP_X;
    constexpr int rowPitch = MB_CHIP_H + MB_GAP_Y;
    int relX = x - (MB_GRID_LEFT - MB_GAP_X / 2);
    int relY = y - (MB_GRID_TOP - MB_GAP_Y / 2);
    if (relX < 0 || relY < 0) return -1;
    int col = relX / colPitch;
    int row = relY / rowPitch;
    if (col < 0 || col >= MB_GRID_COLS) return -1;
    constexpr int rows = (MB_ADDR_COUNT + MB_GRID_COLS - 1) / MB_GRID_COLS;
    if (row < 0 || row >= rows) return -1;
    int index = row * MB_GRID_COLS + col;
    return index < MB_ADDR_COUNT ? index : -1;
}

// ---- low-level drawing helpers -----------------------------------------------------------
// All take a `gfx` reference (either the real `tft` or `frameSprite`, both TFT_eSPI-derived and
// sharing the same drawing API) so a full redraw can be composed off-screen and pushed in one
// shot - see DISPLAY_USE_SPRITE_BUFFER above.

void drawWrappedText(TFT_eSPI& gfx, const String& text, int x, int y, int maxWidth, int lineHeight,
                      uint16_t color, uint8_t font) {
    gfx.setTextFont(font);
    gfx.setTextColor(color, COLOR_BG);
    gfx.setTextDatum(TL_DATUM);

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
            if (gfx.textWidth(candidate) > maxWidth && cursor > start) {
                end = (lastSpace > start) ? lastSpace : cursor;
                break;
            }
            end = cursor + 1;
            cursor++;
        }
        String lineText = text.substring(start, end);
        lineText.trim();
        gfx.drawString(lineText, x, y + line * lineHeight);
        start = end;
        while (start < len && text[start] == ' ') start++;
        line++;
    }
}

void drawBatteryIcon(TFT_eSPI& gfx, int x, int y, int w, int h, float socPercent) {
    int nubW = w / 8;
    int nubH = h / 2;
    int bodyW = w - nubW - 2;

    gfx.drawRoundRect(x, y, bodyW, h, 4, COLOR_TEXT_DIM);
    gfx.fillRoundRect(x + bodyW + 2, y + (h - nubH) / 2, nubW, nubH, 2, COLOR_TEXT_DIM);

    int pad = 3;
    float clamped = socPercent < 0 ? 0 : (socPercent > 100 ? 100 : socPercent);
    int fillW = (int)((bodyW - 2 * pad) * (clamped / 100.0f));
    uint16_t fillColor = clamped > 60 ? COLOR_OK : (clamped > 25 ? COLOR_AMBER : COLOR_WARN);
    if (fillW > 0) gfx.fillRoundRect(x + pad, y + pad, fillW, h - 2 * pad, 2, fillColor);
}

// Small triangular current-direction indicator: up = charging (current > 0), down = discharging.
void drawCurrentArrow(TFT_eSPI& gfx, int cx, int cy, float currentA) {
    if (fabsf(currentA) < 0.05f) return;
    uint16_t color = currentA > 0 ? COLOR_OK : COLOR_AMBER;
    if (currentA > 0) {
        gfx.fillTriangle(cx, cy - 6, cx - 6, cy + 5, cx + 6, cy + 5, color);
    } else {
        gfx.fillTriangle(cx, cy + 6, cx - 6, cy - 5, cx + 6, cy - 5, color);
    }
}

void drawStatRow(TFT_eSPI& gfx, int x, int y, int w, int h, const char* label, const String& value,
                  bool divider, uint8_t valueFont = 4) {
    // The label (always font 2) and value (font 2 or 4, caller's choice) only share a vertical
    // center when they're offset to match - the -8/+2 pair below was tuned for a font-4 value
    // sitting next to a font-2 label; when the value is font 2 too (System tab rows), both need
    // the same offset (0) or the label visibly sits higher than the value.
    int labelYOffset = valueFont >= 4 ? -8 : 0;
    gfx.setTextDatum(ML_DATUM);
    gfx.setTextFont(2);
    gfx.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    gfx.drawString(label, x, y + h / 2 + labelYOffset);

    gfx.setTextDatum(MR_DATUM);
    gfx.setTextFont(valueFont);
    gfx.setTextColor(COLOR_TEXT, COLOR_BG);
    gfx.drawString(value, x + w, y + h / 2 + (valueFont >= 4 ? 2 : 0));

    if (divider) gfx.drawFastHLine(x, y + h - 1, w, COLOR_DIVIDER);
    gfx.setTextDatum(TL_DATUM);
}

void drawTabIcon(TFT_eSPI& gfx, Page page, int cx, int cy, uint16_t color) {
    switch (page) {
        case Page::Overview:
            gfx.drawRoundRect(cx - 8, cy - 5, 14, 10, 2, color);
            gfx.fillRect(cx + 6, cy - 2, 2, 4, color);
            gfx.fillRect(cx - 6, cy - 3, 10, 6, color);
            break;
        case Page::Cells:
            gfx.fillRect(cx - 7, cy - 2, 3, 7, color);
            gfx.fillRect(cx - 2, cy - 6, 3, 11, color);
            gfx.fillRect(cx + 3, cy - 4, 3, 9, color);
            break;
        case Page::Status:
            gfx.fillTriangle(cx, cy - 7, cx - 7, cy + 5, cx + 7, cy + 5, color);
            gfx.fillRect(cx - 1, cy - 4, 2, 5, COLOR_BG);
            gfx.fillRect(cx - 1, cy + 2, 2, 2, COLOR_BG);
            break;
        case Page::System:
            gfx.drawCircle(cx, cy, 7, color);
            gfx.drawCircle(cx, cy, 2, color);
            for (int a = 0; a < 360; a += 45) {
                float rad = a * 3.14159f / 180.0f;
                int x1 = cx + (int)(cosf(rad) * 8);
                int y1 = cy + (int)(sinf(rad) * 8);
                int x2 = cx + (int)(cosf(rad) * 11);
                int y2 = cy + (int)(sinf(rad) * 11);
                gfx.drawLine(x1, y1, x2, y2, color);
            }
            break;
    }
}

// ---- page drawing --------------------------------------------------------------------------

// Activity dot in the top-right corner - the only part of the top bar that changes on its own, so
// it clears only its own small zone first, not the whole bar (that used to clear+redraw the entire
// top bar every second just for this, which visibly flickered the static IP/WiFi text next to it
// for no reason). Replaced the old numeric "age in seconds" readout with a colour that reflects
// the actual request/response cycle instead, per user request - a number added nothing a glance at
// the colour doesn't already say faster:
//   grey    = idle, waiting for the next poll cycle (plain dot)
//   red     = a request just went out, still waiting on a reply (plain dot)
//   green   = a reply just came back, brief flash (plain dot)
//   amber   = no reply in a long time - BMS not answering (WLAN down, pack disconnected, etc.) -
//             drawn as a small warning triangle instead of a dot, so this one specifically doesn't
//             read as just another colour among the other three (same shape language as the
//             Status tab's icon, see drawTabIcon)
constexpr int FRESH_ZONE_X = SCREEN_WIDTH - 24;
constexpr int FRESH_ZONE_W = 24;
constexpr unsigned long FRESH_FLASH_HOLD_MS = 300;

void drawFreshnessIndicator(TFT_eSPI& gfx) {
    gfx.fillRect(FRESH_ZONE_X, 0, FRESH_ZONE_W, TOP_BAR_H, COLOR_CARD_ALT);

    unsigned long now = millis();
    unsigned long reqMs = BmsActivity::lastRequestMs();
    unsigned long respMs = BmsActivity::lastResponseMs();
    unsigned long staleThresholdMs = RuntimeSettings::bmsPollIntervalMs() * 3;

    int cx = SCREEN_WIDTH - 12;
    int cy = TOP_BAR_H / 2;

    if (respMs == 0 || now - respMs > staleThresholdMs) {
        gfx.fillTriangle(cx, cy - 6, cx - 6, cy + 5, cx + 6, cy + 5, COLOR_AMBER);
        gfx.fillRect(cx - 1, cy - 3, 2, 4, COLOR_CARD_ALT);
        gfx.fillRect(cx - 1, cy + 2, 2, 2, COLOR_CARD_ALT);
        return;
    }

    uint16_t dotColor;
    if (reqMs > respMs) {
        dotColor = COLOR_WARN;
    } else if (now - respMs < FRESH_FLASH_HOLD_MS) {
        dotColor = COLOR_OK;
    } else {
        dotColor = COLOR_TEXT_DIM;
    }

    gfx.fillCircle(cx, cy, 4, dotColor);
}

// Full top bar (title, WiFi/IP status, activity dot) - only needed once per full page redraw, not
// on the fast heartbeat (see drawFreshnessIndicator above for that).
void drawTopBar(TFT_eSPI& gfx) {
    gfx.fillRect(0, 0, SCREEN_WIDTH, TOP_BAR_H, COLOR_CARD_ALT);
    gfx.setTextDatum(ML_DATUM);
    gfx.setTextFont(2);
    gfx.setTextColor(COLOR_TEXT, COLOR_CARD_ALT);
    gfx.drawString(CredentialsManager::instance().getHostname(), 8, TOP_BAR_H / 2);

    gfx.setTextDatum(TC_DATUM);
    gfx.setTextFont(1);
    gfx.setTextColor(COLOR_TEXT_DIM, COLOR_CARD_ALT);
    gfx.drawString(WifiManager::statusText(), SCREEN_WIDTH / 2, 3);

    drawFreshnessIndicator(gfx);
}

void drawTabBar(TFT_eSPI& gfx) {
    gfx.fillRect(0, SCREEN_HEIGHT - TAB_BAR_H, SCREEN_WIDTH, TAB_BAR_H, COLOR_CARD_ALT);
    const char* labels[TAB_COUNT] = {"Uebersicht", "Zellen", "Status", "System"};
    for (int i = 0; i < TAB_COUNT; i++) {
        int x = i * TAB_W;
        int y = SCREEN_HEIGHT - TAB_BAR_H;
        bool active = (int)currentPage == i;
        uint16_t color = active ? COLOR_ACCENT : COLOR_TEXT_DIM;
        if (active) gfx.drawFastHLine(x + 6, y, TAB_W - 12, COLOR_ACCENT);
        drawTabIcon(gfx, (Page)i, x + TAB_W / 2, y + 11, color);
        gfx.setTextDatum(TC_DATUM);
        gfx.setTextFont(1);
        gfx.setTextColor(color, COLOR_CARD_ALT);
        gfx.drawString(labels[i], x + TAB_W / 2, y + 21);
    }
    gfx.setTextDatum(TL_DATUM);
}

void drawEmptyState(TFT_eSPI& gfx, const char* text) {
    gfx.setTextDatum(MC_DATUM);
    gfx.setTextFont(2);
    gfx.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    gfx.drawString(text, SCREEN_WIDTH / 2, CONTENT_TOP + CONTENT_H / 2);
    gfx.setTextDatum(TL_DATUM);
}

// Cycles selectedPack by dir (+1/-1) through Gesamt(-1) -> Pack 0 -> ... -> Pack packCount-1 ->
// back to Gesamt, wrapping in both directions.
void advancePack(int dir, uint8_t packCount) {
    int total = (int)packCount + 1;  // +1 slot for "Gesamt"
    int current = selectedPack + 1;  // shift so Gesamt=0, pack i -> i+1
    current = ((current + dir) % total + total) % total;
    selectedPack = current - 1;
}

void drawPackBar(TFT_eSPI& gfx, const PaceBmsSnapshot& snapshot) {
    String label = selectedPack < 0 || selectedPack >= snapshot.packCount
                       ? "Gesamt"
                       : "Pack " + String(snapshot.packAddress[selectedPack]) + " von " +
                             String(snapshot.packCount);
    gfx.setTextDatum(TC_DATUM);
    gfx.setTextFont(1);
    gfx.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    gfx.drawString(label, SCREEN_WIDTH / 2, CONTENT_TOP + 4);

    // Small chevrons hinting that this bar (and the page below it) responds to a horizontal swipe.
    int cy = CONTENT_TOP + PACK_BAR_H / 2;
    gfx.drawLine(SCREEN_WIDTH / 2 - 46, cy - 4, SCREEN_WIDTH / 2 - 50, cy, COLOR_TEXT_DIM);
    gfx.drawLine(SCREEN_WIDTH / 2 - 50, cy, SCREEN_WIDTH / 2 - 46, cy + 4, COLOR_TEXT_DIM);
    gfx.drawLine(SCREEN_WIDTH / 2 + 46, cy - 4, SCREEN_WIDTH / 2 + 50, cy, COLOR_TEXT_DIM);
    gfx.drawLine(SCREEN_WIDTH / 2 + 50, cy, SCREEN_WIDTH / 2 + 46, cy + 4, COLOR_TEXT_DIM);
    gfx.setTextDatum(TL_DATUM);
}

void drawOverview(TFT_eSPI& gfx, const PaceBmsSnapshot& snapshot) {
    gfx.fillRect(0, CONTENT_TOP, SCREEN_WIDTH, CONTENT_H, COLOR_BG);
    if (snapshot.packCount == 0) {
        drawEmptyState(gfx, "Keine Paketdaten");
        return;
    }
    drawPackBar(gfx, snapshot);

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
            agg.cellCount = p.cellCount;  // packs are the same model in practice; last one wins
            if (snapshot.warn[i].warnings.length() > 0) {
                if (warnText.length() > 0) warnText += " | ";
                warnText += "Pack " + String(snapshot.packAddress[i]) + ": " + snapshot.warn[i].warnings;
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
    // LiFePO4's flat discharge curve makes the measured voltage a good stand-in for the average
    // voltage over the remaining discharge in most of the SOC range - but right after a full
    // charge it briefly overshoots above the nominal per-cell voltage (surface charge), which would
    // overstate remaining energy. Capping at nominal (not replacing the measurement outright) fixes
    // that one case while keeping the more accurate real reading everywhere else, including near
    // empty where voltage sags below nominal.
    constexpr float kLfpNominalCellVoltage = 3.2f;
    float nominalPackVoltage = kLfpNominalCellVoltage * pack.cellCount;
    float energyVoltage = pack.cellCount > 0 ? min(pack.packVoltageV, nominalPackVoltage) : pack.packVoltageV;
    energyKwh = energyVoltage * (pack.remainingCapacityMah / 1000.0f) / 1000.0f;  // V * Ah / 1000

    bool hasWarning = warnText.length() > 0;
    // Always reserved, whether or not a warning is currently showing - if this shrank/grew with
    // hasWarning, every stat row would visibly jump size each time a warning appeared/disappeared.
    constexpr int bannerH = 22;
    int mainH = PAGE_H - bannerH;

    // Left column: battery icon + big SOC readout.
    int leftW = 128;
    int iconY = PAGE_TOP + 14;
    drawBatteryIcon(gfx, 12, iconY, leftW - 24, 40, pack.socPercent);

    gfx.setTextDatum(TC_DATUM);
    gfx.setTextFont(7);
    gfx.setTextColor(COLOR_TEXT, COLOR_BG);
    String socInt = String((int)(pack.socPercent + 0.5f));
    gfx.drawString(socInt, 12 + (leftW - 24) / 2 - 8, iconY + 54);
    gfx.setTextFont(4);
    gfx.setTextDatum(TL_DATUM);
    gfx.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    gfx.drawString("%", 12 + (leftW - 24) / 2 + (socInt.length() >= 2 ? 44 : 26), iconY + 60);

    drawCurrentArrow(gfx, leftW - 4, iconY + 20, pack.packCurrentA);

    // Right column: stat rows. Aggregate has no meaningful "Zyklen" (cycle counts don't sum
    // across packs), so it gets one fewer row than an individual pack's view.
    int rightX = leftW + 4;
    int rightW = SCREEN_WIDTH - rightX - 8;
    int rowCount = aggregate ? 4 : 5;
    int rowH = mainH / rowCount;
    char buf[24];
    int row = 0;

    snprintf(buf, sizeof(buf), "%.2f V", pack.packVoltageV);
    drawStatRow(gfx, rightX, PAGE_TOP + row * rowH, rightW, rowH,
                aggregate ? "Bus-Spannung" : "Spannung", buf, true);
    row++;

    snprintf(buf, sizeof(buf), "%+.2f A", pack.packCurrentA);
    drawStatRow(gfx, rightX, PAGE_TOP + row * rowH, rightW, rowH, "Strom", buf, true);
    row++;

    snprintf(buf, sizeof(buf), "%.0f %%", pack.sohPercent);
    drawStatRow(gfx, rightX, PAGE_TOP + row * rowH, rightW, rowH, "SOH", buf, true);
    row++;

    if (!aggregate) {
        snprintf(buf, sizeof(buf), "%u", pack.cycles);
        drawStatRow(gfx, rightX, PAGE_TOP + row * rowH, rightW, rowH, "Zyklen", buf, true);
        row++;
    }

    snprintf(buf, sizeof(buf), "%.2f kWh", energyKwh);
    drawStatRow(gfx, rightX, PAGE_TOP + row * rowH, rightW, rowH, "Energie", buf, false);

    if (hasWarning) {
        int by = CONTENT_BOTTOM - bannerH;
        gfx.fillRect(0, by, SCREEN_WIDTH, bannerH, COLOR_WARN_DIM);
        gfx.setTextDatum(ML_DATUM);
        gfx.setTextFont(2);
        gfx.setTextColor(COLOR_WARN, COLOR_WARN_DIM);
        String msg = warnText;
        if (msg.length() > 40) msg = msg.substring(0, 37) + "...";
        gfx.drawString(msg, 6, by + bannerH / 2);
        gfx.setTextDatum(TL_DATUM);
    }
}

void drawCells(TFT_eSPI& gfx, const PaceBmsSnapshot& snapshot) {
    gfx.fillRect(0, CONTENT_TOP, SCREEN_WIDTH, CONTENT_H, COLOR_BG);
    if (snapshot.packCount == 0) {
        drawEmptyState(gfx, "Keine Zellendaten");
        return;
    }
    drawPackBar(gfx, snapshot);
    uint8_t idx = (selectedPack >= 0 && selectedPack < snapshot.packCount) ? selectedPack : 0;
    const PacePackAnalog& pack = snapshot.packs[idx];
    if (pack.cellCount == 0) {
        drawEmptyState(gfx, "Keine Zellendaten");
        return;
    }

    gfx.setTextDatum(TL_DATUM);
    gfx.setTextFont(2);
    gfx.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    char header[48];
    snprintf(header, sizeof(header), "%u Zellen  -  Diff %u mV", pack.cellCount, pack.cellMaxDiffMv);
    gfx.drawString(header, 6, PAGE_TOP + 4);

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

        gfx.fillRoundRect(x, y, w, h, 3, cardColor);
        gfx.drawRoundRect(x, y, w, h, 3, accentColor);

        gfx.setTextDatum(TC_DATUM);
        gfx.setTextFont(1);
        gfx.setTextColor(COLOR_TEXT_DIM, cardColor);
        gfx.drawString("Z" + String(i + 1), x + w / 2, y + 3);

        gfx.setTextFont(2);
        gfx.setTextColor(COLOR_TEXT, cardColor);
        gfx.drawString(String(mv), x + w / 2, y + h / 2 + 1);
    }
    gfx.setTextDatum(TL_DATUM);
}

void drawPill(TFT_eSPI& gfx, int x, int y, int w, int h, const char* label, bool active) {
    uint16_t bg = active ? COLOR_ACCENT_DIM : COLOR_CARD;
    uint16_t fg = active ? COLOR_OK : COLOR_TEXT_DIM;
    gfx.fillRoundRect(x, y, w, h, 4, bg);
    gfx.setTextDatum(MC_DATUM);
    gfx.setTextFont(2);
    gfx.setTextColor(fg, bg);
    gfx.drawString(label, x + w / 2, y + h / 2);
    gfx.setTextDatum(TL_DATUM);
}

void drawStatus(TFT_eSPI& gfx, const PaceBmsSnapshot& snapshot) {
    gfx.fillRect(0, CONTENT_TOP, SCREEN_WIDTH, CONTENT_H, COLOR_BG);
    if (snapshot.packCount == 0) {
        drawEmptyState(gfx, "Keine Statusdaten");
        return;
    }
    drawPackBar(gfx, snapshot);
    uint8_t idx = (selectedPack >= 0 && selectedPack < snapshot.packCount) ? selectedPack : 0;
    const PacePackWarn& warn = snapshot.warn[idx];
    const PacePackAnalog& pack = snapshot.packs[idx];

    int y = PAGE_TOP + 4;
    if (warn.warnings.length() > 0) {
        drawWrappedText(gfx, warn.warnings, 6, y, SCREEN_WIDTH - 12, 14, COLOR_WARN, 2);
        y += 32;
    } else {
        gfx.setTextFont(2);
        gfx.setTextColor(COLOR_OK, COLOR_BG);
        gfx.drawString("Keine Warnungen", 6, y);
        y += 18;
    }

    gfx.drawFastHLine(0, y, SCREEN_WIDTH, COLOR_DIVIDER);
    y += 6;

    int pillW = (SCREEN_WIDTH - 18) / 3;
    int pillH = 26;
    drawPill(gfx, 6, y, pillW, pillH, "Laden", warn.chargeFetOn);
    drawPill(gfx, 6 + pillW + 3, y, pillW, pillH, "Entladen", warn.dischargeFetOn);
    drawPill(gfx, 6 + 2 * (pillW + 3), y, pillW, pillH, "Netzteil", warn.acInOn);
    y += pillH + 4;
    drawPill(gfx, 6, y, pillW, pillH, "I-Limit", warn.currentLimitOn);
    drawPill(gfx, 6 + pillW + 3, y, pillW, pillH, "Balancing",
             warn.balanceState1 != 0 || warn.balanceState2 != 0);
    drawPill(gfx, 6 + 2 * (pillW + 3), y, pillW, pillH, "Vollgeladen", warn.fullyCharged);
    y += pillH + 8;

    gfx.setTextFont(2);
    gfx.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    char buf[64];
    snprintf(buf, sizeof(buf), "Rest %lu mAh   Voll %lu mAh   Design %lu mAh",
             (unsigned long)pack.remainingCapacityMah, (unsigned long)pack.fullCapacityMah,
             (unsigned long)pack.designCapacityMah);
    gfx.drawString(buf, 6, y);
}

void drawSystemInfo(TFT_eSPI& gfx) {
    gfx.fillRect(0, CONTENT_TOP, SCREEN_WIDTH, CONTENT_H, COLOR_BG);

    gfx.setTextDatum(TC_DATUM);
    gfx.setTextFont(4);
    gfx.setTextColor(COLOR_TEXT, COLOR_BG);
    gfx.drawString("System", SCREEN_WIDTH / 2, CONTENT_TOP + 6);

    // Top-right, well away from the tappable rows below - two distinct touch targets that are
    // easy to tell apart, so one can't be hit by accident going for the other.
    rebootBtnW = 64;
    rebootBtnH = 20;
    rebootBtnX = SCREEN_WIDTH - rebootBtnW - 8;
    rebootBtnY = CONTENT_TOP + 2;
    gfx.fillRoundRect(rebootBtnX, rebootBtnY, rebootBtnW, rebootBtnH, 4, COLOR_WARN_DIM);
    gfx.setTextDatum(MC_DATUM);
    gfx.setTextFont(1);
    gfx.setTextColor(COLOR_WARN, COLOR_WARN_DIM);
    gfx.drawString("Neustart", rebootBtnX + rebootBtnW / 2, rebootBtnY + rebootBtnH / 2);

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
    drawStatRow(gfx, x, y, w, rowH, "Laufzeit", buf, true, 2);
    y += rowH;

    if (WiFi.status() == WL_CONNECTED) {
        snprintf(buf, sizeof(buf), "%d dBm", WiFi.RSSI());
    } else {
        snprintf(buf, sizeof(buf), "--");
    }
    drawStatRow(gfx, x, y, w, rowH, "WLAN-Signal", buf, true, 2);
    y += rowH;

    snprintf(buf, sizeof(buf), "%u KB", (unsigned)(ESP.getFreeHeap() / 1024));
    drawStatRow(gfx, x, y, w, rowH, "Freier Speicher", buf, true, 2);
    y += rowH;

    // Three real buttons side by side, not stacked list rows - a thin full-width row wedged
    // between others turned out too fiddly to hit reliably on a small resistive touchscreen even
    // with gaps added between rows. Properly sized, clearly bordered, well-separated buttons are a
    // much bigger and more forgiving target.
    constexpr int kBtnGap = 10;
    constexpr int kBtnH = 70;  // plenty of unused space below the read-only rows to grow into
    y += kBtnGap;

    int btnW = (w - 2 * kBtnGap) / 3;

    // Toggle buttons show the *current* state large, and the state a tap switches *to* smaller
    // and in parentheses below - otherwise it's not obvious that tapping "RS232" does anything, let
    // alone that it switches to Modbus specifically.
    bool useModbus = RuntimeSettings::useModbus();
    protocolRowX = x;
    protocolRowY = y;
    protocolRowW = btnW;
    protocolRowH = kBtnH;
    gfx.fillRoundRect(protocolRowX, y, btnW, kBtnH, 6, COLOR_CARD);
    gfx.drawRoundRect(protocolRowX, y, btnW, kBtnH, 6, COLOR_DIVIDER);
    gfx.setTextDatum(MC_DATUM);
    gfx.setTextFont(2);
    gfx.setTextColor(COLOR_TEXT, COLOR_CARD);
    gfx.drawString(useModbus ? "Modbus" : "RS232", protocolRowX + btnW / 2, y + kBtnH / 2 - 12);
    gfx.setTextFont(1);
    gfx.setTextColor(COLOR_TEXT_DIM, COLOR_CARD);
    gfx.drawString(useModbus ? "(RS232)" : "(Modbus)", protocolRowX + btnW / 2, y + kBtnH / 2 + 14);

    modbusBtnRowX = protocolRowX + btnW + kBtnGap;
    modbusBtnRowY = y;
    modbusBtnRowW = btnW;
    modbusBtnRowH = kBtnH;
    gfx.fillRoundRect(modbusBtnRowX, y, btnW, kBtnH, 6, COLOR_ACCENT_DIM);
    gfx.drawRoundRect(modbusBtnRowX, y, btnW, kBtnH, 6, COLOR_ACCENT);
    gfx.setTextFont(2);
    gfx.setTextColor(COLOR_OK, COLOR_ACCENT_DIM);
    gfx.drawString("Modbus", modbusBtnRowX + btnW / 2, y + kBtnH / 2 - 12);
    gfx.setTextFont(1);
    gfx.drawString("Konfig", modbusBtnRowX + btnW / 2, y + kBtnH / 2 + 14);

    simRowX = modbusBtnRowX + btnW + kBtnGap;
    simRowY = y;
    simRowW = x + w - simRowX;  // fill remaining width exactly, absorbs any rounding remainder
    simRowH = kBtnH;
    bool simOn = RuntimeSettings::simulateBmsData();
    gfx.fillRoundRect(simRowX, y, simRowW, kBtnH, 6, COLOR_CARD);
    gfx.drawRoundRect(simRowX, y, simRowW, kBtnH, 6, COLOR_DIVIDER);
    gfx.setTextFont(2);
    gfx.setTextColor(COLOR_TEXT, COLOR_CARD);
    gfx.drawString(simOn ? "SIM: AN" : "SIM: AUS", simRowX + simRowW / 2, y + kBtnH / 2 - 12);
    gfx.setTextFont(1);
    gfx.setTextColor(COLOR_TEXT_DIM, COLOR_CARD);
    gfx.drawString(simOn ? "(AUS)" : "(AN)", simRowX + simRowW / 2, y + kBtnH / 2 + 14);
    gfx.setTextDatum(TC_DATUM);
}

// Not sprite-buffered (draws straight to tft) - a rarely-used setup screen, not the periodic
// redraw the sprite buffer was added for.
void drawModbusConfig() {
    tft.fillScreen(COLOR_BG);

    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_TEXT_DIM, COLOR_BG);
    tft.drawString("< Zurueck", MB_BACK_X + 4, MB_BACK_Y + 3);

    tft.setTextDatum(TC_DATUM);
    tft.setTextColor(COLOR_TEXT, COLOR_BG);
    tft.drawString("Modbus Pack-Adressen", SCREEN_WIDTH / 2, MB_BACK_Y + 3);

    for (int i = 0; i < MB_ADDR_COUNT; i++) {
        int cx, cy, cw, ch;
        modbusChipBounds(i, cx, cy, cw, ch);
        bool on = modbusConfigEditMask & (1u << i);
        uint16_t bg = on ? COLOR_ACCENT_DIM : COLOR_CARD;
        uint16_t fg = on ? COLOR_OK : COLOR_TEXT_DIM;
        tft.fillRoundRect(cx, cy, cw, ch, 4, bg);
        tft.drawRoundRect(cx, cy, cw, ch, 4, on ? COLOR_ACCENT : COLOR_DIVIDER);
        tft.setTextDatum(MC_DATUM);
        tft.setTextFont(2);
        tft.setTextColor(fg, bg);
        tft.drawString(String(i), cx + cw / 2, cy + ch / 2);
    }

    tft.fillRoundRect(MB_SAVE_X, MB_SAVE_Y, MB_SAVE_W, MB_SAVE_H, 5, COLOR_ACCENT);
    tft.setTextDatum(MC_DATUM);
    tft.setTextFont(2);
    tft.setTextColor(COLOR_BG, COLOR_ACCENT);
    tft.drawString("Speichern & Neustart", MB_SAVE_X + MB_SAVE_W / 2, MB_SAVE_Y + MB_SAVE_H / 2);
    tft.setTextDatum(TL_DATUM);
}

void draw(const PaceBmsSnapshot& snapshot) {
    if (modbusConfigOpen) {
        drawModbusConfig();
        return;
    }

    TFT_eSPI& gfx = spriteReady ? static_cast<TFT_eSPI&>(frameSprite) : tft;
    drawTopBar(gfx);
    switch (currentPage) {
        case Page::Overview: drawOverview(gfx, snapshot); break;
        case Page::Cells: drawCells(gfx, snapshot); break;
        case Page::Status: drawStatus(gfx, snapshot); break;
        case Page::System: drawSystemInfo(gfx); break;
    }
    drawTabBar(gfx);
    if (spriteReady) frameSprite.pushSprite(0, 0);
}

// Returns true if handling the touch requires a redraw.
bool handleTouch(int x, int y) {
    if (modbusConfigOpen) {
        if (x >= MB_BACK_X && x < MB_BACK_X + MB_BACK_W && y >= MB_BACK_Y &&
            y < MB_BACK_Y + MB_BACK_H) {
            modbusConfigOpen = false;
            return true;
        }
        if (x >= MB_SAVE_X && x < MB_SAVE_X + MB_SAVE_W && y >= MB_SAVE_Y &&
            y < MB_SAVE_Y + MB_SAVE_H) {
            RuntimeSettings::setModbusPackAddressMask(modbusConfigEditMask);
            tft.fillScreen(COLOR_BG);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(COLOR_TEXT, COLOR_BG);
            tft.setTextFont(2);
            tft.drawString("Gespeichert - Neustart...", SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
            tft.setTextDatum(TL_DATUM);
            delay(600);
            ESP.restart();
        }
        int chipIdx = modbusChipIndexAt(x, y);
        if (chipIdx >= 0) {
            modbusConfigEditMask ^= (1u << chipIdx);
            return true;
        }
        return false;
    }

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

        if (protocolRowW > 0 && x >= protocolRowX && x < protocolRowX + protocolRowW &&
            y >= protocolRowY && y < protocolRowY + protocolRowH) {
            bool newValue = !RuntimeSettings::useModbus();
            RuntimeSettings::setUseModbus(newValue);
            tft.fillScreen(COLOR_BG);
            tft.setTextDatum(MC_DATUM);
            tft.setTextColor(COLOR_TEXT, COLOR_BG);
            tft.setTextFont(2);
            tft.drawString(String("BMS-Anschluss: ") + (newValue ? "Modbus" : "RS232") +
                                " - Neustart...",
                            SCREEN_WIDTH / 2, SCREEN_HEIGHT / 2);
            tft.setTextDatum(TL_DATUM);
            delay(800);
            ESP.restart();
        }

        if (modbusBtnRowW > 0 && x >= modbusBtnRowX && x < modbusBtnRowX + modbusBtnRowW &&
            y >= modbusBtnRowY && y < modbusBtnRowY + modbusBtnRowH) {
            modbusConfigEditMask = RuntimeSettings::modbusPackAddressMask();
            modbusConfigOpen = true;
            return true;
        }

        if (simRowW > 0 && x >= simRowX && x < simRowX + simRowW && y >= simRowY &&
            y < simRowY + simRowH) {
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

    if (DISPLAY_USE_SPRITE_BUFFER) {
        // 8-bit (RGB332) rather than 16-bit: the largest contiguous free heap block on this board
        // (no PSRAM, ~110KB in practice) isn't enough for a 16-bit full-screen sprite (~150KB), but
        // comfortably fits an 8-bit one (~77KB). Coordinates are unaffected either way - only the
        // per-pixel storage shrinks - so this needed no other code changes.
        frameSprite.setColorDepth(8);
        spriteReady = frameSprite.createSprite(SCREEN_WIDTH, SCREEN_HEIGHT) != nullptr;
        if (!spriteReady) {
            Serial.println("Display: Sprite-Puffer konnte nicht angelegt werden, zeichne direkt");
        }
    }
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
        lastActivityDotMs = now;
        everDrawn = true;
    } else if (!modbusConfigOpen && now - lastActivityDotMs > 150) {
        // Keeps the request/response activity dot live without composing/pushing a full frame or
        // touching the static title/WiFi-status text next to it. A short interval (not the ~1s the
        // old age-in-seconds readout used) matters here - a BMS that answers quickly might only be
        // "sending" (red) for a few tens of ms, and a coarser heartbeat could miss ever rendering
        // that colour at all. The Modbus config overlay has no top bar at all, so this must never
        // fire while that's open.
        drawFreshnessIndicator(tft);
        lastActivityDotMs = now;
    }
}

}  // namespace BmsDisplayUi
