#include <Arduino.h>
#include <Adafruit_GC9A01A.h>

#include "astro.h"
#include "ephemeris.h"
#include "moon_render.h"

// Standort (Dezimalgrad) und Darstellungsoptionen – persistent in Config.ino
extern astro::GeoLocation configLocation;
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
        moonRenderAngles(sky, configDisplayOptions, rot, mask);
        Serial.printf("  Julianisches Datum:       %f\n", sky.jd.value());
        Serial.printf("  Mondposition RA/Dek:      %6.2f° / %6.2f°\n", sky.moonRaDek.ra.deg(), sky.moonRaDek.dec.deg());
        Serial.printf("  Mondposition Azimut/Höhe: %6.2f° / %6.2f°\n", sky.moonAzH.azimuth.deg(), sky.moonAzH.altitude.deg());
        Serial.printf("  Mondlibration:            %6.2f° / %6.2f°\n", sky.moonAxle.libration.longitude.deg(), sky.moonAxle.libration.latitude.deg());
        Serial.printf("  Mondachse:                %6.2f°\n", sky.moonAxle.positionAngle.deg());
        Serial.printf("  Mondphase (0-1):          %6.4f\n", sky.phase);
        Serial.printf("  Mondrand:                 %6.2f°\n", sky.chi.deg());
        Serial.printf("  Parallaktischer Winkel:   %6.2f°\n", sky.q.deg());
        Serial.printf("  Sternzeit:                %6.4f h\n", sky.gmst.hours());
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
    const astro::JulianDay jd = astro::JulianDay::fromCalendar(
        zeitInfo.tm_year + 1900,
        zeitInfo.tm_mon  + 1,
        zeitInfo.tm_mday,
        0.0);

    astro::RiseTransitSet s = astro::calculateSunRiseSet(jd, configLocation);

    if (!s.hasRise) {
        Serial.println("  Sonnenauf/-untergang: Polartag oder Polarnacht.");
        return;
    }
    printHHMM("Sonnenaufgang:", s.rise + 2);
    printHHMM("Sonnenuntergang:", s.set + 2);
}

// ── Display ──────────────────────────────────────────────────────────────────

// Zeichnet den mit prepareMoonRender()/prepareMoonRenderAt() vorbereiteten Mond aufs Display,
// zeilenweise in einem Block (setAddrWindow + writePixels) statt einzeln mit drawPixel.
void drawMoon(Adafruit_GC9A01A* tft) {
  const int r = MOON_RADIUS_PX;
  uint16_t row[2 * MOON_RADIUS_PX];
  for (int y = -r; y < r; y++) {
    // Pixel dieser Zeile innerhalb des Mondkreises: −half..half (und < r). Außerhalb muss
    // nichts gezeichnet werden, da das Display rund ist.
    int half = 0;
    while ((half + 1) * (half + 1) + y * y <= r * r) half++;
    const int xStart = -half;
    const int xEnd = std::min(half, r - 1);
    for (int x = xStart; x <= xEnd; x++) {
      row[x - xStart] = moonPixel(x, y);
    }
    tft->drawRGBBitmap(xStart + r, y + r, row, xEnd - xStart + 1, 1);
    yield();
  }
}
