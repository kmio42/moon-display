#ifndef MOON_RENDER_H
#define MOON_RENDER_H

#include <stdint.h>
#include <time.h>
#include "astro.h"
#include "ephemeris.h"

// Darstellungsoptionen (bitweise kombinierbar)
constexpr int OPTION_NONE = 0;
constexpr int OPTION_DARKEN_UNLIT = 1; // Unbeleuchteten Bereich zusätzlich abdunkeln (schwacher Schein um den Mond)
constexpr int OPTION_BLUISH_TINT = 2; // Bläuliche Tönung - wenn unter Horizont
constexpr int OPTION_USE_NASA_MODEL = 4; // Textur aus lroc[] verwenden (statt Vollmond-Textur)
constexpr int OPTION_USE_LIBRATION = 8; // Librationen berücksichtigen

// Radius des Mondes in Pixeln; das Bild ist 2*MOON_RADIUS_PX Pixel breit und hoch
constexpr int MOON_RADIUS_PX = 120;

// Bereitet das Rendern des Mondes vor (Phase, Drehung, Maske, Optionen, Rotationsmatrix).
void prepareMoonRender(float phase, float rotation, float mask, int options, const astro::Mat3& rotMatrix);

// Drehwinkel der Textur und Winkel der Beleuchtungsmaske für eine Himmelsposition.
void moonRenderAngles(const SkyState& sky, double& rotation, double& mask);

// Berechnet die Renderparameter für einen Zeitpunkt (UTC) mit den aktuellen Display-Optionen
// und bereitet das Rendern vor.
void prepareMoonRenderAt(const struct tm& utc);

// Farbe (RGB565) des Pixels (x, y), x und y in [-MOON_RADIUS_PX, MOON_RADIUS_PX).
// Außerhalb des Mondkreises schwarz.
uint16_t moonPixel(int x, int y);

#endif // MOON_RENDER_H
