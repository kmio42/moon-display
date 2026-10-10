/**
 * Telegram.ino – Telegram-Bot: Befehle, Bilder und Tagesbericht
 *
 * Die Befehle selbst stehen in Commands.ino, die Texte in Messages.ino.
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
#include <time.h>

#include "astro.h"
#include "ephemeris.h"
#include "moon_render.h"
#include "clock_face.h"
#include "png_stream.h"
#include "display_mode.h"
#include "commands.h"
#include "messages.h"
#include "i18n.h"
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
extern bool   configAutoMode;
extern int    configAutoTime;
extern void   utcToLocal(time_t t, struct tm& out);

// Pushover (definiert in Pushover.ino)
extern bool pushoverEnabled();
extern bool sendPushover(const String& message, const String& title);

constexpr unsigned long TELEGRAM_POLL_INTERVAL = 3000;  // ms
constexpr unsigned int  TELEGRAM_WAIT_FOR_RESPONSE = 5000; // ms, Library-Standard 1500 ist bei langsamem Netz zu knapp
constexpr int           TELEGRAM_MAX_MESSAGE_LENGTH = 4000; // Byte, Library-Standard 1500 reicht nicht für jedes Update
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

// Für den Befehl daily: Ziele des Tagesberichts
bool telegramConfigured() {
    return telegramEnabled;
}

bool telegramGroupConfigured() {
    return telegramEnabled && groupEnabled();
}

// Nach einem Sprachwechsel das Befehlsmenü mit den neuen Beschreibungen setzen
void telegramLanguageChanged() {
    telegramCommandsSet = false;
}

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

// Sendet das mit pngStreamBegin() begonnene Bild. Die Zeilen werden erst während des Uploads
// berechnet (kein Bildpuffer nötig). Meldet einen Fehler auch im Chat.
static bool sendPngStream(const String& chatId, const char* what) {
    const unsigned long start = millis();
    // Telegram braucht für das Verarbeiten des Bildes länger als die Standard-Wartezeit
    const unsigned int wait = bot.waitForResponse;
    bot.waitForResponse = 15000;
    String response = bot.sendPhotoByBinary(chatId, "image/png", pngStreamSize(),
                                            photoMoreData, nullptr, photoBuffer, photoBufferLen);
    bot.waitForResponse = wait;
    Serial.printf("Telegram: %s (%u Byte) in %lu ms gesendet.\n", what, (unsigned) pngStreamSize(), millis() - start);

    if (response.indexOf("\"ok\":true") < 0) {
        Serial.println("Telegram: Antwort auf Bild: " + response);
        bot.sendMessage(chatId, tr(Txt::ImageFailed), "");
        return false;
    }
    return true;
}

// Rendert den Mond für die aktuelle Zeit und sendet ihn als PNG.
void sendMoonPhoto(const String& chatId) {
    struct tm utc;
    if (!currentUtc(utc)) {
        bot.sendMessage(chatId, tr(Txt::NoValidTime), "");
        return;
    }
    SkyState sky = computeSky(utc);

    prepareMoonRenderAt(utc);
    pngStreamBegin(2 * MOON_RADIUS_PX, 2 * MOON_RADIUS_PX, photoPixel);
    if (!sendPngStream(chatId, "Mondbild")) return;

    bot.sendMessage(chatId, textf(Txt::ImageCaptionFmt, moonPhaseEmoji(sky.phase, sky.waxing),
                                  moonPhaseName(moonPhaseOf(sky.phase, sky.waxing)), sky.phase * 100.0,
                                  formatNow().c_str()), "");
}

// Zeichnet die 24h-Uhr für die aktuelle Zeit, unabhängig vom Anzeigemodus, und sendet sie als PNG.
void sendClockPhoto(const String& chatId) {
    const time_t now = time(nullptr);
    if (now < MIN_VALID_TIME) {
        bot.sendMessage(chatId, tr(Txt::NoValidTime), "");
        return;
    }
    const unsigned long start = millis();
    if (!clockImageBegin(now)) {
        Serial.println("Telegram: kein Speicher für das Bild der Uhr.");
        bot.sendMessage(chatId, tr(Txt::ClockNoMemory), "");
        return;
    }
    Serial.printf("Telegram: Uhr gezeichnet (%lu ms)\n", millis() - start);
    pngStreamBegin(CLOCK_IMAGE_SIZE, CLOCK_IMAGE_SIZE, clockImagePixel);
    sendPngStream(chatId, "Bild der Uhr");
    clockImageEnd();
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

// Für den Befehl avatar: setzt Profil- und Gruppenbild sofort und meldet das Ergebnis.
String refreshMoonPhotos() {
    const int targets = PHOTO_PROFILE | (groupEnabled() ? PHOTO_GROUP : 0);
    const int done = updateMoonPhotos(targets);
    String msg = tr((done & PHOTO_PROFILE) ? Txt::ProfilePhotoSet : Txt::ProfilePhotoFailed);
    if (groupEnabled()) {
        msg += "\n";
        msg += tr((done & PHOTO_GROUP) ? Txt::GroupPhotoSet : Txt::GroupPhotoFailed);
    }
    return msg;
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

    const bool chatDue     = telegramEnabled && day != lastReportDay;
    const bool groupDue    = telegramEnabled && groupEnabled() && day != lastGroupReportDay;
    const bool pushoverDue = pushoverEnabled() && day != lastPushoverDay
                             && (lastPushoverAttempt == 0 || millis() - lastPushoverAttempt >= PUSHOVER_RETRY_INTERVAL);
    if (!chatDue && !groupDue && !pushoverDue) return;

    // Einmal berechnen (ca. 1–2 s) und an alle fälligen Ziele senden
    const unsigned long start = millis();
    const String report = buildDailyReport();
    Serial.printf("Tagesbericht erstellt (%lu ms)\n", millis() - start);

    if (chatDue) {
        Serial.println("Telegram: sende Tagesbericht...");
        if (bot.sendMessage(TELEGRAM_CHAT_ID, report, "")) {
            lastReportDay = day;
        }
    }
    if (groupDue) {
        Serial.println("Telegram: sende Tagesbericht in die Gruppe...");
        if (bot.sendMessage(TELEGRAM_GROUP_ID, report, "")) {
            lastGroupReportDay = day;
        }
    }
    if (pushoverDue) {
        Serial.println("Pushover: sende Tagesbericht...");
        lastPushoverAttempt = millis();
        if (sendPushover(report, "")) {
            lastPushoverDay = day;
            lastPushoverAttempt = 0;
        }
    }
}

// ── Befehle ──────────────────────────────────────────────────────────────────

// Führt einen Befehl aus der Tabelle in Commands.ino aus. In Gruppen kann ein Befehl an einen
// bestimmten Bot gerichtet sein ("/befehl@BotName"); Befehle an andere Bots werden ignoriert.
static void handleCommand(String text, const String& chatId) {
    text.trim();
    const int space = text.indexOf(' ');
    String cmd = (space < 0) ? text : text.substring(0, space);
    const String rest = (space < 0) ? String() : text.substring(space);
    const int at = cmd.indexOf('@');
    if (at >= 0) {
        const String target = cmd.substring(at + 1);
        cmd = cmd.substring(0, at);
        if (bot.userName.length() > 0 && !target.equalsIgnoreCase(bot.userName)) return;
    }
    const Reply reply = runCommand(CH_TELEGRAM, cmd + rest, chatId);
    if (reply.text.length() > 0) {
        bot.sendMessage(chatId, reply.text, reply.html ? "HTML" : "");
    }
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
        telegramCommandsSet = bot.setMyCommands(buildBotCommandsJson());
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
            handleCommand(message.text, message.chat_id);
        }
        count = bot.getUpdates(bot.last_message_received + 1);
    }
    // Höchstens einmal pro TELEGRAM_PROBE_ONLINE, direkt nach dem leeren getUpdates
    if (probed) skipUnreadableUpdate();

    checkMoonPhotos();
}
