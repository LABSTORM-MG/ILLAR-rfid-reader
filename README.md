# RFID-WebSocket-Bridge – ESP32-C6 + RC522

## Projektübersicht

Dieses Projekt verbindet einen Joy-IT RC522 RFID-Reader mit einem Raspberry Pi über ein lokales WLAN-Netzwerk. Der ESP32-C6 liest RFID-Karten-UIDs und sendet sie per WebSocket an den Raspberry Pi. Der Raspberry Pi steuert den RFID-Reader (ein/aus) über denselben WebSocket-Kanal.

| Komponente   | Rolle                                |
|--------------|--------------------------------------|
| ESP32-C6 N4  | WebSocket-Server, SPI-Master         |
| RC522        | RFID-Reader (SPI-Slave)              |
| Raspberry Pi | WebSocket-Client, Empfänger der UIDs |

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
| `ArduinoJson`      | Benoit Blanchon        | Arduino IDE → Bibliotheksverwalter → „ArduinoJson"   |

Alle Bibliotheken sind mit ESP32-C6 und Espressif Arduino Core 3.x kompatibel.

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

| Konstante       | Bedeutung                        | Beispielwert       |
|-----------------|----------------------------------|--------------------|
| `WIFI_SSID`     | Name des WLAN-Netzwerks          | `"MeinHeimnetz"`   |
| `WIFI_PASSWORD` | WLAN-Passwort                    | `"geheim123"`      |
| `WS_PORT`       | WebSocket-Port                   | `8765`             |
| `PIN_RFID_RST`  | GPIO für RC522 RST               | `3`                |
| `PIN_RFID_MISO` | GPIO für SPI MISO                | `2`                |
| `PIN_RFID_MOSI` | GPIO für SPI MOSI                | `7`                |
| `PIN_RFID_SCK`  | GPIO für SPI Clock               | `6`                |
| `PIN_RFID_NSS`  | GPIO für SPI Chip Select (SDA)   | `10`               |

---

## WebSocket-Protokoll

Alle Nachrichten werden als JSON-Strings übertragen.

### Befehle (Raspberry Pi → ESP32)

| JSON                                    | Beschreibung                  |
|-----------------------------------------|-------------------------------|
| `{"command": "rfid", "state": true}`    | RFID-Reader aktivieren        |
| `{"command": "rfid", "state": false}`   | RFID-Reader deaktivieren      |
| `{"command": "ping"}`                   | Verbindung prüfen             |

### Nachrichten (ESP32 → Raspberry Pi)

| JSON                                      | Beschreibung                                              |
|-------------------------------------------|-----------------------------------------------------------|
| `{"type": "connected", "state": "off"}`   | Wird direkt nach Verbindungsaufbau gesendet (Initialzustand) |
| `{"type": "state", "value": "on"}`        | Bestätigung: RFID ist jetzt aktiv                         |
| `{"type": "state", "value": "off"}`       | Bestätigung: RFID ist jetzt inaktiv                       |
| `{"type": "pong"}`                        | Antwort auf ping                                          |
| `{"type": "card", "uid": "A1B2C3D4"}`    | Erkannte Karten-UID                                       |

### UID-Format

Die UID im Feld `"uid"` ist ein zusammenhängender Hexadezimal-String in Großbuchstaben, ohne Leerzeichen oder Trennzeichen. Jedes Byte wird als zwei Hex-Ziffern dargestellt.

**Beispiele:**

| Karten-Bytes (dezimal) | `"uid"`-Wert |
|------------------------|--------------|
| 161, 178, 195, 212     | `"A1B2C3D4"` |
| 1, 2, 3, 4             | `"01020304"` |

Ein Cooldown von 2000 ms verhindert, dass dieselbe Karte mehrfach hintereinander gesendet wird.

---

## Verbindungsanleitung für den Raspberry Pi

### WebSocket-URL

```
ws://<ESP32-IP>:<WS_PORT>
```

Die IP-Adresse des ESP32 wird nach dem Start im seriellen Monitor ausgegeben (Baudrate 115200).

**Beispiel:**
```
ws://192.168.1.42:8765
```

### Python-Beispiel (websockets-Bibliothek)

```python
import asyncio
import json
import websockets

async def main():
    uri = "ws://192.168.1.42:8765"
    async with websockets.connect(uri) as ws:
        # Initialzustand empfangen
        msg = json.loads(await ws.recv())
        print(f"Verbunden, Zustand: {msg['state']}")  # {"type": "connected", "state": "off"}

        # RFID aktivieren
        await ws.send(json.dumps({"command": "rfid", "state": True}))
        response = json.loads(await ws.recv())
        print(f"Antwort: {response}")  # {"type": "state", "value": "on"}

        # Karten lesen
        while True:
            msg = json.loads(await ws.recv())
            if msg["type"] == "card":
                print(f"UID: {msg['uid']}")

asyncio.run(main())
```

Installation der Python-Bibliothek:
```bash
pip install websockets
```

### Hinweise zum Verbindungsverhalten

- Es ist immer nur **ein Client** gleichzeitig verbunden.
- Trennt der Client die Verbindung, bleibt der RFID-Modus erhalten (ON oder OFF).
- Der WebSocket-Server läuft dauerhaft weiter und akzeptiert neue Verbindungen.

---

## Projektstruktur

```
RFID_Reader/
├── RFID_Reader.ino   – Vollständiger Arduino-Quellcode
└── README.md         – Diese Dokumentation
```
