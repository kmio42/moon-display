#include "i18n.h"

#include <array>

#include "lang/en.h"
#include "lang/de.h"

// Aktuelle Sprache als Index in LANGUAGES (definiert in Config.ino)
extern int configLanguage;

namespace {

constexpr size_t TEXT_COUNT = static_cast<size_t>(Txt::COUNT);
using TextTable = std::array<const char*, TEXT_COUNT>;

constexpr const char* TEXT_NAMES[] = {
#define TXT_NAME(id) #id,
    TEXT_IDS(TXT_NAME)
#undef TXT_NAME
};

constexpr size_t index(Txt id) {
    return static_cast<size_t>(id);
}

// ── Prüfungen zur Compilezeit ────────────────────────────────────────────────

// Meldung für static_assert, enthält Sprache und ID des ersten Fehlers. Leer: alles in Ordnung.
struct CheckResult {
    char buf[128] = {};
    size_t len = 0;

    constexpr CheckResult& add(const char* s) {
        while (*s && len < sizeof(buf)) buf[len++] = *s++;
        return *this;
    }
    constexpr bool ok() const { return len == 0; }
    constexpr size_t size() const { return len; }
    constexpr const char* data() const { return buf; }
};

constexpr bool endsWith(const char* s, const char* suffix) {
    size_t n = 0, m = 0;
    while (s[n]) n++;
    while (suffix[m]) m++;
    if (m > n) return false;
    for (size_t i = 0; i < m; i++) {
        if (s[n - m + i] != suffix[i]) return false;
    }
    return true;
}

constexpr bool isOneOf(char c, const char* chars) {
    for (; *chars; chars++) {
        if (*chars == c) return true;
    }
    return false;
}

// Springt zum nächsten printf-Platzhalter und gibt Längenangabe und Typ zurück ("ld", "s", ...).
// Flags, Breite und Genauigkeit dürfen sich zwischen den Sprachen unterscheiden, der Typ nicht.
// Leer am Ende des Strings.
struct Conversion {
    char spec[4] = {};
};

constexpr Conversion nextConversion(const char*& p) {
    Conversion c;
    while (*p) {
        if (*p++ != '%') continue;
        if (*p == '%') {
            p++;
            continue;
        }
        while (isOneOf(*p, "-+ #0")) p++;
        while (isOneOf(*p, "0123456789*.")) p++;
        size_t n = 0;
        while (isOneOf(*p, "hlLqjzt") && n < 2) c.spec[n++] = *p++;
        if (*p) c.spec[n] = *p++;
        return c;
    }
    return c;
}

constexpr bool sameConversion(const Conversion& a, const Conversion& b) {
    for (size_t i = 0; i < sizeof(a.spec); i++) {
        if (a.spec[i] != b.spec[i]) return false;
    }
    return true;
}

constexpr bool sameFormat(const char* a, const char* b) {
    while (true) {
        const Conversion ca = nextConversion(a);
        const Conversion cb = nextConversion(b);
        if (!sameConversion(ca, cb)) return false;
        if (ca.spec[0] == '\0') return true;
    }
}

template <size_t N>
constexpr CheckResult checkLanguage(const char* code, const TextEntry (&entries)[N], bool complete) {
    CheckResult result;
    bool seen[TEXT_COUNT] = {};
    for (const TextEntry& e : entries) {
        const size_t i = index(e.id);
        if (seen[i]) return result.add(code).add(": doppelt: ").add(TEXT_NAMES[i]);
        seen[i] = true;
        if (endsWith(TEXT_NAMES[i], "Fmt")) {
            for (const TextEntry& ref : LANG_EN) {
                if (ref.id == e.id && !sameFormat(ref.text, e.text)) {
                    return result.add(code).add(": Platzhalter weichen von en ab: ").add(TEXT_NAMES[i]);
                }
            }
        }
    }
    if (complete) {
        for (size_t i = 0; i < TEXT_COUNT; i++) {
            if (!seen[i]) return result.add(code).add(": fehlt: ").add(TEXT_NAMES[i]);
        }
    }
    return result;
}

// ── Sprachen ─────────────────────────────────────────────────────────────────

struct Language {
    const char* code;
    const char* name;
    CheckResult check;
    TextTable texts;
};

// Ordnet die Texte nach ID; fehlende kommen aus der Referenzsprache Englisch.
template <size_t N>
constexpr Language language(const char* code, const char* name, const TextEntry (&entries)[N]) {
    Language lang{code, name, checkLanguage(code, entries, &entries[0] == &LANG_EN[0]), {}};
    for (const TextEntry& e : LANG_EN) lang.texts[index(e.id)] = e.text;
    for (const TextEntry& e : entries) lang.texts[index(e.id)] = e.text;
    return lang;
}

// Neue Sprache: lang/<code>.h anlegen, oben einbinden und hier eine Zeile ergänzen.
constexpr Language LANGUAGES[] = {
    language("en", "English", LANG_EN),
    language("de", "Deutsch", LANG_DE),
};

constexpr int LANGUAGE_COUNT = sizeof(LANGUAGES) / sizeof(LANGUAGES[0]);

constexpr CheckResult checkAll() {
    for (const Language& lang : LANGUAGES) {
        if (!lang.check.ok()) return lang.check;
    }
    return {};
}

// Meldung mit dem Namen der ID (static_assert mit erzeugter Meldung aus C++26, GCC ab 14)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wc++26-extensions"
static_assert(checkAll().ok(), checkAll());
#pragma GCC diagnostic pop

static_assert(index(Txt::PhaseWaningCrescent) - index(Txt::PhaseNew) + 1 == MOON_PHASE_COUNT,
              "Phasennamen passen nicht zu MoonPhase");

} // namespace

const char* tr(Txt id) {
    const int lang = (configLanguage >= 0 && configLanguage < LANGUAGE_COUNT) ? configLanguage : 0;
    return LANGUAGES[lang].texts[index(id)];
}

int languageCount() {
    return LANGUAGE_COUNT;
}

const char* languageCode(int i) {
    return LANGUAGES[i].code;
}

const char* languageName(int i) {
    return LANGUAGES[i].name;
}

int findLanguage(const char* code) {
    for (int i = 0; i < LANGUAGE_COUNT; i++) {
        const char* a = LANGUAGES[i].code;
        const char* b = code;
        while (*a && *a == *b) {
            a++;
            b++;
        }
        if (*a == '\0' && *b == '\0') return i;
    }
    return -1;
}

const char* moonPhaseName(MoonPhase phase) {
    return tr(static_cast<Txt>(index(Txt::PhaseNew) + static_cast<size_t>(phase)));
}
