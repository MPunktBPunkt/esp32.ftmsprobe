#include "ProbeTypes.h"

/** Suffix der SIG-Basis-UUID, ab Position 8 der 36-Zeichen-Form. */
static const char* kSigBaseTail = "-0000-1000-8000-00805F9B34FB";

/**
 * Klappt die 128-Bit-Schreibweise einer SIG-UUID auf die kurze Form zusammen.
 *
 * Ohne das vergleicht sich "00002AD2-0000-1000-8000-00805F9B34FB" nicht mit
 * "2AD2" — ble_uuid_cmp vergleicht zuerst den Typ und liefert bei
 * unterschiedlicher Breite immer "ungleich". Genau daran scheitern
 * Charakteristik-Suchen, wenn ein Geraet seine Attribute in Langform fuehrt.
 */
static void collapseSigUuid(String& s) {
    if (s.length() != 36) return;
    if (strcasecmp(s.c_str() + 8, kSigBaseTail) != 0) return;
    if (!s.startsWith("0000")) return;  // 32-Bit-Basis bleibt lang
    s = s.substring(4, 8);
}

String probeUuidToKey(const NimBLEUUID& uuid) {
    // NimBLE liefert "0x2ad2" fuer 16 Bit und die gestrichelte Form fuer 128 Bit.
    // Wir normalisieren auf Grossbuchstaben ohne "0x".
    std::string s = NimBLEUUID(uuid).toString();
    String out(s.c_str());
    out.trim();
    if (out.startsWith("0x") || out.startsWith("0X")) out = out.substring(2);
    out.toUpperCase();
    collapseSigUuid(out);
    return out;
}

String probeUuidNormalizeKey(const char* key) {
    String out(key ? key : "");
    out.trim();
    if (out.startsWith("0x") || out.startsWith("0X")) out = out.substring(2);
    out.toUpperCase();
    collapseSigUuid(out);
    return out;
}

bool probeUuidFromKey(const char* key, NimBLEUUID& out) {
    if (!key || !*key) return false;
    String s(key);
    s.trim();
    if (s.startsWith("0x") || s.startsWith("0X")) s = s.substring(2);
    if (s.length() == 4) {
        char* end = nullptr;
        unsigned long v = strtoul(s.c_str(), &end, 16);
        if (!end || *end) return false;
        out = NimBLEUUID((uint16_t)v);
        return true;
    }
    if (s.length() == 8) {
        char* end = nullptr;
        unsigned long v = strtoul(s.c_str(), &end, 16);
        if (!end || *end) return false;
        out = NimBLEUUID((uint32_t)v);
        return true;
    }
    if (s.length() == 36) {
        s.toLowerCase();
        NimBLEUUID parsed(std::string(s.c_str()));
        // Ein misslungener Parse liefert eine leere UUID
        if (parsed.bitSize() == 0) return false;
        out = parsed;
        return true;
    }
    return false;
}

struct UuidLabel {
    const char* key;
    const char* label;
};

/** Reihenfolge folgt BLE-SCAN.md, damit die Tabellen vergleichbar bleiben. */
static const UuidLabel kLabels[] = {
    {"1826", "Fitness Machine Service"},
    {"1818", "Cycling Power Service"},
    {"1816", "Cycling Speed and Cadence"},
    {"180D", "Heart Rate"},
    {"180F", "Battery"},
    {"180A", "Device Information"},
    {"1800", "Generic Access"},
    {"1801", "Generic Attribute"},
    {"1814", "Running Speed and Cadence"},
    {"2ACC", "Fitness Machine Feature"},
    {"2AD2", "Indoor Bike Data"},
    {"2AD3", "Training Status"},
    {"2AD6", "Supported Resistance Level Range"},
    {"2AD8", "Supported Power Range"},
    {"2AD9", "Fitness Machine Control Point"},
    {"2ADA", "Fitness Machine Status"},
    {"2AD1", "Rower Data"},
    {"2ACE", "Cross Trainer Data"},
    {"2A37", "Heart Rate Measurement"},
    {"2A38", "Body Sensor Location"},
    {"2A19", "Battery Level"},
    {"2A63", "Cycling Power Measurement"},
    {"2A65", "Cycling Power Feature"},
    {"2A5B", "CSC Measurement"},
    {"2A5C", "CSC Feature"},
    {"2A29", "Manufacturer Name"},
    {"2A24", "Model Number"},
    {"2A25", "Serial Number"},
    {"2A26", "Firmware Revision"},
    {"2A27", "Hardware Revision"},
    {"2A28", "Software Revision"},
    {"2A00", "Device Name"},
    {"2A01", "Appearance"},
    {"2902", "CCCD"},
    {"2901", "User Description"},
    {"FFF0", "Vendor (FitShow-verdaechtig)"},
    {"FFE0", "Vendor (FitShow-verdaechtig)"},
    {"FF00", "Vendor (FitShow-verdaechtig)"},
    {"FFF1", "Vendor"},
    {"FFF2", "Vendor"},
    {"FFE1", "Vendor"},
};

const char* probeUuidLabel(const char* key) {
    if (!key) return nullptr;
    for (size_t i = 0; i < sizeof(kLabels) / sizeof(kLabels[0]); i++) {
        if (strcasecmp(kLabels[i].key, key) == 0) return kLabels[i].label;
    }
    return nullptr;
}

bool probeUuidIsVendorSuspect(const char* key) {
    if (!key) return false;
    // 16-Bit-UUIDs im Hersteller-Bereich, plus alles 128-Bit-Eigene
    if (strlen(key) == 4) {
        return strncasecmp(key, "FF", 2) == 0;
    }
    if (strlen(key) == 36) {
        // Die SIG-Basis endet auf 0000-1000-8000-00805F9B34FB
        return strcasecmp(key + 8, "-0000-1000-8000-00805F9B34FB") != 0;
    }
    return false;
}
