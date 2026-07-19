# ESP32 PaceBMS

> **Vorabversion (Pre-Release).** Nachbau, Verdrahtung und Nutzung auf eigene
> Gefahr - keine Garantie für Vollständigkeit, Richtigkeit oder Eignung für
> einen bestimmten Zweck. Insbesondere der eigentliche BMS-Protokoll-Teil wurde
> bisher **nicht gegen ein echtes PACE-BMS verifiziert** (siehe „Stand / Umfang"
> unten) - vor produktivem Einsatz an einem echten Akkupack selbst prüfen.

ESP32-Firmware zum Auslesen eines PACE-basierten BMS mit Web-Oberfläche und
MQTT/Home-Assistant-Anbindung. Zwei wählbare Anschlussarten (Umschalter im
Konfiguration-Tab):

- **RS232** (Standard) — das ASCII-Hex-Protokoll, portiert von
  [Tertiush/bmspace](https://github.com/Tertiush/bmspace) (Python).
- **Modbus RTU / RS485** — nach dem offiziellen Modbus-Registerdokument von
  [syssi/esphome-pace-bms](https://github.com/syssi/esphome-pace-bms) (siehe
  „Modbus RTU / RS485" unten für Einschränkungen).

## Stand / Umfang

- **Nur Lesen, bewusst.** Implementiert sind ausschließlich Auslese-Kommandos
  (Version, Seriennummer, Zellspannungen/Temperaturen/Strom/Spannung, Kapazität,
  Warn-/Schutz-/Balancing-Status). Das offizielle PACE-RS232-Protokoll definiert
  zwar auch Schreib-/Steuerbefehle (u.a. Lade-/Entlade-MOSFET schalten, Buzzer,
  Strombegrenzung-Gangwahl) — diese werden hier aus Sicherheitsgründen bewusst
  **nicht** implementiert, da ein versehentlich geschaltetes MOSFET an einem
  Akkupack ein echtes Risiko ist, kein reines Komfort-Feature.
- Unterstützt mehrere parallelgeschaltete Packs (wie das Python-Original), sofern
  das BMS das meldet — Display, Web-UI und MQTT decken alle gemeldeten Packs ab
  (siehe „Pack-Erkennung & Verhalten bei Trennung" unten).
- MQTT mit **Home-Assistant-Autoerkennung** (MQTT Discovery) — Sensoren erscheinen
  automatisch in Home Assistant, keine manuelle YAML-Konfiguration nötig (siehe
  „MQTT / Home Assistant" unten).
- **Zwei BMS-Anschlussarten** (RS232 oder Modbus RTU/RS485), umschaltbar über den
  Konfiguration-Tab, ohne Neu-Flashen — siehe „Modbus RTU / RS485" unten.

## Kompatible Geräte (ungetestet)

Folgende Geräte laufen laut [syssi/esphome-pace-bms](https://github.com/syssi/esphome-pace-bms)
mit einem PACE-BMS-Innenleben über dessen RS485/Modbus-Protokoll — mit dem hier
neu eingebauten Modbus-Modus (siehe unten) also potenziell direkt nutzbar, auch
wenn wir das an keinem dieser Geräte selbst getestet haben:

- Katbatt 6.4kWh LiFePO4 (PACE BMS P16S200A)
- Gobel Power GP-SR1-LF280-RN150 51.2V 280Ah (PACE BMS S16A150)
- Joyvoit Suns Energy Battery JVBW5KW (PACE BMS P16S100A)
- Orient Power Wall Mounted Battery 48V100AH
- SOK 100Ah 48V Server Rack Berry (PACE BMS P16S100A)
- MeritSun / i-finity LFP 200 - 48V
- Revov R100 51.2V 100Ah
- NPP 51.2V 280AH FLCD-30048
- PowMr 51.2V 100Ah (PACE BMS P16S150A)
- BSL Batt (mehrere Modelle, Firmware 1.51)
- Docan Energy Panda Battery Bank

Zusätzlich vom Python-Referenzprojekt (RS232, dieselbe Protokollfamilie wie hier)
bestätigt: Greenrich U-P5000, Hubble Lithium (AM2, AM4, X-101), Revov R9, SOK 48V
(100Ah), YouthPower Rack Module, Allith 10kW, Joyvoit BW5KW.

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

### Modbus RTU / RS485 (alternativer Anschluss, UART1)

Anderer physischer Port am BMS als RS232 (siehe „Modbus RTU / RS485" weiter
unten für Protokoll-Details/Einschränkungen). Nutzt ein MAX485-Modul mit
gemeinsamer DE/RE-Steuerleitung (ein GPIO für Sende-/Empfangsumschaltung, üblich
bei günstigen Modulen):

| ESP32 (UART1) | MAX485-Modul |
|---------------|--------------|
| GPIO26 (TX)   | DI           |
| GPIO25 (RX)   | RO           |
| GPIO13        | DE + RE (verbunden) |
| 3.3V          | VCC          |
| GND           | GND          |

Modul-Pins `A`/`B` zum RS485-Port des BMS (RJ45, siehe
[syssi/esphome-pace-bms](https://github.com/syssi/esphome-pace-bms) für
Pinbelegung/Farbcode). UART-Parameter: **9600 Baud, 8N1**.

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
- **System** — Laufzeit, WLAN-Signal, freier Speicher, Chip/CPU/Flash-Info,
  ein Schalter für den Simulationsmodus (unten) sowie ein **Neustart**-Button
  (oben rechts, bewusst räumlich getrennt vom Simulations-Schalter, damit man
  die beiden Touch-Flächen nicht verwechselt).

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

## Modbus RTU / RS485

Alternative zum RS232-Anschluss, umschaltbar im **Konfiguration**-Tab (Web) —
Auswahl speichern startet neu, wie bei WLAN/MQTT-Änderungen. Implementiert
nach dem Registerdokument
[PACE-BMS-Modbus-Protocol-for-RS485-V1.3-20170627.pdf](https://github.com/syssi/esphome-pace-bms/blob/main/docs/PACE-BMS-Modbus-Protocol-for-RS485-V1.3-20170627.pdf)
(`syssi/esphome-pace-bms`). Ein einzelner Read-Holding-Registers-Request
(Register 0-36) liefert Strom/Spannung/SOC/SOH/Kapazitäten/Zyklen, Warn-/
Schutz-/Status-Flags, alle 16 Zellspannungen sowie 6 Temperatursensoren (4×
Zelle, MOSFET, Umgebung) in einem Rutsch — Version/Seriennummer werden über
Modbus nicht gelesen.

**Einschränkungen gegenüber RS232:**
- Nur **ein Pack** — das Modbus-Registerschema kennt kein Mehrpack-Konzept wie
  der RS232-Befehl `0x42` ("alle Packs").
- Keine Entsprechung für "Vollgeladen", "Pack Indicate" und "Netzteil/AC-In" im
  Status-Tab — diese Felder gibt es im Modbus-Registersatz schlicht nicht,
  bleiben also leer/aus.
- Version/Seriennummer werden nicht ausgelesen (Anzeige zeigt "Modbus" statt
  einer echten Versionsnummer).

Bisher **nicht an echter Hardware getestet** — nur der Protokoll-Code selbst
(CRC16, Framing) wurde gegen die PDF-Spezifikation implementiert.

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

Die einzelnen Zellspannungs-/Temperatur-MQTT-Topics (`pack_N/v_cells/cell_i`,
`pack_N/temps/temp_i`) werden dabei ebenfalls auf 0 nachpubliziert (`MqttManager`
merkt sich je Pack die höchste je gemeldete Zell-/Sensorzahl und veröffentlicht
bis dahin immer, auch wenn die aktuelle Anzahl kleiner ist) — keine der
retained-Werte bleibt auf einem alten Stand stehen.

## Simulationsmodus (Entwicklung/Vorschau ohne BMS)

Liefert anstelle einer echten BMS-Abfrage ein erfundenes, aber plausibles und
langsam driftendes 3-Pack-Testszenario (16 Zellen/Pack wie bei echten
16S-LiFePO4-Packs, realistische Zellspannungs-Kennlinie mit flachem
Mittelplateau, SOC/Strom pendelnd über ~4 Minuten) - lässt sich Display/Web-UI
ohne angeschlossenes BMS entwickeln und testen.

Umschaltbar zur Laufzeit über den **System**-Tab (Display: Zeile "Simulation
(tippen)"; Web-UI: Button auf der System-Seite) - speichert die Einstellung im
NVS und startet neu. `SIMULATE_BMS_DATA` in `include/Config.h` ist nur der
Startwert für die allererste Inbetriebnahme.

**Wichtig:** Vor dem Anschluss eines echten BMS auf "AUS" umschalten - sonst
werden dauerhaft Fake-Daten statt echter Messwerte angezeigt, ohne dass das
offensichtlich wäre.

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

Das Gerät ist per mDNS auch unter `http://<OTA_HOSTNAME>.local` erreichbar
(Standard: `http://pacebms.local`), ohne die IP-Adresse nachschlagen zu müssen.

Für spätere Updates steht danach auch OTA über
[ElegantOTA](https://github.com/ayushsharma82/ElegantOTA) bereit:
`http://<ip-oder-hostname>/update` (HTTP-Basic-Auth, siehe `OTA_HOSTNAME`/
`OTA_PASSWORD` in `include/Config.h` — vor einem echten Einsatz das
Standardpasswort ändern). Ein Neustart-Button (Display: System-Tab, oben
rechts; Web-UI: System-Tab) startet das Gerät auch ohne Update jederzeit neu.

## Web-Oberfläche

Nach dem Verbinden läuft ein Webserver auf Port 80 mit Tabs analog zum Display
(Übersicht/Zellen/Status, aktualisiert alle 5 Sekunden per `GET /api/data`,
JSON; System per `GET /api/system`) plus dem oben beschriebenen
**Konfiguration**-Tab.

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
- `include/ModbusRtuProtocol.h`, `src/ModbusRtuProtocol.cpp` — Modbus-RTU-
  Framing (CRC16, Read-Holding-Registers), analog zu `PaceBmsProtocol`.
- `include/PaceModbusClient.h`, `src/PaceModbusClient.cpp` — liest das
  PACE-Modbus-Registerschema (siehe „Modbus RTU / RS485" oben) in dieselben
  `BmsData.h`-Strukturen wie `PaceBmsClient`.
- `include/BmsData.h` — Datenstrukturen für den zuletzt gelesenen Zustand,
  protokollunabhängig (RS232 und Modbus füllen dieselben Structs).
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
- `include/RuntimeSettings.h`, `src/RuntimeSettings.cpp` — NVS-persistierte
  Laufzeit-Einstellungen (Simulationsmodus, BMS-Anschlussart RS232/Modbus).
- `include/FactoryReset.h`, `src/FactoryReset.cpp` — Werksreset über den
  BOOT/FLASH-Button (siehe oben).
- `src/main.cpp` — Core-1-Einstiegspunkt: nur Display-Setup + Display-Loop.

## Lizenz

[PolyForm Noncommercial License 1.0.0](LICENSE) - nichtkommerzielle Nutzung frei
erlaubt, kommerzielle Nutzung ausgeschlossen. Keine Gewährleistung, siehe oben
und Lizenztext.
