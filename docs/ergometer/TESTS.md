# Erweiterte Tests & Ergebnis-Abruf

## Schneller Abruf für Agent / Tester

| Endpoint | Zweck |
|----------|-------|
| `GET /api/probe/summary` | Kompakter Zustand: Link, Live-IBD, Feature-Hex, Disconnect-Grund, Hints |
| `GET /api/probe/live` | Nur dekodiertes `0x2AD2` + Links |
| `GET /api/probe/export` | NDJSON-Header (Summary) + Hinweis auf Log |
| `GET /api/probe/log?since=0&max=768` | Roh-JSONL |
| `GET /api/status` | Voller Status inkl. `probe.live` und Guard |

CLI:

```bash
./tools/probe-run.py --host 192.168.178.88 summary
./tools/probe-run.py --host 192.168.178.88 live
```

Web-UI: Tab **Live** → Summary JSON / Export.

Laborbericht: [`ERGEBNISBERICHT.md`](ERGEBNISBERICHT.md)

## Empfohlene erweiterte Tests

| Test | Befehl | Warum |
|------|--------|-------|
| Watt-Nachtest | `probe-run.py watt --watt 100 --no-prompt` | Feature-Bit sagt nein; Success ohne Wirkung unklar — **durchgehend treten** |
| Resistance-Stufen | `control --level 5` und `--level 12` | Grenzen 1.0…16.0, sint16/0,1 |
| Simulation | `probe-run.py sim --grade 2` | Feature-Bit gesetzt; Guard `allowSim` nötig |
| Dual Bike+H9 | `probe-run.py dual --hr-mac …` | Verbindungsbudget §14 |
| Crash unter Last | `probe-run.py crash` | Was hält das Bike ohne Stop? |
| Suite | `probe-run.py suite --no-prompt --keep-link` | Reads + BikeData + Control + Watt (+ Sim) |

## Probe v0.1.1 Labor-Defaults

- Deadman-Modus **`lab`**: scharf erst bei Last-Opcodes `04`/`05`/`11`
- **Kein Scan während Link** (außer Config)
- **Auto-Reconnect** auf gemerktes `bikeMac`
- Live-Kachel Power/Cadence/Speed/HR
