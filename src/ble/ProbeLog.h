#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include <WebServer.h>
#include "BuildFlags.h"

/**
 * Ringpuffer fuer alles, was ueber die Links geht. Ausgabe als NDJSON, eine
 * Zeile je Eintrag — genau das Format, das der Runner als Fixture fuer die
 * spaeteren FtmsCodec-Tests ablegt (Abnahmekriterium 3 der v0.1).
 *
 * UUIDs und Phasenlabels werden internalisiert: der Eintrag haelt nur Indizes.
 * Das drueckt einen Eintrag auf gut 70 Byte und macht einen 256er-Ring auf dem
 * D1 Mini bezahlbar.
 */
class ProbeLog {
public:
    enum Dir : uint8_t {
        Info = 0,
        Adv,
        Read,
        Write,
        Resp,     // Indication auf dem Control Point, einem Write zugeordnet
        Notify,
        Indicate,
        Guard,    // vom Safety-Limiter veraendert oder abgelehnt
        Err,
        PhaseMark
    };

    void begin();

    /** Setzt das aktuelle Phasenlabel (idle, pedaling, write-power-100, ...). */
    void setPhase(const char* label);
    const char* phase() const;

    void add(Dir dir, int8_t link, const char* uuidKey, const uint8_t* data, size_t len,
             const char* msg = nullptr);
    void addMsg(Dir dir, int8_t link, const char* msg);

    uint32_t lastSeq() const { return seq_; }
    uint32_t firstSeq() const;
    uint16_t count() const { return count_; }
    uint32_t dropped() const { return dropped_; }

    /** Streamt NDJSON-Zeilen mit seq > since, hoechstens max Stueck.
     *  Gibt die Anzahl geschriebener Zeilen zurueck. */
    uint16_t streamNdjson(WebServer& server, uint32_t since, uint16_t max,
                          const char* phaseFilter);

    void appendStatusJson(JsonObject obj) const;
    void clear();

    /** Unixzeit, die millis()==bootAnchorMs entspricht; 0 solange NTP fehlt. */
    void anchorTime(uint32_t unixNow);
    uint32_t anchorUnix() const { return anchorUnix_; }
    uint32_t anchorMs() const { return anchorMs_; }

private:
    struct Entry {
        uint32_t seq;
        uint32_t ts;
        uint8_t dir;
        int8_t link;
        uint8_t phase;
        uint8_t uuid;
        uint8_t len;
        uint8_t data[PROBE_LOG_DATA];
        char msg[24];
    };

    static constexpr uint8_t kUuidTabSize = 32;

    /** Erwarten einen gehaltenen Lock — portMUX ist nicht rekursiv. */
    uint8_t internUuidLocked(const char* key);
    uint8_t internPhaseLocked(const char* label);
    void appendLine(const Entry& e, String& buf) const;

    Entry ring_[PROBE_LOG_SIZE];
    uint16_t head_ = 0;    // naechster Schreibindex
    uint16_t count_ = 0;
    uint32_t seq_ = 0;
    uint32_t dropped_ = 0;

    char uuidTab_[kUuidTabSize][37];
    uint8_t uuidCount_ = 0;
    char phaseTab_[PROBE_MAX_PHASES][24];
    uint8_t phaseCount_ = 0;
    uint8_t phaseCur_ = 0;

    uint32_t anchorUnix_ = 0;
    uint32_t anchorMs_ = 0;
};
