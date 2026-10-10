#include "ephemeris.h"

#include <algorithm>
#include <cmath>

using astro::Angle;
using astro::JulianDay;
using namespace astro::literals;

// Standort – persistent in Config.ino
extern astro::GeoLocation configLocation;
// Umrechnung in Lokalzeit (Config.ino)
extern void utcToLocal(time_t t, struct tm& out);

SkyState computeSky(const struct tm& utc) {
    SkyState s;
    s.jd = JulianDay::fromTm(utc);

    // Sternzeit in Greenwich und am Standort
    s.gmst         = astro::calculateGreenwichSiderealTime(s.jd);
    s.siderealTime = astro::calculateLocalSiderealTime(s.jd, configLocation);

    // ── Sonnenposition ───────────────────────────────────────────────────────
    const Angle eps = astro::calculateObliquity(s.jd);
    s.sun      = astro::calculateSunPosition(s.jd);
    s.sunRaDek = astro::calculateEquatorial(s.sun, eps);
    s.sunAzH   = astro::calculateHorizontal(s.sunRaDek, s.siderealTime, configLocation);

    // ── Mondposition ─────────────────────────────────────────────────────────
    s.moon = astro::calculateMoonPosition(s.jd);
    s.moonRaDek = astro::calculateEquatorial(s.moon, eps);
    // Äquatoriale Horizontalparallaxe, Kapitel 45
    const Angle moonParallax = Angle::asin(astro::EARTH_RADIUS / s.moon.distance);
    s.moonRaDek = astro::calculateTopocentric(s.moonRaDek, moonParallax, s.siderealTime, configLocation);
    s.moonAzH   = astro::calculateHorizontal(s.moonRaDek, s.siderealTime, configLocation);

    // ── Mondachse & Libration ────────────────────────────────────────────────
    s.moonAxle = astro::calculateMoonOrientation(s.jd, s.moon);

    // ── Mondphase und parallaktischer Winkel ──────────────────────────────────
    s.phase  = astro::calculateMoonIlluminatedFraction(s.sunRaDek, s.sun.distance, s.moonRaDek, s.moon.distance);
    // Zunehmend, solange der Mond der Sonne in ekliptikaler Länge um weniger als 180° vorausläuft
    s.waxing = (s.moon.longitude - s.sun.longitude).normalized() < 180_deg;

    // Positionswinkel (Mitte) des beleuchteten Mondrandes
    // Formel 46.5 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    s.chi = Angle::atan2(cos(s.sunRaDek.dec) * sin(s.sunRaDek.ra - s.moonRaDek.ra),
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
static void takeEvent(JulianDay jd0, double utHours, int localDay, bool& found, time_t& out) {
    if (found) return;
    time_t t = jd0.toUnixTime() + (time_t) llround(utHours * 3600.0);
    struct tm lt;
    utcToLocal(t, lt);
    if (localDayKey(lt) == localDay) {
        out = t;
        found = true;
    }
}

// Julianischer Tag der UT-Mitternacht des UT-Tags, der t enthält.
static JulianDay utMidnight(time_t t) {
    return JulianDay::fromUnixTime(t).midnight();
}

// Zwischenspeicher für Mondauf-/-untergang. Uhr und Tagesbericht brauchen dieselben UT-Tage mehrfach
// (je Tag ca. 0,2 s auf dem ESP32-C3); die Uhr höchstens 5 (gestern bis morgen, im Winter je 3 UT-Tage).
constexpr int MOON_CACHE_DAYS = 5;

struct MoonCacheEntry {
    int32_t day;                    // ganzzahliger Teil der JD der UT-Mitternacht, 0 = leer
    astro::RiseTransitSet events;
};

static struct {
    Angle latitude, longitude;      // Standort, für den die Einträge gelten
    double elevation;               // [m]
    MoonCacheEntry entries[MOON_CACHE_DAYS];
    int next;                       // als Nächstes zu ersetzender Eintrag
} moonCache;

// Mondauf-/-untergang des UT-Tags ab jd0 (UT-Mitternacht). Die Referenz gilt bis zum nächsten Aufruf.
static const astro::RiseTransitSet& moonRiseSetForUtDay(JulianDay jd0) {
    // Nach einer Standortänderung alle Einträge verwerfen
    if (!(moonCache.latitude == configLocation.latitude && moonCache.longitude == configLocation.longitude
          && moonCache.elevation == configLocation.elevation)) {
        for (MoonCacheEntry& e : moonCache.entries) e.day = 0;
        moonCache.latitude  = configLocation.latitude;
        moonCache.longitude = configLocation.longitude;
        moonCache.elevation = configLocation.elevation;
    }

    const int32_t day = static_cast<int32_t>(jd0.value());
    for (const MoonCacheEntry& e : moonCache.entries) {
        if (e.day == day) return e.events;
    }
    MoonCacheEntry& e = moonCache.entries[moonCache.next];
    moonCache.next = (moonCache.next + 1) % MOON_CACHE_DAYS;
    e.day = day;
    e.events = astro::calculateMoonRiseSet(jd0 + 0.5, configLocation);
    return e.events;
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
    const JulianDay jdFirst = utMidnight(dayStart - 3600);
    const JulianDay jdLast  = utMidnight(dayStart + 86400 + 3600);

    DayEvents e = {};
    for (JulianDay jd0 = jdFirst; jd0 <= jdLast + 0.1; jd0 = jd0 + 1.0) {
        const astro::RiseTransitSet sun = astro::calculateSunRiseSet(jd0 + 0.5, configLocation);
        if (sun.hasRise || sun.hasSet) e.sunValid = true;
        if (sun.hasRise) takeEvent(jd0, sun.rise, localDay, e.hasSunrise, e.sunrise);
        if (sun.hasSet)  takeEvent(jd0, sun.set,  localDay, e.hasSunset,  e.sunset);
        if (withMoon) {
            const astro::RiseTransitSet& moon = moonRiseSetForUtDay(jd0);
            if (moon.hasRise)    takeEvent(jd0, moon.rise,    localDay, e.hasMoonrise,    e.moonrise);
            if (moon.hasSet)     takeEvent(jd0, moon.set,     localDay, e.hasMoonset,     e.moonset);
            if (moon.hasTransit) takeEvent(jd0, moon.transit, localDay, e.hasMoonTransit, e.moonTransit);
        }
    }
    return e;
}

// ── Ausblick ─────────────────────────────────────────────────────────────────

constexpr Angle MOON_MAX_DECLINATION = Angle::fromDeg(23.44 + 5.15); // Große Mondwende
constexpr Angle LUNAR_ECLIPSE_LIMIT  = 0.95_deg;  // |β| beim Vollmond: Kernschatten
constexpr Angle SOLAR_ECLIPSE_LIMIT  = 1.45_deg;  // |β| beim Neumond: partiell oder mehr
constexpr double SUPER_MOON_DISTANCE  = 360000.0;  // km
constexpr double MINI_MOON_DISTANCE   = 405000.0;  // km
constexpr double OUTLOOK_DAYS         = 14.0;

// Kulminationshöhe (geozentrisch) des Mondes am lokalen Kalendertag, der t enthält.
static Angle culminationHeight(time_t t) {

    struct tm lt;
    utcToLocal(t, lt);
    const int localDay = localDayKey(lt);
    const time_t dayStart = t - (lt.tm_hour * 3600 + lt.tm_min * 60 + lt.tm_sec);

    // Meridiandurchgang des lokalen Tags; ohne Durchgang (ca. 1x im Monat) Mittag
    bool found = false;
    time_t transit = dayStart + 12 * 3600;
    const JulianDay jdFirst = utMidnight(dayStart - 3600);
    const JulianDay jdLast  = utMidnight(dayStart + 86400 + 3600);
    for (JulianDay jd0 = jdFirst; jd0 <= jdLast + 0.1; jd0 = jd0 + 1.0) {
        const astro::RiseTransitSet& moon = moonRiseSetForUtDay(jd0);
        if (moon.hasTransit) takeEvent(jd0, moon.transit, localDay, found, transit);
    }

    const JulianDay jdTransit = JulianDay::fromUnixTime(transit);
    const astro::Ecliptic moon = astro::calculateMoonPosition(jdTransit);
    const Angle dec = astro::calculateEquatorial(moon, astro::calculateObliquity(jdTransit)).dec;
    return 90_deg - (configLocation.latitude - dec).abs();
}

// Elongation des Mondes (ekliptikale Länge Mond − Sonne), −180°..180°
static Angle elongation(JulianDay jd) {
    return (astro::calculateMoonPosition(jd).longitude - astro::calculateSunPosition(jd).longitude).normalizedSigned();
}

// Sucht den ersten Zeitpunkt in [jdStart, jdEnd] [JD], an dem die Elongation target (0° oder 180°) erreicht.
static bool findElongation(double jdStart, double jdEnd, Angle target, double& jdOut) {
    // Abweichung von target [rad], −π..π
    auto diff = [&](double jd) {
        return (elongation(JulianDay(jd)) - target).normalizedSigned().rad();
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
    const Angle lat = configLocation.latitude;

    // Höhenbereich am Standort bei maximaler Monddeklination
    const Angle hA = 90_deg - (lat - MOON_MAX_DECLINATION).abs();
    const Angle hB = 90_deg - (lat + MOON_MAX_DECLINATION).abs();
    const Angle hMin = std::min(hA, hB);
    const Angle hMax = lat.abs() < MOON_MAX_DECLINATION ? 90_deg : std::max(hA, hB);

    const Angle today    = culminationHeight(t);
    const Angle tomorrow = culminationHeight(t + 86400);
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
    if (findElongation(jdStart, jdEnd, 180_deg, jd)) {
        o.hasFullMoon = true;
        o.fullMoon = JulianDay(jd).toUnixTime();
        const astro::Ecliptic moon = astro::calculateMoonPosition(JulianDay(jd));
        o.lunarEclipse = moon.latitude.abs() < LUNAR_ECLIPSE_LIMIT;
        o.superMoon = moon.distance < SUPER_MOON_DISTANCE;
        o.miniMoon  = moon.distance > MINI_MOON_DISTANCE;
        if (o.lunarEclipse) {
            struct tm utc;
            gmtime_r(&o.fullMoon, &utc);
            o.lunarEclipseVisible = computeSky(utc).moonAzH.altitude > Angle();
        }
    }
    if (findElongation(jdStart, jdEnd, 0_deg, jd)) {
        o.hasNewMoon = true;
        o.newMoon = JulianDay(jd).toUnixTime();
        o.solarEclipse = astro::calculateMoonPosition(JulianDay(jd)).latitude.abs() < SOLAR_ECLIPSE_LIMIT;
    }
    return o;
}

MoonPhase moonPhaseOf(double phase, bool waxing) {
    if (phase < 0.03) return MoonPhase::New;
    if (phase > 0.97) return MoonPhase::Full;
    if (fabs(phase - 0.5) < 0.03) return waxing ? MoonPhase::FirstQuarter : MoonPhase::LastQuarter;
    if (phase < 0.5) return waxing ? MoonPhase::WaxingCrescent : MoonPhase::WaningCrescent;
    return waxing ? MoonPhase::WaxingGibbous : MoonPhase::WaningGibbous;
}

const char* moonPhaseEmoji(double phase, bool waxing) {
    static const char* const EMOJI[MOON_PHASE_COUNT] = {"🌑", "🌒", "🌓", "🌔", "🌕", "🌖", "🌗", "🌘"};
    return EMOJI[static_cast<int>(moonPhaseOf(phase, waxing))];
}
