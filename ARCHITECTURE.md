# Architektur

Dieses Dokument beschreibt, wie die Firmware des Mond-Displays aufgebaut ist. Wie man das Gerät
bedient und konfiguriert, steht in der [readme.md](readme.md).

## Überblick

Die Firmware läuft auf einem ESP32-C3 (Arduino-Framework, PlatformIO/pioarduino) und steuert ein
rundes 240×240-Display (GC9A01, SPI). Das Netzwerk dient nur als Zeitquelle (NTP) und für die
Benachrichtigungswege (Telegram, Pushover). **Alle astronomischen Berechnungen laufen auf dem Gerät.**

```
                    ┌────────────────────────────── MondPhase.ino ──────────────────────────────┐
                    │  setup(), loop(), Serielle Befehle, Moduswechsel, WLAN                    │
                    └───────┬───────────────────┬──────────────────────┬────────────────────────┘
                            │                   │                      │
              ┌─────────────▼───┐     ┌─────────▼──────────┐    ┌──────▼───────────────┐
              │ Anzeige         │     │ Benachrichtigung   │    │ Konfiguration        │
              │ Moon.ino        │     │ Telegram.ino       │    │ Config.ino (NVS)     │
              │ clock_face.cpp  │     │ Pushover.ino       │    │ credentials.h        │
              └───────┬─────────┘     └─────────┬──────────┘    └──────────────────────┘
                      │                         │
              ┌───────▼─────────┐               │ (Tagesbericht, Mondbild)
              │ moon_render.cpp │◄──────────────┤
              │ png_stream.cpp  │◄──────────────┘
              │ picture.ino     │  Texturen (Flash)
              └───────┬─────────┘
                      │
              ┌───────▼─────────┐
              │ ephemeris.cpp   │  SkyState, Tagesereignisse, Ausblick
              └───────┬─────────┘
                      │
              ┌───────▼─────────┐
              │ astro.cpp       │  Formeln nach Meeus, reine Mathematik
              └─────────────────┘
```

Die Schichten sind strikt geordnet: Eine Schicht kennt nur die unter ihr liegenden. `astro.cpp` und
`ephemeris.cpp` wissen nichts vom Display und vom Netzwerk.

## Dateien

| Datei | Aufgabe |
|---|---|
| [MondPhase.ino](src/MondPhase.ino) | Einstiegspunkt: `setup()`, `loop()`, WLAN-Verbindung, NTP, serielle Befehle, Moduswechsel |
| [Config.ino](src/Config.ino) | Persistente Einstellungen in NVS, Umrechnung UTC → Lokalzeit, Prüfung von Zeitzone und `HH:MM` |
| [Moon.ino](src/Moon.ino) | Mondanzeige: bereitet das Rendern vor und schreibt die Pixel aufs Display; Debug-Ausgabe |
| [clock_face.cpp](src/clock_face.cpp) | 24h-Uhr: Zeichnen von Ring, Segment, Zeigern und Mondsymbol |
| [moon_render.cpp](src/moon_render.cpp) | Mondbild als Funktion `moonPixel(x, y)`, ohne Bildspeicher |
| [picture.ino](src/picture.ino) | Texturen im Flash: `FullMoon` (Foto, 240×240) und `lroc` (NASA-Karte, 480×240) |
| [png_stream.cpp](src/png_stream.cpp) | PNG-Schreiber, der das Bild Stück für Stück erzeugt |
| [ephemeris.cpp](src/ephemeris.cpp) | Sonne, Mond, Auf-/Untergänge und Ausblick für einen Standort und Zeitpunkt |
| [astro.cpp](src/astro.cpp) | Astronomische Formeln nach Jean Meeus |
| [Telegram.ino](src/Telegram.ino) | Bot, Tagesbericht, Mondbild, Profil- und Gruppenbild |
| [Pushover.ino](src/Pushover.ino) | Nachrichten über die Pushover-API |
| [display_mode.h](src/display_mode.h) | Konstanten der Anzeigemodi |
| [credentials.h](src/credentials.h.template) | WLAN-Daten und Zugangsdaten der Dienste (nicht im Repository) |

Die `.ino`-Dateien sind Teile eines einzigen Sketches. Der Arduino-Build fügt sie zusammen, deshalb
rufen sie sich über `extern`-Deklarationen gegenseitig auf. Die `.cpp`-Dateien sind eigene
Übersetzungseinheiten mit Header und enthalten die Logik, die auch ohne Gerät übersetzbar sein soll
(siehe [Vorschau auf dem PC](#vorschau-auf-dem-pc)).

## Ablauf zur Laufzeit

Es gibt einen einzigen Kontrollfluss, keine Tasks und keine Interrupts. `loop()` in
[MondPhase.ino](src/MondPhase.ino) wiederholt etwa alle 100 ms:

1. WLAN pflegen (`WiFiMulti`, bis zu 3 Netzwerke, höchstens einmal pro Sekunde).
2. Serielle Befehle lesen.
3. `handleTelegram()`: Nachrichten abholen (alle 3 s), Tagesbericht und Mondbilder prüfen.
4. Je nach Anzeigemodus zeichnen: im Mond-Modus einmal pro Minute, im Uhren-Modus einmal pro Minute
   den Stundenzeiger.

Daraus folgt: Alles, was lange dauert, blockiert die Anzeige. Das betrifft vor allem das Senden
eines Bildes an Telegram und das Rendern und Hochladen des Profilbildes. Währenddessen wird die
Anzeige nicht aktualisiert.

`setup()` lädt die Konfiguration, startet WLAN und NTP (`configTime`), initialisiert das Display und
die seriellen Befehle und ruft `setupTelegram()` auf. Solange NTP nicht synchronisiert hat, liegt
die Systemzeit vor 2023. Alle Teile prüfen das (`MIN_VALID_TIME`) und warten.

## Zeitkonventionen

- Das System läuft in **UTC** (`TZ=UTC0`). Alle Berechnungen in `astro` und `ephemeris` nutzen UT.
- Erst die Ausgabe wird in die Lokalzeit umgerechnet, mit `utcToLocal()` in
  [Config.ino](src/Config.ino) und dem POSIX-String `configTimezone`. Die Funktion stellt `TZ` nur
  kurz um und setzt es danach wieder auf UTC. `TZ` wird dabei nicht mit `setenv()` gesetzt, sondern
  in einem einmal angelegten Puffer überschrieben (`setTimezone()`): newlib gibt bei `setenv()` den
  alten Wert nicht frei, wenn der neue länger ist, und der ständige Wechsel hat so den Heap geleert.
- Tageswerte (Sonnenauf-/-untergang, Mondereignisse, Tagesbericht) gelten für den **lokalen
  Kalendertag**. `eventsForLocalDay()` berechnet die Ereignisse der UT-Tage, die den lokalen Tag
  überlappen, und filtert sie nach lokalem Datum.
- Ein lokaler Tag wird als Schlüssel `Jahr*1000 + Tag im Jahr` geführt (`localDayKey`).

## Astronomie

**[astro.cpp](src/astro.cpp)** enthält die Formeln aus Meeus, *Astronomische Algorithmen*: Julianisches
Datum, Sternzeit, Sonne, Mond, Parallaxe, Libration, parallaktischer Winkel, Auf- und Untergang der
Sonne. Es hat keine Abhängigkeiten außer `<cmath>`.

**[ephemeris.cpp](src/ephemeris.cpp)** setzt darauf auf und kennt den Standort (`configLatitude`,
`configLongitude`):

- `computeSky(utc)` liefert einen `SkyState`: Sonne und Mond (der Mond topozentrisch, also
  parallaxenkorrigiert), Azimut und Höhe, Phase, zunehmend oder abnehmend, Positionswinkel `chi` des
  hellen Randes, parallaktischer Winkel `q`, Mondachse und Libration.
- `eventsForLocalDay()` liefert Auf- und Untergänge von Sonne und Mond als UTC-Zeitpunkte. Der Mond hat
  keine Meeus-Formel für den Aufgang, deshalb wird seine Höhe stündlich abgetastet und der
  Horizontdurchgang durch quadratische Interpolation bestimmt.
- `moonOutlookFor()` bestimmt für den Tagesbericht die Kulminationshöhe, die Tendenz und die Ereignisse der
  nächsten 14 Tage: Voll- und Neumond (durch Bisektion der Elongation), Finsternisse, Super- und
  Minimond.

## Mondbild

[moon_render.cpp](src/moon_render.cpp) rendert den Mond **ohne Bildpuffer**. Der ESP32-C3 hat zu wenig
RAM für ein 240×240-Bild mit 16 Bit, deshalb gibt `moonPixel(x, y)` die Farbe eines einzelnen Pixels
zurück, und der Aufrufer entscheidet, wohin sie geht:

- Display: [Moon.ino](src/Moon.ino) schreibt Pixel für Pixel mit `drawPixel`.
- Telegram: [png_stream.cpp](src/png_stream.cpp) holt Zeile für Zeile und sendet sie direkt; der
  JPEG-Encoder fürs Profilbild holt 16×16-Blöcke.
- Uhren-Modus: [clock_face.cpp](src/clock_face.cpp) verkleinert das Bild auf das Mondsymbol (Mittel aus
  2×2 Abtastpunkten je Pixel).

Vorbereitung und Rendern sind getrennt: `prepareMoonRenderAt(utc)` berechnet einmal den Himmelszustand
und legt Phase, Winkel, Rotationsmatrix und Optionen in statischen Variablen ab. `moonPixel()` nutzt diese
Werte für jedes Pixel. Damit ist das Rendern **nicht wiederverwendbar im Sinne mehrerer gleichzeitiger
Bilder**: Wer zwischen `prepare` und dem letzten `moonPixel` ein anderes `prepare` aufruft, verfälscht
das Bild.

Pro Pixel wird geprüft:

1. Liegt es im Mondkreis?
2. Ist es beleuchtet? Der Terminator ist eine Ellipse, die mit der Phase skaliert und um den Winkel der
   Maske gedreht ist. Zusammen mit dem Kreis ergibt das die beleuchtete Fläche.
3. Welche Farbe hat die Textur an dieser Stelle? Mit Libration wird der Punkt auf der Kugel mit der
   Rotationsmatrix zurückgerechnet und in der NASA-Karte (`lroc`) oder im Foto nachgeschlagen.
4. Abdunkeln der unbeleuchteten Seite und bläuliche Tönung unter dem Horizont, falls gewählt.

Die Darstellungsoptionen sind ein Bitfeld (siehe [moon_render.h](src/moon_render.h)): abdunkeln,
Tönung, NASA-Textur, Libration, parallaktischen Winkel und Positionswinkel ignorieren. Die Option
„ignorieren“ dreht Textur und Terminator gemeinsam, damit der Terminator auf denselben Mondformationen
bleibt.

Das Profilbild von Telegram nutzt **feste** Optionen (13), unabhängig von der Einstellung.

## Uhren-Modus

[clock_face.cpp](src/clock_face.cpp) zeichnet ein 24-Stunden-Zifferblatt (0 Uhr unten, 12 Uhr oben).
`computeClockData()` bildet die Ereignisse des Tages auf Stunden ab. Die Ereignisse werden pro
lokalem Tag zwischengespeichert, nur die gestrichelte Linie des Mondlaufs hängt von der Uhrzeit ab.

Gezeichnet wird inkrementell, weil das komplette Zifferblatt auf dem Gerät spürbar dauert:

- **Komplett neu** beim ersten Mal, bei Tageswechsel und wenn sich Zeiger oder Segment ändern.
- **Minütlich** wird nur der alte Stundenzeiger schwarz übermalt, die Elemente in seiner Nähe werden
  neu gezeichnet (`NEAR_HOURS`), und der neue Zeiger wird gesetzt.
- **Mondsymbol** alle 5 Minuten und beim Horizontdurchgang (Tönung).
- **Telegram-Verbindung:** Die unteren 5 Pixel der 0-Uhr-Markierung sind weiß, wenn Telegram eingerichtet,
  aber nicht erreichbar ist. Die Uhr fragt dazu `telegramConnectionLost()` aus
  [Telegram.ino](src/Telegram.ino) ab; ändert sich der Zustand, werden nur diese Pixel neu gezeichnet.

Ring und Mondsegment werden pixelweise um den echten Mittelpunkt (119,5 | 119,5) gezeichnet, damit
sie rund und symmetrisch sind. Dicke Linien setzen sich aus zwei Dreiecken zusammen, weil
Adafruit_GFX keine dicken Linien kennt. Senkrechte und waagerechte Linien sind Rechtecke.

## Telegram und Pushover

[Telegram.ino](src/Telegram.ino) pollt alle 3 Sekunden ohne Long-Polling, damit die Anzeige weiterläuft.

**Verbindungsstatus:** `updateTelegramConnection()` prüft mit `getMe`, ob Telegram erreichbar ist,
bei bestehender Verbindung einmal pro Minute, sonst alle 10 Sekunden; ohne WLAN gilt Telegram als nicht
erreichbar. `getUpdates` taugt dafür nicht, weil es „keine neuen Nachrichten“ und „keine Verbindung“
beide mit 0 beantwortet. Die Uhr zeigt den Zustand an (siehe Uhren-Modus). Ist Telegram nicht
eingerichtet, gilt die Verbindung nicht als verloren.
Nach einer fehlgeschlagenen Prüfung wird der Client immer geschlossen, und ohne Verbindung fällt
`getUpdates` aus, damit der Loop nicht auf Timeouts wartet. Die Library wartet 5 s statt 1,5 s auf
Antworten und nimmt Updates bis 4000 Byte an.

**Unlesbare Updates:** Kann die Library ein Update nicht parsen (z. B. zu groß), liefert
`getUpdates` 0, ohne den Offset weiterzurücken, und alle folgenden Nachrichten hängen.
`skipUnreadableUpdate()` holt deshalb nach jeder Verbindungsprüfung das nächste anstehende Update
selbst; steht dieselbe `update_id` bei zwei Prüfungen hintereinander an, wird sie übersprungen.

**Wer darf was:**

- Befehle werden nur akzeptiert, wenn der Absender `TELEGRAM_CHAT_ID` ist, im privaten Chat oder in
  der Gruppe `TELEGRAM_GROUP_ID`. Alles andere wird ignoriert. In der Gruppe reagiert der Bot nur auf
  Befehle, nicht auf normalen Text und nicht auf `/befehl@AndererBot`.
- Antworten gehen an den Chat, aus dem der Befehl kam.

**Täglicher Ablauf zur Berichtszeit** (`configAutoTime`, unabhängig voneinander getrackt, mit
Wiederholung bei Fehlschlag):

| Ziel | Inhalt | Wiederholung |
|---|---|---|
| Telegram, privater Chat | Tagesbericht | beim nächsten Durchlauf |
| Telegram, Gruppe | Tagesbericht | beim nächsten Durchlauf |
| Pushover | Tagesbericht | alle 5 Minuten |
| Bot-Profilbild und Gruppenbild | gerendertes Mondbild | alle 15 Minuten, nur das fehlgeschlagene Ziel |

Das Profil- und das Gruppenbild entstehen aus **einem** JPEG (240×240, in einem temporären 48-KB-Puffer, im
Flash wird nichts gespeichert). Die Bibliothek `UniversalTelegramBot` kennt `setMyProfilePhoto` und
`setChatPhoto` nicht; deshalb baut `uploadJpeg()` die multipart-Anfrage selbst.

[Pushover.ino](src/Pushover.ino) ist nur ausgehend. Es öffnet pro Nachricht eine eigene TLS-Verbindung und
gibt sie danach wieder frei, damit der Speicher für TLS nicht dauerhaft belegt ist.

Telegram und Pushover sind optional und unabhängig. Fehlen die Zugangsdaten in `credentials.h`, ist der
jeweilige Teil abgeschaltet. Pushover braucht kein Telegram.

## Konfiguration

Alle Einstellungen liegen als RAM-Kopie in globalen Variablen in [Config.ino](src/Config.ino) und werden
bei Änderungen mit `saveConfig()` in den NVS-Flash (`Preferences`, Namespace `moon-cfg`) geschrieben.
Sie lassen sich über die serielle Schnittstelle oder über Telegram ändern; beide setzen die Variablen
und rufen `saveConfig()`.

| Schlüssel | Bedeutung |
|---|---|
| `lat`, `lon` | Standort in Dezimalgrad |
| `opts` | Darstellungsoptionen (Bitfeld) |
| `auto`, `atime` | Tagesbericht ein/aus, Uhrzeit in Minuten nach Mitternacht |
| `tz` | POSIX-Zeitzone |
| `mode` | Anzeigemodus (Mond oder Uhr) |

Zugangsdaten (WLAN, Telegram, Pushover) stehen **nicht** im NVS, sondern fest im Programm über
`credentials.h`. Diese Datei ist in der `.gitignore`; als Vorlage dient `credentials.h.template`.

## Speicher und Ressourcen

- Kein Bildpuffer; Pixel werden einzeln berechnet und gezeichnet (siehe oben). Das spart RAM, macht aber
  das Rendern rechenintensiv.
- Die Texturen liegen im Flash (`PROGMEM`). Die Partitionstabelle `huge_app.csv` schafft Platz dafür.
- Das Profil-JPEG belegt kurzzeitig 48 KB Heap, das JPEG-Encoder-Objekt etwa 3 KB (auf dem Heap, nicht
  auf dem Stack).
- Telegram nutzt einen dauerhaften `WiFiClientSecure`, Pushover legt pro Nachricht einen eigenen an.

## Vorschau auf dem PC

Die `.cpp`-Dateien lassen sich ohne Gerät übersetzen. [tools/preview/](tools/preview/) enthält ein
Programm, das Uhr und Mondbild als PNG ausgibt. Dazu liefert `tools/preview/stub/` einen minimalen
Ersatz für die Arduino-Header, und `Adafruit_GFX` zeichnet in einen Speicherpuffer statt auf das Display.
Der Aufruf (`make clock`, `make moon`) steht im [Makefile](Makefile) und in der readme.md.

So lässt sich Zeichencode ändern und prüfen, ohne jedes Mal zu flashen. Der Preview-Code ersetzt
nur `utcToLocal()`, `telegramConnectionLost()` und die Konfigurationsvariablen aus
[Config.ino](src/Config.ino).

## Erweitern

- **Neue Darstellungsoption:** Bit in [moon_render.h](src/moon_render.h) ergänzen, in `moonPixel()` oder
  `prepareMoonRender()` auswerten, dann Anzeige in `config` (seriell, [MondPhase.ino](src/MondPhase.ino))
  und `/config` ([Telegram.ino](src/Telegram.ino)) sowie die Optionstabelle in der readme.md nachziehen.
- **Neuer Telegram-Befehl:** in `handleCommand()` ergänzen, außerdem in `HELP_TEXT` und `BOT_COMMANDS`.
- **Neuer Anzeigemodus:** Konstante in [display_mode.h](src/display_mode.h), Zeichenfunktion in `loop()`
  einhängen, `requestRedraw()` und `displayModeName()` anpassen.
- **Neuer Benachrichtigungskanal:** wie Pushover als eigene Datei mit `send…()` und `…Enabled()` anlegen
  und in `checkAutoReport()` mit eigenem Tag-Zähler einhängen.
