#include "WebUiServer.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ElegantOTA.h>
#include "Config.h"
#include "CredentialsStorage.h"
#include "SnapshotStore.h"

namespace WebUiServer {

namespace {
AsyncWebServer server(WEB_SERVER_PORT);

// Same dark theme/colors as the physical touch display (BmsDisplayUi.cpp) - background #0c0d12,
// card #1a1c24, teal accent #00c896, warn tomato #ff6347, ok green #5ad282.
const char kIndexHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PACE BMS</title>
<style>
  :root {
    --bg: #0c0d12; --card: #1a1c24; --card-alt: #14151b; --accent: #00c896;
    --warn: #ff6347; --warn-bg: #3a2422; --ok: #5ad282; --text: #f0f0f2; --dim: #8c919f;
    --divider: #2b2d36;
  }
  * { box-sizing: border-box; }
  body { font-family: -apple-system, Segoe UI, Roboto, sans-serif; margin: 0; background: var(--bg); color: var(--text); }
  header { background: var(--card-alt); padding: 0.75rem 1rem; display: flex; justify-content: space-between; align-items: center; }
  header h1 { font-size: 1.1rem; margin: 0; }
  #meta { color: var(--dim); font-size: 0.8rem; }
  nav { display: flex; background: var(--card-alt); border-bottom: 1px solid var(--divider); overflow-x: auto; }
  nav button, nav a { flex: 1 0 auto; background: none; border: none; color: var(--dim); padding: 0.7rem 1rem; font-size: 0.85rem; cursor: pointer; text-decoration: none; text-align: center; white-space: nowrap; }
  nav button.active, nav a.active { color: var(--accent); border-bottom: 2px solid var(--accent); }
  main { padding: 1rem; max-width: 720px; margin: 0 auto; }
  .page { display: none; }
  .page.active { display: block; }
  .pack { border: 1px solid var(--divider); border-radius: 10px; padding: 0.9rem; margin-bottom: 1rem; background: var(--card); }
  .pack h2 { font-size: 0.95rem; margin: 0 0 0.6rem; color: var(--dim); font-weight: 600; }
  .cells { display: grid; grid-template-columns: repeat(auto-fill, minmax(72px, 1fr)); gap: 6px; margin-top: 0.5rem; }
  .cell { background: var(--card-alt); border-radius: 6px; padding: 6px; text-align: center; font-size: 0.78rem; border: 1px solid var(--divider); }
  .cell.max { border-color: var(--accent); }
  .cell.min { border-color: var(--warn); }
  .stats { display: grid; grid-template-columns: repeat(auto-fill, minmax(130px, 1fr)); gap: 8px; margin-top: 0.5rem; }
  .stat { background: var(--card-alt); border-radius: 8px; padding: 8px 10px; }
  .stat .label { font-size: 0.7rem; color: var(--dim); }
  .stat .value { font-size: 1.1rem; margin-top: 2px; }
  .warn { color: var(--warn); background: var(--warn-bg); border-radius: 6px; padding: 6px 10px; margin-top: 0.6rem; font-size: 0.85rem; }
  .ok-text { color: var(--ok); font-size: 0.85rem; }
  .pills { display: grid; grid-template-columns: repeat(auto-fill, minmax(120px, 1fr)); gap: 6px; margin-top: 0.6rem; }
  .pill { border-radius: 6px; padding: 6px 8px; font-size: 0.78rem; text-align: center; background: var(--card-alt); color: var(--dim); }
  .pill.active { background: #0d3a2f; color: var(--ok); }
  .placeholder { color: var(--dim); font-size: 0.9rem; line-height: 1.5; text-align: center; margin-top: 3rem; }
</style>
</head>
<body>
<header>
  <h1>PACE BMS</h1>
  <div id="meta">lade...</div>
</header>
<nav>
  <button data-page="uebersicht" class="active">Uebersicht</button>
  <button data-page="zellen">Zellen</button>
  <button data-page="status">Status</button>
  <button data-page="system">System</button>
  <a href="/konfiguration">Konfiguration</a>
</nav>
<main>
  <div class="page active" id="page-uebersicht"></div>
  <div class="page" id="page-zellen"></div>
  <div class="page" id="page-status"></div>
  <div class="page" id="page-system"></div>
</main>
<script>
function activatePage(name) {
  if (!document.getElementById('page-' + name)) return;
  document.querySelectorAll('nav button[data-page]').forEach(b => b.classList.remove('active'));
  document.querySelectorAll('.page').forEach(p => p.classList.remove('active'));
  document.querySelector('nav button[data-page="' + name + '"]').classList.add('active');
  document.getElementById('page-' + name).classList.add('active');
}

document.querySelectorAll('nav button[data-page]').forEach(btn => {
  btn.addEventListener('click', () => {
    location.hash = btn.dataset.page;
    activatePage(btn.dataset.page);
  });
});

activatePage((location.hash || '#uebersicht').substring(1));

function fmt(n, d) { return (typeof n === 'number') ? n.toFixed(d) : '-'; }

async function refresh() {
  try {
    const res = await fetch('/api/data');
    const data = await res.json();
    document.getElementById('meta').textContent =
      (data.valid ? 'verbunden' : 'keine gueltigen Daten') +
      ' | Vers. ' + (data.bmsVersion || '-') +
      ' | vor ' + Math.round((data.lastUpdateAgoMs || 0) / 1000) + 's';

    const packs = data.packs || [];

    document.getElementById('page-uebersicht').innerHTML = packs.map(p => `
      <div class="pack">
        <h2>Pack ${p.index}</h2>
        <div class="stats">
          <div class="stat"><div class="label">Spannung</div><div class="value">${fmt(p.voltageV,2)} V</div></div>
          <div class="stat"><div class="label">Strom</div><div class="value">${fmt(p.currentA,2)} A</div></div>
          <div class="stat"><div class="label">SOC</div><div class="value">${fmt(p.socPercent,1)} %</div></div>
          <div class="stat"><div class="label">SOH</div><div class="value">${fmt(p.sohPercent,1)} %</div></div>
          <div class="stat"><div class="label">Zyklen</div><div class="value">${p.cycles}</div></div>
          <div class="stat"><div class="label">Zelldiff</div><div class="value">${p.cellMaxDiffMv} mV</div></div>
          ${(p.temperaturesC||[]).map((t,i) => `<div class="stat"><div class="label">Temp ${i+1}</div><div class="value">${fmt(t,1)} &deg;C</div></div>`).join('')}
        </div>
        ${p.warnings ? `<div class="warn">${p.warnings}</div>` : ''}
      </div>`).join('') || '<div class="placeholder">Keine Paketdaten</div>';

    document.getElementById('page-zellen').innerHTML = packs.map(p => {
      const cells = p.cellsMv || [];
      const minMv = Math.min(...cells), maxMv = Math.max(...cells);
      return `<div class="pack"><h2>Pack ${p.index} &ndash; Diff ${p.cellMaxDiffMv} mV</h2>
        <div class="cells">${cells.map((mv,i) => {
          let cls = 'cell';
          if (cells.length > 1 && mv === maxMv) cls += ' max';
          if (cells.length > 1 && mv === minMv) cls += ' min';
          return `<div class="${cls}">Z${i+1}<br>${mv} mV</div>`;
        }).join('')}</div></div>`;
    }).join('') || '<div class="placeholder">Keine Zellendaten</div>';

    document.getElementById('page-status').innerHTML = packs.map(p => `
      <div class="pack">
        <h2>Pack ${p.index}</h2>
        ${p.warnings ? `<div class="warn">${p.warnings}</div>` : '<div class="ok-text">Keine Warnungen</div>'}
        <div class="pills">
          <div class="pill ${p.chargeFetOn ? 'active' : ''}">Laden</div>
          <div class="pill ${p.dischargeFetOn ? 'active' : ''}">Entladen</div>
          <div class="pill ${p.acInOn ? 'active' : ''}">Netzteil</div>
          <div class="pill ${p.currentLimitOn ? 'active' : ''}">I-Limit</div>
          <div class="pill ${(p.balanceState1 || p.balanceState2) ? 'active' : ''}">Balancing</div>
          <div class="pill ${p.fullyCharged ? 'active' : ''}">Vollgeladen</div>
        </div>
        <div class="stats">
          <div class="stat"><div class="label">Rest</div><div class="value">${p.remainingCapacityMah} mAh</div></div>
          <div class="stat"><div class="label">Voll</div><div class="value">${p.fullCapacityMah} mAh</div></div>
          <div class="stat"><div class="label">Design</div><div class="value">${p.designCapacityMah} mAh</div></div>
        </div>
      </div>`).join('') || '<div class="placeholder">Keine Statusdaten</div>';
  } catch (e) {
    document.getElementById('meta').textContent = 'Fehler beim Laden: ' + e;
  }
}

function fmtUptime(sec) {
  const h = Math.floor(sec / 3600), m = Math.floor((sec % 3600) / 60), s = Math.floor(sec % 60);
  return h + 'h ' + String(m).padStart(2,'0') + 'm ' + String(s).padStart(2,'0') + 's';
}

async function refreshSystem() {
  try {
    const res = await fetch('/api/system');
    const s = await res.json();
    document.getElementById('page-system').innerHTML = `
      <div class="pack">
        <div class="stats">
          <div class="stat"><div class="label">Laufzeit</div><div class="value">${fmtUptime(s.uptimeSec)}</div></div>
          <div class="stat"><div class="label">WLAN-Signal</div><div class="value">${s.wifiConnected ? s.rssi + ' dBm' : '--'}</div></div>
          <div class="stat"><div class="label">Freier Speicher</div><div class="value">${Math.round(s.freeHeap/1024)} KB</div></div>
          <div class="stat"><div class="label">Chip</div><div class="value">${s.chipModel} Rev ${s.chipRevision}</div></div>
          <div class="stat"><div class="label">CPU-Takt</div><div class="value">${s.cpuFreqMHz} MHz</div></div>
          <div class="stat"><div class="label">Flash</div><div class="value">${Math.round(s.flashSizeBytes/1024/1024)} MB</div></div>
          <div class="stat"><div class="label">IP</div><div class="value">${s.ip}</div></div>
          <div class="stat"><div class="label">MAC</div><div class="value">${s.mac}</div></div>
        </div>
      </div>`;
  } catch (e) {
    document.getElementById('page-system').innerHTML = '<div class="placeholder">Fehler beim Laden: ' + e + '</div>';
  }
}

refresh();
refreshSystem();
setInterval(refresh, 5000);
setInterval(refreshSystem, 5000);
</script>
</body>
</html>
)HTML";

const char kConfigHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>PACE BMS - Konfiguration</title>
<style>
  :root { --bg: #0c0d12; --card: #1a1c24; --card-alt: #14151b; --accent: #00c896; --text: #f0f0f2; --dim: #8c919f; --divider: #2b2d36; }
  * { box-sizing: border-box; }
  body { font-family: -apple-system, Segoe UI, Roboto, sans-serif; margin: 0; background: var(--bg); color: var(--text); }
  header { background: var(--card-alt); padding: 0.75rem 1rem; }
  header h1 { font-size: 1.1rem; margin: 0; }
  nav { display: flex; background: var(--card-alt); border-bottom: 1px solid var(--divider); overflow-x: auto; }
  nav button, nav a { flex: 1 0 auto; background: none; border: none; color: var(--dim); padding: 0.7rem 1rem; font-size: 0.85rem; cursor: pointer; text-decoration: none; text-align: center; white-space: nowrap; }
  nav a.active { color: var(--accent); border-bottom: 2px solid var(--accent); }
  main { padding: 1rem; max-width: 480px; margin: 0 auto; }
  fieldset { border: 1px solid var(--divider); border-radius: 10px; padding: 1rem; margin-bottom: 1.2rem; background: var(--card); }
  legend { color: var(--dim); padding: 0 0.4rem; font-size: 0.85rem; }
  label { display: block; font-size: 0.8rem; color: var(--dim); margin-top: 0.7rem; }
  input { width: 100%; padding: 0.5rem; margin-top: 0.25rem; border-radius: 6px; border: 1px solid var(--divider); background: var(--card-alt); color: var(--text); font-size: 0.9rem; }
  button { margin-top: 1rem; width: 100%; padding: 0.6rem; border: none; border-radius: 6px; background: var(--accent); color: #04231c; font-weight: 600; font-size: 0.9rem; cursor: pointer; }
  a.back { color: var(--dim); font-size: 0.85rem; }
</style>
</head>
<body>
<header><h1>PACE BMS</h1></header>
<nav>
  <a href="/#uebersicht">Uebersicht</a>
  <a href="/#zellen">Zellen</a>
  <a href="/#status">Status</a>
  <a href="/#system">System</a>
  <a href="/konfiguration" class="active">Konfiguration</a>
</nav>
<main>
  <form method="POST" action="/api/config/wifi">
    <fieldset>
      <legend>WLAN</legend>
      <label>SSID<input type="text" name="ssid" value="%WIFI_SSID%"></label>
      <label>Passwort (leer lassen = unveraendert)<input type="password" name="pass" placeholder="unveraendert"></label>
      <button type="submit">WLAN speichern &amp; neu starten</button>
    </fieldset>
  </form>
  <form method="POST" action="/api/config/mqtt">
    <fieldset>
      <legend>MQTT</legend>
      <label>Broker<input type="text" name="host" value="%MQTT_HOST%"></label>
      <label>Port<input type="number" name="port" value="%MQTT_PORT%"></label>
      <label>User<input type="text" name="user" value="%MQTT_USER%"></label>
      <label>Passwort (leer lassen = unveraendert)<input type="password" name="pass" placeholder="unveraendert"></label>
      <button type="submit">MQTT speichern &amp; neu starten</button>
    </fieldset>
  </form>
  <p><a class="back" href="/">&larr; Zurueck zum Dashboard</a></p>
</main>
</body>
</html>
)HTML";

const char kConfigSavedHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="de">
<head><meta charset="utf-8"><title>Gespeichert</title>
<style>body{font-family:sans-serif;background:#0c0d12;color:#f0f0f2;text-align:center;padding-top:3rem;}</style>
</head>
<body><h1>Gespeichert</h1><p>Einstellungen wurden gespeichert. Das Geraet startet jetzt neu ...</p></body>
</html>
)HTML";

String paramOr(AsyncWebServerRequest* request, const char* name, const String& fallback) {
    return request->hasParam(name, true) ? request->getParam(name, true)->value() : fallback;
}

String buildJson() {
    JsonDocument doc;
    PaceBmsSnapshot s = SnapshotStore::get();

    doc["valid"] = s.valid;
    doc["lastUpdateAgoMs"] = s.valid ? (millis() - s.lastUpdateMs) : 0;
    doc["bmsVersion"] = s.bmsVersion;
    doc["bmsSerial"] = s.bmsSerial;
    doc["packSerial"] = s.packSerial;

    JsonArray packs = doc["packs"].to<JsonArray>();
    for (uint8_t p = 0; p < s.packCount; p++) {
        const PacePackAnalog& pack = s.packs[p];
        const PacePackWarn& warn = s.warn[p];
        JsonObject po = packs.add<JsonObject>();
        po["index"] = p + 1;
        po["voltageV"] = pack.packVoltageV;
        po["currentA"] = pack.packCurrentA;
        po["socPercent"] = pack.socPercent;
        po["sohPercent"] = pack.sohPercent;
        po["cycles"] = pack.cycles;
        po["cellMaxDiffMv"] = pack.cellMaxDiffMv;
        po["remainingCapacityMah"] = pack.remainingCapacityMah;
        po["fullCapacityMah"] = pack.fullCapacityMah;
        po["designCapacityMah"] = pack.designCapacityMah;
        po["warnings"] = warn.warnings;
        po["chargeFetOn"] = warn.chargeFetOn;
        po["dischargeFetOn"] = warn.dischargeFetOn;
        po["acInOn"] = warn.acInOn;
        po["currentLimitOn"] = warn.currentLimitOn;
        po["fullyCharged"] = warn.fullyCharged;
        po["balanceState1"] = warn.balanceState1;
        po["balanceState2"] = warn.balanceState2;

        JsonArray cellsMv = po["cellsMv"].to<JsonArray>();
        for (uint8_t i = 0; i < pack.cellCount; i++) cellsMv.add(pack.cellMillivolts[i]);

        JsonArray temps = po["temperaturesC"].to<JsonArray>();
        for (uint8_t i = 0; i < pack.tempCount; i++) temps.add(pack.temperaturesC[i]);
    }

    JsonObject capacity = doc["packCapacity"].to<JsonObject>();
    capacity["remainCapacityMah"] = s.capacity.remainCapacityMah;
    capacity["fullCapacityMah"] = s.capacity.fullCapacityMah;
    capacity["designCapacityMah"] = s.capacity.designCapacityMah;
    capacity["socPercent"] = s.capacity.socPercent;
    capacity["sohPercent"] = s.capacity.sohPercent;

    String out;
    serializeJson(doc, out);
    return out;
}

String buildSystemJson() {
    JsonDocument doc;
    doc["uptimeSec"] = millis() / 1000;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["chipModel"] = ESP.getChipModel();
    doc["chipRevision"] = ESP.getChipRevision();
    doc["cpuFreqMHz"] = ESP.getCpuFreqMHz();
    doc["flashSizeBytes"] = ESP.getFlashChipSize();
    doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
    doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
    doc["mac"] = WiFi.macAddress();

    String out;
    serializeJson(doc, out);
    return out;
}

void handleConfigPage(AsyncWebServerRequest* request) {
    String html = kConfigHtml;
    html.replace("%WIFI_SSID%", CredentialsManager::instance().getWifiSsid());
    html.replace("%MQTT_HOST%", CredentialsManager::instance().getMqttHost());
    html.replace("%MQTT_PORT%", String(CredentialsManager::instance().getMqttPort()));
    html.replace("%MQTT_USER%", CredentialsManager::instance().getMqttUser());
    request->send(200, "text/html", html);
}

void handleSaveWifi(AsyncWebServerRequest* request) {
    String ssid = paramOr(request, "ssid", CredentialsManager::instance().getWifiSsid());
    String passIn = paramOr(request, "pass", "");
    String pass = passIn.length() > 0 ? passIn : CredentialsManager::instance().getWifiPass();
    CredentialsManager::instance().saveWifi(ssid, pass);

    request->send(200, "text/html", kConfigSavedHtml);
    // Short delay so the response reaches the client before the restart drops the connection.
    delay(500);
    ESP.restart();
}

void handleSaveMqtt(AsyncWebServerRequest* request) {
    String host = paramOr(request, "host", CredentialsManager::instance().getMqttHost());
    int port = paramOr(request, "port", String(CredentialsManager::instance().getMqttPort())).toInt();
    String user = paramOr(request, "user", CredentialsManager::instance().getMqttUser());
    String passIn = paramOr(request, "pass", "");
    String pass = passIn.length() > 0 ? passIn : CredentialsManager::instance().getMqttPass();
    CredentialsManager::instance().saveMqtt(host, port, user, pass);

    request->send(200, "text/html", kConfigSavedHtml);
    delay(500);
    ESP.restart();
}

}  // namespace

void begin() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "text/html", kIndexHtml);
    });

    server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", buildJson());
    });

    server.on("/api/system", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", buildSystemJson());
    });

    server.on("/konfiguration", HTTP_GET, handleConfigPage);
    server.on("/api/config/wifi", HTTP_POST, handleSaveWifi);
    server.on("/api/config/mqtt", HTTP_POST, handleSaveMqtt);

    ElegantOTA.begin(&server);
    ElegantOTA.setAuth(OTA_HOSTNAME, OTA_PASSWORD);

    server.begin();
}

void loop() { ElegantOTA.loop(); }

}  // namespace WebUiServer
