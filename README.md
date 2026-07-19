# ESP32 PaceBMS

> **Vorabversion (Pre-Release).** Nachbau, Verdrahtung und Nutzung auf eigene
> Gefahr - keine Garantie für Vollständigkeit, Richtigkeit oder Eignung für
> einen bestimmten Zweck. Insbesondere der eigentliche BMS-Protokoll-Teil wurde
> bisher **nicht gegen ein echtes PACE-BMS verifiziert** (siehe „Stand / Umfang"
> unten) - vor produktivem Einsatz an einem echten Akkupack selbst prüfen.

ESP32-Firmware zum Auslesen eines PACE-basierten BMS (RS232) mit Web-Oberfläche
und MQTT/Home-Assistant-Anbindung. Das ASCII-Hex-Protokoll (Framing, Checksum,
Kommandos) wurde von [Tertiush/bmspace](https://github.com/Tertiush/bmspace)
(Python) nach C++ portiert.

## Stand / Umfang

- **Nur Lesen.** Das Referenzprojekt implementiert ausschließlich Auslese-Kommandos
  (Version, Seriennummer, Zellspannungen/Temperaturen/Strom/Spannung, Kapazität,
  Warn-/Schutz-/Balancing-Status). Es gibt aktuell **keine** Schreib-/Steuerbefehle
  (z.B. Strombegrenzung setzen, FETs schalten) — dafür fehlt eine Dokumentation der
  entsprechenden CID2-Codes. Die Architektur (`PaceBmsProtocol`/`PaceBmsClient`) ist
  so aufgebaut, dass Schreibkommandos später ergänzt werden können.
- Unterstützt mehrere parallelgeschaltete Packs (wie das Python-Original), sofern
  das BMS das meldet — Display, Web-UI und MQTT decken alle gemeldeten Packs ab
  (siehe „Pack-Erkennung & Verhalten bei Trennung" unten).

## Hardware

- ESP32 NodeMCU-Board (PlatformIO-Boardname `esp32dev`)
- MAX3232-Modul (oder ähnlicher RS232-Transceiver) zwischen ESP32-UART2 und dem
  RS232-Port des BMS. Die PACE-BMS-Ports sind **echtes RS232**, nicht TTL — ohne
  Pegelwandler wird nichts empfangen und im schlimmsten Fall Hardware beschädigt.
- 2.8" TFT SPI, 320×240, ILI9341-Treiber + resistiver Touch (XPT2046).

### BMS-UART (UART2)

| ESP32 (UART2) | MAX3232      |
|---------------|---------------|
| GPIO32 (TX2)  | T1IN          |
| GPIO34 (RX2)  | R1OUT         |
| GND           | GND           |
| 3V3           | VCC           |

Bewusst **nicht** GPIO16/17 (Standard-UART2-Pins) — die sind durch das Display
belegt (siehe unten). GPIO34 ist input-only, für reinen RX-Betrieb unproblematisch.

| MAX3232 (RS232-Seite) | BMS RJ11 (Blick in die Buchse, Nase unten) |
|------------------------|---------------------------------------------|
| T1OUT                  | Pin 4 (BMS_Rx)                               |
| R1IN                   | Pin 3 (BMS_Tx)                               |
| GND                    | Pin 2 / Pin 5 (GND)                          |

UART-Parameter: **9600 Baud, 8N1** (siehe `include/Config.h`).

Die genaue RJ11-Belegung kann je nach Marke/Modell abweichen — vor dem
Anschließen mit einem Multimeter/Oszilloskop verifizieren.

### Display + Touch (TFT_eSPI + XPT2046)

Getrennte SPI-Busse wie im Haustürklingel-Projekt: Display auf HSPI (TFT_eSPI-eigene
Instanz), Touch auf VSPI (globales Arduino-`SPI`-Objekt, `XPT2046_Touchscreen`
erlaubt keine eigene Instanz). Konfiguration läuft komplett über `build_flags` in
`platformio.ini` (keine `User_Setup.h`-Änderungen nötig).

| Signal | Pin | Bus |
|---|---|---|
| TFT_SCLK | 14 | HSPI (nativ) |
| TFT_MOSI | 4 | GPIO-Matrix |
| TFT_CS | 17 | GPIO |
| TFT_DC | 21 | GPIO |
| TFT_RST | 16 | GPIO |
| Backlight (PWM) | 22 | GPIO (ledc) |
| TOUCH_SCLK | 18 | VSPI (nativ) |
| TOUCH_MOSI | 23 | VSPI (nativ) |
| TOUCH_MISO | 19 | VSPI (nativ) |
| TOUCH_CS | 27 | GPIO |
| TOUCH_IRQ | 33 | GPIO |

Einbau **horizontal (Querformat)**, `TFT_ROTATION=1` in `include/Config.h`.
Touch-Kalibrierung: werksseitig nur ein grober Fallback-Bereich hinterlegt — die
4-Ecken-Kalibrierung durchführen, indem **10 Sekunden lang** irgendwo auf dem
Display gedrückt gehalten wird (funktioniert unabhängig von WLAN). Die Werte
werden dauerhaft im NVS (`pacebms_calib`) gespeichert.

### Werksreset

Den **BOOT/FLASH-Button** auf dem ESP32-Board (GPIO0) **8 Sekunden lang**
gedrückt halten löscht gespeicherte WLAN-/MQTT-Zugangsdaten (`pacebms_cred`)
und die Touch-Kalibrierung (`pacebms_calib`) aus dem NVS und startet neu —
danach öffnet sich wieder das Einrichtungsportal. Bewusst über den physischen
Button statt eine Touch-Geste, da er auch funktionieren muss, wenn WLAN oder
die Touch-Kalibrierung selbst kaputt sind, und damit er nicht mit der
10s-Kalibrier-Geste am Display kollidiert.

## Display-Oberfläche

Kopfzeile: Titel, WLAN-Status (IP-Adresse, oder `Setup 10.0.0.1` solange das
Einrichtungsportal offen ist, oder `Verbinde...`), Frische-Punkt + Alter der
letzten BMS-Antwort. Darunter vier Tabs am unteren Bildschirmrand (per Touch
umschaltbar):

- **Übersicht** — Batteriesymbol + SOC groß, Lade-/Entladepfeil, Spannung, Strom,
  SOH, bei Einzel-Pack-Ansicht zusätzlich Zyklen, und immer eine Energie-Zeile
  in kWh (Spannung × Restkapazität); Warnbanner unten, falls Warnungen anliegen.
- **Zellen** — Rasteransicht aller Zellspannungen, Min/Max farblich hervorgehoben,
  Zell-Diff im Header.
- **Status** — Warnungen im Klartext, Schutz-/FET-/Balancing-Zustände als Badges,
  Kapazitätswerte (Rest/Voll/Design).
- **Steuerung** — Platzhalter für künftige Schreibbefehle (Strombegrenzung, FETs
  schalten, Buzzer); Flächen sind vorbereitet, aber bewusst deaktiviert, solange
  das Schreibprotokoll nicht dokumentiert ist (siehe oben).

Auf Übersicht/Zellen/Status zeigt eine kleine Leiste unter der Kopfzeile
("Gesamt" oder "Pack X von N" mit `‹ ›`-Wischhinweis), welches Pack gerade
angezeigt wird. Per **horizontalem Wischen** irgendwo im Inhaltsbereich wechselt
man zwischen der aggregierten **Gesamt**-Ansicht (Strom/Kapazität/Energie über
alle Packs summiert, Spannung gemittelt — Zyklen entfällt dort, da sich
Zyklenzahlen nicht sinnvoll summieren lassen) und den einzelnen Packs; die
Auswahl gilt seitenübergreifend für alle drei Tabs.

Nur die Kopfzeile wird sekündlich aktualisiert (kleiner, günstiger Redraw); der
Rest der Seite zeichnet komplett nur neu, wenn sich wirklich etwas ändert (neue
BMS-Daten, Tab-Wechsel, Pack-Wechsel, Kalibrierung) — ein voller Redraw im
Sekundentakt hätte sichtbar geblitzt.

## Pack-Erkennung & Verhalten bei Trennung

Die Anzahl der Packs wird **nicht konfiguriert**, sondern kommt live vom BMS:
die Antwort auf das Analogdaten-Kommando (CID2 `0x42`, Anfrage mit Pack-Byte
`FF` = alle Packs) beginnt mit einem 2-Zeichen-Hex-Feld, das die Pack-Anzahl
angibt (`PaceBmsClient::readAnalogData()`); danach folgen die Werte für jedes
gemeldete Pack der Reihe nach. Diese Zahl bestimmt, was Display/Web-UI/MQTT
anzeigen.

Damit ein Pack, das vorübergehend nicht mehr antwortet, nicht einfach mit
veralteten (falschen) Werten stehen bleibt oder wortlos verschwindet:

- **Meldet das BMS selbst eine kleinere Pack-Anzahl** (Pack wurde z.B. abgeklemmt,
  BMS antwortet aber weiter): Das fehlende Pack wird sofort auf 0 zurückgesetzt,
  bleibt aber als Slot sichtbar/in MQTT veröffentlicht — die Pack-Anzahl selbst
  schrumpft nie von selbst wieder.
- **Antwortet das BMS gar nicht mehr** (Timeout): Erst nach
  `BMS_ZERO_AFTER_CONSECUTIVE_FAILURES` (3, in `Config.h`) aufeinanderfolgenden
  fehlgeschlagenen Poll-Zyklen werden alle bekannten Pack-Slots auf 0 gesetzt —
  entprellt, damit ein einzelner verpasster/fehlerhafter Frame nicht sofort alles
  auf 0 blitzen lässt.

Bekannte Lücke: Die einzelnen Zellspannungs-MQTT-Topics (`pack_N/v_cells/cell_i`)
werden beim Zurücksetzen nicht explizit mit 0 nachpubliziert (die Publish-Schleife
dafür läuft einfach nicht mehr, da `cellCount` auf 0 gesetzt wird) — die
Pack-Summenwerte (Spannung/Strom/SOC/Warnungen/Kapazität) werden aber korrekt
genullt.

## Simulationsmodus (Entwicklung/Vorschau ohne BMS)

`SIMULATE_BMS_DATA` in `include/Config.h` — wenn `true`, liefert
`SimulatedBms::fillSimulatedSnapshot()` anstelle einer echten BMS-Abfrage ein
erfundenes, aber plausibles und langsam driftendes 3-Pack-Testszenario (16
Zellen/Pack wie bei echten 16S-LiFePO4-Packs, realistische Zellspannungs-Kennlinie
mit flachem Mittelplateau, SOC/Strom pendelnd über ~4 Minuten). Damit lassen
sich Display/Web-UI ohne angeschlossenes BMS entwickeln und testen.

**Wichtig:** Vor dem Anschluss eines echten BMS unbedingt auf `false` zurückstellen
und neu flashen — sonst werden dauerhaft Fake-Daten statt echter Messwerte
angezeigt, ohne dass das offensichtlich wäre.

## Architektur: zwei Cores, damit das Display nie einfriert

Die Firmware läuft auf beiden ESP32-Kernen (FreeRTOS), bewusst getrennt:

- **Core 1** (`src/main.cpp`, Arduino-Standard-`loop()`): ausschließlich Display +
  Touch (`BmsDisplayUi::update()`). Blockiert nie länger als eine SPI-Transaktion.
- **Core 0** (`src/NetworkTask.*`): WLAN/Captive-Portal, MQTT, Webserver und
  BMS-Polling. Genau das sind die Stellen, die je nach Zustand spürbar blockieren
  können — `PaceBmsClient::readFrame()` wartet bis zu `BMS_RESPONSE_TIMEOUT_MS`
  (500ms) auf eine Antwort, und das bis zu 5× pro Poll-Zyklus; ist gerade kein BMS
  angeschlossen/antwortend, addiert sich das auf über eine Sekunde Blockierung pro
  Zyklus. Läuft das auf demselben Core wie das Display, wirkt die Bedienung träge
  bis eingefroren, obwohl das Display selbst nichts dafür kann.

Beide Seiten tauschen sich ausschließlich über `SnapshotStore` aus: ein Mutex-
geschütztes Exemplar von `PaceBmsSnapshot`, das Core 0 nach jedem erfolgreichen
Poll per `set()` veröffentlicht und Core 1 (und die Webserver-Handler, die in
ESPAsyncWebServers eigenem AsyncTCP-Task laufen, also einem dritten Kontext) per
`get()` als Kopie abholen. Ohne diesen Mutex wäre das Lesen/Schreiben der darin
enthaltenen `String`-Felder aus mehreren Tasks gleichzeitig ein echtes Absturzrisiko,
kein nur theoretisches.

## WLAN & MQTT einrichten (keine Zugangsdaten im Code)

Es gibt keine kompilierten WLAN-/MQTT-Zugangsdaten mehr. Beim ersten Boot (oder
wann immer kein funktionierendes WLAN gespeichert ist) öffnet das Gerät ein
Einrichtungsportal:

1. Mit dem Hotspot **`PaceBMS-Setup`** verbinden (offen, kein Passwort). Die
   meisten Geräte öffnen die Portal-Seite automatisch (Captive-Portal-Erkennung);
   sonst manuell `http://10.0.0.1` aufrufen.
2. WLAN-SSID/Passwort **und** MQTT-Broker/Port/User/Passwort eintragen (beides auf
   derselben Seite, da keins davon im Voraus bekannt ist).
3. Nach dem Speichern verbindet sich das Gerät und startet MQTT/Webserver.

Beides — WLAN und MQTT — lässt sich später **unabhängig voneinander** ändern, ohne
das Portal erneut zu öffnen: über den **Konfiguration**-Tab der Weboberfläche
(`http://<ip>/konfiguration`), zwei getrennte Formulare mit eigenem
Speichern-Button. Passwortfelder leer lassen = unverändert übernehmen. Jedes
Speichern startet das Gerät neu, um die neuen Werte zu übernehmen.

Das Portal bricht nie mehr in den Bedienfluss ein: Verbindungsaufbau und Portal
laufen nicht-blockierend auf dem Netzwerk-Task (siehe Architektur oben), das
Display bleibt währenddessen normal bedienbar.

## Bauen & Flashen

1. PlatformIO installieren (CLI oder VS-Code-Extension).
2. Bei Bedarf `include/Config.h` anpassen (Pins, Poll-Intervall, MQTT-Basistopic,
   HA-Discovery an/aus, AP-Name/IP der Einrichtung).
3. Bauen und flashen:
   ```
   pio run
   pio run -t upload
   ```
4. WLAN/MQTT wie oben beschrieben über das Einrichtungsportal konfigurieren.

## Web-Oberfläche

Nach dem Verbinden läuft ein Webserver auf Port 80 mit Tabs analog zum Display
(Übersicht/Zellen/Status/Steuerung, aktualisiert alle 5 Sekunden per
`GET /api/data`, JSON) plus dem oben beschriebenen **Konfiguration**-Tab.

## MQTT / Home Assistant

Alle Werte werden unter dem Topic-Präfix `pacebms/...` veröffentlicht (siehe
`Config.h` → `MQTT_BASE_TOPIC`), inklusive Home-Assistant-MQTT-Discovery
(`homeassistant/...`) für Zellspannungen, Temperaturen, SOC/SOH, Zyklen,
Warnungen sowie Schutz-/FET-/Balancing-Status als Binary Sensors. Broker/Port/
User/Passwort werden wie oben beschrieben eingerichtet, nicht im Code.

## Projektstruktur

- `include/PaceBmsProtocol.h`, `src/PaceBmsProtocol.cpp` — Frame-Aufbau,
  Checksum/LCHKSUM, Antwort-Parsing (protokoll-, nicht hardwarespezifisch).
- `include/PaceBmsClient.h`, `src/PaceBmsClient.cpp` — Kommandos (Version,
  Seriennummer, Analogdaten, Kapazität, Warn-Info) über eine `HardwareSerial`.
- `include/BmsData.h` — Datenstrukturen für den zuletzt gelesenen Zustand.
- `include/SnapshotStore.h`, `src/SnapshotStore.cpp` — Mutex-geschützter
  Austausch des Snapshots zwischen Core 0 und Core 1 (siehe Architektur oben).
- `include/NetworkTask.h`, `src/NetworkTask.cpp` — FreeRTOS-Task (Core 0):
  WLAN/MQTT/Webserver/BMS-Polling.
- `include/CredentialsStorage.h`, `src/CredentialsStorage.cpp` — WLAN-/MQTT-
  Zugangsdaten, NVS-persistiert, getrennt speicherbar.
- `src/WifiProvisioning.*` — nicht-blockierendes WLAN-Connect + Captive-Portal
  (WiFiManager-Lib).
- `src/MqttManager.*`, `src/WebUiServer.*` — MQTT/HA-Discovery, Web-UI inkl.
  Konfiguration-Tab.
- `src/DisplayHardware.*` — TFT_eSPI/XPT2046-Init, Backlight-PWM.
- `src/StorageCalibration.*` — Touch-Kalibrierung, NVS-persistiert.
- `src/TouchCalibration.*` — 4-Ecken-Kalibrierroutine (10s-Dauerdruck-Trigger) +
  kalibrierte Touch-Abfrage.
- `src/BmsDisplayUi.*` — Dashboard-Zeichnen + Tab-/Pack-Navigation
  (Tap + horizontales Wischen), Touch-Handling.
- `include/SimulatedBms.h`, `src/SimulatedBms.cpp` — Fake-Datengenerator für den
  Simulationsmodus (siehe oben).
- `include/FactoryReset.h`, `src/FactoryReset.cpp` — Werksreset über den
  BOOT/FLASH-Button (siehe oben).
- `src/main.cpp` — Core-1-Einstiegspunkt: nur Display-Setup + Display-Loop.

## Lizenz

[PolyForm Noncommercial License 1.0.0](LICENSE) - nichtkommerzielle Nutzung frei
erlaubt, kommerzielle Nutzung ausgeschlossen. Keine Gewährleistung, siehe oben
und Lizenztext.
