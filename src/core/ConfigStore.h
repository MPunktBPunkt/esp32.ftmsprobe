#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>
#include "BuildFlags.h"

/**
 * NVS-Namespaces: "esphub" fuer die Familienfelder (Name, Hub), "probe" fuer
 * den Rest. Damit ueberlebt die Grundkonfiguration einen Firmware-Wechsel
 * zwischen heartrate und Sonde auf demselben Chip.
 */
class ConfigStore {
public:
    String deviceName = DEVICE_NAME_DEFAULT;
    String hubHost = HUB_HOST_DEFAULT;
    int hubPort = HUB_PORT_DEFAULT;
    bool enableHub = true;
    uint16_t heartbeatIntervalS = 30;
    bool enableMdns = true;
    /** Hub-Watchdog. Fuer die Sonde standardmaessig AUS: ein Reboot mitten in
     *  einer Messung wuerde den Link ohne Stop-Kommando abreissen lassen. */
    uint16_t watchdogS = 0;

    // Gemerkte Geraete, damit der Runner nicht jedes Mal scannen muss
    String bikeMac;
    String bikeName;
    String hrMac;
    String hrName;

    // ── Safety-Limiter (§6 Pflichtenheft, hier in der Minimalfassung) ───────
    /** Erlaubt Schreibzugriffe auf den FTMS Control Point ueberhaupt. */
    bool guardAllowControl = true;
    /** Klemme fuer 0x05 Set Target Power. */
    uint16_t guardMaxWatt = PROBE_MAX_WATT_DEFAULT;
    /** Klemme fuer 0x04 Set Target Resistance Level. */
    uint8_t guardMaxLevel = PROBE_MAX_LEVEL_DEFAULT;
    /** Erlaubt 0x11 Set Indoor Bike Simulation Parameters. Standard aus:
     *  klaert die offene Frage aus §15, gehoert aber nicht in den Erstlauf. */
    bool guardAllowSim = false;
    /** Klemme fuer die Steigung in 0x11, in Prozent. */
    uint8_t guardMaxGradePct = 5;
    /** Deadman-Fenster in Sekunden; 0 schaltet den Deadman ab (nicht empfohlen). */
    uint16_t guardDeadmanS = PROBE_DEADMAN_S_DEFAULT;

    bool enableNtp = true;
    String ntpServer = NTP_SERVER_DEFAULT;
    String tz = TZ_DEFAULT;

    void begin();
    void load();
    void save();
    void factoryReset();
    void applyDefaults();
    void toJson(JsonObject obj) const;
    bool fromJson(JsonVariantConst obj);

private:
    static constexpr uint8_t kConfigVersion = 1;
};
