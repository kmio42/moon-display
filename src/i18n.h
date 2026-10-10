#ifndef I18N_H
#define I18N_H

/*
 * Übersetzte Texte der Ausgaben (Telegram, serielle Konsole, Pushover).
 *
 * Jede Sprache steht in einer eigenen Datei lang/<code>.h als Liste von Paaren (ID, Text) und
 * wird in i18n.cpp mit einer Zeile registriert. Englisch ist die Referenz und muss vollständig
 * sein; in den anderen Sprachen fehlende Texte erscheinen englisch. Doppelte IDs und abweichende
 * printf-Platzhalter brechen den Build mit dem Namen der ID ab.
 *
 * Die Befehlsnamen sind in allen Sprachen englisch, Log-Ausgaben werden nicht übersetzt.
 */

#include <stddef.h>
#include "i18n_ids.h"
#include "ephemeris.h"

struct TextEntry {
    Txt id;
    const char* text;
};

// Text in der aktuellen Sprache (configLanguage)
const char* tr(Txt id);

// Registrierte Sprachen, Index wie configLanguage
int languageCount();
const char* languageCode(int index);   // "de"
const char* languageName(int index);   // "Deutsch"
int findLanguage(const char* code);    // -1, wenn unbekannt

const char* moonPhaseName(MoonPhase phase);

#endif // I18N_H
