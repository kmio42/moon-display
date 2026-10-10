#ifndef MESSAGES_H
#define MESSAGES_H

// Texte für Telegram, die serielle Konsole und Pushover (Messages.ino). Alle Zeiten werden in UT
// berechnet und erst bei der Ausgabe in Lokalzeit (configLocation.timezone) umgerechnet.

#include <Arduino.h>
#include <time.h>
#include "i18n.h"

constexpr time_t MIN_VALID_TIME = 1700000000; // Zeit gilt erst nach NTP-Sync als gültig

bool   currentUtc(struct tm& utc);            // false, solange NTP nicht synchronisiert hat
String textf(Txt format, ...);                // tr(format) mit printf-Argumenten
String formatLocalTime(time_t t);             // "HH:MM"
String formatLocalDate(time_t t);             // DatePattern
String formatNow();                           // DateTimePattern, Lokalzeit
String buildDailyReport();
String buildMoonMessage();                    // HTML mit Festbreiten-Block (<pre>)
String buildSunMessage();

#endif // MESSAGES_H
