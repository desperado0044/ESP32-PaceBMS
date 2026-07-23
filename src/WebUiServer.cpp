#include "WebUiServer.h"
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <WiFi.h>
#include <ElegantOTA.h>
#include <esp_system.h>
#include "Config.h"
#include "CredentialsStorage.h"
#include "SnapshotStore.h"
#include "RuntimeSettings.h"
#include "ModbusSniff.h"
#include "BmsActivity.h"
#include "MqttManager.h"

namespace WebUiServer {

namespace {
AsyncWebServer server(WEB_SERVER_PORT);

// ElegantOTA::setAuth() only stores the const char* pointer it's given, not a copy - this must
// stay alive for as long as OTA auth checks might reference it (i.e. permanently), so the
// hostname string is kept here rather than as a temporary passed directly to setAuth(). Same
// class of bug as MqttManager's mqttHostBuf for PubSubClient::setServer().
String otaUsernameBuf;

// Same dark theme/colors as the physical touch display (BmsDisplayUi.cpp) - background #0c0d12,
// card #1a1c24, teal accent #00c896, warn tomato #ff6347, ok green #5ad282.
const char kIndexHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>%HOSTNAME%</title>
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
  button { padding: 0.6rem 1rem; border: none; border-radius: 6px; background: var(--accent); color: #04231c; font-weight: 600; font-size: 0.85rem; cursor: pointer; }
  button:disabled { opacity: 0.6; cursor: default; }
</style>
</head>
<body>
<header>
  <h1>%HOSTNAME%</h1>
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

// Mirrors the display's "Gesamt" aggregate (BmsDisplayUi.cpp) so the web overview matches what
// the touchscreen shows: bus voltage averaged (packs are wired in parallel, same bus), current/
// capacities summed, SOC/SOH weighted by capacity (not a plain average across packs), and the
// same nominal-voltage cap on the energy figure (LiFePO4 post-charge surface-voltage overshoot).
function computeAggregate(packs) {
  let voltageSum = 0, currentSum = 0, remainSum = 0, fullSum = 0, designSum = 0, cellCount = 0;
  let onlinePackCount = 0;
  // exposed on the returned object too, so the "Packs" stat can show "online / total" instead of
  // just the total slot count (which never shrinks on its own, see README "Pack-Erkennung") -
  // showing only the raw total looked like a stuck/wrong count once a pack actually disconnected.
  const warnParts = [];
  packs.forEach(p => {
    currentSum += p.currentA;
    remainSum += p.remainingCapacityMah;
    fullSum += p.fullCapacityMah;
    designSum += p.designCapacityMah;
    // Only average voltage (and pick cellCount) from packs currently reporting a real voltage -
    // a failed/zeroed pack's 0V would otherwise drag the average down misleadingly.
    if (p.voltageV > 0) {
      voltageSum += p.voltageV;
      onlinePackCount++;
      cellCount = (p.cellsMv || []).length;
    }
    if (p.warnings) warnParts.push('Pack ' + p.index + ': ' + p.warnings);
    // Surface a failed pack in the Gesamt card too, not just on its own individual card below -
    // otherwise a dropped pack is invisible unless you scroll down to it specifically.
    if (p.voltageV <= 0) warnParts.push('Pack ' + p.index + ' ausgefallen');
  });
  const voltageV = onlinePackCount > 0 ? voltageSum / onlinePackCount : 0;
  const socPercent = fullSum > 0 ? (remainSum * 100 / fullSum) : 0;
  const sohPercent = designSum > 0 ? (fullSum * 100 / designSum) : 0;
  const nominalVoltage = cellCount > 0 ? 3.2 * cellCount : voltageV;
  const energyVoltage = Math.min(voltageV, nominalVoltage);
  const energyKwh = energyVoltage * (remainSum / 1000) / 1000;
  const fullEnergyKwh = energyVoltage * (fullSum / 1000) / 1000;
  const powerW = voltageV * currentSum;
  return { voltageV, currentA: currentSum, socPercent, sohPercent, energyKwh, fullEnergyKwh, powerW,
           onlinePackCount, warnings: warnParts.join(' | ') };
}

async function refresh() {
  try {
    const res = await fetch('/api/data');
    const data = await res.json();
    document.getElementById('meta').textContent =
      (data.valid ? 'verbunden' : 'keine gueltigen Daten') +
      ' | Vers. (Master) ' + (data.bmsVersion || '-') +
      ' | vor ' + Math.round((data.lastUpdateAgoMs || 0) / 1000) + 's';

    const packs = data.packs || [];
    const agg = computeAggregate(packs);
    const aggCard = packs.length > 0 ? `
      <div class="pack">
        <h2>Gesamt</h2>
        <div class="stats">
          <div class="stat"><div class="label">Packs</div><div class="value">${agg.onlinePackCount} / ${packs.length}</div></div>
          <div class="stat"><div class="label">Bus-Spannung</div><div class="value">${fmt(agg.voltageV,2)} V</div></div>
          <div class="stat"><div class="label">Strom</div><div class="value">${fmt(agg.currentA,2)} A</div></div>
          <div class="stat"><div class="label">Leistung</div><div class="value">${fmt(agg.powerW,0)} W</div></div>
          <div class="stat"><div class="label">SOC</div><div class="value">${fmt(agg.socPercent,1)} %</div></div>
          <div class="stat"><div class="label">SOH</div><div class="value">${fmt(agg.sohPercent,1)} %</div></div>
          <div class="stat"><div class="label">Energie (Rest)</div><div class="value">${fmt(agg.energyKwh,2)} kWh</div></div>
          <div class="stat"><div class="label">Energie (Voll)</div><div class="value">${fmt(agg.fullEnergyKwh,2)} kWh</div></div>
        </div>
        ${agg.warnings ? `<div class="warn">${agg.warnings}</div>` : ''}
      </div>` : '';

    document.getElementById('page-uebersicht').innerHTML = aggCard + packs.map(p => `
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
          <div class="stat"><div class="label">Min. freier Speicher</div><div class="value">${Math.round(s.minFreeHeap/1024)} KB</div></div>
          <div class="stat"><div class="label">Freier Update-Speicher</div><div class="value">${Math.round(s.freeSketchSpaceBytes/1024)} KB</div></div>
          <div class="stat"><div class="label">Build</div><div class="value">${s.buildTime}</div></div>
          <div class="stat"><div class="label">Version</div><div class="value">${s.gitVersion}</div></div>
          <div class="stat"><div class="label">Letzter Neustart</div><div class="value">${s.resetReason}</div></div>
          <div class="stat"><div class="label">BMS-Anschluss</div><div class="value">${s.useModbus ? 'Modbus RTU' : 'RS232'}</div></div>
        </div>
      </div>
      <div class="pack">
        <h2>Kommunikation</h2>
        <div class="stats">
          <div class="stat"><div class="label">Poll-Intervall</div><div class="value">${(s.pollIntervalMs/1000).toFixed(1)} s</div></div>
          <div class="stat"><div class="label">Letzter Zyklus (Dauer)</div><div class="value">${s.lastResponseMs >= s.lastRequestMs ? (s.lastResponseMs - s.lastRequestMs) + ' ms' : '...'}</div></div>
          <div class="stat"><div class="label">Fehlversuche in Folge</div><div class="value">${s.consecutiveFailures}</div></div>
          <div class="stat"><div class="label">UART</div><div class="value">${s.uartBaud} Baud, 8N1</div></div>
          <div class="stat"><div class="label">MQTT</div><div class="value">${s.mqttConnected ? 'verbunden' : 'getrennt'}</div></div>
        </div>
        ${s.lastPollError ? `<p style="color:var(--warn);font-size:0.82rem;margin:0.6rem 0 0;word-break:break-all;">${s.lastPollError}</p>` : ''}
        ${s.useModbus && s.packFailCount.length > 0 ? `
        <table style="width:100%;margin-top:0.6rem;font-size:0.85rem;border-collapse:collapse;">
          <thead><tr style="color:var(--dim);text-align:left;"><th>Adresse</th><th>Fehlversuche in Folge</th></tr></thead>
          <tbody>${s.packFailCount.map(p => `<tr><td>${p.address}</td><td>${p.failCount}</td></tr>`).join('')}</tbody>
        </table>` : ''}
        <div style="margin-top:0.8rem;">
          <button id="btn-sniff-start">Bus-Mitschnitt starten (5s)</button>
          <button id="btn-sniff-result">Ergebnis abrufen</button>
        </div>
        <pre id="sniff-result" style="white-space:pre-wrap;word-break:break-all;font-size:0.78rem;margin-top:0.6rem;color:var(--dim);"></pre>
      </div>
      <div class="pack">
        <h2>Simulationsmodus</h2>
        <p style="color:var(--dim);font-size:0.85rem;margin:0 0 0.6rem;">
          Aktuell: <strong style="color:var(--text)">${s.simulateBmsData ? 'AN' : 'AUS'}</strong>
          - schaltet das BMS-Polling auf Fake-Daten um/zurueck und startet neu.
        </p>
        <button id="btn-simulate">Simulation ${s.simulateBmsData ? 'ausschalten' : 'einschalten'}</button>
      </div>
      <div class="pack">
        <h2>Neustart</h2>
        <button id="btn-reboot" style="background:var(--warn);color:#2a0e08;">Geraet neu starten</button>
      </div>`;
    document.getElementById('btn-simulate').addEventListener('click', async () => {
      document.getElementById('btn-simulate').disabled = true;
      document.getElementById('btn-simulate').textContent = 'Neustart...';
      await fetch('/api/system/simulate', { method: 'POST' });
    });
    document.getElementById('btn-reboot').addEventListener('click', async () => {
      document.getElementById('btn-reboot').disabled = true;
      document.getElementById('btn-reboot').textContent = 'Neustart...';
      await fetch('/api/system/reboot', { method: 'POST' });
    });
    document.getElementById('btn-sniff-start').addEventListener('click', async () => {
      const res = await fetch('/api/modbus-sniff', { method: 'POST', headers: {'Content-Type':'application/x-www-form-urlencoded'}, body: 'ms=5000' });
      document.getElementById('sniff-result').textContent = await res.text();
    });
    document.getElementById('btn-sniff-result').addEventListener('click', async () => {
      const res = await fetch('/api/modbus-sniff');
      document.getElementById('sniff-result').textContent = await res.text();
    });
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
<title>%HOSTNAME% - Konfiguration</title>
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
<header><h1>%HOSTNAME%</h1></header>
<nav>
  <a href="/#uebersicht">Uebersicht</a>
  <a href="/#zellen">Zellen</a>
  <a href="/#status">Status</a>
  <a href="/#system">System</a>
  <a href="/konfiguration" class="active">Konfiguration</a>
</nav>
<main>
  <form method="POST" action="/api/config/hostname">
    <fieldset>
      <legend>Geraetename</legend>
      <label>Name (mDNS "&lt;Name&gt;.local", OTA-Login, MQTT-Topic-Praefix)
        <input type="text" name="hostname" value="%HOSTNAME%" pattern="[A-Za-z0-9_-]{1,32}"
               title="Nur Buchstaben, Ziffern, - und _, 1-32 Zeichen, keine Leerzeichen" required></label>
      <p style="color:var(--dim);font-size:0.82rem;margin:0.3rem 0 0.6rem;">
        Bei mehreren Geraeten im selben Netz/Broker pro Geraet unterschiedlich setzen
        (z.B. pacebms1, pacebms2), sonst kollidieren mDNS-Name und MQTT-Topics.
      </p>
      <button type="submit">Geraetename speichern &amp; neu starten</button>
    </fieldset>
  </form>
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
  <form method="POST" action="/api/config/protocol">
    <fieldset>
      <legend>BMS-Anschluss</legend>
      <label style="display:flex;align-items:center;gap:0.5rem;">
        <input type="radio" name="protocol" value="rs232" %PROTOCOL_RS232_CHECKED% style="width:auto;">
        RS232 (ASCII-Protokoll)
      </label>
      <label style="display:flex;align-items:center;gap:0.5rem;">
        <input type="radio" name="protocol" value="modbus" %PROTOCOL_MODBUS_CHECKED% style="width:auto;">
        Modbus RTU / RS485
      </label>
      <button type="submit">Anschluss speichern &amp; neu starten</button>
    </fieldset>
  </form>
  <form method="POST" action="/api/config/poll-interval">
    <fieldset>
      <legend>Abfrageintervall</legend>
      <label>Wie oft das BMS abgefragt wird, in Sekunden
        <input type="number" name="poll_sec" min="1" step="1" value="%POLL_INTERVAL_SEC%">
      </label>
      <button type="submit">Intervall speichern</button>
    </fieldset>
  </form>
  <form method="POST" action="/api/config/modbus-packs">
    <fieldset>
      <legend>Modbus-Konfiguration</legend>
      <p style="font-size:0.78rem;color:var(--dim);margin:0 0 0.4rem;">
        Nur relevant, wenn oben Modbus RTU / RS485 ausgewaehlt ist: welche Pack-Adressen (per
        Dip-Schalter am Pack eingestellt) sind tatsaechlich installiert?
      </p>
      <div style="display:grid;grid-template-columns:repeat(5,1fr);gap:0.4rem;margin-top:0.3rem;">
        %MODBUS_ADDRESS_CHECKBOXES%
      </div>
      <button type="submit">Adressen speichern &amp; neu starten</button>
    </fieldset>
  </form>
  <fieldset>
    <legend>Firmware-Update</legend>
    <p style="font-size:0.82rem;color:var(--dim);margin:0 0 0.4rem;">
      Anmeldung mit Geraetename (%HOSTNAME%) als Benutzername und dem in
      <code>include/Config.h</code> hinterlegten OTA-Passwort.
    </p>
    <a href="/update">Zur Update-Seite</a>
  </fieldset>
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

const char kInvalidHostnameHtml[] PROGMEM = R"HTML(
<!DOCTYPE html>
<html lang="de">
<head><meta charset="utf-8"><title>Ungueltiger Geraetename</title>
<style>body{font-family:sans-serif;background:#0c0d12;color:#f0f0f2;text-align:center;padding-top:3rem;}
a{color:#00c896;}</style>
</head>
<body><h1>Ungueltiger Geraetename</h1>
<p>Nur Buchstaben, Ziffern, "-" und "_" erlaubt, 1-32 Zeichen - keine Leerzeichen oder
Sonderzeichen (der Name wird auch als mDNS-Name, OTA-Login und MQTT-Topic-Praefix genutzt).</p>
<p><a href="/konfiguration">Zurueck</a></p>
</body>
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
        po["index"] = s.packAddress[p];
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

const char* resetReasonText(esp_reset_reason_t reason) {
    switch (reason) {
        case ESP_RST_POWERON: return "Power-On";
        case ESP_RST_EXT: return "Extern (Reset-Pin)";
        case ESP_RST_SW: return "Software (z.B. OTA/Neustart)";
        case ESP_RST_PANIC: return "Absturz (Panic)";
        case ESP_RST_INT_WDT: return "Interner Watchdog";
        case ESP_RST_TASK_WDT: return "Task-Watchdog";
        case ESP_RST_WDT: return "Watchdog";
        case ESP_RST_BROWNOUT: return "Unterspannung (Brownout)";
        case ESP_RST_SDIO: return "SDIO";
        default: return "Unbekannt";
    }
}

String buildSystemJson() {
    JsonDocument doc;
    doc["uptimeSec"] = millis() / 1000;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["minFreeHeap"] = ESP.getMinFreeHeap();
    doc["chipModel"] = ESP.getChipModel();
    doc["chipRevision"] = ESP.getChipRevision();
    doc["cpuFreqMHz"] = ESP.getCpuFreqMHz();
    doc["flashSizeBytes"] = ESP.getFlashChipSize();
    doc["freeSketchSpaceBytes"] = ESP.getFreeSketchSpace();
    doc["buildTime"] = String(__DATE__) + " " + String(__TIME__);
    // Git tag/commit, auto-updated on every build via get_git_version.py (platformio.ini,
    // extra_scripts) - unlike buildTime alone, this tells apart "same day, different commit"
    // builds and reads exactly like the GitHub release tag, so it can never lag behind the
    // actually-installed firmware the way a manually maintained version string would.
    doc["gitVersion"] = GIT_VERSION;
    doc["resetReason"] = resetReasonText(esp_reset_reason());
    doc["wifiConnected"] = WiFi.status() == WL_CONNECTED;
    doc["rssi"] = WiFi.status() == WL_CONNECTED ? WiFi.RSSI() : 0;
    doc["ip"] = WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : "";
    doc["mac"] = WiFi.macAddress();
    doc["simulateBmsData"] = RuntimeSettings::simulateBmsData();
    doc["useModbus"] = RuntimeSettings::useModbus();
    const PaceBmsSnapshot& snap = SnapshotStore::get();
    doc["lastPollError"] = snap.lastPollError;
    doc["lastRequestMs"] = BmsActivity::lastRequestMs();
    doc["lastResponseMs"] = BmsActivity::lastResponseMs();
    doc["pollIntervalMs"] = RuntimeSettings::bmsPollIntervalMs();
    doc["consecutiveFailures"] = snap.consecutiveFailures;
    doc["mqttConnected"] = MqttManager::isConnected();
    doc["uartBaud"] = RuntimeSettings::useModbus() ? MODBUS_UART_BAUD : BMS_UART_BAUD;

    JsonArray packFails = doc["packFailCount"].to<JsonArray>();
    for (uint8_t i = 0; i < snap.packCount; i++) {
        JsonObject pf = packFails.add<JsonObject>();
        pf["address"] = snap.packAddress[i];
        pf["failCount"] = snap.packFailCount[i];
    }

    String out;
    serializeJson(doc, out);
    return out;
}

void handleToggleSimulate(AsyncWebServerRequest* request) {
    bool newValue = !RuntimeSettings::simulateBmsData();
    RuntimeSettings::setSimulateBmsData(newValue);
    request->send(200, "text/plain", newValue ? "an" : "aus");
    delay(300);
    ESP.restart();
}

void handleReboot(AsyncWebServerRequest* request) {
    request->send(200, "text/plain", "Neustart...");
    delay(300);
    ESP.restart();
}

void handleConfigPage(AsyncWebServerRequest* request) {
    String html = kConfigHtml;
    html.replace("%WIFI_SSID%", CredentialsManager::instance().getWifiSsid());
    html.replace("%MQTT_HOST%", CredentialsManager::instance().getMqttHost());
    html.replace("%MQTT_PORT%", String(CredentialsManager::instance().getMqttPort()));
    html.replace("%MQTT_USER%", CredentialsManager::instance().getMqttUser());
    html.replace("%HOSTNAME%", CredentialsManager::instance().getHostname());
    bool modbus = RuntimeSettings::useModbus();
    html.replace("%PROTOCOL_RS232_CHECKED%", modbus ? "" : "checked");
    html.replace("%PROTOCOL_MODBUS_CHECKED%", modbus ? "checked" : "");
    html.replace("%POLL_INTERVAL_SEC%", String(RuntimeSettings::bmsPollIntervalMs() / 1000));

    uint16_t mask = RuntimeSettings::modbusPackAddressMask();
    String checkboxes;
    for (uint8_t addr = 0; addr <= 15; addr++) {
        bool checked = mask & (1u << addr);
        checkboxes += "<label style=\"display:flex;align-items:center;gap:0.3rem;font-size:0.8rem;\">"
                      "<input type=\"checkbox\" name=\"addr" + String(addr) + "\" " +
                      (checked ? "checked" : "") + " style=\"width:auto;\"> " + String(addr) +
                      "</label>";
    }
    html.replace("%MODBUS_ADDRESS_CHECKBOXES%", checkboxes);

    request->send(200, "text/html", html);
}

void handleSaveProtocol(AsyncWebServerRequest* request) {
    String protocol = paramOr(request, "protocol", "rs232");
    RuntimeSettings::setUseModbus(protocol == "modbus");

    request->send(200, "text/html", kConfigSavedHtml);
    delay(500);
    ESP.restart();
}

// Unlike the other config saves, NetworkTask re-reads this every loop iteration rather than
// caching it once at task start - no reboot needed, so this just redirects back to the
// (now-updated) config page instead of showing the "restarting..." page.
void handleSavePollInterval(AsyncWebServerRequest* request) {
    int seconds = paramOr(request, "poll_sec", "5").toInt();
    if (seconds < 1) seconds = 1;
    RuntimeSettings::setBmsPollIntervalMs((unsigned long)seconds * 1000UL);
    request->redirect("/konfiguration");
}

// One-off diagnostic: passively listens on the Modbus/RS485 UART (no transmit at all) and returns
// whatever raw bytes show up as hex - answers "is there any traffic on this bus at all, from
// anything" independent of our own protocol code, useful when our own reads time out and it's
// unclear whether that's a wiring/hardware issue or something in our request/response handling.
// The actual byte collection happens in NetworkTask's own loop (ModbusSniff::collectIfActive) -
// this handler only starts the capture and, separately, reads back whatever's been collected so
// far. An earlier version collected bytes directly in this handler via a several-second blocking
// loop, which starved the AsyncTCP task long enough to trip the watchdog and reboot the board.
void handleModbusSniffStart(AsyncWebServerRequest* request) {
    int durationMs = paramOr(request, "ms", "5000").toInt();
    if (durationMs < 200) durationMs = 200;
    if (durationMs > 20000) durationMs = 20000;
    ModbusSniff::start((unsigned long)durationMs);
    request->send(200, "text/plain",
                  "Mitschnitt gestartet (" + String(durationMs) +
                      "ms) - Ergebnis per GET auf denselben Pfad abrufen.");
}

void handleModbusSniffResult(AsyncWebServerRequest* request) {
    request->send(200, "text/plain", ModbusSniff::resultText());
}

void handleSaveModbusPacks(AsyncWebServerRequest* request) {
    uint16_t mask = 0;
    for (uint8_t addr = 0; addr <= 15; addr++) {
        if (request->hasParam("addr" + String(addr), true)) mask |= (1u << addr);
    }
    RuntimeSettings::setModbusPackAddressMask(mask);

    request->send(200, "text/html", kConfigSavedHtml);
    delay(500);
    ESP.restart();
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

void handleSaveHostname(AsyncWebServerRequest* request) {
    String hostname = paramOr(request, "hostname", CredentialsManager::instance().getHostname());
    if (!CredentialsManager::instance().saveHostname(hostname)) {
        request->send(400, "text/html", kInvalidHostnameHtml);
        return;
    }

    request->send(200, "text/html", kConfigSavedHtml);
    delay(500);
    ESP.restart();
}

}  // namespace

void begin() {
    server.on("/", HTTP_GET, [](AsyncWebServerRequest* request) {
        String html = kIndexHtml;
        html.replace("%HOSTNAME%", CredentialsManager::instance().getHostname());
        request->send(200, "text/html", html);
    });

    server.on("/api/data", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", buildJson());
    });

    server.on("/api/system", HTTP_GET, [](AsyncWebServerRequest* request) {
        request->send(200, "application/json", buildSystemJson());
    });
    server.on("/api/system/simulate", HTTP_POST, handleToggleSimulate);
    server.on("/api/system/reboot", HTTP_POST, handleReboot);

    server.on("/konfiguration", HTTP_GET, handleConfigPage);
    server.on("/api/config/wifi", HTTP_POST, handleSaveWifi);
    server.on("/api/config/mqtt", HTTP_POST, handleSaveMqtt);
    server.on("/api/config/hostname", HTTP_POST, handleSaveHostname);
    server.on("/api/config/protocol", HTTP_POST, handleSaveProtocol);
    server.on("/api/config/poll-interval", HTTP_POST, handleSavePollInterval);
    server.on("/api/modbus-sniff", HTTP_POST, handleModbusSniffStart);
    server.on("/api/modbus-sniff", HTTP_GET, handleModbusSniffResult);
    server.on("/api/config/modbus-packs", HTTP_POST, handleSaveModbusPacks);

    ElegantOTA.begin(&server);
    otaUsernameBuf = CredentialsManager::instance().getHostname();
    ElegantOTA.setAuth(otaUsernameBuf.c_str(), OTA_PASSWORD);

    server.begin();
}

void loop() { ElegantOTA.loop(); }

}  // namespace WebUiServer
