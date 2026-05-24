#include "i18n.h"
#include "lang_de.h"
#include "lang_en_us.h"

const char* const* msgs = msgs_de;

void setLanguage(Language lang) {
    switch (lang) {
        case LANG_EN_US:  msgs = msgs_en_us;  break;
        case LANG_DE:
        default:          msgs = msgs_de;     break;
    }
}
