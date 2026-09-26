#include <Arduino.h>
#include <Preferences.h>
#include <time.h>

// Persistente Konfiguration (RAM-Kopie der NVS-Werte).
// Defaults werden verwendet, wenn noch nichts im Flash gespeichert wurde.
double configLatitude       = 51.0;
double configLongitude      =  9.0;
// Default: OPTION_DARKEN_UNLIT (1) | OPTION_USE_LIBRATION (8) = 9
int    configDisplayOptions = 9;
// Telegram Auto-Modus: täglicher Bericht zur Uhrzeit configAutoTime (Minuten nach Mitternacht, Lokalzeit)
bool   configAutoMode       = false;
int    configAutoTime       = 7 * 60;
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
    prefs.end();
}

// Wandelt eine UTC-Zeit in Lokalzeit (configTimezone) um.
// Die System-Zeitzone wird nur kurz umgestellt und danach wieder auf UTC gesetzt.
void utcToLocal(time_t t, struct tm& out) {
    setenv("TZ", configTimezone.c_str(), 1);
    tzset();
    localtime_r(&t, &out);
    setenv("TZ", "UTC0", 1);
    tzset();
}

// Prüft, ob der POSIX-TZ-String plausibel ist (Name mit mindestens 3 Buchstaben + Offset).
bool isValidTimezone(const char* tz) {
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
