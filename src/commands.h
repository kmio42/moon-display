#ifndef COMMANDS_H
#define COMMANDS_H

// Gemeinsame Befehle der seriellen Schnittstelle und von Telegram (Commands.ino).

#include <Arduino.h>
#include "i18n.h"

enum Channel : uint8_t {
    CH_SERIAL   = 1,
    CH_TELEGRAM = 2,
    CH_ALL      = CH_SERIAL | CH_TELEGRAM,
};

// Antwort eines Befehls. Leerer Text: Die Antwort wurde schon gesendet (Bilder über Telegram).
struct Reply {
    String text;
    bool html = false;  // Telegram: parse_mode HTML; seriell werden die Tags entfernt
};

struct Command;

struct CommandContext {
    Channel channel;
    const Command* command;
    String args;        // alles nach dem Befehlsnamen, ohne führende/folgende Leerzeichen
    String chatId;      // nur Telegram
};

struct Command {
    const char* name;   // englisch, in allen Sprachen gleich
    const char* usage;  // Argumente, z. B. "[lat lon [elev]]"
    Txt help;           // Beschreibung für Hilfe und Telegram-Menü
    uint8_t channels;
    Reply (*run)(const CommandContext& ctx);
};

// Führt eine Zeile aus ("location 50 8" oder "/location 50 8").
Reply  runCommand(Channel channel, const String& line, const String& chatId);
// Befehlsliste für setMyCommands in der aktuellen Sprache
String buildBotCommandsJson();

#endif // COMMANDS_H
