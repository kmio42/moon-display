// Minimaler Arduino-Ersatz, damit Adafruit_GFX und der Projektcode auf dem PC übersetzt werden.
#pragma once
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <algorithm>

#define PROGMEM
#define pgm_read_byte(a)    (*(const uint8_t*)(a))
#define pgm_read_word(a)    (*(const uint16_t*)(a))
#define pgm_read_dword(a)   (*(const uint32_t*)(a))
#define pgm_read_pointer(a) (*(void* const*)(a))
#define radians(d)          ((d) * M_PI / 180.0)

using std::min;
using std::max;
typedef bool boolean;
class __FlashStringHelper;

class String {
public:
    String(const char* s = "") : s_(s) {}
    const char* c_str() const { return s_; }
    unsigned length() const { return strlen(s_); }
private:
    const char* s_;
};

// Debug-Ausgaben des Geräts werden verworfen
struct SerialStub {
    template <class... A> int printf(const char*, A...) { return 0; }
    template <class T> void println(T) {}
};
static SerialStub Serial;

#include "Print.h"
