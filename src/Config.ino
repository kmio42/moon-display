#include <Arduino.h>
#include <Preferences.h>
#include "options_config.h"
#include "i18n.h"

// Persistente Konfiguration (RAM-Kopie der NVS-Werte).
// Defaults stammen aus options_config.h und werden verwendet,
// wenn noch nichts im Flash gespeichert wurde.
double configLatitude       = OPTIONS_CONFIG_DEFAULT_LATITUDE;
double configLongitude      = OPTIONS_CONFIG_DEFAULT_LONGITUDE;
int    configDisplayOptions = OPTIONS_CONFIG_DEFAULT_OPTIONS;
int    configLanguage       = OPTIONS_CONFIG_DEFAULT_LANGUAGE;

static Preferences prefs;
static constexpr const char* PREFS_NAMESPACE = "moon-cfg";

void loadConfig() {
    prefs.begin(PREFS_NAMESPACE, true);
    configLatitude       = prefs.getDouble("lat",  configLatitude);
    configLongitude      = prefs.getDouble("lon",  configLongitude);
    configDisplayOptions = prefs.getInt   ("opts", configDisplayOptions);
    configLanguage       = prefs.getInt   ("lang", configLanguage);
    prefs.end();
    if (configLanguage != LANG_DE && configLanguage != LANG_EN_US) {
        configLanguage = LANG_DE;
    }
    setLanguage((Language) configLanguage);
}

void saveConfig() {
    prefs.begin(PREFS_NAMESPACE, false);
    prefs.putDouble("lat",  configLatitude);
    prefs.putDouble("lon",  configLongitude);
    prefs.putInt   ("opts", configDisplayOptions);
    prefs.putInt   ("lang", configLanguage);
    prefs.end();
}
