/**
 * Messages.ino – Tagesbericht, Mond- und Sonnenkoordinaten als Text
 *
 * Gemeinsam für Telegram, die serielle Konsole und Pushover. Die Texte kommen aus i18n.h.
 * Alle Berechnungen laufen in UT, erst die Ausgabe wird in Lokalzeit (configLocation.timezone)
 * umgerechnet.
 */

#include <algorithm>
#include <iterator>
#include <stdarg.h>
#include <time.h>

#include "astro.h"
#include "ephemeris.h"
#include "messages.h"

extern void utcToLocal(time_t t, struct tm& out);

// ── Formatierung ─────────────────────────────────────────────────────────────

bool currentUtc(struct tm& utc) {
    time_t now = time(nullptr);
    if (now < MIN_VALID_TIME) return false;
    gmtime_r(&now, &utc);
    return true;
}

String textf(Txt format, ...) {
    char buf[256];
    va_list args;
    va_start(args, format);
    vsnprintf(buf, sizeof(buf), tr(format), args);
    va_end(args);
    return buf;
}

// Formatiert eine UTC-Zeit als lokale Uhrzeit "HH:MM".
String formatLocalTime(time_t t) {
    struct tm lt;
    utcToLocal(t, lt);
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", lt.tm_hour, lt.tm_min);
    return buf;
}

// Formatiert eine UTC-Zeit als lokales Datum ohne Jahr.
String formatLocalDate(time_t t) {
    struct tm lt;
    utcToLocal(t, lt);
    char buf[16];
    strftime(buf, sizeof(buf), tr(Txt::DatePattern), &lt);
    return buf;
}

String formatNow() {
    struct tm lt;
    utcToLocal(time(nullptr), lt);
    char buf[32];
    strftime(buf, sizeof(buf), tr(Txt::DateTimePattern), &lt);
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

// Datum ohne Jahr und Uhrzeit in Lokalzeit
static String formatLocalDateTime(time_t t) {
    return formatLocalDate(t) + " " + formatLocalTime(t);
}

// ── Tagesbericht ─────────────────────────────────────────────────────────────

String buildDailyReport() {
    struct tm utc;
    if (!currentUtc(utc)) return tr(Txt::NoValidTime);

    time_t now = time(nullptr);
    struct tm lt;
    utcToLocal(now, lt);
    const time_t dayStart = now - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);

    DayEvents today     = eventsForLocalDay(now, true);
    DayEvents yesterday = eventsForLocalDay(dayStart - 1, false);
    SkyState sky = computeSky(utc);

    char buf[64];
    String msg;

    if (today.sunValid) {
        msg += "☼︎↑ ";
        msg += today.hasSunrise ? formatLocalTime(today.sunrise) : String("-");
        msg += " ☼︎↓ ";
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
    if (first.length() && second.length()) msg += " ";
    msg += second;
    if (!first.length() && !second.length()) msg += "☽-";
    msg += "\n";
    // Phase, Kulminationshöhe als Balken und deren Tendenz
    static const char* const HEIGHT_BARS[] = {"▁", "▂", "▃", "▅", "▇"};
    MoonOutlook outlook = moonOutlookFor(now);
    snprintf(buf, sizeof(buf), "%s %.0f%% (%s) %s%s",
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

// ── Mond und Sonne ───────────────────────────────────────────────────────────

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

// Eine Zeile des Festbreiten-Blocks. Die Bezeichnung wird auf BLOCK_LABEL_WIDTH Zeichen aufgefüllt;
// gezählt werden Zeichen, nicht UTF-8-Bytes (wegen "ö").
static String blockLine(Txt label, const String& value) {
    String line = tr(label);
    int chars = 0;
    for (const char* p = tr(label); *p; p++) {
        if ((*p & 0xC0) != 0x80) chars++;
    }
    for (; chars < BLOCK_LABEL_WIDTH; chars++) line += ' ';
    return line + value + "\n";
}

// Mondkoordinaten wie in einem Sternprogramm, als HTML mit Festbreiten-Block (parse_mode HTML).
String buildMoonMessage() {
    struct tm utc;
    if (!currentUtc(utc)) return tr(Txt::NoValidTime);
    SkyState sky = computeSky(utc);
    const time_t now = time(nullptr);

    char buf[48];
    String msg = String(moonPhaseEmoji(sky.phase, sky.waxing)) + " " + tr(Txt::MoonTitle) + " – " + formatNow() + "\n<pre>";

    msg += blockLine(Txt::LblEcliptic, formatDm(sky.moon.longitude, true, true) + "/" + formatDm(sky.moon.latitude, true));
    msg += blockLine(Txt::LblRaDec, formatHm(sky.moonRaDek.ra) + "/" + formatDm(sky.moonRaDek.dec, true));
    // Azimut nach Meeus von Süden gezählt (S=0°, W=90°)
    msg += blockLine(Txt::LblAzAlt, formatDm(sky.moonAzH.azimuth, true, true) + "/" + formatDm(sky.moonAzH.altitude, true));

    snprintf(buf, sizeof(buf), " (%.0f %%)", diameterPercent(sky.moon.distance));
    msg += blockLine(Txt::LblDiameter, formatArcmin(moonDiameter(sky.moon.distance)) + buf);

    // Elongation, Formel 46.2 aus "Astronomische Algorithmen" von Jean Meeus (Breite der Sonne = 0)
    const astro::Angle elongation = astro::Angle::acos(cos(sky.moon.latitude) * cos(sky.moon.longitude - sky.sun.longitude));
    msg += blockLine(Txt::LblElongation, formatDm(elongation, false) + " " + tr(sky.waxing ? Txt::East : Txt::West));

    snprintf(buf, sizeof(buf), "%s, %.1f %%", tr(sky.waxing ? Txt::Waxing : Txt::Waning), sky.phase * 100.0);
    msg += blockLine(Txt::LblPhase, buf);
    msg += blockLine(Txt::LblLibration, "L " + formatDm(sky.moonAxle.libration.longitude, true)
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
        Txt label;
        bool valid;
        time_t time;
        String suffix;
    } events[] = {
        {Txt::LblRise,    today.hasMoonrise,    today.moonrise,    ""},
        {Txt::LblTransit, today.hasMoonTransit, today.moonTransit, transitHeight},
        {Txt::LblSet,     today.hasMoonset,     today.moonset,     ""},
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
    msg += blockLine(Txt::LblPerigee, formatLocalDateTime(astro::JulianDay(perigee.jde).toUnixTime()) + buf);
    snprintf(buf, sizeof(buf), " (%.0f %%)", diameterPercent(apogee.distance));
    msg += blockLine(Txt::LblApogee, formatLocalDateTime(astro::JulianDay(apogee.jde).toUnixTime()) + buf);
    const astro::NodePassage node = astro::calculateNextMoonNode(sky.jd);
    msg += blockLine(Txt::LblNode, formatLocalDateTime(astro::JulianDay(node.jde).toUnixTime()) + " "
                                 + tr(node.ascending ? Txt::NodeAscending : Txt::NodeDescending));
    msg.remove(msg.length() - 1);  // letzter Zeilenumbruch, sonst Leerzeile im Block
    return msg + "</pre>";
}

String buildSunMessage() {
    struct tm utc;
    if (!currentUtc(utc)) return tr(Txt::NoValidTime);
    SkyState sky = computeSky(utc);

    String msg = String("☀️ ") + tr(Txt::SunTitle) + " – " + formatNow() + "\n\n";
    msg += String(tr(Txt::SunRaDec)) + ": " + formatRa(sky.sunRaDek.ra) + " / " + formatDeg(sky.sunRaDek.dec) + "\n";
    msg += String(tr(Txt::SunAzAlt)) + ": " + formatAzimut(sky.sunAzH.azimuth) + " / " + formatDeg(sky.sunAzH.altitude);
    return msg;
}
