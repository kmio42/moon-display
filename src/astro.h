#ifndef ASTRO_H
#define ASTRO_H

/*
 * Astronomische Berechnungen nach Jean Meeus, "Astronomische Algorithmen",
 * 2. Auflage, Barth 1994 (Übersetzung von "Astronomical Algorithms", 1991).
 *
 * Konventionen:
 *  - Winkel in Radiant, sofern nicht anders angegeben.
 *  - Geographische Länge Ost positiv (abweichend vom Buch, dort West positiv).
 *  - Azimut von Süden über Westen gezählt (wie im Buch, Kapitel 12).
 *  - Zeiten in UT; die Dynamische Zeit TD wird mit UT gleichgesetzt (ΔT aus Kapitel 9 fehlt).
 *
 * Umgesetzt                                        Formeln/Tabellen          Funktion
 *  3  Interpolation                                3.3                       interpolate
 *  7  Julianischer Tag                             7.1                       calculateJulianDate
 * 10  Der Erdglobus (ρ sin φ′, ρ cos φ′)                                     calculateTopocentric
 * 11  Sternzeit in Greenwich                       11.1, 11.4                calculateGreenwichSiderealTime
 * 12  Umwandeln von Koordinaten                    12.3–12.6                 calculateEquatorial, calculateHorizontal
 * 13  Der parallaktische Winkel                    13.1                      calculateParallacticAngle
 * 14  Aufgang, Durchgang, Untergang (Sonne; h0)    14.1, 14.2                calculateSunriseSunset
 * 21  Schiefe der Ekliptik (ohne Nutation)         21.2                      calculateObliquity
 * 24  Koordinaten der Sonne (niedrige Genauigk.)   24.1–24.5                 calculateSunLongitude, calculateEarthSunDistance
 * 26  Äquinoktien und Solstitien                   Tab. 26.2, 26.3           calculateEquinoxSolstice
 * 27  Zeitgleichung (ohne Nutation)                27.1, 27.2                calculateEquationOfTime
 * 29  Keplergleichung                              29.1, 29.5                calculateEarthTrueAnomaly
 * 37  Planeten in Perihel und Aphel (nur Erde)                               calculateEarthApsis
 * 39  Parallaxe                                    39.2, 39.3                calculateTopocentric
 * 45  Die Mondposition                             45.1–45.7, Tab. 45.1/45.2 calculateMoonPosition, calculateMoonAscendingNode
 * 46  Beleuchteter Teil der Mondscheibe            46.1–46.3                 calculateMoonIlluminatedFraction
 * 48  Perigäum und Apogäum des Mondes              48.1, 48.3, Tab. 48.1/48.2 calculateNextMoonApsis
 * 49  Knotendurchgänge des Mondes                                            calculateNextMoonNode
 * 51  Ephemeride zur physischen Beobachtung        51.1, 51.2                calculateMoonOrientation
 *     des Mondes (Libration, Positionswinkel der Achse)
 *
 * Nicht aus dem Buch: Mondauf-/-untergang, Meridiandurchgang und höchster Stand
 * (eigene stündliche Abtastung mit Parabel), calculateMoonRiseSet.
 *
 * Nicht umgesetzt:
 *  1 Hinweise und Tips, 2 Über Genauigkeit (ohne Algorithmen),
 *  4 Kurvenanpassung, 5 Iteration, 6 Sortieren von Zahlen, 8 Osterdatum,
 *  9 Dynamische Zeit und Weltzeit, 15 Atmosphärische Refraktion, 16 Winkelabstand,
 * 17 Planetenkonjunktionen, 18 Objekte auf gerader Linie, 19 Drei Himmelsobjekte im kleinsten Kreis,
 * 20 Präzession, 22 Scheinbarer Ort eines Sternes,
 * 23 Reduktion ekliptikaler Elemente auf ein anderes Äquinoktium,
 * 25 Rechtwinklige Koordinaten der Sonne, 28 Ephemeriden zur physischen Beobachtung der Sonne,
 * 30 Elemente der Planetenbahnen, 31 Positionen der Planeten, 32 Elliptische Bewegung,
 * 33 Parabolische Bewegung, 34 Parabelnahe Bahnen, 35 Die Berechnung einiger Planetenphänomene,
 * 36 Pluto, 38 Knotendurchgänge (Planeten), 40 Beleuchteter Teil und Helligkeit eines Planeten,
 * 41 Physische Beobachtung des Mars, 42 Physische Beobachtung des Jupiter, 43 Jupitermonde,
 * 44 Der Ring des Saturn, 47 Die Mondphasen, 50 Maximale Deklinationen des Mondes,
 * 52 Finsternisse, 53 Halbmesser von Sonne, Mond und Planeten, 54 Sternhelligkeiten,
 * 55 Doppelsterne, 56 Berechnung einer ebenen Sonnenuhr
 */

#include <cmath>
#include <cstdint>

namespace astro {

// ---------------------------------------------------------------
//  Konstanten
// ---------------------------------------------------------------

// Kein eigenes PI: Arduino.h definiert PI als Makro.
constexpr double DEG2RAD = M_PI / 180.0;               // Umrechnung Grad → Radiant [rad/°]
constexpr double RAD2DEG = 180.0 / M_PI;               // Umrechnung Radiant → Grad [°/rad]

constexpr double AU_KM = 149597870.7;                  // Astronomische Einheit [km]
constexpr double EARTH_SEMI_MAJOR_AXIS = 1.000001018;  // Große Halbachse der Erdbahn [AE], Formel 24.5
constexpr double EARTH_RADIUS = 6378.14;               // Äquatorradius der Erde [km], Kapitel 10 und 45
constexpr double MOON_RADIUS = 1738.1;                 // Mondradius [km]
constexpr double MOON_ORBIT_RADIUS = 384400.0;         // Mittlere Entfernung Erde–Mond [km]

// ---------------------------------------------------------------
//  Datenstrukturen
// ---------------------------------------------------------------

// Äquatoriale Koordinaten
struct Equatorial {
    double ra;              // Rektaszension α [rad]
    double dec;             // Deklination δ [rad]
};

// Horizontale Koordinaten
struct Horizontal {
    double azimuth;         // Azimut A [rad], von Süden über Westen gezählt
    double altitude;        // Höhe h [rad]
};

// Geozentrische ekliptikale Position des Mondes
struct MoonPosition {
    double longitude;       // Ekliptikale Länge λ [rad]
    double latitude;        // Ekliptikale Breite β [rad]
    double distance;        // Entfernung Erde–Mond Δ [km]
};

// Libration des Mondes (Kapitel 51)
struct Libration {
    double longitude;       // Libration in Länge l [rad]
    double latitude;        // Libration in Breite b [rad]
};

// Orientierung des Mondes (Kapitel 51)
struct MoonOrientation {
    Libration libration;
    double positionAngle;   // Positionswinkel der Mondachse P [rad]
};

// Sonnenauf- und -untergang eines UT-Tags (Kapitel 14)
struct SunriseSunset {
    double transit;         // Meridiandurchgang [h UT]
    double rise;            // Aufgang [h UT]
    double set;             // Untergang [h UT]
    bool valid;             // false bei Polartag oder Polarnacht
};

// Mondauf-/-untergang, Meridiandurchgang und höchster Stand eines UT-Tags.
struct MoonRiseSet {
    bool   hasRise;          // Aufgang an diesem UT-Tag
    bool   hasSet;           // Untergang an diesem UT-Tag
    bool   hasTransit;       // Meridiandurchgang (Stundenwinkel 0) an diesem UT-Tag
    bool   aboveAtStart;     // Mond um 0 Uhr UT über dem Horizont
    double riseHours;        // Aufgang [h UT]
    double setHours;         // Untergang [h UT]
    double transitHours;     // Meridiandurchgang [h UT]
    double maxAltitude;      // Höchste Höhe über der Aufgangshöhe h0 [rad]
    double maxAltitudeHours; // Zeitpunkt der höchsten Höhe [h UT]
};

// Perigäum/Apogäum des Mondes (Kapitel 48)
enum class MoonApsisType { Perigee, Apogee };
struct MoonApsis {
    double jde;             // Zeitpunkt [JD]; TD wird mit UT gleichgesetzt
    double distance;        // Entfernung Erde–Mond [km]
};

// Knotendurchgang des Mondes durch die Ekliptik (Kapitel 49)
struct MoonNode {
    double jde;             // Zeitpunkt [JD]; TD wird mit UT gleichgesetzt
    bool ascending;         // true: aufsteigender Knoten (Breite wird positiv)
};

// Solstitien und Äquinoktien (Kapitel 26)
enum class EquinoxType { Spring, Summer, Autumn, Winter };

// Perihel/Aphel der Erde (Kapitel 37)
enum class EarthApsisType { Perihelion, Aphelion };

// Vektor und 3x3-Matrix für die Drehung der Mondtextur
struct Vec3 {
    double x, y, z;
};

struct Mat3 {
    double m[3][3];
};

// ---------------------------------------------------------------
//  Funktionen (Einheiten und Formeln siehe Kommentare in astro.cpp)
// ---------------------------------------------------------------

// Winkel-Normalisierung
double normalizeDegrees(double angle);                  // [°] → [0°, 360°)
double normalizeRadiansSigned(double angle);            // [rad] → (−π, π]

// Zeit
double calculateJulianDate(int year, int month, int day, double hours);  // Julianischer Tag [JD]
double calculateJulianCenturies(double jd);             // T seit J2000.0 [Julianische Jahrhunderte]
double calculateGreenwichSiderealTime(double jd);       // Mittlere Sternzeit Greenwich θ0 [h]

// Sonne und Erdbahn
double calculateObliquity(double jd);                   // Mittlere Schiefe der Ekliptik ε0 [rad]
double calculateSunLongitude(double jd);                // Wahre geometrische Länge der Sonne [rad]
double calculateEarthTrueAnomaly(double jd);            // Wahre Anomalie der Erde ν [rad]
double calculateEarthSunDistance(double jd);            // Abstand Erde–Sonne R [AE]
double calculateEarthEccentricity(double jd);           // Exzentrizität der Erdbahn [1]
double calculateEquationOfTime(double jd, bool withEarthTilt);  // Zeitgleichung [min]

// Koordinatentransformation (Winkel [rad], Sternzeit als lokale Sternzeit θ [rad])
Equatorial calculateEquatorial(double eclipticalLongitude, double eclipticalLatitude, double eps);
Horizontal calculateHorizontal(const Equatorial& equ, double localSiderealTime, double latitude);
double calculateParallacticAngle(const Equatorial& equ, double localSiderealTime, double latitude);
Equatorial calculateTopocentric(const Equatorial& equ, double parallax, double localSiderealTime, double latitude);

// Interpolation
double interpolate(double y1, double y2, double y3, double n);

// Sonnenauf-/-untergang
SunriseSunset calculateSunriseSunset(double jd, double latitude, double longitude);

// Solstitien und Äquinoktien, Perihel/Aphel der Erde [JDE]
double calculateEquinoxSolstice(int year, EquinoxType type);
double calculateEarthApsis(int year, int month, EarthApsisType type);

// Mond
MoonPosition calculateMoonPosition(double jd);
double calculateMoonAscendingNode(double jd);           // Länge des aufsteigenden Knotens Ω [rad]
double calculateMoonIlluminatedFraction(const Equatorial& sunEqu, double sunDistance,
                                        const Equatorial& moonEqu, double moonDistance);  // k [0..1]
MoonOrientation calculateMoonOrientation(double jd, const MoonPosition& moon);
MoonRiseSet calculateMoonRiseSet(double jd, double longitude, double latitude);
MoonApsis calculateNextMoonApsis(double jd, MoonApsisType type);
MoonNode calculateNextMoonNode(double jd);

// Matrix-Operationen
Mat3 createRotationMatrix(const Vec3& axle, double angle);
Mat3 multiplyMatrix(const Mat3& m1, const Mat3& m2);
Vec3 applyMatrix(const Vec3& point, const Mat3& matrix);

} // namespace astro

#endif // ASTRO_H
