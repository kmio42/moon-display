# Mond Display
Zeigt den Mond in aktueller Mondphase und Drehung für einen bestimmten Betrachtungsort auf einem runden IPS-Display. Die Zeitsynchronisation findet über WLAN (mit NTP) statt, die Berechnung erfolgt lokal.
Das dargestellte Mondbild entstammt einer eigenen Fotografie. Libration wird nicht berücksichtigt.

## Features
* Zeitsynchronisation über NTP
* Berechnung der astronomischen Daten lokal mit Formeln aus dem Buch "Astronomische Algorithmen" von J. MeeusBerechnung der astronomischen Daten lokal mit Formeln aus dem Buch "Astronomische Algorithmen" von J. Meeus
* Verbindung mit bis zu 3 konfigurierten WLAN-Netzwerken (automatische Auswahl)

## Konfiguration
Die Zugangsdaten werden in `src/credentials.h` eingetragen:

```cpp
#define WIFI_SSID_1     "Netzwerk 1"
#define WIFI_PASSWORD_1 "Passwort 1"

#define WIFI_SSID_2     "Netzwerk 2"
#define WIFI_PASSWORD_2 "Passwort 2"

#define WIFI_SSID_3     ""   // leer lassen, wenn nicht benötigt
#define WIFI_PASSWORD_3 ""
```

Standort und NTP-Server werden in `src/MondPhase.ino` konfiguriert (`LATITUDE`, `LONGITUDE`).

## Hardware
* ESP32C3 Super mini
* 1,28 Zoll IPS Display rund (GC9A01)

