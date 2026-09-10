#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "ConfigStore.h"

namespace NetUtil {
String macNoColon();
String localIp();
String fmtUptime(unsigned long seconds);
String chipModel();
void sendJson(WebServer& server, int code, const JsonDocument& doc);
void sendError(WebServer& server, int code, const char* message);
bool readJsonBody(WebServer& server, JsonDocument& doc);
/** Wie readJsonBody, akzeptiert aber einen leeren Body als leeres Objekt. */
bool readJsonBodyOrEmpty(WebServer& server, JsonDocument& doc);
void addCors(WebServer& server);
String jsonToString(const JsonDocument& doc);

/** Start/reconfigure SNTP (non-blocking). Internal math stays on millis(). */
void configureNtp(const ConfigStore& cfg);
bool timeSynced();
uint32_t unixNow();
String localNowStr();
String formatUnixLocal(uint32_t unix);

/** Bytes -> Grossbuchstaben-Hex ohne Trenner. */
String toHex(const uint8_t* data, size_t len);
/** Hex-String -> Bytes. Trenner (Leerzeichen, ':', '-') und "0x" sind erlaubt.
 *  Gibt die Anzahl Bytes zurueck, -1 bei ungueltiger Eingabe. */
int fromHex(const char* text, uint8_t* out, size_t maxLen);
}
