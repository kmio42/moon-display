/**
 * Telegram.ino – Telegram-Bot für Einstellungen, Koordinaten und Tagesbericht
 *
 * Nur Nachrichten von TELEGRAM_CHAT_ID werden beantwortet, im privaten Chat oder in der Gruppe
 * TELEGRAM_GROUP_ID. In die Gruppe postet der Bot außerdem den Tagesbericht und setzt das Mondbild
 * als Gruppenbild.
 * Der Tagesbericht geht an Telegram und/oder Pushover (Pushover.ino), je nachdem was konfiguriert ist.
 * Alle Berechnungen laufen in UT, erst die Ausgabe wird in Lokalzeit (configLocation.timezone) umgerechnet.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <JPEGENC.h>
#include <new>
#include <algorithm>
#include <iterator>
#include <time.h>

#include "astro.h"
#include "ephemeris.h"
#include "moon_render.h"
#include "png_stream.h"
#include "display_mode.h"
#include "credentials.h"

#ifndef TELEGRAM_BOT_TOKEN
#define TELEGRAM_BOT_TOKEN ""
#endif
#ifndef TELEGRAM_CHAT_ID
#define TELEGRAM_CHAT_ID ""
#endif
#ifndef TELEGRAM_GROUP_ID
#define TELEGRAM_GROUP_ID ""  // leer: keine Gruppe
#endif

// Persistente Konfiguration (definiert in Config.ino)
extern astro::GeoLocation configLocation;
extern int    configDisplayOptions;
extern bool   configAutoMode;
extern int    configAutoTime;
extern void   saveConfig();
extern void   utcToLocal(time_t t, struct tm& out);
extern bool   isValidTimezone(const char* tz);
extern int    parseHHMM(const char* s);

// Display neu zeichnen (definiert in MondPhase.ino)
extern void requestRedraw();
extern void setDisplayMode(int displayMode);
extern const char* displayModeName();
extern int configDisplayMode;

// Pushover (definiert in Pushover.ino)
extern bool pushoverEnabled();
extern bool sendPushover(const String& message, const String& title);

constexpr unsigned long TELEGRAM_POLL_INTERVAL = 3000;  // ms
constexpr unsigned int  TELEGRAM_WAIT_FOR_RESPONSE = 5000; // ms, Library-Standard 1500 ist bei langsamem Netz zu knapp
constexpr int           TELEGRAM_MAX_MESSAGE_LENGTH = 4000; // Byte, Library-Standard 1500 reicht nicht für jedes Update
constexpr time_t        MIN_VALID_TIME         = 1700000000; // Zeit gilt erst nach NTP-Sync als gültig
constexpr unsigned long PUSHOVER_RETRY_INTERVAL = 5UL * 60 * 1000; // ms, schont das Nachrichtenkontingent

static WiFiClientSecure telegramClient;
static UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, telegramClient);
static bool telegramEnabled = false;
static bool telegramCommandsSet = false;
static bool telegramOnline = false;          // Ergebnis der letzten Verbindungsprüfung
static unsigned long lastTelegramProbe = 0;
constexpr unsigned long TELEGRAM_PROBE_ONLINE  = 60000;  // ms, Prüfabstand bei bestehender Verbindung
constexpr unsigned long TELEGRAM_PROBE_OFFLINE = 10000;  // ms, bis zur Wiederherstellung
static unsigned long lastTelegramPoll = 0;
static int lastReportDay = -1;     // Lokaler Tag (Jahr*1000 + Tag im Jahr) des letzten Tagesberichts über Telegram
static int lastPushoverDay = -1;   // dito über Pushover
static unsigned long lastPushoverAttempt = 0;
static int lastGroupReportDay = -1; // dito in der Gruppe
static int lastProfileDay = -1; // Lokaler Tag des letzten Profilbilds, -1: seit dem Start keins
static int lastGroupPhotoDay = -1; // dito Gruppenbild
static unsigned long lastProfileAttempt = 0;

static bool groupEnabled() {
    return strlen(TELEGRAM_GROUP_ID) > 0;
}

// ── Formatierung ─────────────────────────────────────────────────────────────

// Formatiert eine UTC-Zeit als lokale Uhrzeit "HH:MM".
static String formatLocalTime(time_t t) {
    struct tm lt;
    utcToLocal(t, lt);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    return buf;
}

// Formatiert eine UTC-Zeit als lokales Datum "TT.MM.".
static String formatLocalDate(time_t t) {
    struct tm lt;
    utcToLocal(t, lt);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d.%02d.", lt.tm_mday, lt.tm_mon + 1);
    return buf;
}

static String formatDeg(astro::Angle angle) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%+.2f°", angle.deg());
    return buf;
}

static String formatRa(astro::Angle angle) {
    double h = angle.normalized().hours();
    int hh = (int) h;
    int mm = (int) ((h - hh) * 60.0);
    int ss = (int) (((h - hh) * 60.0 - mm) * 60.0);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02dh %02dm %02ds", hh, mm, ss);
    return buf;
}

static String formatAzimut(astro::Angle angle) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°", angle.normalized().deg());
    return buf;
}

// Formatiert einen Winkel klassisch als ±DDD°MM', gerundet auf ganze Bogenminuten.
// full360: Winkel wird auf 0–360° normiert (Länge, Azimut).
static String formatDm(astro::Angle angle, bool sign, bool full360 = false) {
    const double deg = full360 ? angle.normalized().deg() : angle.deg();
    long minutes = lround(fabs(deg) * 60.0);
    if (full360) minutes %= 360 * 60;  // 359°59.6' wird zu 0°00'
    char buf[20];
    if (sign) {
        snprintf(buf, sizeof(buf), "%c%ld°%02ld'", deg < 0 && minutes > 0 ? '-' : '+', minutes / 60, minutes % 60);
    } else {
        snprintf(buf, sizeof(buf), "%ld°%02ld'", minutes / 60, minutes % 60);
    }
    return buf;
}

// Formatiert eine Rektaszension als HHhMMm, gerundet auf ganze Zeitminuten.
static String formatHm(astro::Angle angle) {
    const double h = angle.normalized().hours();
    const long minutes = lround(h * 60.0) % (24 * 60);
    char buf[12];
    snprintf(buf, sizeof(buf), "%02ldh%02ldm", minutes / 60, minutes % 60);
    return buf;
}

// Formatiert einen kleinen Winkel (Monddurchmesser) als MM'SS".
static String formatArcmin(astro::Angle angle) {
    const long seconds = lround(angle.arcsec());
    char buf[16];
    snprintf(buf, sizeof(buf), "%ld'%02ld\"", seconds / 60, seconds % 60);
    return buf;
}

static String formatNow() {
    struct tm lt;
    utcToLocal(time(nullptr), lt);
    char buf[32];
    strftime(buf, sizeof(buf), "%d.%m.%Y %H:%M", &lt);
    return buf;
}

static bool currentUtc(struct tm& utc) {
    time_t now = time(nullptr);
    if (now < MIN_VALID_TIME) return false;
    gmtime_r(&now, &utc);
    return true;
}

// ── Nachrichten ──────────────────────────────────────────────────────────────

String buildDailyReport() {
    struct tm utc;
    if (!currentUtc(utc)) return "Keine gültige Uhrzeit (NTP) verfügbar.";

    time_t now = time(nullptr);
    struct tm lt;
    utcToLocal(now, lt);
    const time_t dayStart = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);

    DayEvents today     = eventsForLocalDay(now, true);
    DayEvents yesterday = eventsForLocalDay(dayStart - 1, false);
    SkyState sky = computeSky(utc);

    char buf[64];
    strftime(buf, sizeof(buf), "📅 %d.%m.%Y", &lt);
    String msg = "";buf;
    //msg += "\n\n";

    if (today.sunValid) {
        msg += "☼\uFE0E↑ ";
        msg += today.hasSunrise ? formatLocalTime(today.sunrise) : String("-");
        msg += "\u2003☼\uFE0E↓ ";
        msg += today.hasSunset ? formatLocalTime(today.sunset) : String("-");
        if (today.hasSunset && yesterday.hasSunset) {
            // Astronomische Differenz, unabhängig von einer Sommerzeitumstellung
            long sec = (long) (today.sunset - yesterday.sunset) - 86400L;
            long absSec = labs(sec);
            snprintf(buf, sizeof(buf), " (%s%ld'%ld'')", sec < 0 ? "−" : "+", absSec / 60, absSec % 60);
            msg += buf;
        }
        msg += "\n";
    } else {
        msg += "☀\n";
    }

    // Mondauf-/untergang in zeitlicher Reihenfolge, fehlende Ereignisse weglassen
    String rise = today.hasMoonrise ? "☽↑ " + formatLocalTime(today.moonrise) : String();
    String set  = today.hasMoonset  ? "☽↓ " + formatLocalTime(today.moonset)  : String();
    bool setFirst = today.hasMoonrise && today.hasMoonset && today.moonset < today.moonrise;
    String first  = setFirst ? set : rise;
    String second = setFirst ? rise : set;
    msg += first;
    if (first.length() && second.length()) msg += "\u2003";
    msg += second;
    if (!first.length() && !second.length()) msg += "☽-";
    msg += "\n";
    // Phase, Kulminationshöhe als Balken und deren Tendenz
    static const char* const HEIGHT_BARS[] = {"▁", "▂", "▃", "▅", "▇"};
    MoonOutlook outlook = moonOutlookFor(now);
    snprintf(buf, sizeof(buf), "%s %.0f%% (%s)\u2003%s%s",
             moonPhaseEmoji(sky.phase, sky.waxing), sky.phase * 100.0, sky.waxing ? "⬈" : "⬊",
             HEIGHT_BARS[outlook.heightLevel], outlook.heightRising ? "⇡" : "⇣");
    msg += buf;

    // Finsternisse und Super-/Minimond der nächsten 14 Tage
    String events;
    if (outlook.lunarEclipse) {
        events += String(" 🌕🔴") + (outlook.lunarEclipseVisible ? "" : "✗") + " " + formatLocalDate(outlook.fullMoon);
    }
    if (outlook.solarEclipse) {
        events += " ☀🌑 " + formatLocalDate(outlook.newMoon);
    }
    if (outlook.superMoon) {
        events += " 🌕＋ " + formatLocalDate(outlook.fullMoon);
    }
    if (outlook.miniMoon) {
        events += " 🌕－ " + formatLocalDate(outlook.fullMoon);
    }
    if (events.length()) {
        msg += "\n" + events.substring(1);
    }
    return msg;
}

// Extremwerte der Mondentfernung im 21. Jahrhundert (gerundet), Grenzen für den Durchmesser
constexpr double MOON_PERIGEE_MIN = 356400.0;  // km
constexpr double MOON_APOGEE_MAX  = 406700.0;  // km
constexpr int    BLOCK_LABEL_WIDTH = 10;       // Zeichen

// Scheinbarer Durchmesser des Mondes bei der Entfernung distance [km]
static astro::Angle moonDiameter(double distance) {
    return 2.0 * astro::Angle::asin(astro::MOON_RADIUS / distance);
}

// Lage des Durchmessers zwischen Apogäum (0 %) und Perigäum (100 %) der Extremwerte
static double diameterPercent(double distance) {
    const astro::Angle dMin = moonDiameter(MOON_APOGEE_MAX);
    const astro::Angle dMax = moonDiameter(MOON_PERIGEE_MIN);
    return constrain((moonDiameter(distance) - dMin) / (dMax - dMin) * 100.0, 0.0, 100.0);
}

// "TT.MM. HH:MM" in Lokalzeit
static String formatLocalDateTime(time_t t) {
    return formatLocalDate(t) + " " + formatLocalTime(t);
}

// Eine Zeile des Festbreiten-Blocks. Die Bezeichnung wird auf BLOCK_LABEL_WIDTH Zeichen aufgefüllt;
// gezählt werden Zeichen, nicht UTF-8-Bytes (wegen "ö").
static String blockLine(const char* label, const String& value) {
    String line = label;
    int chars = 0;
    for (const char* p = label; *p; p++) {
        if ((*p & 0xC0) != 0x80) chars++;
    }
    for (; chars < BLOCK_LABEL_WIDTH; chars++) line += ' ';
    return line + value + "\n";
}

// Mondkoordinaten wie in einem Sternprogramm, als HTML mit Festbreiten-Block (parse_mode HTML).
static String buildMoonMessage() {
    struct tm utc;
    if (!currentUtc(utc)) return "Keine gültige Uhrzeit (NTP) verfügbar.";
    SkyState sky = computeSky(utc);
    const time_t now = time(nullptr);

    char buf[48];
    String msg = String(moonPhaseEmoji(sky.phase, sky.waxing)) + " Mond – " + formatNow() + "\n<pre>";

    msg += blockLine("Ekl. L/B", formatDm(sky.moon.longitude, true, true) + "/" + formatDm(sky.moon.latitude, true));
    msg += blockLine("RA/Dek", formatHm(sky.moonRaDek.ra) + "/" + formatDm(sky.moonRaDek.dec, true));
    // Azimut nach Meeus von Süden gezählt (S=0°, W=90°)
    msg += blockLine("Az/Höhe", formatDm(sky.moonAzH.azimuth, true, true) + "/" + formatDm(sky.moonAzH.altitude, true));

    snprintf(buf, sizeof(buf), " (%.0f %%)", diameterPercent(sky.moon.distance));
    msg += blockLine("Durchm.", formatArcmin(moonDiameter(sky.moon.distance)) + buf);

    // Elongation, Formel 46.2 aus "Astronomische Algorithmen" von Jean Meeus (Breite der Sonne = 0)
    const astro::Angle elongation = astro::Angle::acos(cos(sky.moon.latitude) * cos(sky.moon.longitude - sky.sun.longitude));
    msg += blockLine("Elong.", formatDm(elongation, false) + (sky.waxing ? " Ost" : " West"));

    snprintf(buf, sizeof(buf), "%s, %.1f %%", sky.waxing ? "zunehmend" : "abnehmend", sky.phase * 100.0);
    msg += blockLine("Phase", buf);
    msg += blockLine("Librat.", "L " + formatDm(sky.moonAxle.libration.longitude, true)
                              + " / B " + formatDm(sky.moonAxle.libration.latitude, true));

    // Aufgang, Kulmination und Untergang des lokalen Tages in zeitlicher Reihenfolge,
    // fehlende Ereignisse am Ende mit "-"
    const DayEvents today = eventsForLocalDay(now, true);
    String transitHeight;  // Höhe bei der Kulmination (topozentrisch)
    if (today.hasMoonTransit) {
        struct tm transitUtc;
        gmtime_r(&today.moonTransit, &transitUtc);
        transitHeight = ", " + formatDm(computeSky(transitUtc).moonAzH.altitude, true);
    }
    struct Event {
        const char* label;
        bool valid;
        time_t time;
        String suffix;
    } events[] = {
        {"Aufgang",   today.hasMoonrise,    today.moonrise,    ""},
        {"Kulmin.",   today.hasMoonTransit, today.moonTransit, transitHeight},
        {"Untergang", today.hasMoonset,     today.moonset,     ""},
    };
    std::stable_sort(std::begin(events), std::end(events), [](const Event& a, const Event& b) {
        if (a.valid != b.valid) return a.valid;
        return a.valid && a.time < b.time;
    });
    for (const Event& e : events) {
        msg += blockLine(e.label, e.valid ? formatLocalTime(e.time) + e.suffix : String("-"));
    }

    // Nächstes Perigäum/Apogäum mit Lage zwischen den Extremwerten und nächster Knotendurchgang
    const astro::Apsis perigee = astro::calculateNextMoonApsis(sky.jd, astro::ApsisType::Periapsis);
    const astro::Apsis apogee  = astro::calculateNextMoonApsis(sky.jd, astro::ApsisType::Apoapsis);
    snprintf(buf, sizeof(buf), " (%.0f %%)", diameterPercent(perigee.distance));
    msg += blockLine("Perigäum", formatLocalDateTime(astro::JulianDay(perigee.jde).toUnixTime()) + buf);
    snprintf(buf, sizeof(buf), " (%.0f %%)", diameterPercent(apogee.distance));
    msg += blockLine("Apogäum", formatLocalDateTime(astro::JulianDay(apogee.jde).toUnixTime()) + buf);
    const astro::NodePassage node = astro::calculateNextMoonNode(sky.jd);
    msg += blockLine("Knoten", formatLocalDateTime(astro::JulianDay(node.jde).toUnixTime()) + (node.ascending ? " aufst." : " abst."));
    msg.remove(msg.length() - 1);  // letzter Zeilenumbruch, sonst Leerzeile im Block
    return msg + "</pre>";
}

static String buildSunMessage() {
    struct tm utc;
    if (!currentUtc(utc)) return "Keine gültige Uhrzeit (NTP) verfügbar.";
    SkyState sky = computeSky(utc);

    String msg = "☀️ Sonne – " + formatNow() + "\n\n";
    msg += "RA / Dek: " + formatRa(sky.sunRaDek.ra) + " / " + formatDeg(sky.sunRaDek.dec) + "\n";
    msg += "Azimut / Höhe: " + formatAzimut(sky.sunAzH.azimuth) + " / " + formatDeg(sky.sunAzH.altitude);
    return msg;
}

static String buildConfigMessage() {
    char buf[96];
    String msg = "⚙️ Konfiguration\n\n";
    snprintf(buf, sizeof(buf), "Standort: %.6f / %.6f, %.0f m\n",
             configLocation.latitude.deg(), configLocation.longitude.deg(),
             configLocation.elevation);
    msg += buf;
    snprintf(buf, sizeof(buf), "Optionen: %d\n", configDisplayOptions);
    msg += buf;
    snprintf(buf, sizeof(buf), "  [%c] Unbeleuchtete Seite abdunkeln (1)\n", (configDisplayOptions & 1) ? 'x' : ' ');
    msg += buf;
    snprintf(buf, sizeof(buf), "  [%c] Bläuliche Tönung (2)\n", (configDisplayOptions & 2) ? 'x' : ' ');
    msg += buf;
    snprintf(buf, sizeof(buf), "  [%c] NASA-Textur (4)\n", (configDisplayOptions & 4) ? 'x' : ' ');
    msg += buf;
    snprintf(buf, sizeof(buf), "  [%c] Libration (8)\n", (configDisplayOptions & 8) ? 'x' : ' ');
    msg += buf;
    snprintf(buf, sizeof(buf), "  [%c] Ohne parallaktischen Winkel (16)\n", (configDisplayOptions & 16) ? 'x' : ' ');
    msg += buf;
    snprintf(buf, sizeof(buf), "  [%c] Ohne Positionswinkel (32)\n", (configDisplayOptions & 32) ? 'x' : ' ');
    msg += buf;
    snprintf(buf, sizeof(buf), "Auto-Modus: %s, täglich um %02d:%02d\n",
             configAutoMode ? "an" : "aus", configAutoTime / 60, configAutoTime % 60);
    msg += buf;
    msg += String("Gruppe: ") + (groupEnabled() ? TELEGRAM_GROUP_ID : "nicht konfiguriert") + "\n";
    msg += String("Pushover: ") + (pushoverEnabled() ? "an" : "nicht konfiguriert") + "\n";
    msg += "Zeitzone: " + String(configLocation.timezone) + "\n";
    msg += String("Anzeige: ") + displayModeName() + "\n";
    msg += "Lokalzeit: " + formatNow();
    return msg;
}

static const char HELP_TEXT[] =
    "🌙 Mond-Display\n\n"
    "/mond – aktuelle Mondkoordinaten\n"
    "/bild – aktuelles Mondbild\n"
    "/profilbild – Profil- und Gruppenbild jetzt aktualisieren\n"
    "/sonne – aktuelle Sonnenkoordinaten\n"
    "/bericht – Tagesbericht jetzt senden (auch an Pushover)\n"
    "/config – aktuelle Einstellungen\n"
    "/standort <lat> <lon> [höhe] – Standort in Dezimalgrad, Höhe in m\n"
    "/optionen <wert> – Darstellung (Bits: 1=abdunkeln, 2=bläulich, 4=NASA-Textur, 8=Libration, 16=ohne parallakt. Winkel, 32=ohne Positionswinkel)\n"
    "/auto on|off – täglichen Bericht ein/aus\n"
    "/autozeit HH:MM – Uhrzeit des Berichts (Lokalzeit)\n"
    "/zeitzone <POSIX-TZ> – z. B. CET-1CEST,M3.5.0,M10.5.0/3\n"
    "/modus mond|uhr – Anzeige: Mond oder 24h-Uhr\n\n"
    "Bericht: ▁…▇ Höchststand des Mondes, ⇡⇣ morgen höher/tiefer, "
    "🌕🔴 Mondfinsternis (✗ hier nicht sichtbar), ☀🌑 Sonnenfinsternis, 🌕＋/🌕－ Super-/Minimond";

static const char BOT_COMMANDS[] =
    "["
    "{\"command\":\"mond\",\"description\":\"Aktuelle Mondkoordinaten\"},"
    "{\"command\":\"bild\",\"description\":\"Aktuelles Mondbild\"},"
    "{\"command\":\"profilbild\",\"description\":\"Profil- und Gruppenbild aktualisieren\"},"
    "{\"command\":\"sonne\",\"description\":\"Aktuelle Sonnenkoordinaten\"},"
    "{\"command\":\"bericht\",\"description\":\"Tagesbericht jetzt senden\"},"
    "{\"command\":\"config\",\"description\":\"Aktuelle Einstellungen\"},"
    "{\"command\":\"standort\",\"description\":\"Standort setzen: <lat> <lon> [höhe]\"},"
    "{\"command\":\"optionen\",\"description\":\"Darstellungsoptionen setzen: <wert>\"},"
    "{\"command\":\"auto\",\"description\":\"Täglichen Bericht ein/aus: on|off\"},"
    "{\"command\":\"autozeit\",\"description\":\"Uhrzeit des Berichts: HH:MM\"},"
    "{\"command\":\"zeitzone\",\"description\":\"Zeitzone (POSIX-TZ) setzen\"},"
    "{\"command\":\"modus\",\"description\":\"Anzeige: mond|uhr\"},"
    "{\"command\":\"hilfe\",\"description\":\"Befehlsübersicht\"}"
    "]";

// ── Mondbild ─────────────────────────────────────────────────────────────────

// Aktuelles Datenstück des PNG-Streams. Es wird schon in photoMoreData() geholt, weil die
// Library photoBuffer() und photoBufferLen() als Argumente desselben Aufrufs auswertet
// (Reihenfolge nicht festgelegt).
static const uint8_t* photoPiece = nullptr;
static size_t photoPieceLen = 0;

static bool photoMoreData() {
    if (!pngStreamMore()) return false;
    photoPiece = pngStreamNext(photoPieceLen);
    return true;
}

static byte* photoBuffer() {
    return (byte*) photoPiece;
}

static int photoBufferLen() {
    return (int) photoPieceLen;
}

static uint16_t photoPixel(int x, int y) {
    return moonPixel(x - MOON_RADIUS_PX, y - MOON_RADIUS_PX);
}

// Rendert den Mond für die aktuelle Zeit und sendet ihn als PNG.
// Das Bild wird zeilenweise während des Uploads berechnet (kein Bildpuffer nötig).
static void sendMoonPhoto(const String& chatId) {
    struct tm utc;
    if (!currentUtc(utc)) {
        bot.sendMessage(chatId, "Keine gültige Uhrzeit (NTP) verfügbar.", "");
        return;
    }
    SkyState sky = computeSky(utc);

    const unsigned long start = millis();
    prepareMoonRenderAt(utc);
    pngStreamBegin(2 * MOON_RADIUS_PX, 2 * MOON_RADIUS_PX, photoPixel);

    // Telegram braucht für das Verarbeiten des Bildes länger als die Standard-Wartezeit
    const unsigned int wait = bot.waitForResponse;
    bot.waitForResponse = 15000;
    String response = bot.sendPhotoByBinary(chatId, "image/png", pngStreamSize(),
                                            photoMoreData, nullptr, photoBuffer, photoBufferLen);
    bot.waitForResponse = wait;
    Serial.printf("Telegram: Bild (%u Byte) in %lu ms gesendet.\n", (unsigned) pngStreamSize(), millis() - start);

    if (response.indexOf("\"ok\":true") < 0) {
        Serial.println("Telegram: Antwort auf Bild: " + response);
        bot.sendMessage(chatId, "Das Bild konnte nicht gesendet werden.", "");
        return;
    }
    char buf[64];
    snprintf(buf, sizeof(buf), "%s %s, %.0f %% beleuchtet – ",
             moonPhaseEmoji(sky.phase, sky.waxing), moonPhaseName(sky.phase, sky.waxing), sky.phase * 100.0);
    bot.sendMessage(chatId, String(buf) + formatNow(), "");
}

// ── Profilbild ───────────────────────────────────────────────────────────────

// Feste Darstellung des Profilbilds, unabhängig von den Display-Optionen
constexpr int PROFILE_PHOTO_OPTIONS = OPTION_DARKEN_UNLIT | OPTION_USE_NASA_MODEL | OPTION_USE_LIBRATION | OPTION_IGNORE_PARALLACTIC ; // 13
constexpr int PROFILE_JPEG_BUFFER = 48 * 1024;
constexpr unsigned long PROFILE_RETRY_INTERVAL = 15UL * 60 * 1000; // ms

// Rendert den Mond als JPEG in out (PROFILE_JPEG_BUFFER Byte). Gibt die Länge zurück, 0 bei Fehler.
static int encodeMoonJpeg(const struct tm& utc, uint8_t* out) {
    JPEGENC* jpg = new (std::nothrow) JPEGENC;  // ca. 3 KB, zu groß für den Stack
    if (jpg == nullptr) return 0;
    prepareMoonRenderAt(utc, PROFILE_PHOTO_OPTIONS);

    constexpr int SIZE = 2 * MOON_RADIUS_PX;
    JPEGENCODE enc;
    uint16_t block[16 * 16];
    int rc = jpg->open(out, PROFILE_JPEG_BUFFER);
    if (rc == JPEGE_SUCCESS) {
        rc = jpg->encodeBegin(&enc, SIZE, SIZE, JPEGE_PIXEL_RGB565, JPEGE_SUBSAMPLE_420, JPEGE_Q_HIGH);
    }
    // MCU für MCU (16x16 Pixel) rendern und kodieren
    while (rc == JPEGE_SUCCESS && enc.y < SIZE) {
        for (int y = 0; y < enc.cy; y++) {
            for (int x = 0; x < enc.cx; x++) {
                block[y * enc.cx + x] = photoPixel(enc.x + x, enc.y + y);
            }
        }
        rc = jpg->addMCU(&enc, (uint8_t*) block, enc.cx * sizeof(uint16_t));
        if (enc.x == 0) yield();
    }
    int len = jpg->close();
    if (rc != JPEGE_SUCCESS || jpg->getLastError() != JPEGE_SUCCESS) {
        Serial.printf("Telegram: JPEG-Fehler %d\n", jpg->getLastError());
        len = 0;
    }
    delete jpg;
    return len;
}

static const char PHOTO_BOUNDARY[] = "------------------------mond0profil0photo";

// Ein Textfeld des multipart-Formulars
static String formField(const char* name, const String& value) {
    return String("--") + PHOTO_BOUNDARY + "\r\n"
        "Content-Disposition: form-data; name=\"" + name + "\"\r\n\r\n" + value + "\r\n";
}

// Sendet das JPEG als Datei fileField zusammen mit den Formularfeldern fields an die Bot-Methode
// method. Die Library kennt setMyProfilePhoto und setChatPhoto nicht, deshalb wird die
// multipart-Anfrage hier selbst gebaut.
static bool uploadJpeg(const char* method, const String& fields, const char* fileField,
                       const uint8_t* jpeg, int len) {
    String head = fields + "--" + PHOTO_BOUNDARY + "\r\n"
        "Content-Disposition: form-data; name=\"" + fileField + "\"; filename=\"moon.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    String tail = String("\r\n--") + PHOTO_BOUNDARY + "--\r\n";

    if (!telegramClient.connected() && !telegramClient.connect(TELEGRAM_HOST, TELEGRAM_SSL_PORT)) {
        Serial.printf("Telegram: keine Verbindung für %s.\n", method);
        return false;
    }
    telegramClient.print("POST /bot" TELEGRAM_BOT_TOKEN "/");
    telegramClient.print(method);
    telegramClient.print(" HTTP/1.1\r\n"
                         "Host: " TELEGRAM_HOST "\r\n"
                         "Connection: close\r\n"
                         "Content-Type: multipart/form-data; boundary=");
    telegramClient.print(PHOTO_BOUNDARY);
    telegramClient.print("\r\nContent-Length: ");
    telegramClient.print(head.length() + len + tail.length());
    telegramClient.print("\r\n\r\n");
    telegramClient.print(head);
    for (int pos = 0; pos < len; pos += 1024) {
        telegramClient.write(jpeg + pos, min(1024, len - pos));
    }
    telegramClient.print(tail);

    // Antwort lesen, bis der Server die Verbindung schließt
    String response;
    const unsigned long start = millis();
    while ((telegramClient.connected() || telegramClient.available()) && millis() - start < 15000) {
        while (telegramClient.available()) response += (char) telegramClient.read();
        delay(10);
    }
    telegramClient.stop();

    const bool ok = response.indexOf("\"ok\":true") >= 0;
    if (!ok) Serial.printf("Telegram: Antwort auf %s: %s\n", method, response.c_str());
    return ok;
}

constexpr int PHOTO_PROFILE = 1;  // Profilbild des Bots
constexpr int PHOTO_GROUP   = 2;  // Bild der Gruppe TELEGRAM_GROUP_ID

// Rendert den aktuellen Mond einmal und setzt ihn als Profil- und/oder Gruppenbild (targets).
// Gibt die erfolgreich gesetzten Ziele zurück.
static int updateMoonPhotos(int targets) {
    struct tm utc;
    if (!currentUtc(utc)) return 0;
    uint8_t* jpeg = (uint8_t*) malloc(PROFILE_JPEG_BUFFER);
    if (jpeg == nullptr) {
        Serial.println("Telegram: kein Speicher für Profilbild.");
        return 0;
    }
    const unsigned long start = millis();
    const int len = encodeMoonJpeg(utc, jpeg);
    int done = 0;
    if (len > 0 && (targets & PHOTO_PROFILE)
        && uploadJpeg("setMyProfilePhoto", formField("photo", "{\"type\":\"static\",\"photo\":\"attach://moon\"}"),
                      "moon", jpeg, len)) {
        done |= PHOTO_PROFILE;
    }
    // Der Bot braucht in der Gruppe Adminrechte mit "Gruppeninfo ändern"
    if (len > 0 && (targets & PHOTO_GROUP)
        && uploadJpeg("setChatPhoto", formField("chat_id", TELEGRAM_GROUP_ID), "photo", jpeg, len)) {
        done |= PHOTO_GROUP;
    }
    free(jpeg);
    Serial.printf("Telegram: Mondbild (%d Byte), Profilbild %s, Gruppenbild %s, %lu ms.\n", len,
                  (targets & PHOTO_PROFILE) ? ((done & PHOTO_PROFILE) ? "gesetzt" : "fehlgeschlagen") : "-",
                  (targets & PHOTO_GROUP) ? ((done & PHOTO_GROUP) ? "gesetzt" : "fehlgeschlagen") : "-",
                  millis() - start);
    return done;
}

// Aktualisiert Profil- und Gruppenbild nach dem Start und danach täglich zur Berichtszeit.
// Ein fehlgeschlagenes Ziel wird allein wiederholt, damit die Gruppe keine doppelte
// Meldung über ein neues Gruppenbild bekommt.
static void checkMoonPhotos() {
    time_t now = time(nullptr);
    if (now < MIN_VALID_TIME) return;

    struct tm lt;
    utcToLocal(now, lt);
    const int day = localDayKey(lt);
    const bool due = lt.tm_hour * 60 + lt.tm_min >= configAutoTime;
    int targets = 0;
    if (lastProfileDay == -1 || (day != lastProfileDay && due)) targets |= PHOTO_PROFILE;
    if (groupEnabled() && (lastGroupPhotoDay == -1 || (day != lastGroupPhotoDay && due))) targets |= PHOTO_GROUP;
    if (targets == 0) return;
    if (lastProfileAttempt != 0 && millis() - lastProfileAttempt < PROFILE_RETRY_INTERVAL) return;

    lastProfileAttempt = millis();
    const int done = updateMoonPhotos(targets);
    if (done & PHOTO_PROFILE) lastProfileDay = day;
    if (done & PHOTO_GROUP) lastGroupPhotoDay = day;
    if (done == targets) lastProfileAttempt = 0;
}

// ── Auto-Modus ───────────────────────────────────────────────────────────────

// Markiert den heutigen Bericht als erledigt, wenn die Berichtszeit schon vorbei ist.
// Verhindert, dass nach Neustart oder Umstellen der Uhrzeit sofort ein Bericht kommt.
void resetAutoReportDay() {
    time_t now = time(nullptr);
    if (now < MIN_VALID_TIME) {
        lastReportDay = lastGroupReportDay = lastPushoverDay = -1;
        return;
    }
    struct tm lt;
    utcToLocal(now, lt);
    int minutes = lt.tm_hour * 60 + lt.tm_min;
    lastReportDay = lastGroupReportDay = lastPushoverDay = (minutes >= configAutoTime) ? localDayKey(lt) : 0;
    lastPushoverAttempt = 0;
}

// Sendet den Tagesbericht zur Berichtszeit über jeden konfigurierten Kanal.
// Jeder Kanal wird getrennt wiederholt, bis der Versand klappt.
static void checkAutoReport() {
    time_t now = time(nullptr);
    if (now < MIN_VALID_TIME) return;
    if (lastReportDay == -1) {
        // Erste gültige Zeit nach dem Start
        resetAutoReportDay();
        return;
    }
    if (!configAutoMode) return;

    struct tm lt;
    utcToLocal(now, lt);
    int minutes = lt.tm_hour * 60 + lt.tm_min;
    int day = localDayKey(lt);
    if (minutes < configAutoTime) return;

    if (telegramEnabled && day != lastReportDay) {
        Serial.println("Telegram: sende Tagesbericht...");
        if (bot.sendMessage(TELEGRAM_CHAT_ID, buildDailyReport(), "")) {
            lastReportDay = day;
        }
    }
    if (telegramEnabled && groupEnabled() && day != lastGroupReportDay) {
        Serial.println("Telegram: sende Tagesbericht in die Gruppe...");
        if (bot.sendMessage(TELEGRAM_GROUP_ID, buildDailyReport(), "")) {
            lastGroupReportDay = day;
        }
    }
    if (pushoverEnabled() && day != lastPushoverDay
        && (lastPushoverAttempt == 0 || millis() - lastPushoverAttempt >= PUSHOVER_RETRY_INTERVAL)) {
        Serial.println("Pushover: sende Tagesbericht...");
        lastPushoverAttempt = millis();
        if (sendPushover(buildDailyReport(), "")) {
            lastPushoverDay = day;
            lastPushoverAttempt = 0;
        }
    }
}

// ── Befehle ──────────────────────────────────────────────────────────────────

static String handleCommand(String text, const String& chatId) {
    text.trim();
    int space = text.indexOf(' ');
    String cmd  = (space < 0) ? text : text.substring(0, space);
    String args = (space < 0) ? String("") : text.substring(space + 1);
    args.trim();
    // "/befehl@BotName" in Gruppen; Befehle an andere Bots ignorieren
    int at = cmd.indexOf('@');
    if (at >= 0) {
        String target = cmd.substring(at + 1);
        cmd = cmd.substring(0, at);
        if (bot.userName.length() > 0 && !target.equalsIgnoreCase(bot.userName)) return "";
    }
    cmd.toLowerCase();

    if (cmd == "/start" || cmd == "/hilfe" || cmd == "/help") {
        return HELP_TEXT;
    }
    if (cmd == "/mond") {
        bot.sendMessage(chatId, buildMoonMessage(), "HTML");
        return "";  // Antwort wurde bereits gesendet (HTML wegen des Festbreiten-Blocks)
    }
    if (cmd == "/bild") {
        sendMoonPhoto(chatId);
        return "";  // Antwort wurde bereits gesendet
    }
    if (cmd == "/profilbild") {
        const int targets = PHOTO_PROFILE | (groupEnabled() ? PHOTO_GROUP : 0);
        const int done = updateMoonPhotos(targets);
        if (done == targets) return groupEnabled() ? "Profil- und Gruppenbild aktualisiert." : "Profilbild aktualisiert.";
        String msg = (done & PHOTO_PROFILE) ? "Profilbild aktualisiert." : "Das Profilbild konnte nicht gesetzt werden.";
        if (groupEnabled()) {
            msg += (done & PHOTO_GROUP) ? "\nGruppenbild aktualisiert."
                                        : "\nDas Gruppenbild konnte nicht gesetzt werden (Bot Admin mit \"Gruppeninfo ändern\"?).";
        }
        return msg;
    }
    if (cmd == "/sonne") {
        return buildSunMessage();
    }
    if (cmd == "/bericht") {
        String report = buildDailyReport();
        if (pushoverEnabled() && !sendPushover(report, "")) {
            bot.sendMessage(chatId, "Pushover: Bericht konnte nicht gesendet werden.", "");
        }
        return report;
    }
    if (cmd == "/config") {
        return buildConfigMessage();
    }
    if (cmd == "/standort") {
        const char* s = args.c_str();
        char* endLat = nullptr;
        char* endLon = nullptr;
        char* endElev = nullptr;
        double lat = strtod(s, &endLat);
        double lon = strtod(endLat, &endLon);
        double elev = strtod(endLon, &endElev);  // optional, ohne Angabe 0 m
        if (endElev == endLon) elev = 0.0;
        if (endLat == s || endLon == endLat || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0
            || elev < -500.0 || elev > 9000.0) {
            return "Ungültige Werte. Erwartet: /standort <lat> <lon> [höhe]\nLat: -90..90, Lon: -180..180, Höhe: -500..9000 m";
        }
        configLocation.latitude  = astro::Angle::fromDeg(lat);
        configLocation.longitude = astro::Angle::fromDeg(lon);
        configLocation.elevation = elev;
        saveConfig();
        requestRedraw();
        char buf[80];
        snprintf(buf, sizeof(buf), "Standort gespeichert: %.6f / %.6f, %.0f m", lat, lon, elev);
        return buf;
    }
    if (cmd == "/optionen") {
        const char* s = args.c_str();
        char* end = nullptr;
        long value = strtol(s, &end, 0);
        if (end == s || value < 0 || value > 0x7FFFFFFF) {
            return "Ungültiger Wert. Erwartet: /optionen <wert>";
        }
        configDisplayOptions = (int) value;
        saveConfig();
        requestRedraw();
        return "Display-Optionen gespeichert: " + String(configDisplayOptions);
    }
    if (cmd == "/auto") {
        args.toLowerCase();
        if (args == "on" || args == "an") {
            configAutoMode = true;
        } else if (args == "off" || args == "aus") {
            configAutoMode = false;
        } else {
            return String("Auto-Modus ist ") + (configAutoMode ? "an" : "aus") + ". Erwartet: /auto on|off";
        }
        saveConfig();
        resetAutoReportDay();
        char buf[64];
        snprintf(buf, sizeof(buf), "Auto-Modus %s (täglich um %02d:%02d)",
                 configAutoMode ? "an" : "aus", configAutoTime / 60, configAutoTime % 60);
        return buf;
    }
    if (cmd == "/autozeit") {
        int minutes = parseHHMM(args.c_str());
        if (minutes < 0) {
            return "Ungültiges Format. Erwartet: /autozeit HH:MM";
        }
        configAutoTime = minutes;
        saveConfig();
        resetAutoReportDay();
        char buf[48];
        snprintf(buf, sizeof(buf), "Berichtszeit gespeichert: %02d:%02d", configAutoTime / 60, configAutoTime % 60);
        return buf;
    }
    if (cmd == "/modus") {
        args.toLowerCase();
        if (args == "mond") {
            setDisplayMode(DISPLAY_MODE_MOON);
        } else if (args == "uhr") {
            setDisplayMode(DISPLAY_MODE_CLOCK);
        } else {
            return String("Anzeige: ") + displayModeName() + ". Erwartet: /modus mond|uhr";
        }
        return String("Anzeige: ") + displayModeName();
    }
    if (cmd == "/zeitzone") {
        if (args.length() == 0) {
            return "Zeitzone: " + String(configLocation.timezone) + "\nErwartet: /zeitzone <POSIX-TZ>, z. B. CET-1CEST,M3.5.0,M10.5.0/3";
        }
        if (!isValidTimezone(args.c_str())) {
            return "Ungültige Zeitzone. Beispiel: CET-1CEST,M3.5.0,M10.5.0/3";
        }
        strlcpy(configLocation.timezone, args.c_str(), sizeof(configLocation.timezone));
        saveConfig();
        resetAutoReportDay();
        requestRedraw();
        return "Zeitzone gespeichert: " + String(configLocation.timezone) + "\nLokalzeit: " + formatNow();
    }
    return "Unbekannter Befehl. /hilfe zeigt alle Befehle.";
}

// ── Setup & Loop ─────────────────────────────────────────────────────────────

// Für die Uhr: Telegram ist eingerichtet, aber nicht erreichbar.
bool telegramConnectionLost() {
    return telegramEnabled && !telegramOnline;
}

// Prüft die Verbindung mit getMe, bei bestehender Verbindung nur gelegentlich.
// Gibt true zurück, wenn in diesem Durchlauf geprüft wurde.
static bool updateTelegramConnection() {
    const unsigned long interval = telegramOnline ? TELEGRAM_PROBE_ONLINE : TELEGRAM_PROBE_OFFLINE;
    if (lastTelegramProbe != 0 && millis() - lastTelegramProbe < interval) return false;
    lastTelegramProbe = millis();
    const bool ok = bot.getMe();
    // Die Library schließt nur Verbindungen, die sie noch für offen hält. Nach einem Fehler immer
    // schließen, damit der nächste Versuch sicher mit einer neuen Verbindung beginnt.
    if (!ok) telegramClient.stop();
    telegramOnline = ok;
    return true;
}

// Ein Update, das die Library nicht lesen kann (z. B. größer als maxMessageLength), lässt
// getUpdates 0 liefern, ohne den Offset weiterzurücken; es blockiert dann alle folgenden Nachrichten.
// Steht dasselbe Update bei zwei Prüfungen im Abstand von TELEGRAM_PROBE_ONLINE noch an, wird es
// übersprungen. Eine einzelne Prüfung reicht nicht, das Update kann auch gerade erst angekommen sein.
static long pendingUpdateId = -1;

static void skipUnreadableUpdate() {
    String response = bot.sendGetToTelegram(
        bot.buildCommand("getUpdates?limit=1&offset=" + String(bot.last_message_received + 1)));
    telegramClient.stop();
    const int pos = response.indexOf("\"update_id\":");
    if (pos < 0) {
        pendingUpdateId = -1;
        return;
    }
    const long id = response.substring(pos + 12).toInt();
    if (id != pendingUpdateId) {
        pendingUpdateId = id;
        return;
    }
    Serial.printf("Telegram: Update %ld nicht lesbar, wird übersprungen.\n", id);
    bot.last_message_received = id;
    pendingUpdateId = -1;
}

void setupTelegram() {
    telegramEnabled = strlen(TELEGRAM_BOT_TOKEN) > 0 && strlen(TELEGRAM_CHAT_ID) > 0;
    if (!telegramEnabled) {
        Serial.println("Telegram: kein Token/Chat-ID in credentials.h, Bot deaktiviert.");
        return;
    }
    telegramClient.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    bot.longPoll = 0;  // nicht blockieren, Display-Loop läuft weiter
    bot.waitForResponse = TELEGRAM_WAIT_FOR_RESPONSE;
    bot.maxMessageLength = TELEGRAM_MAX_MESSAGE_LENGTH;
}

void handleTelegram() {
    if (WiFi.status() != WL_CONNECTED) {
        telegramOnline = false;
        lastTelegramProbe = 0;  // nach dem WLAN-Aufbau sofort prüfen
        return;
    }
    if (millis() - lastTelegramPoll < TELEGRAM_POLL_INTERVAL) return;
    lastTelegramPoll = millis();

    // Tagesbericht auch ohne Telegram, wenn nur Pushover konfiguriert ist
    checkAutoReport();
    if (!telegramEnabled) return;

    const bool probed = updateTelegramConnection();
    if (!telegramOnline) {
        checkMoonPhotos();
        return;  // ohne Verbindung würde getUpdates nur bis zum Timeout warten
    }
    if (!telegramCommandsSet) {
        // getMe (oben) liefert auch den eigenen Benutzernamen, um in Gruppen "/befehl@AndererBot" zu erkennen
        telegramCommandsSet = bot.setMyCommands(BOT_COMMANDS);
        if (telegramCommandsSet) Serial.printf("Telegram bereit (@%s).\n", bot.userName.c_str());
    }

    int count = bot.getUpdates(bot.last_message_received + 1);
    while (count > 0) {
        for (int i = 0; i < count; i++) {
            const telegramMessage& message = bot.messages[i];
            // Nur Nachrichten des Besitzers, im privaten Chat oder in der konfigurierten Gruppe
            const bool isGroup = groupEnabled() && message.chat_id == TELEGRAM_GROUP_ID;
            if (message.from_id != TELEGRAM_CHAT_ID || (message.chat_id != TELEGRAM_CHAT_ID && !isGroup)) {
                Serial.printf("Telegram: Nachricht von %s in Chat %s ignoriert.\n",
                              message.from_id.c_str(), message.chat_id.c_str());
                continue;
            }
            // In der Gruppe nur auf Befehle reagieren, nicht auf normale Nachrichten
            if (isGroup && !message.text.startsWith("/")) continue;
            Serial.printf("Telegram: %s\n", message.text.c_str());
            String reply = handleCommand(message.text, message.chat_id);
            if (reply.length() > 0) {
                bot.sendMessage(message.chat_id, reply, "");
            }
        }
        count = bot.getUpdates(bot.last_message_received + 1);
    }
    // Höchstens einmal pro TELEGRAM_PROBE_ONLINE, direkt nach dem leeren getUpdates
    if (probed) skipUnreadableUpdate();

    checkMoonPhotos();
}
