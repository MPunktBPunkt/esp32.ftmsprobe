# esp32.ftmsprobe

Fernsteuerbarer GATT-Explorer am Ergometer. Die Sonde funkt, die Cursor-CLI im
Debian-LXC fuehrt Protokoll: sie faehrt die vier Schritte aus der BLE-Scan-
Checkliste per HTTP ab und legt Rohbytes plus ausgefuellten Bericht in
`docs/ergometer/scan-<datum>/`.

**Laborergebnis Hammer Varon XTR II (2026-09-10):**
[docs/ergometer/ERGEBNISBERICHT.md](docs/ergometer/ERGEBNISBERICHT.md) —
Ausgang 1 (Standard-FTMS, Control offen). Rohdaten unter
[docs/ergometer/scan-20260910/](docs/ergometer/scan-20260910/).
Erweiterte Tests & Abruf: [docs/ergometer/TESTS.md](docs/ergometer/TESTS.md).

Kein Wegwurf-Code. Die Firmware ist die erste Iteration der BLE-Schicht von
`esp32.ergo`; `BleProbe` wird dort zu `BleCentral` plus `FtmsClient`, und
`tools/ftms.py` ist die ausfuehrbare Referenz fuer `FtmsCodec`.

## Warum eine Sonde und nicht das Host-Bluetooth

Bluetooth ist im Linux-Kernel nicht namespace-faehig: ein HCI-Adapter ist aus
dem Container heraus nicht erreichbar, auch nicht per Device-Passthrough. Die
Auswege (BlueZ auf dem PVE-Host plus D-Bus-Proxy in den Container, oder eine VM
mit USB-Passthrough) sind invasiv und legen die Antenne an den Standort des
ThinkCentre statt an das Bike. Die Sonde umgeht das und laeuft auf genau dem
Funkstack, der spaeter tragen muss.

Ob ein unabhaengiger Gegencheck per `bleak` auf dem Host ueberhaupt moeglich
ist, klaert `tools/ble-host-precheck.sh` — auf dem PVE-Host, nicht im LXC.

```
CLI (LXC) ──HTTP──> ESP32-Sonde ──BLE──> Varon XTR II
    │                    ▲    └─────BLE──> Polar H9
    └──HTTP──> ESP-Hub ──┘ (otaUrl im Heartbeat)
```

## Erstflash

Einmal Kabel, danach nie wieder:

```bash
pio run -e probe -t upload          # lokal am USB
# oder ueber das Hub-UI: Reiter Geraete -> Flash (esptool installiert sich selbst)
```

Beim ersten Start oeffnet WiFiManager den Access Point `ESP-Probe-Setup`. Dort
WLAN, Geraetename und Hub-IP setzen. Die Felder liegen im NVS-Namespace
`esphub` — ein Firmware-Wechsel zwischen `heartrate` und Sonde auf demselben
Chip behaelt sie.

## Deployment ohne Kabel

```bash
tools/deploy.sh --ota 192.168.178.55                        # direkt an die Sonde
tools/deploy.sh --hub 192.168.178.113:8093 --mac A0B7651C2D3E   # ueber den Hub
tools/deploy.sh --env probe-s3 --build-only
```

Der direkte Weg lehnt ab, solange die Sonde offene BLE-Links hat — mitten in
einer Messung zu flashen kostet den Lauf. Der Hub-Weg legt die Bin nach dem
Familienschema `ftmsprobe.<version>.<family>.bin` in die Firmware-Ablage
(`POST /api/firmware-upload`) und plant das Update (`POST /api/ota-push`); der
naechste Heartbeat holt sie.

## Der Scan

```bash
chmod +x tools/*.sh tools/probe-run.py     # einmalig, Windows liefert kein x-Bit
tools/probe-run.py --host 192.168.178.55 all
```

Das faehrt Schritt 1 bis 4 in einem Durchgang und fragt an zwei Stellen nach,
weil sich Treten nicht automatisieren laesst: ohne Kadenz sendet `0x2AD2`
Nullen. Einzelschritte gehen auch:

| Aufruf | was passiert |
|---|---|
| `scan` | 15 s scannen, `adv.json`, Kandidat waehlen |
| `gatt` | verbinden, vollstaendiger Attributbaum nach `gatt.json` |
| `read` | `0x2ACC`, `0x2AD6`, `0x2AD8` plus Geraeteinfos, ausgewertet |
| `bikedata` | Notify auf `0x2AD2`, Ruhe- gegen Trittphase |
| `control` | Schritt 4 samt Wirkungsmessung beider Widerstandsvarianten |
| `dual` | Bike und Gurt parallel, Verbindungsbudget aus §14 |
| `crash` | Neustart unter Last, ohne Stop-Kommando |
| `report` | Bericht aus den vorhandenen Dateien |
| `panic` | `08 01`, alles trennen |

Zwei Randbedingungen bleiben bestehen, die kein Skript abnehmen kann: nur ein
Central gleichzeitig, also nRF Connect zu und MyWhoosh/Kinomap vorher beenden;
und das Konsolendisplay des Bikes bleibt waehrend der Verbindung aus.

### Wirkung statt Bauchgefuehl

`control` ersetzt „zieht an" durch eine Messung: `resistance_level` und
`Instantaneous Power` aus `0x2AD2` je 15 s vor und nach dem Write, Mittelwerte
in den Bericht. Getestet werden beide Lesarten des Widerstands-Opcodes,
`04 0A` (uint8-Stufe) und `04 64 00` (sint16 in 0,1er-Schritten) — welche
richtig ist, entscheidet `0x2AD6`, und genau das ist die offene Frage. Die
subjektive Wahrnehmung wird zusaetzlich abgefragt, aber die Zahl ist der Beleg.

### Ergebnisdateien

`adv.json`, `gatt.json`, `reads.json`, `bike-data.jsonl`,
`controlpoint.jsonl`, `probe-log.jsonl`, `effects.json`, `ergebnis.json` und
`bericht.md`. Die JSONL-Dateien sind Zeile fuer Zeile
`{seq, ts, dir, link, phase, uuid, hex}` und damit direkt die Fixtures fuer die
`FtmsCodec`-Tests, die Abnahmekriterium 3 der v0.1 verlangt.

## Sicherheitsschicht

Sie sitzt in der Firmware, nicht im Skript: ein abgestuerztes CLI-Skript darf
kein Ergometer unter Last stehen lassen, waehrend jemand darauf sitzt.

- **Opcode-Whitelist** fuer `0x2AD9`: `00 01 04 05 07 08`. Alles andere wird
  abgelehnt, mit Begruendung im Fehlerkoerper. `0x11` (Simulation) nur nach
  ausdruecklicher Freigabe (`guardAllowSim`) und mit Steigungsklemme.
- **Klemmen**: Zielleistung auf `guardMaxWatt` (Vorgabe 150 W), Zielstufe auf
  `guardMaxLevel` (Vorgabe 12). Beide Lesarten von `0x04` werden geklemmt. Die
  Klemmen selbst sind geklemmt, damit der Limiter nicht per API aushebelbar ist.
- **Deadman**: Modus `lab` (Vorgabe) schaerft erst bei Last-Opcodes `04`/`05`/`11`;
  `safe` schaerft bei jedem Steuerkommando; `off` deaktiviert. Bleibt Keepalive
  `guardDeadmanS` Sekunden aus (Vorgabe 20), sendet die Sonde `08 01` und trennt.
- **Kein Scan waehrend Link** (Config `scanWhileLinked`), **Auto-Reconnect** auf
  gemerktes Bike (`autoReconnect`).
- **Not-Stop** per `POST /api/probe/panic` und als fester Knopf in der Web-UI —
  nicht per Config sperrbar.
- Der Hub-Watchdog ist hier standardmaessig **aus**: ein Reboot mitten in einer
  Messung wuerde den Link abreissen lassen, ohne vorher zu stoppen.

Der Crash-Test unter `POST /api/probe/crash` umgeht das alles absichtlich —
er ist die Antwort auf die Frage aus §14, was das Bike mit einem gesetzten Ziel
macht, wenn der Client wegbricht. Das Log liegt im RAM und ist danach weg, also
vorher abholen. `probe-run.py crash` macht das in der richtigen Reihenfolge.

## HTTP-API

Die Sonde kennt FTMS nur an einer Stelle: dem Control Point, weil Limiter und
Not-Stop ihn erkennen muessen. Alle Protokolllogik liegt im Runner — dadurch
sind Varianten ohne Reflash testbar.

| Methode | Pfad | Zweck |
|---|---|---|
| POST | `/api/probe/scan/start` `{clear}` | Scan starten |
| POST | `/api/probe/scan/stop` | Scan beenden |
| GET | `/api/probe/devices` | Name, MAC, Adresstyp, RSSI, beworbene UUIDs, Rohpaket |
| POST | `/api/probe/connect` `{mac, addrType?, role?}` | verbinden, Antwort enthaelt den GATT-Dump |
| POST | `/api/probe/disconnect` `{link\|mac\|all}` | trennen |
| GET | `/api/probe/gatt?link=` | Attributbaum mit Handles, Properties, Deskriptoren |
| POST | `/api/probe/read` `{link, uuid, service?}` | Read als Hex |
| POST | `/api/probe/subscribe` `{link, uuid, mode, enable}` | notify oder indicate |
| POST | `/api/probe/write` `{link, uuid, hex, awaitIndication, timeoutMs}` | Write, Antwort-Indication zugeordnet |
| GET | `/api/probe/log?since=&max=&phase=` | NDJSON, eine Zeile je Paket |
| POST | `/api/probe/log/clear` | Ring leeren, `seq` laeuft weiter |
| POST | `/api/probe/phase` `{phase}` | Phasenmarke fuer folgende Eintraege |
| POST | `/api/probe/keepalive` | Deadman zuruecksetzen |
| POST | `/api/probe/panic` | `08 01`, trennen, Scan aus |
| POST | `/api/probe/remember` `{role, mac, name}` | Bike oder Gurt merken |
| POST | `/api/probe/reconnect` | Auto-Reconnect auf gemerktes Bike |
| GET | `/api/probe/live` | Dekodiertes Indoor-Bike-Data (`0x2AD2`) |
| GET | `/api/probe/summary` | Kompakt: Link, Live, Feature-Hex, Disconnect-Grund, Hints |
| GET | `/api/probe/export` | NDJSON: Summary + Log-Ring (`since`/`max`/`phase`) |
| POST | `/api/probe/crash` `{confirm:"crash", mode}` | Crash-Test |

Dazu die Familienrouten: `/api/status`, `/api/config/get`, `/api/config/save`,
`/api/system/restart`, `/events` (SSE), `/ota-upload`.

Ergebnis-Abruf und erweiterte Tests: [`docs/ergometer/TESTS.md`](docs/ergometer/TESTS.md).

`awaitIndication` akzeptiert Notify oder Indicate auf `0x2AD9` (dieses Bike
nutzt Notify). Ohne vorheriges Subscribe kommt keine Antwort — der Aufruf sagt
das statt in einen Timeout zu laufen.

## Build

```bash
pio run -e probe          # ESP32 D1 Mini
pio run -e probe-s3       # ESP32-S3
python3 tools/ftms.py     # Selbsttest des Codecs, ohne Hardware
```

NimBLE ist auf 1.4.x gepinnt: gleiche API wie `esp32.heartrate`, damit der Code
unveraendert nach `esp32.ergo` wandern kann. `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3`
prueft das Verbindungsbudget aus §14 gleich mit; die Peripheral-Rolle bleibt
aus, weil die Sonde reiner Central ist. Fuer die FTMS-Bridge in v0.2 muessen
die beiden `ROLE_*_DISABLED`-Zeilen weg.

Die GATT-Aufrufe sind synchron: die HTTP-Antwort traegt das Ergebnis. Das
blockiert den Webserver fuer die Dauer der Operation und ist fuer ein
Laborwerkzeug die richtige Semantik — das Skript will wissen, was das Geraet
geantwortet hat. Notifies laufen davon unabhaengig in den Ringpuffer.
