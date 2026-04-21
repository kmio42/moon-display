#include "astro.h"
#include <cstring>

namespace astro {

// ---------------------------------------------------------------
//  Winkel-Normalisierung
// ---------------------------------------------------------------

/**
 * Normalisiert einen Winkel in Grad auf den Bereich [0, 360).
 * @param angle Winkel in Grad
 * @return Normalisierter Winkel [0, 360) in Grad
 */
double normalizeAngleDegree(double angle) {
    angle = fmod(angle, 360.0);
    if (angle < 0) angle += 360.0;
    return angle;
}

/**
 * Normalisiert einen Winkel in Bogenmaß auf den Bereich (-π, π].
 * @param angle Winkel in Bogenmaß
 * @return Normalisierter Winkel (-π, π] in Bogenmaß
 */
double normalizeAngleDifferenceRad(double angle) {
    double normalized = fmod(angle, 2 * M_PI);
    if (normalized < 0) normalized += 2 * M_PI;
    if (normalized > M_PI) normalized -= 2 * M_PI;
    return normalized;
}

// ---------------------------------------------------------------
//  Julianisches Datum
// ---------------------------------------------------------------

/**
 * Berechnet das Julianische Datum für ein gegebenes Datum und Uhrzeit.
 * Formeln: 7.1 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
 * Julianischer Tag (JD) fortlaufende Zählung der Tage seit 1. Januar 4712 v. Chr. um 12:00 Uhr UT
 * Julianischer Ephemeris-Tag (JDE) fortlaufende Zählung der Tage seit 1. Januar 4712 v. Chr. um 12:00 Uhr ET
 * @param year Jahr
 * @param month Monat (1-12)
 * @param day Tag (1-31)
 * @param hour Stunde (0-23)
 * @param minute Minute (0-59)
 * @param second Sekunde (0-59)
 * @param millisecond Millisekunde (0-999)
 * @return Julianisches Datum als double
 * 
 * @note Formel 7.1 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus
 */
double calculateJulianDate(int year, int month, int day,
                           int hour, int minute, int second, int millisecond) {
    if (month <= 2) {
        year -= 1;
        month += 12;
    }

    int A = year / 100;

    // Gregorianischer Kalender
    int B = 2 - A + A / 4;

    // Julianischer Kalender
    if ((year < 1582) ||
        (year == 1582 && month < 10) ||
        (year == 1582 && month == 10 && day <= 4)) {
        B = 0;
    }

    // Julianisches Datum (Formel 7.1)
    double jd = static_cast<int>(365.25 * (year + 4716))
              + static_cast<int>(30.6001 * (month + 1))
              + day + B - 1524.5
              + (hour + minute / 60.0 + second / 3600.0 + millisecond / 3600000.0) / 24.0;

    return jd;
}

/**
 * Berechnet das Julianische Jahrhundert seit J2000.0.
 * Formel: 
 *  - 11.1 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
 *  - 21.1 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
 * @param jd Julianisches Datum
 * @return Julianisches Jahrhundert
 */
double calculateJulianEpoch(double jd) {
    return (jd - 2451545.0) / 36525.0;
}

// ---------------------------------------------------------------
//  Sternzeit
// ---------------------------------------------------------------

/**
 * Berechnet die Greenwich Mean Sidereal Time (GMST) in Stunden für ein gegebenes Julianisches Datum.
 * Formeln: 11.4 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
 * @return GMST in Stunden, normalisiert auf den Bereich [0, 24)
 */
double calculateSiderealTime(double jd) {

    double T = calculateJulianEpoch(jd);

    // Greenwich Mean Sidereal Time in Grad (Formel 11.4)
    double gmstDeg = 280.46061837 + 360.98564736629 * (jd - 2451545.0)
                   + T * T * (0.000387933 - T / 38710000.0);
    // In Stunden, normalisiert 0–24
    double gmst = fmod(gmstDeg / 15.0, 24.0);
    if (gmst < 0) gmst += 24.0;
    return gmst;
}

// ---------------------------------------------------------------
//  Sonnenposition
// ---------------------------------------------------------------

inline double gradToRad(double grad, double minute = 0, double second = 0 ) {
    return (grad + minute / 60.0 + second / 3600.0) * DEG2RAD;
}
    // Formel 21.2 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    // gradToRad(23,26,21.448) - T * (gradToRad(0,0,46.8150) + T * (gradToRad(0,0,0.00059) - T * gradToRad(0,0,0.001813)));

/**
 * Berechnet die aktuelle Exzentrizität der Erdbahn für ein gegebenes Julianisches Datum.
 *
 * @param jd Julianisches Datum
 * @return Exzentrizität der Erdbahn
 * 
 * @note Formel 24.4 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
 */
double calculateCurrentExcentric(double jd) {
    double T = calculateJulianEpoch(jd);
    return 0.016708617 - T * (0.000042037 + 0.0000001236 * T);
}

/**
 * Berechnet die ekliptikale Länge der Sonne für ein gegebenes Julianisches Datum.
 * Wahre Länge der Sonne bezogen auf mittleres Äquinoktium des Datums
 * Formeln: 24.3 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
 * @param jd Julianisches Datum
 * @return Ekliptikale Länge in Bogenmaß
 * 
 * @note Formel 24.3 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
 */
double calculateEclipticalLength(double jd) {
    double T = calculateJulianEpoch(jd);

    // Mittlere Anomalie der Sonne (Formel 24.3)
    double M = (357.52910 + T * (35999.05030 - 0.0001559 * T) - T * T * T * 0.00000048) * DEG2RAD;
    
    // Mittlere Länge der Sonne bezogen auf mittleres Äquinoktium des Datums (Formel 24.3)
    double L0 = normalizeAngleDegree(280.46644567 + T * (36000.76982779 + T * 0.0003032028));

    // Mittelpunktsgleichung - Alternative zur Lösung der Kepplergleichung
    double C = (1.914600 - T * (0.004817 + 0.000014 * T)) * sin(M)
             + (0.019993 - 0.000101 * T) * sin(2 * M)
             + 0.000290 * sin(3 * M);

    return (L0 + C) * DEG2RAD;
}

/**
 * Berechnet die wahre Anomalie der Erde für ein gegebenes Julianisches Datum.
 * 
 * @param jd Julianisches Datum
 * @return Wahre Anomalie in Bogenmaß
 */
double calculateTrueAnomaly(double jd) {
    double T = calculateJulianEpoch(jd);

    // Mittlere Anomalie der Sonne (Formel 24.3)
    double M = (357.52910 + T * (35999.05030 - T * (0.0001559 + T * 0.00000048))) * DEG2RAD;

    double e_current = calculateCurrentExcentric(jd);

    // Iterative Lösung der Keplergleichung
    double E = M;
    for (int i = 0; i < 10; i++) {
        E = M + e_current * sin(E);
    }

    // Berechnung der wahren Anomalie (ν) in Bogenmaß
    // umgestellte Formel 29.1 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    double nu = 2.0 * atan2(sqrt(1 + e_current) * sin(E / 2),
                             sqrt(1 - e_current) * cos(E / 2));
    return nu;
}

/**
 * Berechnet den Abstand Erde-Sonne für eine gegebene wahre Anomalie.
 * @param nu Wahre Anomalie in Bogenmaß
 * @return Abstand in AU
 */
double calculateOrbitRadiusEarth(double nu) {
    return SEMI_MAJOR_AXIS * (1 - ECCENTRICITY * ECCENTRICITY) / (1 + ECCENTRICITY * cos(nu));
}

/**
 * Berechnet die Zeitgleichung für ein gegebenes Julianisches Datum.
 * @param jd Julianisches Datum
 * @param withEarthTilt Ob die Neigung der Ekliptik berücksichtigt werden soll
 * @return Zeitgleichung in Minuten
 */
double calculateEquationOfTime(double jd, bool withEarthTilt) {
    double T = calculateJulianEpoch(jd);

    // Mittlere Anomalie der Sonne (Formel 24.3)
    double M = (357.52910 + T * (35999.05030 - 0.0001559 * T) - T * T * T * 0.00000048) * DEG2RAD;
    
    // Mittlere Länge der Sonne bezogen auf mittleres Äquinoktium des Datums (Formel 24.3)
    double L0 = normalizeAngleDegree(280.46644567 + T * (36000.76982779 + T * 0.0003032028));

    // Mittelpunktsgleichung
    double C = (1.914600 - T * (0.004817 + 0.000014 * T)) * sin(M)
             + (0.019993 - 0.000101 * T) * sin(2 * M)
             + 0.000290 * sin(3 * M);

    // Wahre Länge der Sonne bezogen auf mittleres Äquinoktium des Datums
    double L = L0 + C;

    double eps = 0;
    if (withEarthTilt) {
        // Schiefe der Ekliptik (Formel 21.2) - Achtung, nicht für zu große T ()
        eps = 23.439292 - 0.013004167 * T - 0.0000001639 * T * T + 0.0000005036 * T * T * T;
    }

    // Rektaszension der Sonne berechnen
    double alpha = atan2(cos(eps * DEG2RAD) * sin(L * DEG2RAD), cos(L * DEG2RAD)) * RAD2DEG;
    alpha = normalizeAngleDegree(alpha);

    // Zeitgleichung (Formel 27.1) ohne Nutation mit Umrechnung in Minuten
    double E = (L0 - 0.0057183 - alpha) * 4; // Minuten

    /*
    * Der Bereich E muss zwischen -20 Minuten und 20 Minuten liegen
    * Vielfache von 24 Stunden können addiert oder subtrahiert werden
    */
    if (E > 60 * 12) E -= 24 * 60;
    else if (E < -60 * 12) E += 24 * 60;

    return E;
}

// ---------------------------------------------------------------
//  Koordinatentransformation
// ---------------------------------------------------------------

/**
 * Transformiert ekliptikale Koordinaten in äquatoriale Koordinaten (RA, Dek).
 * @param eclipticalLongitude Ekliptikale Länge in Bogenmaß
 * @param eclipticalLatitude Ekliptikale Breite in Bogenmaß
 * @return Äquatoriale Koordinaten (RA in Bogenmaß, Dek in Bogenmaß, Distanz 0)
 */
RaDek calculateRaDek(double eclipticalLongitude, double eclipticalLatitude) {
    // Formel 12.3 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    double ra = atan2(
        cos(EPSILON * DEG2RAD) * sin(eclipticalLongitude) - tan(eclipticalLatitude) * sin(EPSILON * DEG2RAD),
        cos(eclipticalLongitude));
    // Formel 12.4 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    double dek = asin(
        sin(eclipticalLatitude) * cos(EPSILON * DEG2RAD)
      + cos(eclipticalLatitude) * sin(EPSILON * DEG2RAD) * sin(eclipticalLongitude));
    return {ra, dek, 0.0};
}

/**
 * Transformiert äquatoriale Koordinaten in horizontale Koordinaten (Azimut, Höhe).
 * @param radek Äquatoriale Koordinaten
 * @param siderealTime Sternzeit in Stunden
 * @param latitude Breitengrad in Grad
 * @return Horizontale Koordinaten (Azimut in Bogenmaß, Höhe in Bogenmaß)
 */
AzimutHeight calculateHAzFromRaDek(const RaDek& radek, double siderealTime, double latitude) {
    double H = siderealTime - radek.ra;
    // Formel 12.5 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    double A = atan2(sin(H),
                     cos(H) * sin(latitude * DEG2RAD)
                   - tan(radek.dek) * cos(latitude * DEG2RAD));
    
    // Formel 12.6 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    double h = asin(sin(latitude * DEG2RAD) * sin(radek.dek)
                  + cos(latitude * DEG2RAD) * cos(radek.dek) * cos(H));
    return {A, h};
}

/**
 * Berechnet den parallaktischen Winkel für ein Objekt.
 * @param radek Äquatoriale Koordinaten des Objekts
 * @param siderealTime Sternzeit in Stunden
 * @param latitude Breitengrad in Grad
 * @return Parallaktischer Winkel in Bogenmaß
 * 
 * @note Formel 13.1 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
 */
double calculateParallacticAngle(const RaDek& radek, double siderealTime, double latitude) {
    return atan2(sin(siderealTime - radek.ra),
                 tan(latitude * DEG2RAD) * cos(radek.dek) - sin(radek.dek) * cos(siderealTime - radek.ra));
}

/**
 * Berechnet die Parallaxe-Korrektur für äquatoriale Koordinaten.
 * @param objRaDek Äquatoriale Koordinaten des Objekts
 * @param parallax Parallaxe in Bogenmaß
 * @param direction Richtung der Parallaxe
 * @param latitude Breitengrad in Grad
 * @return Korrigierte äquatoriale Koordinaten
 */
RaDek calculateParallax(const RaDek& objRaDek, double parallax, double direction, double latitude) {
    
    // Kapitel 10 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    constexpr double ba = 0.99664719;
    constexpr double H = 0; // Höhe über Meeresspiegel

    double u = atan(ba * tan(latitude * DEG2RAD));
    double rhoSinPhi = ba * sin(u) + H / 6378140.0 * sin(latitude * DEG2RAD);
    double rhoCosPhi = cos(u) + H / 6378140.0 * cos(latitude * DEG2RAD);

    // Formel 39.2 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    double alpha = atan(-rhoCosPhi * sin(parallax) * sin(direction - objRaDek.ra)
                       / (cos(objRaDek.dek) - rhoCosPhi * sin(parallax) * cos(direction - objRaDek.ra)));

    // Formel 39.3 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    double dek = atan2(
        (sin(objRaDek.dek) - rhoSinPhi * sin(parallax)) * cos(alpha),
         cos(objRaDek.dek) - rhoCosPhi * sin(parallax) * cos(direction - objRaDek.ra));

    return {objRaDek.ra + alpha, dek, 0.0};
}

// ---------------------------------------------------------------
//  Interpolation
// ---------------------------------------------------------------

/**
 * Interpoliert einen Wert zwischen drei Punkten.
 * @param y1 Erster Wert
 * @param y2 Zweiter Wert
 * @param y3 Dritter Wert
 * @param n Interpolationsfaktor
 * @return Interpolierter Wert
 */
double interpolate(double y1, double y2, double y3, double n) {
    // Formel 3.1 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    double a = y2 - y1;
    double b = y3 - y2;
    double c = b - a;
    // Formel 3.3 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    return y2 + n / 2.0 * (a + b + n * c);
}

// ---------------------------------------------------------------
//  Sonnenauf-/untergang
// ---------------------------------------------------------------

/**
 * Berechnet Sonnenauf- und -untergang für ein gegebenes Datum und Ort.
 * @param jd Julianisches Datum
 * @param latitude Breitengrad in Grad
 * @param longitude Längengrad in Grad
 * @return Struktur mit Aufgang, Untergang und Gültigkeit
 */
SunriseSunset calculateSunriseSunset(double jd, double latitude, double longitude) {
    constexpr double h0 = -0.8333;

    jd = round(jd) - 0.5; // Mitternacht

    double length2 = calculateEclipticalLength(jd);
    RaDek radek2 = calculateRaDek(length2, 0);
    double siderealTime = calculateSiderealTime(jd) * 180.0 / 12.0;

    // Formel 14.1 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    double cH0 = (sin(h0 * DEG2RAD) - sin(latitude * DEG2RAD) * sin(radek2.dek))
               / (cos(latitude * DEG2RAD) * cos(radek2.dek));

    if (cH0 < -1 || cH0 > 1) {
        return {0, 0, 0, false};
    }

    double H0 = acos(cH0) * RAD2DEG;

    // Formel 14.2 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    double m0 = fmod((radek2.ra * RAD2DEG - longitude - siderealTime) / 360.0, 1.0);
    m0 = fmod(m0 + 1.0, 1.0);

    double m1 = fmod(m0 - H0 / 360.0 + 1.0, 1.0);
    double m2 = fmod(m0 + H0 / 360.0 + 1.0, 1.0);

    return {m0 * 24.0, m1 * 24.0, m2 * 24.0, true};
}

// ---------------------------------------------------------------
//  Solstitien und Äquinoktien
// ---------------------------------------------------------------

/**
 * Berechnet das Julianische Datum für Solstitien oder Äquinoktien in einem Jahr.
 * @param year Jahr
 * @param type Typ des Punktes (Frühling, Sommer, Herbst, Winter)
 * @return Julianisches Datum des Ereignisses
 */
double calculateJDOfPoint(int year, EquinoxType type) {

    //Kapitel 26 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    
    //Tabelle 26.3 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    static const double factors[][3] = {
        {485, 324.96,   1934.136},
        {203, 337.23,  32964.467},
        {199, 342.08,     20.186},
        {182,  27.85, 445267.112},
        {156,  73.14,  45036.886},
        {136, 171.52,  22518.443},
        { 77, 222.54,  65928.934},
        { 74, 296.72,   3034.906},
        { 70, 243.58,   9037.513},
        { 58, 119.81,  33718.147},
        { 52, 297.17,    150.678},
        { 50,  21.02,   2281.226},
        { 45, 247.54,  29929.562},
        { 44, 325.15,  31555.956},
        { 29,  60.93,   4443.417},
        { 18, 155.12,  67555.328},
        { 17, 288.79,   4562.452},
        { 16, 198.04,  62894.029},
        { 14, 199.76,  31436.921},
        { 12,  95.39,  14577.848},
        { 12, 287.11,  31931.756},
        { 12, 320.81,  34777.259},
        {  9, 227.73,   1222.114},
        {  8,  15.45,  16859.074},
    };

    // Tabelle 26.2 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    // Die Werte für die Jahre 1000–3000 wurden hier auf 2000 zentriert, um die Genauigkeit zu verbessern.
    static const double parameters[4][5] = {
        {2451623.80984, 365242.37404,  0.05169, -0.00411, -0.00057}, // Frühling
        {2451716.56767, 365241.62603,  0.00325, 0.00888, -0.00030}, // Sommer
        {2451810.21715, 365242.01767,  -0.11575, 0.00337, 0.00078}, // Herbst
        {2451900.05952, 365242.74049,  -0.06223, -0.00823, 0.00032}, // Winter
    };

    int idx = static_cast<int>(type);
    double y = (year - 2000) / 1000.0;

    double JDE0 = parameters[idx][0] +
                y * ( parameters[idx][1] +
                y * ( parameters[idx][2] +
                y * ( parameters[idx][3] +
                y * ( parameters[idx][4] ))));

    double T = calculateJulianEpoch(JDE0);
    double W = 35999.373 * T - 2.47;
    double lambda = 1 + 0.0334 * cos(W * DEG2RAD) + 0.0007 * cos(2 * W * DEG2RAD);

    double S = 0;
    for (int i = 0; i < 24; i++) {
        S += factors[i][0] * cos((factors[i][1] + factors[i][2] * T) * DEG2RAD);
    }

    return JDE0 + 0.00001 * S / lambda;
}

// ---------------------------------------------------------------
//  Perihel / Aphel
// ---------------------------------------------------------------

/**
 * Berechnet das Julianische Datum für Perihel oder Aphel in einem Jahr.
 * @param year Jahr
 * @param month Monat
 * @param type Typ (Perihel oder Aphel)
 * @return Julianisches Datum des Ereignisses
 */
double calculatePerihelAphel(int year, int month, PerihelType type) {
    double y = year + month / 12.0;

    double k = 0.99997 * (y - 2000.01);

    static const double correctionFactorsA[5][2] = {
        {328.41, 132.788585},
        {316.13, 584.903153},
        {346.20, 450.380738},
        {136.95, 659.306737},
        {249.52, 329.653368}
    };

    static const double correctionPerihel[5] = { 1.278, -0.055, -0.091, -0.056, -0.045};
    static const double correctionAphel[5]   = {-1.352,  0.061,  0.062,  0.029,  0.031};

    if (k < 0) k = ceil(k);
    else        k = floor(k);

    if (type == PerihelType::Aphel) k += 0.5;

    const double* cf = (type == PerihelType::Perihel) ? correctionPerihel : correctionAphel;

    double correction = 0;
    for (int i = 0; i < 5; i++) {
        correction += cf[i] * sin((correctionFactorsA[i][0] + correctionFactorsA[i][1] * k) * DEG2RAD);
    }

    double JDE = 2451547.507 + k * (365.2596358 + 0.0000000158 * k);
    return JDE + correction;
}

// ---------------------------------------------------------------
//  Mond
// ---------------------------------------------------------------

/**
 * Berechnet die Position des Mondes für ein gegebenes Julianisches Datum.
 * @param jd Julianisches Datum
 * @return Mondposition (Länge, Breite, Distanz)
 */
MoonPosition calculateMoon(double jd) {
    double T = calculateJulianEpoch(jd);

    double l = normalizeAngleDegree(218.3164591 + T * (481267.88134236 - 0.0013268 * T)
             + T * T * T / 538841.0 - T * T * T * T / 65194000.0) * DEG2RAD;
    double D = normalizeAngleDegree(297.8502042 + T * (445267.1115168 - T * 0.0016300)
             + T * T * T / 545868.0 - T * T * T * T / 113065000.0) * DEG2RAD;
    double M = normalizeAngleDegree(357.5291092 + T * (35999.0502909 + T * (-0.0001536 + T / 24490000.0))) * DEG2RAD;
    double m = normalizeAngleDegree(134.9634114 + T * (477198.8676313 + 0.0089970 * T)
             + T * T * T / 69699.0 - T * T * T * T / 14712000.0) * DEG2RAD;
    double F = normalizeAngleDegree(93.2720993 + T * (483202.0175273 - 0.0034029 * T)
             - T * T * T / 3526000.0 + T * T * T * T / 863310000.0) * DEG2RAD;

    double A1 = normalizeAngleDegree(119.75 + 131.849 * T) * DEG2RAD;
    double A2 = normalizeAngleDegree(53.09 + 479264.290 * T) * DEG2RAD;
    double A3 = normalizeAngleDegree(313.45 + 481266.484 * T) * DEG2RAD;

    double E = 1 + T * (-0.002516 - 0.0000074 * T);

    // Tabelle 45.1: D, M, m, F, suml, sumr
    static const double ta[][6] = {
        {0, 0, 1, 0, 6288774, -20905355},
        {2, 0, -1, 0, 1274027, -3699111},
        {2, 0, 0, 0, 658314, -2955968},
        {0, 0, 2, 0, 213618, -569925},
        {0, 1, 0, 0, -185116, 48888},
        {0, 0, 0, 2, -114332, -3149},
        {2, 0, -2, 0, 58793, 246158},
        {2, -1, -1, 0, 57066, -152138},
        {2, 0, 1, 0, 53322, -170733},
        {2, -1, 0, 0, 45758, -204586},
        {0, 1, -1, 0, -40923, -129620},
        {1, 0, 0, 0, -34720, 108743},
        {0, 1, 1, 0, -30383, 104755},
        {2, 0, 0, -2, 15327, 10321},
        {0, 0, 1, 2, -12528, 0},
        {0, 0, 1, -2, 10980, 79661},
        {4, 0, -1, 0, 10675, -34782},
        {0, 0, 3, 0, 10034, -23210},
        {4, 0, -2, 0, 8548, -21636},
        {2, 1, -1, 0, -7888, 24208},
        {2, 1, 0, 0, -6766, 30824},
        {1, 0, -1, 0, -5163, -8379},
        {1, 1, 0, 0, 4987, -16675},
        {2, -1, 1, 0, 4036, -12831},
        {2, 0, 2, 0, 3994, -10445},
        {4, 0, 0, 0, 3861, -11650},
        {2, 0, -3, 0, 3665, 14403},
        {0, 1, -2, 0, -2689, -7003},
        {2, 0, -1, 2, -2602, 0},
        {2, -1, -2, 0, 2390, 10056},
        {1, 0, 1, 0, -2348, 6322},
        {2, -2, 0, 0, 2236, -9884},
        {0, 1, 2, 0, -2120, 5751},
        {0, 2, 0, 0, -2069, 0},
        {2, -2, -1, 0, 2048, -4950},
        {2, 0, 1, -2, -1773, 4130},
        {2, 0, 0, 2, -1595, 0},
        {4, -1, -1, 0, 1215, -3958},
        {0, 0, 2, 2, -1110, 0},
        {3, 0, -1, 0, -892, 3258},
        {2, 1, 1, 0, -810, 2616},
        {4, -1, -2, 0, 759, -1897},
        {0, 2, -1, 0, -713, -2117},
        {2, 2, -1, 0, -700, 2354},
        {2, 1, -2, 0, 691, 0},
        {2, -1, 0, -2, 596, 0},
        {4, 0, 1, 0, 549, -1423},
        {0, 0, 4, 0, 537, -1117},
        {4, -1, 0, 0, 520, -1571},
        {1, 0, -2, 0, -487, -1739},
        {2, 1, 0, -2, -399, 0},
        {0, 0, 2, -2, -381, -4421},
        {1, 1, 1, 0, 351, 0},
        {3, 0, -2, 0, -340, 0},
        {4, 0, -3, 0, 330, 0},
        {2, -1, 2, 0, 327, 0},
        {0, 2, 1, 0, -323, 1165},
        {1, 1, -1, 0, 299, 0},
        {2, 0, 3, 0, 294, 0},
        {2, 0, -1, -2, 0, 8752}
    };

    // Tabelle 45.2: D, M, m, F, sumb
    static const double tb[][5] = {
        {0, 0, 0, 1, 5128122},
        {0, 0, 1, 1, 280602},
        {0, 0, 1, -1, 277693},
        {2, 0, 0, -1, 173237},
        {2, 0, -1, 1, 55413},
        {2, 0, -1, -1, 46271},
        {2, 0, 0, 1, 32573},
        {0, 0, 2, 1, 17198},
        {2, 0, 1, -1, 9266},
        {0, 0, 2, -1, 8822},
        {2, -1, 0, -1, 8216},
        {2, 0, -2, -1, 4324},
        {2, 0, 1, 1, 4200},
        {2, 1, 0, -1, -3359},
        {2, -1, -1, 1, 2463},
        {2, -1, 0, 1, 2211},
        {2, -1, -1, -1, 2065},
        {0, 1, -1, -1, -1870},
        {4, 0, -1, -1, 1828},
        {0, 1, 0, 1, -1794},
        {0, 0, 0, 3, -1749},
        {0, 1, -1, 1, -1565},
        {1, 0, 0, 1, -1491},
        {0, 1, 1, 1, -1475},
        {0, 1, 1, -1, -1410},
        {0, 1, 0, -1, -1344},
        {1, 0, 0, -1, -1335},
        {0, 0, 3, 1, 1107},
        {4, 0, 0, -1, 1021},
        {4, 0, -1, 1, 833},
        {0, 0, 1, -3, 777},
        {4, 0, -2, 1, 671},
        {2, 0, 0, -3, 607},
        {2, 0, 2, -1, 596},
        {2, -1, 1, -1, 491},
        {2, 0, -2, 1, -451},
        {0, 0, 3, -1, 439},
        {2, 0, 2, 1, 422},
        {2, 0, -3, -1, 421},
        {2, 1, -1, 1, -366},
        {2, 1, 0, 1, -351},
        {4, 0, 0, 1, 331},
        {2, -1, 1, 1, 315},
        {2, -2, 0, -1, 302},
        {0, 0, 1, 3, -283},
        {2, 1, 1, -1, -229},
        {1, 1, 0, -1, 223},
        {1, 1, 0, 1, 223},
        {0, 1, -2, -1, -220},
        {2, 1, -1, -1, -220},
        {1, 0, 1, 1, -185},
        {2, -1, -2, -1, 181},
        {0, 1, 2, 1, -177},
        {4, 0, -2, -1, 176},
        {4, -1, -1, -1, 166},
        {1, 0, 1, -1, -164},
        {4, 0, 1, -1, 132},
        {1, 0, -1, -1, -119},
        {4, -1, 0, -1, 115},
        {2, -2, 0, 1, 107}
    };

    double suml = 0, sumr = 0;

    for (int i = 0; i < 60; i++) {
        double arg = ta[i][0] * D + ta[i][1] * M + ta[i][2] * m + ta[i][3] * F;
        double sl = ta[i][4] * sin(arg);
        double sr = ta[i][5] * cos(arg);
        int mCoeff = static_cast<int>(ta[i][1]);
        int absM = (mCoeff < 0) ? -mCoeff : mCoeff;
        if (absM == 1) {
            sl *= E; sr *= E;
        } else if (absM == 2) {
            sl *= E * E; sr *= E * E;
        }
        suml += sl;
        sumr += sr;
    }

    suml += 3958 * sin(A1);        // Einfluss Venus
    suml += 1962 * sin(l - F);     // Einfluss Abplattung Erde
    suml += 318 * sin(A2);         // Einfluss Jupiter

    double sumb = 0;
    for (int i = 0; i < 60; i++) {
        double arg = tb[i][0] * D + tb[i][1] * M + tb[i][2] * m + tb[i][3] * F;
        double sb = tb[i][4] * sin(arg);
        int mCoeff = static_cast<int>(tb[i][1]);
        int absM = (mCoeff < 0) ? -mCoeff : mCoeff;
        if (absM == 1) {
            sb *= E;
        } else if (absM == 2) {
            sb *= E * E;
        }
        sumb += sb;
    }

    sumb += -2235 * sin(l);
    sumb += 382 * sin(A3);
    sumb += 175 * sin(A1 - F);
    sumb += 175 * sin(A1 + F);
    sumb += 127 * sin(l - m);
    sumb += -115 * sin(l + m);

    return {
        l + (suml / 1000000.0) * DEG2RAD,
        (sumb / 1000000.0) * DEG2RAD,
        385000.56 + sumr / 1000.0
    };
}

/**
 * Berechnet den aufsteigenden Knoten des Mondes für ein gegebenes Julianisches Datum.
 * @param jd Julianisches Datum
 * @return Aufsteigender Knoten in Bogenmaß
 */
double calculateRisingKnotMoon(double jd) {
    double T = calculateJulianEpoch(jd);
    double risingKnot = 125.0445550 - 1934.1361849 * T + 0.0020762 * T * T
                      + T * T * T / 467410.0 - T * T * T * T / 60616000.0;
    return normalizeAngleDegree(risingKnot) * DEG2RAD;
}

/**
 * Berechnet die Mondphase basierend auf Sonne und Mond Positionen.
 * @param sunRaDek Äquatoriale Koordinaten der Sonne
 * @param sunDistance Distanz der Sonne
 * @param moonRaDek Äquatoriale Koordinaten des Mondes
 * @param moonDistance Distanz des Mondes
 * @return Mondphase (0 = Neumond, 1 = Vollmond)
 */
double calculateMoonPhase(const RaDek& sunRaDek, double sunDistance,
                          const RaDek& moonRaDek, double moonDistance) {
    // Geozenrische Elongation zwischen Sonne und Mond
    // Formel 46.2 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.                        
    double cosPsi = sin(sunRaDek.dek) * sin(moonRaDek.dek)
                  + cos(sunRaDek.dek) * cos(moonRaDek.dek) * cos(sunRaDek.ra - moonRaDek.ra);

    double i = atan2(sunDistance * sin(acos(cosPsi)), moonDistance - sunDistance * cosPsi);
    return (1 + cos(i)) / 2.0;
}

/**
 * Berechnet die Libration des Mondes für ein gegebenes Julianisches Datum und Mondposition.
 * @param jd Julianisches Datum
 * @param moon Mondposition
 * @return Mondachse mit Libration
 */
MoonAxle calculateMoonAxle(double jd, const MoonPosition& moon) {
    double T = calculateJulianEpoch(jd);

    double risingKnot = calculateRisingKnotMoon(jd);
    constexpr double I = 1.54242 * DEG2RAD;

    double lambda = moon.longitude;
    double beta = moon.latitude;

    double E = 1 + T * (-0.002516 - 0.0000074 * T);

    double l = normalizeAngleDegree(218.3164591 + T * (481267.88134236 - 0.0013268 * T)
             + T * T * T / 538841.0 - T * T * T * T / 65194000.0) * DEG2RAD;
    double D = normalizeAngleDegree(297.8502042 + T * (445267.1115168 - T * 0.0016300)
             + T * T * T / 545868.0 - T * T * T * T / 113065000.0) * DEG2RAD;
    double M = normalizeAngleDegree(357.5291092 + T * (35999.0502909 + T * (-0.0001536 + T / 24490000.0))) * DEG2RAD;
    double m = normalizeAngleDegree(134.9634114 + T * (477198.8676313 + 0.0089970 * T)
             + T * T * T / 69699.0 - T * T * T * T / 14712000.0) * DEG2RAD;
    double F = normalizeAngleDegree(93.2720993 + T * (483202.0175273 - 0.0034029 * T)
             - T * T * T / 3526000.0 + T * T * T * T / 863310000.0) * DEG2RAD;

    double W = lambda - risingKnot;

    // Formel 51.1
    double A = atan2(sin(W) * cos(beta) * cos(I) - sin(beta) * sin(I), cos(W) * cos(beta));

    // Optische Libration
    double l1 = A - F;
    double b1 = asin(-sin(W) * cos(beta) * sin(I) - sin(beta) * cos(I));

    double K1 = 119.75 + 131.849 * T;
    double K2 = 72.56 + 20.186 * T;

    double rho = (-.02752 * cos(m)
                + -.02245 * sin(F)
                +  .00684 * cos(m - 2 * F)
                + -.00293 * cos(2 * F)
                + -.00085 * cos(2 * (F - D))
                + -.00054 * cos(m - 2 * D)
                + -.0002 * sin(m + F)
                + -.0002 * cos(m + 2 * F)
                + -.0002 * cos(m - F)
                + .00014 * cos(m + 2 * (F - D))) * DEG2RAD;

    double sigma = (-.02816 * sin(m)
                  + .02244 * cos(F)
                  + -.00682 * sin(m - 2 * F)
                  + -.00279 * sin(2 * F)
                  + -.00083 * sin(2 * (F - D))
                  + .00069 * sin(m - 2 * D)
                  + .0004 * cos(m + F)
                  + -.00025 * sin(2 * m)
                  + -.00023 * sin(m + 2 * F)
                  + .0002 * cos(m - F)
                  + .00019 * sin(m - F)
                  + .00013 * sin(m + 2 * (F - D))
                  + -.0001 * cos(m - 3 * F)) * DEG2RAD;

    double tau = (.0252 * sin(M) * E
                + .00473 * sin(2 * (m - F))
                + -.00467 * sin(m)
                + .00396 * sin(K1 * DEG2RAD)
                + .00276 * sin(2 * (m - D))
                + .00196 * sin(risingKnot)
                + -.00183 * cos(m - F)
                + .00115 * sin(m - 2 * D)
                + -.00096 * sin(m - D)
                + .00046 * sin(2 * (F - D))
                + -.00039 * sin(m - F)
                + -.00032 * sin(m - M - D)
                + .00027 * sin(2 * (m - D) - M)
                + .00023 * sin(K2 * DEG2RAD)
                + -.00014 * sin(2 * D)
                + .00014 * cos(2 * (m - F))
                + -.00012 * sin(m - 2 * F)
                + -.00012 * sin(2 * m)
                + .00011 * sin(2 * (m - M - D))) * DEG2RAD;

    double l2 = -tau + (rho * cos(A) + sigma * sin(A)) * tan(b1);
    double b2 = sigma * cos(A) - rho * sin(A);

    RaDek moonRadek = calculateRaDek(moon.longitude, moon.latitude);

    double V = risingKnot + sigma / sin(I);
    double X = sin(I + rho) * sin(V);
    double Y = sin(I + rho) * cos(V) * cos(EPSILON * DEG2RAD) - cos(I + rho) * sin(EPSILON * DEG2RAD);

    double w = atan2(X, Y);
    double P = asin(sqrt(X * X + Y * Y) * cos(moonRadek.ra - w) / cos(moon.latitude));

    return {{l1 + l2, b1 + b2}, P};
}

// ---------------------------------------------------------------
//  Matrix-Operationen
// ---------------------------------------------------------------

/**
 * Erstellt eine Rotationsmatrix um eine gegebene Achse.
 * @param axle Rotationsachse
 * @param angle Rotationswinkel in Bogenmaß
 * @return Rotationsmatrix
 */
Mat3 createRotationMatrix(const Vec3& axle, double angle) {
    double cosA = cos(angle);
    double sinA = sin(angle);

    double len = sqrt(axle.x * axle.x + axle.y * axle.y + axle.z * axle.z);
    double nx = axle.x / len;
    double ny = axle.y / len;
    double nz = axle.z / len;

    double f = 1 - cosA;
    Mat3 rot;
    rot.m[0][0] = nx * nx * f + cosA;
    rot.m[0][1] = nx * ny * f - nz * sinA;
    rot.m[0][2] = nx * nz * f + ny * sinA;
    rot.m[1][0] = nx * ny * f + nz * sinA;
    rot.m[1][1] = ny * ny * f + cosA;
    rot.m[1][2] = ny * nz * f - nx * sinA;
    rot.m[2][0] = nx * nz * f - ny * sinA;
    rot.m[2][1] = ny * nz * f + nx * sinA;
    rot.m[2][2] = nz * nz * f + cosA;
    return rot;
}

/**
 * Multipliziert zwei 3x3 Matrizen.
 * @param m1 Erste Matrix
 * @param m2 Zweite Matrix
 * @return Produktmatrix
 */
Mat3 multiplyMatrix(const Mat3& m1, const Mat3& m2) {
    Mat3 result;
    memset(&result, 0, sizeof(Mat3));
    for (int i = 0; i < 3; i++) {
        for (int k = 0; k < 3; k++) {
            for (int j = 0; j < 3; j++) {
                result.m[i][k] += m1.m[i][j] * m2.m[j][k];
            }
        }
    }
    return result;
}

/**
 * Wendet eine Matrix auf einen Vektor an.
 * @param point Eingabevektor
 * @param matrix Matrix
 * @return Transformierter Vektor
 */
Vec3 applyMatrix(const Vec3& point, const Mat3& matrix) {
    return {
        point.x * matrix.m[0][0] + point.y * matrix.m[0][1] + point.z * matrix.m[0][2],
        point.x * matrix.m[1][0] + point.y * matrix.m[1][1] + point.z * matrix.m[1][2],
        point.x * matrix.m[2][0] + point.y * matrix.m[2][1] + point.z * matrix.m[2][2]
    };
}

// ---------------------------------------------------------------
//  Mondphasen-Rendering
// ---------------------------------------------------------------

/**
 * Rendert die Mondphase in einen Pixelbuffer.
 * @param sunRaDek Äquatoriale Koordinaten der Sonne
 * @param moonRaDek Äquatoriale Koordinaten des Mondes
 * @param moonAxleAngle Achsenwinkel des Mondes
 * @param libration Libration des Mondes
 * @param siderealTime Sternzeit in Stunden
 * @param latitude Breitengrad in Grad
 * @param radius Radius des Mondes in Pixeln
 * @param outputBuffer Ausgabepuffer für Pixel
 * @param moonTexture Mondtextur (optional)
 * @param texSize Größe der Textur
 * @return Anzahl der beleuchteten Pixel
 */
int drawMoonPhase(const RaDek& sunRaDek, const RaDek& moonRaDek,
                  double moonAxleAngle, const Libration& libration,
                  double siderealTime, double latitude,
                  int radius,
                  MoonPhasePixel* outputBuffer,
                  const MoonPhasePixel* moonTexture,
                  int texSize) {

    double cosPsi = sin(sunRaDek.dek) * sin(moonRaDek.dek)
                  + cos(sunRaDek.dek) * cos(moonRaDek.dek) * cos(sunRaDek.ra - moonRaDek.ra);

    double i = atan2(sunRaDek.distance * sin(acos(cosPsi)),
                     moonRaDek.distance - sunRaDek.distance * cosPsi);
    double k = (1 + cos(i)) / 2.0;

    double chi = atan2(cos(sunRaDek.dek) * sin(sunRaDek.ra - moonRaDek.ra),
                       sin(sunRaDek.dek) * cos(moonRaDek.dek)
                     - cos(sunRaDek.dek) * sin(moonRaDek.dek) * cos(sunRaDek.ra - moonRaDek.ra));

    double q = atan2(sin(siderealTime - moonRaDek.ra),
                     tan(latitude * DEG2RAD) * cos(moonRaDek.dek)
                   - sin(moonRaDek.dek) * cos(siderealTime - moonRaDek.ra));

    // Rotationsmatrizen für Textur-Mapping
    Mat3 axleRot = createRotationMatrix({0, 0, 1}, -moonAxleAngle + q + 0.389615);
    Mat3 libLongitudeRot = createRotationMatrix({0, 1, 0}, libration.longitude - 2.009 * DEG2RAD);
    Mat3 libLatitudeRot = createRotationMatrix({1, 0, 0}, libration.latitude - 0.64 * DEG2RAD);
    Mat3 rotMatrix = multiplyMatrix(libLongitudeRot, libLatitudeRot);
    rotMatrix = multiplyMatrix(axleRot, rotMatrix);

    double mask = chi - q + M_PI / 2.0;
    int r = radius;
    double bb = static_cast<double>(r) * r;
    double aa = static_cast<double>(r) * r;

    if (k > 0.5) {
        double f = k * 2 - 1;
        aa *= f * f;
    } else {
        double f = 1 - k * 2;
        aa *= f * f;
    }

    double cosMask = cos(-mask);
    double sinMask = sin(-mask);

    int width = 2 * r;
    int litPixels = 0;

    // Mondfarbe für Pixel ohne Textur
    constexpr MoonPhasePixel moonColor = {175, 168, 156, 255}; // #afa89c
    constexpr MoonPhasePixel black = {0, 0, 0, 0};

    for (int y = -r; y < r; y++) {
        for (int x = -r; x < r; x++) {
            int bufIdx = (y + r) * width + (x + r);

            double circle = static_cast<double>(x) * x + static_cast<double>(y) * y - static_cast<double>(r) * r;
            if (circle > 0) {
                outputBuffer[bufIdx] = black;
                continue;
            }

            double conditionX = x * cosMask + y * sinMask;
            double conditionY = -x * sinMask + y * cosMask;
            double ellipse = conditionX * conditionX * bb + conditionY * conditionY * aa - aa * bb;
            bool pixelActive = false;

            if (k > 0.5) {
                pixelActive = conditionX >= 0 || ellipse <= 0;
            } else {
                pixelActive = conditionX >= 0 && ellipse > 0;
            }

            if (pixelActive) {
                double z = sqrt(static_cast<double>(r) * r - static_cast<double>(x) * x - static_cast<double>(y) * y);
                Vec3 point = applyMatrix({static_cast<double>(x), static_cast<double>(-y), z}, rotMatrix);

                if (moonTexture && texSize > 0 && point.z > 0) {
                    int texX = static_cast<int>(point.x + r);
                    int texY = static_cast<int>(-point.y + r);
                    if (texX >= 0 && texX < texSize && texY >= 0 && texY < texSize) {
                        outputBuffer[bufIdx] = moonTexture[texY * texSize + texX];
                    } else {
                        outputBuffer[bufIdx] = moonColor;
                    }
                } else {
                    outputBuffer[bufIdx] = moonColor;
                }
                litPixels++;
            } else {
                outputBuffer[bufIdx] = black;
            }
        }
    }

    return litPixels;
}

} // namespace astro
