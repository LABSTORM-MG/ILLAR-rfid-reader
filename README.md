# RFID-WebSocket-Bridge – ESP32-C6 + RC522

## Projektübersicht

Dieses Projekt verbindet einen Joy-IT RC522 RFID-Reader mit der Java-Middleware (`Lagerverwaltung`, Tomcat) auf dem Raspberry Pi über ein lokales WLAN-Netzwerk. Der ESP32-C6 liest RFID-Karten-UIDs kontinuierlich und verbindet sich dazu als WebSocket-**Client** zum `/ws/auth`-Endpoint der Middleware. Dort meldet er jede erkannte Karte per Tilde-Textprotokoll (`AUTH~RFID:<uid>`) — demselben Protokoll, das die Middleware bereits für Browser-Clients (`/ws/client`) verwendet.

Dieses Repo wird als `rfid-reader` Git-Submodule im Hauptrepo [storage-room](https://github.com/dejhfm/storage-room) eingebunden, das auch Frontend, InvenTree-Backend und die Gesamtarchitektur des ILLAR-Projekts dokumentiert.

| Komponente   | Rolle                                          |
|--------------|-------------------------------------------------|
| ESP32-C6 N4  | WebSocket-**Client**, SPI-Master                |
| RC522        | RFID-Reader (SPI-Slave)                         |
| Raspberry Pi | WebSocket-Server (Java-Middleware `/ws/auth`)   |

Es gibt keine Ein-/Ausschaltbarkeit des Scannens mehr — die Middleware kennt dafür keinen Befehl, daher scannt der Reader dauerhaft (mit 2000 ms Cooldown pro Karte, damit dieselbe Karte nicht mehrfach hintereinander gemeldet wird).

---

## Hardwareanforderungen

- ESP32-C6 N4 Entwicklungsboard
- Joy-IT RC522 RFID-Modul (MFRC522-Chip)
- 7 Dupont-Kabel (Stecker–Buchse)
- USB-Kabel für die Programmierung des ESP32

---

## Verdrahtungsübersicht ESP32-C6 ↔ RC522

| RC522-Pin | ESP32-C6-GPIO | Beschreibung     |
|-----------|---------------|------------------|
| SDA (NSS) | GPIO 10       | SPI Chip Select  |
| SCK       | GPIO 6        | SPI Clock        |
| MOSI      | GPIO 7        | SPI MOSI         |
| MISO      | GPIO 2        | SPI MISO         |
| IRQ       | —             | nicht verwendet  |
| GND       | GND           | Masse            |
| RST       | GPIO 3        | Reset            |
| 3.3V      | 3.3V          | Versorgungsspannung |

> **Wichtig:** Der RC522 wird zwingend mit 3,3 V betrieben. 5 V zerstören das Modul.

---

## Benötigte Bibliotheken

| Bibliothek         | Autor                  | Installation                                         |
|--------------------|------------------------|------------------------------------------------------|
| `MFRC522`          | GithubCommunity        | Arduino IDE → Bibliotheksverwalter → „MFRC522"       |
| `WebSockets`       | Markus Sattler         | Arduino IDE → Bibliotheksverwalter → „WebSockets"    |

Alle Bibliotheken sind mit ESP32-C6 und Espressif Arduino Core 3.x kompatibel. `ArduinoJson` wird nicht mehr benötigt, da der Reader jetzt das Tilde-Protokoll der Middleware statt JSON spricht.

---

## Board-Package Installation

1. Arduino IDE öffnen → **Datei → Voreinstellungen**
2. Unter „Zusätzliche Boardverwalter-URLs" eintragen:
   ```
   https://espressif.github.io/arduino-esp32/package_esp32_index.json
   ```
3. **Werkzeuge → Board → Boardverwalter** → nach „esp32" suchen → **esp32 by Espressif Systems** in Version 3.x installieren
4. Board auswählen: **Werkzeuge → Board → ESP32 Arduino → ESP32C6 Dev Module**

---

## Konfiguration vor dem Flashen

In `RFID_Reader.ino` die folgenden Konstanten am Anfang der Datei anpassen:

| Konstante       | Bedeutung                                | Beispielwert                |
|-----------------|--------------------------------------------|------------------------------|
| `WIFI_SSID`     | Name des WLAN-Netzwerks                    | `"MeinHeimnetz"`             |
| `WIFI_PASSWORD` | WLAN-Passwort                              | `"geheim123"`                |
| `MW_HOST`       | IP-Adresse des Raspberry Pi (Middleware)   | `"192.168.1.10"`             |
| `MW_PORT`       | Tomcat-Port der Middleware                 | `8080`                        |
| `MW_PATH`       | WebSocket-Pfad der Middleware              | `"/Lagerverwaltung/ws/auth"` |
| `PIN_RFID_RST`  | GPIO für RC522 RST                         | `3`                          |
| `PIN_RFID_MISO` | GPIO für SPI MISO                          | `2`                          |
| `PIN_RFID_MOSI` | GPIO für SPI MOSI                          | `7`                          |
| `PIN_RFID_SCK`  | GPIO für SPI Clock                         | `6`                          |
| `PIN_RFID_NSS`  | GPIO für SPI Chip Select (SDA)             | `10`                         |

---

## WebSocket-Protokoll

Der Reader verbindet sich als Client zu `ws://<MW_HOST>:<MW_PORT><MW_PATH>` und spricht das Tilde-Textprotokoll der Java-Middleware (`CMD~KEY:VALUE~KEY:VALUE`, siehe `Message`/`MessageParser` in `de.ross.websocket.protocol`).

### Nachrichten (ESP32 → Middleware)

| Nachricht            | Beschreibung                |
|-----------------------|------------------------------|
| `AUTH~RFID:A1B2C3D4`   | Erkannte Karten-UID melden   |

### Nachrichten (Middleware → ESP32)

Der Reader verarbeitet keine eingehenden Befehle mehr — es gibt kein Ein-/Ausschalten des Scannens von der Middleware aus (die Middleware kennt dafür keinen Befehl). Eingehende Nachrichten (z.B. `ERROR~MSG:...` bei fehlerhaftem Format) werden nur zu Diagnosezwecken über Serial geloggt.

### UID-Format

Die UID im Feld `"uid"` ist ein zusammenhängender Hexadezimal-String in Großbuchstaben, ohne Leerzeichen oder Trennzeichen. Jedes Byte wird als zwei Hex-Ziffern dargestellt.

**Beispiele:**

| Karten-Bytes (dezimal) | `"uid"`-Wert |
|------------------------|--------------|
| 161, 178, 195, 212     | `"A1B2C3D4"` |
| 1, 2, 3, 4             | `"01020304"` |

Ein Cooldown von 2000 ms verhindert, dass dieselbe Karte mehrfach hintereinander gesendet wird.

---

## Verbindung zur Middleware

Der ESP32 baut die Verbindung auf — nicht umgekehrt. Ziel ist der `/ws/auth`-Endpoint der Java-Middleware (`AuthWebsocket`, Projekt `Lagerverwaltung`):

```
ws://<MW_HOST>:<MW_PORT>/Lagerverwaltung/ws/auth
```

**Beispiel:**
```
ws://192.168.1.10:8080/Lagerverwaltung/ws/auth
```

Die Middleware muss vor dem Einschalten des Readers laufen (Tomcat-Deployment von `Lagerverwaltung`); dank `setReconnectInterval()` verbindet sich der ESP32 automatisch, sobald sie erreichbar ist.

### Manuell testen (ohne Hardware)

Zum Testen der Middleware-Seite kann jedes WebSocket-Tool die Rolle des ESP32 simulieren, z.B. `wscat`:

```bash
npm install -g wscat
wscat -c ws://192.168.1.10:8080/Lagerverwaltung/ws/auth

> AUTH~RFID:A1B2C3D4
```

Alternativ bietet die Middleware selbst eine Debug-Seite unter `web/rfid-test.html` (Karte „RFID Scanner" / `/ws/auth`) mit Log-Ansicht.

### Hinweise zum Verbindungsverhalten

- Mehrere gleichzeitige Verbindungen zu `/ws/auth` sind möglich (z.B. echter ESP32 + `rfid-test.html` parallel) — die Middleware verwaltet alle Auth-Sessions in einem Set.
- Der Reader scannt dauerhaft; es gibt keinen Ein-/Aus-Zustand mehr.
- Bricht die Verbindung ab, versucht der ESP32 alle 5 Sekunden automatisch, sich neu zu verbinden.

---

## Projektstruktur

```
RFID_Reader/
├── RFID_Reader.ino   – Vollständiger Arduino-Quellcode
└── README.md         – Diese Dokumentation
```
