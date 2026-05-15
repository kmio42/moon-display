#include <Arduino.h>
#include <Preferences.h>

// Persistente Konfiguration (RAM-Kopie der NVS-Werte).
// Defaults werden verwendet, wenn noch nichts im Flash gespeichert wurde.
double configLatitude       = 51.0;
double configLongitude      =  9.0;
// Default: OPTION_DARKEN_UNLIT (1) | OPTION_USE_LIBRATION (8) = 9
int    configDisplayOptions = 9;

static Preferences prefs;
static constexpr const char* PREFS_NAMESPACE = "moon-cfg";

void loadConfig() {
    prefs.begin(PREFS_NAMESPACE, true);
    configLatitude       = prefs.getDouble("lat",  configLatitude);
    configLongitude      = prefs.getDouble("lon",  configLongitude);
    configDisplayOptions = prefs.getInt   ("opts", configDisplayOptions);
    prefs.end();
}

void saveConfig() {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putDouble("lat",  configLatitude);
    prefs.putDouble("lon",  configLongitude);
    prefs.putInt   ("opts", configDisplayOptions);
    prefs.end();
}
