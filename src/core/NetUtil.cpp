#include "NetUtil.h"
#include <WiFi.h>
#include <time.h>

namespace NetUtil {

String macNoColon() {
    String mac = WiFi.macAddress();
    mac.replace(":", "");
    mac.toUpperCase();
    return mac;
}

String localIp() {
    return WiFi.localIP().toString();
}

String fmtUptime(unsigned long seconds) {
    if (seconds < 60) return String(seconds) + "s";
    if (seconds < 3600) return String(seconds / 60) + "min " + String(seconds % 60) + "s";
    return String(seconds / 3600) + "h " + String((seconds % 3600) / 60) + "min";
}

String chipModel() {
    return String(ESP.getChipModel());
}

void addCors(WebServer& server) {
    server.sendHeader(F("Access-Control-Allow-Origin"), F("*"));
    server.sendHeader(F("Access-Control-Allow-Headers"), F("Content-Type"));
}

String jsonToString(const JsonDocument& doc) {
    String out;
    serializeJson(doc, out);
    return out;
}

void sendJson(WebServer& server, int code, const JsonDocument& doc) {
    addCors(server);
    server.send(code, F("application/json"), jsonToString(doc));
}

void sendError(WebServer& server, int code, const char* message) {
    JsonDocument doc;
    doc["ok"] = false;
    doc["error"] = message;
    sendJson(server, code, doc);
}

bool readJsonBody(WebServer& server, JsonDocument& doc) {
    String body = server.arg("plain");
    if (body.length() == 0) {
        sendError(server, 400, "leerer Body");
        return false;
    }
    if (deserializeJson(doc, body)) {
        sendError(server, 400, "ungueltiges JSON");
        return false;
    }
    return true;
}

bool readJsonBodyOrEmpty(WebServer& server, JsonDocument& doc) {
    String body = server.arg("plain");
    if (body.length() == 0) {
        doc.to<JsonObject>();
        return true;
    }
    if (deserializeJson(doc, body)) {
        sendError(server, 400, "ungueltiges JSON");
        return false;
    }
    return true;
}

void configureNtp(const ConfigStore& cfg) {
    if (!cfg.enableNtp) {
        Serial.println("[NTP] disabled");
        return;
    }
    setenv("TZ", cfg.tz.c_str(), 1);
    tzset();
    configTime(0, 0, cfg.ntpServer.c_str(), "time.nist.gov");
    Serial.printf("[NTP] server=%s tz=%s\n", cfg.ntpServer.c_str(), cfg.tz.c_str());
}

bool timeSynced() {
    time_t now = time(nullptr);
    return now > 1700000000L;  // after ~2023-11
}

uint32_t unixNow() {
    if (!timeSynced()) return 0;
    return (uint32_t)time(nullptr);
}

String formatUnixLocal(uint32_t unix) {
    if (!unix) return String();
    time_t t = (time_t)unix;
    struct tm ti;
    if (!localtime_r(&t, &ti)) return String();
    char buf[24];
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M", &ti);
    return String(buf);
}

String localNowStr() {
    return formatUnixLocal(unixNow());
}

static const char kHexDigits[] = "0123456789ABCDEF";

String toHex(const uint8_t* data, size_t len) {
    String out;
    if (!data || !len) return out;
    out.reserve(len * 2);
    for (size_t i = 0; i < len; i++) {
        out += kHexDigits[(data[i] >> 4) & 0x0F];
        out += kHexDigits[data[i] & 0x0F];
    }
    return out;
}

static int hexNibble(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return c - 'a' + 10;
    if (c >= 'A' && c <= 'F') return c - 'A' + 10;
    return -1;
}

int fromHex(const char* text, uint8_t* out, size_t maxLen) {
    if (!text || !out) return -1;
    size_t n = 0;
    int hi = -1;
    for (const char* p = text; *p; p++) {
        char c = *p;
        if (c == ' ' || c == ':' || c == '-' || c == ',' || c == '_') continue;
        if ((c == 'x' || c == 'X') && hi == 0) {  // "0x" Praefix
            hi = -1;
            continue;
        }
        int v = hexNibble(c);
        if (v < 0) return -1;
        if (hi < 0) {
            hi = v;
        } else {
            if (n >= maxLen) return -1;
            out[n++] = (uint8_t)((hi << 4) | v);
            hi = -1;
        }
    }
    if (hi >= 0) return -1;  // ungerade Anzahl Nibbles
    return (int)n;
}

}
