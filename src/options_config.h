// Compile-time defaults for the persistent configuration.
// These values are used on first boot (or after an NVS erase).
// They can be overridden at runtime via the serial commands
// `set_location` and `set_options`; once overridden, the values are stored
// in NVS and take precedence over the defaults below until NVS is erased.

// Observer location in decimal degrees.
//   Latitude:  -90.0 .. 90.0
//   Longitude: -180.0 .. 180.0
#define OPTIONS_CONFIG_DEFAULT_LATITUDE   39.00
#define OPTIONS_CONFIG_DEFAULT_LONGITUDE  -76.45

// Display options bitmask:
//   bit 0 (1) = darken_unlit
//   bit 1 (2) = bluish_tint
//   bit 2 (4) = use_nasa_model
//   bit 3 (8) = use_libration
#define OPTIONS_CONFIG_DEFAULT_OPTIONS    11

// Time source (NTP). Compile-time only — not overridable at runtime.
// The firmware keeps internal time in UTC, so both offsets default to 0.
#define OPTIONS_CONFIG_DEFAULT_GMT_OFFSET_SEC   0
#define OPTIONS_CONFIG_DEFAULT_DAYLIGHT_OFFSET  0
#define OPTIONS_CONFIG_DEFAULT_NTP_SERVER       "pool.ntp.org"

// Default UI language used on first boot (and after an NVS erase).
// May be overridden at runtime via `set_lang`; the new value is persisted.
// Permitted values:
//   0 = de    (German)
//   1 = en-us (US English)
#define OPTIONS_CONFIG_DEFAULT_LANGUAGE   0
