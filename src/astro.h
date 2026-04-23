#ifndef ASTRO_H
#define ASTRO_H

#include <cmath>
#include <cstdint>

namespace astro {

// Konstanten
constexpr double AE_TO_KM = 149597870.7;
constexpr double ECCENTRICITY = 0.016708634;    // Exzentrizität der Erdbahn
constexpr double SEMI_MAJOR_AXIS = 1.000001018; // Halbe große Achse in AE
constexpr double EPSILON = 23.439292;           // Mittlere Neigung Erdachse
constexpr double EARTH_RADIUS = 6378.137;       // Erdradius in km
constexpr double MOON_RADIUS = 1738.1;          // Mondradius in km
constexpr double MOON_ORBIT_RADIUS = 384400.0;  // Durchschnittlicher Abstand Erde-Mond in km

constexpr double DEG2RAD = M_PI / 180.0;
constexpr double RAD2DEG = 180.0 / M_PI;

// Datenstrukturen

struct RaDek {
    double ra;
    double dek;
};

struct AzimutHeight {
    double azimut;
    double height;
};

struct MoonPosition {
    double longitude;
    double latitude;
    double distance;
};

struct Libration {
    double longitude;
    double latitude;
};

struct MoonAxle {
    Libration libration;
    double axle;
};

struct SunriseSunset {
    double zenit;
    double rising;
    double setting;
    bool valid;
};

struct Vec3 {
    double x, y, z;
};

// 3x3 Matrix als Array
struct Mat3 {
    double m[3][3];
};

struct MoonPhasePixel {
    uint8_t r, g, b, a;
};

// Winkel-Normalisierung
double normalizeAngleDegree(double angle);
double normalizeAngleDifferenceRad(double angle);

// Julianisches Datum
double calculateJulianDate(int year, int month, int day,
                           int hour = 0, int minute = 0,
                           int second = 0, int millisecond = 0);
double calculateJulianEpoch(double jd);

// Sternzeit
double calculateSiderealTime(double jd);

// Sonnenposition
double calculateEclipticalLength(double jd);
double calculateTrueAnomaly(double jd);
double calculateOrbitRadiusEarth(double nu);
double calculateCurrentExcentric(double jd);
double calculateEquationOfTime(double jd, bool withEarthTilt = true);

// Koordinatentransformation
RaDek calculateRaDek(double eclipticalLongitude, double eclipticalLatitude);
AzimutHeight calculateHAzFromRaDek(const RaDek& radek, double siderealTime, double latitude);
double calculateParallacticAngle(const RaDek& radek, double siderealTime, double latitude);
RaDek calculateParallax(const RaDek& objRaDek, double parallax, double direction, double latitude);

// Interpolation
double interpolate(double y1, double y2, double y3, double n);

// Sonnenauf-/untergang
SunriseSunset calculateSunriseSunset(double jd, double latitude, double longitude);

// Solstitien und Äquinoktien
enum class EquinoxType { Spring, Summer, Autumn, Winter };
double calculateJDOfPoint(int year, EquinoxType type);

// Perihel/Aphel
enum class PerihelType { Perihel, Aphel };
double calculatePerihelAphel(int year, int month, PerihelType type);

// Mond
MoonPosition calculateMoon(double jd);
double calculateRisingKnotMoon(double jd);
double calculateMoonPhase(const RaDek& sunRaDek, double sunDistance,
                          const RaDek& moonRaDek, double moonDistance);
MoonAxle calculateMoonAxle(double jd, const MoonPosition& moon);

// Matrix-Operationen
Mat3 createRotationMatrix(const Vec3& axle, double angle);
Mat3 multiplyMatrix(const Mat3& m1, const Mat3& m2);
Vec3 applyMatrix(const Vec3& point, const Mat3& matrix);

// Mondphasen-Rendering
// Rendert die Mondphase in einen RGBA-Pixel-Buffer der Größe (2*radius) x (2*radius).
// moonTexture: optionaler Pointer auf Textur (RGBA, quadratisch, texSize x texSize), oder nullptr.
// Gibt die Anzahl der beleuchteten Pixel zurück.
int drawMoonPhase(const RaDek& sunRaDek, const RaDek& moonRaDek,
                  double moonAxleAngle, const Libration& libration,
                  double siderealTime, double latitude,
                  int radius,
                  MoonPhasePixel* outputBuffer,
                  const MoonPhasePixel* moonTexture = nullptr,
                  int texSize = 0);

} // namespace astro

#endif // ASTRO_H
