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
constexpr double LATITUDE  =  49.821;
constexpr double LONGITUDE =   8.869;

// NTP
constexpr long   GMT_OFFSET_SEC  = 0;    // UTC verwenden; Zeitzone lokal egal
constexpr int    DAYLIGHT_OFFSET = 0;
constexpr char   NTP_SERVER[]    = "pool.ntp.org";

// Mond-Rendering
extern const  uint16_t FullMoon[];


// Display

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
    unsigned long tStart, tEnd;
    unsigned long tGesamt = micros();

    // Aktuelle UTC-Zeit holen
    struct tm zeitInfo;
    if (!getLocalTime(&zeitInfo)) {
        Serial.println("Keine gültige NTP-Zeit verfügbar.");
        return;
    }

    tStart = micros();
    double jd = julianDateVonTm(zeitInfo);
    tEnd = micros();
    Serial.printf("  Julianisches Datum:   %lu µs\n", tEnd - tStart);

    // Lokale Sternzeit in Radiant (GMST + Längengrad-Offset)
    tStart = micros();
    double gmstStunden  = astro::calculateSiderealTime(jd);
    double siderealTime = gmstStunden * M_PI / 12.0 + LONGITUDE * astro::DEG2RAD;
    tEnd = micros();
    Serial.printf("  Sternzeit:            %lu µs\n", tEnd - tStart);

    // ── Sonnenposition ───────────────────────────────────────────────────────
    tStart = micros();
    double eklLaenge    = astro::calculateEclipticalLength(jd);

    astro::RaDek sunRaDek = astro::calculateRaDek(eklLaenge, 0.0);
    sunRaDek.distance = 149597870;  // in km (1 AE)
    tEnd = micros();
    Serial.printf("  Sonnenposition:       %lu µs\n", tEnd - tStart);

    // ── Mondposition ─────────────────────────────────────────────────────────
    tStart = micros();
    astro::MoonPosition moon = astro::calculateMoon(jd);

    astro::RaDek moonRaDek = astro::calculateRaDek(moon.longitude, moon.latitude);

    moonRaDek.distance = moon.distance;  // in km

    astro::AzimutHeight moonAzimutHeight = astro::calculateHAzFromRaDek(moonRaDek, siderealTime, LATITUDE  * astro::DEG2RAD);

    tEnd = micros();
    Serial.printf("  Mondposition:         %lu µs\n", tEnd - tStart);


    // ── Mondachse & Libration ────────────────────────────────────────────────
    tStart = micros();
    astro::MoonAxle mondAchse = astro::calculateMoonAxle(jd, moon);
    tEnd = micros();
    Serial.printf("  Mondachse/Libration:  %lu µs\n", tEnd - tStart);

    // ── Rendering-Vorbereitung ───────────────────────────────────────────────
    tStart = micros();

    // Geozenrische Elongation zwischen Sonne und Mond
    // Formel 46.2 aus "Astronomical Algorithms" von Jean Meeus.
    double cosPsi = sin(sunRaDek.dek) * sin(moonRaDek.dek)
                  + cos(sunRaDek.dek) * cos(moonRaDek.dek) * cos(sunRaDek.ra - moonRaDek.ra);

    
    // Phasenwinkel zwischen Sonne, Mond und Erde (gesehen vom Mond)
    // Formel 46.3 aus "Astronomical Algorithms" von Jean Meeus.
    // double i = atan2(sunRaDek.distance * sin(acos(cosPsi)),
    //                 moonRaDek.distance - sunRaDek.distance * cosPsi);
    
    // Beleuchtungsanteil des Mondes (0 = Neumond, 1 = Vollmond)
    // Formel 46.1 aus "Astronomical Algorithms" von Jean Meeus.
    //double k = (1 + cos(i)) / 2.0;

    // Alternative Formel für k, die direkt cosPsi verwendet
    double k = (1 - cosPsi) / 2.0;

    // Positionswinkel (Mitte) des beleuchteten Mondrandes 
    // Formel 46.5 aus "Astronomical Algorithms" von Jean Meeus.
    double chi = atan2(cos(sunRaDek.dek) * sin(sunRaDek.ra - moonRaDek.ra),
                       sin(sunRaDek.dek) * cos(moonRaDek.dek)
                     - cos(sunRaDek.dek) * sin(moonRaDek.dek) * cos(sunRaDek.ra - moonRaDek.ra));

    // Parallaktischer Winkel des Mondes
    // Formel 13.1 aus "Astronomical Algorithms" von Jean Meeus.
    double q = atan2(sin(siderealTime - moonRaDek.ra),
                     tan(LATITUDE * astro::DEG2RAD) * cos(moonRaDek.dek)
                   - sin(moonRaDek.dek) * cos(siderealTime - moonRaDek.ra));

    double phase = k;
    // Zenitwinkel des hellen Mondrandes: chi - q
    double mask = (chi-q+M_PI/2);

    double rot = (mondAchse.axle-q-0.389615);
    tEnd = micros();
    Serial.printf("  Phasenberechnung:     %lu µs\n", tEnd - tStart);

    tStart = micros();
    drawMoon(phase, rot,mask);
    tEnd = micros();
    Serial.printf("  drawMoon:             %lu µs\n", tEnd - tStart);

    Serial.printf("  ── Gesamt:            %lu µs\n", micros() - tGesamt);

    Serial.printf("  Mondposition Azimut/Höhe: %f° / %f°\n", moonAzimutHeight.azimut * astro::RAD2DEG, moonAzimutHeight.height * astro::RAD2DEG);
    Serial.printf("  Mondposition RA/Dek:      %f° / %f°\n", moonRaDek.ra * astro::RAD2DEG, moonRaDek.dek * astro::RAD2DEG);
    Serial.printf("  Mondlibration:           %f° / %f°\n", mondAchse.libration.longitude * astro::RAD2DEG, mondAchse.libration.latitude * astro::RAD2DEG);
    Serial.printf("  Mondachse:              %f°\n", mondAchse.axle * astro::RAD2DEG);
    Serial.printf("  Mondphase (0-1):         %f\n", phase);
    Serial.printf("  Maskenwinkel:            %f°\n", mask * astro::RAD2DEG);
    
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
    tft.begin();
    tft.fillScreen(GC9A01A_BLACK);
    berechneMondPhase();
}

void loop() {
    // Alle 60 Sekunden neu berechnen
    delay(60000);
    berechneMondPhase();
}

void drawMoon(float phase, float rotation, float mask) {

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
      int pixelX = (x * cosRot + y * sinRot) + r;
      int pixelY = (-x * sinRot + y * cosRot) + r;
      memcpy_P(&pixelColor, &FullMoon[(pixelY*240 + pixelX)], 2);

      if(!pixelActive) {
        int r = (pixelColor >> 11) & 0x1F;
        int g = (pixelColor >> 5) & 0x3F;
        int b = pixelColor & 0x1F;
        r = r * 0.2;
        g = g * 0.2;
        b = b * 0.2;
        pixelColor = (r << 11) | (g << 5) | b;
      }
      tft.drawPixel(x + r, y + r, pixelColor);
    }
    yield();
  }

}
