/**
 * MondPhase.ino – ESP32 Arduino Sketch
 *
 * Lädt Uhrzeit per NTP, berechnet Mond- und Sonnenposition mit astro.cpp
 * und ruft drawMoonPhase(radius=240) auf.
 *
 * Vom Nutzer zu ergänzen:
 *   - WIFI_SSID_1..3 / WIFI_PASSWORD_1..3  (in credentials.h)
 */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <time.h>

#include <Adafruit_GFX.h>     // Core graphics library
#include <Adafruit_GC9A01A.h>

// ── Konfiguration ────────────────────────────────────────────────────────────

#include "astro.h"
#include "credentials.h"
#include "clock_face.h"
#include "display_mode.h"
#include "commands.h"
#include "messages.h"

WiFiMulti wifiMulti;

// NTP
constexpr long   GMT_OFFSET_SEC  = 0;    // UTC verwenden; Zeitzone lokal egal
constexpr int    DAYLIGHT_OFFSET = 0;
constexpr char   NTP_SERVER[]    = "pool.ntp.org";

// Mond-Rendering
extern void calculateMoon(const struct tm& timeinfo, bool printInfo, Adafruit_GC9A01A* tft);

// Persistente Konfiguration (definiert in Config.ino)
extern int    configDisplayMode;
extern void   loadConfig();
extern void   saveConfig();

// Telegram-Bot (definiert in Telegram.ino)
extern void setupTelegram();
extern void handleTelegram();

// Display
#define TFT_CS 7
#define TFT_DC 10

Adafruit_GC9A01A tft(TFT_CS, TFT_DC);

// Display-Mode
enum mode { MODE_MOON, MODE_STATIC, MODE_DISPLAY, MODE_CLOCK };
mode currentMode = MODE_MOON;

bool wifiOn = true;

// ── Serielle Befehle ─────────────────────────────────────────────────────────

// Liest Zeichen bis zum Zeilenende und führt die Zeile als Befehl aus (Commands.ino).
// Zu lange Zeilen werden abgeschnitten.
static char serialLine[100];
static size_t serialLineLen = 0;

static void readSerialCommands() {
    while (Serial.available() > 0) {
        const char c = (char) Serial.read();
        if (c != '\r' && c != '\n') {
            if (serialLineLen < sizeof(serialLine) - 1) serialLine[serialLineLen++] = c;
            continue;
        }
        if (serialLineLen == 0) continue;
        serialLine[serialLineLen] = '\0';
        serialLineLen = 0;
        Reply reply = runCommand(CH_SERIAL, serialLine, "");
        if (reply.html) {
            reply.text.replace("<pre>", "\n");
            reply.text.replace("</pre>", "");
        }
        Serial.println(reply.text);
    }
}

// ── Setup & Loop ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    loadConfig();

    WiFi.mode(WIFI_STA);
    // WLAN verbinden (alle konfigurierten Netzwerke)
    if (strlen(WIFI_SSID_1) > 0) wifiMulti.addAP(WIFI_SSID_1, WIFI_PASSWORD_1);
    if (strlen(WIFI_SSID_2) > 0) wifiMulti.addAP(WIFI_SSID_2, WIFI_PASSWORD_2);
    if (strlen(WIFI_SSID_3) > 0) wifiMulti.addAP(WIFI_SSID_3, WIFI_PASSWORD_3);

    // NTP starten (UTC)
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, NTP_SERVER);

    tft.begin();
    tft.fillScreen(GC9A01A_BLACK);

    setupTelegram();

    requestRedraw();
}

unsigned long lastMoonCalc = 60000; // Erzwinge Berechnung direkt nach Start, da loop() erst nach 60s aktualisiert
unsigned long lastWifiCheck = 0;

long lastClockMinute = -1;

// Erzwingt ein Neuzeichnen im konfigurierten Anzeigemodus im nächsten loop()-Durchlauf.
void requestRedraw() {
    currentMode = (configDisplayMode == DISPLAY_MODE_CLOCK) ? MODE_CLOCK : MODE_MOON;
    lastMoonCalc = millis() - 60001;
    lastClockMinute = -1;
    clockFaceInvalidate();
}

// Setzt den Anzeigemodus, speichert ihn und zeichnet neu.
void setDisplayMode(int displayMode) {
    configDisplayMode = displayMode;
    saveConfig();
    requestRedraw();
}

// Zeigt den Mond zu einer festen Zeit, bis requestRedraw() wieder die Live-Anzeige startet.
void showMoonAt(const struct tm& utc) {
    currentMode = MODE_STATIC;
    calculateMoon(utc, true, &tft);
}

bool wifiEnabled() {
    return wifiOn;
}

void setWifiEnabled(bool on) {
    if (!on) WiFi.disconnect(true);
    wifiOn = on;
}

void loop() {
    
    if (wifiOn && millis() - lastWifiCheck > 1000) {
        wifiMulti.run(1000);
        lastWifiCheck = millis();
    }

    readSerialCommands();
    handleTelegram();
    struct tm zeitInfo;
    if (currentMode == MODE_MOON && getLocalTime(&zeitInfo) && (millis() - lastMoonCalc > 60000)) {
        Serial.println("Updating moon display...");
        calculateMoon(zeitInfo, false, &tft);
        lastMoonCalc = millis();
    } else if (currentMode == MODE_DISPLAY) {
        // Hier könnte z.B. ein Wechsel zwischen verschiedenen Anzeigemodi implementiert werden
    } else if (currentMode == MODE_CLOCK) {
        // 24h-Uhr: einmal pro Minute aktualisieren
        time_t now = time(nullptr);
        if (now >= MIN_VALID_TIME && now / 60 != lastClockMinute) {
            unsigned long start = millis();
            drawClockFace(&tft, now);
            lastClockMinute = now / 60;
            Serial.printf("Uhr aktualisiert (%lu ms)\n", millis() - start);
        }
    }
    delay(100);
    //berechneSonnenaufgang();
    //berechneMondaufgang();
}


