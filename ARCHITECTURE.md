# Architecture
*English | [Deutsch](ARCHITECTURE.de.md)*

This document describes how the firmware of the Moon Display is structured. How to operate and
configure the device is described in the [readme.md](readme.md) (German: [readme.de.md](readme.de.md)).

## Overview

The firmware runs on an ESP32-C3 (Arduino framework, PlatformIO/pioarduino) and drives a round
240×240 display (GC9A01, SPI). The network is only used as a time source (NTP) and for the
notification channels (Telegram, Pushover). **All astronomical calculations run on the device.**

```
                    ┌────────────────────────────── MondPhase.ino ──────────────────────────────┐
                    │  setup(), loop(), read serial lines, mode switching, Wi-Fi                │
                    └───────┬───────────────────┬──────────────────────┬────────────────────────┘
                            │                   │                      │
                            │         ┌─────────▼──────────┐           │
                            │         │ Commands           │           │
                            │         │ Commands.ino       │──────────►│
                            │         │ Messages.ino, i18n │           │
                            │         └─────────┬──────────┘           │
              ┌─────────────▼───┐     ┌─────────▼──────────┐    ┌──────▼───────────────┐
              │ Display         │     │ Notification       │    │ Configuration        │
              │ Moon.ino        │     │ Telegram.ino       │    │ Config.ino (NVS)     │
              │ clock_face.cpp  │     │ Pushover.ino       │    │ credentials.h        │
              └───────┬─────────┘     └─────────┬──────────┘    └──────────────────────┘
                      │                         │
              ┌───────▼─────────┐               │ (daily report, moon image)
              │ moon_render.cpp │◄──────────────┤
              │ png_stream.cpp  │◄──────────────┘
              │ picture.ino     │  textures (flash)
              └───────┬─────────┘
                      │
              ┌───────▼─────────┐
              │ ephemeris.cpp   │  SkyState, daily events, outlook
              └───────┬─────────┘
                      │
              ┌───────▼─────────┐
              │ astro.cpp       │  formulas after Meeus, pure maths
              └─────────────────┘
```

The layers are strictly ordered: a layer only knows the layers below it. `astro.cpp` and
`ephemeris.cpp` know nothing about the display or the network.

## Files

| File | Purpose |
|---|---|
| [MondPhase.ino](src/MondPhase.ino) | Entry point: `setup()`, `loop()`, Wi-Fi connection, NTP, reading serial lines, mode switching |
| [Commands.ino](src/Commands.ino) | Command table for the serial interface and Telegram ([commands.h](src/commands.h)) |
| [Messages.ino](src/Messages.ino) | Daily report, moon and sun coordinates as text, formatting ([messages.h](src/messages.h)) |
| [i18n.cpp](src/i18n.cpp) | Translated texts: text IDs in [i18n_ids.h](src/i18n_ids.h), one file per language in [lang/](src/lang/) |
| [Config.ino](src/Config.ino) | Persistent settings in NVS, conversion UTC → local time, validation of time zone and `HH:MM` |
| [Moon.ino](src/Moon.ino) | Moon display: prepares rendering and writes the image to the display line by line; debug output |
| [clock_face.cpp](src/clock_face.cpp) | 24-hour clock: drawing of ring, segment, hands and moon symbol |
| [moon_render.cpp](src/moon_render.cpp) | Moon image as a function `moonPixel(x, y)` or scaled down `moonPixelAveraged()`, without an image buffer |
| [picture.ino](src/picture.ino) | Textures in flash: `FullMoon` (photo, 240×240) and `lroc` (NASA map, 480×240) |
| [png_stream.cpp](src/png_stream.cpp) | PNG writer that produces the image piece by piece |
| [ephemeris.cpp](src/ephemeris.cpp) | Sun, moon, rising/setting and outlook for a location and point in time |
| [astro.cpp](src/astro.cpp) | Astronomical formulas after Jean Meeus, data types for angles, sidereal time and Julian Day |
| [Telegram.ino](src/Telegram.ino) | Bot (polling, access rules), daily report, moon and clock image, profile and group photo |
| [Pushover.ino](src/Pushover.ino) | Messages via the Pushover API |
| [display_mode.h](src/display_mode.h) | Constants of the display modes |
| [credentials.h](src/credentials.h.template) | Wi-Fi and service credentials (not in the repository) |

The `.ino` files are parts of a single sketch. The Arduino build concatenates them, so they call
each other via `extern` declarations. Types shared between them live in headers (`commands.h`,
`messages.h`). The `.cpp` files are separate translation units with a header and contain the logic
that should also compile without the device (see [Preview on the PC](#preview-on-the-pc)).

## Runtime flow

There is a single flow of control, no tasks and no interrupts. `loop()` in
[MondPhase.ino](src/MondPhase.ino) repeats roughly every 100 ms:

1. Maintain Wi-Fi (`WiFiMulti`, up to 3 networks, at most once per second).
2. Read serial commands.
3. `handleTelegram()`: fetch messages (every 3 s), check the daily report and moon photos.
4. Draw according to the display mode: in moon mode once per minute, in clock mode the hour hand
   once per minute.

Consequently, anything that takes long blocks the display, Telegram and the serial commands.
Measured durations on the device (ESP32-C3 without FPU, 160 MHz):

| Operation | Duration |
|---|---|
| Redraw the whole clock (day change, start) | 1.7–2.6 s, of which 0.9 s moon ephemerides |
| Moon symbol of the clock (every 5 minutes) | approx. 1.5 s |
| Moon mode, full image (every minute) | 4.3 s |
| Daily report | 1.7 s |
| Render and upload profile and group photo | approx. 6 s |
| Clock, hour hand only (every minute) | approx. 20 ms |

`setup()` loads the configuration, starts Wi-Fi and NTP (`configTime`), initialises the display
and calls `setupTelegram()`. Until NTP has synchronised, the system time is before 2023. All parts
check this (`MIN_VALID_TIME`) and wait.

## Time conventions

- The system runs in **UTC** (`TZ=UTC0`). All calculations in `astro` and `ephemeris` use UT.
- Only the output is converted to local time, with `utcToLocal()` in [Config.ino](src/Config.ino)
  and the POSIX string `configLocation.timezone`. The function switches `TZ` only briefly and then
  sets it back to UTC. `TZ` is not set with `setenv()` but overwritten in a buffer allocated once
  (`setTimezone()`): newlib's `setenv()` does not free the old value when the new one is longer, and
  the constant switching used to drain the heap.
- Daily values (sunrise/sunset, moon events, daily report) refer to the **local calendar day**.
  `eventsForLocalDay()` computes the events of the UT days overlapping the local day and filters them
  by local date.
- A local day is kept as the key `year*1000 + day of year` (`localDayKey`).

## Astronomy

**[astro.cpp](src/astro.cpp)** contains the formulas from Meeus, *Astronomische Algorithmen*, 2nd
edition (Barth 1994; chapter numbering as in the 1991 English edition *Astronomical Algorithms*):
Julian Day, sidereal time, sun, moon, coordinate transformations, parallax, libration, parallactic
angle, sunrise and sunset, perigee and apogee of the moon (ch. 48) and its node passages (ch. 49).
Which chapters are implemented and which are not is listed in the header of [astro.h](src/astro.h).
Comments name chapter and formula number, polynomials use Horner's scheme (`horner(T, c0, c1, …)`
with the coefficients in the book's order). It has no dependencies apart from the C++ standard
library. TD is equated with UT throughout (ΔT ≈ 1 min is neglected).

Units are enforced by data types that cost nothing at runtime (wrappers around `double`):

- `Angle`: an angle, internally in radians. It is only created with a unit (`Angle::fromDeg`,
  `fromHours`, `fromDms`, literal `90_deg`) and read with `.deg()`, `.rad()`, `.hours()`.
  `sin`/`cos`/`tan` take an `Angle` directly, the inverse functions are `Angle::asin` etc.
- `SiderealTime`: sidereal time. Only `θ0 + longitude → θ` and `θ − α → hour angle` are allowed.
- `JulianDay`: a point in time as a parameter, with `fromCalendar`, `fromTm`, `fromUnixTime`,
  `toUnixTime`, `centuries()` (T) and `midnight()`. Results such as rising/setting (hours UT) or the
  JDE of perigee and node remain `double`.

Shared structures: `Ecliptic` (sun and moon), `Equatorial`, `Horizontal`, `Selenographic`
(libration), `RiseTransitSet` (rising/setting of sun and moon), `Apsis`, `NodePassage` and
`GeoLocation` (longitude, latitude, elevation above sea level, time zone). The elevation enters the
topocentric correction (ch. 39).

**[ephemeris.cpp](src/ephemeris.cpp)** builds on it and knows the location (`configLocation`):

- `computeSky(utc)` returns a `SkyState`: sun and moon (the moon topocentric, i.e. corrected for
  parallax), azimuth and altitude, phase, waxing or waning, position angle `chi` of the bright limb,
  parallactic angle `q`, moon axis and libration.
- `eventsForLocalDay()` returns rising and setting of sun and moon and the meridian transit of the
  moon as UTC times. There is no Meeus formula for moonrise, so the moon's altitude is sampled hourly
  and the horizon crossing is found by quadratic interpolation. This costs about 0.2 s per UT day,
  so the results of the last 5 UT days are kept in a cache (`moonRiseSetForUtDay`) shared by the
  clock, the daily report and the culmination height. The cache is cleared when the location changes.
- `moonOutlookFor()` determines, for the daily report, the culmination height, its trend and the
  events of the next 14 days: full and new moon (by bisection of the elongation), eclipses, super
  and mini moon.
- `moonPhaseOf()` returns the phase as `MoonPhase` (8 phases) for name and symbol; the name comes
  from i18n, so `astro` and `ephemeris` contain no text.

## Moon image

[moon_render.cpp](src/moon_render.cpp) renders the moon **without an image buffer**. The ESP32-C3
does not have enough RAM for a 240×240 image at 16 bits, so `moonPixel(x, y)` returns the colour of a
single pixel and the caller decides where it goes:

- Display: [Moon.ino](src/Moon.ino) computes one line inside the moon disc and writes it with
  `drawRGBBitmap` as one block (one address window per line instead of per pixel).
- Telegram: [png_stream.cpp](src/png_stream.cpp) fetches line by line and sends directly; the JPEG
  encoder for the profile photo fetches 16×16 blocks.
- Clock mode: [clock_face.cpp](src/clock_face.cpp) scales the image down to the moon symbol with
  `moonPixelAveraged()`: limb and illumination are checked at 2×2 sample points per pixel, the
  texture is read only once in the centre (average of 2×2 texels). Here too, output is line by line.

Preparation and rendering are separate: `prepareMoonRenderAt(utc)` computes the sky state once and
stores phase, angles, rotation matrix and options in static variables. `moonPixel()` uses these
values for every pixel. Rendering is therefore **not reusable for several simultaneous images**:
calling another `prepare` between `prepare` and the last `moonPixel` corrupts the image.

For each pixel:

1. Is it inside the moon disc?
2. Is it illuminated? The terminator is an ellipse scaled with the phase and rotated by the mask
   angle. Together with the disc this gives the illuminated area.
3. What is the texture colour at this point? With libration, the point on the sphere is rotated back
   with the rotation matrix and looked up in the NASA map (`lroc`) or the photo. This calculation runs
   in `float` (one texel corresponds to about 0.75°) and without normalisation, since the rotated
   vector already has length r; without an FPU it is still the most expensive part.
4. Darkening of the unlit side and bluish tint below the horizon, if selected.

The display options are a bit mask (see [moon_render.h](src/moon_render.h)): darken, tint, NASA
texture, libration, ignore parallactic angle and ignore position angle. The "ignore" options rotate
texture and terminator together, so the terminator stays on the same lunar features.

The Telegram profile photo uses **fixed** options (13), independent of the setting.

## Clock mode

[clock_face.cpp](src/clock_face.cpp) draws a 24-hour dial (midnight at the bottom, noon at the top).
`computeClockData()` maps the day's events to hours. The events are cached per local day; only the
dashed line of the moon's passage depends on the time of day.

Drawing is incremental, because the full dial takes noticeable time on the device:

- **Full redraw** the first time, at day change and when hands or segment change.
- **Every minute** only the old hour hand is painted over in black, the elements near it are redrawn
  (`NEAR_HOURS`), and the new hand is drawn.
- **Moon symbol** every 5 minutes and when crossing the horizon (tint).
- **Telegram connection:** the lowest 5 pixels of the midnight marker are white while Telegram is
  configured but not reachable. The clock queries `telegramConnectionLost()` from
  [Telegram.ino](src/Telegram.ino); if the state changes, only these pixels are redrawn.

Ring and moon segment are drawn pixel by pixel around the true centre (119.5 | 119.5) so they are
round and symmetric. Whether a pixel lies in the segment follows, without trigonometric functions,
from the signs of cross and dot product with the boundary rays. Thick lines consist of two triangles,
because Adafruit_GFX has no thick lines. Vertical and horizontal lines are rectangles.

**Image for Telegram (`/clock`):** `clockImageBegin()` draws ring, segment, hands and digits as in a
full redraw, but into a separate canvas with 4 bits per pixel (the clock has fewer than 16 colours,
28.8 KB instead of 115 KB for RGB565). The moon symbol is only computed when the pixels are fetched
in `clockImagePixel()`, without an image buffer as in `moon_render`. This works because the moon
symbol does not overlap any other element. The display's drawing state stays untouched, so the
image can also be created in moon mode.

## Telegram and Pushover

[Telegram.ino](src/Telegram.ino) polls every 3 seconds without long polling, so the display keeps
running.

**Connection state:** `updateTelegramConnection()` checks with `getMe` whether Telegram is reachable,
once per minute while connected, otherwise every 10 seconds; without Wi-Fi Telegram counts as not
reachable. `getUpdates` is not suitable for this, because it answers both "no new messages" and "no
connection" with 0. The clock shows the state (see clock mode). If Telegram is not configured, the
connection does not count as lost.
After a failed check the client is always closed, and without a connection `getUpdates` is skipped so
the loop does not wait for timeouts. The library waits 5 s instead of 1.5 s for responses and accepts
updates up to 4000 bytes.

**Unreadable updates:** if the library cannot parse an update (e.g. too large), `getUpdates` returns
0 without advancing the offset, and all following messages get stuck. `skipUnreadableUpdate()`
therefore fetches the next pending update itself after each connection check; if the same
`update_id` is pending at two consecutive checks, it is skipped.

**Who may do what:**

- Commands are only accepted if the sender is `TELEGRAM_CHAT_ID`, in the private chat or in the
  group `TELEGRAM_GROUP_ID`. Everything else is ignored. In the group the bot only reacts to
  commands, not to ordinary text and not to `/command@OtherBot`.
- Replies go to the chat the command came from.

**Daily schedule at the report time** (`configAutoTime`, each target tracked independently, retried
on failure). The report is computed once per run and sent to all due targets:

| Target | Content | Retry |
|---|---|---|
| Telegram, private chat | daily report | next run |
| Telegram, group | daily report | next run |
| Pushover | daily report | every 5 minutes |
| Bot profile photo and group photo | rendered moon image | every 15 minutes, only the failed target |

Profile and group photo are created from **one** JPEG (240×240, in a temporary 48 KB buffer, nothing
is stored in flash). The `UniversalTelegramBot` library does not know `setMyProfilePhoto` and
`setChatPhoto`, so `uploadJpeg()` builds the multipart request itself.

[Pushover.ino](src/Pushover.ino) is outgoing only. It opens a separate TLS connection per message and
releases it afterwards, so the memory for TLS is not permanently occupied.

Telegram and Pushover are optional and independent. If the credentials in `credentials.h` are
missing, the respective part is disabled. Pushover does not need Telegram.

## Commands

[Commands.ino](src/Commands.ino) contains **one** table `COMMANDS` for both channels. Each entry has
an English name, a syntax (`usage`), a translated description (`Txt`), the channels (`CH_SERIAL`,
`CH_TELEGRAM`) and a handler. The help (`help`) and the Telegram command menu
(`buildBotCommandsJson()` for `setMyCommands`) are generated from the table.

- **Serial:** `readSerialCommands()` in [MondPhase.ino](src/MondPhase.ino) collects characters up to
  the end of the line (at most 99) and calls `runCommand(CH_SERIAL, …)`. HTML replies (`moon`) are
  printed without the `<pre>` tags.
- **Telegram:** `handleCommand()` in [Telegram.ino](src/Telegram.ino) strips `@BotName`, ignores
  commands addressed to other bots and calls `runCommand(CH_TELEGRAM, …)`. The access rules (see
  above) are checked beforehand in `handleTelegram()`.
- `runCommand()` accepts the name with or without `/`, ignores case and rejects commands that do not
  exist on the channel. A handler receives the rest of the line as `args` and returns a `Reply`; if
  the text is empty, the handler has already answered itself (images).
- **Settings commands** (`location`, `options`, `mode`, `daily`, `timezone`, `language`, serial
  `time`, `wifi`) show the value and the syntax without an argument; with an argument they set it,
  call `saveConfig()` and, where needed, `requestRedraw()` or `resetAutoReportDay()`, and then show
  the new value. There is no separate `config` command.

## Languages

All output to users (replies, help, daily report, image captions) goes through `tr(Txt::…)` from
[i18n.h](src/i18n.h); log output on the serial interface stays German.

- [i18n_ids.h](src/i18n_ids.h) lists all text IDs (X macro `TEXT_IDS`). If an ID ends in `Fmt`, the
  text is a printf format; fill it with `textf()` from [messages.h](src/messages.h). If it ends in
  `Pattern`, it is a `strftime` format.
- Each language lives in [lang/](src/lang/) as a list of pairs `{Txt::…, "…"}` in any order. It is
  registered with one line in `LANGUAGES` in [i18n.cpp](src/i18n.cpp); `language()` sorts the texts
  into an array at compile time, so `tr()` is an array access. The texts are stored in flash.
- **English** is the reference and must contain every ID. IDs missing in other languages appear in
  English, so a language can be translated step by step.
- `static_assert` checks at build time: missing IDs in English, duplicate IDs, and for `…Fmt` the
  same placeholders (type and order) as in English, because newlib's `snprintf` has no positional
  arguments. The message names language and ID, e.g.
  `static assertion failed: de: Platzhalter weichen von en ab: DailyFmt`. This uses `static_assert`
  with a generated message (C++26, GCC 14 and later).
- newlib only prints English month names for `%b`; other languages use digits.
- After switching with `language`, Telegram sets the command menu again with the new descriptions.

## Configuration

All settings are kept as a RAM copy in global variables in [Config.ino](src/Config.ino) and are
written to NVS flash (`Preferences`, namespace `moon-cfg`) with `saveConfig()` when they change.
They are changed with the commands (serial or Telegram), which set the variables and call
`saveConfig()`.

| Key | Meaning |
|---|---|
| `lat`, `lon` | Location in decimal degrees (in RAM as `configLocation`, type `astro::GeoLocation`) |
| `elev` | Elevation above sea level in metres |
| `opts` | Display options (bit mask) |
| `auto`, `atime` | Daily report on/off, time in minutes after midnight |
| `tz` | POSIX time zone |
| `mode` | Display mode (moon or clock) |
| `lang` | Language as code (`de`, `en`); in RAM as index `configLanguage` of the registered languages |

Credentials (Wi-Fi, Telegram, Pushover) are **not** stored in NVS but compiled into the program via
`credentials.h`. This file is in `.gitignore`; `credentials.h.template` serves as a template.

## Memory and resources

- No image buffer; pixels are computed individually and drawn line by line (see above). This saves
  RAM but makes rendering computationally expensive.
- The cache for moonrise/moonset occupies 312 bytes.
- The SPI transfer to the display (40 MHz) is not a bottleneck: a full image takes about 25–30 ms,
  computation dominates.
- The textures are in flash (`PROGMEM`). The partition table `huge_app.csv` makes room for them.
- The profile JPEG briefly occupies 48 KB of heap, the JPEG encoder object about 3 KB (on the heap,
  not on the stack).
- The clock image for `/clock` occupies 28.8 KB of heap during the upload.
- Telegram uses a persistent `WiFiClientSecure`, Pushover creates its own per message.

## Preview on the PC

The `.cpp` files compile without the device. [tools/preview/](tools/preview/) contains a program that
writes the clock and the moon image as PNG. `tools/preview/stub/` provides a minimal replacement for
the Arduino headers, and `Adafruit_GFX` draws into a memory buffer instead of the display.
`drawClockFace()` therefore takes a `ClockDisplay`: on the device `Adafruit_SPITFT` (fast
`drawRGBBitmap`), in the preview `Adafruit_GFX`.
The invocation (`make clock`, `make moon`) is in the [Makefile](Makefile) and in the readme.md.

This way drawing code can be changed and checked without flashing every time. The preview code only
replaces `utcToLocal()`, `telegramConnectionLost()` and the configuration variables from
[Config.ino](src/Config.ino).

## Extending

- **New display option:** add the bit in [moon_render.h](src/moon_render.h), evaluate it in
  `moonPixel()` or `prepareMoonRender()`, then add it to `optionsStatus()`
  ([Commands.ino](src/Commands.ino)) with an `Opt…` text and to the option tables of both readmes.
- **New command:** handler and entry in `COMMANDS` ([Commands.ino](src/Commands.ino)), description as
  `Cmd…` in [i18n_ids.h](src/i18n_ids.h) and the language files; help and Telegram menu follow
  automatically. Add it to the command table of both readmes.
- **New text:** ID in [i18n_ids.h](src/i18n_ids.h), text in [lang/en.h](src/lang/en.h), other
  languages optional.
- **New language:** copy [lang/en.h](src/lang/en.h) to `lang/<code>.h`, rename `LANG_EN` and
  translate, include it in [i18n.cpp](src/i18n.cpp) and add one line to `LANGUAGES`.
- **New display mode:** constant in [display_mode.h](src/display_mode.h), hook the drawing function
  into `loop()`, adjust `requestRedraw()` and extend the `mode` command
  ([Commands.ino](src/Commands.ino)).
- **New notification channel:** like Pushover, as its own file with `send…()` and `…Enabled()`, and
  hook it into `checkAutoReport()` with its own day counter.
