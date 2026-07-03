/*
 * RFID_Reader.ino
 * ESP32-C6 + Joy-IT RC522 RFID Reader + WebSocket Server
 * Board: ESP32-C6 N4 | Core: Espressif Arduino Core 3.x
 * WebSocket-Lib: WebSockets by Markus Sattler (arduinoWebSockets)
 * RFID-Lib:      MFRC522 by GithubCommunity
 * JSON-Lib:      ArduinoJson by Benoit Blanchon
 */

#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WebSocketsServer.h>
#include <ArduinoJson.h>

// ─── Konfiguration – vor dem Flashen anpassen ─────────────────────
const char*    WIFI_SSID     = "YOUR_SSID";
const char*    WIFI_PASSWORD = "YOUR_PASSWORD";
const uint16_t WS_PORT       = 8765;

const int PIN_RFID_RST  = 3;
const int PIN_RFID_MISO = 2;
const int PIN_RFID_MOSI = 7;
const int PIN_RFID_SCK  = 6;
const int PIN_RFID_NSS  = 10;
// ─────────────────────────────────────────────────────────────────

MFRC522        rfid(PIN_RFID_NSS, PIN_RFID_RST);
WebSocketsServer wsServer(WS_PORT);

enum RfidMode { MODE_OFF, MODE_ON };
RfidMode rfidMode = MODE_OFF;

int8_t   wsClientNum   = -1;   // aktiver Client (-1 = keiner)
String   lastCardUID   = "";
unsigned long lastCardTime = 0;
const unsigned long CARD_COOLDOWN_MS = 2000;

// ─── WiFi ─────────────────────────────────────────────────────────
void connectWiFi() {
    Serial.printf("[WiFi] Verbinde mit %s", WIFI_SSID);
    WiFi.mode(WIFI_STA);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
    }
    Serial.printf("\n[WiFi] Verbunden – IP: %s\n", WiFi.localIP().toString().c_str());
}

// ─── UID als Großbuchstaben-Hexstring ─────────────────────────────
String uidToHex(MFRC522::Uid& uid) {
    String s = "";
    for (byte i = 0; i < uid.size; i++) {
        if (uid.uidByte[i] < 0x10) s += "0";
        s += String(uid.uidByte[i], HEX);
    }
    s.toUpperCase();
    return s;
}

// ─── WebSocket-Ereignisbehandlung ─────────────────────────────────
void onWsEvent(uint8_t num, WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {

        case WStype_CONNECTED: {
            IPAddress ip = wsServer.remoteIP(num);
            Serial.printf("[WS] Client #%u verbunden von %s\n", num, ip.toString().c_str());
            wsClientNum = (int8_t)num;
            wsServer.sendTXT(num, "{\"type\":\"connected\",\"state\":\"off\"}");
            break;
        }

        case WStype_DISCONNECTED:
            Serial.printf("[WS] Client #%u getrennt\n", num);
            if (wsClientNum == (int8_t)num) wsClientNum = -1;
            // RFID-Modus bleibt unverändert
            break;

        case WStype_TEXT: {
            String msg = String((char*)payload);
            Serial.printf("[WS] Empfangen: %s\n", msg.c_str());

            JsonDocument doc;
            DeserializationError err = deserializeJson(doc, msg);
            if (err) {
                Serial.printf("[WS] JSON-Fehler: %s\n", err.c_str());
                break;
            }

            const char* command = doc["command"];
            if (strcmp(command, "rfid") == 0) {
                bool state = doc["state"].as<bool>();
                if (state) {
                    rfidMode = MODE_ON;
                    Serial.println("[RFID] Modus → ON");
                    wsServer.sendTXT(num, "{\"type\":\"state\",\"value\":\"on\"}");
                } else {
                    rfidMode = MODE_OFF;
                    Serial.println("[RFID] Modus → OFF");
                    wsServer.sendTXT(num, "{\"type\":\"state\",\"value\":\"off\"}");
                }
            } else if (strcmp(command, "ping") == 0) {
                wsServer.sendTXT(num, "{\"type\":\"pong\"}");
            } else {
                Serial.printf("[WS] Unbekannter Befehl: %s\n", command ? command : "(null)");
            }
            break;
        }

        default:
            break;
    }
}

// ─── RFID-Polling (nicht blockierend) ────────────────────────────
void pollRFID() {
    if (rfidMode != MODE_ON) return;
    if (!rfid.PICC_IsNewCardPresent()) return;
    if (!rfid.PICC_ReadCardSerial()) return;

    String uid = uidToHex(rfid.uid);
    unsigned long now = millis();

    bool sameCard    = (uid == lastCardUID);
    bool cooldownOk  = (now - lastCardTime) >= CARD_COOLDOWN_MS;

    if (!sameCard || cooldownOk) {
        lastCardUID  = uid;
        lastCardTime = now;
        Serial.printf("[RFID] Karte erkannt: %s\n", uid.c_str());
        if (wsClientNum >= 0) {
            String json = "{\"type\":\"card\",\"uid\":\"" + uid + "\"}";
            wsServer.sendTXT((uint8_t)wsClientNum, json);
        }
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
}

// ─── Setup ────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[SYS] Starte RFID-WebSocket-Bridge");

    connectWiFi();

    // SPI mit expliziten Pins initialisieren (ESP32-C6 hat kein festes Pinout)
    SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_NSS);
    rfid.PCD_Init();
    Serial.println("[RFID] Reader initialisiert");

    wsServer.begin();
    wsServer.onEvent(onWsEvent);
    Serial.printf("[WS] Server läuft auf ws://%s:%u\n",
                  WiFi.localIP().toString().c_str(), WS_PORT);
}

// ─── Loop ─────────────────────────────────────────────────────────
void loop() {
    wsServer.loop();
    pollRFID();
}
