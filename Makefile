# Vorschau von Mond- und Uhrenbild auf dem PC (kein Gerät nötig).
#
#   make preview                      baut das Programm build/preview/preview
#   make moon  [ARGS="..."]           schreibt moon.png
#   make clock [ARGS="..."]           schreibt clock.png
#   make clean
#
# Beispiele:
#   make clock ARGS='-t "2026-10-04 18:30" -o 9'
#   make moon  ARGS='--lat 52.5 --lon 13.4 -t "2026-12-24 20:00"'
# Alle Optionen: build/preview/preview
#
# Voraussetzung: einmal "pio run", damit die Adafruit-GFX-Bibliothek in .pio/libdeps liegt.

CXX     ?= g++
# Der Verzeichnisname enthält Leerzeichen, deshalb per Shell suchen
GFX     := $(shell find .pio/libdeps -maxdepth 2 -name "Adafruit GFX Library" -type d 2>/dev/null | head -n 1)
BUILD   := build/preview
BIN     := $(BUILD)/preview
SRC     := src
TOOL    := tools/preview

SOURCES := $(TOOL)/preview.cpp $(SRC)/clock_face.cpp $(SRC)/moon_render.cpp $(SRC)/ephemeris.cpp \
           $(SRC)/astro.cpp $(SRC)/png_stream.cpp $(BUILD)/picture.cpp
CXXFLAGS := -O2 -std=gnu++17 -w -DARDUINO=100 -I$(TOOL)/stub -I$(SRC) -I"$(GFX)"

.PHONY: preview moon clock clean

preview: $(BIN)

$(BIN): $(SOURCES) $(wildcard $(SRC)/*.h) $(TOOL)/stub/Arduino.h
	@test -n "$(GFX)" || { echo "Adafruit GFX Library nicht gefunden – erst 'pio run' ausführen."; exit 1; }
	$(CXX) $(CXXFLAGS) -o $@ $(SOURCES) "$(GFX)/Adafruit_GFX.cpp"

# Die Texturen liegen in picture.ino; als C++ ohne Arduino-Header und PROGMEM übersetzen
$(BUILD)/picture.cpp: $(SRC)/picture.ino
	@mkdir -p $(BUILD)
	sed -e 's/#include <Arduino.h>/#include <stdint.h>/' -e 's/PROGMEM//' $< > $@

moon clock: $(BIN)
	$(BIN) $@ $(ARGS)

clean:
	rm -rf build
