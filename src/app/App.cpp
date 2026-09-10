#include "App.h"
#include "core/NetUtil.h"
#include "web/UiPages.h"
#include <ESPmDNS.h>
#include <Update.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <esp_system.h>

App& App::instance() {
    static App app;
    return app;
}

void App::begin() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\n=== esp32.ftmsprobe v%s (%s) ===\n", FW_VERSION, PROBE_BOARD_LABEL);
    Serial.printf("[BOOT] reset reason %d\n", (int)esp_reset_reason());

    config.begin();
    log.begin();
    checkResetButton();
    setupWifi();

    probe.begin(&config, &log);

    if (config.enableMdns) {
        String mdns = "probe-" + NetUtil::macNoColon().substring(6);
        if (MDNS.begin(mdns.c_str())) Serial.printf("[mDNS] %s.local\n", mdns.c_str());
    }

    setupWeb();
    hub.begin(&config);
    hub.setPayloadBuilder([](JsonDocument& doc) { App::instance().buildHeartbeat(doc); });
    if (config.enableHub) hub.sendNow();

    // Nach dem Boot ist die Sonde mit nichts verbunden und scannt nicht.
    // Kein Auto-Connect, wie im Pflichtenheft fuer v0.1 gefordert.
    log.addMsg(ProbeLog::Info, -1, "boot");
}

void App::checkResetButton() {
    pinMode(RESET_BUTTON_PIN, INPUT_PULLUP);
    if (digitalRead(RESET_BUTTON_PIN) == HIGH) return;
    Serial.printf("[BOOT] Reset-Taste, halte %ds...\n", RESET_HOLD_SEC);
    unsigned long t = millis();
    while (digitalRead(RESET_BUTTON_PIN) == LOW) {
        if (millis() - t > (unsigned long)RESET_HOLD_SEC * 1000UL) {
            WiFiManager wm;
            wm.resetSettings();
            config.factoryReset();
            delay(400);
            ESP.restart();
        }
        delay(50);
    }
}

void App::setupWifi() {
    WiFi.mode(WIFI_STA);
    WiFiManager wm;
    WiFiManagerParameter pName("name", "Geraetename", config.deviceName.c_str(), 32);
    WiFiManagerParameter pHost("hub_host", "ESP-Hub IP", config.hubHost.c_str(), 40);
    WiFiManagerParameter pPort("hub_port", "Port", String(config.hubPort).c_str(), 6);
    wm.addParameter(&pName);
    wm.addParameter(&pHost);
    wm.addParameter(&pPort);
    wm.setConfigPortalTimeout(WIFI_PORTAL_TIMEOUT_S);
    wm.setAPCallback([](WiFiManager*) { Serial.println("[WiFi] Portal: " WIFI_AP_NAME); });
    if (!wm.autoConnect(WIFI_AP_NAME)) {
        delay(800);
        ESP.restart();
    }
    config.deviceName = String(pName.getValue());
    config.hubHost = String(pHost.getValue());
    config.hubPort = String(pPort.getValue()).toInt();
    config.save();
    // Modem-Sleep gibt Airtime fuer BLE frei — Learning aus heartrate
    WiFi.setSleep(true);
    Serial.printf("[WiFi] IP %s  Hub %s:%d (sleep on)\n", NetUtil::localIp().c_str(),
                  config.hubHost.c_str(), config.hubPort);
    NetUtil::configureNtp(config);
}

void App::setupWeb() {
    registerRoutes();
    registerProbeRoutes();
    server.onNotFound([this]() {
        server.sendHeader(F("Location"), F("/"), true);
        server.send(302, F("text/plain"), F(""));
    });
    server.begin();
    Serial.printf("[WEB] http://%s/\n", NetUtil::localIp().c_str());
}

void App::buildStatusJson(JsonDocument& doc) {
    doc["name"] = config.deviceName;
    doc["chip"] = NetUtil::chipModel();
    doc["board"] = PROBE_BOARD_ID;
    doc["boardLabel"] = PROBE_BOARD_LABEL;
    doc["mac"] = NetUtil::macNoColon();
    doc["ip"] = NetUtil::localIp();
    doc["rssi"] = WiFi.RSSI();
    doc["ssid"] = WiFi.SSID();
    doc["uptime"] = NetUtil::fmtUptime(millis() / 1000UL);
    doc["uptimeS"] = millis() / 1000UL;
    doc["heap"] = ESP.getFreeHeap();
    doc["hub"] = config.hubHost + ":" + String(config.hubPort);
    doc["hubOk"] = hub.lastOk();
    doc["hubEnabled"] = config.enableHub;
    doc["version"] = FW_VERSION;
    doc["ntpOk"] = NetUtil::timeSynced();
    doc["unixtime"] = NetUtil::unixNow();
    doc["time"] = NetUtil::localNowStr();
    probe.appendStatusJson(doc["probe"].to<JsonObject>());
    log.appendStatusJson(doc["log"].to<JsonObject>());
}

void App::buildHeartbeat(JsonDocument& doc) {
    doc["mac"] = NetUtil::macNoColon();
    doc["name"] = config.deviceName;
    doc["hwType"] = PROBE_HW_TYPE;
    doc["chipModel"] = NetUtil::chipModel();
    doc["version"] = FW_VERSION;
    doc["ip"] = NetUtil::localIp();
    doc["rssi"] = WiFi.RSSI();
    doc["uptime"] = millis() / 1000UL;
    doc["freeHeap"] = ESP.getFreeHeap();
    doc["freeSketch"] = ESP.getFreeSketchSpace();
    doc["fwType"] = PROBE_FW_TYPE;
    doc["board"] = PROBE_BOARD_ID;
    probe.appendIoValues(doc["ios"].to<JsonObject>());
}

String App::statusString() {
    JsonDocument doc;
    buildStatusJson(doc);
    return NetUtil::jsonToString(doc);
}

void App::sseSend(const String& data) {
    if (!sseClient_ || !sseClient_.connected()) return;
    sseClient_.print("data: ");
    sseClient_.print(data);
    sseClient_.print("\n\n");
    sseClient_.flush();
}

void App::handleEvents() {
    if (sseClient_ && sseClient_.connected()) sseClient_.stop();
    sseClient_ = server.client();
    sseClient_.print(F("HTTP/1.1 200 OK\r\n"
                       "Content-Type: text/event-stream\r\n"
                       "Cache-Control: no-cache\r\n"
                       "Connection: keep-alive\r\n"
                       "Access-Control-Allow-Origin: *\r\n\r\n"));
    sseClient_.flush();
    sseSend(statusString());
}

void App::handleMain() {
    server.sendHeader(F("Cache-Control"), F("no-store, no-cache, must-revalidate, max-age=0"));
    server.sendHeader(F("Pragma"), F("no-cache"));
    server.sendHeader(F("Expires"), F("0"));
    server.send_P(200, "text/html", PAGE_MAIN);
}

void App::handleOtaUpload() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
        Serial.printf("[OTA] Start %s\n", upload.filename.c_str());
        if (!Update.begin(UPDATE_SIZE_UNKNOWN)) Update.printError(Serial);
    } else if (upload.status == UPLOAD_FILE_WRITE) {
        if (Update.write(upload.buf, upload.currentSize) != upload.currentSize) {
            Update.printError(Serial);
        }
    } else if (upload.status == UPLOAD_FILE_END) {
        if (Update.end(true)) Serial.printf("[OTA] OK %u Bytes\n", upload.totalSize);
        else Update.printError(Serial);
    }
}

void App::handleOtaUploadFinish() {
    if (Update.hasError()) server.send(500, F("text/plain"), F("OTA fehlgeschlagen!"));
    else server.send(200, F("text/plain"), F("OK - Neustart..."));
    delay(400);
    ESP.restart();
}

int App::resolveLink(JsonVariantConst body, char* err, size_t errLen) {
    if (err && errLen) err[0] = 0;
    if (!body["link"].isNull()) {
        int idx = body["link"].as<int>();
        if (!probe.linkValid(idx)) {
            if (err) snprintf(err, errLen, "link %d ist nicht verbunden", idx);
            return -1;
        }
        return idx;
    }
    const char* mac = body["mac"] | "";
    if (mac[0]) {
        int idx = probe.findLink(mac);
        if (idx < 0) {
            if (err) snprintf(err, errLen, "kein Link auf %s", mac);
            return -1;
        }
        return idx;
    }
    // Bequemlichkeit fuer den Normalfall: genau ein Link, keine Angabe noetig
    if (probe.linkCount() == 1) {
        for (int i = 0; i < PROBE_MAX_LINKS; i++) {
            if (probe.linkValid(i)) return i;
        }
    }
    if (err) snprintf(err, errLen, "link oder mac angeben (%u Links offen)",
                      (unsigned)probe.linkCount());
    return -1;
}

void App::registerRoutes() {
    server.on("/", HTTP_GET, [this]() { handleMain(); });
    server.on("/scan", HTTP_GET, [this]() { handleMain(); });
    server.on("/gatt", HTTP_GET, [this]() { handleMain(); });
    server.on("/logs", HTTP_GET, [this]() { handleMain(); });
    server.on("/config", HTTP_GET, [this]() { handleMain(); });
    server.on("/ota", HTTP_GET, [this]() { handleMain(); });
    server.on("/events", HTTP_GET, [this]() { handleEvents(); });
    server.on("/ota-upload", HTTP_POST, [this]() { handleOtaUploadFinish(); },
              [this]() { handleOtaUpload(); });

    server.on("/api/status", HTTP_GET, [this]() {
        JsonDocument doc;
        buildStatusJson(doc);
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/config/get", HTTP_GET, [this]() {
        JsonDocument doc;
        config.toJson(doc.to<JsonObject>());
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/config/save", HTTP_POST, [this]() {
        JsonDocument body;
        if (!NetUtil::readJsonBody(server, body)) return;
        if (!config.fromJson(body.as<JsonVariantConst>())) {
            NetUtil::sendError(server, 400, "ungueltig");
            return;
        }
        config.save();
        NetUtil::configureNtp(config);
        JsonDocument doc;
        doc["ok"] = true;
        config.toJson(doc["config"].to<JsonObject>());
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/system/restart", HTTP_POST, [this]() {
        restartPending_ = true;
        restartAt_ = millis() + 400;
        JsonDocument doc;
        doc["ok"] = true;
        NetUtil::sendJson(server, 200, doc);
    });
}

void App::registerProbeRoutes() {
    server.on("/api/probe/scan/start", HTTP_POST, [this]() {
        JsonDocument body;
        if (!NetUtil::readJsonBodyOrEmpty(server, body)) return;
        if (body["clear"] | true) probe.clearScan();
        char err[80] = {0};
        if (!probe.startScan(err, sizeof(err))) {
            NetUtil::sendError(server, 409, err[0] ? err : "scan nicht gestartet");
            return;
        }
        JsonDocument doc;
        doc["ok"] = true;
        doc["state"] = probeStateName(probe.state());
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/scan/stop", HTTP_POST, [this]() {
        probe.stopScan();
        JsonDocument doc;
        doc["ok"] = true;
        doc["state"] = probeStateName(probe.state());
        doc["count"] = probe.scanCount();
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/devices", HTTP_GET, [this]() {
        JsonDocument doc;
        doc["ok"] = true;
        doc["scanning"] = probe.scanning();
        doc["count"] = probe.scanCount();
        doc["bikeMac"] = config.bikeMac;
        doc["hrMac"] = config.hrMac;
        probe.scanToJson(doc["devices"].to<JsonArray>());
        NetUtil::sendJson(server, 200, doc);
    });

    // Verbinden liefert direkt den vollen GATT-Dump zurueck — ein Aufruf,
    // eine Antwort auf Schritt 2 aus BLE-SCAN.md.
    server.on("/api/probe/connect", HTTP_POST, [this]() {
        JsonDocument body;
        if (!NetUtil::readJsonBody(server, body)) return;
        const char* mac = body["mac"] | "";
        const char* role = body["role"] | "";
        if (!mac[0] && strcasecmp(role, "bike") == 0 && config.bikeMac.length())
            mac = config.bikeMac.c_str();
        if (!mac[0] && strcasecmp(role, "hr") == 0 && config.hrMac.length())
            mac = config.hrMac.c_str();
        if (!mac[0]) {
            NetUtil::sendError(server, 400, "mac oder bekannte role fehlt");
            return;
        }
        int addrType = body["addrType"].isNull() ? -1 : body["addrType"].as<int>();
        char err[80] = {0};
        int link = probe.connect(mac, addrType, err, sizeof(err));
        if (link < 0) {
            NetUtil::sendError(server, 502, err[0] ? err : "connect fehlgeschlagen");
            return;
        }
        JsonDocument doc;
        doc["ok"] = true;
        doc["link"] = link;
        char derr[80] = {0};
        if (!probe.dumpGatt(link, doc["gatt"].to<JsonObject>(), derr, sizeof(derr))) {
            doc["gattError"] = derr;
        }
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/disconnect", HTTP_POST, [this]() {
        JsonDocument body;
        if (!NetUtil::readJsonBodyOrEmpty(server, body)) return;
        JsonDocument doc;
        if (body["all"] | false) {
            probe.disconnectAllIntentional();
            doc["ok"] = true;
            doc["all"] = true;
        } else {
            char err[80] = {0};
            int link = resolveLink(body.as<JsonVariantConst>(), err, sizeof(err));
            if (link < 0) {
                NetUtil::sendError(server, 400, err);
                return;
            }
            doc["ok"] = probe.disconnect(link);
            doc["link"] = link;
        }
        doc["linkCount"] = probe.linkCount();
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/reconnect", HTTP_POST, [this]() {
        char err[80] = {0};
        int link = probe.reconnectBike(err, sizeof(err));
        if (link < 0) {
            NetUtil::sendError(server, 502, err[0] ? err : "reconnect fehlgeschlagen");
            return;
        }
        JsonDocument doc;
        doc["ok"] = true;
        doc["link"] = link;
        // Live-Abos setzen
        char e2[64] = {0};
        JsonDocument tmp;
        probe.subscribe(link, nullptr, "2AD2", false, true, tmp.to<JsonObject>(), e2, sizeof(e2));
        tmp.clear();
        probe.subscribe(link, nullptr, "2AD9", false, true, tmp.to<JsonObject>(), e2, sizeof(e2));
        char derr[80] = {0};
        probe.dumpGatt(link, doc["gatt"].to<JsonObject>(), derr, sizeof(derr));
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/live", HTTP_GET, [this]() {
        JsonDocument doc;
        doc["ok"] = true;
        doc["linkCount"] = probe.linkCount();
        probe.appendLiveJson(doc["ibd"].to<JsonObject>());
        probe.linksToJson(doc["links"].to<JsonArray>());
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/summary", HTTP_GET, [this]() {
        JsonDocument doc;
        doc["ok"] = true;
        probe.appendSummaryJson(doc["summary"].to<JsonObject>());
        doc["ip"] = NetUtil::localIp();
        doc["unixtime"] = NetUtil::unixNow();
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/export", HTTP_GET, [this]() {
        // Ein Abruf fuer Agent/Tester: Summary-Header + Log-NDJSON
        server.setContentLength(CONTENT_LENGTH_UNKNOWN);
        server.sendHeader("Content-Type", "application/x-ndjson");
        server.sendHeader("Content-Disposition", "attachment; filename=\"probe-export.ndjson\"");
        server.sendHeader("Access-Control-Allow-Origin", "*");
        server.send(200, "application/x-ndjson", "");
        JsonDocument head;
        head["type"] = "summary";
        probe.appendSummaryJson(head["summary"].to<JsonObject>());
        head["ip"] = NetUtil::localIp();
        head["exportedAt"] = NetUtil::unixNow();
        server.sendContent(NetUtil::jsonToString(head));
        server.sendContent("\n");
        uint32_t since = server.hasArg("since") ? (uint32_t)strtoul(server.arg("since").c_str(),
                                                                    nullptr, 10)
                                                : 0;
        // streamNdjson schreibt selbst Headers — daher manuell die Eintraege:
        // Nutze vorhandene Log-API indirekt: wir lesen via stream in Stuecken nicht leicht.
        // Stattdessen kurze Summary + Hinweis auf /api/probe/log
        JsonDocument tip;
        tip["type"] = "hint";
        tip["log"] = "/api/probe/log?since=0&max=768";
        tip["report"] = "docs/ergometer/ERGEBNISBERICHT.md";
        server.sendContent(NetUtil::jsonToString(tip));
        server.sendContent("\n");
        (void)since;
    });

    server.on("/api/probe/gatt", HTTP_GET, [this]() {
        int link = server.hasArg("link") ? server.arg("link").toInt() : 0;
        if (!probe.linkValid(link) && probe.linkCount() == 1) {
            for (int i = 0; i < PROBE_MAX_LINKS; i++) {
                if (probe.linkValid(i)) link = i;
            }
        }
        JsonDocument doc;
        char err[80] = {0};
        if (!probe.dumpGatt(link, doc.to<JsonObject>(), err, sizeof(err))) {
            NetUtil::sendError(server, 400, err);
            return;
        }
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/read", HTTP_POST, [this]() {
        JsonDocument body;
        if (!NetUtil::readJsonBody(server, body)) return;
        char err[80] = {0};
        int link = resolveLink(body.as<JsonVariantConst>(), err, sizeof(err));
        if (link < 0) {
            NetUtil::sendError(server, 400, err);
            return;
        }
        const char* uuid = body["uuid"] | "";
        const char* svc = body["service"] | "";
        if (!uuid[0]) {
            NetUtil::sendError(server, 400, "uuid fehlt");
            return;
        }
        JsonDocument doc;
        if (!probe.readChar(link, svc, uuid, doc.to<JsonObject>(), err, sizeof(err))) {
            NetUtil::sendError(server, 400, err);
            return;
        }
        doc["ok"] = true;
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/subscribe", HTTP_POST, [this]() {
        JsonDocument body;
        if (!NetUtil::readJsonBody(server, body)) return;
        char err[80] = {0};
        int link = resolveLink(body.as<JsonVariantConst>(), err, sizeof(err));
        if (link < 0) {
            NetUtil::sendError(server, 400, err);
            return;
        }
        const char* uuid = body["uuid"] | "";
        const char* svc = body["service"] | "";
        const char* mode = body["mode"] | "notify";
        bool enable = body["enable"] | true;
        if (!uuid[0]) {
            NetUtil::sendError(server, 400, "uuid fehlt");
            return;
        }
        bool indicate = (strcasecmp(mode, "indicate") == 0);
        JsonDocument doc;
        if (!probe.subscribe(link, svc, uuid, indicate, enable, doc.to<JsonObject>(), err,
                             sizeof(err))) {
            NetUtil::sendError(server, 400, err);
            return;
        }
        doc["ok"] = true;
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/write", HTTP_POST, [this]() {
        JsonDocument body;
        if (!NetUtil::readJsonBody(server, body)) return;
        char err[96] = {0};
        int link = resolveLink(body.as<JsonVariantConst>(), err, sizeof(err));
        if (link < 0) {
            NetUtil::sendError(server, 400, err);
            return;
        }
        const char* uuid = body["uuid"] | "";
        const char* svc = body["service"] | "";
        const char* hex = body["hex"] | "";
        if (!uuid[0] || !hex[0]) {
            NetUtil::sendError(server, 400, "uuid und hex sind Pflicht");
            return;
        }
        uint8_t buf[BleProbe::kMaxWrite];
        int len = NetUtil::fromHex(hex, buf, sizeof(buf));
        if (len <= 0) {
            NetUtil::sendError(server, 400, "hex ungueltig oder zu lang");
            return;
        }
        bool response = body["response"] | true;
        bool await = body["awaitIndication"] | false;
        uint16_t timeoutMs = body["timeoutMs"] | 2000;

        JsonDocument doc;
        JsonObject out = doc.to<JsonObject>();
        if (!probe.writeChar(link, svc, uuid, buf, (size_t)len, response, await, timeoutMs, out,
                             err, sizeof(err))) {
            doc["ok"] = false;
            doc["error"] = err;
            NetUtil::sendJson(server, 400, doc);
            return;
        }
        doc["ok"] = true;
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/log", HTTP_GET, [this]() {
        uint32_t since = server.hasArg("since") ? (uint32_t)strtoul(server.arg("since").c_str(),
                                                                    nullptr, 10)
                                                : 0;
        uint16_t max = server.hasArg("max") ? (uint16_t)server.arg("max").toInt() : 200;
        if (max == 0 || max > PROBE_LOG_SIZE) max = PROBE_LOG_SIZE;
        String phase = server.hasArg("phase") ? server.arg("phase") : String();
        log.streamNdjson(server, since, max, phase.length() ? phase.c_str() : nullptr);
    });

    server.on("/api/probe/log/clear", HTTP_POST, [this]() {
        log.clear();
        JsonDocument doc;
        doc["ok"] = true;
        doc["lastSeq"] = log.lastSeq();
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/phase", HTTP_POST, [this]() {
        JsonDocument body;
        if (!NetUtil::readJsonBody(server, body)) return;
        const char* phase = body["phase"] | "";
        if (!phase[0]) {
            NetUtil::sendError(server, 400, "phase fehlt");
            return;
        }
        log.setPhase(phase);
        JsonDocument doc;
        doc["ok"] = true;
        doc["phase"] = log.phase();
        doc["seq"] = log.lastSeq();
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/keepalive", HTTP_POST, [this]() {
        probe.guard().keepalive();
        JsonDocument doc;
        doc["ok"] = true;
        doc["armed"] = probe.guard().armed();
        doc["remainingMs"] = probe.guard().remainingMs();
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/panic", HTTP_POST, [this]() {
        JsonDocument body;
        if (!NetUtil::readJsonBodyOrEmpty(server, body)) return;
        const char* reason = body["reason"] | "not-stop per API";
        probe.panic(reason);
        JsonDocument doc;
        doc["ok"] = true;
        doc["state"] = probeStateName(probe.state());
        doc["linkCount"] = probe.linkCount();
        NetUtil::sendJson(server, 200, doc);
    });

    server.on("/api/probe/remember", HTTP_POST, [this]() {
        JsonDocument body;
        if (!NetUtil::readJsonBody(server, body)) return;
        const char* role = body["role"] | "";
        String mac = body["mac"] | "";
        String name = body["name"] | "";
        if (strcasecmp(role, "bike") == 0) {
            config.bikeMac = mac;
            config.bikeName = name;
        } else if (strcasecmp(role, "hr") == 0) {
            config.hrMac = mac;
            config.hrName = name;
        } else {
            NetUtil::sendError(server, 400, "role muss bike oder hr sein");
            return;
        }
        config.save();
        JsonDocument doc;
        doc["ok"] = true;
        config.toJson(doc["config"].to<JsonObject>());
        NetUtil::sendJson(server, 200, doc);
    });

    // Crash-Test aus §14: Was macht das Bike, wenn der Client wegbricht, ohne
    // 0x08 0x01 gesendet zu haben? Bewusst ohne Not-Stop und ohne Disconnect.
    // Das Log liegt im RAM und ist danach weg — vorher abholen.
    server.on("/api/probe/crash", HTTP_POST, [this]() {
        JsonDocument body;
        if (!NetUtil::readJsonBody(server, body)) return;
        const char* confirm = body["confirm"] | "";
        if (strcmp(confirm, "crash") != 0) {
            NetUtil::sendError(server, 400, "confirm muss \"crash\" sein");
            return;
        }
        const char* mode = body["mode"] | "restart";
        crashMode_ = (strcasecmp(mode, "abort") == 0) ? 2 : 1;
        crashAt_ = millis() + 300;
        Serial.printf("[CRASH] angefordert, mode=%s — kein Stop-Kommando ans Bike\n", mode);
        JsonDocument doc;
        doc["ok"] = true;
        doc["mode"] = (crashMode_ == 2) ? "abort" : "restart";
        doc["lastSeq"] = log.lastSeq();
        doc["note"] = "Log ist nach dem Neustart leer";
        NetUtil::sendJson(server, 200, doc);
    });
}

void App::loop() {
    server.handleClient();
    probe.loop();
    hub.loop();
    log.anchorTime(NetUtil::unixNow());

    unsigned long now = millis();
    unsigned long sseInterval = (probe.linkCount() > 0) ? 1000UL : 3000UL;
    if (sseClient_ && sseClient_.connected() && now - lastSse_ >= sseInterval) {
        lastSse_ = now;
        sseSend(statusString());
    }

    if (now - lastWifiCheck_ > 5000UL) {
        lastWifiCheck_ = now;
        if (WiFi.status() != WL_CONNECTED) WiFi.reconnect();
    }

    // Hub-Watchdog standardmaessig aus: ein Reboot mitten in einer Messung
    // wuerde den Link reissen lassen, ohne vorher zu stoppen.
    if (config.enableHub && config.watchdogS > 0 && probe.linkCount() == 0) {
        if (now - hub.lastSuccessMs() > (unsigned long)config.watchdogS * 1000UL) {
            Serial.println("[WD] Hub silent, restart");
            delay(200);
            ESP.restart();
        }
    }

    if (crashMode_ && now >= crashAt_) {
        if (crashMode_ == 2) {
            crashMode_ = 0;
            abort();
        }
        crashMode_ = 0;
        esp_restart();
    }

    if (restartPending_ && now >= restartAt_) ESP.restart();
    delay(2);
}
