/**
 * MondPhase.ino – ESP32 Arduino Sketch
 *
 * Lädt Uhrzeit per NTP, berechnet Mond- und Sonnenposition mit astro.cpp
 * und ruft drawMoonPhase(radius=240) auf.
 *
 * Vom Nutzer zu ergänzen:
 *   - WIFI_SSID_1..3 / WIFI_PASSWORD_1..3  (in credentials.h)
 *   - LATITUDE / LONGITUDE (Standort in Dezimalgrad)
 *   - outputBuffer  (MoonPhasePixel[480*480])
 *   - moonTexture   (const MoonPhasePixel* oder nullptr)
 *   - texSize       (Kantenlänge der Textur, oder 0)
 *   - Anzeige-Code nach drawMoonPhase()
 */

#include <WiFi.h>
#include <WiFiMulti.h>
#include <time.h>

#include <Adafruit_GFX.h>     // Core graphics library
#include <Adafruit_GC9A01A.h>

#include <SerialCommands.h>

// ── Konfiguration ────────────────────────────────────────────────────────────

#include "credentials.h"

WiFiMulti wifiMulti;

// NTP
constexpr long   GMT_OFFSET_SEC  = 0;    // UTC verwenden; Zeitzone lokal egal
constexpr int    DAYLIGHT_OFFSET = 0;
constexpr char   NTP_SERVER[]    = "pool.ntp.org";

// Mond-Rendering
extern void calculateMoon(const struct tm& timeinfo, bool printInfo, Adafruit_GC9A01A* tft);

// Display
#define TFT_CS 7
#define TFT_DC 10

Adafruit_GC9A01A tft(TFT_CS, TFT_DC);

// Display-Mode
enum mode { MODE_MOON, MODE_STATIC, MODE_DISPLAY, MODE_CLOCK };
mode currentMode = MODE_MOON;

bool wifiOn = true;

// -- Serial Commands

char serial_command_buffer_[100];
SerialCommands serial_commands_(&Serial, serial_command_buffer_, sizeof(serial_command_buffer_), "\r\n", " ");

//This is the default handler, and gets called when no other command matches. 
// Note: It does not get called for one_key commands that do not match
void cmd_unrecognized(SerialCommands* sender, const char* cmd)
{
  sender->GetSerial()->print("Unrecognized command [");
  sender->GetSerial()->print(cmd);
  sender->GetSerial()->println("]");
  sender->GetSerial()->println("Type 'help' for a list of commands.");
}

bool parseDateTime(const char* arg, struct tm& tm) {
    memset(&tm, 0, sizeof(tm));
    char* parsed = strptime(arg, "%d.%m.%Y %H:%M:%S", &tm);
    if (parsed == nullptr) {
        // Nur Uhrzeit: HH:MM – aktuelles Datum übernehmen
        struct tm now;
        if (!getLocalTime(&now)) {
            return false;
        }
        tm = now;
        parsed = strptime(arg, "%H:%M:%S", &tm);
    }
    if (parsed == nullptr) {
        return false;
    }
    return true;
}

void cmd_moon(SerialCommands* sender)
{
    struct tm tm;
    const char* arg0 = sender->Next();

    if (arg0 != nullptr) {
        char datetime[50];
        // Versuche zuerst vollständiges Datum+Zeit: DD.MM.YYYY HH:MM
        const char* arg1 = sender->Next();
        if (arg1 != nullptr) {
            snprintf(datetime, sizeof(datetime), "%s %s", arg0, arg1);
        } else {
            snprintf(datetime, sizeof(datetime), "%s", arg0);
        }
        if (!parseDateTime(datetime, tm)) {
            sender->GetSerial()->println("Ungültiges Format. Erwartet: HH:MM oder TT.MM.JJJJ HH:MM");
            return;
        }
    } else {
        if (!getLocalTime(&tm)) {
            sender->GetSerial()->println("Keine gültige NTP-Zeit verfügbar.");
            return;
        }
    }

    sender->GetSerial()->println("Calculating moon position...");
    currentMode = MODE_STATIC;
    calculateMoon(tm, true, &tft);
    sender->GetSerial()->println("Done.");
}
SerialCommand cmd_moon_("moon", cmd_moon);

void cmd_moon_run_(SerialCommands* sender)
{
    sender->GetSerial()->println("Starting dynamic moon display...");
    currentMode = MODE_MOON;
}
SerialCommand cmd_moon_run("moon_run", cmd_moon_run_);

void cmd_set_time_(SerialCommands* sender)
{
    const char* arg0 = sender->Next();
    const char* arg1 = sender->Next();
    if (arg0 == nullptr || (arg1 != nullptr && sender->Next() != nullptr)) {
        sender->GetSerial()->println("Ungültiges Format. Erwartet: TT.MM.JJJJ HH:MM");
        return;
    }
    struct tm tm;
    char datetime[50];
    if (arg1 != nullptr) {
        snprintf(datetime, sizeof(datetime), "%s %s", arg0, arg1);
    }
    if (!parseDateTime(datetime, tm)) {
        sender->GetSerial()->println("Ungültiges Format. Erwartet: TT.MM.JJJJ HH:MM");
        return;
    }
    time_t t = mktime(&tm);
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, nullptr);
    sender->GetSerial()->println("Zeit aktualisiert.");
}
SerialCommand cmd_set_time("set_time", cmd_set_time_);

void cmd_wifi_(SerialCommands* sender)
{
    sender->GetSerial()->println("Aktuelle WLAN-Verbindungen:");
    sender->GetSerial()->print(WiFi.SSID());
    sender->GetSerial()->print(" (");
    sender->GetSerial()->print(WiFi.RSSI());
    sender->GetSerial()->println(" dBm)");

    const char *arg0 = sender->Next();
    if (arg0 != nullptr) {
        if(strcmp(arg0, "on") == 0) {
            sender->GetSerial()->println("WLAN-Verbindung aktivieren...");
            wifiOn = true;
        } else if (strcmp(arg0, "off") == 0) {
            sender->GetSerial()->println("WLAN-Verbindung trennen...");
            WiFi.disconnect(true);
            wifiOn = false;
        } else {
            sender->GetSerial()->println("Ungültiges Argument. Erwartet: on oder off");
        }
    }
}
SerialCommand cmd_wifi("wifi", cmd_wifi_);

void cmd_help(SerialCommands* sender)
{
  sender->GetSerial()->println("Available commands:");
  sender->GetSerial()->println("  help - Show this help message");
  sender->GetSerial()->println("  moon [HH:MM | DD.MM.YYYY HH:MM] - Display moon phase for given time (or current time if no argument)");
  sender->GetSerial()->println("  moon_run - Start dynamic moon display (updates every minute)");
  sender->GetSerial()->println("  wifi [on|off] - Turn WiFi on or off");
  sender->GetSerial()->println("  set_time TT.MM.JJJJ HH:MM - Set system time (UTC)");
}
SerialCommand cmd_help_("help", cmd_help);

void setupSerialCommands() {
    serial_commands_.AddCommand(&cmd_help_);
    serial_commands_.AddCommand(&cmd_moon_);
    serial_commands_.AddCommand(&cmd_moon_run);
    serial_commands_.AddCommand(&cmd_set_time);
    serial_commands_.AddCommand(&cmd_wifi);
    serial_commands_.SetDefaultHandler(cmd_unrecognized);
}

// ── Setup & Loop ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    WiFi.mode(WIFI_STA);
    // WLAN verbinden (alle konfigurierten Netzwerke)
    if (strlen(WIFI_SSID_1) > 0) wifiMulti.addAP(WIFI_SSID_1, WIFI_PASSWORD_1);
    if (strlen(WIFI_SSID_2) > 0) wifiMulti.addAP(WIFI_SSID_2, WIFI_PASSWORD_2);
    if (strlen(WIFI_SSID_3) > 0) wifiMulti.addAP(WIFI_SSID_3, WIFI_PASSWORD_3);

    // NTP starten (UTC)
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, NTP_SERVER);

    tft.begin();
    tft.fillScreen(GC9A01A_BLACK);

    // Serial Commands
    setupSerialCommands();
}

unsigned long lastMoonCalc = 60000; // Erzwinge Berechnung direkt nach Start, da loop() erst nach 60s aktualisiert
unsigned long lastWifiCheck = 0;

void loop() {
    
    if (wifiOn && millis() - lastWifiCheck > 1000) {
        wifiMulti.run(1000);
        lastWifiCheck = millis();
    }

    serial_commands_.ReadSerial();
    struct tm zeitInfo;
    if (currentMode == MODE_MOON && getLocalTime(&zeitInfo) && (millis() - lastMoonCalc > 60000)) {
        Serial.println("Updating moon display...");
        calculateMoon(zeitInfo, false, &tft);
        lastMoonCalc = millis();
    } else if (currentMode == MODE_DISPLAY) {
        // Hier könnte z.B. ein Wechsel zwischen verschiedenen Anzeigemodi implementiert werden
    } else if (currentMode == MODE_CLOCK) {
        // Hier könnte z.B. eine Uhrzeit-Anzeige implementiert werden
    }
    delay(100);
    //berechneSonnenaufgang();
    //berechneMondaufgang();
}


