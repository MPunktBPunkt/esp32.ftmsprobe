"""FTMS-Codec in Python.

Reines Parsen und Bauen von Bytes, ohne BLE-Abhaengigkeit — dieselbe Trennung,
die das Pflichtenheft fuer `FtmsCodec` in esp32.ergo vorsieht. Damit ist dieser
Code die ausfuehrbare Referenz fuer die C++-Portierung, und die JSONL-Logs der
Sonde sind die Fixtures dafuer.

Zwei Fallen, die §5 des Pflichtenhefts ausdruecklich nennt und die hier
namentlich getestet sind:

  * Flag-Bit 0 (More Data) ist **invertiert**: 0 bedeutet, dass Instantaneous
    Speed vorhanden ist.
  * Total Distance ist **uint24**, nicht uint32. Wer 4 Byte liest, verschiebt
    alle Folgefelder.

Aufruf als Selbsttest:  python3 ftms.py
"""

from __future__ import annotations

import sys

# ── Indoor Bike Data 0x2AD2 ──────────────────────────────────────────────────

# Reihenfolge ist die Feldreihenfolge im Paket, nicht die Bitnummer — bei FTMS
# folgen nur die gesetzten Felder, aber immer in dieser Ordnung.
# Bit -> Liste von (Schluessel, Bytes, vorzeichenbehaftet, Teiler)
_IBD_ORDER = [
    (0, [("speed", 2, False, 100.0)]),               # invertiert, s.u.
    (1, [("speed_avg", 2, False, 100.0)]),
    (2, [("cadence", 2, False, 2.0)]),
    (3, [("cadence_avg", 2, False, 2.0)]),
    (4, [("distance", 3, False, 1.0)]),              # uint24
    (5, [("resistance", 2, True, 1.0)]),
    (6, [("power", 2, True, 1.0)]),
    (7, [("power_avg", 2, True, 1.0)]),
    (8, [("energy_total", 2, False, 1.0),            # dreiteilig
         ("energy_per_hour", 2, False, 1.0),
         ("energy_per_min", 1, False, 1.0)]),
    (9, [("heart_rate", 1, False, 1.0)]),
    (10, [("met", 1, False, 10.0)]),
    (11, [("elapsed_s", 2, False, 1.0)]),
    (12, [("remaining_s", 2, False, 1.0)]),
]

# Maximal moegliche Paketlaenge: alle Felder gesetzt, Bit 0 geloescht.
MAX_IBD_LEN = 30

IBD_FLAG_NAMES = {
    0: "More Data (invertiert: 0 = Instantaneous Speed vorhanden)",
    1: "Average Speed",
    2: "Instantaneous Cadence",
    3: "Average Cadence",
    4: "Total Distance (uint24)",
    5: "Resistance Level",
    6: "Instantaneous Power",
    7: "Average Power",
    8: "Expended Energy",
    9: "Heart Rate",
    10: "Metabolic Equivalent",
    11: "Elapsed Time",
    12: "Remaining Time",
}


class FtmsParseError(ValueError):
    pass


def _uint(data: bytes, off: int, size: int) -> int:
    return int.from_bytes(data[off:off + size], "little", signed=False)


def _sint(data: bytes, off: int, size: int) -> int:
    return int.from_bytes(data[off:off + size], "little", signed=True)


def parse_indoor_bike_data(data: bytes) -> dict:
    """Zerlegt ein 0x2AD2-Paket. Wirft FtmsParseError bei zu kurzen Paketen."""
    if len(data) < 2:
        raise FtmsParseError(f"Paket zu kurz ({len(data)} Byte)")
    flags = _uint(data, 0, 2)
    out: dict = {"flags": flags, "flags_bin": f"{flags:016b}"}
    off = 2

    def take(key: str, size: int, signed: bool, div: float):
        nonlocal off
        if off + size > len(data):
            raise FtmsParseError(
                f"Feld {key} braucht {size} Byte ab Offset {off}, "
                f"Paket hat nur {len(data)}"
            )
        raw = _sint(data, off, size) if signed else _uint(data, off, size)
        off += size
        out[key] = raw / div if div != 1.0 else raw
        out.setdefault("_raw", {})[key] = raw

    for bit, fields in _IBD_ORDER:
        if bit == 0:
            # Die Falle: Bit 0 gesetzt heisst "kein Instantaneous Speed".
            present = (flags & 1) == 0
        else:
            present = bool(flags & (1 << bit))
        if not present:
            continue
        for key, size, signed, div in fields:
            take(key, size, signed, div)

    out["_consumed"] = off
    out["_trailing"] = len(data) - off
    return out


def ibd_flag_list(flags: int) -> list[str]:
    names = []
    for bit, label in IBD_FLAG_NAMES.items():
        if bit == 0:
            if (flags & 1) == 0:
                names.append("Instantaneous Speed")
            continue
        if flags & (1 << bit):
            names.append(label)
    return names


# ── Fitness Machine Feature 0x2ACC ───────────────────────────────────────────

_MACHINE_FEATURES = [
    "Average Speed", "Cadence", "Total Distance", "Inclination",
    "Elevation Gain", "Pace", "Step Count", "Resistance Level",
    "Stride Count", "Expended Energy", "Heart Rate Measurement",
    "Metabolic Equivalent", "Elapsed Time", "Remaining Time",
    "Power Measurement", "Force on Belt and Power Output",
    "User Data Retention",
]

_TARGET_FEATURES = [
    "Speed Target", "Inclination Target", "Resistance Target", "Power Target",
    "Heart Rate Target", "Targeted Expended Energy", "Targeted Step Number",
    "Targeted Stride Number", "Targeted Distance", "Targeted Training Time",
    "Targeted Time in Two HR Zones", "Targeted Time in Three HR Zones",
    "Targeted Time in Five HR Zones", "Indoor Bike Simulation Parameters",
    "Wheel Circumference", "Spin Down Control", "Targeted Cadence",
]


def _bits(value: int, names: list[str]) -> list[str]:
    return [n for i, n in enumerate(names) if value & (1 << i)]


def parse_feature(data: bytes) -> dict:
    """0x2ACC: zwei uint32. Das zweite entscheidet, was steuerbar ist."""
    if len(data) < 8:
        raise FtmsParseError(f"0x2ACC braucht 8 Byte, hat {len(data)}")
    machine = _uint(data, 0, 4)
    target = _uint(data, 4, 4)
    return {
        "machine_raw": machine,
        "target_raw": target,
        "machine": _bits(machine, _MACHINE_FEATURES),
        "target": _bits(target, _TARGET_FEATURES),
        "supports_power_target": bool(target & (1 << 3)),
        "supports_resistance_target": bool(target & (1 << 2)),
        "supports_hr_target": bool(target & (1 << 4)),
        "supports_sim": bool(target & (1 << 13)),
    }


def parse_resistance_range(data: bytes) -> dict:
    """0x2AD6: min/max sint16, Inkrement uint16 — laut Spec in 0,1er-Schritten.

    Genau hier entscheidet sich, ob 0x04 den Parameter als uint8 (Stufe) oder
    als sint16 (0,1er) erwartet. Deshalb stehen beide Lesarten im Ergebnis.
    """
    if len(data) < 6:
        raise FtmsParseError(f"0x2AD6 braucht 6 Byte, hat {len(data)}")
    lo, hi, inc = _sint(data, 0, 2), _sint(data, 2, 2), _uint(data, 4, 2)
    return {
        "raw": [lo, hi, inc],
        "as_spec_0p1": {"min": lo / 10.0, "max": hi / 10.0, "increment": inc / 10.0},
        "as_levels": {"min": lo, "max": hi, "increment": inc},
        # Ein Maximum von 41 passt zu den 41 Stufen des Varon als ganze Zahlen,
        # ein Maximum von 410 spricht fuer die 0,1er-Lesart.
        "likely_unit": "level" if hi <= 100 else "0.1-level",
    }


def parse_power_range(data: bytes) -> dict:
    """0x2AD8: min/max sint16 in Watt, Inkrement uint16 in Watt."""
    if len(data) < 6:
        raise FtmsParseError(f"0x2AD8 braucht 6 Byte, hat {len(data)}")
    return {
        "raw": [_sint(data, 0, 2), _sint(data, 2, 2), _uint(data, 4, 2)],
        "min_w": _sint(data, 0, 2),
        "max_w": _sint(data, 2, 2),
        "increment_w": _uint(data, 4, 2),
    }


# ── Control Point 0x2AD9 ─────────────────────────────────────────────────────

OPCODES = {
    0x00: "Request Control",
    0x01: "Reset",
    0x04: "Set Target Resistance Level",
    0x05: "Set Target Power",
    0x07: "Start or Resume",
    0x08: "Stop or Pause",
    0x11: "Set Indoor Bike Simulation Parameters",
    0x80: "Response Code",
}

RESULTS = {
    0x01: "Success",
    0x02: "Op Code not supported",
    0x03: "Invalid Parameter",
    0x04: "Operation Failed",
    0x05: "Control Not Permitted",
}


def parse_control_response(data: bytes) -> dict:
    if len(data) < 3 or data[0] != 0x80:
        return {"ok": False, "note": "kein 0x80-Antwortrahmen", "hex": data.hex().upper()}
    return {
        "ok": data[2] == 0x01,
        "opcode": data[1],
        "opcode_name": OPCODES.get(data[1], "unbekannt"),
        "result": data[2],
        "result_name": RESULTS.get(data[2], "unbekannt"),
        "hex": data.hex().upper(),
    }


def request_control() -> bytes:
    return bytes([0x00])


def reset() -> bytes:
    return bytes([0x01])


def start_resume() -> bytes:
    return bytes([0x07])


def stop() -> bytes:
    return bytes([0x08, 0x01])


def pause() -> bytes:
    return bytes([0x08, 0x02])


def set_target_power(watt: int) -> bytes:
    return bytes([0x05]) + int(watt).to_bytes(2, "little", signed=True)


def set_target_level(level: float, wide: bool = False) -> bytes:
    """wide=False -> uint8 Stufe, wide=True -> sint16 in 0,1er-Schritten."""
    if wide:
        return bytes([0x04]) + int(round(level * 10)).to_bytes(2, "little", signed=True)
    return bytes([0x04, int(level) & 0xFF])


def hexs(data: bytes) -> str:
    return " ".join(f"{b:02X}" for b in data)


# ── Selbsttest ───────────────────────────────────────────────────────────────

def _selftest() -> int:
    fails = []

    def check(name, got, want):
        if got != want:
            fails.append(f"{name}: {got!r} != {want!r}")

    # 1) Die invertierte Bit-0-Falle. Flags 0x0000 heisst: Speed IST da.
    pkt = bytes.fromhex("0000") + (2500).to_bytes(2, "little")
    r = parse_indoor_bike_data(pkt)
    check("bit0 clear -> speed da", r.get("speed"), 25.0)
    check("bit0 clear consumed", r["_consumed"], 4)

    # Bit 0 gesetzt heisst: kein Speed-Feld. Dasselbe Paket, andere Bedeutung.
    pkt = bytes.fromhex("0100") + (90).to_bytes(2, "little")
    r = parse_indoor_bike_data(pkt)
    check("bit0 set -> kein speed", "speed" in r, False)
    check("bit0 set -> nichts konsumiert", r["_consumed"], 2)
    check("bit0 set -> 2 Byte uebrig", r["_trailing"], 2)

    # 2) uint24-Distanz darf die Folgefelder nicht verschieben.
    #    Flags: bit0=1 (kein Speed), bit2 Kadenz, bit4 Distanz, bit6 Power
    flags = (1 << 0) | (1 << 2) | (1 << 4) | (1 << 6)
    pkt = (flags.to_bytes(2, "little")
           + (170).to_bytes(2, "little")            # Kadenz 85 rpm
           + (100000).to_bytes(3, "little")         # 100 km
           + (214).to_bytes(2, "little", signed=True))
    r = parse_indoor_bike_data(pkt)
    check("kadenz halbiert", r["cadence"], 85.0)
    check("distanz uint24", r["distance"], 100000)
    check("power nach uint24", r["power"], 214)
    check("nichts uebrig", r["_trailing"], 0)

    # Derselbe Puffer mit uint32 gelesen wuerde die Power zerreissen — hier
    # nur als Nachweis, dass die Laenge exakt passt.
    check("laenge exakt", len(pkt), 2 + 2 + 3 + 2)

    # 3) Volles Paket mit allen Feldern: 30 Byte. Das ist das Maximum und
    #    passt damit in die 32 Byte, die ein Logeintrag der Sonde traegt.
    flags = 0x1FFE  # bit0=0 (Speed da) plus bit1..bit12
    pkt = (flags.to_bytes(2, "little")
           + (3000).to_bytes(2, "little")           # speed 30,00
           + (2800).to_bytes(2, "little")           # avg speed
           + (180).to_bytes(2, "little")            # cadence 90
           + (176).to_bytes(2, "little")            # avg cadence 88
           + (8412).to_bytes(3, "little")           # distance
           + (18).to_bytes(2, "little", signed=True)
           + (214).to_bytes(2, "little", signed=True)
           + (198).to_bytes(2, "little", signed=True)
           + (312).to_bytes(2, "little")            # energy total
           + (640).to_bytes(2, "little")            # energy/h
           + bytes([11])                            # energy/min
           + bytes([148])                           # hr
           + bytes([95])                            # met 9,5
           + (1451).to_bytes(2, "little")           # elapsed
           + (749).to_bytes(2, "little"))           # remaining
    r = parse_indoor_bike_data(pkt)
    check("volles paket laenge", len(pkt), MAX_IBD_LEN)
    check("volles paket konsumiert", r["_consumed"], MAX_IBD_LEN)
    check("volles paket power", r["power"], 214)
    check("volles paket hr", r["heart_rate"], 148)
    check("volles paket met", r["met"], 9.5)
    check("volles paket energie", r["energy_total"], 312)
    check("volles paket restzeit", r["remaining_s"], 749)

    # 4) Zu kurzes Paket muss auffallen, nicht stillschweigend Muell liefern.
    try:
        parse_indoor_bike_data(bytes.fromhex("4000") + b"\x01")
        fails.append("zu kurzes Paket wurde akzeptiert")
    except FtmsParseError:
        pass

    # 5) Feature-Bits
    f = parse_feature(bytes.fromhex("0680000008000000"))
    check("feature power target", f["supports_power_target"], True)
    check("feature resistance target", f["supports_resistance_target"], False)

    # 6) Control-Point-Kommandos aus BLE-SCAN.md Schritt 4
    check("100 W", set_target_power(100).hex().upper(), "056400")
    check("Stufe 10 schmal", set_target_level(10).hex().upper(), "040A")
    check("Stufe 10 breit", set_target_level(10, wide=True).hex().upper(), "046400")
    check("Stop", stop().hex().upper(), "0801")
    r = parse_control_response(bytes.fromhex("800001"))
    check("Antwort ok", r["ok"], True)
    check("Antwort name", r["opcode_name"], "Request Control")
    r = parse_control_response(bytes.fromhex("800004"))
    check("Control not permitted", r["result_name"], "Operation Failed")

    # 7) Widerstandsbereich: beide Lesarten
    r = parse_resistance_range(bytes.fromhex("010029000100"))
    check("range likely level", r["likely_unit"], "level")
    check("range max als stufe", r["as_levels"]["max"], 41)
    r = parse_resistance_range(bytes.fromhex("0A009A010A00"))
    check("range likely 0.1", r["likely_unit"], "0.1-level")

    if fails:
        print("FEHLGESCHLAGEN:")
        for f in fails:
            print("  -", f)
        return 1
    print("ftms.py Selbsttest: alles gruen")
    return 0


if __name__ == "__main__":
    sys.exit(_selftest())
