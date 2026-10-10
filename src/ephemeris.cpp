#include "ephemeris.h"

#include <algorithm>
#include <cmath>

// Standort – persistent in Config.ino
extern astro::GeoLocation configLocation;
// Umrechnung in Lokalzeit (Config.ino)
extern void utcToLocal(time_t t, struct tm& out);

double julianDateFromTm(const struct tm& utc) {
    return astro::calculateJulianDate(
        utc.tm_year + 1900,
        utc.tm_mon  + 1,
        utc.tm_mday,
        utc.tm_hour + utc.tm_min / 60.0 + utc.tm_sec / 3600.0
    );
}

time_t timeFromJulianDate(double jd) {
    // JD 2440587.5 = 01.01.1970 00:00 UTC
    return (time_t) llround((jd - 2440587.5) * 86400.0);
}

SkyState computeSky(const struct tm& utc) {
    SkyState s;
    s.jd = julianDateFromTm(utc);

    // Lokale Sternzeit in Radiant (GMST + Längengrad-Offset)
    s.gmstHours    = astro::calculateGreenwichSiderealTime(s.jd);
    s.siderealTime = astro::calculateLocalSiderealTime(s.jd, configLocation);

    // ── Sonnenposition ───────────────────────────────────────────────────────
    const double eps = astro::calculateObliquity(s.jd);
    s.sun      = astro::calculateSunPosition(s.jd);
    s.sunRaDek = astro::calculateEquatorial(s.sun, eps);
    s.sunAzH   = astro::calculateHorizontal(s.sunRaDek, s.siderealTime, configLocation);

    // ── Mondposition ─────────────────────────────────────────────────────────
    s.moon = astro::calculateMoonPosition(s.jd);
    s.moonRaDek = astro::calculateEquatorial(s.moon, eps);
    // Äquatoriale Horizontalparallaxe [rad], Kapitel 45
    const double moonParallax = asin(astro::EARTH_RADIUS / s.moon.distance);
    s.moonRaDek = astro::calculateTopocentric(s.moonRaDek, moonParallax, s.siderealTime, configLocation);
    s.moonAzH   = astro::calculateHorizontal(s.moonRaDek, s.siderealTime, configLocation);

    // ── Mondachse & Libration ────────────────────────────────────────────────
    s.moonAxle = astro::calculateMoonOrientation(s.jd, s.moon);

    // ── Mondphase und parallaktischer Winkel ──────────────────────────────────
    s.phase  = astro::calculateMoonIlluminatedFraction(s.sunRaDek, s.sun.distance, s.moonRaDek, s.moon.distance);
    // Zunehmend, solange der Mond der Sonne in ekliptikaler Länge um weniger als 180° vorausläuft
    s.waxing = fmod(s.moon.longitude - s.sun.longitude + 4 * M_PI, 2 * M_PI) < M_PI;

    // Positionswinkel (Mitte) des beleuchteten Mondrandes
    // Formel 46.5 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    s.chi = atan2(cos(s.sunRaDek.dec) * sin(s.sunRaDek.ra - s.moonRaDek.ra),
                  sin(s.sunRaDek.dec) * cos(s.moonRaDek.dec)
                - cos(s.sunRaDek.dec) * sin(s.moonRaDek.dec) * cos(s.sunRaDek.ra - s.moonRaDek.ra));

    // Parallaktischer Winkel für Mond (ortsabhängig)
    s.q = astro::calculateParallacticAngle(s.moonRaDek, s.siderealTime, configLocation);

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
        const astro::RiseTransitSet sun = astro::calculateSunRiseSet(jd0 + 0.5, configLocation);
        if (sun.hasRise || sun.hasSet) e.sunValid = true;
        if (sun.hasRise) takeEvent(jd0, sun.rise, localDay, e.hasSunrise, e.sunrise);
        if (sun.hasSet)  takeEvent(jd0, sun.set,  localDay, e.hasSunset,  e.sunset);
        if (withMoon) {
            const astro::RiseTransitSet moon = astro::calculateMoonRiseSet(jd0 + 0.5, configLocation);
            if (moon.hasRise)    takeEvent(jd0, moon.rise,    localDay, e.hasMoonrise,    e.moonrise);
            if (moon.hasSet)     takeEvent(jd0, moon.set,     localDay, e.hasMoonset,     e.moonset);
            if (moon.hasTransit) takeEvent(jd0, moon.transit, localDay, e.hasMoonTransit, e.moonTransit);
        }
    }
    return e;
}

// ── Ausblick ─────────────────────────────────────────────────────────────────

constexpr double MOON_MAX_DECLINATION = (23.44 + 5.15) * astro::DEG2RAD; // Große Mondwende
constexpr double LUNAR_ECLIPSE_LIMIT  = 0.95 * astro::DEG2RAD;  // |β| beim Vollmond: Kernschatten
constexpr double SOLAR_ECLIPSE_LIMIT  = 1.45 * astro::DEG2RAD;  // |β| beim Neumond: partiell oder mehr
constexpr double SUPER_MOON_DISTANCE  = 360000.0;  // km
constexpr double MINI_MOON_DISTANCE   = 405000.0;  // km
constexpr double OUTLOOK_DAYS         = 14.0;

// Kulminationshöhe (geozentrisch) des Mondes am lokalen Kalendertag, der t enthält.
static double culminationHeight(time_t t) {

    struct tm lt;
    utcToLocal(t, lt);
    const int localDay = localDayKey(lt);
    const time_t dayStart = t - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);

    // Meridiandurchgang des lokalen Tags; ohne Durchgang (ca. 1x im Monat) Mittag
    bool found = false;
    time_t transit = dayStart + 12 * 3600;
    const double jdFirst = utMidnight(dayStart - 3600);
    const double jdLast  = utMidnight(dayStart + 86400 + 3600);
    for (double jd0 = jdFirst; jd0 <= jdLast + 0.1; jd0 += 1.0) {
        const astro::RiseTransitSet moon = astro::calculateMoonRiseSet(jd0 + 0.5, configLocation);
        if (moon.hasTransit) takeEvent(jd0, moon.transit, localDay, found, transit);
    }

    const double jdTransit = transit / 86400.0 + 2440587.5;
    const astro::Ecliptic moon = astro::calculateMoonPosition(jdTransit);
    const double dec = astro::calculateEquatorial(moon, astro::calculateObliquity(jdTransit)).dec;
    return M_PI / 2 - fabs(configLocation.latitude - dec);
}

// Elongation des Mondes (ekliptikale Länge Mond − Sonne), -π..π
static double elongation(double jd) {
    const double e = astro::calculateMoonPosition(jd).longitude - astro::calculateSunPosition(jd).longitude;
    return atan2(sin(e), cos(e));
}

// Sucht den ersten Zeitpunkt in [jdStart, jdEnd], an dem die Elongation target (0 oder π) erreicht.
static bool findElongation(double jdStart, double jdEnd, double target, double& jdOut) {
    auto diff = [&](double jd) {
        const double d = elongation(jd) - target;
        return atan2(sin(d), cos(d));
    };
    double a = jdStart;
    double da = diff(a);
    for (double b = a + 0.25; a < jdEnd; a = b, b += 0.25) {
        const double db = diff(b);
        // Vorzeichenwechsel von − nach + nahe 0 (nicht der Sprung bei ±π)
        if (da < 0 && db >= 0 && db - da < M_PI) {
            for (int i = 0; i < 20; i++) {
                const double m = (a + b) / 2;
                if (diff(m) < 0) a = m; else b = m;
            }
            jdOut = (a + b) / 2;
            return jdOut <= jdEnd;
        }
        da = db;
    }
    return false;
}

MoonOutlook moonOutlookFor(time_t t) {
    MoonOutlook o = {};
    const double lat = configLocation.latitude;

    // Höhenbereich am Standort bei maximaler Monddeklination
    const double hA = M_PI / 2 - fabs(lat - MOON_MAX_DECLINATION);
    const double hB = M_PI / 2 - fabs(lat + MOON_MAX_DECLINATION);
    const double hMin = std::min(hA, hB);
    const double hMax = fabs(lat) < MOON_MAX_DECLINATION ? M_PI / 2 : std::max(hA, hB);

    const double today    = culminationHeight(t);
    const double tomorrow = culminationHeight(t + 86400);
    // Die Deklination verläuft etwa sinusförmig, der Mond verweilt also lange nahe den Extremen.
    // asin() macht daraus eine zeitlich gleichmäßige Skala u (-1..1); die äußeren Stufen sind
    // schmal, damit ▁ und ▇ nur an den Tagen um das Extrem erscheinen (je ~10 % bei großer Mondwende).
    const double x = std::max(-1.0, std::min(1.0, 2 * (today - hMin) / (hMax - hMin) - 1));
    const double u = asin(x) / (M_PI / 2);
    o.heightLevel = u < -0.8 ? 0 : u < -0.3 ? 1 : u <= 0.3 ? 2 : u <= 0.8 ? 3 : 4;
    o.heightRising = tomorrow > today;

    struct tm lt;
    utcToLocal(t, lt);
    const time_t dayStart = t - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);
    const double jdStart = dayStart / 86400.0 + 2440587.5;
    const double jdEnd   = jdStart + OUTLOOK_DAYS;

    double jd;
    if (findElongation(jdStart, jdEnd, M_PI, jd)) {
        o.hasFullMoon = true;
        o.fullMoon = timeFromJulianDate(jd);
        const astro::Ecliptic moon = astro::calculateMoonPosition(jd);
        o.lunarEclipse = fabs(moon.latitude) < LUNAR_ECLIPSE_LIMIT;
        o.superMoon = moon.distance < SUPER_MOON_DISTANCE;
        o.miniMoon  = moon.distance > MINI_MOON_DISTANCE;
        if (o.lunarEclipse) {
            struct tm utc;
            gmtime_r(&o.fullMoon, &utc);
            o.lunarEclipseVisible = computeSky(utc).moonAzH.altitude > 0;
        }
    }
    if (findElongation(jdStart, jdEnd, 0.0, jd)) {
        o.hasNewMoon = true;
        o.newMoon = timeFromJulianDate(jd);
        o.solarEclipse = fabs(astro::calculateMoonPosition(jd).latitude) < SOLAR_ECLIPSE_LIMIT;
    }
    return o;
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
