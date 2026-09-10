#include "HubClient.h"
#include "NetUtil.h"
#include <HTTPClient.h>
#include <Update.h>
#include <WiFi.h>

void HubClient::begin(ConfigStore* config) {
    config_ = config;
    lastSuccess_ = millis();
    lastHeartbeat_ = 0;
}

void HubClient::setPayloadBuilder(void (*builder)(JsonDocument& doc)) {
    builder_ = builder;
}

void HubClient::sendNow() {
    sendHeartbeat();
    lastHeartbeat_ = millis();
}

int HubClient::postJson(const char* path, const String& payload, uint16_t timeoutMs) {
    if (!config_ || !path || WiFi.status() != WL_CONNECTED) return -1;
    String url = "http://" + config_->hubHost + ":" + String(config_->hubPort) + path;
    HTTPClient http;
    http.begin(url);
    http.addHeader(F("Content-Type"), F("application/json"));
    http.setTimeout(timeoutMs);
    int code = http.POST(payload);
    if (code == 200) lastSuccess_ = millis();
    http.end();
    return code;
}

void HubClient::loop() {
    if (!config_ || !config_->enableHub) return;
    unsigned long now = millis();
    unsigned long interval = (unsigned long)config_->heartbeatIntervalS * 1000UL;
    if (interval < 5000UL) interval = 5000UL;
    if (now - lastHeartbeat_ >= interval) {
        lastHeartbeat_ = now;
        sendHeartbeat();
    }
    if (otaPending_) {
        otaPending_ = false;
        performOta(otaUrl_);
        otaUrl_ = "";
    }
}

void HubClient::sendHeartbeat() {
    if (!config_) return;
    if (WiFi.status() != WL_CONNECTED) {
        lastOk_ = false;
        return;
    }
    JsonDocument doc;
    if (builder_) builder_(doc);
    String payload;
    serializeJson(doc, payload);
    String url = "http://" + config_->hubHost + ":" + String(config_->hubPort) + "/api/register";
    HTTPClient http;
    http.begin(url);
    http.addHeader(F("Content-Type"), F("application/json"));
    http.setTimeout(8000);
    int code = http.POST(payload);
    lastOk_ = (code == 200);
    if (code == 200) {
        String body = http.getString();
        lastSuccess_ = millis();
        JsonDocument resp;
        if (deserializeJson(resp, body) == DeserializationError::Ok) {
            if (!resp["interval"].isNull()) {
                unsigned long ni = (unsigned long)resp["interval"].as<int>();
                if (ni >= 5 && ni <= 600) config_->heartbeatIntervalS = (uint16_t)ni;
            }
            if (!resp["otaUrl"].isNull()) {
                String u = resp["otaUrl"].as<String>();
                if (u.length() > 0) {
                    otaPending_ = true;
                    otaUrl_ = u;
                }
            }
        }
    }
    http.end();
}

void HubClient::performOta(const String& url) {
    Serial.println("[OTA] " + url);
    HTTPClient http;
    http.begin(url);
    http.setTimeout(30000);
    int code = http.GET();
    if (code != 200) {
        Serial.printf("[OTA] Fehler: %d\n", code);
        http.end();
        return;
    }
    int len = http.getSize();
    if (!Update.begin(len > 0 ? len : UPDATE_SIZE_UNKNOWN)) {
        Update.printError(Serial);
        http.end();
        return;
    }
    WiFiClient* stream = http.getStreamPtr();
    uint8_t buf[512];
    size_t written = 0;
    while (http.connected() && (len <= 0 || written < (size_t)len)) {
        size_t avail = stream->available();
        if (!avail) {
            delay(1);
            continue;
        }
        size_t n = stream->readBytes(buf, avail > sizeof(buf) ? sizeof(buf) : avail);
        if (!n) break;
        Update.write(buf, n);
        written += n;
    }
    if (Update.end(true)) {
        Serial.println("[OTA] OK, Neustart...");
        http.end();
        delay(400);
        ESP.restart();
    } else {
        Update.printError(Serial);
    }
    http.end();
}
