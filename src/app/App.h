#pragma once

#include <ArduinoJson.h>
#include <WebServer.h>
#include <WiFiClient.h>
#include "ble/BleProbe.h"
#include "ble/ProbeLog.h"
#include "core/ConfigStore.h"
#include "core/HubClient.h"

class App {
public:
    static App& instance();

    ConfigStore config;
    HubClient hub;
    ProbeLog log;
    BleProbe probe;
    WebServer server{80};

    void begin();
    void loop();
    void buildStatusJson(JsonDocument& doc);
    void buildHeartbeat(JsonDocument& doc);
    String statusString();

private:
    App() {}
    void checkResetButton();
    void setupWifi();
    void setupWeb();
    void registerRoutes();
    void registerProbeRoutes();
    void handleMain();
    void handleOtaUpload();
    void handleOtaUploadFinish();
    void handleEvents();
    void sseSend(const String& data);

    /** Loest link/mac aus dem Body auf. Bei genau einem Link ist beides optional. */
    int resolveLink(JsonVariantConst body, char* err, size_t errLen);

    WiFiClient sseClient_;
    unsigned long lastSse_ = 0;
    unsigned long lastWifiCheck_ = 0;
    bool restartPending_ = false;
    unsigned long restartAt_ = 0;
    /** 0 = nichts, 1 = esp_restart, 2 = abort() — nur fuer den Crash-Test. */
    uint8_t crashMode_ = 0;
    unsigned long crashAt_ = 0;
};
