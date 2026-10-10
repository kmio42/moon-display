#ifndef CLOCK_FACE_H
#define CLOCK_FACE_H

#include <Adafruit_GFX.h>
#include <time.h>

// Auf dem Gerät ein SPI-Display, dessen drawRGBBitmap eine Zeile in einem Block schreibt;
// in der PC-Vorschau (tools/preview) eine Zeichenfläche im Speicher.
#if defined(ESP32)
#include <Adafruit_SPITFT.h>
using ClockDisplay = Adafruit_SPITFT;
#else
using ClockDisplay = Adafruit_GFX;
#endif

// 24h-Zifferblatt: 0 Uhr unten, 6 Uhr links, 12 Uhr oben, 18 Uhr rechts.
// Zeigt Sonnen- und Mondauf-/-untergang des lokalen Kalendertags, die Zeit des Mondes über dem
// Horizont als Segment, die aktuelle Uhrzeit und in der Mitte die aktuelle Mondphase.

// Auf- und Untergänge des lokalen Kalendertags in lokalen Stunden (0..24).
struct ClockData {
    bool  hasSunrise, hasSunset, hasMoonrise, hasMoonset;
    float sunrise, sunset, moonrise, moonset;
    bool  moonUpAllDay;     // kein Mondereignis heute und Mond über dem Horizont
    // Gestrichelt: nächstes Ereignis des laufenden bzw. nächsten Mondlaufs an einem anderen Datum
    bool  hasDashed;
    bool  dashedIsRise;
    float dashed;
    int   dashedDayOffset;  // +1 Folgetag, -1 Vortag
};

// Berechnet die Daten für den Zeitpunkt now (UTC). Die Ereignisse werden pro lokalem Tag
// zwischengespeichert; nur die gestrichelte Linie hängt von der Uhrzeit ab.
ClockData computeClockData(time_t now);

// Zeichnet die Uhr. Beim ersten Aufruf, nach clockFaceInvalidate(), bei Tageswechsel und
// stündlich (Mondsymbol) wird komplett neu gezeichnet, sonst nur der Stundenzeiger bewegt.
void drawClockFace(ClockDisplay* gfx, time_t now);

// Erzwingt Neuberechnung und komplettes Neuzeichnen (z. B. nach Standort- oder Zeitzonenwechsel).
void clockFaceInvalidate();

// Bild der Uhr unabhängig vom Display (für Telegram), CLOCK_IMAGE_SIZE Pixel breit und hoch.
// Der Zeichenzustand des Displays bleibt unberührt.
constexpr int CLOCK_IMAGE_SIZE = 240;

// Zeichnet die Uhr für den Zeitpunkt now (UTC) komplett in einen Puffer mit 16 Farben (28,8 KB)
// und bereitet das Mondsymbol vor. false, wenn der Speicher fehlt.
bool clockImageBegin(time_t now);

// Farbe (RGB565) des Pixels (x, y), x und y in [0, CLOCK_IMAGE_SIZE). Das Mondsymbol wird erst hier
// berechnet; dazwischen darf kein anderes prepareMoonRender…() laufen.
uint16_t clockImagePixel(int x, int y);

// Gibt den Puffer frei.
void clockImageEnd();

#endif // CLOCK_FACE_H
