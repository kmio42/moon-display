#ifndef CLOCK_FACE_H
#define CLOCK_FACE_H

#include <Adafruit_GFX.h>
#include <time.h>

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
void drawClockFace(Adafruit_GFX* gfx, time_t now);

// Erzwingt Neuberechnung und komplettes Neuzeichnen (z. B. nach Standort- oder Zeitzonenwechsel).
void clockFaceInvalidate();

#endif // CLOCK_FACE_H
