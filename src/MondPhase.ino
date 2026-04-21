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
#include <SPI.h>

#include <SerialCommands.h>

// ── Konfiguration ────────────────────────────────────────────────────────────

#include "credentials.h"

WiFiMulti wifiMulti;

// Standort (Dezimalgrad)
constexpr double LATITUDE  =  49.821;
constexpr double LONGITUDE =   8.869;

// NTP
constexpr long   GMT_OFFSET_SEC  = 0;    // UTC verwenden; Zeitzone lokal egal
constexpr int    DAYLIGHT_OFFSET = 0;
constexpr char   NTP_SERVER[]    = "pool.ntp.org";

// Mond-Rendering
extern void calculateMoon(const struct tm& timeinfo, bool renderToDisplay, bool useTexture);

// Display
#define TFT_CS 7
#define TFT_DC 10

Adafruit_GC9A01A tft(TFT_CS, TFT_DC);

// Display-Mode
enum mode { MODE_MOON, MODE_STATIC, MODE_DISPLAY, MODE_CLOCK };
mode currentMode = MODE_MOON;


// ── Hilfsfunktionen ─────────────────────────

static double julianDateVonTm(const struct tm& t) {
    return astro::calculateJulianDate(
        t.tm_year + 1900,
        t.tm_mon  + 1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec,
        0
    );
}

// boolean isSummerTime(unsigned int yyyy, unsigned int mnth, unsigned int dd, unsigned int hh, int tzHours) 
// // European Daylight Savings Time calculation by "jurs" for German Arduino Forum
// // input parameters: "normal time" for year, month, day, hour and tzHours (0=UTC, 1=MEZ)
// // return value: returns true during Daylight Saving Time, false otherwise
// {
//   if (mnth < 3 || mnth > 10) return false; // keine Sommerzeit in Jan, Feb, Nov, Dez
//   if (mnth > 3 && mnth < 10) return true; // Sommerzeit in Apr, Mai, Jun, Jul, Aug, Sep
//   if ((mnth == 3 && (hh + 24 * dd) >= (1 + tzHours + 24 * (31 - (5 * yyyy / 4 + 4) % 7))) || ((mnth == 10) && (hh + 24 * dd) < (1 + tzHours + 24 * (31 - (5 * yyyy / 4 + 1) % 7))))
//     return true;
//   else
//     return false;
// }

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
    calculateMoon(tm, true, true);
    sender->GetSerial()->println("Done.");
}
SerialCommand cmd_moon_("moon", cmd_moon);

void cmd_moon_dynamic_(SerialCommands* sender)
{
    sender->GetSerial()->println("Starting dynamic moon display...");
    currentMode = MODE_MOON;
}

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
    for (int i = 0; i < wifiMulti.run(); i++) {
        sender->GetSerial()->print("  ");
        sender->GetSerial()->print(i);
        sender->GetSerial()->print(": ");
        sender->GetSerial()->print(WiFi.SSID(i));
        sender->GetSerial()->print(" (");
        sender->GetSerial()->print(WiFi.RSSI(i));
        sender->GetSerial()->println(" dBm)");
    }

    const char *arg0 = sender->Next();
    if (arg0 != nullptr) {
        if(strcmp(arg0, "on") == 0) {
            sender->GetSerial()->println("WLAN-Verbindung aktivieren...");
            wifiMulti.addAP(WIFI_SSID_1, WIFI_PASSWORD_1);
            wifiMulti.addAP(WIFI_SSID_2, WIFI_PASSWORD_2);
            wifiMulti.addAP(WIFI_SSID_3, WIFI_PASSWORD_3);
        } else if (strcmp(arg0, "off") == 0) {
            sender->GetSerial()->println("WLAN-Verbindung trennen...");
            WiFi.disconnect(true);
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
  sender->GetSerial()->println("  moon_display [HH:MM | DD.MM.YYYY HH:MM] - Display moon phase for given time (or current time if no argument)");
  sender->GetSerial()->println("  wifi [on|off] - Turn WiFi on or off");
  sender->GetSerial()->println("  set_time TT.MM.JJJJ HH:MM - Set system time (UTC)");
}
SerialCommand cmd_help_("help", cmd_help);

void setupSerialCommands() {
    serial_commands_.AddCommand(&cmd_help_);
    serial_commands_.AddCommand(&cmd_moon_);
    serial_commands_.AddCommand(&cmd_moon_dynamic);
    serial_commands_.AddCommand(&cmd_set_time);
    serial_commands_.AddCommand(&cmd_wifi);
    serial_commands_.SetDefaultHandler(cmd_unrecognized);
}

// ── Sonnenauf- / -untergang ──────────────────────────────────────────────────

static void printHHMM(const char* label, double decimalHours) {
    double h = fmod(decimalHours, 24.0);
    if (h < 0) h += 24.0;
    int hh = (int)h;
    int mm = (int)((h - hh) * 60.0 + 0.5);
    if (mm == 60) { hh = (hh + 1) % 24; mm = 0; }
    Serial.printf("  %s %02d:%02d UTC\n", label, hh, mm);
}

void berechneSonnenaufgang() {
    struct tm zeitInfo;
    if (!getLocalTime(&zeitInfo)) {
        Serial.println("Keine gültige NTP-Zeit für Sonnenberechnung.");
        return;
    }
    double jd = astro::calculateJulianDate(
        zeitInfo.tm_year + 1900,
        zeitInfo.tm_mon  + 1,
        zeitInfo.tm_mday);

    astro::SunriseSunset s = astro::calculateSunriseSunset(jd, LATITUDE, LONGITUDE);

    if (!s.valid) {
        Serial.println("  Sonnenauf/-untergang: Polartag oder Polarnacht.");
        return;
    }
    printHHMM("Sonnenaufgang:", s.rising + 2);
    printHHMM("Sonnenuntergang:", s.setting + 2);
}

// ── Mondauf- / -untergang ────────────────────────────────────────────────────

void berechneMondaufgang() {
    struct tm zeitInfo;
    if (!getLocalTime(&zeitInfo)) {
        Serial.println("Keine gültige NTP-Zeit für Mondberechnung.");
        return;
    }
    double jd0 = astro::calculateJulianDate(
        zeitInfo.tm_year + 1900,
        zeitInfo.tm_mon  + 1,
        zeitInfo.tm_mday);
    jd0 = round(jd0) - 0.5;  // Mitternacht UTC

    // Mondpositionen für jd0-1, jd0, jd0+1
    astro::MoonPosition mp1 = astro::calculateMoon(jd0 - 1);
    astro::MoonPosition mp2 = astro::calculateMoon(jd0);
    astro::MoonPosition mp3 = astro::calculateMoon(jd0 + 1);
    astro::RaDek r1 = astro::calculateRaDek(mp1.longitude, mp1.latitude);
    astro::RaDek r2 = astro::calculateRaDek(mp2.longitude, mp2.latitude);
    astro::RaDek r3 = astro::calculateRaDek(mp3.longitude, mp3.latitude);

    // RA in Grad, Sprünge um ±360° bei 0h/24h korrigieren
    double a1 = r1.ra * astro::RAD2DEG;
    double a2 = r2.ra * astro::RAD2DEG;
    double a3 = r3.ra * astro::RAD2DEG;
    if      (a2 - a1 >  180.0) a1 += 360.0;
    else if (a2 - a1 < -180.0) a1 -= 360.0;
    if      (a3 - a2 >  180.0) a3 -= 360.0;
    else if (a3 - a2 < -180.0) a3 += 360.0;

    double d1 = r1.dek * astro::RAD2DEG;
    double d2 = r2.dek * astro::RAD2DEG;
    double d3 = r3.dek * astro::RAD2DEG;

    // Horizontalparallaxe → Standard-Aufgangshöhe (Meeus Kap. 15)
    double parallax = asin(astro::EARTH_RADIUS / mp2.distance) * astro::RAD2DEG;
    double h0 = 0.7275 * parallax - 0.5667;

    // Greenwicher Sternzeit bei Mitternacht (Grad)
    double theta0 = astro::calculateSiderealTime(jd0) * 180.0 / 12.0;

    // Anfangsschätzung über Stundenwinkel bei jd0
    double cH0 = (sin(h0 * astro::DEG2RAD) - sin(LATITUDE * astro::DEG2RAD) * sin(r2.dek))
               / (cos(LATITUDE * astro::DEG2RAD) * cos(r2.dek));
    if (cH0 < -1.0 || cH0 > 1.0) {
        Serial.println("  Mondauf/-untergang: Mond geht heute nicht auf oder nicht unter.");
        return;
    }
    double H0 = acos(cH0) * astro::RAD2DEG;

    // Startwerte m ∈ [0,1] = Bruchteil des Tages
    double m0 = fmod((a2 - LONGITUDE - theta0) / 360.0 + 1.0, 1.0);
    double m1 = fmod(m0 - H0 / 360.0 + 1.0, 1.0);
    double m2 = fmod(m0 + H0 / 360.0 + 1.0, 1.0);

    // // Iterative Korrektur nach Meeus Kap. 15
    // for (int iter = 0; iter < 3; iter++) {
    //     // Transit
    //     {
    //         double a = astro::interpolate(a1, a2, a3, m0);
    //         double H = fmod(theta0 + 360.985647 * m0 - LONGITUDE - a + 180.0, 360.0) - 180.0;
    //         m0 -= H / 360.0;
    //     }
    //     // Aufgang
    //     {
    //         double a = astro::interpolate(a1, a2, a3, m1);
    //         double d = astro::interpolate(d1, d2, d3, m1);
    //         double H = fmod(theta0 + 360.985647 * m1 - LONGITUDE - a, 360.0);
    //         double h = asin(sin(LATITUDE * astro::DEG2RAD) * sin(d * astro::DEG2RAD)
    //                  + cos(LATITUDE * astro::DEG2RAD) * cos(d * astro::DEG2RAD)
    //                  * cos(H * astro::DEG2RAD)) * astro::RAD2DEG;
    //         m1 += (h - h0) / (360.0 * cos(d * astro::DEG2RAD)
    //              * cos(LATITUDE * astro::DEG2RAD) * sin(H * astro::DEG2RAD));
    //     }
    //     // Untergang
    //     {
    //         double a = astro::interpolate(a1, a2, a3, m2);
    //         double d = astro::interpolate(d1, d2, d3, m2);
    //         double H = fmod(theta0 + 360.985647 * m2 - LONGITUDE - a, 360.0);
    //         double h = asin(sin(LATITUDE * astro::DEG2RAD) * sin(d * astro::DEG2RAD)
    //                  + cos(LATITUDE * astro::DEG2RAD) * cos(d * astro::DEG2RAD)
    //                  * cos(H * astro::DEG2RAD)) * astro::RAD2DEG;
    //         m2 += (h - h0) / (360.0 * cos(d * astro::DEG2RAD)
    //              * cos(LATITUDE * astro::DEG2RAD) * sin(H * astro::DEG2RAD));
    //     }
    // }

    double jd_set = jd0+fmod(m2 + 1, 1.0);
    double gmstStunden  = astro::calculateSiderealTime(jd_set);
    double siderealTime = gmstStunden * M_PI / 12.0 + LONGITUDE * astro::DEG2RAD;

    astro::MoonPosition moon1 = astro::calculateMoon(jd_set);
    astro::RaDek r_set = astro::calculateRaDek(moon1.longitude, moon1.latitude);
    astro::AzimutHeight moonAzimutHeight1 = astro::calculateHAzFromRaDek(r_set, siderealTime, LATITUDE  * astro::DEG2RAD);

    printHHMM("Mondaufgang:    ", fmod(m1 + 1.0, 1.0) * 24.0);
    printHHMM("Mondkulmination:", fmod(m0 + 1.0, 1.0) * 24.0);
    printHHMM("Monduntergang:  ", fmod(m2 + 1.0, 1.0) * 24.0);
        
    Serial.printf("  Monduntergang Azimut/Höhe: %f° / %f°\n", moonAzimutHeight1.azimut * astro::RAD2DEG, moonAzimutHeight1.height * astro::RAD2DEG);
}

// ── Setup & Loop ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    // WLAN verbinden (alle konfigurierten Netzwerke)
    if (strlen(WIFI_SSID_1) > 0) wifiMulti.addAP(WIFI_SSID_1, WIFI_PASSWORD_1);
    if (strlen(WIFI_SSID_2) > 0) wifiMulti.addAP(WIFI_SSID_2, WIFI_PASSWORD_2);
    if (strlen(WIFI_SSID_3) > 0) wifiMulti.addAP(WIFI_SSID_3, WIFI_PASSWORD_3);
    Serial.println("Suche WLAN …");
    while (wifiMulti.run() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\nVerbunden mit %s. IP: %s\n",
        WiFi.SSID().c_str(), WiFi.localIP().toString().c_str());

    // NTP starten (UTC)
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, NTP_SERVER);

    // Warten bis Zeit synchronisiert ist
    struct tm zeitInfo;
    Serial.print("Warte auf NTP-Sync …");
    while (!getLocalTime(&zeitInfo)) {
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\nZeit synchronisiert: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
        zeitInfo.tm_year + 1900,
        zeitInfo.tm_mon  + 1,
        zeitInfo.tm_mday,
        zeitInfo.tm_hour,
        zeitInfo.tm_min,
        zeitInfo.tm_sec);
    tft.begin();
    tft.fillScreen(GC9A01A_BLACK);

    // Serial Commands
    setupSerialCommands();
}

long lastMoonCalc = 0;

void loop() {
    // Alle 60 Sekunden neu berechnen
    
    serial_commands_.ReadSerial();
    if (currentMode == MODE_MOON && getLocalTime(nullptr) && (millis() - lastMoonCalc > 60000)) {
            // Aktuelle UTC-Zeit holen
        struct tm zeitInfo;
        getLocalTime(&zeitInfo);
        calculateMoon(zeitInfo, false);
        lastMoonCalc = millis();
    } else if (currentMode == MODE_DISPLAY) {
        // Hier könnte z.B. ein Wechsel zwischen verschiedenen Anzeigemodi implementiert werden
    } else if (currentMode == MODE_CLOCK) {
        // Hier könnte z.B. eine Uhrzeit-Anzeige implementiert werden
    }
    berechneSonnenaufgang();
    //berechneMondaufgang();
}


