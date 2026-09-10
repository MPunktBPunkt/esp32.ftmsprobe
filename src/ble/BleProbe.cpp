#include "BleProbe.h"
#include "core/NetUtil.h"

static BleProbe* g_probe = nullptr;

static void probeNotifyCb(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len,
                          bool isNotify) {
    if (g_probe) g_probe->onNotify(chr, data, len, isNotify);
}

class ProbeAdvCallbacks : public NimBLEAdvertisedDeviceCallbacks {
    void onResult(NimBLEAdvertisedDevice* dev) override {
        if (g_probe) g_probe->onScanResult(dev);
    }
};

class ProbeClientCallbacks : public NimBLEClientCallbacks {
    void onConnect(NimBLEClient*) override {}
    void onDisconnect(NimBLEClient* client) override {
        if (g_probe) g_probe->onClientDisconnect(client);
    }
};

static ProbeAdvCallbacks g_advCb;
static ProbeClientCallbacks g_cliCb;

static const char* resultName(uint8_t code) {
    switch (code) {
        case 0x01: return "Success";
        case 0x02: return "Op Code not supported";
        case 0x03: return "Invalid Parameter";
        case 0x04: return "Operation Failed";
        case 0x05: return "Control Not Permitted";
        default: return "unbekannt";
    }
}

void BleProbe::begin(ConfigStore* cfg, ProbeLog* log) {
    cfg_ = cfg;
    log_ = log;
    g_probe = this;
    memset(links_, 0, sizeof(links_));
    memset(scan_, 0, sizeof(scan_));
    scanCount_ = 0;
    guard_.begin(cfg, log);

    NimBLEDevice::init("");
    NimBLEDevice::setPower(ESP_PWR_LVL_P9);
    NimBLEDevice::setSecurityAuth(false, false, false);
    setState(ProbeState::Idle);
    if (log_) log_->addMsg(ProbeLog::Info, -1, "probe ready");
    Serial.printf("[PROBE] ready, max links %d\n", (int)PROBE_MAX_LINKS);
}

void BleProbe::setState(ProbeState s) {
    if (state_ == s) return;
    state_ = s;
    Serial.printf("[PROBE] state %s\n", probeStateName(s));
}

void BleProbe::updateState() {
    if (linkCount() > 0) setState(ProbeState::Linked);
    else if (scanning_) setState(ProbeState::Scanning);
    else setState(ProbeState::Idle);
}

// ── Scan ────────────────────────────────────────────────────────────────────

int BleProbe::findScanIndex(const char* addr) const {
    for (uint8_t i = 0; i < scanCount_; i++) {
        if (strcasecmp(scan_[i].addr, addr) == 0) return i;
    }
    return -1;
}

uint8_t BleProbe::resolveAddrType(const char* mac) const {
    int si = findScanIndex(mac);
    if (si >= 0) return scan_[si].addrType;
    return BLE_ADDR_PUBLIC;
}

bool BleProbe::startScan(char* err, size_t errLen) {
    if (linkCount() > 0 && cfg_ && !cfg_->scanWhileLinked) {
        if (err)
            snprintf(err, errLen,
                     "Scan waehrend Link gesperrt (Config scanWhileLinked=false)");
        if (log_) log_->addMsg(ProbeLog::Info, -1, "scan blocked: link open");
        return false;
    }
    stopScan();
    NimBLEScan* scan = NimBLEDevice::getScan();
    scan->setAdvertisedDeviceCallbacks(&g_advCb, false);
    scan->setActiveScan(true);  // Scan Response holen, sonst fehlen Namen
    scan->setInterval(160);
    scan->setWindow(80);
    scanning_ = true;
    scanStartedAt_ = millis();
    scan->start(0, nullptr, false);  // laeuft bis zum Stop
    if (log_) log_->addMsg(ProbeLog::Info, -1, "scan start");
    updateState();
    return true;
}

void BleProbe::stopScan() {
    NimBLEScan* scan = NimBLEDevice::getScan();
    if (scan->isScanning()) scan->stop();
    if (scanning_ && log_) log_->addMsg(ProbeLog::Info, -1, "scan stop");
    scanning_ = false;
    updateState();
}

void BleProbe::clearScan() {
    memset(scan_, 0, sizeof(scan_));
    scanCount_ = 0;
}

void BleProbe::onScanResult(NimBLEAdvertisedDevice* dev) {
    if (!dev) return;
    std::string addrStr = dev->getAddress().toString();
    const char* a = addrStr.c_str();

    int idx = findScanIndex(a);
    bool isNew = false;
    if (idx < 0) {
        if (scanCount_ >= PROBE_MAX_SCAN) return;
        idx = scanCount_++;
        memset(&scan_[idx], 0, sizeof(ProbeAdvDevice));
        strncpy(scan_[idx].addr, a, sizeof(scan_[idx].addr) - 1);
        isNew = true;
    }
    ProbeAdvDevice& d = scan_[idx];
    d.rssi = dev->getRSSI();
    d.lastSeen = millis();
    d.addrType = dev->getAddress().getType();
    d.advType = dev->getAdvType();
    if (d.seenCount < 0xFFFF) d.seenCount++;

    std::string name = dev->getName();
    if (!name.empty()) {
        strncpy(d.name, name.c_str(), sizeof(d.name) - 1);
        d.name[sizeof(d.name) - 1] = 0;
    }

    // UUIDs ueber alle Pakete hinweg sammeln: manche Geraete legen den Service
    // nur in die Scan Response, andere wechseln zwischen den Paketen.
    size_t n = dev->getServiceUUIDCount();
    for (size_t i = 0; i < n; i++) {
        String key = probeUuidToKey(dev->getServiceUUID(i));
        if (!key.length()) continue;
        bool known = false;
        for (uint8_t k = 0; k < d.uuidCount; k++) {
            if (strcasecmp(d.uuids[k], key.c_str()) == 0) {
                known = true;
                break;
            }
        }
        if (known) continue;
        if (d.uuidCount < PROBE_MAX_ADV_UUID) {
            strncpy(d.uuids[d.uuidCount], key.c_str(), sizeof(d.uuids[0]) - 1);
            d.uuids[d.uuidCount][sizeof(d.uuids[0]) - 1] = 0;
            d.uuidCount++;
        }
        if (key == PROBE_UUID_FTMS) d.hasFtms = true;
        if (key == "180D") d.hasHr = true;
    }

    // Laengstes Rohpaket behalten — damit bleiben die AD-Strukturen offline
    // nachpruefbar, auch die, die NimBLE nicht selbst aufloest.
    size_t plen = dev->getPayloadLength();
    uint8_t* payload = dev->getPayload();
    if (payload && plen > d.payloadLen) {
        if (plen > sizeof(d.payload)) plen = sizeof(d.payload);
        memcpy(d.payload, payload, plen);
        d.payloadLen = (uint8_t)plen;
    }

    if (isNew && log_) {
        char msg[24];
        snprintf(msg, sizeof(msg), "adv %.15s", d.name[0] ? d.name : a);
        log_->addMsg(ProbeLog::Adv, -1, msg);
    }
}

void BleProbe::scanToJson(JsonArray arr) const {
    for (uint8_t i = 0; i < scanCount_; i++) {
        const ProbeAdvDevice& d = scan_[i];
        JsonObject o = arr.add<JsonObject>();
        o["mac"] = d.addr;
        o["name"] = d.name;
        o["rssi"] = d.rssi;
        o["addrType"] = d.addrType;
        o["advType"] = d.advType;
        o["ageMs"] = millis() - d.lastSeen;
        o["seen"] = d.seenCount;
        o["ftms"] = d.hasFtms;
        o["hr"] = d.hasHr;
        JsonArray uu = o["services"].to<JsonArray>();
        for (uint8_t k = 0; k < d.uuidCount; k++) {
            JsonObject u = uu.add<JsonObject>();
            u["uuid"] = d.uuids[k];
            const char* label = probeUuidLabel(d.uuids[k]);
            if (label) u["label"] = label;
            u["vendor"] = probeUuidIsVendorSuspect(d.uuids[k]);
        }
        if (d.payloadLen) o["payload"] = NetUtil::toHex(d.payload, d.payloadLen);
        if (cfg_) {
            if (cfg_->bikeMac.length() && strcasecmp(d.addr, cfg_->bikeMac.c_str()) == 0)
                o["role"] = "bike";
            else if (cfg_->hrMac.length() && strcasecmp(d.addr, cfg_->hrMac.c_str()) == 0)
                o["role"] = "hr";
        }
    }
}

// ── Links ───────────────────────────────────────────────────────────────────

int BleProbe::findLink(const char* mac) const {
    if (!mac || !*mac) return -1;
    for (int i = 0; i < PROBE_MAX_LINKS; i++) {
        if (links_[i].inUse && strcasecmp(links_[i].mac, mac) == 0) return i;
    }
    return -1;
}

bool BleProbe::linkValid(int link) const {
    if (link < 0 || link >= PROBE_MAX_LINKS) return false;
    return links_[link].inUse && links_[link].client && links_[link].client->isConnected();
}

uint8_t BleProbe::linkCount() const {
    uint8_t n = 0;
    for (int i = 0; i < PROBE_MAX_LINKS; i++) {
        if (links_[i].inUse) n++;
    }
    return n;
}

const BleProbe::Link* BleProbe::link(int idx) const {
    if (idx < 0 || idx >= PROBE_MAX_LINKS) return nullptr;
    return &links_[idx];
}

int BleProbe::freeSlot() const {
    for (int i = 0; i < PROBE_MAX_LINKS; i++) {
        if (!links_[i].inUse) return i;
    }
    return -1;
}

int BleProbe::linkOfClient(NimBLEClient* client) const {
    if (!client) return -1;
    for (int i = 0; i < PROBE_MAX_LINKS; i++) {
        if (links_[i].client == client) return i;
    }
    return -1;
}

int BleProbe::connect(const char* mac, int addrTypeHint, char* err, size_t errLen) {
    if (err && errLen) err[0] = 0;
    if (!mac || !*mac) {
        if (err) snprintf(err, errLen, "mac fehlt");
        return -1;
    }
    int existing = findLink(mac);
    if (existing >= 0) return existing;

    int slot = freeSlot();
    if (slot < 0) {
        if (err) snprintf(err, errLen, "kein Linkslot frei (max %d)", (int)PROBE_MAX_LINKS);
        return -1;
    }

    // Scannen und Verbinden gleichzeitig macht den Controller unnoetig fragil.
    if (scanning_) stopScan();

    Link& l = links_[slot];
    memset(&l, 0, sizeof(Link));
    strncpy(l.mac, mac, sizeof(l.mac) - 1);
    uint8_t addrType = (addrTypeHint >= 0) ? (uint8_t)addrTypeHint : resolveAddrType(mac);

    l.client = NimBLEDevice::createClient();
    if (!l.client) {
        if (err) snprintf(err, errLen, "createClient fehlgeschlagen");
        return -1;
    }
    l.client->setClientCallbacks(&g_cliCb, false);
    // Weichere Parameter: weniger Controller-Stress, laengerer Supervision-Timeout
    l.client->setConnectionParams(40, 80, 0, 400, 80, 60);
    l.client->setConnectTimeout(12);
    setState(ProbeState::Connecting);

    bool ok = l.client->connect(NimBLEAddress(mac, addrType));
    if (!ok) {
        // Adresstyp ist die haeufigste Ursache — zweiter Versuch mit dem anderen
        uint8_t alt = (addrType == BLE_ADDR_PUBLIC) ? BLE_ADDR_RANDOM : BLE_ADDR_PUBLIC;
        ok = l.client->connect(NimBLEAddress(mac, alt));
        if (ok) addrType = alt;
    }
    if (!ok) {
        NimBLEDevice::deleteClient(l.client);
        memset(&l, 0, sizeof(Link));
        if (err) snprintf(err, errLen, "connect fehlgeschlagen");
        if (log_) log_->addMsg(ProbeLog::Err, (int8_t)slot, "connect failed");
        updateState();
        return -1;
    }

    l.inUse = true;
    l.addrType = addrType;
    l.connectedAt = millis();
    strncpy(l.mac, l.client->getPeerAddress().toString().c_str(), sizeof(l.mac) - 1);
    l.mac[sizeof(l.mac) - 1] = 0;
    int si = findScanIndex(l.mac);
    if (si >= 0 && scan_[si].name[0]) {
        strncpy(l.name, scan_[si].name, sizeof(l.name) - 1);
    }

    // Volle Attributsuche jetzt, damit read/write/subscribe spaeter ohne
    // Nachentdecken funktionieren. Dauert je Characteristic einen Roundtrip —
    // beim Bike also ein paar Sekunden, dafuer ist danach alles im Cache.
    l.serviceCount = discoverAll(l);
    if (!l.serviceCount && log_) {
        log_->addMsg(ProbeLog::Err, (int8_t)slot, "keine Services gefunden");
    }
    l.rssi = (int8_t)l.client->getRssi();

    suppressReconnect_ = false;
    reconnectTries_ = 0;
    nextReconnectAt_ = 0;
    if (cfg_) {
        // Bike merken, wenn noch keines gesetzt oder MAC matcht
        if (!cfg_->bikeMac.length() || strcasecmp(cfg_->bikeMac.c_str(), l.mac) == 0) {
            cfg_->bikeMac = l.mac;
            if (l.name[0]) cfg_->bikeName = l.name;
            cfg_->bikeAddrType = (int8_t)addrType;
            cfg_->save();
        }
    }

    if (log_) {
        char msg[24];
        snprintf(msg, sizeof(msg), "link %d up, %u svc", slot, (unsigned)l.serviceCount);
        log_->addMsg(ProbeLog::Info, (int8_t)slot, msg);
    }
    Serial.printf("[PROBE] link %d %s type=%u services=%u\n", slot, l.mac, (unsigned)addrType,
                  (unsigned)l.serviceCount);
    cacheFtmsProfile(slot);
    updateState();
    return slot;
}

void BleProbe::cacheFtmsProfile(int link) {
    if (!linkValid(link)) return;
    Link& l = links_[link];
    auto tryRead = [&](const char* uuid, char* dest, size_t destLen) -> bool {
        NimBLERemoteCharacteristic* c = findChar(l, nullptr, uuid);
        if (!c || !c->canRead()) return false;
        std::string v = c->readValue();
        size_t len = v.size();
        if (len > kMaxRead) len = kMaxRead;
        String hx = NetUtil::toHex((const uint8_t*)v.data(), len);
        if (dest && destLen) {
            strncpy(dest, hx.c_str(), destLen - 1);
            dest[destLen - 1] = 0;
        }
        if (log_) log_->add(ProbeLog::Read, (int8_t)link, uuid, (const uint8_t*)v.data(), len);
        return true;
    };

    tryRead("2ACC", featureHex_, sizeof(featureHex_));
    tryRead("2AD6", resistanceRangeHex_, sizeof(resistanceRangeHex_));
    if (tryRead("2AD8", powerRangeHex_, sizeof(powerRangeHex_))) {
        powerRangeMissing_ = false;
    } else {
        powerRangeHex_[0] = 0;
        powerRangeMissing_ = true;
    }

    NimBLERemoteCharacteristic* nameChr = findChar(l, nullptr, "2A00");
    if (nameChr && nameChr->canRead()) {
        std::string v = nameChr->readValue();
        size_t len = v.size();
        if (len > 0 && len < sizeof(deviceNameCache_)) {
            memcpy(deviceNameCache_, v.data(), len);
            deviceNameCache_[len] = 0;
            if (log_)
                log_->add(ProbeLog::Read, (int8_t)link, "2A00", (const uint8_t*)v.data(), len);
            if (!l.name[0]) strncpy(l.name, deviceNameCache_, sizeof(l.name) - 1);
        }
    }

    if (log_) {
        char msg[40];
        snprintf(msg, sizeof(msg), "profile %s%s", featureHex_[0] ? "ftms" : "no-ftms",
                 powerRangeMissing_ ? " no-2AD8" : "");
        log_->addMsg(ProbeLog::Info, (int8_t)link, msg);
    }
}

void BleProbe::armLabSubs(int link) {
    if (!linkValid(link)) return;
    JsonDocument tmp;
    char e2[64] = {0};
    subscribe(link, nullptr, "2AD2", false, true, tmp.to<JsonObject>(), e2, sizeof(e2));
    tmp.clear();
    e2[0] = 0;
    subscribe(link, nullptr, "2AD9", false, true, tmp.to<JsonObject>(), e2, sizeof(e2));
}

void BleProbe::releaseLink(int idx) {
    if (idx < 0 || idx >= PROBE_MAX_LINKS) return;
    Link& l = links_[idx];
    if (l.client) {
        NimBLEDevice::deleteClient(l.client);
        l.client = nullptr;
    }
    // Die Sub-Zeiger zeigen in den freigegebenen Client — nur verwerfen.
    l.subCount = 0;
    l.inUse = false;
    l.dropped = false;
    l.respWait = false;
    l.respSeen = false;
    l.disconnectedAt = millis();
    updateState();
    if (linkCount() == 0) {
        // Live-Werte nicht als aktuell ausgeben, wenn kein Link mehr da ist.
        // Feature/Range-Hex bleiben als Cache fuer Summary/Ergo-Start.
        liveIbd_ = FtmsIbdSample{};
    }
}

bool BleProbe::disconnect(int link) {
    if (link < 0 || link >= PROBE_MAX_LINKS || !links_[link].inUse) return false;
    Link& l = links_[link];
    strncpy(lastDisconnectReason_, "user-disconnect", sizeof(lastDisconnectReason_) - 1);
    lastDisconnectAt_ = millis();
    suppressReconnect_ = true;
    if (l.client && l.client->isConnected()) l.client->disconnect();
    // Auf den Callback warten, damit die HTTP-Antwort die Wahrheit sagt
    uint32_t t0 = millis();
    while (!l.dropped && millis() - t0 < 1500) delay(10);
    if (log_) {
        char msg[24];
        snprintf(msg, sizeof(msg), "link %d down", link);
        log_->addMsg(ProbeLog::Info, (int8_t)link, msg);
    }
    releaseLink(link);
    return true;
}

void BleProbe::disconnectAll() {
    for (int i = 0; i < PROBE_MAX_LINKS; i++) {
        if (links_[i].inUse) disconnect(i);
    }
}

void BleProbe::disconnectAllIntentional() {
    suppressReconnect_ = true;
    strncpy(lastDisconnectReason_, "user-disconnect", sizeof(lastDisconnectReason_) - 1);
    lastDisconnectAt_ = millis();
    disconnectAll();
}

int BleProbe::reconnectBike(char* err, size_t errLen) {
    if (!cfg_ || !cfg_->bikeMac.length()) {
        if (err) snprintf(err, errLen, "kein bikeMac gemerkt");
        return -1;
    }
    suppressReconnect_ = false;
    int existing = findLink(cfg_->bikeMac.c_str());
    if (existing >= 0) return existing;
    return connect(cfg_->bikeMac.c_str(), cfg_->bikeAddrType, err, errLen);
}

void BleProbe::onClientDisconnect(NimBLEClient* client) {
    int idx = linkOfClient(client);
    if (idx < 0) return;
    links_[idx].dropped = true;
}

void BleProbe::linksToJson(JsonArray arr) const {
    for (int i = 0; i < PROBE_MAX_LINKS; i++) {
        const Link& l = links_[i];
        if (!l.inUse) continue;
        JsonObject o = arr.add<JsonObject>();
        o["link"] = i;
        o["mac"] = l.mac;
        o["name"] = l.name;
        o["addrType"] = l.addrType;
        o["rssi"] = l.rssi;
        o["services"] = l.serviceCount;
        o["upMs"] = millis() - l.connectedAt;
        o["notifies"] = l.notifies;
        o["lastNotifyAgeMs"] = l.lastNotifyMs ? (long)(millis() - l.lastNotifyMs) : (long)-1;
        o["connected"] = l.client && l.client->isConnected();
        JsonArray subs = o["subs"].to<JsonArray>();
        for (uint8_t k = 0; k < l.subCount; k++) {
            JsonObject s = subs.add<JsonObject>();
            s["uuid"] = l.subs[k].uuid;
            s["mode"] = l.subs[k].indicate ? "indicate" : "notify";
            s["packets"] = l.subs[k].count;
        }
    }
}

// ── GATT ────────────────────────────────────────────────────────────────────

uint16_t BleProbe::discoverAll(Link& l) {
    if (!l.client) return 0;
    std::vector<NimBLERemoteService*>* svcs = l.client->getServices(true);
    if (!svcs) return 0;
    for (auto svc : *svcs) {
        if (!svc) continue;
        std::vector<NimBLERemoteCharacteristic*>* chrs = svc->getCharacteristics(true);
        if (!chrs) continue;
        for (auto c : *chrs) {
            if (c) c->getDescriptors(true);
        }
    }
    return (uint16_t)svcs->size();
}

NimBLERemoteCharacteristic* BleProbe::findChar(const Link& l, const char* svcKey,
                                               const char* chrKey) {
    if (!l.client || !chrKey || !*chrKey) return nullptr;
    String wantChr = probeUuidNormalizeKey(chrKey);
    String wantSvc = (svcKey && *svcKey) ? probeUuidNormalizeKey(svcKey) : String();

    std::vector<NimBLERemoteService*>* svcs = l.client->getServices(false);
    if (!svcs) return nullptr;
    for (auto svc : *svcs) {
        if (!svc) continue;
        if (wantSvc.length()) {
            if (probeUuidToKey(svc->getUUID()) != wantSvc) continue;
        }
        std::vector<NimBLERemoteCharacteristic*>* chrs = svc->getCharacteristics(false);
        if (!chrs) continue;
        for (auto c : *chrs) {
            if (!c) continue;
            if (probeUuidToKey(c->getUUID()) == wantChr) return c;
        }
    }
    return nullptr;
}

static void appendProps(JsonObject obj, NimBLERemoteCharacteristic* c) {
    JsonObject p = obj["props"].to<JsonObject>();
    p["read"] = c->canRead();
    p["write"] = c->canWrite();
    p["writeNR"] = c->canWriteNoResponse();
    p["notify"] = c->canNotify();
    p["indicate"] = c->canIndicate();
    p["broadcast"] = c->canBroadcast();
}

bool BleProbe::dumpGatt(int link, JsonObject out, char* err, size_t errLen) {
    if (!linkValid(link)) {
        if (err) snprintf(err, errLen, "link %d nicht verbunden", link);
        return false;
    }
    Link& l = links_[link];
    out["link"] = link;
    out["mac"] = l.mac;
    out["name"] = l.name;
    out["addrType"] = l.addrType;
    out["rssi"] = (int)l.client->getRssi();

    std::vector<NimBLERemoteService*>* svcs = l.client->getServices(false);
    if (!svcs || svcs->empty()) {
        discoverAll(l);
        svcs = l.client->getServices(false);
    }
    if (!svcs) {
        if (err) snprintf(err, errLen, "keine Services gefunden");
        return false;
    }
    l.serviceCount = (uint16_t)svcs->size();

    bool hasFtms = false;
    bool hasControl = false;
    bool hasBikeData = false;
    uint8_t vendorCount = 0;

    JsonArray sarr = out["services"].to<JsonArray>();
    for (auto svc : *svcs) {
        if (!svc) continue;
        String skey = probeUuidToKey(svc->getUUID());
        JsonObject so = sarr.add<JsonObject>();
        so["uuid"] = skey;
        const char* slabel = probeUuidLabel(skey.c_str());
        if (slabel) so["label"] = slabel;
        // NimBLE 1.4.x: getStartHandle/getEndHandle sind privat — Char-Handles reichen.
        bool vendor = probeUuidIsVendorSuspect(skey.c_str());
        so["vendor"] = vendor;
        if (vendor) vendorCount++;
        if (skey == PROBE_UUID_FTMS) hasFtms = true;

        JsonArray carr = so["chars"].to<JsonArray>();
        std::vector<NimBLERemoteCharacteristic*>* chrs = svc->getCharacteristics(false);
        if (!chrs) continue;
        for (auto c : *chrs) {
            if (!c) continue;
            String ckey = probeUuidToKey(c->getUUID());
            JsonObject co = carr.add<JsonObject>();
            co["uuid"] = ckey;
            const char* clabel = probeUuidLabel(ckey.c_str());
            if (clabel) co["label"] = clabel;
            co["handle"] = c->getHandle();
            appendProps(co, c);
            if (ckey == PROBE_UUID_CONTROL_POINT) hasControl = true;
            if (ckey == PROBE_UUID_INDOOR_BIKE) hasBikeData = true;

            std::vector<NimBLERemoteDescriptor*>* descs = c->getDescriptors(false);
            if (descs && !descs->empty()) {
                JsonArray darr = co["descriptors"].to<JsonArray>();
                for (auto d : *descs) {
                    if (!d) continue;
                    JsonObject dobj = darr.add<JsonObject>();
                    String dkey = probeUuidToKey(d->getUUID());
                    dobj["uuid"] = dkey;
                    const char* dlabel = probeUuidLabel(dkey.c_str());
                    if (dlabel) dobj["label"] = dlabel;
                    dobj["handle"] = d->getHandle();
                }
            }
        }
    }

    // Die drei Fragen aus BLE-SCAN.md Schritt 2, direkt beantwortet
    JsonObject sum = out["summary"].to<JsonObject>();
    sum["ftms"] = hasFtms;
    sum["controlPoint"] = hasControl;
    sum["indoorBikeData"] = hasBikeData;
    sum["vendorServices"] = vendorCount;
    sum["verdict"] = hasFtms ? (hasControl ? "ftms-mit-controlpoint" : "ftms-ohne-controlpoint")
                             : (vendorCount ? "nur-vendor-services" : "kein-ftms");

    if (log_) {
        char msg[24];
        snprintf(msg, sizeof(msg), "gatt %u svc ftms=%d", (unsigned)l.serviceCount,
                 hasFtms ? 1 : 0);
        log_->addMsg(ProbeLog::Info, (int8_t)link, msg);
    }
    return true;
}

static void appendAsciiText(JsonObject out, const uint8_t* data, size_t len) {
    // Nur setzen, wenn der Inhalt groesstenteils druckbarer Text ist
    // (Device Name, Manufacturer, Firmware …). Reine Binaerwerte wie 2ACC bleiben Hex.
    while (len > 0 && data[len - 1] == 0) len--;
    if (len == 0 || len > 96) return;
    size_t good = 0;
    for (size_t i = 0; i < len; i++) {
        if (data[i] >= 32 && data[i] <= 126) good++;
    }
    if (good * 4 < len * 3) return;  // < 75 % druckbar
    char buf[97];
    for (size_t i = 0; i < len; i++) {
        uint8_t b = data[i];
        buf[i] = (b >= 32 && b <= 126) ? (char)b : '.';
    }
    buf[len] = 0;
    out["text"] = buf;
}

bool BleProbe::readChar(int link, const char* svcKey, const char* chrKey, JsonObject out,
                        char* err, size_t errLen) {
    if (!linkValid(link)) {
        if (err) snprintf(err, errLen, "link %d nicht verbunden", link);
        return false;
    }
    Link& l = links_[link];
    NimBLERemoteCharacteristic* c = findChar(l, svcKey, chrKey);
    if (!c) {
        if (err) snprintf(err, errLen, "characteristic %s nicht gefunden", chrKey);
        return false;
    }
    if (!c->canRead()) {
        if (err) snprintf(err, errLen, "characteristic %s ist nicht lesbar", chrKey);
        return false;
    }
    std::string v = c->readValue();
    size_t len = v.size();
    if (len > kMaxRead) len = kMaxRead;
    String key = probeUuidToKey(c->getUUID());
    String hx = NetUtil::toHex((const uint8_t*)v.data(), len);
    out["link"] = link;
    out["uuid"] = key;
    const char* label = probeUuidLabel(key.c_str());
    if (label) out["label"] = label;
    out["handle"] = c->getHandle();
    out["len"] = (uint16_t)len;
    out["hex"] = hx;
    appendAsciiText(out, (const uint8_t*)v.data(), len);
    appendProps(out, c);
    if (log_) log_->add(ProbeLog::Read, (int8_t)link, key.c_str(), (const uint8_t*)v.data(), len);
    if (key == "2ACC") {
        strncpy(featureHex_, hx.c_str(), sizeof(featureHex_) - 1);
    } else if (key == "2AD6") {
        strncpy(resistanceRangeHex_, hx.c_str(), sizeof(resistanceRangeHex_) - 1);
    } else if (key == "2AD8") {
        strncpy(powerRangeHex_, hx.c_str(), sizeof(powerRangeHex_) - 1);
        powerRangeMissing_ = false;
    } else if (key == "2A00" && len > 0 && len < sizeof(deviceNameCache_)) {
        memcpy(deviceNameCache_, v.data(), len);
        deviceNameCache_[len] = 0;
    }
    return true;
}

bool BleProbe::readAllReadable(int link, JsonObject out, char* err, size_t errLen) {
    if (!linkValid(link)) {
        if (err) snprintf(err, errLen, "link %d nicht verbunden", link);
        return false;
    }
    Link& l = links_[link];
    std::vector<NimBLERemoteService*>* svcs = l.client->getServices(false);
    if (!svcs || svcs->empty()) {
        discoverAll(l);
        svcs = l.client->getServices(false);
    }
    if (!svcs) {
        if (err) snprintf(err, errLen, "keine Services gefunden");
        return false;
    }

    out["link"] = link;
    out["mac"] = l.mac;
    JsonArray reads = out["reads"].to<JsonArray>();
    uint16_t okCount = 0;
    uint16_t failCount = 0;
    uint16_t skipCount = 0;

    for (auto svc : *svcs) {
        if (!svc) continue;
        String skey = probeUuidToKey(svc->getUUID());
        std::vector<NimBLERemoteCharacteristic*>* chrs = svc->getCharacteristics(false);
        if (!chrs) continue;
        for (auto c : *chrs) {
            if (!c) continue;
            if (!c->canRead()) {
                skipCount++;
                continue;
            }
            String ckey = probeUuidToKey(c->getUUID());
            JsonObject ro = reads.add<JsonObject>();
            char e2[80] = {0};
            if (readChar(link, skey.c_str(), ckey.c_str(), ro, e2, sizeof(e2))) {
                ro["ok"] = true;
                okCount++;
            } else {
                ro["ok"] = false;
                ro["uuid"] = ckey;
                ro["error"] = e2[0] ? e2 : "read fehlgeschlagen";
                failCount++;
            }
        }
    }

    out["okCount"] = okCount;
    out["failCount"] = failCount;
    out["skipCount"] = skipCount;
    if (log_) {
        char msg[36];
        snprintf(msg, sizeof(msg), "read-all %u ok %u fail", (unsigned)okCount,
                 (unsigned)failCount);
        log_->addMsg(ProbeLog::Info, (int8_t)link, msg);
    }
    return true;
}

bool BleProbe::subscribe(int link, const char* svcKey, const char* chrKey, bool indicate,
                         bool enable, JsonObject out, char* err, size_t errLen) {
    if (!linkValid(link)) {
        if (err) snprintf(err, errLen, "link %d nicht verbunden", link);
        return false;
    }
    Link& l = links_[link];
    NimBLERemoteCharacteristic* c = findChar(l, svcKey, chrKey);
    if (!c) {
        if (err) snprintf(err, errLen, "characteristic %s nicht gefunden", chrKey);
        return false;
    }
    String key = probeUuidToKey(c->getUUID());

    if (enable) {
        if (indicate && !c->canIndicate()) {
            if (err) snprintf(err, errLen, "%s kann keine Indications", key.c_str());
            return false;
        }
        if (!indicate && !c->canNotify()) {
            if (err) snprintf(err, errLen, "%s kann keine Notifications", key.c_str());
            return false;
        }
        // Manche Peripherals antworten auf CCCD-Writes nicht (Write-Response),
        // obwohl das Abo greift — dann mit response=false nachziehen.
        if (!c->subscribe(!indicate, probeNotifyCb, true) &&
            !c->subscribe(!indicate, probeNotifyCb, false)) {
            if (err) snprintf(err, errLen, "subscribe auf %s fehlgeschlagen", key.c_str());
            return false;
        }
        int found = -1;
        for (uint8_t i = 0; i < l.subCount; i++) {
            if (strcasecmp(l.subs[i].uuid, key.c_str()) == 0) found = i;
        }
        if (found < 0) {
            if (l.subCount >= PROBE_MAX_SUBS) {
                if (err) snprintf(err, errLen, "zu viele Abos (max %d)", (int)PROBE_MAX_SUBS);
                return false;
            }
            found = l.subCount++;
            memset(&l.subs[found], 0, sizeof(Sub));
            strncpy(l.subs[found].uuid, key.c_str(), sizeof(l.subs[0].uuid) - 1);
        }
        l.subs[found].chr = c;
        l.subs[found].indicate = indicate;
    } else {
        c->unsubscribe(true);
        for (uint8_t i = 0; i < l.subCount; i++) {
            if (strcasecmp(l.subs[i].uuid, key.c_str()) != 0) continue;
            for (uint8_t k = i; k + 1 < l.subCount; k++) l.subs[k] = l.subs[k + 1];
            l.subCount--;
            break;
        }
    }

    out["link"] = link;
    out["uuid"] = key;
    out["mode"] = indicate ? "indicate" : "notify";
    out["enabled"] = enable;
    if (log_) {
        char msg[24];
        snprintf(msg, sizeof(msg), "%s %s", enable ? "sub" : "unsub",
                 indicate ? "indicate" : "notify");
        log_->add(ProbeLog::Info, (int8_t)link, key.c_str(), nullptr, 0, msg);
    }
    return true;
}

bool BleProbe::writeChar(int link, const char* svcKey, const char* chrKey, const uint8_t* data,
                         size_t len, bool response, bool awaitIndication, uint16_t timeoutMs,
                         JsonObject out, char* err, size_t errLen) {
    if (!linkValid(link)) {
        if (err) snprintf(err, errLen, "link %d nicht verbunden", link);
        return false;
    }
    if (!data || !len || len > kMaxWrite) {
        if (err) snprintf(err, errLen, "Nutzlast fehlt oder ist zu lang (max %u)",
                          (unsigned)kMaxWrite);
        return false;
    }
    Link& l = links_[link];
    NimBLERemoteCharacteristic* c = findChar(l, svcKey, chrKey);
    if (!c) {
        if (err) snprintf(err, errLen, "characteristic %s nicht gefunden", chrKey);
        return false;
    }
    if (!c->canWrite() && !c->canWriteNoResponse()) {
        if (err) snprintf(err, errLen, "characteristic %s ist nicht schreibbar", chrKey);
        return false;
    }
    String key = probeUuidToKey(c->getUUID());

    uint8_t payload[kMaxWrite];
    size_t plen = len;
    memcpy(payload, data, len);

    const bool isControl = ProbeGuard::isControlPoint(key.c_str());
    if (isControl) {
        ProbeGuard::Verdict v;
        guard_.check(data, len, v);
        out["guard"]["checked"] = true;
        out["guard"]["allowed"] = v.allowed;
        out["guard"]["modified"] = v.modified;
        out["guard"]["opcode"] = data[0];
        out["guard"]["opcodeName"] = ProbeGuard::opcodeName(data[0]);
        if (v.reason[0]) out["guard"]["reason"] = v.reason;
        if (!v.allowed) {
            if (err) snprintf(err, errLen, "%s", v.reason[0] ? v.reason : "vom Limiter abgelehnt");
            return false;
        }
        memcpy(payload, v.data, v.len);
        plen = v.len;
        if (v.modified) out["guard"]["hexSent"] = NetUtil::toHex(payload, plen);

        if (awaitIndication) {
            // Spec: Indicate. Manche Bikes (u.a. dieses Hammer/TC-Geraet) melden
            // die Antwort nur als Notify — beides akzeptieren.
            bool subscribed = false;
            for (uint8_t i = 0; i < l.subCount; i++) {
                if (strcasecmp(l.subs[i].uuid, key.c_str()) == 0)
                    subscribed = true;
            }
            if (!subscribed) {
                if (err)
                    snprintf(err, errLen,
                             "Notify/Indicate auf %s erst aktivieren, sonst kommt keine Antwort",
                             key.c_str());
                return false;
            }
        }
    }

    if (awaitIndication) {
        l.respSeen = false;
        l.respLen = 0;
        l.respWait = true;
    }

    bool ok = c->writeValue(payload, plen, response && c->canWrite());
    if (log_) {
        log_->add(ProbeLog::Write, (int8_t)link, key.c_str(), payload, plen,
                  ok ? nullptr : "write failed");
    }
    out["link"] = link;
    out["uuid"] = key;
    out["hex"] = NetUtil::toHex(payload, plen);
    out["written"] = ok;

    if (!ok) {
        l.respWait = false;
        if (err) snprintf(err, errLen, "write auf %s fehlgeschlagen", key.c_str());
        return false;
    }
    if (isControl) guard_.maybeArm(payload[0]);

    if (!awaitIndication) return true;

    if (timeoutMs < 100) timeoutMs = 100;
    if (timeoutMs > 8000) timeoutMs = 8000;
    uint32_t t0 = millis();
    while (!l.respSeen && millis() - t0 < timeoutMs) delay(5);
    l.respWait = false;

    JsonObject resp = out["response"].to<JsonObject>();
    if (!l.respSeen) {
        resp["timeout"] = true;
        resp["waitedMs"] = millis() - t0;
        return true;  // Der Write selbst war erfolgreich, die Antwort fehlt
    }
    uint8_t rlen = l.respLen;
    resp["timeout"] = false;
    resp["waitedMs"] = millis() - t0;
    resp["hex"] = NetUtil::toHex(l.respData, rlen);
    resp["len"] = rlen;
    // Antwortformat: 80 <opcode> <result>
    if (rlen >= 3 && l.respData[0] == 0x80) {
        resp["opcode"] = l.respData[1];
        resp["opcodeName"] = ProbeGuard::opcodeName(l.respData[1]);
        resp["result"] = l.respData[2];
        resp["resultName"] = resultName(l.respData[2]);
        resp["success"] = (l.respData[2] == 0x01);
        resp["matches"] = (l.respData[1] == payload[0]);
    } else {
        resp["success"] = false;
        resp["note"] = "kein 0x80-Antwortrahmen";
    }
    if (log_) {
        log_->add(ProbeLog::Resp, (int8_t)link, key.c_str(), l.respData, rlen);
    }
    return true;
}

// ── Notifies ────────────────────────────────────────────────────────────────

void BleProbe::onNotify(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len,
                        bool isNotify) {
    if (!chr) return;
    int idx = -1;
    NimBLERemoteService* svc = chr->getRemoteService();
    if (svc) idx = linkOfClient(svc->getClient());

    String key = probeUuidToKey(chr->getUUID());
    if (log_) {
        log_->add(isNotify ? ProbeLog::Notify : ProbeLog::Indicate, (int8_t)idx, key.c_str(),
                  data, len);
    }
    if (idx < 0) return;

    Link& l = links_[idx];
    l.notifies++;
    l.lastNotifyMs = millis();
    for (uint8_t i = 0; i < l.subCount; i++) {
        if (strcasecmp(l.subs[i].uuid, key.c_str()) == 0) l.subs[i].count++;
    }
    if (l.respWait && ProbeGuard::isControlPoint(key.c_str())) {
        size_t n = len > sizeof(l.respData) ? sizeof(l.respData) : len;
        memcpy(l.respData, data, n);
        l.respLen = (uint8_t)n;
        l.respSeen = true;
    }
    if (strcasecmp(key.c_str(), PROBE_UUID_INDOOR_BIKE) == 0 || key == "2AD2") {
        FtmsIbdSample s;
        if (ftmsParseIndoorBike(data, len, s)) {
            s.atMs = millis();
            s.seq = ++liveIbdCount_;
            liveIbd_ = s;
        }
    }
}

// ── Not-Stop ────────────────────────────────────────────────────────────────

void BleProbe::panic(const char* reason) {
    strncpy(lastPanic_, reason ? reason : "panic", sizeof(lastPanic_) - 1);
    lastPanic_[sizeof(lastPanic_) - 1] = 0;
    lastPanicAt_ = millis();
    setState(ProbeState::Panic);
    if (log_) log_->addMsg(ProbeLog::Err, -1, lastPanic_);
    Serial.printf("[PROBE] PANIC: %s\n", lastPanic_);

    // 0x08 0x01 geht direkt raus, nicht durch den Limiter: der Not-Stop darf
    // nicht per Config sperrbar sein.
    static const uint8_t kStop[2] = {0x08, 0x01};
    for (int i = 0; i < PROBE_MAX_LINKS; i++) {
        Link& l = links_[i];
        if (!l.inUse || !l.client || !l.client->isConnected()) continue;
        NimBLERemoteCharacteristic* c = findChar(l, nullptr, PROBE_UUID_CONTROL_POINT);
        if (!c || (!c->canWrite() && !c->canWriteNoResponse())) continue;
        bool ok = c->writeValue(kStop, sizeof(kStop), c->canWrite());
        if (log_) {
            log_->add(ProbeLog::Write, (int8_t)i, PROBE_UUID_CONTROL_POINT, kStop, sizeof(kStop),
                      ok ? "panic stop" : "panic stop failed");
        }
        // Kurz warten, damit das Kommando die Luftschnittstelle verlaesst,
        // bevor der Link abgebaut wird.
        delay(120);
    }

    guard_.disarm();
    suppressReconnect_ = true;
    strncpy(lastDisconnectReason_, reason && reason[0] ? reason : "panic",
            sizeof(lastDisconnectReason_) - 1);
    lastDisconnectAt_ = millis();
    // Direkt trennen ohne erneut suppress zu setzen
    for (int i = 0; i < PROBE_MAX_LINKS; i++) {
        if (!links_[i].inUse) continue;
        Link& l = links_[i];
        if (l.client && l.client->isConnected()) l.client->disconnect();
        uint32_t t0 = millis();
        while (!l.dropped && millis() - t0 < 800) delay(10);
        releaseLink(i);
    }
    stopScan();
    updateState();
}

// ── Zustand ─────────────────────────────────────────────────────────────────

void BleProbe::loop() {
    for (int i = 0; i < PROBE_MAX_LINKS; i++) {
        Link& l = links_[i];
        if (!l.inUse || !l.dropped) continue;
        linkLosses_++;
        strncpy(lastDisconnectReason_, "peer-drop", sizeof(lastDisconnectReason_) - 1);
        lastDisconnectAt_ = millis();
        if (log_) {
            char msg[28];
            snprintf(msg, sizeof(msg), "link %d lost (rssi %d)", i, (int)l.rssi);
            log_->addMsg(ProbeLog::Err, (int8_t)i, msg);
        }
        Serial.printf("[PROBE] link %d lost\n", i);
        releaseLink(i);
        if (cfg_ && cfg_->autoReconnect && !suppressReconnect_) {
            nextReconnectAt_ = millis() + 3000UL;
        }
    }

    if (guard_.expired()) {
        panic("deadman: kein Keepalive");
    }

    // Auto-Reconnect auf gemerktes Bike
    if (cfg_ && cfg_->autoReconnect && !suppressReconnect_ && cfg_->bikeMac.length() &&
        linkCount() == 0 && !scanning_ && nextReconnectAt_ && millis() >= nextReconnectAt_) {
        nextReconnectAt_ = millis() + 8000UL;
        if (reconnectTries_ < 8) {
            reconnectTries_++;
            char err[64] = {0};
            if (log_) {
                char msg[36];
                snprintf(msg, sizeof(msg), "reconnect try %u", (unsigned)reconnectTries_);
                log_->addMsg(ProbeLog::Info, -1, msg);
            }
            int link = connect(cfg_->bikeMac.c_str(), cfg_->bikeAddrType, err, sizeof(err));
            if (link >= 0) {
                armLabSubs(link);
            } else if (reconnectTries_ >= 8) {
                nextReconnectAt_ = 0;
                if (log_) log_->addMsg(ProbeLog::Err, -1, "reconnect give up");
            }
        }
    }

    static uint32_t lastRssi = 0;
    if (millis() - lastRssi > 5000UL) {
        lastRssi = millis();
        for (int i = 0; i < PROBE_MAX_LINKS; i++) {
            Link& l = links_[i];
            if (!l.inUse || !l.client || !l.client->isConnected()) continue;
            int r = l.client->getRssi();
            if (r != 0 && r >= -105 && r <= 0) l.rssi = (int8_t)r;
        }
    }
}

void BleProbe::appendLiveJson(JsonObject obj) const {
    ftmsIbdToJson(liveIbd_, obj);
    obj["packets"] = liveIbdCount_;
}

void BleProbe::appendSummaryJson(JsonObject obj) const {
    obj["version"] = FW_VERSION;
    obj["board"] = PROBE_BOARD_LABEL;
    obj["state"] = probeStateName(state_);
    obj["linkCount"] = linkCount();
    obj["linkLosses"] = linkLosses_;
    const bool linked = linkCount() > 0;
    if (linked) {
        obj["verdict"] = featureHex_[0] ? "ftms-linked" : "linked";
    } else {
        obj["verdict"] = featureHex_[0] ? "ftms-cached" : "idle";
    }
    if (cfg_) {
        obj["bikeMac"] = cfg_->bikeMac;
        obj["bikeName"] = cfg_->bikeName;
        obj["hrMac"] = cfg_->hrMac;
        obj["deadmanMode"] = cfg_->guardDeadmanMode;
        obj["autoReconnect"] = cfg_->autoReconnect;
    }
    if (deviceNameCache_[0]) obj["deviceName"] = deviceNameCache_;
    if (featureHex_[0]) obj["featureHex"] = featureHex_;
    if (resistanceRangeHex_[0]) obj["resistanceRangeHex"] = resistanceRangeHex_;
    if (powerRangeHex_[0]) obj["powerRangeHex"] = powerRangeHex_;
    obj["powerRangeMissing"] = powerRangeMissing_;
    if (lastPanic_[0]) {
        obj["lastPanic"] = lastPanic_;
        obj["lastPanicAgoMs"] = millis() - lastPanicAt_;
    }
    if (lastDisconnectReason_[0]) {
        obj["lastDisconnect"] = lastDisconnectReason_;
        obj["lastDisconnectAgoMs"] = millis() - lastDisconnectAt_;
    }
    obj["suppressReconnect"] = suppressReconnect_;
    obj["reconnectTries"] = reconnectTries_;
    {
        JsonObject live = obj["live"].to<JsonObject>();
        ftmsIbdToJson(liveIbd_, live);
        if (!linked && liveIbd_.valid) live["stale"] = true;
    }
    obj["livePackets"] = liveIbdCount_;
    JsonArray h = obj["hints"].to<JsonArray>();
    h.add("GET /api/probe/summary");
    h.add("GET /api/probe/live");
    h.add("GET /api/probe/export");
    h.add("docs/ergometer/ERGEBNISBERICHT.md");
}

void BleProbe::appendStatusJson(JsonObject obj) const {
    obj["state"] = probeStateName(state_);
    obj["scanning"] = scanning_;
    obj["scanCount"] = scanCount_;
    obj["scanAgeMs"] = scanStartedAt_ ? (millis() - scanStartedAt_) : 0;
    obj["linkCount"] = linkCount();
    obj["maxLinks"] = (int)PROBE_MAX_LINKS;
    obj["linkLosses"] = linkLosses_;
    obj["autoReconnect"] = cfg_ ? cfg_->autoReconnect : false;
    obj["suppressReconnect"] = suppressReconnect_;
    obj["scanWhileLinked"] = cfg_ ? cfg_->scanWhileLinked : false;
    if (lastDisconnectReason_[0]) {
        obj["lastDisconnect"] = lastDisconnectReason_;
        obj["lastDisconnectAgoMs"] = millis() - lastDisconnectAt_;
    }
    if (lastPanic_[0]) {
        obj["lastPanic"] = lastPanic_;
        obj["lastPanicAgoMs"] = millis() - lastPanicAt_;
    }
    linksToJson(obj["links"].to<JsonArray>());
    guard_.appendStatusJson(obj["guard"].to<JsonObject>());
    ftmsIbdToJson(liveIbd_, obj["live"].to<JsonObject>());
}

void BleProbe::appendIoValues(JsonObject ios) const {
    auto addS = [&](const char* key, const char* value) {
        JsonObject o = ios[key].to<JsonObject>();
        o["type"] = "sensor";
        o["value"] = value;
        o["unit"] = "";
    };
    auto addN = [&](const char* key, float value, const char* unit) {
        JsonObject o = ios[key].to<JsonObject>();
        o["type"] = "sensor";
        o["value"] = value;
        o["unit"] = unit;
    };

    addS("probe_state", probeStateName(state_));
    addN("scanning", scanning_ ? 1 : 0, "");
    addN("scan_devices", scanCount_, "");
    addN("link_count", linkCount(), "");
    addN("link_losses", linkLosses_, "");
    addN("cp_writes", guard_.writeCount(), "");
    addN("cp_denies", guard_.denyCount(), "");
    addN("deadman_armed", guard_.armed() ? 1 : 0, "");
    uint32_t notifies = 0;
    for (int i = 0; i < PROBE_MAX_LINKS; i++) {
        if (links_[i].inUse) notifies += links_[i].notifies;
    }
    addN("notify_packets", (float)notifies, "");
    addS("bike_mac", (cfg_ && cfg_->bikeMac.length()) ? cfg_->bikeMac.c_str() : "-");
}
