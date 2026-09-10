#pragma once

#include <Arduino.h>
#include <ArduinoJson.h>

/**
 * Minimaler FTMS-Decoder fuer Live-Anzeige und Summary.
 * Volle Codec-Logik bleibt in tools/ftms.py — hier nur das, was die UI braucht.
 */
struct FtmsIbdSample {
    bool valid = false;
    uint16_t flags = 0;
    float speed = 0;       // km/h
    float cadence = 0;     // rpm
    uint32_t distance = 0; // m
    int16_t power = 0;     // W
    int16_t resistance = 0;
    bool hasResistance = false;
    uint16_t energyTotal = 0;
    uint8_t heartRate = 0;
    uint16_t elapsedS = 0;
    char hex[48] = {0};
    uint32_t atMs = 0;
    uint32_t seq = 0;
};

bool ftmsParseIndoorBike(const uint8_t* data, size_t len, FtmsIbdSample& out);
void ftmsIbdToJson(const FtmsIbdSample& s, JsonObject obj);
