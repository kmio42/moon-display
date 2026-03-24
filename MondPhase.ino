/**
 * MondPhase.ino – ESP32 Arduino Sketch
 *
 * Lädt Uhrzeit per NTP, berechnet Mond- und Sonnenposition mit astro.cpp
 * und ruft drawMoonPhase(radius=240) auf.
 *
 * Vom Nutzer zu ergänzen:
 *   - WIFI_SSID / WIFI_PASSWORD
 *   - LATITUDE / LONGITUDE (Standort in Dezimalgrad)
 *   - outputBuffer  (MoonPhasePixel[480*480])
 *   - moonTexture   (const MoonPhasePixel* oder nullptr)
 *   - texSize       (Kantenlänge der Textur, oder 0)
 *   - Anzeige-Code nach drawMoonPhase()
 */

#include <WiFi.h>
#include <time.h>
#include "astro.h"

#include <Adafruit_GFX.h>     // Core graphics library
#include "Adafruit_GC9A01A.h"
#include <SPI.h>

// ── Konfiguration ────────────────────────────────────────────────────────────

#include "credentials.h"

// Standort (Dezimalgrad)
constexpr double LATITUDE  =  48.137;   // z.B. München
constexpr double LONGITUDE =  11.576;

// NTP
constexpr long   GMT_OFFSET_SEC  = 0;    // UTC verwenden; Zeitzone lokal egal
constexpr int    DAYLIGHT_OFFSET = 0;
constexpr char   NTP_SERVER[]    = "pool.ntp.org";

// Mond-Rendering
constexpr int MOON_RADIUS = 240;         // Pixel

// ── Nutzer-Deklarationen (hier einfügen oder in separater Datei) ─────────────
// Beispiel:
//   static astro::MoonPhasePixel outputBuffer[2 * MOON_RADIUS * 2 * MOON_RADIUS];
//   static const astro::MoonPhasePixel* moonTexture = nullptr;
//   static const int texSize = 0;
//
// Diese drei Variablen müssen HIER oder in einer anderen .ino/.cpp-Datei
// definiert sein, bevor berechneMondPhase() aufgerufen werden kann.


#define TFT_CS 7
#define TFT_DC 10

Adafruit_GC9A01A tft(TFT_CS, TFT_DC);

// ── Hilfsfunktion: Julianisches Datum aus struct tm ─────────────────────────

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

// ── Berechnung & Rendering ──────────────────────────────────────────────────

void berechneMondPhase() {
    // Aktuelle UTC-Zeit holen
    struct tm zeitInfo;
    if (!getLocalTime(&zeitInfo)) {
        Serial.println("Keine gültige NTP-Zeit verfügbar.");
        return;
    }

    double jd = julianDateVonTm(zeitInfo);

    // Lokale Sternzeit in Radiant (GMST + Längengrad-Offset)
    double gmstStunden  = astro::calculateSiderealTime(jd);
    double siderealTime = gmstStunden * M_PI / 12.0 + LONGITUDE * astro::DEG2RAD;

    // ── Sonnenposition ───────────────────────────────────────────────────────
    double eklLaenge    = astro::calculateEclipticalLength(jd);
    double trueAnomaly  = astro::calculateTrueAnomaly(jd);
    double orbitRadius  = astro::calculateOrbitRadiusEarth(trueAnomaly); // in AE

    astro::RaDek sunRaDek = astro::calculateRaDek(eklLaenge, 0.0);
    sunRaDek.distance = orbitRadius * astro::AE_TO_KM;  // in km

    // ── Mondposition ─────────────────────────────────────────────────────────
    astro::MoonPosition moon = astro::calculateMoon(jd);

    astro::RaDek moonRaDekRoh = astro::calculateRaDek(moon.longitude, moon.latitude);

    // Parallaxen-Korrektur
    double mondParallax = asin(astro::EARTH_RADIUS / moon.distance);
    astro::RaDek moonRaDek = astro::calculateParallax(
        moonRaDekRoh, mondParallax, siderealTime, LATITUDE);
    moonRaDek.distance = moon.distance;  // in km

    // ── Mondachse & Libration ────────────────────────────────────────────────
    astro::MoonAxle mondAchse = astro::calculateMoonAxle(jd, moon);

    // ── Rendering ────────────────────────────────────────────────────────────
    
    double cosPsi = sin(sunRaDek.dek) * sin(moonRaDek.dek)
                  + cos(sunRaDek.dek) * cos(moonRaDek.dek) * cos(sunRaDek.ra - moonRaDek.ra);

    double i = atan2(sunRaDek.distance * sin(acos(cosPsi)),
                     moonRaDek.distance - sunRaDek.distance * cosPsi);
    double k = (1 + cos(i)) / 2.0;

    double chi = atan2(cos(sunRaDek.dek) * sin(sunRaDek.ra - moonRaDek.ra),
                       sin(sunRaDek.dek) * cos(moonRaDek.dek)
                     - cos(sunRaDek.dek) * sin(moonRaDek.dek) * cos(sunRaDek.ra - moonRaDek.ra));

    double q = atan2(sin(siderealTime - moonRaDek.ra),
                     tan(LATITUDE * astro::DEG2RAD) * cos(moonRaDek.dek)
                   - sin(moonRaDek.dek) * cos(siderealTime - moonRaDek.ra));
    
    double phase = k;
    double mask = (chi-q+M_PI/2);
    double rot = (q+mondAchse.axle);

    drawMoon(phase, rot,mask);

    // ── Hier Anzeige-Code des Nutzers einfügen ───────────────────────────────
    // z.B. TFT-Display, LVGL, DMA-Transfer, etc.
}

// ── Setup & Loop ─────────────────────────────────────────────────────────────

void setup() {
    Serial.begin(115200);

    // WLAN verbinden
    Serial.printf("Verbinde mit %s …\n", WIFI_SSID);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\nVerbunden. IP: %s\n", WiFi.localIP().toString().c_str());

    // NTP starten (UTC)
    configTime(GMT_OFFSET_SEC, DAYLIGHT_OFFSET, NTP_SERVER);

    // Warten bis Zeit synchronisiert ist
    struct tm zeitInfo;
    Serial.print("Warte auf NTP-Sync …");
    while (!getLocalTime(&zeitInfo)) {
        delay(500);
        Serial.print('.');
    }
    Serial.printf("\nZeit synchronisiert: %04d-%02d-%02d %02d:%02d:%02d UTC\n",
        zeitInfo.tm_year + 1900,
        zeitInfo.tm_mon  + 1,
        zeitInfo.tm_mday,
        zeitInfo.tm_hour,
        zeitInfo.tm_min,
        zeitInfo.tm_sec);

    berechneMondPhase();
}

void loop() {
    // Alle 60 Sekunden neu berechnen
    delay(60000);
    berechneMondPhase();
}

void drawMoon(float phase, float rotation, float mask) {

  int r = 120;
  int b = r * r;
  int a = r * r;
  if (phase > 0.5) {
    a *= (phase * 2 - 1)*(phase * 2 - 1);
  } else {
    a *= (1 - phase * 2)*(1 - phase * 2);
  }
  float cosMask = cos(-mask);
  float sinMask = sin(-mask);
  float cosRot = cos(-rotation);
  float sinRot = sin(-rotation);

  for (int y = -r; y < r; y++) {
    for (int x = -r; x < r; x++) {
      int circle = x * x + y * y - r * r;
      if (circle > 0) {
        //tft.drawPixel(x+r,y+r,ST77XX_BLACK);
        continue;
      }
      int conditionX = x * cosMask + y * sinMask;
      int conditionY = -x * sinMask + y * cosMask;
      int ellipse = conditionX * conditionX * b + conditionY * conditionY * a - a * b;
      bool pixelActive = false;

      if (phase > 0.5) {
        pixelActive = conditionX >= 0 || ellipse <= 0;
      } else {
        pixelActive = conditionX >= 0 && ellipse > 0;
      }
      uint16_t pixelColor = 0;
      if(pixelActive) {
        int pixelX = (x * cosRot + y * sinRot) + r;
        int pixelY = (-x * sinRot + y * cosRot) + r;
        memcpy_P(&pixelColor, &FullMoon[(pixelY*240 + pixelX)], 2);
      }
      tft.drawPixel(x + r, y + r, pixelColor);
    }
    yield();
  }

}
