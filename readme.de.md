# Mond-Display
*[English](readme.md) | Deutsch*

Das Mond-Display ist ein kompaktes Tischgerät mit einem ESP32-C3 und einem runden IPS-Display. Es zeigt den Mond in Echtzeit: nicht nur die Phase, sondern sein tatsächliches Aussehen, wie es am Standort in diesem Moment zu sehen ist. Mehrere Darstellungsoptionen stehen zur Wahl, darunter die Textur, das Abdunkeln der unbeleuchteten Seite und die Berücksichtigung der Libration.
Das Internet dient nur als Zeitquelle – alle astronomischen Berechnungen laufen auf dem Gerät selbst.

![Mond-Display](device.jpg)

## Funktionen
* Echtzeit-Darstellung des Mondes, wie er vom Standort aus gerade aussieht
* Genaue Berechnung von Mondphase, Libration und Lage der Mondachse
* Parallaxenkorrektur nach geographischer Position und Höhe des Standorts
* Alle astronomischen Berechnungen auf dem Gerät, mit den Formeln aus *„Astronomische Algorithmen“* von Jean Meeus (2. deutsche Auflage, Kapitelzählung wie in der englischen Ausgabe von 1991)
* Zeitabgleich über NTP – das Internet wird nur als Zeitquelle gebraucht
* Verbindet sich mit bis zu 3 konfigurierten WLANs (automatische Auswahl nach Signalstärke)
* Texturen: hochaufgelöste Textur aus dem CGI Moon Kit der NASA oder ein eigenes Foto
* Optional abgedunkelte unbeleuchtete Seite
* Optional bläuliche Tönung, wenn der Mond unter dem Horizont steht
* Zweiter Anzeigemodus: 24-Stunden-Uhr mit Sonnenauf- und -untergang, Mondauf- und -untergang und der Zeit, in der der Mond über dem Horizont steht
* Telegram-Bot zum Ändern der Einstellungen, für aktuelle Sonnen- und Mondkoordinaten und einen optionalen Tagesbericht
* Der Tagesbericht kann zusätzlich (oder nur) als Push-Nachricht über die App [Pushover](https://pushover.net) kommen
* Das Profilbild des Telegram-Bots zeigt den aktuellen Mond und wird täglich erneuert
* Ausgaben auf Deutsch oder Englisch, zur Laufzeit umschaltbar; weitere Sprachen lassen sich als einzelne Datei ergänzen
* Dieselben Befehle über Telegram und die serielle Schnittstelle (115200 Baud), dazu Debug-Ausgaben auf der seriellen Schnittstelle: Julianisches Datum, RA/Dek, Azimut/Höhe, Libration, Phase, parallaktischer Winkel, Sternzeit, Drehung, Maske, Sonnenauf- und -untergang

## Konfiguration

### WLAN-Zugangsdaten
`src/credentials.h.template` nach `src/credentials.h` kopieren und die Netzwerke eintragen:

```cpp
#define WIFI_SSID_1     "Netzwerk 1"
#define WIFI_PASSWORD_1 "Passwort 1"

#define WIFI_SSID_2     "Netzwerk 2"
#define WIFI_PASSWORD_2 "Passwort 2"

#define WIFI_SSID_3     ""   // leer lassen, wenn nicht benutzt
#define WIFI_PASSWORD_3 ""
```

### Telegram-Bot (optional)
1. Mit [@BotFather](https://t.me/BotFather) einen Bot anlegen und das Token kopieren.
2. Die eigene Chat-ID herausfinden, z. B. mit [@userinfobot](https://t.me/userinfobot).
3. Beides in `src/credentials.h` eintragen:
```cpp
#define TELEGRAM_BOT_TOKEN "123456:ABC..."
#define TELEGRAM_CHAT_ID   "12345678"
```
Der Bot beantwortet nur Nachrichten dieser Chat-ID. Ist das Token leer, ist der Bot abgeschaltet.

**Gruppe (optional):** Der Bot kann auch eine Gruppe bedienen. Er postet dort den Tagesbericht und setzt den aktuellen Mond als Gruppenbild.
1. Den Bot zur Gruppe hinzufügen und zum Administrator mit dem Recht „Gruppeninfo ändern“ machen.
2. Die Gruppen-ID herausfinden (beginnt bei Supergruppen mit `-100…`), z. B. indem man eine Nachricht der Gruppe an [@userinfobot](https://t.me/userinfobot) weiterleitet, oder aus `getUpdates`.
3. In `src/credentials.h` eintragen:
```cpp
#define TELEGRAM_GROUP_ID  "-1001234567890"
```
In der Gruppe reagiert der Bot nur auf Befehle des Nutzers `TELEGRAM_CHAT_ID`. Nachrichten anderer Mitglieder, normale Nachrichten und Befehle an andere Bots (`/befehl@AndererBot`) werden ignoriert. Antworten gehen in den Chat, aus dem der Befehl kam. Telegram zeigt in der Gruppe bei jedem neuen Bild eine Servicemeldung, also einmal am Tag.

Die Befehle stehen unter [Befehle](#befehle).

**Tagesbericht:** Einmal am Tag zur eingestellten Uhrzeit (`daily`) schickt der Bot einen kurzen Bericht an den privaten Chat, an die Gruppe (falls eingerichtet) und an Pushover (falls eingerichtet), zum Beispiel:
```
☼↑ 07:14 ☼↓ 19:05 (−2'3'')
☽↓ 08:12 ☽↑ 19:40
🌔 73% (⬈) ▅⇡
🌕🔴 28.08.
```
* `☼↑`/`☼↓`: Sonnenauf- und -untergang, danach die Änderung des Sonnenuntergangs gegenüber dem Vortag (Minuten'Sekunden'')
* `☽↑`/`☽↓`: Mondauf- und -untergang in zeitlicher Reihenfolge; was an diesem Tag nicht stattfindet, fehlt
* Symbol der Mondphase, beleuchteter Anteil und `⬈` zunehmend / `⬊` abnehmend
* `▁▂▃▅▇`: Kulminationshöhe des Mondes heute im Verhältnis zum Bereich, der an der Breite des Standorts möglich ist (Deklination ±28,6° bei der großen Mondwende). Die äußeren Stufen sind schmal, `▁`/`▇` erscheinen also nur an den wenigen Tagen um die monatlichen Extreme.
* `⇡`/`⇣`: Die Kulmination morgen ist höher/tiefer als heute
* Ereignisse der nächsten 14 Tage (die Zeile gibt es nur, wenn eines ansteht), jeweils mit lokalem Datum:
  * `🌕🔴` Mondfinsternis (Kernschatten, |β| < 0,95° bei Vollmond), dahinter `✗`, wenn der Mond zur Finsternismitte unter dem Horizont steht
  * `☀🌑` Sonnenfinsternis irgendwo auf der Erde (|β| < 1,45° bei Neumond; die Sichtbarkeit am Standort wird nicht geprüft)
  * `🌕＋`/`🌕－` Supermond (Vollmond näher als 360 000 km) / Minimond (weiter als 405 000 km)

Sonnenauf- und -untergang werden nach Meeus berechnet, Mondauf- und -untergang durch stündliches Abtasten der Mondhöhe und quadratische (Lagrange-)Interpolation des Horizontdurchgangs, mit der Horizonthöhe nach Meeus. Voll- und Neumond werden durch Bisektion der Elongation des Mondes bestimmt. Gerechnet wird in UT; der Bericht gilt für den lokalen Kalendertag (die Ereignisse der UT-Tage, die ihn überlappen, werden in Lokalzeit umgerechnet und nach lokalem Datum gefiltert).

**Profil- und Gruppenbild:** Nach dem Start und danach täglich zur Berichtszeit (unabhängig davon, ob der Tagesbericht eingeschaltet ist) rendert der Bot den aktuellen Mond mit den festen Darstellungsoptionen 13 (abgedunkelte unbeleuchtete Seite, NASA-Textur, Libration), kodiert ihn auf dem Gerät als JPEG mit 240×240 Pixeln und setzt ihn mit `setMyProfilePhoto` als Profilbild und, falls eine Gruppe eingerichtet ist, mit `setChatPhoto` als Gruppenbild. Schlägt eines davon fehl, wird nur dieses alle 15 Minuten wiederholt. Das JPEG entsteht in einem temporären 48-KB-Puffer im RAM; im Flash wird nichts gespeichert.

### Pushover (optional)
Der Tagesbericht kann auch als Push-Nachricht über [Pushover](https://pushover.net) kommen, mit oder ohne Telegram-Bot; sind beide eingerichtet, geht der Bericht an beide.
1. Unter [pushover.net/apps/build](https://pushover.net/apps/build) eine Anwendung anlegen und deren API-Token kopieren.
2. Den User Key aus dem Pushover-Dashboard kopieren.
3. Beides in `src/credentials.h` eintragen:
```cpp
#define PUSHOVER_APP_TOKEN "azGD..."
#define PUSHOVER_USER_KEY  "uQiR..."
#define PUSHOVER_DEVICE    ""   // optional: Gerätename, leer = alle Geräte
```
Pushover empfängt nur. Ein- und Ausschalten des Tagesberichts sowie die Uhrzeit laufen über `daily` per Telegram oder serieller Schnittstelle. Schlägt das Senden fehl, wird es alle 5 Minuten wiederholt.

### NTP-Server
NTP-Server und UTC-Versatz stehen in `src/MondPhase.ino`:
```cpp
constexpr char NTP_SERVER[] = "pool.ntp.org";
```

## Befehle
Telegram und die serielle Schnittstelle (**115200 Baud**) haben dieselben Befehle. In Telegram beginnen sie mit `/` (`/moon`), seriell ohne (`moon`). Die Befehlsnamen sind in jeder Sprache englisch. Einstellungen liegen im Flash (NVS) und bleiben nach einem Neustart erhalten. **Ein Einstellungsbefehl ohne Argument zeigt den aktuellen Wert, die möglichen Werte und die Syntax**, mit Argument ändert er die Einstellung.

| Befehl | Kanal | Beschreibung |
|---|---|---|
| `help` | beide | Befehlsübersicht (in Telegram auch `/start`) |
| `moon` | beide | Aktuelle Mondkoordinaten in Grad und Bogenminuten: ekliptikale Länge/Breite, RA/Dek, Azimut (von Süden gezählt)/Höhe; scheinbarer Durchmesser mit seiner Lage zwischen Minimum und Maximum, Elongation, Phase, Libration, Mondaufgang, Kulmination (mit Höhe) und Monduntergang des Tages sowie nächstes Perigäum und Apogäum (mit ihrer Lage zwischen den Extremwerten) und nächster Knotendurchgang |
| `sun` | beide | Aktuelle Sonnenkoordinaten: RA/Dek, Azimut/Höhe |
| `report` | beide | Tagesbericht jetzt senden (auch an Pushover, falls eingerichtet) |
| `image` | Telegram | Aktuelles Mondbild wie auf dem Display (PNG) |
| `clock` | Telegram | Aktuelles Bild der 24-Stunden-Uhr (PNG), unabhängig vom Anzeigemodus |
| `avatar` | Telegram | Profilbild des Bots (und Gruppenbild) jetzt erneuern |
| `location [lat lon [elev]]` | beide | Standort in Dezimalgrad, optional mit Höhe über dem Meeresspiegel in Metern (z. B. `location 51.0 9.0 200`) |
| `options [value]` | beide | Darstellungsoptionen als Bitfeld (siehe unten); ohne Argument eine Liste aller Optionen |
| `mode [moon\|clock]` | beide | Anzeigemodus: Live-Mond oder 24-Stunden-Uhr |
| `daily [on\|off\|HH:MM]` | beide | Tagesbericht ein oder aus; eine Uhrzeit (Lokalzeit) setzt die Berichtszeit und schaltet ihn ein. Ohne Argument auch die eingerichteten Ziele (Telegram-Chat, Gruppe, Pushover) |
| `timezone [POSIX-TZ]` | beide | Zeitzone aller angezeigten Zeiten (Standard `CET-1CEST,M3.5.0,M10.5.0/3`); ohne Argument auch die Lokalzeit |
| `language [code]` | beide | Sprache aller Ausgaben: `de` (Standard) oder `en`. Das Befehlsmenü in Telegram folgt der Sprache |
| `show [[YYYY-MM-DD] HH:MM[:SS]]` | seriell | Mond zu einer festen Zeit anzeigen (UTC, ohne Datum heute); `show` ohne Argument kehrt zur Live-Anzeige zurück |
| `time [[YYYY-MM-DD] HH:MM[:SS]]` | seriell | Systemzeit (UTC) anzeigen oder setzen; NTP überschreibt sie bei der nächsten Synchronisation |
| `wifi [on\|off]` | seriell | WLAN-Status, WLAN ein- oder ausschalten |

**Bitfeld der Darstellungsoptionen** (Werte addieren):

| Bit | Wert | Beschreibung |
|---|---|---|
| 0 | 1 | Unbeleuchtete Seite abdunkeln (Erdschein) |
| 1 | 2 | Bläuliche Tönung, wenn der Mond unter dem Horizont steht |
| 2 | 4 | NASA-LROC-Textur statt Foto |
| 3 | 8 | Libration berücksichtigen |
| 4 | 16 | Parallaktischen Winkel ignorieren: Himmelsnorden oben statt Zenit |
| 5 | 32 | Positionswinkel der Mondachse ignorieren: Mondnordpol oben |

Beispiel: `options 9` schaltet Erdschein und Libration ein.

Ohne Optionen erscheint der Mond wie am Himmel, mit dem Zenit oben. Mit Bit 4 sieht er aus wie in einem parallaktisch montierten Fernrohr (Himmelsnorden oben), mit Bit 4 und 5 zusammen zeigt der Mondnordpol senkrecht nach oben, wie auf einer Mondkarte. Der Terminator dreht sich mit, die beleuchteten Formationen bleiben also richtig.

## Anzeigemodi

**Mond** (Standard): Echtzeit-Darstellung des Mondes, jede Minute aktualisiert.

**24-Stunden-Uhr**: Mitternacht unten, 6 Uhr links, Mittag oben, 18 Uhr rechts. Alle Zeiten beziehen sich auf den aktuellen lokalen Kalendertag.
* Hellgelbe Zeiger: Sonnenauf- und -untergang
* Hellblaue Zeiger: Mondauf- und -untergang; das hellblaue Segment am Rand zeigt, wann der Mond über dem Horizont steht
* Gestrichelter hellblauer Zeiger mit `+1`/`-1`: das nächste Ereignis des laufenden oder kommenden Mondlaufs, das auf den nächsten oder vorigen Tag fällt (höchstens eines)
* Roter Zeiger: aktuelle Uhrzeit
* Graue Linie unten: Tagesgrenze (Mitternacht); ihre unteren 5 Pixel werden weiß, solange Telegram eingerichtet, aber nicht erreichbar ist
* Mitte: aktueller Mond, gerendert wie im Mond-Modus

![24-Stunden-Uhr](clockmode.png)

## Bildvorschau auf dem PC
`make clock` und `make moon` erzeugen die 24-Stunden-Uhr und das Mondbild als PNG (`clock.png`, `moon.png`, 240×240) ohne Gerät, mit dem unveränderten Projektcode. Dafür braucht es `g++`, `make` und die Bibliothek Adafruit GFX, die `pio run` einmal nach `.pio/libdeps` holt.
```
make clock ARGS='-t "2026-10-04 18:30" -o 9'
make moon  ARGS='--lat 52.5 --lon 13.4 -t "2026-12-24 20:00" -f weihnachten.png'
```
Optionen: `-t` Lokalzeit (`-u`: UTC; Standard jetzt), `-o` Bitfeld der Darstellungsoptionen (Standard 13), `--lat`/`--lon` Standort (Standard Darmstadt), `--elev` Höhe in Metern, `--tz` POSIX-Zeitzone, `--square` (Uhr ohne runde Maske), `--no-telegram` (Uhr mit der weißen Markierung „Telegram nicht erreichbar“), `-f` Ausgabedatei. Das Programm liegt in `tools/preview/`.

## Hardware
* ESP32-C3 Super Mini
* Rundes 1,28-Zoll-IPS-Display (GC9A01)

## Software
* [VS Code](https://code.visualstudio.com/) mit der [pioarduino-IDE-Erweiterung](https://github.com/pioarduino/platform-espressif32)
* Folgende Bibliotheken lädt pioarduino automatisch:
  * `Adafruit GFX Library`
  * `Adafruit GC9A01A`
  * `UniversalTelegramBot`
  * `ArduinoJson` (v6)
  * `JPEGENC` (JPEG-Encoder für das Profilbild des Bots)

## Bauen und Flashen
1. Repository klonen oder herunterladen und den Ordner in VS Code öffnen.
2. pioarduino installiert die nötigen Bibliotheken beim ersten Bauen.
3. `src/credentials.h.template` nach `src/credentials.h` kopieren und die WLAN-Zugangsdaten eintragen (optional auch Token und Chat-ID des Telegram-Bots und/oder App-Token und User Key von Pushover).
4. Den ESP32-C3 per USB anschließen.
5. In der pioarduino-Leiste auf **Upload** klicken (oder `platformio run --target upload` ausführen), um die Firmware zu bauen und zu flashen.
6. Den **Serial Monitor** von pioarduino mit 115200 Baud öffnen, um die Debug-Ausgaben zu sehen.
