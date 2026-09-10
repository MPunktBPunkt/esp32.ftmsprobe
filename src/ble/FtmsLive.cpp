#include "FtmsLive.h"
#include "core/NetUtil.h"

bool ftmsParseIndoorBike(const uint8_t* data, size_t len, FtmsIbdSample& out) {
    out = FtmsIbdSample{};
    if (!data || len < 2) return false;
    uint16_t flags = (uint16_t)data[0] | ((uint16_t)data[1] << 8);
    out.flags = flags;
    size_t off = 2;

    auto need = [&](size_t n) -> bool { return off + n <= len; };
    auto u16 = [&]() -> uint16_t {
        uint16_t v = (uint16_t)data[off] | ((uint16_t)data[off + 1] << 8);
        off += 2;
        return v;
    };
    auto s16 = [&]() -> int16_t { return (int16_t)u16(); };
    auto u24 = [&]() -> uint32_t {
        uint32_t v = (uint32_t)data[off] | ((uint32_t)data[off + 1] << 8) |
                     ((uint32_t)data[off + 2] << 16);
        off += 3;
        return v;
    };

    // Bit 0 inverted: clear => Instantaneous Speed present
    if ((flags & 0x0001) == 0) {
        if (!need(2)) return false;
        out.speed = u16() / 100.0f;
    }
    if (flags & 0x0002) {
        if (!need(2)) return false;
        (void)u16();  // avg speed
    }
    if (flags & 0x0004) {
        if (!need(2)) return false;
        out.cadence = u16() / 2.0f;
    }
    if (flags & 0x0008) {
        if (!need(2)) return false;
        (void)u16();  // avg cadence
    }
    if (flags & 0x0010) {
        if (!need(3)) return false;
        out.distance = u24();
    }
    if (flags & 0x0020) {
        if (!need(2)) return false;
        out.resistance = s16();
        out.hasResistance = true;
    }
    if (flags & 0x0040) {
        if (!need(2)) return false;
        out.power = s16();
    }
    if (flags & 0x0080) {
        if (!need(2)) return false;
        (void)s16();  // avg power
    }
    if (flags & 0x0100) {
        if (!need(5)) return false;
        out.energyTotal = u16();
        (void)u16();  // per hour
        off += 1;     // per min
    }
    if (flags & 0x0200) {
        if (!need(1)) return false;
        out.heartRate = data[off++];
    }
    if (flags & 0x0400) {
        if (!need(1)) return false;
        off += 1;  // MET
    }
    if (flags & 0x0800) {
        if (!need(2)) return false;
        out.elapsedS = u16();
    }

    String hx = NetUtil::toHex(data, len > 24 ? 24 : len);
    strncpy(out.hex, hx.c_str(), sizeof(out.hex) - 1);
    out.valid = true;
    return true;
}

void ftmsIbdToJson(const FtmsIbdSample& s, JsonObject obj) {
    obj["valid"] = s.valid;
    if (!s.valid) return;
    obj["flags"] = s.flags;
    char fh[12];
    snprintf(fh, sizeof(fh), "0x%04X", (unsigned)s.flags);
    obj["flagsHex"] = fh;
    obj["speed"] = s.speed;
    obj["cadence"] = s.cadence;
    obj["distance"] = s.distance;
    obj["power"] = s.power;
    if (s.hasResistance) obj["resistance"] = s.resistance;
    obj["energyTotal"] = s.energyTotal;
    obj["heartRate"] = s.heartRate;
    obj["elapsedS"] = s.elapsedS;
    obj["hex"] = s.hex;
    obj["ageMs"] = s.atMs ? (millis() - s.atMs) : 0;
    obj["seq"] = s.seq;
}
