/**
 * Commands.ino – Befehle der seriellen Schnittstelle und von Telegram
 *
 * Beide Kanäle nutzen dieselbe Tabelle COMMANDS. Die Namen sind englisch und in allen Sprachen
 * gleich, die Ausgaben folgen der eingestellten Sprache (i18n.h).
 * Einstellungsbefehle ohne Argument zeigen den aktuellen Wert und die Syntax, mit Argument setzen
 * sie den Wert und zeigen ihn danach an.
 */

#include <WiFi.h>
#include <ArduinoJson.h>
#include <time.h>
#include <sys/time.h>

#include "astro.h"
#include "commands.h"
#include "messages.h"
#include "i18n.h"
#include "moon_render.h"
#include "display_mode.h"

// Persistente Konfiguration (definiert in Config.ino)
extern astro::GeoLocation configLocation;
extern int    configDisplayOptions;
extern bool   configAutoMode;
extern int    configAutoTime;
extern int    configDisplayMode;
extern int    configLanguage;
extern void   saveConfig();
extern bool   isValidTimezone(const char* tz);
extern int    parseHHMM(const char* s);

// Anzeige und WLAN (definiert in MondPhase.ino)
extern void requestRedraw();
extern void setDisplayMode(int displayMode);
extern void showMoonAt(const struct tm& utc);
extern void setWifiEnabled(bool on);
extern bool wifiEnabled();

// Telegram (definiert in Telegram.ino)
extern bool   telegramConfigured();
extern bool   telegramGroupConfigured();
extern void   telegramLanguageChanged();
extern void   resetAutoReportDay();
extern void   sendMoonPhoto(const String& chatId);
extern void   sendClockPhoto(const String& chatId);
extern String refreshMoonPhotos();

// Pushover (definiert in Pushover.ino)
extern bool pushoverEnabled();
extern bool sendPushover(const String& message, const String& title);

// ── Hilfsfunktionen ──────────────────────────────────────────────────────────

// Telegram-Befehle beginnen mit "/", serielle nicht
static const char* commandPrefix(Channel channel) {
    return channel == CH_TELEGRAM ? "/" : "";
}

// "Ändern: location [lat lon [elev]]"
static String changeHint(const CommandContext& ctx) {
    return textf(Txt::ChangeFmt, commandPrefix(ctx.channel), ctx.command->name, ctx.command->usage);
}

static Reply invalidInput(const CommandContext& ctx, const String& hint = String()) {
    String msg = textf(Txt::InvalidInputFmt, commandPrefix(ctx.channel), ctx.command->name, ctx.command->usage);
    if (hint.length() > 0) msg += "\n" + hint;
    return {msg};
}

// Antwort eines Einstellungsbefehls: ohne Argument mit Syntax, nach dem Setzen nur der Wert
static Reply settingReply(const CommandContext& ctx, const String& status) {
    if (ctx.args.length() == 0) return {status + "\n" + changeHint(ctx)};
    return {status};
}

static bool atEnd(const char* p) {
    while (*p == ' ') p++;
    return *p == '\0';
}

// Datum und Uhrzeit (UTC) als "YYYY-MM-DD HH:MM[:SS]" oder nur "HH:MM[:SS]" für heute.
static bool parseDateTime(const String& s, struct tm& tm) {
    static const char* const WITH_DATE[] = {"%Y-%m-%d %H:%M:%S", "%Y-%m-%d %H:%M"};
    static const char* const TIME_ONLY[] = {"%H:%M:%S", "%H:%M"};
    for (const char* format : WITH_DATE) {
        memset(&tm, 0, sizeof(tm));
        const char* end = strptime(s.c_str(), format, &tm);
        if (end != nullptr && atEnd(end)) return true;
    }
    const time_t now = time(nullptr);
    for (const char* format : TIME_ONLY) {
        gmtime_r(&now, &tm);
        tm.tm_sec = 0;
        const char* end = strptime(s.c_str(), format, &tm);
        if (end != nullptr && atEnd(end)) return true;
    }
    return false;
}

static String formatIsoUtc(const struct tm& utc) {
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &utc);
    return buf;
}

// ── Abfragen ─────────────────────────────────────────────────────────────────

static Reply cmdHelp(const CommandContext& ctx);

static Reply cmdMoon(const CommandContext&) {
    return {buildMoonMessage(), true};
}

static Reply cmdSun(const CommandContext&) {
    return {buildSunMessage()};
}

static Reply cmdReport(const CommandContext&) {
    String report = buildDailyReport();
    if (pushoverEnabled() && !sendPushover(report, "")) {
        report += "\n\n" + String(tr(Txt::ReportPushoverFailed));
    }
    return {report};
}

static Reply cmdImage(const CommandContext& ctx) {
    sendMoonPhoto(ctx.chatId);
    return {};
}

static Reply cmdClock(const CommandContext& ctx) {
    sendClockPhoto(ctx.chatId);
    return {};
}

static Reply cmdAvatar(const CommandContext&) {
    return {refreshMoonPhotos()};
}

// ── Einstellungen ────────────────────────────────────────────────────────────

static String locationStatus() {
    return textf(Txt::LocationFmt, configLocation.latitude.deg(), configLocation.longitude.deg(),
                 configLocation.elevation);
}

static Reply cmdLocation(const CommandContext& ctx) {
    if (ctx.args.length() > 0) {
        const char* s = ctx.args.c_str();
        char* endLat = nullptr;
        char* endLon = nullptr;
        char* endElev = nullptr;
        const double lat = strtod(s, &endLat);
        const double lon = strtod(endLat, &endLon);
        double elev = strtod(endLon, &endElev);  // optional, ohne Angabe 0 m
        if (endElev == endLon) elev = 0.0;
        if (endLat == s || endLon == endLat || !atEnd(endElev) || lat < -90.0 || lat > 90.0
            || lon < -180.0 || lon > 180.0 || elev < -500.0 || elev > 9000.0) {
            return invalidInput(ctx, tr(Txt::LocationRange));
        }
        configLocation.latitude  = astro::Angle::fromDeg(lat);
        configLocation.longitude = astro::Angle::fromDeg(lon);
        configLocation.elevation = elev;
        saveConfig();
        requestRedraw();
    }
    return settingReply(ctx, locationStatus());
}

static String optionsStatus() {
    static const struct {
        int bit;
        Txt text;
    } OPTIONS[] = {
        {OPTION_DARKEN_UNLIT,          Txt::OptDarkenUnlit},
        {OPTION_BLUISH_TINT,           Txt::OptBluishTint},
        {OPTION_USE_NASA_MODEL,        Txt::OptNasaTexture},
        {OPTION_USE_LIBRATION,         Txt::OptLibration},
        {OPTION_IGNORE_PARALLACTIC,    Txt::OptIgnoreParallactic},
        {OPTION_IGNORE_POSITION_ANGLE, Txt::OptIgnorePositionAngle},
    };
    String msg = textf(Txt::OptionsFmt, configDisplayOptions);
    char buf[80];
    for (const auto& o : OPTIONS) {
        snprintf(buf, sizeof(buf), "\n  [%c] %s (%d)", (configDisplayOptions & o.bit) ? 'x' : ' ', tr(o.text), o.bit);
        msg += buf;
    }
    return msg;
}

static Reply cmdOptions(const CommandContext& ctx) {
    if (ctx.args.length() > 0) {
        const char* s = ctx.args.c_str();
        char* end = nullptr;
        const long value = strtol(s, &end, 0);  // 0: dezimal, 0x… hexadezimal
        if (end == s || !atEnd(end) || value < 0 || value > 0x7FFFFFFF) return invalidInput(ctx);
        configDisplayOptions = (int) value;
        saveConfig();
        requestRedraw();
    }
    return settingReply(ctx, optionsStatus());
}

static String modeStatus() {
    return textf(Txt::ModeFmt, tr(configDisplayMode == DISPLAY_MODE_CLOCK ? Txt::ModeClock : Txt::ModeMoon));
}

static Reply cmdMode(const CommandContext& ctx) {
    if (ctx.args.length() > 0) {
        String arg = ctx.args;
        arg.toLowerCase();
        if (arg == "moon") {
            setDisplayMode(DISPLAY_MODE_MOON);
        } else if (arg == "clock") {
            setDisplayMode(DISPLAY_MODE_CLOCK);
        } else {
            return invalidInput(ctx);
        }
    }
    return settingReply(ctx, modeStatus());
}

static String dailyStatus() {
    String msg = textf(Txt::DailyFmt, tr(configAutoMode ? Txt::On : Txt::Off), configAutoTime / 60, configAutoTime % 60);
    String targets;
    if (telegramConfigured()) targets += tr(Txt::TargetChat);
    if (telegramGroupConfigured()) targets += String(targets.length() ? ", " : "") + tr(Txt::TargetGroup);
    if (pushoverEnabled()) targets += String(targets.length() ? ", " : "") + "Pushover";
    if (targets.length() == 0) targets = tr(Txt::TargetNone);
    return msg + "\n" + textf(Txt::DailyTargetsFmt, targets.c_str());
}

// "on", "off" oder eine Uhrzeit, die den Bericht zugleich einschaltet
static Reply cmdDaily(const CommandContext& ctx) {
    if (ctx.args.length() > 0) {
        String arg = ctx.args;
        arg.toLowerCase();
        const int minutes = parseHHMM(arg.c_str());
        if (arg == "on") {
            configAutoMode = true;
        } else if (arg == "off") {
            configAutoMode = false;
        } else if (minutes >= 0) {
            configAutoMode = true;
            configAutoTime = minutes;
        } else {
            return invalidInput(ctx);
        }
        saveConfig();
        resetAutoReportDay();
    }
    return settingReply(ctx, dailyStatus());
}

static String timezoneStatus() {
    return textf(Txt::TimezoneFmt, configLocation.timezone) + "\n" + textf(Txt::LocalTimeFmt, formatNow().c_str());
}

static Reply cmdTimezone(const CommandContext& ctx) {
    if (ctx.args.length() == 0) {
        return {timezoneStatus() + "\n" + changeHint(ctx) + "\n" + tr(Txt::TimezoneExample)};
    }
    if (!isValidTimezone(ctx.args.c_str())) return invalidInput(ctx, tr(Txt::TimezoneExample));
    strlcpy(configLocation.timezone, ctx.args.c_str(), sizeof(configLocation.timezone));
    saveConfig();
    resetAutoReportDay();
    requestRedraw();
    return {timezoneStatus()};
}

static String availableLanguages() {
    String codes;
    for (int i = 0; i < languageCount(); i++) {
        if (i > 0) codes += ", ";
        codes += languageCode(i);
    }
    return textf(Txt::LanguagesFmt, codes.c_str());
}

static Reply cmdLanguage(const CommandContext& ctx) {
    if (ctx.args.length() > 0) {
        String arg = ctx.args;
        arg.toLowerCase();
        const int lang = findLanguage(arg.c_str());
        if (lang < 0) return invalidInput(ctx, availableLanguages());
        configLanguage = lang;
        saveConfig();
        telegramLanguageChanged();
    }
    const String status = textf(Txt::LanguageFmt, languageName(configLanguage), languageCode(configLanguage));
    return settingReply(ctx, status + "\n" + availableLanguages());
}

// ── Nur seriell ──────────────────────────────────────────────────────────────

static Reply cmdShow(const CommandContext& ctx) {
    if (ctx.args.length() == 0) {
        requestRedraw();
        return {tr(Txt::ShowLive)};
    }
    struct tm utc;
    if (!parseDateTime(ctx.args, utc)) return invalidInput(ctx);
    showMoonAt(utc);
    return {textf(Txt::ShowAtFmt, formatIsoUtc(utc).c_str())};
}

static String timeStatus() {
    const time_t now = time(nullptr);
    struct tm utc;
    gmtime_r(&now, &utc);
    String msg = textf(Txt::SystemTimeFmt, formatIsoUtc(utc).c_str()) + "\n";
    if (now < MIN_VALID_TIME) return msg + tr(Txt::NoValidTime);
    return msg + textf(Txt::LocalTimeFmt, formatNow().c_str());
}

static Reply cmdTime(const CommandContext& ctx) {
    if (ctx.args.length() > 0) {
        struct tm utc;
        if (!parseDateTime(ctx.args, utc)) return invalidInput(ctx);
        struct timeval tv = { .tv_sec = mktime(&utc) };  // System läuft in UTC
        settimeofday(&tv, nullptr);
    }
    return settingReply(ctx, timeStatus());
}

static String wifiStatus() {
    if (!wifiEnabled()) return tr(Txt::WifiOff);
    if (WiFi.status() == WL_CONNECTED) return textf(Txt::WifiConnectedFmt, WiFi.SSID().c_str(), (int) WiFi.RSSI());
    return tr(Txt::WifiDisconnected);
}

static Reply cmdWifi(const CommandContext& ctx) {
    if (ctx.args.length() > 0) {
        String arg = ctx.args;
        arg.toLowerCase();
        if (arg == "on") {
            setWifiEnabled(true);
        } else if (arg == "off") {
            setWifiEnabled(false);
        } else {
            return invalidInput(ctx);
        }
    }
    return settingReply(ctx, wifiStatus());
}

// ── Tabelle ──────────────────────────────────────────────────────────────────

// Reihenfolge wie in der Hilfe und im Telegram-Menü
static const Command COMMANDS[] = {
    {"help",     "",                 Txt::CmdHelp,     CH_ALL,      cmdHelp},
    {"moon",     "",                 Txt::CmdMoon,     CH_ALL,      cmdMoon},
    {"sun",      "",                 Txt::CmdSun,      CH_ALL,      cmdSun},
    {"report",   "",                 Txt::CmdReport,   CH_ALL,      cmdReport},
    {"image",    "",                 Txt::CmdImage,    CH_TELEGRAM, cmdImage},
    {"clock",    "",                 Txt::CmdClock,    CH_TELEGRAM, cmdClock},
    {"avatar",   "",                 Txt::CmdAvatar,   CH_TELEGRAM, cmdAvatar},
    {"location", "[lat lon [elev]]", Txt::CmdLocation, CH_ALL,      cmdLocation},
    {"options",  "[value]",          Txt::CmdOptions,  CH_ALL,      cmdOptions},
    {"mode",     "[moon|clock]",     Txt::CmdMode,     CH_ALL,      cmdMode},
    {"daily",    "[on|off|HH:MM]",   Txt::CmdDaily,    CH_ALL,      cmdDaily},
    {"timezone", "[POSIX-TZ]",       Txt::CmdTimezone, CH_ALL,      cmdTimezone},
    {"language", "[code]",           Txt::CmdLanguage, CH_ALL,      cmdLanguage},
    {"show",     "[[YYYY-MM-DD] HH:MM[:SS]]", Txt::CmdShow, CH_SERIAL, cmdShow},
    {"time",     "[[YYYY-MM-DD] HH:MM[:SS]]", Txt::CmdTime, CH_SERIAL, cmdTime},
    {"wifi",     "[on|off]",         Txt::CmdWifi,     CH_SERIAL,   cmdWifi},
};

static Reply cmdHelp(const CommandContext& ctx) {
    String msg = String(tr(Txt::HelpTitle)) + "\n\n";
    for (const Command& c : COMMANDS) {
        if (!(c.channels & ctx.channel)) continue;
        msg += commandPrefix(ctx.channel);
        msg += c.name;
        if (c.usage[0] != '\0') msg += String(" ") + c.usage;
        msg += String(" – ") + tr(c.help) + "\n";
    }
    return {msg + "\n" + tr(Txt::HelpSettings) + "\n\n" + tr(Txt::HelpLegend)};
}

Reply runCommand(Channel channel, const String& line, const String& chatId) {
    String text = line;
    text.trim();
    const int space = text.indexOf(' ');
    String name = (space < 0) ? text : text.substring(0, space);
    String args = (space < 0) ? String() : text.substring(space + 1);
    args.trim();
    if (name.startsWith("/")) name.remove(0, 1);
    name.toLowerCase();
    if (name == "start") name = "help";  // Telegram sendet /start beim ersten Kontakt

    for (const Command& c : COMMANDS) {
        if ((c.channels & channel) && name == c.name) {
            return c.run({channel, &c, args, chatId});
        }
    }
    return {textf(Txt::UnknownCommandFmt, name.c_str(), commandPrefix(channel))};
}

String buildBotCommandsJson() {
    DynamicJsonDocument doc(2048);
    JsonArray list = doc.to<JsonArray>();
    for (const Command& c : COMMANDS) {
        if (!(c.channels & CH_TELEGRAM)) continue;
        JsonObject entry = list.createNestedObject();
        entry["command"] = c.name;
        entry["description"] = tr(c.help);
    }
    String json;
    serializeJson(doc, json);
    return json;
}
