#include "ProbeGuard.h"
#include "ProbeTypes.h"

void ProbeGuard::begin(ConfigStore* cfg, ProbeLog* log) {
    cfg_ = cfg;
    log_ = log;
    armed_ = false;
    lastKeepalive_ = 0;
    writes_ = 0;
    denies_ = 0;
}

bool ProbeGuard::isControlPoint(const char* uuidKey) {
    if (!uuidKey) return false;
    if (strcasecmp(uuidKey, PROBE_UUID_CONTROL_POINT) == 0) return true;
    // 128-Bit-Schreibweise derselben SIG-UUID
    return strcasecmp(uuidKey, "00002AD9-0000-1000-8000-00805F9B34FB") == 0;
}

const char* ProbeGuard::opcodeName(uint8_t opcode) {
    switch (opcode) {
        case 0x00: return "Request Control";
        case 0x01: return "Reset";
        case 0x02: return "Set Target Speed";
        case 0x03: return "Set Target Incline";
        case 0x04: return "Set Target Resistance";
        case 0x05: return "Set Target Power";
        case 0x06: return "Set Target Heart Rate";
        case 0x07: return "Start or Resume";
        case 0x08: return "Stop or Pause";
        case 0x11: return "Set Sim Parameters";
        case 0x80: return "Response Code";
        default: return "unbekannt";
    }
}

void ProbeGuard::check(const uint8_t* in, size_t len, Verdict& v) {
    v.allowed = false;
    v.modified = false;
    v.len = 0;
    v.reason[0] = 0;

    if (!cfg_) {
        strncpy(v.reason, "Guard nicht initialisiert", sizeof(v.reason) - 1);
        denies_++;
        return;
    }
    if (!len || !in) {
        strncpy(v.reason, "leeres Kommando", sizeof(v.reason) - 1);
        denies_++;
        return;
    }
    if (len > sizeof(v.data)) {
        snprintf(v.reason, sizeof(v.reason), "Kommando zu lang (%u Byte)", (unsigned)len);
        denies_++;
        return;
    }
    if (!cfg_->guardAllowControl) {
        strncpy(v.reason, "Control-Writes per Config gesperrt", sizeof(v.reason) - 1);
        denies_++;
        return;
    }

    memcpy(v.data, in, len);
    v.len = (uint8_t)len;
    const uint8_t op = in[0];

    switch (op) {
        case 0x00:  // Request Control
        case 0x01:  // Reset
        case 0x07:  // Start or Resume
            v.allowed = true;
            break;

        case 0x08:  // Stop or Pause — immer erlaubt, das ist der Notausgang
            if (len == 1) {
                v.data[1] = 0x01;  // ohne Parameter als Stop absetzen
                v.len = 2;
                v.modified = true;
                strncpy(v.reason, "Parameter fehlte, als Stop (01) gesendet", sizeof(v.reason) - 1);
            } else if (v.data[1] != 0x01 && v.data[1] != 0x02) {
                v.data[1] = 0x01;
                v.len = 2;
                v.modified = true;
                strncpy(v.reason, "Parameter ungueltig, auf Stop (01) geklemmt", sizeof(v.reason) - 1);
            }
            v.allowed = true;
            break;

        case 0x04: {  // Set Target Resistance Level
            const uint8_t maxLevel = cfg_->guardMaxLevel;
            if (len == 2) {
                if (v.data[1] > maxLevel) {
                    snprintf(v.reason, sizeof(v.reason), "Stufe %u auf %u geklemmt",
                             (unsigned)v.data[1], (unsigned)maxLevel);
                    v.data[1] = maxLevel;
                    v.modified = true;
                }
                v.allowed = true;
            } else if (len == 3) {
                // Variante sint16 in 0,1er-Schritten (BLE-SCAN.md, Schritt 4)
                int16_t raw = (int16_t)((uint16_t)v.data[1] | ((uint16_t)v.data[2] << 8));
                int16_t maxRaw = (int16_t)(maxLevel * 10);
                int16_t clamped = raw;
                if (clamped < 0) clamped = 0;
                if (clamped > maxRaw) clamped = maxRaw;
                if (clamped != raw) {
                    snprintf(v.reason, sizeof(v.reason), "Stufe %d auf %d geklemmt (0,1er)",
                             (int)raw, (int)clamped);
                    v.data[1] = (uint8_t)(clamped & 0xFF);
                    v.data[2] = (uint8_t)((clamped >> 8) & 0xFF);
                    v.modified = true;
                }
                v.allowed = true;
            } else {
                strncpy(v.reason, "0x04 braucht 1 oder 2 Parameterbytes", sizeof(v.reason) - 1);
            }
            break;
        }

        case 0x05: {  // Set Target Power
            if (len != 3) {
                strncpy(v.reason, "0x05 braucht sint16 (2 Byte)", sizeof(v.reason) - 1);
                break;
            }
            int16_t watt = (int16_t)((uint16_t)v.data[1] | ((uint16_t)v.data[2] << 8));
            int16_t clamped = watt;
            if (clamped < 0) clamped = 0;
            if (clamped > (int16_t)cfg_->guardMaxWatt) clamped = (int16_t)cfg_->guardMaxWatt;
            if (clamped != watt) {
                snprintf(v.reason, sizeof(v.reason), "%d W auf %d W geklemmt", (int)watt,
                         (int)clamped);
                v.data[1] = (uint8_t)(clamped & 0xFF);
                v.data[2] = (uint8_t)((clamped >> 8) & 0xFF);
                v.modified = true;
            }
            v.allowed = true;
            break;
        }

        case 0x11: {  // Set Indoor Bike Simulation Parameters
            if (!cfg_->guardAllowSim) {
                strncpy(v.reason, "0x11 gesperrt (guardAllowSim=false)", sizeof(v.reason) - 1);
                break;
            }
            if (len != 7) {
                strncpy(v.reason, "0x11 braucht 6 Parameterbytes", sizeof(v.reason) - 1);
                break;
            }
            // Steigung: sint16 in 0,01 % an Offset 3
            int16_t grade = (int16_t)((uint16_t)v.data[3] | ((uint16_t)v.data[4] << 8));
            int16_t maxGrade = (int16_t)((int)cfg_->guardMaxGradePct * 100);
            int16_t clamped = grade;
            if (clamped > maxGrade) clamped = maxGrade;
            if (clamped < -maxGrade) clamped = -maxGrade;
            if (clamped != grade) {
                snprintf(v.reason, sizeof(v.reason), "Steigung %d auf %d geklemmt (0,01%%)",
                         (int)grade, (int)clamped);
                v.data[3] = (uint8_t)(clamped & 0xFF);
                v.data[4] = (uint8_t)((clamped >> 8) & 0xFF);
                v.modified = true;
            }
            v.allowed = true;
            break;
        }

        default:
            snprintf(v.reason, sizeof(v.reason), "Opcode 0x%02X nicht in der Whitelist",
                     (unsigned)op);
            break;
    }

    if (!v.allowed) {
        denies_++;
        v.len = 0;
        if (log_) log_->addMsg(ProbeLog::Guard, -1, v.reason);
        return;
    }
    writes_++;
    if (v.modified && log_) log_->addMsg(ProbeLog::Guard, -1, v.reason);
}

void ProbeGuard::arm() {
    if (!armed_) {
        armed_ = true;
        if (log_) log_->addMsg(ProbeLog::Info, -1, "deadman armed");
    }
    lastKeepalive_ = millis();
}

void ProbeGuard::disarm() {
    if (armed_ && log_) log_->addMsg(ProbeLog::Info, -1, "deadman disarmed");
    armed_ = false;
    lastKeepalive_ = 0;
}

void ProbeGuard::keepalive() {
    lastKeepalive_ = millis();
}

bool ProbeGuard::expired() const {
    if (!armed_ || !cfg_ || cfg_->guardDeadmanS == 0) return false;
    return (millis() - lastKeepalive_) > (uint32_t)cfg_->guardDeadmanS * 1000UL;
}

uint32_t ProbeGuard::remainingMs() const {
    if (!armed_ || !cfg_ || cfg_->guardDeadmanS == 0) return 0;
    uint32_t window = (uint32_t)cfg_->guardDeadmanS * 1000UL;
    uint32_t elapsed = millis() - lastKeepalive_;
    return elapsed >= window ? 0 : (window - elapsed);
}

void ProbeGuard::appendStatusJson(JsonObject obj) const {
    obj["allowControl"] = cfg_ ? cfg_->guardAllowControl : false;
    obj["allowSim"] = cfg_ ? cfg_->guardAllowSim : false;
    obj["maxWatt"] = cfg_ ? cfg_->guardMaxWatt : 0;
    obj["maxLevel"] = cfg_ ? cfg_->guardMaxLevel : 0;
    obj["deadmanS"] = cfg_ ? cfg_->guardDeadmanS : 0;
    obj["armed"] = armed_;
    obj["remainingMs"] = remainingMs();
    obj["writes"] = writes_;
    obj["denies"] = denies_;
}
