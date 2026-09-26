#include "clock_face.h"

#include <math.h>
#include <Fonts/FreeSansBold9pt7b.h>

#include "ephemeris.h"
#include "moon_render.h"

// Umrechnung in Lokalzeit (Config.ino)
extern void utcToLocal(time_t t, struct tm& out);

namespace {

// ── Geometrie und Farben ─────────────────────────────────────────────────────

constexpr float CX = 119.5f;            // Mittelpunkt des 240×240-Displays
constexpr float CY = 119.5f;
constexpr float RING = 118.0f;          // Außenring
constexpr float SEGMENT_WIDTH = 14.0f;  // Breite des Mondsegments
constexpr float HAND_START = 40.0f;     // Zeiger beginnen außerhalb des Mondsymbols
constexpr float RED_HAND_END = RING - 34.0f;
constexpr float DIGIT_RADIUS = RING - 28.0f;
constexpr float LABEL_RADIUS = RING - 24.0f;
constexpr int   MOON_SYMBOL_RADIUS = 35;

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

constexpr uint16_t COLOR_BLACK   = 0x0000;
constexpr uint16_t COLOR_SUN     = rgb565(255, 243, 160);
constexpr uint16_t COLOR_MOON    = rgb565(160, 216, 255);
constexpr uint16_t COLOR_SEGMENT = rgb565(40, 90, 130);
constexpr uint16_t COLOR_NOW     = rgb565(255, 48, 48);
constexpr uint16_t COLOR_RING    = rgb565(90, 90, 90);
constexpr uint16_t COLOR_MAJOR   = rgb565(200, 200, 200);
constexpr uint16_t COLOR_MINOR   = rgb565(110, 110, 110);

// Punkt auf dem Zifferblatt für eine Uhrzeit (Stunden) und einen Radius.
// 0 Uhr unten, im Uhrzeigersinn: Winkel von oben = 180° + Stunde * 15°.
void polar(float hours, float r, float& x, float& y) {
    const float a = (180.0f + hours * 15.0f) * (float) M_PI / 180.0f;
    x = CX + sinf(a) * r;
    y = CY - cosf(a) * r;
}

// Linie mit Breite w als zwei gefüllte Dreiecke
void thickLine(Adafruit_GFX* gfx, float x0, float y0, float x1, float y1, float w, uint16_t color) {
    const float dx = x1 - x0;
    const float dy = y1 - y0;
    const float len = sqrtf(dx * dx + dy * dy);
    if (len < 0.01f) return;
    if (w <= 1.0f) {
        gfx->drawLine(lroundf(x0), lroundf(y0), lroundf(x1), lroundf(y1), color);
        return;
    }
    const float nx = -dy / len * w * 0.5f;
    const float ny =  dx / len * w * 0.5f;
    const int16_t ax = lroundf(x0 + nx), ay = lroundf(y0 + ny);
    const int16_t bx = lroundf(x0 - nx), by = lroundf(y0 - ny);
    const int16_t cx = lroundf(x1 - nx), cy = lroundf(y1 - ny);
    const int16_t dx2 = lroundf(x1 + nx), dy2 = lroundf(y1 + ny);
    gfx->fillTriangle(ax, ay, bx, by, cx, cy, color);
    gfx->fillTriangle(ax, ay, cx, cy, dx2, dy2, color);
}

void radialLine(Adafruit_GFX* gfx, float hours, float r0, float r1, float w, uint16_t color) {
    float x0, y0, x1, y1;
    polar(hours, r0, x0, y0);
    polar(hours, r1, x1, y1);
    thickLine(gfx, x0, y0, x1, y1, w, color);
}

void dashedRadialLine(Adafruit_GFX* gfx, float hours, float r0, float r1, float w, uint16_t color) {
    constexpr float DASH = 6.0f;
    constexpr float GAP = 4.0f;
    for (float r = r0; r < r1; r += DASH + GAP) {
        radialLine(gfx, hours, r, fminf(r + DASH, r1), w, color);
    }
}

// Ringsegment zwischen den Uhrzeiten h0 und h1 (h0 < h1, beide in 0..24)
void arcSegment(Adafruit_GFX* gfx, float h0, float h1, float rIn, float rOut, uint16_t color) {
    constexpr float STEP = 0.1f;  // Stunden je Teilstück (1,5°)
    const int steps = (int) ceilf((h1 - h0) / STEP);
    if (steps <= 0) return;
    float ox0, oy0, ix0, iy0;
    polar(h0, rOut, ox0, oy0);
    polar(h0, rIn, ix0, iy0);
    for (int i = 1; i <= steps; i++) {
        const float h = h0 + (h1 - h0) * i / steps;
        float ox1, oy1, ix1, iy1;
        polar(h, rOut, ox1, oy1);
        polar(h, rIn, ix1, iy1);
        gfx->fillTriangle(lroundf(ox0), lroundf(oy0), lroundf(ix0), lroundf(iy0), lroundf(ix1), lroundf(iy1), color);
        gfx->fillTriangle(lroundf(ox0), lroundf(oy0), lroundf(ix1), lroundf(iy1), lroundf(ox1), lroundf(oy1), color);
        ox0 = ox1; oy0 = oy1; ix0 = ix1; iy0 = iy1;
    }
}

// Text zentriert um (x, y)
void centeredText(Adafruit_GFX* gfx, const char* text, float x, float y, uint16_t color, bool clearBackground) {
    int16_t bx, by;
    uint16_t bw, bh;
    gfx->getTextBounds(text, 0, 0, &bx, &by, &bw, &bh);
    const int16_t left = lroundf(x - bw / 2.0f);
    const int16_t top = lroundf(y - bh / 2.0f);
    if (clearBackground) {
        gfx->fillRect(left - 2, top - 2, bw + 4, bh + 4, COLOR_BLACK);
    }
    gfx->setTextColor(color);
    gfx->setCursor(left - bx, top - by);
    gfx->print(text);
}

// ── Daten ────────────────────────────────────────────────────────────────────

float localHours(time_t t) {
    struct tm lt;
    utcToLocal(t, lt);
    return lt.tm_hour + lt.tm_min / 60.0f + lt.tm_sec / 3600.0f;
}

int dayKeyOf(time_t t) {
    struct tm lt;
    utcToLocal(t, lt);
    return localDayKey(lt);
}

struct MoonEvent {
    time_t time;
    bool rise;
};

// Zwischenspeicher für einen lokalen Tag
struct DayCache {
    int day = -1;
    DayEvents today;
    MoonEvent moon[8];      // Mondereignisse von gestern, heute und morgen, chronologisch
    int moonCount = 0;
} cache;

void addMoonEvents(const DayEvents& e) {
    if (e.hasMoonrise && cache.moonCount < 8) cache.moon[cache.moonCount++] = { e.moonrise, true };
    if (e.hasMoonset  && cache.moonCount < 8) cache.moon[cache.moonCount++] = { e.moonset, false };
}

void updateCache(time_t now) {
    const int day = dayKeyOf(now);
    if (day == cache.day) return;

    cache.day = day;
    cache.moonCount = 0;
    cache.today = eventsForLocalDay(now, true);
    addMoonEvents(eventsForLocalDay(now - 86400, true));
    addMoonEvents(cache.today);
    addMoonEvents(eventsForLocalDay(now + 86400, true));
    // Chronologisch sortieren (wenige Einträge)
    for (int i = 1; i < cache.moonCount; i++) {
        for (int j = i; j > 0 && cache.moon[j].time < cache.moon[j - 1].time; j--) {
            MoonEvent tmp = cache.moon[j];
            cache.moon[j] = cache.moon[j - 1];
            cache.moon[j - 1] = tmp;
        }
    }
}

bool moonAboveHorizon(time_t now) {
    struct tm utc;
    gmtime_r(&now, &utc);
    return computeSky(utc).moonAzH.height > 0.0;
}

void setDashed(ClockData& d, const MoonEvent& e, int today) {
    d.hasDashed = true;
    d.dashedIsRise = e.rise;
    d.dashed = localHours(e.time);
    d.dashedDayOffset = (dayKeyOf(e.time) > today) ? 1 : -1;
}

// ── Zeichenzustand ───────────────────────────────────────────────────────────

constexpr time_t MOON_SYMBOL_INTERVAL = 5 * 60;  // Sekunden

bool needsFullRedraw = true;
int lastDrawnDay = -1;
ClockData lastDrawnData;
time_t lastMoonSymbol = 0;
bool lastMoonUp = false;
float lastNowHours = -1.0f;

bool sameData(const ClockData& a, const ClockData& b) {
    return a.hasSunrise == b.hasSunrise && a.sunrise == b.sunrise
        && a.hasSunset == b.hasSunset && a.sunset == b.sunset
        && a.hasMoonrise == b.hasMoonrise && a.moonrise == b.moonrise
        && a.hasMoonset == b.hasMoonset && a.moonset == b.moonset
        && a.moonUpAllDay == b.moonUpAllDay
        && a.hasDashed == b.hasDashed && a.dashedIsRise == b.dashedIsRise
        && a.dashed == b.dashed && a.dashedDayOffset == b.dashedDayOffset;
}

void drawMoonSymbol(Adafruit_GFX* gfx, time_t now) {
    struct tm utc;
    gmtime_r(&now, &utc);
    prepareMoonRenderAt(utc);

    // Verkleinerung des Mondbildes; je Pixel Mittelwert aus 2×2 Abtastpunkten
    const float scale = (float) MOON_RADIUS_PX / MOON_SYMBOL_RADIUS;
    const int16_t cx = lroundf(CX);
    const int16_t cy = lroundf(CY);
    for (int y = -MOON_SYMBOL_RADIUS; y < MOON_SYMBOL_RADIUS; y++) {
        for (int x = -MOON_SYMBOL_RADIUS; x < MOON_SYMBOL_RADIUS; x++) {
            if (x * x + y * y > MOON_SYMBOL_RADIUS * MOON_SYMBOL_RADIUS) continue;
            uint32_t r = 0, g = 0, b = 0;
            for (int sy = 0; sy < 2; sy++) {
                for (int sx = 0; sx < 2; sx++) {
                    const uint16_t c = moonPixel((int) floorf((x + 0.25f + sx * 0.5f) * scale),
                                                 (int) floorf((y + 0.25f + sy * 0.5f) * scale));
                    r += (c >> 11) & 0x1F;
                    g += (c >> 5) & 0x3F;
                    b += c & 0x1F;
                }
            }
            gfx->drawPixel(cx + x, cy + y, ((r / 4) << 11) | ((g / 4) << 5) | (b / 4));
        }
    }
}

// Winkelabstand zweier Uhrzeiten in Stunden (0..12)
float hourDistance(float a, float b) {
    return fabsf(remainderf(a - b, 24.0f));
}

// Elemente innerhalb dieses Winkelabstands zum alten Stundenzeiger können von ihm überdeckt worden
// sein (bei 40 px Abstand zur Mitte sind 0,6 h ≈ 6 px, mehr als die halben Linienbreiten).
constexpr float NEAR_HOURS = 0.6f;

bool isNear(float hours, float nearHours) {
    return nearHours < 0.0f || hourDistance(hours, nearHours) < NEAR_HOURS;
}

// Äußerer Teil: Segment, Ring und Striche. Liegt außerhalb der Reichweite des Stundenzeigers.
void drawOuterDial(Adafruit_GFX* gfx, const ClockData& d) {
    const float segIn = RING - SEGMENT_WIDTH;

    // Mondsegment (Kalendertag)
    if (d.moonUpAllDay) {
        arcSegment(gfx, 0.0f, 24.0f, segIn, RING, COLOR_SEGMENT);
    } else if (d.hasMoonrise && d.hasMoonset && d.moonrise < d.moonset) {
        arcSegment(gfx, d.moonrise, d.moonset, segIn, RING, COLOR_SEGMENT);
    } else {
        if (d.hasMoonset)  arcSegment(gfx, 0.0f, d.moonset, segIn, RING, COLOR_SEGMENT);
        if (d.hasMoonrise) arcSegment(gfx, d.moonrise, 24.0f, segIn, RING, COLOR_SEGMENT);
    }

    // Tagesgrenze (0 Uhr) liegt senkrecht unter der Mitte, zwischen den Pixelspalten 119 und 120.
    // Pixelgenau mit Rechtecken, damit sie symmetrisch ist: 2 px Grau (Spalten 119–120),
    // im Segment beidseitig 2 px Schwarz (Spalten 117–118 und 121–122).
    const int16_t midLeft = (int16_t) floorf(CX);  // 119
    const int16_t segTop = lroundf(CY + segIn - 1.0f);
    const int16_t ringInner = lroundf(CY + RING - 2.0f);
    gfx->fillRect(midLeft - 2, segTop, 6, ringInner - segTop + 1, COLOR_BLACK);

    // Ring
    const int16_t cx = lroundf(CX);
    const int16_t cy = lroundf(CY);
    gfx->drawCircle(cx, cy, lroundf(RING), COLOR_RING);
    gfx->drawCircle(cx, cy, lroundf(RING) - 1, COLOR_RING);

    // Stundenstriche (0 Uhr ist die Tagesgrenze)
    for (int h = 1; h < 24; h++) {
        if (h % 6 == 0) {
            radialLine(gfx, h, RING - 14.0f, RING, 3.0f, COLOR_MAJOR);
        } else {
            radialLine(gfx, h, RING - 7.0f, RING, 1.0f, COLOR_MINOR);
        }
    }

}

// Innerer Teil: Tagesgrenze, Ziffern und Zeiger. Mit nearHours >= 0 nur die Elemente in der Nähe
// dieser Uhrzeit (nach dem Löschen des alten Stundenzeigers), sonst alle.
void drawInnerDial(Adafruit_GFX* gfx, const ClockData& d, float nearHours) {
    // Tagesgrenze im Grau des Rings bis an den Rand
    const int16_t midLeft = (int16_t) floorf(CX);  // 119
    const int16_t lineTop = lroundf(CY + HAND_START);
    const int16_t lineBottom = lroundf(CY + RING);
    if (isNear(0.0f, nearHours)) {
        gfx->fillRect(midLeft, lineTop, 2, lineBottom - lineTop, COLOR_RING);
    }

    // Ziffern
    gfx->setFont(&FreeSansBold9pt7b);
    gfx->setTextSize(1);
    const struct { int hour; const char* text; } digits[] = { { 6, "6" }, { 12, "12" }, { 18, "18" } };
    for (const auto& digit : digits) {
        if (!isNear(digit.hour, nearHours)) continue;
        float x, y;
        polar(digit.hour, DIGIT_RADIUS, x, y);
        centeredText(gfx, digit.text, x, y, COLOR_MAJOR, false);
    }

    // Sonne und Mond
    const float handEnd = RING - 2.0f;
    if (d.hasSunrise  && isNear(d.sunrise,  nearHours)) radialLine(gfx, d.sunrise,  HAND_START, handEnd, 3.0f, COLOR_SUN);
    if (d.hasSunset   && isNear(d.sunset,   nearHours)) radialLine(gfx, d.sunset,   HAND_START, handEnd, 3.0f, COLOR_SUN);
    if (d.hasMoonrise && isNear(d.moonrise, nearHours)) radialLine(gfx, d.moonrise, HAND_START, handEnd, 3.0f, COLOR_MOON);
    if (d.hasMoonset  && isNear(d.moonset,  nearHours)) radialLine(gfx, d.moonset,  HAND_START, handEnd, 3.0f, COLOR_MOON);

    if (d.hasDashed && isNear(d.dashed, nearHours)) {
        dashedRadialLine(gfx, d.dashed, HAND_START, handEnd, 2.0f, COLOR_MOON);
        gfx->setFont(nullptr);
        float x, y;
        polar(d.dashed, LABEL_RADIUS, x, y);
        centeredText(gfx, d.dashedDayOffset > 0 ? "+1" : "-1", x, y, COLOR_MOON, true);
    }
    gfx->setFont(nullptr);
}

void drawNowHand(Adafruit_GFX* gfx, float hours, uint16_t color) {
    radialLine(gfx, hours, HAND_START, RED_HAND_END, 4.0f, color);
}

} // namespace

ClockData computeClockData(time_t now) {
    updateCache(now);

    ClockData d = {};
    const DayEvents& t = cache.today;
    if (t.hasSunrise)  { d.hasSunrise = true;  d.sunrise = localHours(t.sunrise); }
    if (t.hasSunset)   { d.hasSunset = true;   d.sunset = localHours(t.sunset); }
    if (t.hasMoonrise) { d.hasMoonrise = true; d.moonrise = localHours(t.moonrise); }
    if (t.hasMoonset)  { d.hasMoonset = true;  d.moonset = localHours(t.moonset); }

    // Steht der Mond gerade über dem Horizont? Aus dem letzten vergangenen Ereignis,
    // ersatzweise aus der aktuellen Mondhöhe.
    int last = -1;
    for (int i = 0; i < cache.moonCount && cache.moon[i].time <= now; i++) last = i;
    const bool up = (last >= 0) ? cache.moon[last].rise : moonAboveHorizon(now);

    if (!d.hasMoonrise && !d.hasMoonset) {
        d.moonUpAllDay = up;
    }

    // Gestrichelt: höchstens ein Ereignis an einem anderen Datum
    const int today = cache.day;
    if (up) {
        // Laufender Mondlauf: Aufgang (ggf. gestern) oder Untergang (ggf. morgen)
        if (last >= 0 && dayKeyOf(cache.moon[last].time) != today) {
            setDashed(d, cache.moon[last], today);
        } else if (last + 1 < cache.moonCount && dayKeyOf(cache.moon[last + 1].time) != today) {
            setDashed(d, cache.moon[last + 1], today);
        }
    } else {
        // Nächster Mondlauf: nächster Aufgang, falls nicht heute; sonst der folgende Untergang
        const int rise = last + 1;
        if (rise < cache.moonCount && cache.moon[rise].rise) {
            if (dayKeyOf(cache.moon[rise].time) != today) {
                setDashed(d, cache.moon[rise], today);
            } else if (rise + 1 < cache.moonCount && dayKeyOf(cache.moon[rise + 1].time) != today) {
                setDashed(d, cache.moon[rise + 1], today);
            }
        }
    }
    return d;
}

void drawClockFace(Adafruit_GFX* gfx, time_t now) {
    const ClockData d = computeClockData(now);
    const float nowHours = localHours(now);
    const bool moonUp = moonAboveHorizon(now);

    if (needsFullRedraw || cache.day != lastDrawnDay || !sameData(d, lastDrawnData)) {
        // Komplett neu: beim ersten Mal, bei Tageswechsel und wenn sich Zeiger oder Segment ändern
        // (z. B. wechselt die gestrichelte Linie nach einem Mondauf- oder -untergang)
        gfx->fillScreen(COLOR_BLACK);
        drawMoonSymbol(gfx, now);
        drawOuterDial(gfx, d);
        drawInnerDial(gfx, d, -1.0f);
        needsFullRedraw = false;
        lastDrawnDay = cache.day;
        lastDrawnData = d;
        lastMoonSymbol = now;
        lastMoonUp = moonUp;
    } else {
        // Mondsymbol zu jeder vollen 5-Minuten-Marke und beim Horizontdurchgang (bläuliche Tönung)
        // an Ort und Stelle übermalen; es überschneidet sich mit keinem anderen Element
        if (now / MOON_SYMBOL_INTERVAL != lastMoonSymbol / MOON_SYMBOL_INTERVAL || moonUp != lastMoonUp) {
            drawMoonSymbol(gfx, now);
            lastMoonSymbol = now;
            lastMoonUp = moonUp;
        }
        // Alten Stundenzeiger löschen und nur die Elemente nachzeichnen, die er überdeckt haben kann
        if (lastNowHours >= 0.0f && lastNowHours != nowHours) {
            drawNowHand(gfx, lastNowHours, COLOR_BLACK);
            drawInnerDial(gfx, d, lastNowHours);
        }
    }
    drawNowHand(gfx, nowHours, COLOR_NOW);
    lastNowHours = nowHours;
}

void clockFaceInvalidate() {
    needsFullRedraw = true;
    cache.day = -1;
    lastNowHours = -1.0f;
}
