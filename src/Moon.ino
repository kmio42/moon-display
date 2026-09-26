#include <Arduino.h>
#include <Adafruit_GC9A01A.h>

#include "astro.h"
#include "ephemeris.h"
#include "moon_render.h"

// Standort (Dezimalgrad) und Darstellungsoptionen – persistent in Config.ino
extern double configLatitude;
extern double configLongitude;
extern int    configDisplayOptions;

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

void calculateMoon(const struct tm& time, bool printInfo, Adafruit_GC9A01A* tft = nullptr) {

    if(tft != nullptr) {
        EVAL_START();
        prepareMoonRenderAt(time);
        drawMoon(tft);
        EVAL_END("drawMoon");
    }

    EVAL_PRINT_TOTAL();


    if (printInfo) {
        SkyState sky = computeSky(time);
        double rot, mask;
        moonRenderAngles(sky, rot, mask);
        Serial.printf("  Julianisches Datum:       %f\n", sky.jd);
        Serial.printf("  Mondposition RA/Dek:      %6.2f° / %6.2f°\n", sky.moonRaDek.ra * astro::RAD2DEG, sky.moonRaDek.dek * astro::RAD2DEG);
        Serial.printf("  Mondposition Azimut/Höhe: %6.2f° / %6.2f°\n", sky.moonAzH.azimut * astro::RAD2DEG, sky.moonAzH.height * astro::RAD2DEG);
        Serial.printf("  Mondlibration:            %6.2f° / %6.2f°\n", sky.moonAxle.libration.longitude * astro::RAD2DEG, sky.moonAxle.libration.latitude * astro::RAD2DEG);
        Serial.printf("  Mondachse:                %6.2f°\n", sky.moonAxle.axle * astro::RAD2DEG);
        Serial.printf("  Mondphase (0-1):          %6.4f\n", sky.phase);
        Serial.printf("  Mondrand:                 %6.2f°\n", sky.chi * astro::RAD2DEG);
        Serial.printf("  Parallaktischer Winkel:   %6.2f°\n", sky.q * astro::RAD2DEG);
        Serial.printf("  Sternzeit:                %6.2f°\n", sky.gmstHours);
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

// Zeichnet den mit prepareMoonRender()/prepareMoonRenderAt() vorbereiteten Mond aufs Display.
void drawMoon(Adafruit_GC9A01A* tft) {
  const int r = MOON_RADIUS_PX;
  for (int y = -r; y < r; y++) {
    for (int x = -r; x < r; x++) {
      if (x * x + y * y > r * r) {
        // Pixel außerhalb des Mondkreises, also Hintergrund
        // Da Display rund ist, muss hier kein Wert zugewiesen werden
        continue;
      }
      tft->drawPixel(x + r, y + r, moonPixel(x, y));
    }
    yield();
  }
}
