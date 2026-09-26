#include "ephemeris.h"

// Standort – persistent in Config.ino
extern double configLatitude;
extern double configLongitude;
// Umrechnung in Lokalzeit (Config.ino)
extern void utcToLocal(time_t t, struct tm& out);

double julianDateFromTm(const struct tm& utc) {
    return astro::calculateJulianDate(
        utc.tm_year + 1900,
        utc.tm_mon  + 1,
        utc.tm_mday,
        utc.tm_hour,
        utc.tm_min,
        utc.tm_sec,
        0
    );
}

time_t timeFromJulianDate(double jd) {
    // JD 2440587.5 = 01.01.1970 00:00 UTC
    return (time_t) llround((jd - 2440587.5) * 86400.0);
}

SkyState computeSky(const struct tm& utc) {
    SkyState s;
    const double latitude = configLatitude * astro::DEG2RAD;

    s.jd = julianDateFromTm(utc);

    // Lokale Sternzeit in Radiant (GMST + Längengrad-Offset)
    s.gmstHours    = astro::calculateSiderealTime(s.jd);
    s.siderealTime = s.gmstHours * M_PI / 12.0 + configLongitude * astro::DEG2RAD;

    // ── Sonnenposition ───────────────────────────────────────────────────────
    s.sunLongitude = astro::calculateEclipticalLength(s.jd);
    s.sunRaDek     = astro::calculateRaDek(s.sunLongitude, 0.0);
    s.sunAzH       = astro::calculateHAzFromRaDek(s.sunRaDek, s.siderealTime, latitude);
    double sunDistance = 149597870;  // in km (1 AE)

    // ── Mondposition ─────────────────────────────────────────────────────────
    s.moon = astro::calculateMoon(s.jd);
    s.moonRaDek = astro::calculateRaDek(s.moon.longitude, s.moon.latitude);
    double moonParallax = asin(6378.14 / s.moon.distance);
    s.moonRaDek = astro::calculateParallax(s.moonRaDek, moonParallax, s.siderealTime, latitude);
    s.moonAzH   = astro::calculateHAzFromRaDek(s.moonRaDek, s.siderealTime, latitude);

    // ── Mondachse & Libration ────────────────────────────────────────────────
    s.moonAxle = astro::calculateMoonAxle(s.jd, s.moon);

    // ── Mondphase und parallaktischer Winkel ──────────────────────────────────
    s.phase  = astro::calculateMoonPhase(s.sunRaDek, sunDistance, s.moonRaDek, s.moon.distance);
    // Zunehmend, solange der Mond der Sonne in ekliptikaler Länge um weniger als 180° vorausläuft
    s.waxing = fmod(s.moon.longitude - s.sunLongitude + 4 * M_PI, 2 * M_PI) < M_PI;

    // Positionswinkel (Mitte) des beleuchteten Mondrandes
    // Formel 46.5 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    s.chi = atan2(cos(s.sunRaDek.dek) * sin(s.sunRaDek.ra - s.moonRaDek.ra),
                  sin(s.sunRaDek.dek) * cos(s.moonRaDek.dek)
                - cos(s.sunRaDek.dek) * sin(s.moonRaDek.dek) * cos(s.sunRaDek.ra - s.moonRaDek.ra));

    // Parallaktischer Winkel für Mond (ortsabhängig)
    s.q = astro::calculateParallacticAngle(s.moonRaDek, s.siderealTime, latitude);

    return s;
}

// Schlüssel für einen Kalendertag (Jahr*1000 + Tag im Jahr)
int localDayKey(const struct tm& t) {
    return t.tm_year * 1000 + t.tm_yday;
}

// Übernimmt ein Ereignis (UT-Stunden ab UT-Mitternacht jd0), wenn es lokal auf den Tag localDay fällt.
static void takeEvent(double jd0, double utHours, int localDay, bool& found, time_t& out) {
    if (found) return;
    time_t t = timeFromJulianDate(jd0) + (time_t) llround(utHours * 3600.0);
    struct tm lt;
    utcToLocal(t, lt);
    if (localDayKey(lt) == localDay) {
        out = t;
        found = true;
    }
}

// Julianisches Datum der UT-Mitternacht des UT-Tags, der t enthält.
static double utMidnight(time_t t) {
    return floor(t / 86400.0) + 2440587.5;
}

// Ermittelt die Ereignisse des lokalen Kalendertags, der den Zeitpunkt t enthält.
// Gerechnet wird in UT: Ein lokaler Tag überdeckt bis zu zwei UT-Tage, deren Ereignisse
// nach Umrechnung in Lokalzeit gefiltert werden.
DayEvents eventsForLocalDay(time_t t, bool withMoon) {
    const double lat = configLatitude  * astro::DEG2RAD;
    const double lon = configLongitude * astro::DEG2RAD;

    struct tm lt;
    utcToLocal(t, lt);
    const int localDay = localDayKey(lt);
    const time_t dayStart = t - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);

    // UT-Mitternacht des ersten und letzten beteiligten UT-Tags.
    // ±1 h Puffer, da dayStart an Tagen mit Sommerzeitumstellung um eine Stunde abweichen kann.
    const double jdFirst = utMidnight(dayStart - 3600);
    const double jdLast  = utMidnight(dayStart + 86400 + 3600);

    DayEvents e = {};
    for (double jd0 = jdFirst; jd0 <= jdLast + 0.1; jd0 += 1.0) {
        astro::SunriseSunset sun = astro::calculateSunriseSunset(jd0 + 0.5, lat, lon);
        if (sun.valid) {
            e.sunValid = true;
            takeEvent(jd0, sun.rising,  localDay, e.hasSunrise, e.sunrise);
            takeEvent(jd0, sun.setting, localDay, e.hasSunset,  e.sunset);
        }
        if (withMoon) {
            astro::MoonRiseSet moon = astro::calculateMoonRiseSet(jd0 + 0.5, lon, lat);
            if (moon.hasRise) takeEvent(jd0, moon.riseHours, localDay, e.hasMoonrise, e.moonrise);
            if (moon.hasSet)  takeEvent(jd0, moon.setHours,  localDay, e.hasMoonset,  e.moonset);
        }
    }
    return e;
}

const char* moonPhaseName(double phase, bool waxing) {
    if (phase < 0.03) return "Neumond";
    if (phase > 0.97) return "Vollmond";
    if (fabs(phase - 0.5) < 0.03) return waxing ? "Erstes Viertel" : "Letztes Viertel";
    if (phase < 0.5) return waxing ? "Zunehmende Sichel" : "Abnehmende Sichel";
    return waxing ? "Zunehmender Mond" : "Abnehmender Mond";
}

const char* moonPhaseEmoji(double phase, bool waxing) {
    if (phase < 0.03) return "🌑";
    if (phase > 0.97) return "🌕";
    if (fabs(phase - 0.5) < 0.03) return waxing ? "🌓" : "🌗";
    if (phase < 0.5) return waxing ? "🌒" : "🌘";
    return waxing ? "🌔" : "🌖";
}
