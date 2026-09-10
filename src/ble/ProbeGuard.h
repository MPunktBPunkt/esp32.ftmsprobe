#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "BuildFlags.h"
#include "ProbeLog.h"
#include "core/ConfigStore.h"

/**
 * Der einzige Pfad, durch den ein Write an den FTMS Control Point geht.
 * Bewusst in der Firmware und nicht im Runner: ein abgestuerztes CLI-Skript
 * darf kein Ergometer unter Last stehen lassen, waehrend jemand darauf sitzt.
 *
 * Drei Regeln, angelehnt an §6 des Pflichtenhefts:
 *   1. Opcode-Whitelist — 0x00 0x01 0x04 0x05 0x07 0x08 (0x11 nur nach Freigabe)
 *   2. Klemmen fuer Zielleistung und Widerstandsstufe
 *   3. Deadman — nach dem ersten Steuerkommando muss die CLI regelmaessig
 *      ein Keepalive schicken, sonst sendet die Sonde selbst 0x08 0x01
 *      und trennt.
 */
class ProbeGuard {
public:
    struct Verdict {
        bool allowed = false;
        bool modified = false;
        uint8_t data[8] = {0};
        uint8_t len = 0;
        char reason[56] = {0};
    };

    void begin(ConfigStore* cfg, ProbeLog* log);

    static bool isControlPoint(const char* uuidKey);
    static const char* opcodeName(uint8_t opcode);

    /** Prueft und klemmt ein Kommando fuer den Control Point. */
    void check(const uint8_t* in, size_t len, Verdict& v);

    /** Nach einem tatsaechlich abgesetzten Steuerkommando aufrufen. */
    void arm();
    void disarm();
    void keepalive();
    bool armed() const { return armed_; }
    /** True, wenn das Keepalive-Fenster gerissen ist. */
    bool expired() const;
    uint32_t remainingMs() const;
    uint16_t writeCount() const { return writes_; }
    uint16_t denyCount() const { return denies_; }

    void appendStatusJson(JsonObject obj) const;

private:
    ConfigStore* cfg_ = nullptr;
    ProbeLog* log_ = nullptr;
    bool armed_ = false;
    uint32_t lastKeepalive_ = 0;
    uint16_t writes_ = 0;
    uint16_t denies_ = 0;
};
