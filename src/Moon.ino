#include <Arduino.h>
#include <Adafruit_GC9A01A.h>

#include "astro.h"

// Standort (Dezimalgrad) und Darstellungsoptionen – persistent in Config.ino
extern double configLatitude;
extern double configLongitude;
extern int    configDisplayOptions;

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
    double siderealTime = gmstStunden * M_PI / 12.0 + configLongitude * astro::DEG2RAD;
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

    moonRaDek = astro::calculateParallax(moonRaDek, moonParallax, siderealTime, configLatitude * astro::DEG2RAD);
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
    double q = calculateParallacticAngle(moonRaDek, siderealTime, configLatitude * astro::DEG2RAD);
    EVAL_END("Mondphase & Parallaktischer Winkel");

    // Zenitwinkel des hellen Mondrandes: chi - q
    double mask = (chi - q + M_PI/2);

    double rot = (mondAchse.axle-q + 0.004919 - 0.116413461); //ermittelte Korrektur mit Stellarium

    astro::AzimutHeight moonAzimutHeight = astro::calculateHAzFromRaDek(moonRaDek, siderealTime, configLatitude  * astro::DEG2RAD);

    const astro::Mat3 libLongitudeRot = astro::createRotationMatrix({0, -1, 0}, -mondAchse.libration.longitude);
    const astro::Mat3 libLatitudeRot = astro::createRotationMatrix({1, 0, 0}, -mondAchse.libration.latitude);
    const astro::Mat3 axleRot = astro::createRotationMatrix({0, 0, 1}, -mondAchse.axle + q);

    astro::Mat3 rotMatrix = multiplyMatrix(libLongitudeRot, libLatitudeRot);
                rotMatrix = multiplyMatrix(rotMatrix, axleRot);

    if(tft != nullptr) {
        EVAL_START();
        int options = configDisplayOptions;
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

    astro::SunriseSunset s = astro::calculateSunriseSunset(jd, configLatitude, configLongitude);

    if (!s.valid) {
        Serial.println("  Sonnenauf/-untergang: Polartag oder Polarnacht.");
        return;
    }
    printHHMM("Sonnenaufgang:", s.rising + 2);
    printHHMM("Sonnenuntergang:", s.setting + 2);
}

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

  astro::Mat3 rotMat = rotMatrix;

  if((options & OPTION_USE_LIBRATION) && !(options & OPTION_USE_NASA_MODEL)) {
    const astro::Mat3 libLongitudeRotCorr = astro::createRotationMatrix({0,-1,0},-2.24*astro::DEG2RAD);
    const astro::Mat3 libLatitudeRotCorr = astro::createRotationMatrix({1,0,0},5.78*astro::DEG2RAD);
    const astro::Mat3 axleRotCorr = astro::createRotationMatrix({0,0,1},9*astro::DEG2RAD);

    astro::Mat3 rotMat_temp = multiplyMatrix(axleRotCorr,libLatitudeRotCorr);
                rotMat_temp = multiplyMatrix(rotMat_temp,libLongitudeRotCorr);
                rotMat = multiplyMatrix(rotMat_temp, rotMatrix);
    for(int i = 0; i < 3; ++i)
      for(int j = 0; j < 3; ++j)
        Serial.printf("rotMat.m[%d][%d] = %f\n", i, j, rotMat.m[i][j]);
  }

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
        //Default-color of moon surface, if pixel is outside of texture or if no texture is used.
        pixelColor = ((uint16_t)(175/255.0f * 31.0f + 0.01f) << 11)
                   | ((uint16_t)(168/255.0f * 63.0f + 0.01f) <<  5)
                   |  (uint16_t)(156/255.0f * 31.0f + 0.01f);

        if(options & OPTION_USE_NASA_MODEL) {
          double z = sqrt(r*r - x*x - y*y);
          astro::Vec3 point = astro::applyMatrix({(double) x,-(double) y,z},rotMat);
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
          int pixelX = 0;
          int pixelY = 0;
          if(options & OPTION_USE_LIBRATION) {
            double z = sqrt(r*r - x*x - y*y);
            astro::Vec3 point = astro::applyMatrix({(double) x,-(double) y,z},rotMat);
            pixelX = point.x+r;
            pixelY = -point.y+r;
            if(point.z > 0) {
              memcpy_P(&pixelColor, &FullMoon[(pixelY*240 + pixelX)], 2);
            }
          } else {
            pixelX = (x * cosRot + y * sinRot) + r;
            pixelY = (-x * sinRot + y * cosRot) + r;
            memcpy_P(&pixelColor, &FullMoon[(pixelY*240 + pixelX)], 2);
          }
        }
        // Normalisieren auf 0..1
        float fr = ((pixelColor >> 11) & 0x1F) / 31.0f;
        float fg = ((pixelColor >>  5) & 0x3F) / 63.0f;
        float fb = ( pixelColor        & 0x1F) / 31.0f;

        if(!pixelActive && (options & OPTION_DARKEN_UNLIT)) {
          // Unbeleuchteten Bereich zusätzlich abdunkeln
          fr *= 0.5f;
          fg *= 0.5f;
          fb *= 0.5f; 
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
