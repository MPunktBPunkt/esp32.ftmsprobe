# Ergebnisbericht — Hammer Varon XTR II / BLE-FTMS-Probe

**Datum:** 2026-09-10  
**Sonde:** `esp32.ftmsprobe` v0.1.1 auf ESP32-S3 (`192.168.178.88`, MAC `68B6B329339C`)  
**Abruf:** `GET /api/probe/summary` · `GET /api/probe/live` · siehe [`TESTS.md`](TESTS.md)  
**Bike:** Hammer Varon XTR II, BLE-Name **TC174**, MAC `c2:32:a5:1e:bf:b5`  
**Ausgang:** **1 — Standard-FTMS mit offenem Control Point**  
**Rohdaten:** [`scan-20260910/`](scan-20260910/)

Dieser Bericht ist die Arbeitsgrundlage für `esp32.ergo` v0.1. Er fasst den
automatisierten Lauf (`tools/probe-run.py all`), die manuellen GATT-Reads in der
Web-UI und die Erkenntnisse aus dem Rohbyte-Log zusammen.

---

## 1. Kurzfazit

| Frage | Antwort |
|-------|---------|
| Hat das Bike FTMS (`0x1826`)? | **Ja** |
| FitShow / proprietär (`0xFFF0` o. ä.)? | **Nein** (nur schmaler Vendor-Service `0x1850`) |
| Control Point offen? | **Ja** — `00` → `80 00 01` Success |
| Widerstand steuerbar? | **Ja** — Opcode `04` als **sint16 in 0,1er-Schritten** (`04 64 00` = Stufe 10,0) → Power ~25 W → ~90–108 W |
| Watt-ERG (`05` Set Target Power)? | Feature-Bit **nein**; Kommando antwortet Success, Wirkung **nicht belegt** (Tritt war bei Test auf 0) |
| Power-Range `0x2AD8`? | **fehlt** |
| Polar H9 parallel scanbar? | **Ja** (`Polar H9 1DF6CA`) |

**Folge für `esp32.ergo`:** Parser und Bridge auf Standard-FTMS aufsetzen.
Steuerpfad v0.1 = **Resistance Target**. Simulation (`0x11`) als Option offen
halten. Klassisches ERG über Watt erst nach einem sauberen Nachtest mit
durchgehendem Treten entscheiden — oder über Widerstand + äußeren Regler
emulieren.

---

## 2. Gerätidentität

| Quelle | Wert |
|--------|------|
| Advertising-Name | `TC174` (bewirbt Service `0x1826`) |
| MAC | `c2:32:a5:1e:bf:b5` (AddrType 0 / Public im Connect) |
| RSSI (Labor) | ca. −48 … −55 dBm |
| Manufacturer (`2A29`) | `ASR` |
| Model (`2A24`) | `BLE-1.0` |
| Serial (`2A25`) | `1.0.0.0-LE` |
| Hardware (`2A27`) | `1.0.0` |
| Firmware (`2A26`) | `6.1.2` |
| Software (`2A28`) | `6.3.0` |
| Appearance (`2A01`) | `0000` (nicht gesetzt) |
| PnP ID (`2A50`) | Platzhalter `08 8888 8888 8888` |
| System ID (`2A23`) | Platzhalter `123456FFFE9ABCDE` |
| Vendor `2C01` | `app-1.3.4-20211216.0001` (Prefix `03 80`) |

Hinweis: Der Handelsname „Hammer / Varon / XTR“ erscheint **nicht** im
Advertising. Erkennung: Name `TC174`, Service `0x1826`, Hersteller ASR, oder
gemerktes MAC.

---

## 3. GATT-Übersicht

Befund der Sonde: **`ftms-mit-controlpoint`**

```
1800 Generic Access
  2A00 Device Name              R
  2A01 Appearance               R
  2AC9 (Client Supported Feat.) R   (leer)

1801 Generic Attribute
  2A05 Service Changed          I
  2B29 Client Supported Features R/W
  2B2A Database Hash            R

180A Device Information
  2A29 / 2A24 / 2A25 / 2A27 / 2A26 / 2A28 / 2A23 / 2A2A / 2A50

1850 (Vendor, 16-bit)
  2C00                          WriteNR
  2C01                          R + Notify   → app-1.3.4-…

1826 Fitness Machine Service
  2ACE Cross Trainer Data       Notify
  2AD9 Control Point            Write + Notify   ← kein Indicate!
  2AD6 Supported Resistance Range  Read
  2ACC Fitness Machine Feature  Read
  2AD2 Indoor Bike Data         Notify
  (2AD8 Supported Power Range   FEHLT)
```

### Wichtige Abweichungen von der Spec-Erwartung

1. **Control Point antwortet per Notify, nicht Indicate.**  
   CCCD = Notify (`0x0001`). Sonde und Runner müssen das akzeptieren
   (`awaitIndication` bei vorhandenem Notify-Abo).
2. **Kein `0x2AD8`.** Watt-Grenzen unbekannt / nicht über FTMS exponiert.
3. **`0x2ACE` streamt parallel zu `0x2AD2`.** Für die Bridge reicht `2AD2`;
   `2ACE` ist redundant.
4. **Indoor Bike Data enthält kein Resistance-Level-Feld** (Flag-Bit 5 aus),
   obwohl Feature „Resistance Level“ als Maschinenfähigkeit setzt. Aktuelle
   Stufe nur indirekt über Power/Kadenz beobachtbar.
5. Handles `start`/`end` der Services in NimBLE 1.4.x nicht öffentlich —
   Char-Handles reichen für die Arbeit.

---

## 4. Feature- und Bereichs-Bytes

### `0x2ACC` Fitness Machine Feature — `A646000004200000`

| Wort | Roh | Bedeutung |
|------|-----|-----------|
| Machine Features | `0x46A6` | Cadence, Total Distance, Pace, Resistance Level, Expended Energy, Heart Rate Measurement, **Power Measurement** |
| Target Setting | `0x2004` | **Resistance Target**, **Indoor Bike Simulation Parameters** |

Abgeleitet:

- `supports_power_target` = **false**
- `supports_resistance_target` = **true**
- `supports_sim` = **true**

### `0x2AD6` Supported Resistance Level Range — `0A00A0000A00`

Little-endian sint16: min=10, max=160, increment=10.

| Lesart | Interpretation |
|--------|----------------|
| Spec 0,1-Level | **min 1.0 · max 16.0 · step 1.0** ← wahrscheinlich korrekt |
| „Ganzstufen“ | min 10 · max 160 · step 10 |

Der Wirkungstest bestätigt die **0,1-Lesart**: Opcode-Payload `64 00` (= 100
dezimal) = Stufe **10,0**.

### `0x2AD8` Supported Power Range

Nicht vorhanden.

---

## 5. Indoor Bike Data (`0x2AD2`)

### Flags (konstant)

**`0x0B54`** — Felder in Paketreihenfolge:

| Bit | Feld | Einheit |
|-----|------|---------|
| 0 clear | Instantaneous Speed | 0,01 km/h |
| 2 | Instantaneous Cadence | 0,5 rpm |
| 4 | Total Distance | uint24 Meter |
| 6 | Instantaneous Power | sint16 Watt |
| 8 | Expended Energy | total /h /min |
| 9 | Heart Rate | bpm |
| 11 | Elapsed Time | Sekunden |

**Nicht gesetzt:** Average-Felder, Resistance Level, MET, Remaining Time.

### Paketlayout (19 Byte)

```
Flags(2) Speed(2) Cadence(2) Distance(3) Power(2)
EnergyTotal(2) EnergyPerHour(2) EnergyPerMin(1) HR(1) Elapsed(2)
```

### Beispielpakete (Fixtures)

| Phase | Hex | Dekodiert |
|-------|-----|-----------|
| Leichtes Treten | `540BDE087A002A09001A000B00000000519A01` | 22,7 km/h · 61 rpm · 26 W · HR 81 · Dist 2346 m · t=410 s |
| Nach `04 64 00` | `540B98087600130A006C000C0000000051C301` | 22,0 km/h · 59 rpm · **108 W** · HR 81 · Dist 2579 m |
| Stillstand | `540B000000006F090000000B0000000054A501` | Speed/Cad/Power = 0, Distanz hält |

Volle Serie: [`scan-20260910/bike-data.jsonl`](scan-20260910/bike-data.jsonl)
(~274+ Pakete im automatisierten Lauf; UI-Session zusätzlich).

### Hinweis HR

Das Bike liefert ein HR-Feld in `2AD2` (Konsolen-/5‑kHz-Empfänger). Das ist
**nicht** der Polar-H9-Link. Für Coaching parallel H9 über `0x180D`/`0x2A37`
nutzen (bereits in `esp32.heartrate`).

---

## 6. Control Point (`0x2AD9`)

### Protokollverhalten

- Properties: **Write + Notify** (Spec würde Indicate erwarten).
- Antwortformat: `80 <opcode> <result>` mit result `01` = Success.
- Reihenfolge: zuerst Notify auf `2AD9` abonnieren, dann `00` (Request Control).

### Getestete Kommandos

| # | Hex | Name | Antwort | Wirkung |
|---|-----|------|---------|---------|
| 1 | `00` | Request Control | `80 00 01` | Steuerung freigegeben |
| 2 | `07` | Start/Resume | `80 07 01` | ok |
| 3 | `05 64 00` | Set Target Power 100 W | `80 05 01` | Success; **Wirkung unklar** (Tritt bereits 0) |
| 4a | `04 0A` | Set Resistance uint8 Stufe 10 | `80 04 01` | Success; schwache/uneindeutige Delta |
| 4b | **`04 64 00`** | Set Resistance sint16 10,0 | `80 04 01` | **klar: Power +64 W** (≈25→89 W Mittel, Peak 108 W) |
| 5 | `08 01` | Stop/Pause | `80 08 01` | ok; Last bleibt kurz hoch |

Rohprotokoll: [`scan-20260910/controlpoint.jsonl`](scan-20260910/controlpoint.jsonl)  
Wirkungsmessung: [`scan-20260910/effects.json`](scan-20260910/effects.json)

### Empfohlene Steuer-API für `esp32.ergo`

```text
Request Control:  00
Start:            07
Set Resistance:   04 <lo> <hi>     # sint16 LE, Einheit 0,1 Stufe
                  Beispiel Stufe 10,0 → 04 64 00
                  Beispiel Stufe  5,0 → 04 32 00
Stop:             08 01
```

Klemmen (aus Sonde übernommen, sinnvoll fürs Bike): max Stufe 12,0 (= `04 78 00`),
Deadman mit Keepalive, Panic = `08 01` + Disconnect.

Simulation `0x11` (Feature-Bit gesetzt) wurde **nicht** getestet — Guard der
Sonde blockt sie standardmäßig (`allowSim=false`).

---

## 7. Die sechs Punkte (Checklisten-Antwort)

1. **Advertising:** `TC174`, MAC `c2:32:a5:1e:bf:b5`, Service `0x1826`.  
2. **`0x1826`:** ja, inkl. `2AD2` + `2AD9`.  
3. **Bytes:** `2ACC=A646000004200000`, `2AD6=0A00A0000A00`, `2AD8` fehlt.  
4. **Notify `2AD2`:** Flags `0x0B54`, stabile 19-Byte-Pakete, siehe JSONL.  
5. **`00` am CP:** `80 00 01`.  
6. **Widerstand:** **ja** bei `04 64 00`; Watt-`05` formal Success ohne Wirkungsbeleg.

Maschinelles Gesamtergebnis: [`scan-20260910/ergebnis.json`](scan-20260910/ergebnis.json)  
Kurzbericht des Runners: [`scan-20260910/bericht.md`](scan-20260910/bericht.md)

---

## 8. Randbedingungen & Fallstricke

| Thema | Beobachtung | Konsequenz |
|-------|-------------|------------|
| Ein Central | Bike akzeptiert nur eine Verbindung | MyWhoosh/Kinomap/nRF zu |
| Konsolendisplay | Aus während BLE-Link | Normal |
| Deadman der Sonde | UI-Write auf `2AD9` arm't Deadman ohne Keepalive → nach 20 s Stop+Disconnect | Runner hält Keepalive; UI-CP nur mit Keepalive oder Deadman aus |
| CCCD Write-Response | Manche Subscribe-Calls scheitern mit `response=true` | Firmware fällt auf `response=false` zurück |
| Scan während Link | Bike advertised oft nicht verbunden | Connect per gemerkter MAC / `--mac` |
| Random vs Public | Connect mit AddrType 0 funktionierte | AddrType im Remember speichern |
| Log-Ring | 768 Einträge, Drop bei Dauerstream+Scan | Für Fixtures JSONL vom Runner nutzen, nicht nur UI-Ring |
| Power-Test-Artefakt | Vor `05 64 00` bereits Cadence/Power=0 | Watt-ERG nachtesten |

---

## 9. Architektur-Empfehlungen für `esp32.ergo`

### Übernehmen aus der Sonde

| Modul jetzt | Ziel in ergo |
|-------------|--------------|
| `BleProbe` (Central, multi-link) | `BleCentral` |
| `tools/ftms.py` | `FtmsCodec` (C++) — gleiche Fixtures |
| `ProbeGuard` | Steuer-Limiter + Deadman + Panic |
| Hub-Heartbeat / OTA / Config NVS `esphub` | Familienstandard beibehalten |

### v0.1 Mindestumfang

1. Connect + GATT-Cache FTMS.  
2. Notify `2AD2` → Live-Dashboard (Power, Cadence, Speed, Distance, HR, Time).  
3. Control: `00` → `07` → Resistance `04` sint16/0,1 → `08 01`.  
4. Optional: Simulation `0x11` hinter Feature-Flag.  
5. Dual-Link-Budget: Bike + Polar H9 (`MAX_CONNECTIONS≥2`, besser 3).

### v0.2 / offen

- Watt-ERG: Nachtest `05`; falls wirkungslos → Resistance-Regler (PID auf gemessene Power) oder Simulation.  
- Ob `2ACE` irgendwann Mehrwert hat (Cross-Trainer-Felder).  
- Vendor `1850`/`2C00`/`2C01` nur anfassen, wenn Standard-FTMS nicht reicht.  
- FTMS-Bridge (Peripheral-Rolle für Zwift o. ä.): NimBLE
  `ROLE_PERIPHERAL_DISABLED` / `BROADCASTER_DISABLED` in der Sonde wieder
  einschalten.

### Erkennungsheuristik

```
ftms advertised OR name == "TC174" OR manufacturer == "ASR"
→ Kandidat Varon/Hammer-Klasse
```

---

## 10. Sonde — technische Fixes während des Scans

In diesem Repo (gegenüber dem Erst-Commit) nötig geworden:

1. **`BleProbe`:** Service-`getStartHandle`/`getEndHandle` entfernt (NimBLE 1.4 privat).  
2. **`BleProbe`:** `awaitIndication` akzeptiert Notify-Abo auf `2AD9`.  
3. **`BleProbe`:** Subscribe-Fallback ohne Write-Response.  
4. **`probe-run.py`:** Indicate→Notify-Fallback; `--mac` auch ohne Scan-Treffer.  
5. Tools: CRLF entfernt (Windows-Checkout).

---

## 11. Dateiindex `scan-20260910/`

| Datei | Inhalt |
|-------|--------|
| `ergebnis.json` | Strukturiertes Gesamtergebnis inkl. Effects |
| `effects.json` | Vorher/Nachher-Fenster der drei Steuerkommandos |
| `bericht.md` | Runner-Kurzbericht (6 Punkte) |
| `gatt.json` | Vollständiger Attributbaum beim Connect |
| `reads.json` | Feature/Range/Device-Info-Reads |
| `adv.json` | Scan-Snapshot (Bike war während Link oft unsichtbar) |
| `bike-data.jsonl` | `2AD2`-Rohbytes mit Phase |
| `controlpoint.jsonl` | `2AD9`-Writes/Notifies/Responses |
| `probe-log.jsonl` | Gesamtes Sonden-Log des Laufs |
| `runner.log` | Konsolenausgabe `probe-run.py` |
| `state.json` | Runner-Zwischenstand |

Selbsttest Codec: `python3 tools/ftms.py`

---

## 12. Nächste konkrete Schritte

1. **Watt-Nachtest:** `probe-run.py control --watt 100` bei gleichmäßigem Treten (kein Stoppen vor dem Write).  
2. **Simulation-Smoke:** Guard `allowSim` kurz freigeben, `0x11` mit kleiner Steigung.  
3. **Dual:** `probe-run.py dual --hr-mac <H9>` (H9 war `…1DF6CA`).  
4. **`esp32.ergo` Repo anlegen:** `FtmsCodec` aus `ftms.py` + Fixtures aus diesem Ordner.  
5. Geräte-Profil: `TC174` / ASR / Resistance 1.0–16.0 / CP-Notify-Modus in Config hinterlegen.

---

*Erzeugt aus dem Laborlauf 2026-09-10 mit `esp32.ftmsprobe` und manueller
GATT-Verifikation in der Web-UI.*
