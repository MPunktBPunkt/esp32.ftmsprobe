#include "ConfigStore.h"
#include <Preferences.h>

static Preferences prefs;

void ConfigStore::applyDefaults() {
    deviceName = DEVICE_NAME_DEFAULT;
    hubHost = HUB_HOST_DEFAULT;
    hubPort = HUB_PORT_DEFAULT;
    enableHub = true;
    heartbeatIntervalS = 30;
    enableMdns = true;
    watchdogS = 0;
    bikeMac = "";
    bikeName = "";
    hrMac = "";
    hrName = "";
    guardAllowControl = true;
    guardMaxWatt = PROBE_MAX_WATT_DEFAULT;
    guardMaxLevel = PROBE_MAX_LEVEL_DEFAULT;
    guardAllowSim = false;
    guardMaxGradePct = 5;
    guardDeadmanS = PROBE_DEADMAN_S_DEFAULT;
    guardDeadmanMode = "lab";
    autoReconnect = true;
    scanWhileLinked = false;
    bikeAddrType = -1;
    enableNtp = true;
    ntpServer = NTP_SERVER_DEFAULT;
    tz = TZ_DEFAULT;
}

void ConfigStore::begin() {
    applyDefaults();
    load();
}

void ConfigStore::load() {
    prefs.begin("esphub", true);
    deviceName = prefs.getString("name", deviceName);
    hubHost = prefs.getString("hub_host", hubHost);
    hubPort = prefs.getInt("hub_port", hubPort);
    prefs.end();

    prefs.begin("probe", true);
    uint8_t ver = prefs.getUChar("cfg_ver", 0);
    if (ver == 0) {
        prefs.end();
        return;
    }
    enableHub = prefs.getBool("en_hub", enableHub);
    heartbeatIntervalS = prefs.getUShort("hb_s", heartbeatIntervalS);
    enableMdns = prefs.getBool("en_mdns", enableMdns);
    watchdogS = prefs.getUShort("wdt_s", watchdogS);
    bikeMac = prefs.getString("bike_mac", bikeMac);
    bikeName = prefs.getString("bike_name", bikeName);
    hrMac = prefs.getString("hr_mac", hrMac);
    hrName = prefs.getString("hr_name", hrName);
    guardAllowControl = prefs.getBool("g_ctrl", guardAllowControl);
    guardMaxWatt = prefs.getUShort("g_watt", guardMaxWatt);
    guardMaxLevel = prefs.getUChar("g_level", guardMaxLevel);
    guardAllowSim = prefs.getBool("g_sim", false);  // sicherer Default erzwungen
    guardMaxGradePct = prefs.getUChar("g_grade", guardMaxGradePct);
    guardDeadmanS = prefs.getUShort("g_dead", guardDeadmanS);
    guardDeadmanMode = prefs.getString("g_dmode", guardDeadmanMode);
    autoReconnect = prefs.getBool("auto_rc", autoReconnect);
    scanWhileLinked = prefs.getBool("scan_link", scanWhileLinked);
    bikeAddrType = (int8_t)prefs.getChar("bike_at", bikeAddrType);
    enableNtp = prefs.getBool("en_ntp", enableNtp);
    ntpServer = prefs.getString("ntp", ntpServer);
    tz = prefs.getString("tz", tz);
    prefs.end();
}

void ConfigStore::save() {
    prefs.begin("esphub", false);
    prefs.putString("name", deviceName);
    prefs.putString("hub_host", hubHost);
    prefs.putInt("hub_port", hubPort);
    prefs.end();

    prefs.begin("probe", false);
    prefs.putUChar("cfg_ver", kConfigVersion);
    prefs.putBool("en_hub", enableHub);
    prefs.putUShort("hb_s", heartbeatIntervalS);
    prefs.putBool("en_mdns", enableMdns);
    prefs.putUShort("wdt_s", watchdogS);
    prefs.putString("bike_mac", bikeMac);
    prefs.putString("bike_name", bikeName);
    prefs.putString("hr_mac", hrMac);
    prefs.putString("hr_name", hrName);
    prefs.putBool("g_ctrl", guardAllowControl);
    prefs.putUShort("g_watt", guardMaxWatt);
    prefs.putUChar("g_level", guardMaxLevel);
    prefs.putBool("g_sim", guardAllowSim);
    prefs.putUChar("g_grade", guardMaxGradePct);
    prefs.putUShort("g_dead", guardDeadmanS);
    prefs.putString("g_dmode", guardDeadmanMode);
    prefs.putBool("auto_rc", autoReconnect);
    prefs.putBool("scan_link", scanWhileLinked);
    prefs.putChar("bike_at", (char)bikeAddrType);
    prefs.putBool("en_ntp", enableNtp);
    prefs.putString("ntp", ntpServer);
    prefs.putString("tz", tz);
    prefs.end();
}

void ConfigStore::factoryReset() {
    prefs.begin("esphub", false);
    prefs.clear();
    prefs.end();
    prefs.begin("probe", false);
    prefs.clear();
    prefs.end();
    applyDefaults();
}

void ConfigStore::toJson(JsonObject obj) const {
    obj["deviceName"] = deviceName;
    obj["hubHost"] = hubHost;
    obj["hubPort"] = hubPort;
    obj["enableHub"] = enableHub;
    obj["heartbeatIntervalS"] = heartbeatIntervalS;
    obj["enableMdns"] = enableMdns;
    obj["watchdogS"] = watchdogS;
    obj["bikeMac"] = bikeMac;
    obj["bikeName"] = bikeName;
    obj["hrMac"] = hrMac;
    obj["hrName"] = hrName;
    obj["guardAllowControl"] = guardAllowControl;
    obj["guardMaxWatt"] = guardMaxWatt;
    obj["guardMaxLevel"] = guardMaxLevel;
    obj["guardAllowSim"] = guardAllowSim;
    obj["guardMaxGradePct"] = guardMaxGradePct;
    obj["guardDeadmanS"] = guardDeadmanS;
    obj["guardDeadmanMode"] = guardDeadmanMode;
    obj["autoReconnect"] = autoReconnect;
    obj["scanWhileLinked"] = scanWhileLinked;
    obj["bikeAddrType"] = bikeAddrType;
    obj["enableNtp"] = enableNtp;
    obj["ntpServer"] = ntpServer;
    obj["tz"] = tz;
    obj["board"] = PROBE_BOARD_ID;
    obj["boardLabel"] = PROBE_BOARD_LABEL;
}

static String jsonString(JsonVariantConst v, const String& fallback) {
    if (v.isNull()) return fallback;
    return v.as<String>();
}

bool ConfigStore::fromJson(JsonVariantConst obj) {
    if (!obj.is<JsonObjectConst>()) return false;
    deviceName = jsonString(obj["deviceName"], deviceName);
    hubHost = jsonString(obj["hubHost"], hubHost);
    if (!obj["hubPort"].isNull()) hubPort = obj["hubPort"].as<int>();
    if (!obj["enableHub"].isNull()) enableHub = obj["enableHub"].as<bool>();
    if (!obj["heartbeatIntervalS"].isNull()) heartbeatIntervalS = obj["heartbeatIntervalS"].as<uint16_t>();
    if (!obj["enableMdns"].isNull()) enableMdns = obj["enableMdns"].as<bool>();
    if (!obj["watchdogS"].isNull()) watchdogS = obj["watchdogS"].as<uint16_t>();
    bikeMac = jsonString(obj["bikeMac"], bikeMac);
    bikeName = jsonString(obj["bikeName"], bikeName);
    hrMac = jsonString(obj["hrMac"], hrMac);
    hrName = jsonString(obj["hrName"], hrName);
    if (!obj["guardAllowControl"].isNull()) guardAllowControl = obj["guardAllowControl"].as<bool>();
    if (!obj["guardMaxWatt"].isNull()) guardMaxWatt = obj["guardMaxWatt"].as<uint16_t>();
    if (!obj["guardMaxLevel"].isNull()) guardMaxLevel = obj["guardMaxLevel"].as<uint8_t>();
    if (!obj["guardAllowSim"].isNull()) guardAllowSim = obj["guardAllowSim"].as<bool>();
    if (!obj["guardMaxGradePct"].isNull()) guardMaxGradePct = obj["guardMaxGradePct"].as<uint8_t>();
    if (!obj["guardDeadmanS"].isNull()) guardDeadmanS = obj["guardDeadmanS"].as<uint16_t>();
    guardDeadmanMode = jsonString(obj["guardDeadmanMode"], guardDeadmanMode);
    if (guardDeadmanMode != "safe" && guardDeadmanMode != "lab" && guardDeadmanMode != "off")
        guardDeadmanMode = "lab";
    if (!obj["autoReconnect"].isNull()) autoReconnect = obj["autoReconnect"].as<bool>();
    if (!obj["scanWhileLinked"].isNull()) scanWhileLinked = obj["scanWhileLinked"].as<bool>();
    if (!obj["bikeAddrType"].isNull()) bikeAddrType = (int8_t)obj["bikeAddrType"].as<int>();
    if (!obj["enableNtp"].isNull()) enableNtp = obj["enableNtp"].as<bool>();
    ntpServer = jsonString(obj["ntpServer"], ntpServer);
    tz = jsonString(obj["tz"], tz);

    if (ntpServer.length() == 0) ntpServer = NTP_SERVER_DEFAULT;
    if (tz.length() == 0) tz = TZ_DEFAULT;
    if (heartbeatIntervalS < 5) heartbeatIntervalS = 5;
    // Die Klemmen selbst sind geklemmt — sonst waere der Limiter per API aushebelbar
    if (guardMaxWatt > 400) guardMaxWatt = 400;
    if (guardMaxWatt < 20) guardMaxWatt = 20;
    if (guardMaxLevel > 41) guardMaxLevel = 41;
    if (guardMaxLevel < 1) guardMaxLevel = 1;
    if (guardMaxGradePct > 10) guardMaxGradePct = 10;
    if (guardDeadmanS > 300) guardDeadmanS = 300;
    return true;
}
