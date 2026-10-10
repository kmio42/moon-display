#ifndef EPHEMERIS_H
#define EPHEMERIS_H

#include <time.h>
#include "astro.h"

// Momentaufnahme von Sonne und Mond für den konfigurierten Standort.
// Mond topozentrisch (parallaxenkorrigiert).
struct SkyState {
    astro::JulianDay jd;
    astro::SiderealTime gmst;       // Greenwich-Sternzeit
    astro::SiderealTime siderealTime;  // Lokale Sternzeit
    astro::Equatorial sunRaDek;
    astro::Horizontal sunAzH;
    astro::Ecliptic sun;            // Geozentrisch, Distanz in km
    astro::Ecliptic moon;           // Geozentrisch, Distanz in km
    astro::Equatorial moonRaDek;         // Topozentrisch
    astro::Horizontal moonAzH;
    astro::MoonOrientation moonAxle;
    double phase;                   // Beleuchteter Anteil 0..1
    bool waxing;                    // Zunehmend
    astro::Angle chi;               // Positionswinkel des hellen Mondrandes
    astro::Angle q;                 // Parallaktischer Winkel des Mondes
};

// Auf- und Untergänge eines lokalen Kalendertags als UTC-Zeitpunkte.
struct DayEvents {
    bool sunValid;          // Sonne geht an mindestens einem beteiligten UT-Tag auf/unter
    bool hasSunrise, hasSunset, hasMoonrise, hasMoonset, hasMoonTransit;
    time_t sunrise, sunset, moonrise, moonset, moonTransit;  // moonTransit: Meridiandurchgang
};

// Ausblick für den Tagesbericht: Mondhöhe und Ereignisse der nächsten 14 Tage.
struct MoonOutlook {
    int  heightLevel;       // Kulminationshöhe heute, 0 (sehr tief) .. 4 (sehr hoch)
    bool heightRising;      // Morgen höher als heute
    // Nächster Voll-/Neumond ab Beginn des lokalen Tags innerhalb von 14 Tagen (UTC)
    bool hasFullMoon, hasNewMoon;
    time_t fullMoon, newMoon;
    bool lunarEclipse;      // Kernschatten-Mondfinsternis beim Vollmond
    bool lunarEclipseVisible; // Mond zur Finsternismitte über dem Horizont
    bool solarEclipse;      // Sonnenfinsternis beim Neumond (irgendwo auf der Erde)
    bool superMoon, miniMoon;
};

// Mondphase für Name und Symbol. Die Reihenfolge entspricht den Texten PhaseNew … PhaseWaningCrescent
// in i18n_ids.h.
enum class MoonPhase {
    New, WaxingCrescent, FirstQuarter, WaxingGibbous, Full, WaningGibbous, LastQuarter, WaningCrescent
};
constexpr int MOON_PHASE_COUNT = 8;

SkyState computeSky(const struct tm& utc);
int localDayKey(const struct tm& t);
DayEvents eventsForLocalDay(time_t t, bool withMoon);
MoonOutlook moonOutlookFor(time_t t);
MoonPhase moonPhaseOf(double phase, bool waxing);
const char* moonPhaseEmoji(double phase, bool waxing);

#endif // EPHEMERIS_H
