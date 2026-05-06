#include <Arduino.h>
#include <Adafruit_GC9A01A.h>

#include "astro.h"

// Standort (Dezimalgrad)
constexpr double LATITUDE  =  49.821;
constexpr double LONGITUDE =   8.869;

extern const uint16_t FullMoon[];
extern const uint16_t lroc[];

// Darstellungsoptionen (bitweise kombinierbar)
constexpr int OPTION_NONE = 0;
constexpr int OPTION_DARKEN_UNLIT = 1; // Unbeleuchteten Bereich zusätzlich abdunkeln (schwacher Schein um den Mond)
constexpr int OPTION_BLUISH_TINT = 2; // Bläuliche Tönung - wenn unter Horizont
constexpr int OPTION_USE_NASA_MODEL = 4; // Textur aus lroc[] verwenden (statt Vollmond-Textur)
constexpr int OPTION_USE_LIBRATION = 8; // Librationen berücksichtigen

// Expliziter Prototyp, da der automatische .ino-Prototyp bei Referenztypen fehlschlagen kann.
void drawMoon(Adafruit_GC9A01A* tft, float phase, float rotation, float mask, int options, const astro::Mat3 &rotMatrix);

// ── Timing-Makros für die Performance-Analyse ─────────────────────────────────────────
#if 0
unsigned long tStart, tEnd, tTotal;
#define EVAL_START() tStart = micros();
#define EVAL_END(label) do { \
        tEnd = micros(); \
        tTotal += tEnd - tStart; \
        Serial.printf("  %s: %lu µs\n", label, tEnd - tStart); \
    } while(0);
#define EVAL_RESET() tTotal = 0;
#define EVAL_PRINT_TOTAL() Serial.printf("  ── Gesamt: %lu µs\n", tTotal);
#else
#define EVAL_START()
#define EVAL_END(label)
#define EVAL_RESET()
#define EVAL_PRINT_TOTAL()
#endif

static double julianDateVonTm(const struct tm& t) {
    return astro::calculateJulianDate(
        t.tm_year + 1900,
        t.tm_mon  + 1,
        t.tm_mday,
        t.tm_hour,
        t.tm_min,
        t.tm_sec,
        0
    );
}

void calculateMoon(const struct tm& time, bool printInfo, Adafruit_GC9A01A* tft = nullptr) {

    EVAL_START();
    double jd = julianDateVonTm(time);
    EVAL_END("Julianisches Datum");

    // Lokale Sternzeit in Radiant (GMST + Längengrad-Offset)
    EVAL_START();
    double gmstStunden  = astro::calculateSiderealTime(jd);
    double siderealTime = gmstStunden * M_PI / 12.0 + LONGITUDE * astro::DEG2RAD;
    EVAL_END("Sternzeit");

    // ── Sonnenposition ───────────────────────────────────────────────────────
    EVAL_START();
    double eklLaenge    = astro::calculateEclipticalLength(jd);

    astro::RaDek sunRaDek = astro::calculateRaDek(eklLaenge, 0.0);
    double sun_distance = 149597870;  // in km (1 AE)
    EVAL_END("Sonnenposition");

    // ── Mondposition ─────────────────────────────────────────────────────────
    EVAL_START();
    astro::MoonPosition moon = astro::calculateMoon(jd);

    astro::RaDek moonRaDek = astro::calculateRaDek(moon.longitude, moon.latitude);
    double moonParallax = asin(6378.14/moon.distance);

    moonRaDek = astro::calculateParallax(moonRaDek, moonParallax, siderealTime, LATITUDE * astro::DEG2RAD);
    EVAL_END("Mondposition RA/Dek");

    // ── Mondachse & Libration ────────────────────────────────────────────────
    EVAL_START();
    astro::MoonAxle mondAchse = astro::calculateMoonAxle(jd, moon);
    EVAL_END("Mondachse/Libration");

    // ── Mondphase und parallaktischer Winkel ──────────────────────────────────
    EVAL_START();

    double phase = calculateMoonPhase(sunRaDek, sun_distance, moonRaDek, moon.distance);

    // Positionswinkel (Mitte) des beleuchteten Mondrandes
    // Formel 46.5 aus "Astronomische Algorithmen, 2. Auflage" von Jean Meeus.
    double chi = atan2(cos(sunRaDek.dek) * sin(sunRaDek.ra - moonRaDek.ra),
                       sin(sunRaDek.dek) * cos(moonRaDek.dek)
                     - cos(sunRaDek.dek) * sin(moonRaDek.dek) * cos(sunRaDek.ra - moonRaDek.ra));


    // ── Parallaktischer Winkel für Mond (Ortsabhängig) ─────────────────────────
    EVAL_START();
    double q = calculateParallacticAngle(moonRaDek, siderealTime, LATITUDE * astro::DEG2RAD);
    EVAL_END("Mondphase & Parallaktischer Winkel");

    // Zenitwinkel des hellen Mondrandes: chi - q
    double mask = (chi - q + M_PI/2);

    double rot = (mondAchse.axle-q + 0.004919 - 0.116413461); //ermittelte Korrektur mit Stellarium

    astro::AzimutHeight moonAzimutHeight = astro::calculateHAzFromRaDek(moonRaDek, siderealTime, LATITUDE  * astro::DEG2RAD);

    const astro::Mat3 axleRot = astro::createRotationMatrix({0, 0, 1}, -mondAchse.axle + q); // -0.389615 ermittelte Korrektur mit Stellarium
    const astro::Mat3 libLatitudeRot = astro::createRotationMatrix({1, 0, 0}, -mondAchse.libration.latitude);
    const astro::Mat3 libLongitudeRot = astro::createRotationMatrix({0, -1, 0}, -mondAchse.libration.longitude);
    astro::Mat3 rotMatrix = astro::multiplyMatrix(libLongitudeRot, libLatitudeRot);
                rotMatrix = astro::multiplyMatrix(rotMatrix, axleRot);

    if(tft != nullptr) {
        EVAL_START();
        int options = OPTION_DARKEN_UNLIT;
        if (moonAzimutHeight.height < 0) {
            options |= OPTION_BLUISH_TINT;
        }
        drawMoon(tft, phase, rot, mask, options, rotMatrix);
        EVAL_END("drawMoon");
    }

    EVAL_PRINT_TOTAL();


    if (printInfo) {
        Serial.printf("  Julianisches Datum:       %f\n", jd);
        Serial.printf("  Mondposition RA/Dek:      %6.2f° / %6.2f°\n", moonRaDek.ra * astro::RAD2DEG, moonRaDek.dek * astro::RAD2DEG);
        Serial.printf("  Mondposition Azimut/Höhe: %6.2f° / %6.2f°\n", moonAzimutHeight.azimut * astro::RAD2DEG, moonAzimutHeight.height * astro::RAD2DEG);
        Serial.printf("  Mondlibration:            %6.2f° / %6.2f°\n", mondAchse.libration.longitude * astro::RAD2DEG, mondAchse.libration.latitude * astro::RAD2DEG);
        Serial.printf("  Mondachse:                %6.2f°\n", mondAchse.axle * astro::RAD2DEG);
        Serial.printf("  Mondphase (0-1):          %6.4f\n", phase);
        Serial.printf("  Mondrand:                 %6.2f°\n", (chi) * astro::RAD2DEG);
        Serial.printf("  Parallaktischer Winkel:   %6.2f°\n", q * astro::RAD2DEG);
        Serial.printf("  Sternzeit:                %6.2f°\n", gmstStunden);
        Serial.printf(" rot:                      %f\n", rot);
        Serial.printf(" mask:                     %f\n", mask);
    }
}

// ── Sonnenauf- / -untergang ──────────────────────────────────────────────────

static void printHHMM(const char* label, double decimalHours) {
    double h = fmod(decimalHours, 24.0);
    if (h < 0) h += 24.0;
    int hh = (int)h;
    int mm = (int)((h - hh) * 60.0 + 0.5);
    if (mm == 60) { hh = (hh + 1) % 24; mm = 0; }
    Serial.printf("  %s %02d:%02d UTC\n", label, hh, mm);
}

void berechneSonnenaufgang() {
    struct tm zeitInfo;
    if (!getLocalTime(&zeitInfo)) {
        Serial.println("Keine gültige NTP-Zeit für Sonnenberechnung.");
        return;
    }
    double jd = astro::calculateJulianDate(
        zeitInfo.tm_year + 1900,
        zeitInfo.tm_mon  + 1,
        zeitInfo.tm_mday);

    astro::SunriseSunset s = astro::calculateSunriseSunset(jd, LATITUDE, LONGITUDE);

    if (!s.valid) {
        Serial.println("  Sonnenauf/-untergang: Polartag oder Polarnacht.");
        return;
    }
    printHHMM("Sonnenaufgang:", s.rising + 2);
    printHHMM("Sonnenuntergang:", s.setting + 2);
}

// ── Mondauf- / -untergang ────────────────────────────────────────────────────

// void berechneMondaufgang() {
//     struct tm zeitInfo;
//     if (!getLocalTime(&zeitInfo)) {
//         Serial.println("Keine gültige NTP-Zeit für Mondberechnung.");
//         return;
//     }
//     double jd0 = astro::calculateJulianDate(
//         zeitInfo.tm_year + 1900,
//         zeitInfo.tm_mon  + 1,
//         zeitInfo.tm_mday);
//     jd0 = round(jd0) - 0.5;  // Mitternacht UTC

//     // Mondpositionen für jd0-1, jd0, jd0+1
//     astro::MoonPosition mp1 = astro::calculateMoon(jd0 - 1);
//     astro::MoonPosition mp2 = astro::calculateMoon(jd0);
//     astro::MoonPosition mp3 = astro::calculateMoon(jd0 + 1);
//     astro::RaDek r1 = astro::calculateRaDek(mp1.longitude, mp1.latitude);
//     astro::RaDek r2 = astro::calculateRaDek(mp2.longitude, mp2.latitude);
//     astro::RaDek r3 = astro::calculateRaDek(mp3.longitude, mp3.latitude);

//     // RA in Grad, Sprünge um ±360° bei 0h/24h korrigieren
//     double a1 = r1.ra * astro::RAD2DEG;
//     double a2 = r2.ra * astro::RAD2DEG;
//     double a3 = r3.ra * astro::RAD2DEG;
//     if      (a2 - a1 >  180.0) a1 += 360.0;
//     else if (a2 - a1 < -180.0) a1 -= 360.0;
//     if      (a3 - a2 >  180.0) a3 -= 360.0;
//     else if (a3 - a2 < -180.0) a3 += 360.0;

//     double d1 = r1.dek * astro::RAD2DEG;
//     double d2 = r2.dek * astro::RAD2DEG;
//     double d3 = r3.dek * astro::RAD2DEG;

//     // Horizontalparallaxe → Standard-Aufgangshöhe (Meeus Kap. 15)
//     double parallax = asin(astro::EARTH_RADIUS / mp2.distance) * astro::RAD2DEG;
//     double h0 = 0.7275 * parallax - 0.5667;

//     // Greenwicher Sternzeit bei Mitternacht (Grad)
//     double theta0 = astro::calculateSiderealTime(jd0) * 180.0 / 12.0;

//     // Anfangsschätzung über Stundenwinkel bei jd0
//     double cH0 = (sin(h0 * astro::DEG2RAD) - sin(LATITUDE * astro::DEG2RAD) * sin(r2.dek))
//                / (cos(LATITUDE * astro::DEG2RAD) * cos(r2.dek));
//     if (cH0 < -1.0 || cH0 > 1.0) {
//         Serial.println("  Mondauf/-untergang: Mond geht heute nicht auf oder nicht unter.");
//         return;
//     }
//     double H0 = acos(cH0) * astro::RAD2DEG;

//     // Startwerte m ∈ [0,1] = Bruchteil des Tages
//     double m0 = fmod((a2 - LONGITUDE - theta0) / 360.0 + 1.0, 1.0);
//     double m1 = fmod(m0 - H0 / 360.0 + 1.0, 1.0);
//     double m2 = fmod(m0 + H0 / 360.0 + 1.0, 1.0);

//     double jd_set = jd0+fmod(m2 + 1, 1.0);
//     double gmstStunden  = astro::calculateSiderealTime(jd_set);
//     double siderealTime = gmstStunden * M_PI / 12.0 + LONGITUDE * astro::DEG2RAD;

//     astro::MoonPosition moon1 = astro::calculateMoon(jd_set);
//     astro::RaDek r_set = astro::calculateRaDek(moon1.longitude, moon1.latitude);
//     astro::AzimutHeight moonAzimutHeight1 = astro::calculateHAzFromRaDek(r_set, siderealTime, LATITUDE  * astro::DEG2RAD);

//     printHHMM("Mondaufgang:    ", fmod(m1 + 1.0, 1.0) * 24.0);
//     printHHMM("Mondkulmination:", fmod(m0 + 1.0, 1.0) * 24.0);
//     printHHMM("Monduntergang:  ", fmod(m2 + 1.0, 1.0) * 24.0);

//     Serial.printf("  Monduntergang Azimut/Höhe: %f° / %f°\n", moonAzimutHeight1.azimut * astro::RAD2DEG, moonAzimutHeight1.height * astro::RAD2DEG);
// }

// ── Display ──────────────────────────────────────────────────────────────────

void drawMoon(Adafruit_GC9A01A* tft, float phase, float rotation, float mask, int options, const astro::Mat3 &rotMatrix) {

  // Bildet den Mond auf einem 240x240 Pixel großen Kreis ab.
  // Es wird eine Textur (FullMoon) verwendet, die bei Vollmond vollständig sichtbar ist.
  // Die Textur stammt von einem echten Foto des Vollmondes. Libration wird nicht berücksichtigt.
  // Der Parameter "phase" steuert die Beleuchtung (0 = Neumond, 1 = Vollmond).
  // "rotation" gibt den Winkel der Textur an
  // "mask" gibt den Winkel der Maskierung an, um die Richtung der Beleuchtung zu steuern (z.B. zunehmender oder abnehmender Mond).

  // Grundidee:
  // Zunächst wird nur die Form der beleuchteten Fläche betrachtet, ohne Drehung (zunehmender oder abnehmender Mond) und ohne Textur.
  // Die Form der beleuchteten Fläche wird durch die Kombination von folgenden geometrischen Formen definiert:
  // 1. Ein Kreis (Radius r), bildet die Grundform des Mondes ab. Alle Pixel innerhalb dieses Kreises gehören zum Mond.
  // 2. Eine Ellipse mit Hauptachse Nord-Süd des Mondes definiert den Terminator zwischen beleuchteten und unbeleuchteten Bereich.
  //    Die Ellipse ist entweder Teil des beleuchteten Bereichs (Phase > 0.5) oder des unbeleuchteten Bereichs (Phase <= 0.5).
  //    Ist sie Teil des beleuchteten Bereichs, so ist alles vom westlichen Rand der Ellipse bis zum östlichen Rand des Kreises beleuchtet.
  //    Ist sie Teil des unbeleuchteten Bereichs, so ist alles vom östlichen Rand der Ellipse bis östlichen Rand des Kreises unbeleuchtet.
  // Durch die Kombination von Kreis- und Ellipsengleichungen wird entschieden, ob ein Pixel beleuchtet ist oder nicht.

  // Die Rotationen wird ausgehend vom Zielbild rückwärts auf die Pixelkoordinaten angewandt
  // So wird für jedes Pixel im Zielbild berechnet, ob ein Pixel beleuchtet ist oder nicht, und welcher Punkt aus der Textur verwendet werden soll.

  int r = 120;
  int b = r * r; // Nord-Süd-Halbachse der Ellipse
  int a = r * r; // Ost-West-Halbachse der Ellipse, wird später mit dem Phasenparameter skaliert
  if (phase > 0.5) {
    a *= (phase * 2 - 1)*(phase * 2 - 1);
  } else {
    a *= (1 - phase * 2)*(1 - phase * 2);
  }
  // Vorberechnung der Rotationsparameter für die Maskierung und die Texturkoordinaten
  float cosMask = cos(-mask);
  float sinMask = sin(-mask);
  float cosRot = cos(-rotation);
  float sinRot = sin(-rotation);

  for (int y = -r; y < r; y++) {
    for (int x = -r; x < r; x++) {
      int circle = x * x + y * y - r * r;
      if (circle > 0) {
        // Pixel außerhalb des Mondkreises, also Hintergrund
        // Da Display rund ist, muss hier kein Wert zugewiesen werden
        continue;
      }
      // conditionX und conditionY sind die Koordinaten des Pixels bezogen auf Basis-Mondform
      int conditionX = x * cosMask + y * sinMask;
      int conditionY = -x * sinMask + y * cosMask;
      // Punkt (x,y) liegt innerhalb der Ellipse, wenn x^2/b + y^2/a <= 1 ist, bzw. x^2*b + y^2*a - a*b <= 0
      int ellipse = conditionX * conditionX * b + conditionY * conditionY * a - a * b;

      bool pixelActive = false;
      if (phase > 0.5) {
        //gesamter östlicher Teil des Kreises ist beleuchtet (conditionX >= 0),
        //zusätzlich ist der westliche Teil der Ellipse beleuchtet (ellipse <= 0)
        pixelActive = conditionX >= 0 || ellipse <= 0;
      } else {
        //Nur der Teil beleuchtet, der östlich ist und außerhalb der Ellipse liegt
        pixelActive = conditionX >= 0 && ellipse > 0;
      }

      uint16_t pixelColor = 0;


      if(pixelActive || (options & OPTION_DARKEN_UNLIT)) {

        if(options & OPTION_USE_NASA_MODEL) {
          // Berechnung der Texturkoordinaten über sphärische Projektion auf die Mondkugel
          double z = sqrt(r*r - x*x - y*y);
          astro::Vec3 point = astro::applyMatrix({(double) x,-(double) y,z},rotMatrix);
          //Normalisieren
          double len = sqrt(point.x * point.x + point.y * point.y + point.z * point.z);
          point.x /= len;
          point.y /= len;
          point.z /= len;
          double lon_moon = atan2(point.x, point.z);
          double lat_moon = asin(std::max(-1.0, std::min(1.0, point.y)));
          int u = (int) ((lon_moon + M_PI) / (2 * M_PI) * 480);
          int v = (int) ((M_PI/2 - lat_moon) / M_PI * 240);
          memcpy_P(&pixelColor, &lroc[(v*480 + u)], 2);
        } else {
          // Berechnung der Texturkoordinaten
          int pixelX = (x * cosRot + y * sinRot) + r;
          int pixelY = (-x * sinRot + y * cosRot) + r;
          memcpy_P(&pixelColor, &FullMoon[(pixelY*240 + pixelX)], 2);
        }
        // Normalisieren auf 0..1
        float fr = ((pixelColor >> 11) & 0x1F) / 31.0f;
        float fg = ((pixelColor >>  5) & 0x3F) / 63.0f;
        float fb = ( pixelColor        & 0x1F) / 31.0f;

        if(!pixelActive && (options & OPTION_DARKEN_UNLIT)) {
          // Unbeleuchteten Bereich zusätzlich abdunkeln (schwacher Schein um den Mond)
          //fr *= 0.15f;
          //fg *= 0.15f;
          //fb *= 0.15f; 
          fr = fg = fb = (fr + fg + fb) / 3.0f*0.15f;
        }

        if(options & OPTION_BLUISH_TINT) {
          // Bläuliche Tönung, wenn unter Horizont
          fr *= 0.7f;
          fg *= 0.7f;
          fb = fb * 0.8f + 0.14; // Blau leicht aufhellen
          fr = min(fr, 1.0f);
          fg = min(fg, 1.0f);
          fb = min(fb, 1.0f);
        }

        pixelColor = ((uint16_t)(fr * 31.0f + 0.01f) << 11)
                   | ((uint16_t)(fg * 63.0f + 0.01f) <<  5)
                   |  (uint16_t)(fb * 31.0f + 0.01f);
      }
      
      tft->drawPixel(x + r, y + r, pixelColor);

    }
    yield();
  }
}
