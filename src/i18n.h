#pragma once

// Output language. Stored as an int in NVS (key "lang"). Default LANG_DE.
enum Language {
    LANG_DE    = 0,
    LANG_EN_US = 1
};

// Every user-facing string is referenced by one of these IDs.
// The per-language tables in lang_de.h / lang_en_us.h are indexed by these
// values, so the order here is the contract — keep both tables in sync.
enum MsgId {
    // info / general
    MSG_TYPE_HELP_FOR_COMMANDS,
    MSG_CALCULATING_MOON,
    MSG_DONE,
    MSG_STARTING_DYNAMIC_MOON,
    MSG_UPDATING_MOON,
    MSG_TIME_UPDATED,
    MSG_WIFI_CONNECTIONS,
    MSG_WIFI_ENABLING,
    MSG_WIFI_DISABLING,
    MSG_CONFIG_HEADER,
    MSG_LANGUAGE_LINE,
    MSG_LANGUAGE_SET,
    MSG_POLAR_DAY_NIGHT,
    MSG_EXPECTED_TIME_FORMAT,
    MSG_CONFIG_TIME_UNAVAILABLE,
    MSG_CONFIG_WIFI_DISABLED,
    MSG_CONFIG_WIFI_DISCONNECTED,
    MSG_NTP_UNKNOWN,
    MSG_NTP_NOT_SYNCED,
    MSG_NTP_SYNCED,
    MSG_NTP_SYNCING,

    // format strings (require printf with arguments)
    FMT_UNRECOGNIZED_CMD,
    FMT_LOCATION_SAVED,
    FMT_OPTIONS_SAVED,
    FMT_CONFIG_LATITUDE,
    FMT_CONFIG_LONGITUDE,
    FMT_CONFIG_OPTIONS,
    FMT_CONFIG_OPT_DARKEN,
    FMT_CONFIG_OPT_BLUISH,
    FMT_CONFIG_OPT_NASA,
    FMT_CONFIG_OPT_LIBRATION,
    FMT_CONFIG_TIME,
    FMT_CONFIG_NTP,
    FMT_CONFIG_WIFI_CONNECTED,

    // errors
    ERR_INVALID_FORMAT_LABEL,
    ERR_NO_NTP_TIME,
    ERR_INVALID_DATETIME_FORMAT,
    ERR_INVALID_WIFI_ARG,
    ERR_INVALID_LOCATION_FORMAT,
    ERR_INVALID_COORDINATES,
    ERR_INVALID_OPTIONS_FORMAT,
    ERR_INVALID_OPTIONS_VALUE,
    ERR_INVALID_LANG,
    ERR_NO_NTP_SUN,

    // help block
    HELP_HEADER,
    HELP_HELP,
    HELP_MOON,
    HELP_MOON_RUN,
    HELP_WIFI,
    HELP_SET_TIME,
    HELP_SET_LOCATION,
    HELP_SET_OPTIONS,
    HELP_OPTIONS_BITS,
    HELP_CONFIG,
    HELP_SET_LANG,

    // moon-calculation diagnostics
    DBG_JULIAN_DATE,
    DBG_MOON_RADEC,
    DBG_MOON_AZALT,
    DBG_MOON_LIBRATION,
    DBG_MOON_AXLE,
    DBG_MOON_PHASE,
    DBG_MOON_LIMB,
    DBG_PARALLACTIC_ANGLE,
    DBG_SIDEREAL_TIME,
    DBG_TIMING_TOTAL,

    // sun-calc labels (consumed by printHHMM)
    HHMM_SUNRISE,
    HHMM_SUNSET,

    MSG_COUNT  // sentinel — leave last
};

// Active string table. Read-only for callers; written only by setLanguage().
extern const char* const* msgs;

// Switch the active language. Safe to call from any context.
void setLanguage(Language lang);
