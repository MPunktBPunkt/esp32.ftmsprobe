#pragma once

#include <ArduinoJson.h>
#include <NimBLEDevice.h>
#include "FtmsLive.h"
#include "ProbeGuard.h"
#include "ProbeLog.h"
#include "ProbeTypes.h"
#include "core/ConfigStore.h"

/**
 * Fernsteuerbarer GATT-Explorer. Kennt FTMS absichtlich nur an einer Stelle:
 * dem Control Point, weil Safety-Limiter und Not-Stop ihn erkennen muessen.
 * Alle Protokolllogik (Flags, Feldreihenfolge, Feature-Bits) liegt im Runner —
 * damit sind Varianten ohne Reflash testbar.
 *
 * Aufrufe sind synchron: die HTTP-Antwort traegt das Ergebnis. Das blockiert
 * den Webserver fuer die Dauer der GATT-Operation, ist fuer ein Laborwerkzeug
 * aber die richtige Semantik — das Skript will wissen, was das Geraet gesagt hat.
 * Notifies laufen davon unabhaengig in den Ringpuffer.
 */
class BleProbe {
public:
    static constexpr size_t kMaxWrite = 20;
    static constexpr size_t kMaxRead = 64;

    struct Sub {
        char uuid[37];
        NimBLERemoteCharacteristic* chr;
        bool indicate;
        uint32_t count;
    };

    struct Link {
        NimBLEClient* client;
        bool inUse;
        volatile bool dropped;  // vom Disconnect-Callback gesetzt
        char mac[18];
        char name[32];
        uint8_t addrType;
        uint32_t connectedAt;
        uint32_t disconnectedAt;
        int8_t rssi;
        uint16_t serviceCount;
        uint32_t notifies;
        uint32_t lastNotifyMs;
        uint8_t subCount;
        Sub subs[PROBE_MAX_SUBS];
        // Slot fuer die Control-Point-Indication, die zu einem Write gehoert
        volatile bool respWait;
        volatile bool respSeen;
        uint8_t respData[20];
        volatile uint8_t respLen;
    };

    void begin(ConfigStore* cfg, ProbeLog* log);
    void loop();

    // ── Scan ────────────────────────────────────────────────────────────────
    /** Startet den Scan. Scheitert, wenn Links offen und scanWhileLinked=false. */
    bool startScan(char* err = nullptr, size_t errLen = 0);
    void stopScan();
    bool scanning() const { return scanning_; }
    uint8_t scanCount() const { return scanCount_; }
    void clearScan();
    void scanToJson(JsonArray arr) const;

    // ── Links ───────────────────────────────────────────────────────────────
    /** Verbindet und entdeckt alle Attribute. Gibt den Linkindex oder -1. */
    int connect(const char* mac, int addrTypeHint, char* err, size_t errLen);
    bool disconnect(int link);
    void disconnectAll();
    /** Intentional trennen und Auto-Reconnect unterdruecken. */
    void disconnectAllIntentional();
    /** Sofort versuchen, das gemerkte Bike neu zu verbinden. */
    int reconnectBike(char* err, size_t errLen);
    /** Liest 2ACC/2AD6/2AD8(+2A00) in den Summary-Cache. */
    void cacheFtmsProfile(int link);
    /** Labor-Standardabos: Indoor Bike Data + Control Point (Notify). */
    void armLabSubs(int link);
    void setSuppressReconnect(bool v) { suppressReconnect_ = v; }
    int findLink(const char* mac) const;
    bool linkValid(int link) const;
    uint8_t linkCount() const;
    const Link* link(int idx) const;
    void linksToJson(JsonArray arr) const;

    // ── GATT ────────────────────────────────────────────────────────────────
    bool dumpGatt(int link, JsonObject out, char* err, size_t errLen);
    bool readChar(int link, const char* svcKey, const char* chrKey, JsonObject out, char* err,
                  size_t errLen);
    bool subscribe(int link, const char* svcKey, const char* chrKey, bool indicate, bool enable,
                   JsonObject out, char* err, size_t errLen);
    bool writeChar(int link, const char* svcKey, const char* chrKey, const uint8_t* data,
                   size_t len, bool response, bool awaitIndication, uint16_t timeoutMs,
                   JsonObject out, char* err, size_t errLen);

    /** Not-Stop: 0x08 0x01 auf jeden Link mit Control Point, dann alles trennen.
     *  Nicht durch die Config sperrbar. */
    void panic(const char* reason);

    /** Live Indoor-Bike-Sample (aus 0x2AD2-Notifies). */
    const FtmsIbdSample& liveIbd() const { return liveIbd_; }
    void appendLiveJson(JsonObject obj) const;
    void appendSummaryJson(JsonObject obj) const;

    ProbeGuard& guard() { return guard_; }
    ProbeState state() const { return state_; }
    uint16_t linkLosses() const { return linkLosses_; }
    const char* lastDisconnectReason() const { return lastDisconnectReason_; }

    void appendStatusJson(JsonObject obj) const;
    void appendIoValues(JsonObject ios) const;

    // ── NimBLE-Callbacks ────────────────────────────────────────────────────
    void onScanResult(NimBLEAdvertisedDevice* dev);
    void onNotify(NimBLERemoteCharacteristic* chr, uint8_t* data, size_t len, bool isNotify);
    void onClientDisconnect(NimBLEClient* client);

private:
    int findScanIndex(const char* addr) const;
    uint8_t resolveAddrType(const char* mac) const;
    int freeSlot() const;
    void releaseLink(int idx);
    NimBLERemoteCharacteristic* findChar(const Link& l, const char* svcKey, const char* chrKey);
    /** Services, Characteristics und Deskriptoren vollstaendig entdecken.
     *  Gibt die Anzahl Services zurueck. */
    uint16_t discoverAll(Link& l);
    int linkOfClient(NimBLEClient* client) const;
    void setState(ProbeState s);
    void updateState();

    ConfigStore* cfg_ = nullptr;
    ProbeLog* log_ = nullptr;
    ProbeGuard guard_;

    ProbeState state_ = ProbeState::Idle;
    bool scanning_ = false;

    ProbeAdvDevice scan_[PROBE_MAX_SCAN];
    uint8_t scanCount_ = 0;
    uint32_t scanStartedAt_ = 0;

    Link links_[PROBE_MAX_LINKS];
    uint16_t linkLosses_ = 0;
    char lastPanic_[48] = {0};
    uint32_t lastPanicAt_ = 0;
    char lastDisconnectReason_[48] = {0};
    uint32_t lastDisconnectAt_ = 0;

    FtmsIbdSample liveIbd_;
    uint32_t liveIbdCount_ = 0;
    char featureHex_[24] = {0};
    char resistanceRangeHex_[24] = {0};
    char powerRangeHex_[24] = {0};
    bool powerRangeMissing_ = true;
    char deviceNameCache_[32] = {0};

    bool suppressReconnect_ = false;
    uint32_t nextReconnectAt_ = 0;
    uint8_t reconnectTries_ = 0;
};
