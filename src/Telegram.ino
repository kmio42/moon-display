/**
 * Telegram.ino – Telegram-Bot für Einstellungen, Koordinaten und Tagesbericht
 *
 * Nur Nachrichten aus TELEGRAM_CHAT_ID werden beantwortet.
 * Alle Berechnungen laufen in UT, erst die Ausgabe wird in Lokalzeit (configTimezone) umgerechnet.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <UniversalTelegramBot.h>
#include <JPEGENC.h>
#include <new>
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

// Persistente Konfiguration (definiert in Config.ino)
extern double configLatitude;
extern double configLongitude;
extern int    configDisplayOptions;
extern bool   configAutoMode;
extern int    configAutoTime;
extern String configTimezone;
extern void   saveConfig();
extern void   utcToLocal(time_t t, struct tm& out);
extern bool   isValidTimezone(const char* tz);
extern int    parseHHMM(const char* s);

// Display neu zeichnen (definiert in MondPhase.ino)
extern void requestRedraw();
extern void setDisplayMode(int displayMode);
extern const char* displayModeName();
extern int configDisplayMode;

constexpr unsigned long TELEGRAM_POLL_INTERVAL = 3000;  // ms
constexpr time_t        MIN_VALID_TIME         = 1700000000; // Zeit gilt erst nach NTP-Sync als gültig

static WiFiClientSecure telegramClient;
static UniversalTelegramBot bot(TELEGRAM_BOT_TOKEN, telegramClient);
static bool telegramEnabled = false;
static bool telegramCommandsSet = false;
static unsigned long lastTelegramPoll = 0;
static int lastReportDay = -1;  // Lokaler Tag (Jahr*1000 + Tag im Jahr) des letzten Tagesberichts
static int lastProfileDay = -1; // Lokaler Tag des letzten Profilbilds, -1: seit dem Start keins
static unsigned long lastProfileAttempt = 0;

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

static String formatDeg(double rad) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%+.2f°", rad * astro::RAD2DEG);
    return buf;
}

static String formatRa(double rad) {
    double h = fmod(rad * astro::RAD2DEG / 15.0 + 24.0, 24.0);
    int hh = (int) h;
    int mm = (int) ((h - hh) * 60.0);
    int ss = (int) (((h - hh) * 60.0 - mm) * 60.0);
    char buf[16];
    snprintf(buf, sizeof(buf), "%02dh %02dm %02ds", hh, mm, ss);
    return buf;
}

// Azimut in astro.cpp wird nach Meeus von Süden gezählt; für die Ausgabe ab Norden (N=0°, O=90°).
static String formatAzimut(double rad) {
    char buf[16];
    snprintf(buf, sizeof(buf), "%.1f°", fmod(rad * astro::RAD2DEG + 540.0, 360.0));
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
    String msg = buf;
    msg += "\n\n";

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

static String buildMoonMessage() {
    struct tm utc;
    if (!currentUtc(utc)) return "Keine gültige Uhrzeit (NTP) verfügbar.";
    SkyState sky = computeSky(utc);

    char buf[64];
    String msg = String(moonPhaseEmoji(sky.phase, sky.waxing)) + " Mond – " + formatNow() + "\n\n";
    msg += "RA / Dek: " + formatRa(sky.moonRaDek.ra) + " / " + formatDeg(sky.moonRaDek.dek) + "\n";
    msg += "Azimut / Höhe: " + formatAzimut(sky.moonAzH.azimut) + " / " + formatDeg(sky.moonAzH.height) + "\n";
    snprintf(buf, sizeof(buf), "Entfernung: %.0f km\n", sky.moon.distance);
    msg += buf;
    snprintf(buf, sizeof(buf), "Phase: %s, %.1f %% beleuchtet\n", moonPhaseName(sky.phase, sky.waxing), sky.phase * 100.0);
    msg += buf;
    msg += "Libration: Länge " + formatDeg(sky.moonAxle.libration.longitude)
         + " / Breite " + formatDeg(sky.moonAxle.libration.latitude);
    return msg;
}

static String buildSunMessage() {
    struct tm utc;
    if (!currentUtc(utc)) return "Keine gültige Uhrzeit (NTP) verfügbar.";
    SkyState sky = computeSky(utc);

    String msg = "☀️ Sonne – " + formatNow() + "\n\n";
    msg += "RA / Dek: " + formatRa(sky.sunRaDek.ra) + " / " + formatDeg(sky.sunRaDek.dek) + "\n";
    msg += "Azimut / Höhe: " + formatAzimut(sky.sunAzH.azimut) + " / " + formatDeg(sky.sunAzH.height);
    return msg;
}

static String buildConfigMessage() {
    char buf[96];
    String msg = "⚙️ Konfiguration\n\n";
    snprintf(buf, sizeof(buf), "Standort: %.6f / %.6f\n", configLatitude, configLongitude);
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
    snprintf(buf, sizeof(buf), "Auto-Modus: %s, täglich um %02d:%02d\n",
             configAutoMode ? "an" : "aus", configAutoTime / 60, configAutoTime % 60);
    msg += buf;
    msg += "Zeitzone: " + configTimezone + "\n";
    msg += String("Anzeige: ") + displayModeName() + "\n";
    msg += "Lokalzeit: " + formatNow();
    return msg;
}

static const char HELP_TEXT[] =
    "🌙 Mond-Display\n\n"
    "/mond – aktuelle Mondkoordinaten\n"
    "/bild – aktuelles Mondbild\n"
    "/profilbild – Profilbild des Bots jetzt aktualisieren\n"
    "/sonne – aktuelle Sonnenkoordinaten\n"
    "/bericht – Tagesbericht jetzt senden\n"
    "/config – aktuelle Einstellungen\n"
    "/standort <lat> <lon> – Standort in Dezimalgrad\n"
    "/optionen <wert> – Darstellung (Bits: 1=abdunkeln, 2=bläulich, 4=NASA-Textur, 8=Libration)\n"
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
    "{\"command\":\"profilbild\",\"description\":\"Profilbild des Bots aktualisieren\"},"
    "{\"command\":\"sonne\",\"description\":\"Aktuelle Sonnenkoordinaten\"},"
    "{\"command\":\"bericht\",\"description\":\"Tagesbericht jetzt senden\"},"
    "{\"command\":\"config\",\"description\":\"Aktuelle Einstellungen\"},"
    "{\"command\":\"standort\",\"description\":\"Standort setzen: <lat> <lon>\"},"
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
constexpr int PROFILE_PHOTO_OPTIONS = OPTION_DARKEN_UNLIT | OPTION_USE_NASA_MODEL | OPTION_USE_LIBRATION; // 13
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

// Setzt das JPEG als Profilbild des Bots (setMyProfilePhoto). Die Library kennt die Methode
// nicht, deshalb wird die multipart-Anfrage hier selbst gebaut.
static bool uploadProfilePhoto(const uint8_t* jpeg, int len) {
    static const char BOUNDARY[] = "------------------------mond0profil0photo";
    String head = String("--") + BOUNDARY + "\r\n"
        "Content-Disposition: form-data; name=\"photo\"\r\n\r\n"
        "{\"type\":\"static\",\"photo\":\"attach://moon\"}\r\n"
        "--" + BOUNDARY + "\r\n"
        "Content-Disposition: form-data; name=\"moon\"; filename=\"moon.jpg\"\r\n"
        "Content-Type: image/jpeg\r\n\r\n";
    String tail = String("\r\n--") + BOUNDARY + "--\r\n";

    if (!telegramClient.connected() && !telegramClient.connect(TELEGRAM_HOST, TELEGRAM_SSL_PORT)) {
        Serial.println("Telegram: keine Verbindung für Profilbild.");
        return false;
    }
    telegramClient.print("POST /bot" TELEGRAM_BOT_TOKEN "/setMyProfilePhoto HTTP/1.1\r\n"
                         "Host: " TELEGRAM_HOST "\r\n"
                         "Connection: close\r\n"
                         "Content-Type: multipart/form-data; boundary=");
    telegramClient.print(BOUNDARY);
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
    if (!ok) Serial.println("Telegram: Antwort auf Profilbild: " + response);
    return ok;
}

// Rendert den aktuellen Mond und setzt ihn als Profilbild.
static bool updateProfilePhoto() {
    struct tm utc;
    if (!currentUtc(utc)) return false;
    uint8_t* jpeg = (uint8_t*) malloc(PROFILE_JPEG_BUFFER);
    if (jpeg == nullptr) {
        Serial.println("Telegram: kein Speicher für Profilbild.");
        return false;
    }
    const unsigned long start = millis();
    const int len = encodeMoonJpeg(utc, jpeg);
    const bool ok = len > 0 && uploadProfilePhoto(jpeg, len);
    free(jpeg);
    Serial.printf("Telegram: Profilbild (%d Byte) %s in %lu ms.\n", len, ok ? "gesetzt" : "fehlgeschlagen", millis() - start);
    return ok;
}

// Aktualisiert das Profilbild nach dem Start und danach täglich zur Berichtszeit.
static void checkProfilePhoto() {
    time_t now = time(nullptr);
    if (now < MIN_VALID_TIME) return;

    struct tm lt;
    utcToLocal(now, lt);
    const int day = localDayKey(lt);
    if (lastProfileDay != -1 && (day == lastProfileDay || lt.tm_hour * 60 + lt.tm_min < configAutoTime)) return;
    if (lastProfileAttempt != 0 && millis() - lastProfileAttempt < PROFILE_RETRY_INTERVAL) return;

    lastProfileAttempt = millis();
    if (updateProfilePhoto()) {
        lastProfileDay = day;
        lastProfileAttempt = 0;
    }
}

// ── Auto-Modus ───────────────────────────────────────────────────────────────

// Markiert den heutigen Bericht als erledigt, wenn die Berichtszeit schon vorbei ist.
// Verhindert, dass nach Neustart oder Umstellen der Uhrzeit sofort ein Bericht kommt.
void resetAutoReportDay() {
    time_t now = time(nullptr);
    if (now < MIN_VALID_TIME) {
        lastReportDay = -1;
        return;
    }
    struct tm lt;
    utcToLocal(now, lt);
    int minutes = lt.tm_hour * 60 + lt.tm_min;
    lastReportDay = (minutes >= configAutoTime) ? localDayKey(lt) : 0;
}

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
    if (day != lastReportDay && minutes >= configAutoTime) {
        Serial.println("Telegram: sende Tagesbericht...");
        if (bot.sendMessage(TELEGRAM_CHAT_ID, buildDailyReport(), "")) {
            lastReportDay = day;
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
    // "/befehl@BotName" in Gruppen
    int at = cmd.indexOf('@');
    if (at >= 0) cmd = cmd.substring(0, at);
    cmd.toLowerCase();

    if (cmd == "/start" || cmd == "/hilfe" || cmd == "/help") {
        return HELP_TEXT;
    }
    if (cmd == "/mond") {
        return buildMoonMessage();
    }
    if (cmd == "/bild") {
        sendMoonPhoto(chatId);
        return "";  // Antwort wurde bereits gesendet
    }
    if (cmd == "/profilbild") {
        return updateProfilePhoto() ? "Profilbild aktualisiert." : "Das Profilbild konnte nicht gesetzt werden.";
    }
    if (cmd == "/sonne") {
        return buildSunMessage();
    }
    if (cmd == "/bericht") {
        return buildDailyReport();
    }
    if (cmd == "/config") {
        return buildConfigMessage();
    }
    if (cmd == "/standort") {
        const char* s = args.c_str();
        char* endLat = nullptr;
        char* endLon = nullptr;
        double lat = strtod(s, &endLat);
        double lon = strtod(endLat, &endLon);
        if (endLat == s || endLon == endLat || lat < -90.0 || lat > 90.0 || lon < -180.0 || lon > 180.0) {
            return "Ungültige Werte. Erwartet: /standort <lat> <lon>\nLat: -90..90, Lon: -180..180";
        }
        configLatitude  = lat;
        configLongitude = lon;
        saveConfig();
        requestRedraw();
        char buf[64];
        snprintf(buf, sizeof(buf), "Standort gespeichert: %.6f / %.6f", configLatitude, configLongitude);
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
            return "Zeitzone: " + configTimezone + "\nErwartet: /zeitzone <POSIX-TZ>, z. B. CET-1CEST,M3.5.0,M10.5.0/3";
        }
        if (!isValidTimezone(args.c_str())) {
            return "Ungültige Zeitzone. Beispiel: CET-1CEST,M3.5.0,M10.5.0/3";
        }
        configTimezone = args;
        saveConfig();
        resetAutoReportDay();
        requestRedraw();
        return "Zeitzone gespeichert: " + configTimezone + "\nLokalzeit: " + formatNow();
    }
    return "Unbekannter Befehl. /hilfe zeigt alle Befehle.";
}

// ── Setup & Loop ─────────────────────────────────────────────────────────────

void setupTelegram() {
    telegramEnabled = strlen(TELEGRAM_BOT_TOKEN) > 0 && strlen(TELEGRAM_CHAT_ID) > 0;
    if (!telegramEnabled) {
        Serial.println("Telegram: kein Token/Chat-ID in credentials.h, Bot deaktiviert.");
        return;
    }
    telegramClient.setCACert(TELEGRAM_CERTIFICATE_ROOT);
    bot.longPoll = 0;  // nicht blockieren, Display-Loop läuft weiter
}

void handleTelegram() {
    if (!telegramEnabled || WiFi.status() != WL_CONNECTED) return;
    if (millis() - lastTelegramPoll < TELEGRAM_POLL_INTERVAL) return;
    lastTelegramPoll = millis();

    if (!telegramCommandsSet) {
        telegramCommandsSet = bot.setMyCommands(BOT_COMMANDS);
        if (telegramCommandsSet) Serial.println("Telegram bereit.");
    }

    int count = bot.getUpdates(bot.last_message_received + 1);
    while (count > 0) {
        for (int i = 0; i < count; i++) {
            const telegramMessage& message = bot.messages[i];
            if (message.chat_id != TELEGRAM_CHAT_ID) {
                Serial.printf("Telegram: Nachricht von unbekannter Chat-ID %s ignoriert.\n", message.chat_id.c_str());
                continue;
            }
            Serial.printf("Telegram: %s\n", message.text.c_str());
            String reply = handleCommand(message.text, message.chat_id);
            if (reply.length() > 0) {
                bot.sendMessage(message.chat_id, reply, "");
            }
        }
        count = bot.getUpdates(bot.last_message_received + 1);
    }

    checkAutoReport();
    checkProfilePhoto();
}
