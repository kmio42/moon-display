/**
 * preview – erzeugt PNG-Bilder von Mond und 24h-Uhr, wie sie das Display zeigt.
 *
 * Verwendet den Projektcode (moon_render, clock_face, ephemeris, astro) unverändert; Adafruit_GFX
 * zeichnet in einen Speicherpuffer. Bauen und Aufruf siehe Makefile im Projektordner.
 */

#include <Adafruit_GFX.h>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>

#include "clock_face.h"
#include "moon_render.h"
#include "png_stream.h"

// Konfiguration, die auf dem Gerät aus Config.ino kommt
double configLatitude  = 49.87;
double configLongitude = 8.65;
int    configDisplayOptions = 13;
static std::string tzName = "CET-1CEST,M3.5.0,M10.5.0/3";

static bool telegramLost = false;
bool telegramConnectionLost() {
    return telegramLost;
}

void utcToLocal(time_t t, struct tm& out) {
    localtime_r(&t, &out);
}

// Zeichenfläche mit 240×240 RGB565-Pixeln
class Canvas : public Adafruit_GFX {
public:
    uint16_t px[240 * 240] = {};
    Canvas() : Adafruit_GFX(240, 240) {}
    void drawPixel(int16_t x, int16_t y, uint16_t c) override {
        if (x >= 0 && y >= 0 && x < 240 && y < 240) px[y * 240 + x] = c;
    }
};

static Canvas canvas;
static bool roundMask = true;

static uint16_t canvasPixel(int x, int y) {
    // Außerhalb des runden Displays dunkelgrau, damit der Rand erkennbar ist
    if (roundMask && (x - 119.5f) * (x - 119.5f) + (y - 119.5f) * (y - 119.5f) > 120.0f * 120.0f) return 0x2104;
    return canvas.px[y * 240 + x];
}

static uint16_t moonOnlyPixel(int x, int y) {
    return moonPixel(x - MOON_RADIUS_PX, y - MOON_RADIUS_PX);
}

static void usage() {
    fprintf(stderr,
        "Aufruf: preview moon|clock [Optionen]\n"
        "  -t, --time \"JJJJ-MM-TT HH:MM\"  Lokalzeit (Standard: jetzt)\n"
        "  -u, --utc                       --time ist UTC statt Lokalzeit\n"
        "  -o, --options N                 Display-Optionen als Bitmaske (Standard 13)\n"
        "      --lat B --lon L             Standort in Dezimalgrad (Standard Darmstadt)\n"
        "      --tz POSIX-TZ               Zeitzone (Standard CET-1CEST,M3.5.0,M10.5.0/3)\n"
        "      --square                    clock: keine runde Maske außen\n"
        "      --no-telegram               clock: Telegram-Verbindung fehlt (weiße Markierung)\n"
        "  -f, --file DATEI                Ausgabedatei (Standard moon.png bzw. clock.png)\n");
}

int main(int argc, char** argv) {
    if (argc < 2) { usage(); return 1; }
    const std::string mode = argv[1];
    if (mode != "moon" && mode != "clock") { usage(); return 1; }

    std::string timeText, file = mode + ".png";
    bool utc = false;
    for (int i = 2; i < argc; i++) {
        const std::string a = argv[i];
        const bool hasValue = i + 1 < argc;
        if ((a == "-t" || a == "--time") && hasValue)      timeText = argv[++i];
        else if (a == "-u" || a == "--utc")                utc = true;
        else if ((a == "-o" || a == "--options") && hasValue) configDisplayOptions = atoi(argv[++i]);
        else if (a == "--lat" && hasValue)                 configLatitude = atof(argv[++i]);
        else if (a == "--lon" && hasValue)                 configLongitude = atof(argv[++i]);
        else if (a == "--tz" && hasValue)                  tzName = argv[++i];
        else if (a == "--square")                          roundMask = false;
        else if (a == "--no-telegram")                     telegramLost = true;
        else if ((a == "-f" || a == "--file") && hasValue) file = argv[++i];
        else { usage(); return 1; }
    }
    setenv("TZ", tzName.c_str(), 1);
    tzset();

    time_t now = time(nullptr);
    if (!timeText.empty()) {
        struct tm tmIn = {};
        if (!strptime(timeText.c_str(), "%Y-%m-%d %H:%M", &tmIn)) {
            fprintf(stderr, "Ungültige Zeit: %s (erwartet \"JJJJ-MM-TT HH:MM\")\n", timeText.c_str());
            return 1;
        }
        tmIn.tm_isdst = -1;
        now = utc ? timegm(&tmIn) : mktime(&tmIn);
    }

    if (mode == "moon") {
        struct tm gm;
        gmtime_r(&now, &gm);
        prepareMoonRenderAt(gm);
        roundMask = false;
        pngStreamBegin(2 * MOON_RADIUS_PX, 2 * MOON_RADIUS_PX, moonOnlyPixel);
    } else {
        drawClockFace(&canvas, now);
        pngStreamBegin(240, 240, canvasPixel);
    }

    FILE* out = fopen(file.c_str(), "wb");
    if (!out) { perror(file.c_str()); return 1; }
    while (pngStreamMore()) {
        size_t len;
        const uint8_t* piece = pngStreamNext(len);
        fwrite(piece, 1, len, out);
    }
    fclose(out);

    struct tm lt;
    localtime_r(&now, &lt);
    char buf[32];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M %Z", &lt);
    printf("%s: %s, Optionen %d, %s\n", file.c_str(), mode.c_str(), configDisplayOptions, buf);
    return 0;
}
