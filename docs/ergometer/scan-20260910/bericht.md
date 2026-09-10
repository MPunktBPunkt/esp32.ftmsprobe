# BLE-Scan Varon XTR II — 2026-09-10 20:35

Aufgenommen mit `esp32.ftmsprobe` auf http://192.168.178.88, gesteuert von `tools/probe-run.py`. Rohbytes liegen als JSONL im selben Ordner und sind damit direkt als Fixtures fuer die `FtmsCodec`-Tests brauchbar.

## Die sechs Punkte

### 1. Advertising-Name und beworbene Services
- Name: `(nicht im Scan)`
- MAC: `c2:32:a5:1e:bf:b5` (Adresstyp None)
- RSSI: None dBm, None Pakete gesehen
- Beworbene Services: keine

### 2. Ist 0x1826 (FTMS) vorhanden?
- Im GATT: **ja**
- Control Point 0x2AD9: ja
- Indoor Bike Data 0x2AD2: ja
- Vendor-Services: 0
- Maschinenbefund: `ftms-mit-controlpoint`

### 3. Bytes von 0x2ACC, 0x2AD6, 0x2AD8

| UUID | Bedeutung | Rohbytes | Auswertung |
|---|---|---|---|
| `2ACC` | Fitness Machine Feature | `A646000004200000` | steuerbar: Resistance Target, Indoor Bike Simulation Parameters |
| `2AD6` | Supported Resistance Level Range | `0A00A0000A00` | roh [10, 160, 10], Lesart 0.1-level (Stufen 10..160) |
| `2AD8` | Supported Power Range | `-` | characteristic 2AD8 nicht gefunden |
| `2A00` | Device Name | `5443313734` | TC174 |
| `2A29` | Manufacturer Name | `415352` | ASR |
| `2A24` | Model Number | `424C452D312E30` | BLE-1.0 |
| `2A26` | Firmware Revision | `362E312E32` | 6.1.2 |


### 4. Notify-Pakete von 0x2AD2
- 274 Pakete geparst, Felder: cadence, distance, elapsed_s, energy_per_hour, energy_per_min, energy_total, heart_rate, power, speed
- Flags 0x0B54 (Instantaneous Speed, Instantaneous Cadence, Total Distance (uint24), Instantaneous Power, Expended Energy, Heart Rate, Elapsed Time)
- Ruhe: `23 Pakete  power 25.3W (24.0..26.0)  cadence 60.3rpm (58.0..62.0)  speed 22.5km/h (21.6..23.1)`
- Treten: `77 Pakete  power 25.0W (10.0..26.0)  cadence 59.4rpm (25.0..62.0)  speed 22.1km/h (9.3..23.1)`
- Beispielpaket: `540BDE087A002A09001A000B00000000519A01`
- Beispielpaket: `540BDE087A002D09001A000B00000000519A01`
- Beispielpaket: `540BDE087A003009001A000B00000000519B01`
- Vollstaendige Rohbytes: `bike-data.jsonl`

### 5. Antwort auf 0x00 am Control Point
- Gesendet: `00`
- Antwort: `800001` → Success
- Steuerung freigegeben: **ja**

### 6. Zieht der Widerstand an?

| Kommando | gesendet | Antwort | Power vor→nach | resistance vor→nach | gefuehlt | Befund |
|---|---|---|---|---|---|---|
| Set Target Power 100 W | `056400` | Success | 23.7 → 0.0 | - | - | wirkt — Power -23.7 W |
| Set Target Resistance 10 (uint8) | `040A` | Success | 10.6 → 24.3 | - | - | wirkt — Power +13.7 W |
| Set Target Resistance 10,0 (sint16, 0,1er) | `046400` | Success | 24.9 → 88.9 | - | - | wirkt — Power +64.0 W |

Die Deltas stammen aus je 12 s Mittelwert vor und nach dem Write, nicht aus dem Gefuehl im Bein.

## Ausgang

**Ausgang 1: Standard-FTMS mit offenem Control Point.** Die Annahme des Pflichtenhefts traegt. esp32.ergo v0.1 kann wie geplant gebaut werden; die Sonden-Firmware wandert als `BleCentral` plus `FtmsClient` weiter.

## Randbedingungen des Laufs

- Nur ein Central gleichzeitig: nRF Connect, MyWhoosh und Kinomap waren zu.
- Das Konsolendisplay des Bikes ist waehrend der Verbindung aus.
- Alle Control-Writes liefen durch den Limiter der Firmware (Whitelist 00/01/04/05/07/08, Watt- und Stufenklemme, Deadman).

## Dateien

- `adv.json` — Advertising aller gesehenen Geraete
- `gatt.json` — vollstaendiger Attributbaum
- `reads.json` — statische Reads mit Auswertung
- `bike-data.jsonl` — Rohbytes 0x2AD2 mit Phasenmarke
- `controlpoint.jsonl` — Rohbytes 0x2AD9 und 0x2ADA
- `probe-log.jsonl` — alles, in Reihenfolge
