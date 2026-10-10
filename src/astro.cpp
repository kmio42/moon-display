#include "astro.h"
#include <cstdlib>
#include <utility>

// Kapitel- und Formelnummern beziehen sich auf Jean Meeus, "Astronomische Algorithmen",
// 2. Auflage, Barth 1994 (siehe Übersicht in astro.h).

namespace astro {

// ---------------------------------------------------------------
//  Hilfsfunktionen für die Formeln
// ---------------------------------------------------------------

/**
 * Horner-Schema: c0 + x·(c1 + x·(c2 + …)).
 * Die Koeffizienten stehen in derselben Reihenfolge wie im Buch (konstantes Glied zuerst);
 * die Einheit des Ergebnisses ist die der Koeffizienten.
 */
static inline constexpr double horner(const double) {
    return 0.0;
}

template <typename... C>
static inline constexpr double horner(const double x, const double c0, const C... c) {
    return c0 + x * horner(x, c...);
}

// ---------------------------------------------------------------
//  Julianischer Tag und Sternzeit (Kapitel 7, 11)
// ---------------------------------------------------------------

/**
 * Julianischer Tag für ein Datum des Gregorianischen bzw. (vor dem 15.10.1582) Julianischen Kalenders.
 * Formel 7.1.
 * @param year Jahr, astronomisch gezählt
 * @param month Monat [1..12]
 * @param day Tag [1..31]
 * @param hours Uhrzeit [h]
 */
JulianDay JulianDay::fromCalendar(int year, int month, const int day, const double hours) {
    // Januar und Februar zählen als 13. und 14. Monat des Vorjahres
    if (month <= 2) {
        year -= 1;
        month += 12;
    }

    const int A = year / 100;
    // Julianischer Kalender bis 4.10.1582: B = 0
    const bool julian = (year < 1582) || (year == 1582 && month < 10) || (year == 1582 && month == 10 && day <= 4);
    const int B = julian ? 0 : 2 - A + A / 4;

    // Formel 7.1
    return JulianDay(static_cast<int>(365.25 * (year + 4716))
                     + static_cast<int>(30.6001 * (month + 1))
                     + day + B - 1524.5
                     + hours / 24.0);
}

/**
 * Julianischer Tag für eine Zeit in UTC als struct tm.
 */
JulianDay JulianDay::fromTm(const struct tm& utc) {
    return fromCalendar(utc.tm_year + 1900, utc.tm_mon + 1, utc.tm_mday,
                        utc.tm_hour + utc.tm_min / 60.0 + utc.tm_sec / 3600.0);
}

/**
 * Mittlere Sternzeit in Greenwich für einen beliebigen Zeitpunkt, Formel 11.4.
 * @param jd Julianischer Tag (UT)
 * @return Sternzeit θ0
 */
SiderealTime calculateGreenwichSiderealTime(const JulianDay jd) {
    const double T = jd.centuries();

    // Formel 11.4 [°]
    const double theta0 = 280.46061837 + 360.98564736629 * (jd.value() - 2451545.0)
                        + T * T * horner(T, 0.000387933, -1.0 / 38710000.0);

    return SiderealTime(Angle::fromDeg(theta0));
}

/**
 * Lokale Sternzeit θ = θ0 − L (Kapitel 12; dort ist die Länge L West positiv).
 * @param jd Julianischer Tag (UT)
 * @param location Beobachtungsort (Länge Ost positiv)
 * @return Lokale Sternzeit θ
 */
SiderealTime calculateLocalSiderealTime(const JulianDay jd, const GeoLocation& location) {
    return calculateGreenwichSiderealTime(jd) + location.longitude;
}

// ---------------------------------------------------------------
//  Sonne (Kapitel 21, 24, 27, 29)
// ---------------------------------------------------------------

/**
 * Mittlere Schiefe der Ekliptik ε0, Formel 21.2.
 * @param jd Julianischer Tag
 * @return ε0
 */
Angle calculateObliquity(const JulianDay jd) {
    const double T = jd.centuries();
    // 23°26′21″.448 − 46″.8150 T − 0″.00059 T² + 0″.001813 T³
    return Angle::fromDms(23, 26, horner(T, 21.448, -46.8150, -0.00059, 0.001813));
}

// Mittlere Größen der Sonne bei niedriger Genauigkeit (Kapitel 24)
struct SunArguments {
    Angle L0;       // Geometrische mittlere Länge, Formel 24.2
    Angle M;        // Mittlere Anomalie, Formel 24.3
    double e;       // Exzentrizität der Erdbahn [1], Formel 24.4
    Angle C;        // Mittelpunktsgleichung
};

static inline SunArguments sunArguments(const double T) {
    const Angle M = Angle::fromDeg(horner(T, 357.52910, 35999.05030, -0.0001559, -0.00000048));
    return {
        .L0 = Angle::fromDeg(horner(T, 280.46645, 36000.76983, 0.0003032)).normalized(),
        .M  = M,
        .e  = horner(T, 0.016708617, -0.000042037, -0.0000001236),
        .C  = Angle::fromDeg(horner(T, 1.914600, -0.004817, -0.000014) * sin(M)
                             + horner(T, 0.019993, -0.000101) * sin(2 * M)
                             + 0.000290 * sin(3 * M)),
    };
}

/**
 * Exzentrizität der Erdbahn, Formel 24.4.
 * @param jd Julianischer Tag
 * @return Exzentrizität e [1]
 */
double calculateEarthEccentricity(const JulianDay jd) {
    return sunArguments(jd.centuries()).e;
}

/**
 * Geozentrische Position der Sonne bei niedriger Genauigkeit (Kapitel 24):
 * wahre geometrische Länge ☉ = L0 + C, bezogen auf das mittlere Äquinoktium des Datums,
 * Breite 0 (sie ist nie größer als 1″.2) und Radiusvektor
 * R = 1.000001018·(1 − e²)/(1 + e·cos ν) mit ν = M + C (Formel 24.5).
 * @param jd Julianischer Tag
 * @return Länge ☉, Breite und Entfernung R [km]
 */
Ecliptic calculateSunPosition(const JulianDay jd) {
    const SunArguments sun = sunArguments(jd.centuries());
    const Angle nu = sun.M + sun.C;
    return {
        .longitude = sun.L0 + sun.C,
        .latitude  = Angle(),
        .distance  = EARTH_SEMI_MAJOR_AXIS * (1 - sun.e * sun.e) / (1 + sun.e * cos(nu)) * AU_KM,
    };
}

/**
 * Wahre Anomalie der Erde aus der Keplergleichung E = M + e·sin E (Formel 29.5),
 * iterativ gelöst, und tan(ν/2) = √((1+e)/(1−e))·tan(E/2) (Formel 29.1).
 * @param jd Julianischer Tag
 * @return Wahre Anomalie ν
 */
Angle calculateEarthTrueAnomaly(const JulianDay jd) {
    const SunArguments sun = sunArguments(jd.centuries());

    // Exzentrische Anomalie E, Formel 29.5 (in Radiant gerechnet)
    Angle E = sun.M;
    for (int i = 0; i < 10; i++) {
        E = sun.M + Angle::fromRad(sun.e * sin(E));
    }

    // Formel 29.1
    return 2.0 * Angle::atan2(sqrt(1 + sun.e) * sin(E / 2), sqrt(1 - sun.e) * cos(E / 2));
}

/**
 * Zeitgleichung nach Formel 27.1 mit geometrischer statt scheinbarer Rektaszension.
 * @param jd Julianischer Tag
 * @param withEarthTilt false: Rektaszension ohne Schiefe der Ekliptik (nur Anteil der Bahnexzentrizität)
 * @return Zeitgleichung [min], wahre minus mittlere Sonnenzeit
 */
double calculateEquationOfTime(const JulianDay jd, const bool withEarthTilt) {
    const SunArguments sun = sunArguments(jd.centuries());

    // Mittlere Länge der Sonne L0, Formel 27.2 mit τ in Julianischen Jahrtausenden
    const double tau = jd.millennia();
    const Angle L0 = Angle::fromDeg(horner(tau, 280.4664567, 360007.6982779, 0.03032028,
                                           1.0 / 49931.0, -1.0 / 15299.0, -1.0 / 1988000.0)).normalized();

    // Wahre Länge der Sonne, Kapitel 24
    const Angle L = sun.L0 + sun.C;
    const Angle eps = withEarthTilt ? calculateObliquity(jd) : Angle();

    // Rektaszension der Sonne, Formel 12.3 mit β = 0
    const Angle alpha = Angle::atan2(cos(eps) * sin(L), cos(L)).normalized();

    // Formel 27.1: E = L0 − 0°.0057183 − α + Δψ·cos ε. Die Konstante (Aberration und FK5) und
    // Δψ·cos ε (Nutation) bringen die scheinbare Rektaszension auf die geometrische; da α hier
    // bereits geometrisch ist, entfallen beide. 1° entspricht 4 Zeitminuten.
    double E = (L0 - alpha).deg() * 4;

    // Vielfache von 24 h entfernen; |E| ist immer kleiner als 20 min
    if (E > 60 * 12) E -= 24 * 60;
    else if (E < -60 * 12) E += 24 * 60;

    return E;
}

// ---------------------------------------------------------------
//  Koordinatentransformation (Kapitel 10, 12, 13, 39)
// ---------------------------------------------------------------

/**
 * Ekliptikale in äquatoriale Koordinaten, Formeln 12.3 und 12.4.
 * @param ecliptic Ekliptikale Länge λ und Breite β; die Entfernung wird nicht benötigt
 * @param eps Schiefe der Ekliptik ε, z. B. aus calculateObliquity
 * @return Rektaszension α und Deklination δ
 */
Equatorial calculateEquatorial(const Ecliptic& ecliptic, const Angle eps) {
    const Angle lambda = ecliptic.longitude;
    const Angle beta = ecliptic.latitude;
    return {
        .ra  = Angle::atan2(sin(lambda) * cos(eps) - tan(beta) * sin(eps), cos(lambda)),     // Formel 12.3
        .dec = Angle::asin(sin(beta) * cos(eps) + cos(beta) * sin(eps) * sin(lambda)),       // Formel 12.4
    };
}

/**
 * Äquatoriale in horizontale Koordinaten, Formeln 12.5 und 12.6.
 * @param equ Rektaszension und Deklination
 * @param localSiderealTime Lokale Sternzeit θ
 * @param location Beobachtungsort; benötigt wird nur die Breite φ, die Länge steckt bereits in θ
 * @return Azimut A (von Süden über Westen) und Höhe h
 */
Horizontal calculateHorizontal(const Equatorial& equ, const SiderealTime localSiderealTime, const GeoLocation& location) {
    const Angle phi = location.latitude;
    // Lokaler Stundenwinkel H = θ − α
    const Angle H = localSiderealTime - equ.ra;
    return {
        .azimuth  = Angle::atan2(sin(H), cos(H) * sin(phi) - tan(equ.dec) * cos(phi)),         // Formel 12.5
        .altitude = Angle::asin(sin(phi) * sin(equ.dec) + cos(phi) * cos(equ.dec) * cos(H)),  // Formel 12.6
    };
}

/**
 * Parallaktischer Winkel q, Formel 13.1.
 * @param equ Rektaszension und Deklination
 * @param localSiderealTime Lokale Sternzeit θ
 * @param location Beobachtungsort; benötigt wird nur die Breite φ, die Länge steckt bereits in θ
 * @return q, vor dem Meridiandurchgang negativ
 */
Angle calculateParallacticAngle(const Equatorial& equ, const SiderealTime localSiderealTime, const GeoLocation& location) {
    const Angle phi = location.latitude;
    const Angle H = localSiderealTime - equ.ra;
    return Angle::atan2(sin(H), tan(phi) * cos(equ.dec) - sin(equ.dec) * cos(H));
}

/**
 * Topozentrische aus geozentrischen äquatorialen Koordinaten, Formeln 39.2 und 39.3.
 * ρ sin φ′ und ρ cos φ′ nach Kapitel 10 mit der Höhe des Beobachters über dem Meeresspiegel.
 * @param equ Geozentrische Rektaszension und Deklination
 * @param parallax Äquatoriale Horizontalparallaxe π
 * @param localSiderealTime Lokale Sternzeit θ
 * @param location Beobachtungsort; benötigt werden Breite φ und Höhe H, die Länge steckt bereits in θ
 * @return Topozentrische Rektaszension und Deklination
 */
Equatorial calculateTopocentric(const Equatorial& equ, const Angle parallax, const SiderealTime localSiderealTime,
                                const GeoLocation& location) {
    const Angle phi = location.latitude;
    // Kapitel 10: b/a des Erdellipsoids [1] und Höhe über dem Meeresspiegel [m]
    constexpr double ba = 0.99664719;
    const double height = location.elevation;

    const Angle u = Angle::atan(ba * tan(phi));
    const double rhoSinPhi = ba * sin(u) + height / (EARTH_RADIUS * 1000.0) * sin(phi);
    const double rhoCosPhi = cos(u) + height / (EARTH_RADIUS * 1000.0) * cos(phi);

    // Geozentrischer Stundenwinkel H
    const Angle H = localSiderealTime - equ.ra;

    // Formel 39.2: Δα
    const Angle deltaAlpha = Angle::atan(-rhoCosPhi * sin(parallax) * sin(H)
                                         / (cos(equ.dec) - rhoCosPhi * sin(parallax) * cos(H)));

    // Formel 39.3: δ′
    const Angle dec = Angle::atan2((sin(equ.dec) - rhoSinPhi * sin(parallax)) * cos(deltaAlpha),
                                   cos(equ.dec) - rhoCosPhi * sin(parallax) * cos(H));

    return { .ra = equ.ra + deltaAlpha, .dec = dec };
}

// ---------------------------------------------------------------
//  Interpolation (Kapitel 3)
// ---------------------------------------------------------------

/**
 * Interpolation aus drei äquidistanten Tabellenwerten, Formeln 3.1 und 3.3.
 * @param y1 Tabellenwert bei x2 − 1 [beliebige Einheit]
 * @param y2 Tabellenwert bei x2 (zentraler Wert)
 * @param y3 Tabellenwert bei x2 + 1
 * @param n Interpolationsfaktor n = x − x2 [Tabellenintervalle]
 * @return Interpolierter Wert [Einheit der Tabellenwerte]
 */
double interpolate(const double y1, const double y2, const double y3, const double n) {
    // Erste und zweite Differenzen, Formel 3.1
    const double a = y2 - y1;
    const double b = y3 - y2;
    const double c = b - a;
    // Formel 3.3
    return y2 + n / 2.0 * (a + b + n * c);
}

// ---------------------------------------------------------------
//  Sonnenauf-/-untergang (Kapitel 14)
// ---------------------------------------------------------------

/**
 * Genäherte Zeiten von Sonnenaufgang, Meridiandurchgang und Sonnenuntergang, Formeln 14.1 und 14.2
 * (ohne die Korrekturen Δm). Rektaszension und Deklination gelten für 0h UT.
 * @param jd Julianischer Tag eines Zeitpunkts des gewünschten UT-Tags
 * @param location Beobachtungsort
 * @return Zeiten [h UT]; bei Polartag oder Polarnacht weder Auf- noch Untergang
 */
RiseTransitSet calculateSunRiseSet(const JulianDay jd, const GeoLocation& location) {
    const Angle phi = location.latitude;

    // Standardhöhe der Sonne h0 = −0°50′
    constexpr Angle h0 = -0.8333_deg;

    const JulianDay midnight = jd.midnight();
    const Equatorial sun = calculateEquatorial(calculateSunPosition(midnight), calculateObliquity(midnight));
    const SiderealTime theta0 = calculateGreenwichSiderealTime(midnight);

    // Formel 14.2, Durchgang m0 = (α + L − θ0)/360° in Tagesbruchteilen; die Länge L zählt im Buch West positiv
    const double m0 = (sun.ra - location.longitude - theta0.angle()).normalized() / 360_deg;

    // Höhe beim Durchgang, Formel 12.6 mit H = 0
    const Angle transitAltitude = Angle::asin(sin(phi) * sin(sun.dec) + cos(phi) * cos(sun.dec));

    RiseTransitSet result = {
        .hasRise = false, .hasSet = false, .hasTransit = true,
        .rise = 0.0, .set = 0.0, .transit = m0 * 24.0,
        .maxAltitude = transitAltitude - h0, .maxAltitudeTime = m0 * 24.0,
    };

    // Formel 14.1; außerhalb [−1, 1] Polartag oder Polarnacht
    const double cosH0 = (sin(h0) - sin(phi) * sin(sun.dec)) / (cos(phi) * cos(sun.dec));
    if (cosH0 < -1 || cosH0 > 1) {
        return result;
    }
    const Angle H0 = Angle::acos(cosH0);

    // Formel 14.2, Aufgang m1 und Untergang m2 in Tagesbruchteilen
    result.hasRise = result.hasSet = true;
    result.rise = fmod(m0 - H0 / 360_deg + 1.0, 1.0) * 24.0;
    result.set  = fmod(m0 + H0 / 360_deg + 1.0, 1.0) * 24.0;
    return result;
}

// ---------------------------------------------------------------
//  Solstitien und Äquinoktien (Kapitel 26)
// ---------------------------------------------------------------

/**
 * Zeitpunkt eines Äquinoktiums oder Solstitiums, Tabellen 26.2 und 26.3 (Jahre +1000 bis +3000).
 * @param year Jahr
 * @param type Frühlings-, Sommer-, Herbst- oder Winteranfang
 * @return Zeitpunkt [JDE]
 */
double calculateEquinoxSolstice(const int year, const EquinoxType type) {
    // Tabelle 26.2: JDE0 = c0 + c1·Y + c2·Y² + c3·Y³ + c4·Y⁴, Y = (Jahr − 2000)/1000
    static const double meanPoint[4][5] = {
        {2451623.80984, 365242.37404,  0.05169, -0.00411, -0.00057}, // Märzäquinoktium
        {2451716.56767, 365241.62603,  0.00325,  0.00888, -0.00030}, // Junisolstitium
        {2451810.21715, 365242.01767, -0.11575,  0.00337,  0.00078}, // Septemberäquinoktium
        {2451900.05952, 365242.74049, -0.06223, -0.00823,  0.00032}, // Dezembersolstitium
    };

    // Tabelle 26.3: S = Σ A·cos(B + C·T), B und C in Grad
    static const double periodic[24][3] = {
        {485, 324.96,   1934.136}, {203, 337.23,  32964.467}, {199, 342.08,     20.186},
        {182,  27.85, 445267.112}, {156,  73.14,  45036.886}, {136, 171.52,  22518.443},
        { 77, 222.54,  65928.934}, { 74, 296.72,   3034.906}, { 70, 243.58,   9037.513},
        { 58, 119.81,  33718.147}, { 52, 297.17,    150.678}, { 50,  21.02,   2281.226},
        { 45, 247.54,  29929.562}, { 44, 325.15,  31555.956}, { 29,  60.93,   4443.417},
        { 18, 155.12,  67555.328}, { 17, 288.79,   4562.452}, { 16, 198.04,  62894.029},
        { 14, 199.76,  31436.921}, { 12,  95.39,  14577.848}, { 12, 287.11,  31931.756},
        { 12, 320.81,  34777.259}, {  9, 227.73,   1222.114}, {  8,  15.45,  16859.074},
    };

    const double* c = meanPoint[static_cast<int>(type)];
    const double Y = (year - 2000) / 1000.0;
    const double JDE0 = horner(Y, c[0], c[1], c[2], c[3], c[4]);

    const double T = JulianDay(JDE0).centuries();
    const Angle W = Angle::fromDeg(35999.373 * T - 2.47);
    const double deltaLambda = 1 + 0.0334 * cos(W) + 0.0007 * cos(2 * W);

    double S = 0;
    for (const auto& term : periodic) {
        S += term[0] * cos(Angle::fromDeg(term[1] + term[2] * T));
    }

    return JDE0 + 0.00001 * S / deltaLambda;
}

// ---------------------------------------------------------------
//  Perihel / Aphel der Erde (Kapitel 37)
// ---------------------------------------------------------------

// Perihel (k ganzzahlig) oder Aphel (k + 0.5) der Erde nach Kapitel 37 [JDE], mit den Korrekturen
// für den Mittelpunkt der Erde statt des Baryzentrums Erde–Mond.
static double earthApsisForK(const double k, const ApsisType type) {
    // Korrekturterme [d]: Koeffizient × sin(A0 + A1·k), A0, A1 in Grad
    static const double angles[5][2] = {
        {328.41, 132.788585},
        {316.13, 584.903153},
        {346.20, 450.380738},
        {136.95, 659.306737},
        {249.52, 329.653368},
    };
    static const double perihelion[5] = { 1.278, -0.055, -0.091, -0.056, -0.045};
    static const double aphelion[5]   = {-1.352,  0.061,  0.062,  0.029,  0.031};
    const double* coefficient = (type == ApsisType::Periapsis) ? perihelion : aphelion;

    double correction = 0;
    for (int i = 0; i < 5; i++) {
        correction += coefficient[i] * sin(Angle::fromDeg(angles[i][0] + angles[i][1] * k));
    }

    return horner(k, 2451547.507, 365.2596358, 0.0000000158) + correction;
}

/**
 * Erstes Perihel bzw. Aphel der Erde nach jd (Kapitel 37). Der mittlere Fehler beträgt drei Stunden.
 * @param jd Julianischer Tag
 * @param type Perihel (Periapsis) oder Aphel (Apoapsis)
 * @return Zeitpunkt [JDE] und Entfernung Erde–Sonne nach Formel 24.5 [km]
 */
Apsis calculateNextEarthApsis(const JulianDay jd, const ApsisType type) {
    // k ≈ 0.99997·(Jahr − 2000.01), ganzzahlig für das Perihel, + 0.5 für das Aphel
    const double year = 2000.0 + (jd.value() - 2451545.0) / 365.25;
    double k = floor(0.99997 * (year - 2000.01)) - 1;
    if (type == ApsisType::Apoapsis) k += 0.5;
    double jde = earthApsisForK(k, type);
    while (jde <= jd.value()) {
        k += 1;
        jde = earthApsisForK(k, type);
    }
    return { .jde = jde, .distance = calculateSunPosition(JulianDay(jde)).distance };
}

// ---------------------------------------------------------------
//  Mondposition (Kapitel 45)
// ---------------------------------------------------------------

// Fundamentalargumente des Mondes
struct MoonArguments {
    Angle Lp;       // Mittlere Länge des Mondes L′, Formel 45.1
    Angle D;        // Mittlere Elongation des Mondes D, Formel 45.2
    Angle M;        // Mittlere Anomalie der Sonne M, Formel 45.3
    Angle Mp;       // Mittlere Anomalie des Mondes M′, Formel 45.4
    Angle F;        // Argument der Breite des Mondes F, Formel 45.5
    double E;       // Faktor für die abnehmende Exzentrizität der Erdbahn [1], Formel 45.6
};

static inline MoonArguments moonArguments(const double T) {
    return {
        .Lp = Angle::fromDeg(horner(T, 218.3164591, 481267.88134236, -0.0013268, 1.0 / 538841.0, -1.0 / 65194000.0)).normalized(),
        .D  = Angle::fromDeg(horner(T, 297.8502042, 445267.1115168, -0.0016300, 1.0 / 545868.0, -1.0 / 113065000.0)).normalized(),
        .M  = Angle::fromDeg(horner(T, 357.5291092, 35999.0502909, -0.0001536, 1.0 / 24490000.0)).normalized(),
        .Mp = Angle::fromDeg(horner(T, 134.9634114, 477198.8676313, 0.0089970, 1.0 / 69699.0, -1.0 / 14712000.0)).normalized(),
        .F  = Angle::fromDeg(horner(T, 93.2720993, 483202.0175273, -0.0034029, -1.0 / 3526000.0, 1.0 / 863310000.0)).normalized(),
        .E  = horner(T, 1.0, -0.002516, -0.0000074),
    };
}

// Periodischer Term für Länge und Entfernung: Σl += l·sin(arg), Σr += r·cos(arg),
// arg = D·D + M·M + Mp·M′ + F·F
struct MoonTermLR {
    int8_t D, M, Mp, F;
    int32_t l;      // Koeffizient [0.000001°]
    int32_t r;      // Koeffizient [0.001 km]
};

// Periodischer Term für die Breite: Σb += b·sin(arg)
struct MoonTermB {
    int8_t D, M, Mp, F;
    int32_t b;      // Koeffizient [0.000001°]
};

// Tabelle 45.1
static const MoonTermLR MOON_TERMS_LR[] = {
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
    {2, 0, -1, -2, 0, 8752},
};

// Tabelle 45.2
static const MoonTermB MOON_TERMS_B[] = {
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
    {2, -2, 0, 1, 107},
};

// Terme mit M bzw. 2M werden mit E bzw. E² multipliziert (Text zu Formel 45.6)
static inline double eccentricityFactor(const int multipleOfM, const double E) {
    switch (abs(multipleOfM)) {
        case 1:  return E;
        case 2:  return E * E;
        default: return 1.0;
    }
}

/**
 * Geozentrische Position des Mondes, bezogen auf das mittlere Äquinoktium des Datums (Kapitel 45).
 * @param jd Julianischer Tag
 * @return Länge λ, Breite β und Entfernung Δ [km]
 */
Ecliptic calculateMoonPosition(const JulianDay jd) {
    const double T = jd.centuries();
    const MoonArguments a = moonArguments(T);

    // Zusätzliche Argumente A1 (Venus), A2 (Jupiter), A3
    const Angle A1 = Angle::fromDeg(horner(T, 119.75, 131.849)).normalized();
    const Angle A2 = Angle::fromDeg(horner(T, 53.09, 479264.290)).normalized();
    const Angle A3 = Angle::fromDeg(horner(T, 313.45, 481266.484)).normalized();

    // Σl [0.000001°] und Σr [0.001 km], Tabelle 45.1
    double suml = 0, sumr = 0;
    for (const MoonTermLR& t : MOON_TERMS_LR) {
        const Angle arg = t.D * a.D + t.M * a.M + t.Mp * a.Mp + t.F * a.F;
        const double e = eccentricityFactor(t.M, a.E);
        suml += t.l * e * sin(arg);
        sumr += t.r * e * cos(arg);
    }
    suml += 3958 * sin(A1)              // Venus
          + 1962 * sin(a.Lp - a.F)      // Abplattung der Erde
          +  318 * sin(A2);             // Jupiter

    // Σb [0.000001°], Tabelle 45.2
    double sumb = 0;
    for (const MoonTermB& t : MOON_TERMS_B) {
        const Angle arg = t.D * a.D + t.M * a.M + t.Mp * a.Mp + t.F * a.F;
        sumb += t.b * eccentricityFactor(t.M, a.E) * sin(arg);
    }
    sumb += -2235 * sin(a.Lp)
          +   382 * sin(A3)
          +   175 * sin(A1 - a.F)
          +   175 * sin(A1 + a.F)
          +   127 * sin(a.Lp - a.Mp)
          -   115 * sin(a.Lp + a.Mp);

    return {
        .longitude = a.Lp + Angle::fromDeg(suml / 1000000.0),
        .latitude  = Angle::fromDeg(sumb / 1000000.0),
        .distance  = 385000.56 + sumr / 1000.0,
    };
}

/**
 * Länge des mittleren aufsteigenden Knotens der Mondbahn, Formel 45.7.
 * @param jd Julianischer Tag
 * @return Ω
 */
Angle calculateMoonAscendingNode(const JulianDay jd) {
    const double T = jd.centuries();
    return Angle::fromDeg(horner(T, 125.0445550, -1934.1361849, 0.0020762, 1.0 / 467410.0, -1.0 / 60616000.0)).normalized();
}

// ---------------------------------------------------------------
//  Beleuchteter Teil der Mondscheibe (Kapitel 46)
// ---------------------------------------------------------------

/**
 * Beleuchteter Teil der Mondscheibe, Formeln 46.1 bis 46.3.
 * @param sunEqu Rektaszension und Deklination der Sonne
 * @param sunDistance Entfernung Erde–Sonne R [km]
 * @param moonEqu Rektaszension und Deklination des Mondes
 * @param moonDistance Entfernung Erde–Mond Δ [km]
 * @return Beleuchteter Teil k [0..1], 0 = Neumond, 1 = Vollmond
 */
double calculateMoonIlluminatedFraction(const Equatorial& sunEqu, const double sunDistance,
                                        const Equatorial& moonEqu, const double moonDistance) {
    // Geozentrische Elongation ψ, Formel 46.2
    const double cosPsi = sin(sunEqu.dec) * sin(moonEqu.dec)
                        + cos(sunEqu.dec) * cos(moonEqu.dec) * cos(sunEqu.ra - moonEqu.ra);
    const Angle psi = Angle::acos(cosPsi);

    // Phasenwinkel i, Formel 46.3
    const Angle i = Angle::atan2(sunDistance * sin(psi), moonDistance - sunDistance * cosPsi);

    // Formel 46.1
    return (1 + cos(i)) / 2.0;
}

// ---------------------------------------------------------------
//  Libration und Positionswinkel der Achse (Kapitel 51)
// ---------------------------------------------------------------

/**
 * Optische und physische Libration sowie Positionswinkel der Mondachse (Kapitel 51, ohne Nutation).
 * @param jd Julianischer Tag
 * @param moon Geozentrische Position des Mondes aus calculateMoonPosition
 * @return Libration l (−π, π] und b, Positionswinkel P
 */
MoonOrientation calculateMoonOrientation(const JulianDay jd, const Ecliptic& moon) {
    const double T = jd.centuries();
    const MoonArguments a = moonArguments(T);
    const Angle Omega = calculateMoonAscendingNode(jd);

    // Neigung des mittleren Mondäquators gegen die Ekliptik I = 1°32′32″.7
    constexpr Angle I = 1.54242_deg;
    const Angle lambda = moon.longitude;
    const Angle beta = moon.latitude;

    // Optische Libration l′, b′, Formel 51.1
    const Angle W = lambda - Omega;
    const Angle A = Angle::atan2(sin(W) * cos(beta) * cos(I) - sin(beta) * sin(I), cos(W) * cos(beta));
    const Angle l1 = A - a.F;
    const Angle b1 = Angle::asin(-sin(W) * cos(beta) * sin(I) - sin(beta) * cos(I));

    // Physische Libration: ρ, σ, τ (Koeffizienten in Grad)
    const Angle K1 = Angle::fromDeg(horner(T, 119.75, 131.849));
    const Angle K2 = Angle::fromDeg(horner(T, 72.56, 20.186));
    const Angle D = a.D, M = a.M, Mp = a.Mp, F = a.F;

    const Angle rho = Angle::fromDeg(-0.02752 * cos(Mp)
                                     - 0.02245 * sin(F)
                                     + 0.00684 * cos(Mp - 2 * F)
                                     - 0.00293 * cos(2 * F)
                                     - 0.00085 * cos(2 * F - 2 * D)
                                     - 0.00054 * cos(Mp - 2 * D)
                                     - 0.00020 * sin(Mp + F)
                                     - 0.00020 * cos(Mp + 2 * F)
                                     - 0.00020 * cos(Mp - F)
                                     + 0.00014 * cos(Mp + 2 * F - 2 * D));

    const Angle sigma = Angle::fromDeg(-0.02816 * sin(Mp)
                                       + 0.02244 * cos(F)
                                       - 0.00682 * sin(Mp - 2 * F)
                                       - 0.00279 * sin(2 * F)
                                       - 0.00083 * sin(2 * F - 2 * D)
                                       + 0.00069 * sin(Mp - 2 * D)
                                       + 0.00040 * cos(Mp + F)
                                       - 0.00025 * sin(2 * Mp)
                                       - 0.00023 * sin(Mp + 2 * F)
                                       + 0.00020 * cos(Mp - F)
                                       + 0.00019 * sin(Mp - F)
                                       + 0.00013 * sin(Mp + 2 * F - 2 * D)
                                       - 0.00010 * cos(Mp - 3 * F));

    const Angle tau = Angle::fromDeg(+0.02520 * a.E * sin(M)
                                     + 0.00473 * sin(2 * Mp - 2 * F)
                                     - 0.00467 * sin(Mp)
                                     + 0.00396 * sin(K1)
                                     + 0.00276 * sin(2 * Mp - 2 * D)
                                     + 0.00196 * sin(Omega)
                                     - 0.00183 * cos(Mp - F)
                                     + 0.00115 * sin(Mp - 2 * D)
                                     - 0.00096 * sin(Mp - D)
                                     + 0.00046 * sin(2 * F - 2 * D)
                                     - 0.00039 * sin(Mp - F)
                                     - 0.00032 * sin(Mp - M - D)
                                     + 0.00027 * sin(2 * Mp - M - 2 * D)
                                     + 0.00023 * sin(K2)
                                     - 0.00014 * sin(2 * D)
                                     + 0.00014 * cos(2 * Mp - 2 * F)
                                     - 0.00012 * sin(Mp - 2 * F)
                                     - 0.00012 * sin(2 * Mp)
                                     + 0.00011 * sin(2 * Mp - 2 * M - 2 * D));

    // Physische Libration l″, b″, Formel 51.2
    const Angle l2 = -tau + (rho * cos(A) + sigma * sin(A)) * tan(b1);
    const Angle b2 = sigma * cos(A) - rho * sin(A);

    // Positionswinkel der Achse P
    const Angle eps = calculateObliquity(jd);
    const Equatorial moonEqu = calculateEquatorial(moon, eps);
    const Angle V = Omega + sigma / sin(I);
    const double X = sin(I + rho) * sin(V);
    const double Y = sin(I + rho) * cos(V) * cos(eps) - cos(I + rho) * sin(eps);
    const Angle omega = Angle::atan2(X, Y);
    const Angle b = b1 + b2;
    const Angle P = Angle::asin(sqrt(X * X + Y * Y) * cos(moonEqu.ra - omega) / cos(b));

    return { .libration = { .longitude = (l1 + l2).normalizedSigned(), .latitude = b }, .positionAngle = P };
}

// ---------------------------------------------------------------
//  Perigäum / Apogäum (Kapitel 48) und Knotendurchgänge (Kapitel 49) des Mondes
// ---------------------------------------------------------------

// Periodischer Term (a0 + aT·T)·sin(arg) bzw. ·cos(arg), arg = nD·D + nM·M + nF·F
struct ApsisTerm {
    double a0, aT;
    int8_t nD, nM, nF;
};

// Tabelle 48.1: Terme für die Zeit [d] (Sinus)
static const ApsisTerm PERIGEE_TIME[] = {
    {-1.6769, 0, 2, 0, 0}, {0.4589, 0, 4, 0, 0}, {-0.1856, 0, 6, 0, 0}, {0.0883, 0, 8, 0, 0},
    {-0.0773, 0.00019, 2, -1, 0}, {0.0502, -0.00013, 0, 1, 0}, {-0.0460, 0, 10, 0, 0},
    {0.0422, -0.00011, 4, -1, 0}, {-0.0256, 0, 6, -1, 0}, {0.0253, 0, 12, 0, 0}, {0.0237, 0, 1, 0, 0},
    {0.0162, 0, 8, -1, 0}, {-0.0145, 0, 14, 0, 0}, {0.0129, 0, 0, 0, 2}, {-0.0112, 0, 3, 0, 0},
    {-0.0104, 0, 10, -1, 0}, {0.0086, 0, 16, 0, 0}, {0.0069, 0, 12, -1, 0}, {0.0066, 0, 5, 0, 0},
    {-0.0053, 0, 2, 0, 2}, {-0.0052, 0, 18, 0, 0}, {-0.0046, 0, 14, -1, 0}, {-0.0041, 0, 7, 0, 0},
    {0.0040, 0, 2, 1, 0}, {0.0032, 0, 20, 0, 0}, {-0.0032, 0, 1, 1, 0}, {0.0031, 0, 16, -1, 0},
    {-0.0029, 0, 4, 1, 0}, {0.0027, 0, 9, 0, 0}, {0.0027, 0, 4, 0, 2}, {-0.0027, 0, 2, -2, 0},
    {0.0024, 0, 4, -2, 0}, {-0.0021, 0, 6, -2, 0}, {-0.0021, 0, 22, 0, 0}, {-0.0021, 0, 18, -1, 0},
    {0.0019, 0, 6, 1, 0}, {-0.0018, 0, 11, 0, 0}, {-0.0014, 0, 8, 1, 0}, {-0.0014, 0, 4, 0, -2},
    {-0.0014, 0, 6, 0, 2}, {0.0014, 0, 3, 1, 0}, {-0.0014, 0, 5, 1, 0}, {0.0013, 0, 13, 0, 0},
    {0.0013, 0, 20, -1, 0}, {0.0011, 0, 3, 2, 0}, {-0.0011, 0, 4, -2, 2}, {-0.0010, 0, 1, 2, 0},
    {-0.0009, 0, 22, -1, 0}, {-0.0008, 0, 0, 0, 4}, {0.0008, 0, 6, 0, -2}, {0.0008, 0, 2, 1, -2},
    {0.0007, 0, 0, 2, 0}, {0.0007, 0, 0, -1, 2}, {0.0007, 0, 2, 0, 4}, {-0.0006, 0, 0, -2, 2},
    {-0.0006, 0, 2, 2, -2}, {0.0006, 0, 24, 0, 0}, {0.0005, 0, 4, 0, -4}, {0.0005, 0, 2, 2, 0},
    {-0.0004, 0, 1, -1, 0},
};

static const ApsisTerm APOGEE_TIME[] = {
    {0.4392, 0, 2, 0, 0}, {0.0684, 0, 4, 0, 0}, {0.0456, -0.00011, 0, 1, 0},
    {0.0426, -0.00011, 2, -1, 0}, {0.0212, 0, 0, 0, 2}, {-0.0189, 0, 1, 0, 0}, {0.0144, 0, 6, 0, 0},
    {0.0113, 0, 4, -1, 0}, {0.0047, 0, 2, 0, 2}, {0.0036, 0, 1, 1, 0}, {0.0035, 0, 8, 0, 0},
    {0.0034, 0, 6, -1, 0}, {-0.0034, 0, 2, 0, -2}, {0.0022, 0, 2, -2, 0}, {-0.0017, 0, 3, 0, 0},
    {0.0013, 0, 4, 0, 2}, {0.0011, 0, 8, -1, 0}, {0.0010, 0, 4, -2, 0}, {0.0009, 0, 10, 0, 0},
    {0.0007, 0, 3, 1, 0}, {0.0006, 0, 0, 2, 0}, {0.0005, 0, 2, 1, 0}, {0.0005, 0, 2, 2, 0},
    {0.0004, 0, 6, 0, 2}, {0.0004, 0, 6, -2, 0}, {0.0004, 0, 10, -1, 0}, {-0.0004, 0, 5, 0, 0},
    {-0.0004, 0, 4, 0, -2}, {0.0003, 0, 0, 1, 2}, {0.0003, 0, 12, 0, 0}, {0.0003, 0, 2, -1, 2},
    {-0.0003, 0, 1, -1, 0},
};

// Tabelle 48.2: Terme für die Äquatorial-Horizontalparallaxe [″] (Kosinus)
static const ApsisTerm PERIGEE_PARALLAX[] = {
    {3629.215, 0, 0, 0, 0}, {63.224, 0, 2, 0, 0}, {-6.990, 0, 4, 0, 0}, {2.834, -0.0071, 2, -1, 0},
    {1.927, 0, 6, 0, 0}, {-1.263, 0, 1, 0, 0}, {-0.702, 0, 8, 0, 0}, {0.696, -0.0017, 0, 1, 0},
    {-0.690, 0, 0, 0, 2}, {-0.629, 0.0016, 4, -1, 0}, {-0.392, 0, 2, 0, -2}, {0.297, 0, 10, 0, 0},
    {0.260, 0, 6, -1, 0}, {0.201, 0, 3, 0, 0}, {-0.161, 0, 2, 1, 0}, {0.157, 0, 1, 1, 0},
    {-0.138, 0, 12, 0, 0}, {-0.127, 0, 8, -1, 0}, {0.104, 0, 2, 0, 2}, {0.104, 0, 2, -2, 0},
    {-0.079, 0, 5, 0, 0}, {0.068, 0, 14, 0, 0}, {0.067, 0, 10, -1, 0}, {0.054, 0, 4, 1, 0},
    {-0.038, 0, 12, -1, 0}, {-0.038, 0, 4, -2, 0}, {0.037, 0, 7, 0, 0}, {-0.037, 0, 4, 0, 2},
    {-0.035, 0, 16, 0, 0}, {-0.030, 0, 3, 1, 0}, {0.029, 0, 1, -1, 0}, {-0.025, 0, 6, 1, 0},
    {0.023, 0, 0, 2, 0}, {0.023, 0, 14, -1, 0}, {-0.023, 0, 2, 2, 0}, {0.022, 0, 6, -2, 0},
    {-0.021, 0, 2, -1, -2}, {-0.020, 0, 9, 0, 0}, {0.019, 0, 18, 0, 0}, {0.017, 0, 6, 0, 2},
    {0.014, 0, 0, -1, 2}, {-0.014, 0, 16, -1, 0}, {0.013, 0, 4, 0, -2}, {0.012, 0, 8, 1, 0},
    {0.011, 0, 11, 0, 0}, {0.010, 0, 5, 1, 0}, {-0.010, 0, 20, 0, 0},
};

static const ApsisTerm APOGEE_PARALLAX[] = {
    {3245.251, 0, 0, 0, 0}, {-9.147, 0, 2, 0, 0}, {-0.841, 0, 1, 0, 0}, {0.697, 0, 0, 0, 2},
    {-0.656, 0.0016, 0, 1, 0}, {0.355, 0, 4, 0, 0}, {0.159, 0, 2, -1, 0}, {0.127, 0, 1, 1, 0},
    {0.065, 0, 4, -1, 0}, {0.052, 0, 6, 0, 0}, {0.043, 0, 2, 1, 0}, {0.031, 0, 2, 0, 2},
    {-0.023, 0, 2, 0, -2}, {0.022, 0, 2, -2, 0}, {0.019, 0, 2, 2, 0}, {-0.016, 0, 0, 2, 0},
    {0.014, 0, 6, -1, 0}, {0.010, 0, 8, 0, 0},
};

template <size_t N>
static double sumApsisTerms(const ApsisTerm (&terms)[N], const double T,
                            const Angle D, const Angle M, const Angle F, const bool useCos) {
    double sum = 0;
    for (const ApsisTerm& t : terms) {
        const Angle arg = t.nD * D + t.nM * M + t.nF * F;
        sum += (t.a0 + t.aT * T) * (useCos ? cos(arg) : sin(arg));
    }
    return sum;
}

// Perigäum (k ganzzahlig) oder Apogäum (k + 0.5) nach Kapitel 48.
static Apsis moonApsisForK(const double k, const ApsisType type) {
    // Formel 48.3 [Julianische Jahrhunderte]
    const double T = k / 1325.55;

    // Formel 48.1: Zeit des mittleren Perigäums bzw. Apogäums [JDE]
    const double jdeMean = 2451534.6698 + 27.55454988 * k + T * T * horner(T, -0.0006886, -0.000001098, 0.0000000052);

    // D, M, F (Koeffizienten in Grad)
    const Angle D = Angle::fromDeg(171.9179 + 335.9106046 * k + T * T * horner(T, -0.0100250, -0.00001156, 0.000000055));
    const Angle M = Angle::fromDeg(347.3477 + 27.1577721 * k + T * T * horner(T, -0.0008323, -0.0000010));
    const Angle F = Angle::fromDeg(316.6109 + 364.5287911 * k + T * T * horner(T, -0.0125131, -0.0000148));

    const bool perigee = type == ApsisType::Periapsis;
    const double jde = jdeMean + (perigee ? sumApsisTerms(PERIGEE_TIME, T, D, M, F, false)
                                          : sumApsisTerms(APOGEE_TIME, T, D, M, F, false));
    // Äquatoriale Horizontalparallaxe (Tabelle in Bogensekunden)
    const Angle parallax = Angle::fromArcsec(perigee ? sumApsisTerms(PERIGEE_PARALLAX, T, D, M, F, true)
                                                     : sumApsisTerms(APOGEE_PARALLAX, T, D, M, F, true));
    return { .jde = jde, .distance = EARTH_RADIUS / sin(parallax) };
}

/**
 * Erstes Perigäum bzw. Apogäum des Mondes nach jd (Kapitel 48).
 * Meeus gibt die Genauigkeit des Zeitpunkts mit bis zu 31 min (Perigäum) bzw. 3 min (Apogäum) an.
 * @param jd Julianischer Tag
 * @param type Perigäum (Periapsis) oder Apogäum (Apoapsis)
 * @return Zeitpunkt [JDE] und Entfernung [km]
 */
Apsis calculateNextMoonApsis(const JulianDay jd, const ApsisType type) {
    // k zählt die anomalistischen Monate ab dem Perigäum 1999 Dezember 22 (Formel 48.1 nach k umgestellt)
    double k = floor((jd.value() - 2451534.6698) / 27.55454988) - 1;
    if (type == ApsisType::Apoapsis) k += 0.5;
    Apsis apsis = moonApsisForK(k, type);
    while (apsis.jde <= jd.value()) {
        k += 1;
        apsis = moonApsisForK(k, type);
    }
    return apsis;
}

// Knotendurchgang (k ganzzahlig: aufsteigend, k + 0.5: absteigend) nach Kapitel 49 [JDE].
static double moonNodeForK(const double k) {
    // [Julianische Jahrhunderte]
    const double T = k / 1342.23;

    // D, M, M′, Ω, V, P (Koeffizienten in Grad)
    const Angle D     = Angle::fromDeg(183.6380 + 331.73735691 * k + T * T * horner(T, 0.0015057, 0.00000209, -0.000000010));
    const Angle M     = Angle::fromDeg(17.4006 + 26.82037250 * k + T * T * horner(T, 0.0000999, 0.00000006));
    const Angle Mp    = Angle::fromDeg(38.3776 + 355.52747322 * k + T * T * horner(T, 0.0123577, 0.000014628, -0.000000069));
    const Angle Omega = Angle::fromDeg(123.9767 - 1.44098949 * k + T * T * horner(T, 0.0020625, 0.00000214, -0.000000016));
    const Angle V     = Angle::fromDeg(horner(T, 299.75, 132.85, -0.009173));
    const Angle P     = Omega + Angle::fromDeg(horner(T, 272.75, -2.3));
    // Formel 45.6 [1]
    const double E    = horner(T, 1.0, -0.002516, -0.0000074);

    return 2451565.1619 + 27.212220817 * k + T * T * horner(T, 0.0002572, 0.000000021, -0.000000000088)
         - 0.4721 * sin(Mp)
         - 0.1649 * sin(2 * D)
         - 0.0868 * sin(2 * D - Mp)
         + 0.0084 * sin(2 * D + Mp)
         - 0.0083 * sin(2 * D - M) * E
         - 0.0039 * sin(2 * D - M - Mp) * E
         + 0.0034 * sin(2 * Mp)
         - 0.0031 * sin(2 * D - 2 * Mp)
         + 0.0030 * sin(2 * D + M) * E
         + 0.0028 * sin(M - Mp) * E
         + 0.0026 * sin(M) * E
         + 0.0025 * sin(4 * D)
         + 0.0024 * sin(D)
         + 0.0022 * sin(M + Mp) * E
         + 0.0017 * sin(Omega)
         + 0.0014 * sin(4 * D - Mp)
         + 0.0005 * sin(2 * D + M - Mp) * E
         + 0.0004 * sin(2 * D - M + Mp) * E
         - 0.0003 * sin(2 * D - 2 * M) * E
         + 0.0003 * sin(4 * D - M) * E
         + 0.0003 * sin(V)
         + 0.0003 * sin(P);
}

/**
 * Erster Durchgang des Mondes durch einen Knoten seiner Bahn nach jd (Kapitel 49).
 * @param jd Julianischer Tag
 * @return Zeitpunkt [JDE] und Art des Knotens
 */
NodePassage calculateNextMoonNode(const JulianDay jd) {
    // k zählt die drakonitischen Monate ab dem aufsteigenden Knoten 2000 Januar 21
    double k = floor((jd.value() - 2451565.1619) / 27.212220817) - 1;
    double jde = moonNodeForK(k);
    while (jde <= jd.value()) {
        k += 0.5;
        jde = moonNodeForK(k);
    }
    return { .jde = jde, .ascending = k == floor(k) };
}

// ---------------------------------------------------------------
//  Mondauf-/-untergang (eigenes Verfahren, nicht aus dem Buch)
// ---------------------------------------------------------------

namespace {

// Parabel p(u) = A·u² + B·u + C
struct Parabola {
    double A, B, C;
};

// Parabel durch die Punkte (0, y0), (1, y1), (2, y2).
// Herleitung über die Lagrange-Darstellung
//   p(u) = y0·(u−1)(u−2)/2 − y1·u(u−2) + y2·u(u−1)/2
// und Ausmultiplizieren nach Potenzen von u.
Parabola parabolaThrough(const double y0, const double y1, const double y2) {
    return {
        .A = 0.5 * (y0 - 2.0 * y1 + y2),
        .B = 0.5 * (-3.0 * y0 + 4.0 * y1 - y2),
        .C = y0,
    };
}

double parabolaValue(const Parabola& p, const double u) {
    return horner(u, p.C, p.B, p.A);
}

// Reelle Nullstellen r1 ≤ r2 der Parabel (A ≠ 0)
struct ParabolaRoots {
    bool real;          // false: keine reellen Nullstellen
    double r1, r2;
};

ParabolaRoots parabolaRoots(const Parabola& p) {
    const double discriminant = p.B * p.B - 4.0 * p.A * p.C;
    if (discriminant < 0.0) {
        return { .real = false, .r1 = 0, .r2 = 0 };
    }
    // Auslöschungsfreie Form der Lösungsformel
    const double q = -0.5 * (p.B + copysign(sqrt(discriminant), p.B));
    double r1 = q / p.A;
    double r2 = (q != 0.0) ? p.C / q : r1;
    if (r1 > r2) std::swap(r1, r2);
    return { .real = true, .r1 = r1, .r2 = r2 };
}

// Nullstelle der Parabel im Intervall [0, 1].
// Voraussetzung: p(0) und p(1) haben unterschiedliche Vorzeichen.
double parabolaRootInUnitInterval(const Parabola& p) {
    // Rückfall: lineare Interpolation zwischen den Intervallgrenzen
    const double p1 = parabolaValue(p, 1.0);
    const double linear = p.C / (p.C - p1);

    if (fabs(p.A) < 1e-12) {
        return (fabs(p.B) < 1e-12) ? linear : -p.C / p.B;
    }
    const ParabolaRoots roots = parabolaRoots(p);
    if (!roots.real) {
        return linear;
    }
    for (const double r : { roots.r1, roots.r2 }) {
        if (r >= -1e-9 && r <= 1.0 + 1e-9) {
            return r;
        }
    }
    return linear;
}

// Zustand des Mondes zu einem Zeitpunkt, bezogen auf Horizont und Meridian.
// Die Parabeln rechnen mit den Werten in Radiant.
struct MoonSample {
    double altitude;    // Höhe über der Aufgangshöhe h0 [rad] (> 0: sichtbar)
    double hourAngle;   // Stundenwinkel [rad], (−π, π]
};

MoonSample sampleMoon(const JulianDay jd, const GeoLocation& location) {
    const Ecliptic moon = calculateMoonPosition(jd);
    const Equatorial equ = calculateEquatorial(moon, calculateObliquity(jd));
    const SiderealTime theta = calculateLocalSiderealTime(jd, location);
    const Horizontal hor = calculateHorizontal(equ, theta, location);

    // Standardhöhe des Mondes h0 = 0.7275·π − 0°34′ (Kapitel 14) für die geozentrische Position;
    // berücksichtigt Horizontalparallaxe π, Halbmesser und Refraktion.
    const Angle parallax = Angle::asin(EARTH_RADIUS / moon.distance);
    const Angle h0 = 0.7275 * parallax - 0.5667_deg;

    return { .altitude = (hor.altitude - h0).rad(), .hourAngle = (theta - equ.ra).normalizedSigned().rad() };
}

// Stetige Fortsetzung eines Stundenwinkels relativ zu einem Bezugswert [rad]
double continueHourAngle(const double reference, const double hourAngle) {
    return reference + Angle::fromRad(hourAngle - reference).normalizedSigned().rad();
}

} // namespace

/**
 * Mondauf- und -untergang, Meridiandurchgang und höchster Stand für den UT-Tag, der jd enthält.
 * Die Mondhöhe wird stündlich berechnet. Für jedes Stundenintervall [k, k+1] wird eine
 * Parabel durch die Stützstellen k, k+1, k+2 gelegt, mit der Horizont- und Meridiandurchgänge
 * sowie der Scheitel innerhalb des Intervalls bestimmt werden.
 * @param jd Julianischer Tag
 * @param location Beobachtungsort
 * @return Ereignisse [h UT] und höchste Höhe über h0
 */
RiseTransitSet calculateMoonRiseSet(const JulianDay jd, const GeoLocation& location) {
    const JulianDay midnight = jd.midnight();
    auto sampleAt = [&](int hour) {
        return sampleMoon(midnight + hour / 24.0, location);
    };

    // Gleitendes Fenster aus drei aufeinanderfolgenden Stunden k, k+1, k+2
    MoonSample s0 = sampleAt(0);
    MoonSample s1 = sampleAt(1);

    RiseTransitSet result = {};
    double maxAltitude = s0.altitude;   // [rad]
    result.maxAltitudeTime = 0.0;

    for (int k = 0; k < 24; k++) {
        const MoonSample s2 = sampleAt(k + 2);
        const Parabola altitude = parabolaThrough(s0.altitude, s1.altitude, s2.altitude);

        // Horizont in [k, k+1]. Die Anzahl der Durchgänge richtet sich nach den Vorzeichen der
        // Stützstellen, damit Nullstellen an der Intervallgrenze weder doppelt noch gar nicht zählen.
        const bool belowAtStart = s0.altitude < 0.0;
        if (belowAtStart != (s1.altitude < 0.0)) {
            // Vorzeichenwechsel: genau ein Durchgang
            const double hours = k + parabolaRootInUnitInterval(altitude);
            if (belowAtStart && !result.hasRise) {
                result.hasRise = true;
                result.rise = hours;
            } else if (!belowAtStart && !result.hasSet) {
                result.hasSet = true;
                result.set = hours;
            }
        } else if (fabs(altitude.A) > 1e-12) {
            // Gleiches Vorzeichen an beiden Enden: zwei Durchgänge, wenn der Scheitel im Intervall
            // auf der anderen Seite des Horizonts liegt (Mond streift den Horizont)
            const double u = -altitude.B / (2.0 * altitude.A);
            const double vertex = parabolaValue(altitude, u);
            if (u > 0.0 && u < 1.0 && (vertex < 0.0) != belowAtStart) {
                const ParabolaRoots roots = parabolaRoots(altitude);
                if (roots.real) {
                    const double riseHours = k + (belowAtStart ? roots.r1 : roots.r2);
                    const double setHours  = k + (belowAtStart ? roots.r2 : roots.r1);
                    if (!result.hasRise) {
                        result.hasRise = true;
                        result.rise = riseHours;
                    }
                    if (!result.hasSet) {
                        result.hasSet = true;
                        result.set = setHours;
                    }
                }
            }
        }

        // Meridian: Stundenwinkel wechselt von negativ nach positiv (nicht der Sprung bei ±π)
        if (!result.hasTransit && s0.hourAngle < 0.0 && s1.hourAngle >= 0.0
            && fabs(s0.hourAngle) < M_PI / 2 && fabs(s1.hourAngle) < M_PI / 2) {
            // Stundenwinkel relativ zur mittleren Stützstelle stetig fortsetzen
            const double w0 = continueHourAngle(s1.hourAngle, s0.hourAngle);
            const double w2 = continueHourAngle(s1.hourAngle, s2.hourAngle);
            result.hasTransit = true;
            result.transit = k + parabolaRootInUnitInterval(parabolaThrough(w0, s1.hourAngle, w2));
        }

        // Höchster Stand: Stützstelle k+1 und Parabelscheitel innerhalb des Fensters (und des Tages)
        if (s1.altitude > maxAltitude) {
            maxAltitude = s1.altitude;
            result.maxAltitudeTime = k + 1;
        }
        if (altitude.A < 0.0) {
            const double u = -altitude.B / (2.0 * altitude.A);
            const double vertex = parabolaValue(altitude, u);
            if (u > 0.0 && u < 2.0 && k + u <= 24.0 && vertex > maxAltitude) {
                maxAltitude = vertex;
                result.maxAltitudeTime = k + u;
            }
        }

        s0 = s1;
        s1 = s2;
    }
    result.maxAltitude = Angle::fromRad(maxAltitude);
    return result;
}

// ---------------------------------------------------------------
//  Matrix-Operationen
// ---------------------------------------------------------------

/**
 * Rotationsmatrix für eine Drehung um eine Achse (Rodrigues-Formel).
 * @param axle Drehachse (beliebige Länge)
 * @param angle Drehwinkel
 * @return Rotationsmatrix
 */
Mat3 createRotationMatrix(const Vec3& axle, const Angle angle) {
    const double cosA = cos(angle);
    const double sinA = sin(angle);

    const double len = sqrt(axle.x * axle.x + axle.y * axle.y + axle.z * axle.z);
    const double nx = axle.x / len;
    const double ny = axle.y / len;
    const double nz = axle.z / len;

    const double f = 1 - cosA;
    return {{
        {nx * nx * f + cosA,      nx * ny * f - nz * sinA, nx * nz * f + ny * sinA},
        {nx * ny * f + nz * sinA, ny * ny * f + cosA,      ny * nz * f - nx * sinA},
        {nx * nz * f - ny * sinA, ny * nz * f + nx * sinA, nz * nz * f + cosA},
    }};
}

/**
 * Produkt zweier 3x3-Matrizen m1·m2.
 */
Mat3 operator*(const Mat3& m1, const Mat3& m2) {
    Mat3 result{};
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
 * Matrix angewandt auf einen Vektor: matrix·point.
 */
Vec3 operator*(const Mat3& matrix, const Vec3& point) {
    return {
        .x = point.x * matrix.m[0][0] + point.y * matrix.m[0][1] + point.z * matrix.m[0][2],
        .y = point.x * matrix.m[1][0] + point.y * matrix.m[1][1] + point.z * matrix.m[1][2],
        .z = point.x * matrix.m[2][0] + point.y * matrix.m[2][1] + point.z * matrix.m[2][2],
    };
}

} // namespace astro
