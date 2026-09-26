#ifndef PNG_STREAM_H
#define PNG_STREAM_H

#include <stddef.h>
#include <stdint.h>

// Erzeugt ein PNG (8 Bit RGB) stückweise, ohne das ganze Bild im Speicher zu halten.
// Die Bilddaten werden als unkomprimierte Deflate-Blöcke abgelegt; dadurch steht die
// Dateigröße vorab fest und jede Bildzeile wird erst beim Abholen berechnet.

constexpr int PNG_STREAM_MAX_WIDTH = 240;

// Startet ein neues Bild. pixel(x, y) liefert die Farbe als RGB565, x in [0, width), y in [0, height).
void pngStreamBegin(int width, int height, uint16_t (*pixel)(int x, int y));

// Gesamtgröße der PNG-Datei in Byte.
size_t pngStreamSize();

// true, solange noch Daten abzuholen sind.
bool pngStreamMore();

// Liefert das nächste Datenstück; gültig bis zum nächsten Aufruf.
const uint8_t* pngStreamNext(size_t& len);

#endif // PNG_STREAM_H
