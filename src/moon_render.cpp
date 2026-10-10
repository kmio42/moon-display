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
static float pixelRot[3][3];     // Rotationsmatrix für die Pixelrechnung (float genügt)
static float rf = MOON_RADIUS_PX; // Radius [Pixel]

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

  astro::Mat3 rotMat = rotMatrix;

  if((options & OPTION_USE_LIBRATION) && !(options & OPTION_USE_NASA_MODEL)) {
    const astro::Mat3 libLongitudeRotCorr = astro::createRotationMatrix({0,-1,0}, astro::Angle::fromDeg(-2.24));
    const astro::Mat3 libLatitudeRotCorr = astro::createRotationMatrix({1,0,0}, astro::Angle::fromDeg(5.78));
    const astro::Mat3 axleRotCorr = astro::createRotationMatrix({0,0,1}, astro::Angle::fromDeg(9));

    rotMat = axleRotCorr * libLatitudeRotCorr * libLongitudeRotCorr * rotMatrix;
#ifdef ARDUINO
    for(int i = 0; i < 3; ++i)
      for(int j = 0; j < 3; ++j)
        Serial.printf("rotMat.m[%d][%d] = %f\n", i, j, rotMat.m[i][j]);
#endif
  }
  for(int i = 0; i < 3; ++i)
    for(int j = 0; j < 3; ++j)
      pixelRot[i][j] = (float) rotMat.m[i][j];
}

// Parallaktischer Winkel q und Positionswinkel P der Mondachse, wie sie laut Optionen
// in die Darstellung eingehen (0, wenn ignoriert).
static void orientationAngles(const SkyState& sky, int options, astro::Angle& q, astro::Angle& p) {
  q = (options & OPTION_IGNORE_PARALLACTIC) ? astro::Angle() : sky.q;
  p = (options & OPTION_IGNORE_POSITION_ANGLE) ? astro::Angle() : sky.moonAxle.positionAngle;
}

void moonRenderAngles(const SkyState& sky, int options, double& rotation, double& mask) {
  astro::Angle q, p;
  orientationAngles(sky, options, q, p);

  // Zenitwinkel des hellen Mondrandes: chi - q. Ohne Positionswinkel wird das ganze Bild
  // um P zurückgedreht (Mondnordpol oben), der helle Rand also mit.
  mask = (sky.chi - (sky.moonAxle.positionAngle - p) - q + astro::Angle::fromDeg(90)).rad();

  rotation = (p - q).rad() + 0.004919 - 0.116413461; //ermittelte Korrektur mit Stellarium
}

void prepareMoonRenderAt(const struct tm& utc) {
  prepareMoonRenderAt(utc, -1);
}

void prepareMoonRenderAt(const struct tm& utc, int options) {
  SkyState sky = computeSky(utc);

  // options < 0: Display-Optionen; mit OPTION_BLUISH_TINT nur unter dem Horizont bläulich getönt
  int opts = options;
  if (opts < 0) {
    opts = configDisplayOptions & ~OPTION_BLUISH_TINT;
    if ((configDisplayOptions & OPTION_BLUISH_TINT) && sky.moonAzH.altitude < astro::Angle()) {
      opts |= OPTION_BLUISH_TINT;
    }
  }

  double rot, mask;
  moonRenderAngles(sky, opts, rot, mask);

  astro::Angle q, p;
  orientationAngles(sky, opts, q, p);
  const astro::Mat3 libLongitudeRot = astro::createRotationMatrix({0, -1, 0}, -sky.moonAxle.libration.longitude);
  const astro::Mat3 libLatitudeRot = astro::createRotationMatrix({1, 0, 0}, -sky.moonAxle.libration.latitude);
  const astro::Mat3 axleRot = astro::createRotationMatrix({0, 0, 1}, -p + q);

  const astro::Mat3 rotMatrix = libLongitudeRot * libLatitudeRot * axleRot;

  prepareMoonRender(sky.phase, rot, mask, opts, rotMatrix);
}

// Liegt der Punkt (x, y) im beleuchteten Teil des Mondes? (Punkt innerhalb des Mondkreises)
static bool isLit(int x, int y) {
  // conditionX und conditionY sind die Koordinaten des Pixels bezogen auf Basis-Mondform
  int conditionX = x * cosMask + y * sinMask;
  int conditionY = -x * sinMask + y * cosMask;
  // Punkt (x,y) liegt innerhalb der Ellipse, wenn x^2/b + y^2/a <= 1 ist, bzw. x^2*b + y^2*a - a*b <= 0
  int ellipse = conditionX * conditionX * b + conditionY * conditionY * a - a * b;

  if (phase > 0.5) {
    //gesamter östlicher Teil des Kreises ist beleuchtet (conditionX >= 0),
    //zusätzlich ist der westliche Teil der Ellipse beleuchtet (ellipse <= 0)
    return conditionX >= 0 || ellipse <= 0;
  }
  //Nur der Teil beleuchtet, der östlich ist und außerhalb der Ellipse liegt
  return conditionX >= 0 && ellipse > 0;
}

// Farbe einer Textur (RGB565, width × height) an der Stelle (u, v) [Texel].
// average = false: das Texel, in dem (u, v) liegt; true: Mittel der 2×2 Texel um (u, v).
// wrapU: u ist periodisch (Länge auf der Kugel), sonst wird am Rand begrenzt.
static uint16_t textureSample(const uint16_t* texture, int width, int height, float u, float v,
                              bool average, bool wrapU) {
  auto column = [&](int i) { return wrapU ? (i % width + width) % width : std::clamp(i, 0, width - 1); };
  auto row    = [&](int j) { return std::clamp(j, 0, height - 1); };
  if (!average) {
    return texture[row((int) v) * width + column((int) u)];
  }
  const int u0 = (int) floorf(u - 0.5f);
  const int v0 = (int) floorf(v - 0.5f);
  uint32_t sr = 0, sg = 0, sb = 0;
  for (int j = 0; j < 2; j++) {
    for (int i = 0; i < 2; i++) {
      const uint16_t c = texture[row(v0 + j) * width + column(u0 + i)];
      sr += (c >> 11) & 0x1F;
      sg += (c >> 5) & 0x3F;
      sb += c & 0x1F;
    }
  }
  return ((sr / 4) << 11) | ((sg / 4) << 5) | (sb / 4);
}

// Grundfarbe der Mondoberfläche, wenn der Punkt außerhalb der Textur liegt
constexpr uint16_t MOON_DEFAULT_COLOR = ((uint16_t)(175/255.0f * 31.0f + 0.01f) << 11)
                                      | ((uint16_t)(168/255.0f * 63.0f + 0.01f) <<  5)
                                      |  (uint16_t)(156/255.0f * 31.0f + 0.01f);

// Texturfarbe am Punkt (x, y) des Mondbilds (Pixel, Mittelpunkt 0/0), ohne Beleuchtung.
// Gerechnet wird in float: ein Texel entspricht etwa 0,75°.
static uint16_t textureColor(float x, float y, bool average) {
  if (options & (OPTION_USE_NASA_MODEL | OPTION_USE_LIBRATION)) {
    // Punkt auf der Kugel mit Radius r, gedreht nach Libration und Achse. Der Vektor (x, −y, z)
    // hat die Länge r und die Rotationsmatrix erhält Längen, daher ist keine Normierung nötig.
    const float z = sqrtf(std::max(0.0f, rf * rf - x * x - y * y));
    const float px = pixelRot[0][0] * x - pixelRot[0][1] * y + pixelRot[0][2] * z;
    const float py = pixelRot[1][0] * x - pixelRot[1][1] * y + pixelRot[1][2] * z;
    const float pz = pixelRot[2][0] * x - pixelRot[2][1] * y + pixelRot[2][2] * z;

    if (options & OPTION_USE_NASA_MODEL) {
      // Länge und Breite auf dem Mond, Textur 480 × 240 Texel (Länge −180°..180°, Breite 90°..−90°)
      const float lon = atan2f(px, pz);
      const float lat = asinf(std::clamp(py / rf, -1.0f, 1.0f));
      const float u = (lon + (float) M_PI) / (2.0f * (float) M_PI) * 480.0f;
      const float v = ((float) M_PI / 2.0f - lat) / (float) M_PI * 240.0f;
      return textureSample(lroc, 480, 240, u, v, average, true);
    }
    // Vollmond-Textur mit Libration: nur die uns zugewandte Halbkugel ist im Bild
    if (pz <= 0.0f) return MOON_DEFAULT_COLOR;
    return textureSample(FullMoon, 240, 240, px + rf, -py + rf, average, false);
  }
  // Vollmond-Textur ohne Libration, nur gedreht
  return textureSample(FullMoon, 240, 240, (x * cosRot + y * sinRot) + rf, (-x * sinRot + y * cosRot) + rf,
                       average, false);
}

// Beleuchtung und Tönung auf eine Texturfarbe anwenden
static uint16_t shade(uint16_t pixelColor, bool lit) {
  if (!lit && !(options & OPTION_DARKEN_UNLIT)) return 0;

  // Normalisieren auf 0..1
  float fr = ((pixelColor >> 11) & 0x1F) / 31.0f;
  float fg = ((pixelColor >>  5) & 0x3F) / 63.0f;
  float fb = ( pixelColor        & 0x1F) / 31.0f;

  if(!lit) {
    // Unbeleuchteten Bereich zusätzlich abdunkeln
    fr *= 0.5f;
    fg *= 0.5f;
    fb *= 0.5f;
  }

  if(options & OPTION_BLUISH_TINT) {
    // Bläuliche Tönung, wenn unter Horizont
    fr *= 0.7f;
    fg *= 0.7f;
    fb = fb * 0.8f + 0.14f; // Blau leicht aufhellen
    fr = std::min(fr, 1.0f);
    fg = std::min(fg, 1.0f);
    fb = std::min(fb, 1.0f);
  }

  return ((uint16_t)(fr * 31.0f + 0.01f) << 11)
       | ((uint16_t)(fg * 63.0f + 0.01f) <<  5)
       |  (uint16_t)(fb * 31.0f + 0.01f);
}

static bool insideMoon(int x, int y) {
  return x * x + y * y <= r * r;
}

uint16_t moonPixel(int x, int y) {
  if (!insideMoon(x, y)) {
    // Pixel außerhalb des Mondkreises, also Hintergrund
    return 0;
  }
  const bool lit = isLit(x, y);
  if (!lit && !(options & OPTION_DARKEN_UNLIT)) return 0;
  return shade(textureColor(x, y, false), lit);
}

uint16_t moonPixelAveraged(int x0, int y0, int x1, int y1) {
  const int xs[2] = { x0, x1 };
  const int ys[2] = { y0, y1 };
  bool textureDone = false;
  uint16_t texture = 0;
  // Die Textur ist für alle Abtastpunkte gleich, die Farbe hängt also nur von lit ab
  bool shadedDone[2] = { false, false };
  uint16_t shaded[2] = { 0, 0 };
  uint32_t sr = 0, sg = 0, sb = 0;
  for (int j = 0; j < 2; j++) {
    for (int i = 0; i < 2; i++) {
      const int x = xs[i], y = ys[j];
      if (!insideMoon(x, y)) continue;   // Hintergrund (schwarz)
      const bool lit = isLit(x, y);
      if (!lit && !(options & OPTION_DARKEN_UNLIT)) continue;
      if (!textureDone) {
        // Die Textur nur einmal, in der Mitte der Abtastpunkte
        texture = textureColor((x0 + x1) * 0.5f, (y0 + y1) * 0.5f, true);
        textureDone = true;
      }
      if (!shadedDone[lit]) {
        shaded[lit] = shade(texture, lit);
        shadedDone[lit] = true;
      }
      const uint16_t c = shaded[lit];
      sr += (c >> 11) & 0x1F;
      sg += (c >> 5) & 0x3F;
      sb += c & 0x1F;
    }
  }
  return ((sr / 4) << 11) | ((sg / 4) << 5) | (sb / 4);
}
