#include "ProbeLog.h"
#include "core/NetUtil.h"

/**
 * Notifies kommen vom NimBLE-Host-Task, Reads/Writes vom Loop-Task. Beide
 * schreiben in denselben Ring und internieren UUIDs, deshalb liegt alles unter
 * einem Spinlock. portMUX ist nicht rekursiv — die *Locked-Helfer duerfen den
 * Lock daher nicht selbst nehmen.
 */
static portMUX_TYPE g_logMux = portMUX_INITIALIZER_UNLOCKED;

static const char* dirName(uint8_t d) {
    switch (d) {
        case ProbeLog::Info: return "info";
        case ProbeLog::Adv: return "adv";
        case ProbeLog::Read: return "read";
        case ProbeLog::Write: return "write";
        case ProbeLog::Resp: return "resp";
        case ProbeLog::Notify: return "notify";
        case ProbeLog::Indicate: return "indicate";
        case ProbeLog::Guard: return "guard";
        case ProbeLog::Err: return "error";
        case ProbeLog::PhaseMark: return "phase";
        default: return "?";
    }
}

void ProbeLog::begin() {
    portENTER_CRITICAL(&g_logMux);
    head_ = 0;
    count_ = 0;
    seq_ = 0;
    dropped_ = 0;
    uuidCount_ = 0;
    phaseCount_ = 0;
    phaseCur_ = internPhaseLocked("idle");
    portEXIT_CRITICAL(&g_logMux);
    anchorUnix_ = 0;
    anchorMs_ = 0;
}

void ProbeLog::anchorTime(uint32_t unixNow) {
    if (!unixNow || anchorUnix_) return;
    anchorUnix_ = unixNow;
    anchorMs_ = millis();
}

uint8_t ProbeLog::internUuidLocked(const char* key) {
    if (!key || !*key) return 0xFF;
    for (uint8_t i = 0; i < uuidCount_; i++) {
        if (strcasecmp(uuidTab_[i], key) == 0) return i;
    }
    if (uuidCount_ >= kUuidTabSize) return 0xFF;
    uint8_t idx = uuidCount_++;
    strncpy(uuidTab_[idx], key, sizeof(uuidTab_[idx]) - 1);
    uuidTab_[idx][sizeof(uuidTab_[idx]) - 1] = 0;
    return idx;
}

uint8_t ProbeLog::internPhaseLocked(const char* label) {
    if (!label || !*label) return phaseCur_;
    for (uint8_t i = 0; i < phaseCount_; i++) {
        if (strcasecmp(phaseTab_[i], label) == 0) return i;
    }
    if (phaseCount_ >= PROBE_MAX_PHASES) return phaseCur_;
    uint8_t idx = phaseCount_++;
    strncpy(phaseTab_[idx], label, sizeof(phaseTab_[idx]) - 1);
    phaseTab_[idx][sizeof(phaseTab_[idx]) - 1] = 0;
    return idx;
}

void ProbeLog::setPhase(const char* label) {
    portENTER_CRITICAL(&g_logMux);
    phaseCur_ = internPhaseLocked(label);
    portEXIT_CRITICAL(&g_logMux);
    // Nicht verschachtelt: add() nimmt den Lock erneut, nachdem er frei ist.
    addMsg(PhaseMark, -1, label);
}

const char* ProbeLog::phase() const {
    if (phaseCur_ >= phaseCount_) return "idle";
    return phaseTab_[phaseCur_];
}

void ProbeLog::add(Dir dir, int8_t link, const char* uuidKey, const uint8_t* data, size_t len,
                   const char* msg) {
    if (len > PROBE_LOG_DATA) len = PROBE_LOG_DATA;

    portENTER_CRITICAL(&g_logMux);
    uint8_t uidx = internUuidLocked(uuidKey);
    Entry& e = ring_[head_];
    if (count_ == PROBE_LOG_SIZE) dropped_++;
    e.seq = ++seq_;
    e.ts = millis();
    e.dir = (uint8_t)dir;
    e.link = link;
    e.phase = phaseCur_;
    e.uuid = uidx;
    e.len = (uint8_t)len;
    if (data && len) memcpy(e.data, data, len);
    if (msg) {
        strncpy(e.msg, msg, sizeof(e.msg) - 1);
        e.msg[sizeof(e.msg) - 1] = 0;
    } else {
        e.msg[0] = 0;
    }
    head_ = (uint16_t)((head_ + 1) % PROBE_LOG_SIZE);
    if (count_ < PROBE_LOG_SIZE) count_++;
    portEXIT_CRITICAL(&g_logMux);
}

void ProbeLog::addMsg(Dir dir, int8_t link, const char* msg) {
    add(dir, link, nullptr, nullptr, 0, msg);
}

uint32_t ProbeLog::firstSeq() const {
    if (!count_) return seq_;
    uint16_t oldest = (uint16_t)((head_ + PROBE_LOG_SIZE - count_) % PROBE_LOG_SIZE);
    return ring_[oldest].seq;
}

void ProbeLog::appendLine(const Entry& e, String& buf) const {
    buf += "{\"seq\":";
    buf += e.seq;
    buf += ",\"ts\":";
    buf += e.ts;
    buf += ",\"dir\":\"";
    buf += dirName(e.dir);
    buf += "\",\"link\":";
    buf += (int)e.link;
    buf += ",\"phase\":\"";
    buf += (e.phase < phaseCount_) ? phaseTab_[e.phase] : "idle";
    buf += "\"";
    if (e.uuid != 0xFF && e.uuid < uuidCount_) {
        buf += ",\"uuid\":\"";
        buf += uuidTab_[e.uuid];
        buf += "\"";
    }
    if (e.len) {
        buf += ",\"hex\":\"";
        buf += NetUtil::toHex(e.data, e.len);
        buf += "\",\"len\":";
        buf += e.len;
    }
    if (e.msg[0]) {
        buf += ",\"msg\":\"";
        // Meldungen sind firmwareseitig gesetzt; Anfuehrungszeichen und
        // Backslashes koennen nicht vorkommen, werden aber ersetzt statt
        // escaped — kaputtes JSON waere hier teurer als ein Zeichen daneben.
        for (const char* p = e.msg; *p; p++) {
            if (*p == '"' || *p == '\\') buf += '\'';
            else buf += *p;
        }
        buf += "\"";
    }
    buf += "}\n";
}

uint16_t ProbeLog::appendNdjson(WebServer& server, uint32_t since, uint16_t max,
                                const char* phaseFilter) {
    portENTER_CRITICAL(&g_logMux);
    uint16_t snapHead = head_;
    uint16_t snapCount = count_;
    portEXIT_CRITICAL(&g_logMux);

    uint16_t written = 0;
    String buf;
    buf.reserve(1400);
    Entry copy;
    for (uint16_t i = 0; i < snapCount && written < max; i++) {
        uint16_t idx = (uint16_t)((snapHead + PROBE_LOG_SIZE - snapCount + i) % PROBE_LOG_SIZE);
        portENTER_CRITICAL(&g_logMux);
        memcpy(&copy, &ring_[idx], sizeof(Entry));
        portEXIT_CRITICAL(&g_logMux);
        if (copy.seq <= since) continue;
        if (phaseFilter && *phaseFilter) {
            const char* ph = (copy.phase < phaseCount_) ? phaseTab_[copy.phase] : "idle";
            if (strcasecmp(ph, phaseFilter) != 0) continue;
        }
        appendLine(copy, buf);
        written++;
        if (buf.length() > 1200) {
            server.sendContent(buf);
            buf = "";
        }
    }
    if (buf.length()) server.sendContent(buf);
    return written;
}

uint16_t ProbeLog::streamNdjson(WebServer& server, uint32_t since, uint16_t max,
                                const char* phaseFilter) {
    NetUtil::addCors(server);
    server.sendHeader(F("Cache-Control"), F("no-store"));
    server.setContentLength(CONTENT_LENGTH_UNKNOWN);
    server.send(200, F("application/x-ndjson"), F(""));
    uint16_t written = appendNdjson(server, since, max, phaseFilter);
    server.sendContent(F(""));
    return written;
}

void ProbeLog::appendStatusJson(JsonObject obj) const {
    obj["entries"] = count_;
    obj["capacity"] = (uint16_t)PROBE_LOG_SIZE;
    obj["firstSeq"] = firstSeq();
    obj["lastSeq"] = seq_;
    obj["dropped"] = dropped_;
    obj["phase"] = phase();
    obj["anchorUnix"] = anchorUnix_;
    obj["anchorMs"] = anchorMs_;
    obj["nowMs"] = millis();
}

void ProbeLog::clear() {
    portENTER_CRITICAL(&g_logMux);
    head_ = 0;
    count_ = 0;
    dropped_ = 0;
    portEXIT_CRITICAL(&g_logMux);
    // seq_ laeuft absichtlich weiter: der Runner erkennt am Sprung, dass
    // geleert wurde, statt alte Eintraege doppelt zu sehen.
}
