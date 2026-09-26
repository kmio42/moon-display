#ifndef EPHEMERIS_H
#define EPHEMERIS_H

#include <time.h>
#include "astro.h"

// Momentaufnahme von Sonne und Mond für den konfigurierten Standort.
// Winkel in Radiant, Mond topozentrisch (parallaxenkorrigiert).
struct SkyState {
    double jd;
    double gmstHours;               // Greenwich-Sternzeit in Stunden
    double siderealTime;            // Lokale Sternzeit in Radiant
    double sunLongitude;            // Ekliptikale Länge der Sonne
    astro::RaDek sunRaDek;
    astro::AzimutHeight sunAzH;
    astro::MoonPosition moon;       // Geozentrisch, Distanz in km
    astro::RaDek moonRaDek;         // Topozentrisch
    astro::AzimutHeight moonAzH;
    astro::MoonAxle moonAxle;
    double phase;                   // Beleuchteter Anteil 0..1
    bool waxing;                    // Zunehmend
    double chi;                     // Positionswinkel des hellen Mondrandes
    double q;                       // Parallaktischer Winkel des Mondes
};

// Auf- und Untergänge eines lokalen Kalendertags als UTC-Zeitpunkte.
struct DayEvents {
    bool sunValid;          // Sonne geht an mindestens einem beteiligten UT-Tag auf/unter
    bool hasSunrise, hasSunset, hasMoonrise, hasMoonset;
    time_t sunrise, sunset, moonrise, moonset;
};

double julianDateFromTm(const struct tm& utc);
time_t timeFromJulianDate(double jd);
SkyState computeSky(const struct tm& utc);
int localDayKey(const struct tm& t);
DayEvents eventsForLocalDay(time_t t, bool withMoon);
const char* moonPhaseName(double phase, bool waxing);
const char* moonPhaseEmoji(double phase, bool waxing);

#endif // EPHEMERIS_H
