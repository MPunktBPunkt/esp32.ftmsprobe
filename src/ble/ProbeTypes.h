#pragma once

#include <Arduino.h>
#include <NimBLEDevice.h>
#include "BuildFlags.h"

/** Normalform einer UUID fuer API und Log: "2AD2" bei 16 Bit, sonst 36 Zeichen. */
String probeUuidToKey(const NimBLEUUID& uuid);

/** Parst "2AD2", "0x2ad2", 8-stellig oder die 36-Zeichen-Form. */
bool probeUuidFromKey(const char* key, NimBLEUUID& out);

/** Bringt Benutzereingaben auf dieselbe Normalform wie probeUuidToKey.
 *  Die 128-Bit-Langform einer SIG-UUID wird dabei auf 16 Bit zusammengeklappt. */
String probeUuidNormalizeKey(const char* key);

/** Klartextname fuer bekannte SIG-UUIDs, sonst nullptr.
 *  Deckt die Tabellen aus BLE-SCAN.md Schritt 2 und 3 ab, damit ein GATT-Dump
 *  ohne Nachschlagen lesbar ist. */
const char* probeUuidLabel(const char* key);

/** True fuer die proprietaeren Service-UUIDs, die typisch fuer FitShow sind. */
bool probeUuidIsVendorSuspect(const char* key);

// FTMS-Kennungen. Die Sonde ist generisch; nur der Safety-Limiter und der
// Not-Stop muessen den Control Point erkennen.
#define PROBE_UUID_FTMS "1826"
#define PROBE_UUID_INDOOR_BIKE "2AD2"
#define PROBE_UUID_CONTROL_POINT "2AD9"

struct ProbeAdvDevice {
    char addr[18];
    char name[32];
    int8_t rssi;
    uint8_t addrType;
    uint8_t advType;
    uint32_t lastSeen;
    uint16_t seenCount;
    uint8_t uuidCount;
    /** Union aller je beworbenen Service-UUIDs, nicht nur die des letzten Pakets. */
    char uuids[PROBE_MAX_ADV_UUID][37];
    /** Laengstes gesehenes Rohpaket (Advertising bzw. Scan Response).
     *  Damit bleibt die Antwort auf "alle beworbenen Services" offline nachpruefbar. */
    uint8_t payload[62];
    uint8_t payloadLen;
    bool hasFtms;
    bool hasHr;
};

enum class ProbeState : uint8_t { Idle = 0, Scanning, Connecting, Linked, Panic, Error };

inline const char* probeStateName(ProbeState s) {
    switch (s) {
        case ProbeState::Idle: return "IDLE";
        case ProbeState::Scanning: return "SCANNING";
        case ProbeState::Connecting: return "CONNECTING";
        case ProbeState::Linked: return "LINKED";
        case ProbeState::Panic: return "PANIC";
        case ProbeState::Error: return "ERROR";
        default: return "UNKNOWN";
    }
}
