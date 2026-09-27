/**
 * Pushover.ino – Benachrichtigungen über die Pushover-App (https://pushover.net)
 *
 * Nur ausgehend: sendet Nachrichten über die Messages-API. Aktiv, wenn
 * PUSHOVER_APP_TOKEN und PUSHOVER_USER_KEY in credentials.h gesetzt sind.
 */

#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>

#include "credentials.h"

#ifndef PUSHOVER_APP_TOKEN
#define PUSHOVER_APP_TOKEN ""
#endif
#ifndef PUSHOVER_USER_KEY
#define PUSHOVER_USER_KEY ""
#endif
#ifndef PUSHOVER_DEVICE
#define PUSHOVER_DEVICE ""  // leer: an alle Geräte des Nutzers
#endif

constexpr char PUSHOVER_URL[] = "https://api.pushover.net/1/messages.json";

// DigiCert Global Root G2 (Root-CA von api.pushover.net), gültig bis 15.01.2038
static const char PUSHOVER_ROOT_CA[] =
    "-----BEGIN CERTIFICATE-----\n"
    "MIIDjjCCAnagAwIBAgIQAzrx5qcRqaC7KGSxHQn65TANBgkqhkiG9w0BAQsFADBh\n"
    "MQswCQYDVQQGEwJVUzEVMBMGA1UEChMMRGlnaUNlcnQgSW5jMRkwFwYDVQQLExB3\n"
    "d3cuZGlnaWNlcnQuY29tMSAwHgYDVQQDExdEaWdpQ2VydCBHbG9iYWwgUm9vdCBH\n"
    "MjAeFw0xMzA4MDExMjAwMDBaFw0zODAxMTUxMjAwMDBaMGExCzAJBgNVBAYTAlVT\n"
    "MRUwEwYDVQQKEwxEaWdpQ2VydCBJbmMxGTAXBgNVBAsTEHd3dy5kaWdpY2VydC5j\n"
    "b20xIDAeBgNVBAMTF0RpZ2lDZXJ0IEdsb2JhbCBSb290IEcyMIIBIjANBgkqhkiG\n"
    "9w0BAQEFAAOCAQ8AMIIBCgKCAQEAuzfNNNx7a8myaJCtSnX/RrohCgiN9RlUyfuI\n"
    "2/Ou8jqJkTx65qsGGmvPrC3oXgkkRLpimn7Wo6h+4FR1IAWsULecYxpsMNzaHxmx\n"
    "1x7e/dfgy5SDN67sH0NO3Xss0r0upS/kqbitOtSZpLYl6ZtrAGCSYP9PIUkY92eQ\n"
    "q2EGnI/yuum06ZIya7XzV+hdG82MHauVBJVJ8zUtluNJbd134/tJS7SsVQepj5Wz\n"
    "tCO7TG1F8PapspUwtP1MVYwnSlcUfIKdzXOS0xZKBgyMUNGPHgm+F6HmIcr9g+UQ\n"
    "vIOlCsRnKPZzFBQ9RnbDhxSJITRNrw9FDKZJobq7nMWxM4MphQIDAQABo0IwQDAP\n"
    "BgNVHRMBAf8EBTADAQH/MA4GA1UdDwEB/wQEAwIBhjAdBgNVHQ4EFgQUTiJUIBiV\n"
    "5uNu5g/6+rkS7QYXjzkwDQYJKoZIhvcNAQELBQADggEBAGBnKJRvDkhj6zHd6mcY\n"
    "1Yl9PMWLSn/pvtsrF9+wX3N3KjITOYFnQoQj8kVnNeyIv/iPsGEMNKSuIEyExtv4\n"
    "NeF22d+mQrvHRAiGfzZ0JFrabA0UWTW98kndth/Jsw1HKj2ZL7tcu7XUIOGZX1NG\n"
    "Fdtom/DzMNU+MeKNhJ7jitralj41E6Vf8PlwUHBHQRFXGU7Aj64GxJUTFy8bJZ91\n"
    "8rGOmaFvE7FBcf6IKshPECBV1/MUReXgRPTqh5Uykw7+U0b6LJ3/iyK5S9kJRaTe\n"
    "pLiaWN0bfVKfjllDiIGknibVb63dDcY3fe0Dkhvld1927jyNxF1WW6LZZm6zNTfl\n"
    "MrY=\n"
    "-----END CERTIFICATE-----\n";

bool pushoverEnabled() {
    return strlen(PUSHOVER_APP_TOKEN) > 0 && strlen(PUSHOVER_USER_KEY) > 0;
}

// Kodiert einen UTF-8-String für application/x-www-form-urlencoded.
static String urlEncode(const String& s) {
    static const char HEX_DIGITS[] = "0123456789ABCDEF";
    String out;
    out.reserve(s.length() * 3);
    for (size_t i = 0; i < s.length(); i++) {
        const uint8_t c = (uint8_t) s[i];
        if (isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            out += (char) c;
        } else {
            out += '%';
            out += HEX_DIGITS[c >> 4];
            out += HEX_DIGITS[c & 0x0F];
        }
    }
    return out;
}

// Sendet eine Nachricht. Ohne Titel zeigt Pushover den Namen der App an.
bool sendPushover(const String& message, const String& title) {
    if (!pushoverEnabled() || WiFi.status() != WL_CONNECTED) return false;

    String body = "token=" PUSHOVER_APP_TOKEN "&user=" PUSHOVER_USER_KEY;
    if (strlen(PUSHOVER_DEVICE) > 0) body += "&device=" + urlEncode(PUSHOVER_DEVICE);
    if (title.length() > 0) body += "&title=" + urlEncode(title);
    body += "&message=" + urlEncode(message);

    // Eigener Client nur für die Dauer der Anfrage, damit der TLS-Speicher danach frei ist
    WiFiClientSecure client;
    client.setCACert(PUSHOVER_ROOT_CA);
    HTTPClient http;
    http.setTimeout(15000);
    if (!http.begin(client, PUSHOVER_URL)) {
        Serial.println("Pushover: keine Verbindung.");
        return false;
    }
    http.addHeader("Content-Type", "application/x-www-form-urlencoded");
    const int code = http.POST(body);
    const String response = http.getString();
    http.end();

    // Erfolgreiche Antwort: HTTP 200 mit {"status":1,...}
    const bool ok = code == 200 && response.indexOf("\"status\":1") >= 0;
    if (!ok) Serial.printf("Pushover: Fehler %d: %s\n", code, response.c_str());
    return ok;
}
