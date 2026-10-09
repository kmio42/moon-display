#include <Arduino.h>
#include <Preferences.h>
#include <time.h>
#include <string.h>

#include "display_mode.h"

// Persistente Konfiguration (RAM-Kopie der NVS-Werte).
// Defaults werden verwendet, wenn noch nichts im Flash gespeichert wurde.
double configLatitude       = 51.0;
double configLongitude      =  9.0;
// Default: OPTION_DARKEN_UNLIT (1) | OPTION_USE_LIBRATION (8) = 9
int    configDisplayOptions = 9;
// Telegram Auto-Modus: täglicher Bericht zur Uhrzeit configAutoTime (Minuten nach Mitternacht, Lokalzeit)
bool   configAutoMode       = false;
int    configAutoTime       = 7 * 60;
// Anzeigemodus (display_mode.h)
int    configDisplayMode    = DISPLAY_MODE_MOON;
// Zeitzone als POSIX-TZ-String, wird nur für die Ausgabe verwendet (System läuft in UTC)
String configTimezone       = "CET-1CEST,M3.5.0,M10.5.0/3";

static Preferences prefs;
static constexpr const char* PREFS_NAMESPACE = "moon-cfg";

void loadConfig() {
    prefs.begin(PREFS_NAMESPACE, true);
    configLatitude       = prefs.getDouble("lat",   configLatitude);
    configLongitude      = prefs.getDouble("lon",   configLongitude);
    configDisplayOptions = prefs.getInt   ("opts",  configDisplayOptions);
    configAutoMode       = prefs.getBool  ("auto",  configAutoMode);
    configAutoTime       = prefs.getInt   ("atime", configAutoTime);
    configTimezone       = prefs.getString("tz",    configTimezone);
    configDisplayMode    = prefs.getInt   ("mode",  configDisplayMode);
    prefs.end();
}

void saveConfig() {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putDouble("lat",   configLatitude);
    prefs.putDouble("lon",   configLongitude);
    prefs.putInt   ("opts",  configDisplayOptions);
    prefs.putBool  ("auto",  configAutoMode);
    prefs.putInt   ("atime", configAutoTime);
    prefs.putString("tz",    configTimezone);
    prefs.putInt   ("mode",  configDisplayMode);
    prefs.end();
}

// Länge eines TZ-Strings einschließlich Nullbyte
constexpr size_t TZ_CAPACITY = 64;
static char* tzBuffer = nullptr;

// Setzt die System-Zeitzone. setenv() in newlib gibt den alten Wert nicht frei, wenn der neue
// länger ist; beim ständigen Wechsel zwischen configTimezone und "UTC0" ging so bei jedem Aufruf ein
// Puffer verloren, bis der Heap leer war. Deshalb bekommt TZ einmal einen Puffer mit TZ_CAPACITY
// Zeichen, der danach nur noch überschrieben wird. Hat jemand anderes TZ neu gesetzt (configTime),
// wird der Puffer neu angelegt.
static void setTimezone(const char* tz) {
    if (tzBuffer == nullptr || getenv("TZ") != tzBuffer) {
        char placeholder[TZ_CAPACITY];
        memset(placeholder, 'x', TZ_CAPACITY - 1);
        placeholder[TZ_CAPACITY - 1] = '\0';
        setenv("TZ", placeholder, 1);
        tzBuffer = getenv("TZ");
    }
    strlcpy(tzBuffer, tz, TZ_CAPACITY);
    tzset();
}

// Wandelt eine UTC-Zeit in Lokalzeit (configTimezone) um.
// Die System-Zeitzone wird nur kurz umgestellt und danach wieder auf UTC gesetzt.
void utcToLocal(time_t t, struct tm& out) {
    setTimezone(configTimezone.c_str());
    localtime_r(&t, &out);
    setTimezone("UTC0");
}

// Prüft, ob der POSIX-TZ-String plausibel ist (Name mit mindestens 3 Buchstaben + Offset, kürzer als TZ_CAPACITY).
bool isValidTimezone(const char* tz) {
    if (strlen(tz) >= TZ_CAPACITY) return false;
    int letters = 0;
    const char* p = tz;
    if (*p == '<') {
        while (*p && *p != '>') p++;
        if (*p != '>') return false;
        letters = 3;
        p++;
    } else {
        while (isalpha((unsigned char) *p)) { letters++; p++; }
    }
    return letters >= 3 && (*p == '+' || *p == '-' || isdigit((unsigned char) *p));
}

// Parst "HH:MM" in Minuten nach Mitternacht, -1 bei Fehler.
int parseHHMM(const char* s) {
    int h, m;
    char extra;
    if (sscanf(s, "%d:%d%c", &h, &m, &extra) != 2 || h < 0 || h > 23 || m < 0 || m > 59) {
        return -1;
    }
    return h * 60 + m;
}
