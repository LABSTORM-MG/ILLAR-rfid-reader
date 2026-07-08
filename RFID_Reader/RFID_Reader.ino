/*
 * RFID_Reader.ino
 * ESP32-C6 + Joy-IT RC522 RFID Reader + WebSocket Client
 * Board: ESP32-C6 N4 | Core: Espressif Arduino Core 3.x
 * WebSocket-Lib: WebSockets by Markus Sattler (arduinoWebSockets)
 * RFID-Lib:      MFRC522 by GithubCommunity
 *
 * Verbindet sich als WebSocket-CLIENT zur Java-Middleware (Lagerverwaltung,
 * Endpoint /ws/auth) und spricht deren Tilde-Textprotokoll (CMD~KEY:VALUE~...).
 * Der Reader scannt kontinuierlich — die Middleware kennt keinen Befehl zum
 * Ein-/Ausschalten des Scannens, daher gibt es hier keine Modus-Umschaltung mehr.
 */

#include <SPI.h>
#include <MFRC522.h>
#include <WiFi.h>
#include <WebSocketsClient.h>

// ─── Konfiguration – vor dem Flashen anpassen ─────────────────────
const char*    WIFI_SSID     = "YOUR_SSID";
const char*    WIFI_PASSWORD = "YOUR_PASSWORD";

// Java-Middleware (Lagerverwaltung, Tomcat) — Adresse des Raspberry Pi
const char*    MW_HOST = "192.168.1.10";
const uint16_t MW_PORT = 8080;
const char*    MW_PATH = "/Lagerverwaltung/ws/auth";

const int PIN_RFID_RST  = 3;
const int PIN_RFID_MISO = 2;
const int PIN_RFID_MOSI = 7;
const int PIN_RFID_SCK  = 6;
const int PIN_RFID_NSS  = 10;
// ─────────────────────────────────────────────────────────────────

MFRC522          rfid(PIN_RFID_NSS, PIN_RFID_RST);
WebSocketsClient mwClient;

bool          mwConnected  = false;
String        lastCardUID  = "";
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

// ─── Middleware-WebSocket-Ereignisbehandlung ──────────────────────
// Der Reader empfängt nichts Handlungsrelevantes von der Middleware — es wird
// nur zu Diagnosezwecken geloggt (z.B. ERROR~MSG:... bei fehlerhaftem AUTH).
void onMwEvent(WStype_t type, uint8_t* payload, size_t length) {
    switch (type) {
        case WStype_CONNECTED:
            mwConnected = true;
            Serial.printf("[MW] Verbunden mit ws://%s:%u%s\n", MW_HOST, MW_PORT, MW_PATH);
            break;

        case WStype_DISCONNECTED:
            mwConnected = false;
            Serial.println("[MW] Getrennt — automatischer Reconnect läuft");
            break;

        case WStype_TEXT: {
            String msg = String((char*)payload, length);
            Serial.printf("[MW] Empfangen: %s\n", msg.c_str());
            break;
        }

        default:
            break;
    }
}

// ─── RFID-Polling (nicht blockierend) ────────────────────────────
void pollRFID() {
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
        if (mwConnected) {
            mwClient.sendTXT("AUTH~RFID:" + uid);
        }
    }

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();
}

// ─── Setup ────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(200);
    Serial.println("\n[SYS] Starte RFID-Middleware-Client");

    connectWiFi();

    // SPI mit expliziten Pins initialisieren (ESP32-C6 hat kein festes Pinout)
    SPI.begin(PIN_RFID_SCK, PIN_RFID_MISO, PIN_RFID_MOSI, PIN_RFID_NSS);
    rfid.PCD_Init();
    Serial.println("[RFID] Reader initialisiert");

    mwClient.begin(MW_HOST, MW_PORT, MW_PATH);
    mwClient.onEvent(onMwEvent);
    mwClient.setReconnectInterval(5000);
    Serial.printf("[MW] Verbinde zu ws://%s:%u%s ...\n", MW_HOST, MW_PORT, MW_PATH);
}

// ─── Loop ─────────────────────────────────────────────────────────
void loop() {
    mwClient.loop();
    pollRFID();
}
