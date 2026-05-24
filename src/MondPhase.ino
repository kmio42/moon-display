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
#include <strings.h>
#include <esp_sntp.h>

#include <Adafruit_GFX.h>     // Core graphics library
#include <Adafruit_GC9A01A.h>

#include <SerialCommands.h>

// ── Konfiguration ────────────────────────────────────────────────────────────

#include "credentials.h"
#include "options_config.h"
#include "i18n.h"

WiFiMulti wifiMulti;

// NTP
constexpr long   GMT_OFFSET_SEC  = OPTIONS_CONFIG_DEFAULT_GMT_OFFSET_SEC;
constexpr int    DAYLIGHT_OFFSET = OPTIONS_CONFIG_DEFAULT_DAYLIGHT_OFFSET;
constexpr char   NTP_SERVER[]    = OPTIONS_CONFIG_DEFAULT_NTP_SERVER;

// Mond-Rendering
extern void calculateMoon(const struct tm& timeinfo, bool printInfo, Adafruit_GC9A01A* tft);

// Persistente Konfiguration (definiert in Config.ino)
extern double configLatitude;
extern double configLongitude;
extern int    configDisplayOptions;
extern int    configLanguage;
extern void   loadConfig();
extern void   saveConfig();

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
  sender->GetSerial()->printf(msgs[FMT_UNRECOGNIZED_CMD], cmd);
  sender->GetSerial()->println(msgs[MSG_TYPE_HELP_FOR_COMMANDS]);
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
        // Versuche zuerst vollständiges Datum+Zeit: DD.MM.YYYY HH:MM:SS
        const char* arg1 = sender->Next();
        if (arg1 != nullptr) {
            snprintf(datetime, sizeof(datetime), "%s %s", arg0, arg1);
        } else {
            snprintf(datetime, sizeof(datetime), "%s", arg0);
        }
        if (!parseDateTime(datetime, tm)) {
            sender->GetSerial()->println(msgs[ERR_INVALID_FORMAT_LABEL]);
            sender->GetSerial()->println(datetime);
            sender->GetSerial()->println(msgs[MSG_EXPECTED_TIME_FORMAT]);
            return;
        }
    } else {
        if (!getLocalTime(&tm)) {
            sender->GetSerial()->println(msgs[ERR_NO_NTP_TIME]);
            return;
        }
    }

    sender->GetSerial()->println(msgs[MSG_CALCULATING_MOON]);
    currentMode = MODE_STATIC;
    calculateMoon(tm, true, &tft);
    sender->GetSerial()->println(msgs[MSG_DONE]);
}
SerialCommand cmd_moon_("moon", cmd_moon);

void cmd_moon_run_(SerialCommands* sender)
{
    sender->GetSerial()->println(msgs[MSG_STARTING_DYNAMIC_MOON]);
    currentMode = MODE_MOON;
}
SerialCommand cmd_moon_run("moon_run", cmd_moon_run_);

void cmd_set_time_(SerialCommands* sender)
{
    const char* arg0 = sender->Next();
    const char* arg1 = sender->Next();
    if (arg0 == nullptr || (arg1 != nullptr && sender->Next() != nullptr)) {
        sender->GetSerial()->println(msgs[ERR_INVALID_DATETIME_FORMAT]);
        return;
    }
    struct tm tm;
    char datetime[50];
    if (arg1 != nullptr) {
        snprintf(datetime, sizeof(datetime), "%s %s", arg0, arg1);
    }
    if (!parseDateTime(datetime, tm)) {
        sender->GetSerial()->println(msgs[ERR_INVALID_DATETIME_FORMAT]);
        return;
    }
    time_t t = mktime(&tm);
    struct timeval now = { .tv_sec = t };
    settimeofday(&now, nullptr);
    sender->GetSerial()->println(msgs[MSG_TIME_UPDATED]);
}
SerialCommand cmd_set_time("set_time", cmd_set_time_);

void cmd_wifi_(SerialCommands* sender)
{
    sender->GetSerial()->println(msgs[MSG_WIFI_CONNECTIONS]);
    sender->GetSerial()->print(WiFi.SSID());
    sender->GetSerial()->print(" (");
    sender->GetSerial()->print(WiFi.RSSI());
    sender->GetSerial()->println(" dBm)");

    const char *arg0 = sender->Next();
    if (arg0 != nullptr) {
        if(strcmp(arg0, "on") == 0) {
            sender->GetSerial()->println(msgs[MSG_WIFI_ENABLING]);
            wifiOn = true;
        } else if (strcmp(arg0, "off") == 0) {
            sender->GetSerial()->println(msgs[MSG_WIFI_DISABLING]);
            WiFi.disconnect(true);
            wifiOn = false;
        } else {
            sender->GetSerial()->println(msgs[ERR_INVALID_WIFI_ARG]);
        }
    }
}
SerialCommand cmd_wifi("wifi", cmd_wifi_);

void cmd_set_location_(SerialCommands* sender)
{
    const char* arg0 = sender->Next();
    const char* arg1 = sender->Next();
    if (arg0 == nullptr || arg1 == nullptr) {
        sender->GetSerial()->println(msgs[ERR_INVALID_LOCATION_FORMAT]);
        return;
    }
    char* endLat = nullptr;
    char* endLon = nullptr;
    double lat = strtod(arg0, &endLat);
    double lon = strtod(arg1, &endLon);
    if (endLat == arg0 || endLon == arg1 || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
        sender->GetSerial()->println(msgs[ERR_INVALID_COORDINATES]);
        return;
    }
    configLatitude  = lat;
    configLongitude = lon;
    saveConfig();
    sender->GetSerial()->printf(msgs[FMT_LOCATION_SAVED], configLatitude, configLongitude);
}
SerialCommand cmd_set_location("set_location", cmd_set_location_);

void cmd_set_options_(SerialCommands* sender)
{
    const char* arg0 = sender->Next();
    if (arg0 == nullptr) {
        sender->GetSerial()->println(msgs[ERR_INVALID_OPTIONS_FORMAT]);
        return;
    }
    char* end = nullptr;
    long value = strtol(arg0, &end, 0); // 0 = autodetect dec/hex/oct
    if (end == arg0 || value < 0 || value > 0x7FFFFFFF) {
        sender->GetSerial()->println(msgs[ERR_INVALID_OPTIONS_VALUE]);
        return;
    }
    configDisplayOptions = (int) value;
    saveConfig();
    sender->GetSerial()->printf(msgs[FMT_OPTIONS_SAVED], configDisplayOptions);
}
SerialCommand cmd_set_options("set_options", cmd_set_options_);

void cmd_set_lang_(SerialCommands* sender)
{
    const char* arg0 = sender->Next();
    if (arg0 == nullptr) {
        sender->GetSerial()->println(msgs[ERR_INVALID_LANG]);
        return;
    }
    Language newLang;
    if (strcasecmp(arg0, "de") == 0) {
        newLang = LANG_DE;
    } else if (strcasecmp(arg0, "en-us") == 0 || strcasecmp(arg0, "en") == 0) {
        newLang = LANG_EN_US;
    } else {
        sender->GetSerial()->println(msgs[ERR_INVALID_LANG]);
        return;
    }
    configLanguage = (int) newLang;
    setLanguage(newLang);
    saveConfig();
    // Confirmation prints in the newly-selected language.
    sender->GetSerial()->println(msgs[MSG_LANGUAGE_SET]);
}
SerialCommand cmd_set_lang("set_lang", cmd_set_lang_);

void cmd_config_(SerialCommands* sender)
{
    sender->GetSerial()->println(msgs[MSG_CONFIG_HEADER]);
    sender->GetSerial()->printf(msgs[FMT_CONFIG_LATITUDE],  configLatitude);
    sender->GetSerial()->printf(msgs[FMT_CONFIG_LONGITUDE], configLongitude);

    struct tm now;
    if (getLocalTime(&now, 0)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S UTC", &now);
        sender->GetSerial()->printf(msgs[FMT_CONFIG_TIME], buf);
    } else {
        sender->GetSerial()->println(msgs[MSG_CONFIG_TIME_UNAVAILABLE]);
    }

    const char* ntpStatus = msgs[MSG_NTP_UNKNOWN];
    switch (sntp_get_sync_status()) {
        case SNTP_SYNC_STATUS_RESET:       ntpStatus = msgs[MSG_NTP_NOT_SYNCED]; break;
        case SNTP_SYNC_STATUS_COMPLETED:   ntpStatus = msgs[MSG_NTP_SYNCED];     break;
        case SNTP_SYNC_STATUS_IN_PROGRESS: ntpStatus = msgs[MSG_NTP_SYNCING];    break;
    }
    sender->GetSerial()->printf(msgs[FMT_CONFIG_NTP], ntpStatus, NTP_SERVER);

    if (!wifiOn) {
        sender->GetSerial()->println(msgs[MSG_CONFIG_WIFI_DISABLED]);
    } else if (WiFi.status() == WL_CONNECTED) {
        sender->GetSerial()->printf(msgs[FMT_CONFIG_WIFI_CONNECTED],
            WiFi.SSID().c_str(), (int) WiFi.RSSI(), WiFi.localIP().toString().c_str());
    } else {
        sender->GetSerial()->println(msgs[MSG_CONFIG_WIFI_DISCONNECTED]);
    }

    sender->GetSerial()->printf(msgs[FMT_CONFIG_OPTIONS],       configDisplayOptions, configDisplayOptions);
    sender->GetSerial()->printf(msgs[FMT_CONFIG_OPT_DARKEN],    (configDisplayOptions & 1) ? 'x' : ' ');
    sender->GetSerial()->printf(msgs[FMT_CONFIG_OPT_BLUISH],    (configDisplayOptions & 2) ? 'x' : ' ');
    sender->GetSerial()->printf(msgs[FMT_CONFIG_OPT_NASA],      (configDisplayOptions & 4) ? 'x' : ' ');
    sender->GetSerial()->printf(msgs[FMT_CONFIG_OPT_LIBRATION], (configDisplayOptions & 8) ? 'x' : ' ');
    sender->GetSerial()->print(msgs[MSG_LANGUAGE_LINE]);  // trailing \n is part of the token
}
SerialCommand cmd_config("config", cmd_config_);

void cmd_help(SerialCommands* sender)
{
  sender->GetSerial()->println(msgs[HELP_HEADER]);
  sender->GetSerial()->println(msgs[HELP_HELP]);
  sender->GetSerial()->println(msgs[HELP_MOON]);
  sender->GetSerial()->println(msgs[HELP_MOON_RUN]);
  sender->GetSerial()->println(msgs[HELP_WIFI]);
  sender->GetSerial()->println(msgs[HELP_SET_TIME]);
  sender->GetSerial()->println(msgs[HELP_SET_LOCATION]);
  sender->GetSerial()->println(msgs[HELP_SET_OPTIONS]);
  sender->GetSerial()->println(msgs[HELP_OPTIONS_BITS]);
  sender->GetSerial()->println(msgs[HELP_CONFIG]);
  sender->GetSerial()->println(msgs[HELP_SET_LANG]);
}
SerialCommand cmd_help_("help", cmd_help);

void setupSerialCommands() {
    serial_commands_.AddCommand(&cmd_help_);
    serial_commands_.AddCommand(&cmd_moon_);
    serial_commands_.AddCommand(&cmd_moon_run);
    serial_commands_.AddCommand(&cmd_set_time);
    serial_commands_.AddCommand(&cmd_wifi);
    serial_commands_.AddCommand(&cmd_set_location);
    serial_commands_.AddCommand(&cmd_set_options);
    serial_commands_.AddCommand(&cmd_config);
    serial_commands_.AddCommand(&cmd_set_lang);
    serial_commands_.SetDefaultHandler(cmd_unrecognized);
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
        Serial.println(msgs[MSG_UPDATING_MOON]);
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
