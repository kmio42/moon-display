# Moon Display
*English | [Deutsch](readme.de.md)*

The Moon Display is a compact desk device built around an ESP32-C3 and a round IPS display that shows the current view of the Moon in real time: not just the phase, but the actual appearance as seen from the observer's location at this very moment. Various display options are available, including texture choice, dimming of the unilluminated side, and whether libration is taken into account.
An internet connection is only needed as a time source — all astronomical calculations are performed on the device itself.

![Moon Display](device.jpg)

## Features
* Real-time moon rendering showing the current appearance as seen from the observer's location
* Accurate moon phase, libration, and axial tilt calculation
* Parallax correction based on the observer's geographic position and elevation
* All astronomical calculations performed locally on the device using formulas from *"Astronomical Algorithms"* by Jean Meeus (German 2nd edition, chapter numbering as in the 1991 English edition)
* Time synchronisation via NTP — internet connection only needed as a time source
* Connects to up to 3 configured Wi-Fi networks (automatic selection by signal strength)
* Texture options: high-resolution texture from NASA's CGI Moon Kit or self-taken photograph
* Optional dimming of the unilluminated lunar side
* Optional bluish tint when the moon is below the horizon
* Second display mode: 24-hour clock with sunrise/sunset, moonrise/moonset and the time the moon is above the horizon
* Telegram bot for changing settings, querying current sun/moon coordinates and an optional daily report
* Daily report can additionally (or exclusively) be sent as a push notification via the [Pushover](https://pushover.net) app
* The Telegram bot's profile photo shows the current moon and is updated daily
* Messages in German or English, switchable at runtime; further languages can be added as a single file
* The same commands via Telegram and the serial interface (115200 baud), plus debug output on the serial interface: Julian date, RA/Dec, azimuth/altitude, libration, phase, parallactic angle, sidereal time, rotation, mask, sunrise/sunset times

## Configuration

### Wi-Fi credentials
Copy `src/credentials.h.template` to `src/credentials.h` and add your networks:

```cpp
#define WIFI_SSID_1     "Network 1"
#define WIFI_PASSWORD_1 "Password 1"

#define WIFI_SSID_2     "Network 2"
#define WIFI_PASSWORD_2 "Password 2"

#define WIFI_SSID_3     ""   // leave empty, if not used
#define WIFI_PASSWORD_3 ""
```

### Telegram bot (optional)
1. Create a bot with [@BotFather](https://t.me/BotFather) and copy the token.
2. Find your own chat ID, e.g. with [@userinfobot](https://t.me/userinfobot).
3. Add both to `src/credentials.h`:
```cpp
#define TELEGRAM_BOT_TOKEN "123456:ABC..."
#define TELEGRAM_CHAT_ID   "12345678"
```
The bot only answers messages from this chat ID. Leave the token empty to disable the bot.

**Group (optional):** the bot can also serve a group. It posts the daily report there and sets the current moon as the group photo.
1. Add the bot to the group and make it an administrator with the right to change group info.
2. Find the group ID (starts with `-100…` for supergroups), e.g. by forwarding a group message to [@userinfobot](https://t.me/userinfobot) or from `getUpdates`.
3. Add it to `src/credentials.h`:
```cpp
#define TELEGRAM_GROUP_ID  "-1001234567890"
```
In the group the bot only reacts to commands sent by the user `TELEGRAM_CHAT_ID`; messages from other members, ordinary messages and commands addressed to other bots (`/cmd@OtherBot`) are ignored. Answers go to the chat where the command was sent. Telegram shows a service message in the group each time the photo changes, i.e. once a day.

The commands are described under [Commands](#commands).

**Daily report:** once a day at the configured time (`daily`) the bot sends a compact report to the private chat, the group (if configured) and Pushover (if configured), for example:
```
☼↑ 07:14 ☼↓ 19:05 (−2'3'')
☽↓ 08:12 ☽↑ 19:40
🌔 73% (⬈) ▅⇡
🌕🔴 28.08.
```
* `☼↑`/`☼↓`: sunrise and sunset, followed by the change of sunset time compared to the previous day (minutes'seconds'')
* `☽↑`/`☽↓`: moonrise and moonset in chronological order; events that do not occur on this day are omitted
* Moon phase symbol, illuminated fraction and `⬈` waxing / `⬊` waning
* `▁▂▃▅▇`: today's culmination height of the moon relative to the range possible at the observer's latitude (declination ±28.6° at major lunar standstill). The outer levels are narrow, so `▁`/`▇` only appear on the few days around the monthly extremes.
* `⇡`/`⇣`: tomorrow's culmination is higher/lower than today's
* Events within the next 14 days (line only present if there are any), each with its local date:
  * `🌕🔴` lunar eclipse (umbral, |β| < 0.95° at full moon), `✗` appended if the moon is below the horizon at mid-eclipse
  * `☀🌑` solar eclipse somewhere on Earth (|β| < 1.45° at new moon; local visibility is not checked)
  * `🌕＋`/`🌕－` super moon (full moon closer than 360 000 km) / mini moon (farther than 405 000 km)

Sunrise/sunset are calculated with Meeus' method, moonrise/moonset by hourly sampling of the moon's altitude with quadratic (Lagrange) interpolation of the horizon crossing, using the horizon altitude after Meeus. Full and new moon are found by bisection of the moon's elongation. All calculations are done in UT; the report covers the local calendar day (events of the UT days overlapping it are converted to local time and filtered by local date).

**Profile and group photo:** after start and then daily at the report time (independent of whether the daily report is switched on), the bot renders the current moon with the fixed display options 13 (darkened unilluminated side, NASA texture, libration), encodes it as a 240×240 JPEG on the device and sets it as its profile photo via `setMyProfilePhoto` and, if a group is configured, as the group photo via `setChatPhoto`. If an update fails, only the failed one is retried every 15 minutes. The JPEG is created in a temporary 48 KB RAM buffer; nothing is stored in flash.

### Pushover (optional)
The daily report can also be delivered as a push notification via [Pushover](https://pushover.net). This works with or without the Telegram bot; if both are configured, the report goes to both.
1. Create an application at [pushover.net/apps/build](https://pushover.net/apps/build) and copy its API token.
2. Copy your user key from the Pushover dashboard.
3. Add both to `src/credentials.h`:
```cpp
#define PUSHOVER_APP_TOKEN "azGD..."
#define PUSHOVER_USER_KEY  "uQiR..."
#define PUSHOVER_DEVICE    ""   // optional: device name, empty = all devices
```
Pushover only receives; the daily report is switched on and its time is set with `daily` via Telegram or the serial interface. If sending fails, it is retried every 5 minutes.

### NTP server
The NTP server and UTC offset are configured in `src/MondPhase.ino`:
```cpp
constexpr char NTP_SERVER[] = "pool.ntp.org";
```

## Commands
Telegram and the serial interface (**115200 baud**) share the same commands. In Telegram they start with `/` (`/moon`), on the serial interface without (`moon`). Command names are English in every language. Settings are stored in flash (NVS) and survive reboots. **A settings command without an argument shows the current value, the possible values and the syntax**; with an argument it changes the setting.

| Command | Channel | Description |
|---|---|---|
| `help` | both | List of commands (`/start` in Telegram as well) |
| `moon` | both | Current moon coordinates in degrees and arcminutes: ecliptic longitude/latitude, RA/Dec, azimuth (counted from south)/altitude; apparent diameter with its position between minimum and maximum, elongation, phase, libration, today's moonrise, culmination (with altitude) and moonset, and the next perigee and apogee (with their position between the extreme values) and node passage |
| `sun` | both | Current sun coordinates: RA/Dec, azimuth/altitude |
| `report` | both | Send the daily report now (also to Pushover, if configured) |
| `image` | Telegram | Current moon image as shown on the display (PNG) |
| `clock` | Telegram | Current image of the 24-hour clock (PNG), regardless of the display mode |
| `avatar` | Telegram | Update the bot's profile photo (and the group photo) now |
| `location [lat lon [elev]]` | both | Observer location in decimal degrees, optionally with elevation above sea level in metres (e.g. `location 51.0 9.0 200`) |
| `options [value]` | both | Display options as a bitmask (see below); without argument a list of all options |
| `mode [moon\|clock]` | both | Display mode: live moon or 24-hour clock |
| `daily [on\|off\|HH:MM]` | both | Daily report on or off; a time (local time) sets the report time and switches it on. Without argument also the configured targets (Telegram chat, group, Pushover) |
| `timezone [POSIX-TZ]` | both | Time zone used for all displayed times (default `CET-1CEST,M3.5.0,M10.5.0/3`); without argument also the local time |
| `language [code]` | both | Language of all messages: `de` (default) or `en`. The Telegram command menu follows the language |
| `show [[YYYY-MM-DD] HH:MM[:SS]]` | serial | Show the moon for a fixed time (UTC, today if no date is given); `show` without argument returns to the live display |
| `time [[YYYY-MM-DD] HH:MM[:SS]]` | serial | Show or set the system time (UTC); NTP overwrites it again at the next synchronisation |
| `wifi [on\|off]` | serial | Wi-Fi status, switch Wi-Fi on or off |

**Display options bitmask** (combine with `+`):

| Bit | Value | Description |
|---|---|---|
| 0 | 1 | Darken unilluminated side (earthshine) |
| 1 | 2 | Bluish tint when moon is below horizon |
| 2 | 4 | Use NASA LROC texture instead of photograph |
| 3 | 8 | Apply libration |
| 4 | 16 | Ignore the parallactic angle: celestial north up instead of the zenith |
| 5 | 32 | Ignore the position angle of the moon's axis: lunar north pole up |

Example: `options 9` enables earthshine + libration.

By default the moon is shown as seen in the sky, with the zenith up. With bit 4 it appears as in an equatorially mounted telescope (celestial north up); with bits 4 and 5 combined, the lunar north pole points straight up, as in a lunar map. The terminator is rotated along with the moon, so the lit features stay correct.

## Display modes

**Moon** (default): real-time rendering of the moon, updated every minute.

**24-hour clock**: midnight at the bottom, 6 am on the left, noon at the top, 6 pm on the right. All times refer to the current local calendar day.
* Light yellow hands: sunrise and sunset
* Light blue hands: moonrise and moonset; the light blue segment at the rim shows when the moon is above the horizon
* Dashed light blue hand with `+1`/`-1`: the next event of the current or upcoming moon passage that falls on the next or previous day (at most one)
* Red hand: current time
* Grey line at the bottom: day boundary (midnight); its lowest 5 pixels turn white while Telegram is configured but not reachable
* Centre: current moon, rendered like the moon display

![24-hour clock mode](clockmode.png)

## Image preview on the PC
`make clock` and `make moon` render the 24-hour clock and the moon image to PNG files (`clock.png`, `moon.png`, 240×240) without the device, using the unchanged project code. This needs `g++`, `make` and the Adafruit GFX library, which `pio run` fetches once into `.pio/libdeps`.
```
make clock ARGS='-t "2026-10-04 18:30" -o 9'
make moon  ARGS='--lat 52.5 --lon 13.4 -t "2026-12-24 20:00" -f weihnachten.png'
```
Options: `-t` local time (`-u`: UTC; default now), `-o` display options bitmask (default 13), `--lat`/`--lon` location (default Darmstadt), `--elev` elevation in metres, `--tz` POSIX time zone, `--square` (clock without round mask), `--no-telegram` (clock with the white "Telegram not reachable" marker), `-f` output file. The tool is in `tools/preview/`.

## Hardware
* ESP32-C3 Super Mini
* 1.28 inch round IPS display (GC9A01)

## Software Requirements
* [VS Code](https://code.visualstudio.com/) with the [pioarduino IDE extension](https://github.com/pioarduino/platform-espressif32)
* The following libraries are fetched automatically by pioarduino:
  * `Adafruit GFX Library`
  * `Adafruit GC9A01A`
  * `UniversalTelegramBot`
  * `ArduinoJson` (v6)
  * `JPEGENC` (JPEG encoder for the bot's profile photo)

## Building and Flashing
1. Clone or download this repository and open the folder in VS Code.
2. pioarduino will automatically install the required libraries on first build.
3. Copy `src/credentials.h.template` to `src/credentials.h` and fill in your Wi-Fi credentials (and optionally the Telegram bot token and chat ID and/or the Pushover app token and user key).
4. Connect the ESP32-C3 via USB.
5. Click **Upload** in the pioarduino toolbar (or run `platformio run --target upload`) to build and flash the firmware.
6. Use the pioarduino **Serial Monitor** at 115200 baud to view debug output.

