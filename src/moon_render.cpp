#include "moon_render.h"

#include <algorithm>
#include <cmath>

#ifdef ARDUINO
#include <Arduino.h>
#endif

// Darstellungsoptionen – persistent in Config.ino
extern int configDisplayOptions;

// Texturen (picture.ino). Auf dem ESP32 liegt PROGMEM im direkt adressierbaren Flash.
extern const uint16_t FullMoon[];
extern const uint16_t lroc[];

// Vorberechneter Zustand für moonPixel(), gesetzt von prepareMoonRender()
static float phase;
static int options;
static int r = MOON_RADIUS_PX;
static int a, b;
static float cosMask, sinMask, cosRot, sinRot;
static astro::Mat3 rotMat;

void prepareMoonRender(float phaseParam, float rotation, float mask, int optionsParam, const astro::Mat3& rotMatrix) {

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

  phase = phaseParam;
  options = optionsParam;
  b = r * r; // Nord-Süd-Halbachse der Ellipse
  a = r * r; // Ost-West-Halbachse der Ellipse, wird mit dem Phasenparameter skaliert
  if (phase > 0.5) {
    a *= (phase * 2 - 1)*(phase * 2 - 1);
  } else {
    a *= (1 - phase * 2)*(1 - phase * 2);
  }
  // Vorberechnung der Rotationsparameter für die Maskierung und die Texturkoordinaten
  cosMask = cos(-mask);
  sinMask = sin(-mask);
  cosRot = cos(-rotation);
  sinRot = sin(-rotation);

  rotMat = rotMatrix;

  if((options & OPTION_USE_LIBRATION) && !(options & OPTION_USE_NASA_MODEL)) {
    const astro::Mat3 libLongitudeRotCorr = astro::createRotationMatrix({0,-1,0},-2.24*astro::DEG2RAD);
    const astro::Mat3 libLatitudeRotCorr = astro::createRotationMatrix({1,0,0},5.78*astro::DEG2RAD);
    const astro::Mat3 axleRotCorr = astro::createRotationMatrix({0,0,1},9*astro::DEG2RAD);

    astro::Mat3 rotMat_temp = multiplyMatrix(axleRotCorr,libLatitudeRotCorr);
                rotMat_temp = multiplyMatrix(rotMat_temp,libLongitudeRotCorr);
                rotMat = multiplyMatrix(rotMat_temp, rotMatrix);
#ifdef ARDUINO
    for(int i = 0; i < 3; ++i)
      for(int j = 0; j < 3; ++j)
        Serial.printf("rotMat.m[%d][%d] = %f\n", i, j, rotMat.m[i][j]);
#endif
  }
}

void moonRenderAngles(const SkyState& sky, double& rotation, double& mask) {
  // Zenitwinkel des hellen Mondrandes: chi - q
  mask = (sky.chi - sky.q + M_PI/2);

  rotation = (sky.moonAxle.axle - sky.q + 0.004919 - 0.116413461); //ermittelte Korrektur mit Stellarium
}

void prepareMoonRenderAt(const struct tm& utc) {
  prepareMoonRenderAt(utc, -1);
}

void prepareMoonRenderAt(const struct tm& utc, int options) {
  SkyState sky = computeSky(utc);

  double rot, mask;
  moonRenderAngles(sky, rot, mask);

  const astro::Mat3 libLongitudeRot = astro::createRotationMatrix({0, -1, 0}, -sky.moonAxle.libration.longitude);
  const astro::Mat3 libLatitudeRot = astro::createRotationMatrix({1, 0, 0}, -sky.moonAxle.libration.latitude);
  const astro::Mat3 axleRot = astro::createRotationMatrix({0, 0, 1}, -sky.moonAxle.axle + sky.q);

  astro::Mat3 rotMatrix = multiplyMatrix(libLongitudeRot, libLatitudeRot);
              rotMatrix = multiplyMatrix(rotMatrix, axleRot);

  // options < 0: Display-Optionen, unter dem Horizont bläulich getönt
  int opts = options;
  if (opts < 0) {
    opts = configDisplayOptions;
    if (sky.moonAzH.height < 0) {
      opts |= OPTION_BLUISH_TINT;
    }
  }
  prepareMoonRender(sky.phase, rot, mask, opts, rotMatrix);
}

uint16_t moonPixel(int x, int y) {
  int circle = x * x + y * y - r * r;
  if (circle > 0) {
    // Pixel außerhalb des Mondkreises, also Hintergrund
    return 0;
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
      pixelColor = lroc[v*480 + u];
    } else {
      int pixelX = 0;
      int pixelY = 0;
      if(options & OPTION_USE_LIBRATION) {
        double z = sqrt(r*r - x*x - y*y);
        astro::Vec3 point = astro::applyMatrix({(double) x,-(double) y,z},rotMat);
        pixelX = point.x+r;
        pixelY = -point.y+r;
        if(point.z > 0) {
          pixelColor = FullMoon[pixelY*240 + pixelX];
        }
      } else {
        pixelX = (x * cosRot + y * sinRot) + r;
        pixelY = (-x * sinRot + y * cosRot) + r;
        pixelColor = FullMoon[pixelY*240 + pixelX];
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
      fr = std::min(fr, 1.0f);
      fg = std::min(fg, 1.0f);
      fb = std::min(fb, 1.0f);
    }

    pixelColor = ((uint16_t)(fr * 31.0f + 0.01f) << 11)
               | ((uint16_t)(fg * 63.0f + 0.01f) <<  5)
               |  (uint16_t)(fb * 31.0f + 0.01f);
  }
  return pixelColor;
}
