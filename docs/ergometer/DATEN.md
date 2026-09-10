# Welche Daten liefert das Bike (und die Sonde)?

Kurzfassung für `esp32.ergo` und Tester. Messungen: Hammer Varon XTR II /
BLE-Name **TC174**, Labor 2026-09-10. Rohbericht:
[`ERGEBNISBERICHT.md`](ERGEBNISBERICHT.md).

---

## 1. Drei Arten von Daten

| Art | Wann | Wo | Zweck |
|-----|------|----|--------|
| **Profil (statisch)** | einmal nach Connect | `2ACC`, `2AD6`, optional `2AD8` | Was kann / darf die Maschine? |
| **Live-Stream** | ~1 Hz beim Treten | Notify `0x2AD2` | Aktuelle Fahrtwerte |
| **Steuer-Antworten** | nach jedem Write | Notify `0x2AD9` | Success / Fehler zum Opcode |

Die Sonde legt Rohbytes ins Log (`/api/probe/log` bzw. `export`) und dekodiert
den Live-Stream für UI/Summary. Volle Codec-Logik: `tools/ftms.py`.

---

## 2. Profil — einmal lesen, merken

Nach Connect liest die Sonde das automatisch in `/api/probe/summary`:

### Fitness Machine Feature `0x2ACC` = `A646000004200000`

| Bereich | Was das für uns heißt |
|---------|------------------------|
| Machine Features | Bike misst u. a. **Kadenz, Distanz, Leistung, Energie, HR** |
| Target Setting | Steuerbar: **Widerstand** und **Simulation** — **nicht** Watt-Ziel |

Konkret für Ergo v0.1:

- `supports_resistance_target` = **ja**
- `supports_sim` = **ja** (noch ungetestet)
- `supports_power_target` = **nein** (Bit fehlt; `0x2AD8` fehlt ebenfalls)

### Widerstands-Bereich `0x2AD6` = `0A00A0000A00`

| | Roh | Bedeutung (0,1-Stufen) |
|--|-----|------------------------|
| min | 10 | **1,0** |
| max | 160 | **16,0** |
| step | 10 | **1,0** |

Steuerkommando: `04 <lo> <hi>` als **sint16 little-endian in Zehntelstufen**.  
Beispiel Stufe **10,0** → `04 64 00`.

### Power-Range `0x2AD8`

**Fehlt.** Keine FTMS-Wattgrenzen vom Gerät.

### Geräte-Info (DIS)

| UUID | Wert |
|------|------|
| Name `2A00` | `TC174` |
| Manufacturer `2A29` | `ASR` |
| Firmware `2A26` | `6.1.2` |
| Software `2A28` | `6.3.0` |

Handelsname „Hammer / Varon“ kommt **nicht** im BLE vor.

---

## 3. Live-Stream — Indoor Bike Data `0x2AD2`

### Was wir wirklich bekommen

Auf diesem Bike sind die Flags **immer** `0x0B54`. Dadurch stecken in **jedem**
Paket dieselben Felder (19 Byte):

| Feld in JSON (`/api/probe/live`) | Einheit | Rohformat | Bedeutung |
|----------------------------------|---------|-----------|-----------|
| `speed` | km/h | uint16 / 100 | Momentangeschwindigkeit |
| `cadence` | rpm | uint16 / 2 | Trittfrequenz |
| `distance` | m | uint24 | Gesamtdistanz der Session |
| `power` | W | sint16 | Momentanleistung |
| `energyTotal` | kcal | uint16 | verbrauchte Energie (gesamt) |
| `heartRate` | bpm | uint8 | HR vom **Bike-Empfänger** (ohne Gurt oft 0) |
| `elapsedS` | s | uint16 | Session-Zeit |
| `flags` / `flagsHex` | — | uint16 | Feldmaske (`0x0B54`) |
| `hex` | — | — | Rohbytes (Debug) |
| `ageMs` / `seq` | — | — | Frische / Zähler der Sonde |

Energie hat im Rohpaket noch „pro Stunde“ und „pro Minute“ — die Live-API
zeigt nur `energyTotal` (für Ergo meist ausreichend).

### Paketlayout (dieses Bike)

```
Offset  Feld
0–1     Flags = 0x0B54
2–3     Speed
4–5     Cadence
6–8     Distance (3 Byte)
9–10    Power
11–12   Energy total
13–14   Energy / hour   (in Live-JSON weggelassen)
15      Energy / min    (dito)
16      Heart Rate
17–18   Elapsed Time
```

### Beispiel (leichtes Treten)

Roh: `540B02086E00A0010017000200000000005D00`

| Feld | Wert |
|------|------|
| Speed | 20,5 km/h |
| Cadence | 55 rpm |
| Distance | 416 m |
| Power | 23 W |
| Energy | 2 kcal |
| HR | 0 |
| Elapsed | 93 s |

Mit gesetztem Widerstand (`04 64 00` = Stufe 10) stieg Power im Labor von
~25 W auf ~90–108 W bei ähnlicher Kadenz — das ist der messbare Effekt.

### Was im Live-Stream **nicht** kommt

| Fehlt | Konsequenz |
|-------|------------|
| **Resistance Level** (Flag-Bit 5 aus) | Aktuelle Stufe nicht aus `2AD2` lesbar — nur steuern und Wirkung an Power sehen |
| Average Speed/Cadence/Power | nicht gesendet |
| MET, Remaining Time | nicht gesendet |
| Polar-H9-Puls | eigener Link `0x180D` / `0x2A37`, nicht das Bike-HR-Feld |

Zusätzlich existiert oft ein paralleler Stream `0x2ACE` (Cross Trainer) — für
Ergo **redundant**; Bridge nur `2AD2` abonnieren.

---

## 4. Steuerkanal — Control Point `0x2AD9`

Kein periodischer Stream, sondern **Antwort auf Writes**:

| Richtung | Inhalt |
|----------|--------|
| Write | Opcode + Parameter (z. B. `00`, `07`, `04 64 00`, `08 01`) |
| Notify (nicht Indicate!) | `80 <opcode> <result>` — `01` = Success |

Wichtige Opcodes für Ergo:

| Hex | Bedeutung |
|-----|-----------|
| `00` | Request Control |
| `07` | Start/Resume |
| `04 xx xx` | Set Target Resistance (sint16, 0,1) |
| `08 01` | Stop/Pause |
| `05 xx xx` | Set Target Power — Bit sagt nein; Wirkung unklar |

---

## 5. So holt man die Daten ab

| Abruf | Inhalt |
|-------|--------|
| `GET /api/probe/summary` | Profil-Cache + letztes Live-Sample + Link/Disconnect |
| `GET /api/probe/live` | nur dekodiertes `2AD2` + Link-Status |
| `GET /api/probe/export` | Summary-Zeile + NDJSON-Log (Rohbytes) |
| UI-Tab **Live** | Power / Cadence / Speed / HR live |

Beispiel Summary (Auszug):

```json
{
  "verdict": "ftms-linked",
  "featureHex": "A646000004200000",
  "resistanceRangeHex": "0A00A0000A00",
  "powerRangeMissing": true,
  "deviceName": "TC174",
  "live": {
    "valid": true,
    "speed": 20.5,
    "cadence": 55,
    "power": 23,
    "distance": 416,
    "heartRate": 0,
    "elapsedS": 93,
    "flagsHex": "0x0B54"
  }
}
```

CLI:

```bash
./tools/probe-run.py --host 192.168.178.88 summary
./tools/probe-run.py --host 192.168.178.88 live
./tools/probe-run.py --host 192.168.178.88 export
```

---

## 6. Was Ergo daraus machen soll

1. **Lesen:** `2AD2` → Speed, Cadence, Power, Distance, Energy, Elapsed (+ HR optional).  
2. **Steuern:** Resistance Target über `2AD9` (Notify-Abo vorher).  
3. **Nicht erwarten:** Watt-ERG aus Feature-Bits; aktuelle Widerstandsstufe im Notify.  
4. **Fixtures:** `scan-20260910/bike-data.jsonl` + `tools/ftms.py` als Codec-Referenz.
