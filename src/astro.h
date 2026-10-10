#ifndef ASTRO_H
#define ASTRO_H

/*
 * Astronomische Berechnungen nach Jean Meeus, "Astronomische Algorithmen",
 * 2. Auflage, Barth 1994 (Übersetzung von "Astronomical Algorithms", 1991).
 *
 * Konventionen:
 *  - Winkel als Angle (intern Radiant), Sternzeit als SiderealTime, Zeitpunkte als JulianDay.
 *  - Geographische Länge Ost positiv (abweichend vom Buch, dort West positiv).
 *  - Azimut von Süden über Westen gezählt (wie im Buch, Kapitel 12).
 *  - Zeiten in UT; die Dynamische Zeit TD wird mit UT gleichgesetzt (ΔT aus Kapitel 9 fehlt).
 *
 * Umgesetzt                                        Formeln/Tabellen          Funktion
 *  3  Interpolation                                3.3                       interpolate
 *  7  Julianischer Tag                             7.1                       JulianDay::fromCalendar
 * 10  Der Erdglobus (ρ sin φ′, ρ cos φ′)                                     calculateTopocentric
 * 11  Sternzeit in Greenwich                       11.1, 11.4                calculateGreenwichSiderealTime
 * 12  Umwandeln von Koordinaten                    12.3–12.6                 calculateEquatorial, calculateHorizontal
 * 13  Der parallaktische Winkel                    13.1                      calculateParallacticAngle
 * 14  Aufgang, Durchgang, Untergang (Sonne; h0)    14.1, 14.2                calculateSunRiseSet
 * 21  Schiefe der Ekliptik (ohne Nutation)         21.2                      calculateObliquity
 * 24  Koordinaten der Sonne (niedrige Genauigk.)   24.1–24.5                 calculateSunPosition, calculateEarthEccentricity
 * 26  Äquinoktien und Solstitien                   Tab. 26.2, 26.3           calculateEquinoxSolstice
 * 27  Zeitgleichung (ohne Nutation)                27.1, 27.2                calculateEquationOfTime
 * 29  Keplergleichung                              29.1, 29.5                calculateEarthTrueAnomaly
 * 37  Planeten in Perihel und Aphel (nur Erde)                               calculateNextEarthApsis
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
#include <cstddef>
#include <cstdint>
#include <ctime>

namespace astro {

// sin, cos und tan für double neben den Überladungen für Angle sichtbar halten
using std::sin;
using std::cos;
using std::tan;

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

constexpr size_t TIMEZONE_CAPACITY = 64;               // Länge eines POSIX-TZ-Strings einschließlich Nullbyte

// ---------------------------------------------------------------
//  Winkel, Sternzeit und Julianischer Tag
// ---------------------------------------------------------------

// Winkel. Intern in Radiant; die Einheit wird nur beim Erzeugen und beim Auslesen genannt.
class Angle {
public:
    constexpr Angle() = default;

    static constexpr Angle fromRad(const double rad) { return Angle(rad); }
    static constexpr Angle fromDeg(const double deg) { return Angle(deg * DEG2RAD); }
    static constexpr Angle fromHours(const double hours) { return Angle(hours * 15.0 * DEG2RAD); }    // 1 h = 15°
    static constexpr Angle fromArcsec(const double arcsec) { return Angle(arcsec / 3600.0 * DEG2RAD); }
    static constexpr Angle fromDms(const double deg, const double min, const double sec) {
        return fromDeg(deg + min / 60.0 + sec / 3600.0);
    }

    constexpr double rad() const { return rad_; }
    constexpr double deg() const { return rad_ * RAD2DEG; }
    constexpr double hours() const { return deg() / 15.0; }
    constexpr double arcsec() const { return deg() * 3600.0; }

    // Auf [0, 2π) bzw. (−π, π] gebracht
    Angle normalized() const {
        const double r = std::fmod(rad_, 2 * M_PI);
        return Angle(r < 0 ? r + 2 * M_PI : r);
    }
    Angle normalizedSigned() const {
        const double r = normalized().rad_;
        return Angle(r > M_PI ? r - 2 * M_PI : r);
    }
    // Betrag (als Methode, damit abs(int) und fabs(double) in astro nicht verdeckt werden).
    // fabs löscht nur das Vorzeichenbit; ein Vergleich wäre ohne FPU ein Bibliotheksaufruf.
    constexpr Angle abs() const { return Angle(std::fabs(rad_)); }

    // Umkehrfunktionen der Winkelfunktionen
    static Angle asin(const double x) { return Angle(std::asin(x)); }
    static Angle acos(const double x) { return Angle(std::acos(x)); }
    static Angle atan(const double x) { return Angle(std::atan(x)); }
    static Angle atan2(const double y, const double x) { return Angle(std::atan2(y, x)); }

    constexpr Angle operator-() const { return Angle(-rad_); }
    constexpr Angle& operator+=(const Angle b) { rad_ += b.rad_; return *this; }
    constexpr Angle& operator-=(const Angle b) { rad_ -= b.rad_; return *this; }
    friend constexpr Angle operator+(const Angle a, const Angle b) { return Angle(a.rad_ + b.rad_); }
    friend constexpr Angle operator-(const Angle a, const Angle b) { return Angle(a.rad_ - b.rad_); }
    friend constexpr Angle operator*(const Angle a, const double f) { return Angle(a.rad_ * f); }
    friend constexpr Angle operator*(const double f, const Angle a) { return Angle(f * a.rad_); }
    friend constexpr Angle operator/(const Angle a, const double f) { return Angle(a.rad_ / f); }
    friend constexpr double operator/(const Angle a, const Angle b) { return a.rad_ / b.rad_; }
    friend constexpr bool operator<(const Angle a, const Angle b) { return a.rad_ < b.rad_; }
    friend constexpr bool operator>(const Angle a, const Angle b) { return a.rad_ > b.rad_; }
    friend constexpr bool operator<=(const Angle a, const Angle b) { return a.rad_ <= b.rad_; }
    friend constexpr bool operator>=(const Angle a, const Angle b) { return a.rad_ >= b.rad_; }
    friend constexpr bool operator==(const Angle a, const Angle b) { return a.rad_ == b.rad_; }

private:
    constexpr explicit Angle(const double rad) : rad_(rad) {}
    double rad_ = 0.0;
};

inline double sin(const Angle a) { return std::sin(a.rad()); }
inline double cos(const Angle a) { return std::cos(a.rad()); }
inline double tan(const Angle a) { return std::tan(a.rad()); }

// Literale, z. B. -0.8333_deg; außerhalb von astro mit using namespace astro::literals
inline namespace literals {
constexpr Angle operator""_deg(const long double deg) { return Angle::fromDeg(static_cast<double>(deg)); }
constexpr Angle operator""_deg(const unsigned long long deg) { return Angle::fromDeg(static_cast<double>(deg)); }
constexpr Angle operator""_rad(const long double rad) { return Angle::fromRad(static_cast<double>(rad)); }
}

// Sternzeit: Stundenwinkel des Frühlingspunkts, auf [0, 2π) gebracht (Kapitel 11 und 12)
class SiderealTime {
public:
    constexpr SiderealTime() = default;
    explicit SiderealTime(const Angle angle) : angle_(angle.normalized()) {}

    constexpr Angle angle() const { return angle_; }
    constexpr double hours() const { return angle_.hours(); }

    // Lokale aus Greenwich-Sternzeit: θ = θ0 − L, im Buch mit L West positiv; hier Länge Ost positiv
    friend SiderealTime operator+(const SiderealTime theta0, const Angle eastLongitude) {
        return SiderealTime(theta0.angle_ + eastLongitude);
    }
    // Stundenwinkel H = θ − α
    friend constexpr Angle operator-(const SiderealTime theta, const Angle ra) { return theta.angle_ - ra; }

private:
    Angle angle_;
};

// Julianischer Tag (Kapitel 7); TD wird mit UT gleichgesetzt
class JulianDay {
public:
    constexpr JulianDay() = default;
    constexpr explicit JulianDay(const double days) : days_(days) {}

    static JulianDay fromCalendar(int year, int month, int day, double hours);   // Formel 7.1
    static JulianDay fromTm(const struct tm& utc);
    static constexpr JulianDay fromUnixTime(const time_t t) { return JulianDay(t / 86400.0 + 2440587.5); }

    constexpr double value() const { return days_; }                            // [d]
    time_t toUnixTime() const { return (time_t) std::llround((days_ - 2440587.5) * 86400.0); }

    constexpr double centuries() const { return (days_ - 2451545.0) / 36525.0; }  // T seit J2000.0, Formel 11.1
    constexpr double millennia() const { return (days_ - 2451545.0) / 365250.0; } // τ seit J2000.0, Formel 27.2
    JulianDay midnight() const { return JulianDay(std::round(days_) - 0.5); }     // 0h UT desselben UT-Tags

    friend constexpr JulianDay operator+(const JulianDay jd, const double days) { return JulianDay(jd.days_ + days); }
    friend constexpr JulianDay operator-(const JulianDay jd, const double days) { return JulianDay(jd.days_ - days); }
    friend constexpr double operator-(const JulianDay a, const JulianDay b) { return a.days_ - b.days_; }   // [d]
    friend constexpr bool operator<(const JulianDay a, const JulianDay b) { return a.days_ < b.days_; }
    friend constexpr bool operator<=(const JulianDay a, const JulianDay b) { return a.days_ <= b.days_; }
    friend constexpr bool operator>(const JulianDay a, const JulianDay b) { return a.days_ > b.days_; }
    friend constexpr bool operator>=(const JulianDay a, const JulianDay b) { return a.days_ >= b.days_; }

private:
    double days_ = 0.0;
};

// ---------------------------------------------------------------
//  Datenstrukturen
// ---------------------------------------------------------------

// Beobachtungsort auf der Erde
struct GeoLocation {
    Angle longitude;                    // Geographische Länge, Ost positiv
    Angle latitude;                     // Geographische Breite φ
    double elevation;                   // Höhe über dem Meeresspiegel H [m], Kapitel 10
    char timezone[TIMEZONE_CAPACITY];   // Zeitzone als POSIX-TZ-String, z. B. "CET-1CEST,M3.5.0,M10.5.0/3";
                                        // nur für die Ausgabe in Lokalzeit, die Berechnungen laufen in UT
};

// Ekliptikale Koordinaten, bezogen auf das mittlere Äquinoktium des Datums
struct Ecliptic {
    Angle longitude;        // Ekliptikale Länge λ
    Angle latitude;         // Ekliptikale Breite β
    double distance;        // Entfernung vom Bezugspunkt (geozentrisch: von der Erde) [km]
};

// Äquatoriale Koordinaten
struct Equatorial {
    Angle ra;               // Rektaszension α
    Angle dec;              // Deklination δ
};

// Horizontale Koordinaten
struct Horizontal {
    Angle azimuth;          // Azimut A, von Süden über Westen gezählt
    Angle altitude;         // Höhe h
};

// Selenografische Koordinaten (Kapitel 51), Länge positiv in Richtung Mare Crisium
struct Selenographic {
    Angle longitude;        // Selenografische Länge, (−π, π]
    Angle latitude;         // Selenografische Breite
};

// Orientierung des Mondes (Kapitel 51)
struct MoonOrientation {
    Selenographic libration;    // Libration = selenografische Koordinaten der Erde
    Angle positionAngle;        // Positionswinkel der Mondachse P
};

// Aufgang, Meridiandurchgang, Untergang und höchster Stand eines Himmelskörpers an einem UT-Tag.
// Ohne Auf- und Untergang steht der Körper den ganzen Tag über (maxAltitude > 0) oder unter dem Horizont.
struct RiseTransitSet {
    bool   hasRise;         // Aufgang an diesem UT-Tag
    bool   hasSet;          // Untergang an diesem UT-Tag
    bool   hasTransit;      // Meridiandurchgang (Stundenwinkel 0) an diesem UT-Tag
    double rise;            // Aufgang [h UT]
    double set;             // Untergang [h UT]
    double transit;         // Meridiandurchgang [h UT]
    Angle  maxAltitude;     // Höchste Höhe über der Standardhöhe h0
    double maxAltitudeTime; // Zeitpunkt der höchsten Höhe [h UT]
};

// Apsiden einer Bahn: Perigäum/Apogäum (Mond, Kapitel 48), Perihel/Aphel (Planeten, Kapitel 37)
enum class ApsisType { Periapsis, Apoapsis };
struct Apsis {
    double jde;             // Zeitpunkt [JDE]; TD wird mit UT gleichgesetzt
    double distance;        // Entfernung vom Zentralkörper [km]
};

// Durchgang durch einen Knoten der Bahn (Mond: Kapitel 49, Planeten: Kapitel 38)
struct NodePassage {
    double jde;             // Zeitpunkt [JDE]; TD wird mit UT gleichgesetzt
    bool ascending;         // true: aufsteigender Knoten (Breite wird positiv)
};

// Solstitien und Äquinoktien (Kapitel 26)
enum class EquinoxType { Spring, Summer, Autumn, Winter };

// Vektor und 3x3-Matrix für die Drehung der Mondtextur
struct Vec3 {
    double x, y, z;
};

struct Mat3 {
    double m[3][3];
};

// ---------------------------------------------------------------
//  Funktionen (Formeln siehe Kommentare in astro.cpp)
// ---------------------------------------------------------------

// Sternzeit
SiderealTime calculateGreenwichSiderealTime(JulianDay jd);                      // Mittlere Sternzeit θ0
SiderealTime calculateLocalSiderealTime(JulianDay jd, const GeoLocation& location);  // Lokale Sternzeit θ

// Sonne und Erdbahn
Angle calculateObliquity(JulianDay jd);                 // Mittlere Schiefe der Ekliptik ε0
Ecliptic calculateSunPosition(JulianDay jd);            // Geozentrische Position der Sonne
Angle calculateEarthTrueAnomaly(JulianDay jd);          // Wahre Anomalie der Erde ν
double calculateEarthEccentricity(JulianDay jd);        // Exzentrizität der Erdbahn [1]
double calculateEquationOfTime(JulianDay jd, bool withEarthTilt);  // Zeitgleichung [min]

// Koordinatentransformation; vom Beobachtungsort wird nur die Breite (Topozentrisch: und die Höhe)
// benötigt, die Länge steckt in der lokalen Sternzeit
Equatorial calculateEquatorial(const Ecliptic& ecliptic, Angle eps);
Horizontal calculateHorizontal(const Equatorial& equ, SiderealTime localSiderealTime, const GeoLocation& location);
Angle calculateParallacticAngle(const Equatorial& equ, SiderealTime localSiderealTime, const GeoLocation& location);
Equatorial calculateTopocentric(const Equatorial& equ, Angle parallax, SiderealTime localSiderealTime,
                                const GeoLocation& location);

// Interpolation
double interpolate(double y1, double y2, double y3, double n);

// Auf-/Untergang
RiseTransitSet calculateSunRiseSet(JulianDay jd, const GeoLocation& location);
RiseTransitSet calculateMoonRiseSet(JulianDay jd, const GeoLocation& location);

// Solstitien und Äquinoktien [JDE], Perihel/Aphel der Erde
double calculateEquinoxSolstice(int year, EquinoxType type);
Apsis calculateNextEarthApsis(JulianDay jd, ApsisType type);

// Mond
Ecliptic calculateMoonPosition(JulianDay jd);           // Geozentrische Position des Mondes
Angle calculateMoonAscendingNode(JulianDay jd);         // Länge des aufsteigenden Knotens Ω
double calculateMoonIlluminatedFraction(const Equatorial& sunEqu, double sunDistance,
                                        const Equatorial& moonEqu, double moonDistance);  // k [0..1]
MoonOrientation calculateMoonOrientation(JulianDay jd, const Ecliptic& moon);
Apsis calculateNextMoonApsis(JulianDay jd, ApsisType type);
NodePassage calculateNextMoonNode(JulianDay jd);

// Matrix-Operationen
Mat3 createRotationMatrix(const Vec3& axle, Angle angle);
Mat3 operator*(const Mat3& m1, const Mat3& m2);         // Matrixprodukt m1·m2
Vec3 operator*(const Mat3& matrix, const Vec3& point);  // Matrix angewandt auf einen Vektor

} // namespace astro

#endif // ASTRO_H
