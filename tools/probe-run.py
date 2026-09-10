#!/usr/bin/env python3
"""Faehrt die vier Schritte aus docs/ergometer/BLE-SCAN.md gegen die ESP32-Sonde ab.

Die Sonde macht das Funken, dieses Skript macht das Protokoll: es setzt
Phasenmarken, holt die Rohbytes als JSONL ab, rechnet die Wirkung eines
Steuerkommandos aus statt sie zu erfuehlen, und schreibt am Ende einen Bericht
gegen die sechs Punkte aus "Was ich brauche".

Was nicht automatisierbar ist: treten. Ohne Kadenz sendet 0x2AD2 Nullen. An den
passenden Stellen wartet das Skript auf eine Bestaetigung.

    ./probe-run.py --host 192.168.178.55 all
    ./probe-run.py --host 192.168.178.55 control --watt 100
    ./probe-run.py --host 192.168.178.55 dual --minutes 10
    ./probe-run.py --host 192.168.178.55 panic

Nur Standardbibliothek, damit im Container kein pip-Schritt noetig ist.
"""

from __future__ import annotations

import argparse
import datetime as dt
import json
import statistics
import sys
import threading
import time
import urllib.error
import urllib.request
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import ftms  # noqa: E402

# Kandidaten fuer die Geraetesuche. Der Varon XTR II meldet sich je nach
# Firmware als FS-..., iConsole oder mit dem Handelsnamen.
NAME_HINTS = ("FS-", "FTMS", "VARON", "XTR", "HAMMER", "ICONSOLE", "BIKE", "ERGO")
HR_HINTS = ("POLAR", "H9", "H10", "HRM")

UUID_BIKE_DATA = "2AD2"
UUID_CONTROL = "2AD9"
UUID_STATUS = "2ADA"
UUID_FEATURE = "2ACC"
UUID_RES_RANGE = "2AD6"
UUID_PWR_RANGE = "2AD8"


class ProbeError(RuntimeError):
    pass


def c(text: str, code: str) -> str:
    return f"\033[{code}m{text}\033[0m"


def say(msg: str) -> None:
    print(msg, flush=True)


def head(msg: str) -> None:
    print("\n" + c("== " + msg, "1"), flush=True)


def warn(msg: str) -> None:
    print(c("!! " + msg, "33"), flush=True)


def bad(msg: str) -> None:
    print(c("XX " + msg, "31"), flush=True)


def good(msg: str) -> None:
    print(c("ok " + msg, "32"), flush=True)


# ── HTTP-Klient ──────────────────────────────────────────────────────────────

class Probe:
    def __init__(self, host: str, timeout: float = 25.0):
        if not host.startswith("http"):
            host = "http://" + host
        self.base = host.rstrip("/")
        self.timeout = timeout

    def _open(self, path: str, body=None, timeout=None):
        url = self.base + path
        data = None
        headers = {}
        if body is not None:
            data = json.dumps(body).encode()
            headers["Content-Type"] = "application/json"
        req = urllib.request.Request(url, data=data, headers=headers,
                                     method="POST" if body is not None else None)
        return urllib.request.urlopen(req, timeout=timeout or self.timeout)

    def get(self, path: str) -> dict:
        try:
            with self._open(path) as r:
                return json.loads(r.read().decode() or "{}")
        except urllib.error.HTTPError as e:
            return self._err_body(e)
        except OSError as e:
            raise ProbeError(f"{path}: {e}") from e

    def get_text(self, path: str) -> str:
        try:
            with self._open(path) as r:
                return r.read().decode()
        except urllib.error.HTTPError as e:
            raise ProbeError(f"{path}: HTTP {e.code}") from e
        except OSError as e:
            raise ProbeError(f"{path}: {e}") from e

    def post(self, path: str, body: dict | None = None, timeout=None) -> dict:
        try:
            with self._open(path, body if body is not None else {}, timeout) as r:
                return json.loads(r.read().decode() or "{}")
        except urllib.error.HTTPError as e:
            return self._err_body(e)
        except OSError as e:
            raise ProbeError(f"{path}: {e}") from e

    @staticmethod
    def _err_body(e: urllib.error.HTTPError) -> dict:
        # Die Sonde legt den Grund als JSON in den Fehlerkoerper — der ist
        # wertvoller als der Statuscode (Limiter-Ablehnungen stehen dort).
        try:
            d = json.loads(e.read().decode())
            d.setdefault("ok", False)
            d["http"] = e.code
            return d
        except Exception:
            return {"ok": False, "http": e.code, "error": str(e)}

    # ── Bequemlichkeiten ────────────────────────────────────────────────
    def status(self) -> dict:
        return self.get("/api/status")

    def now_ms(self) -> int:
        return int(self.status().get("log", {}).get("nowMs", 0))

    def devices(self) -> dict:
        return self.get("/api/probe/devices")

    def scan_start(self) -> dict:
        return self.post("/api/probe/scan/start", {"clear": True})

    def scan_stop(self) -> dict:
        return self.post("/api/probe/scan/stop")

    def connect(self, mac: str) -> dict:
        return self.post("/api/probe/connect", {"mac": mac}, timeout=45)

    def disconnect_all(self) -> dict:
        return self.post("/api/probe/disconnect", {"all": True}, timeout=30)

    def read(self, uuid: str, link: int) -> dict:
        return self.post("/api/probe/read", {"link": link, "uuid": uuid})

    def subscribe(self, uuid: str, mode: str, link: int) -> dict:
        return self.post("/api/probe/subscribe",
                         {"link": link, "uuid": uuid, "mode": mode, "enable": True})

    def write(self, uuid: str, hexstr: str, link: int, await_ind=False,
              timeout_ms=3000) -> dict:
        return self.post("/api/probe/write", {
            "link": link, "uuid": uuid, "hex": hexstr,
            "awaitIndication": await_ind, "timeoutMs": timeout_ms,
        }, timeout=(timeout_ms / 1000.0) + 15)

    def phase(self, name: str) -> dict:
        return self.post("/api/probe/phase", {"phase": name})

    def keepalive(self) -> dict:
        return self.post("/api/probe/keepalive", timeout=8)

    def panic(self, reason: str = "runner") -> dict:
        return self.post("/api/probe/panic", {"reason": reason}, timeout=30)

    def log_lines(self, since: int, max_n: int = 300) -> list[dict]:
        txt = self.get_text(f"/api/probe/log?since={since}&max={max_n}")
        out = []
        for line in txt.splitlines():
            line = line.strip()
            if not line:
                continue
            try:
                out.append(json.loads(line))
            except json.JSONDecodeError:
                warn(f"unlesbare Logzeile verworfen: {line[:60]}")
        return out


class Keepalive(threading.Thread):
    """Haelt den Deadman der Firmware bei Laune, solange gesteuert wird."""

    def __init__(self, probe: Probe, interval: float = 5.0):
        super().__init__(daemon=True)
        self.probe = probe
        self.interval = interval
        self._stop = threading.Event()
        self.failures = 0

    def run(self):
        while not self._stop.wait(self.interval):
            try:
                self.probe.keepalive()
            except Exception:
                self.failures += 1

    def stop(self):
        self._stop.set()


# ── Runner ───────────────────────────────────────────────────────────────────

class Runner:
    def __init__(self, probe: Probe, outdir: Path, args):
        self.p = probe
        self.out = outdir
        self.args = args
        self.out.mkdir(parents=True, exist_ok=True)
        self.seq = 0
        self.link = None
        self.hr_link = None
        self.bike = None
        self.samples: list[tuple[int, dict]] = []
        self.parse_errors: list[str] = []
        self.results: dict = {
            "host": probe.base,
            "started": dt.datetime.now().isoformat(timespec="seconds"),
            "effects": [],
        }
        self.f_all = (self.out / "probe-log.jsonl").open("a", encoding="utf-8")
        self.f_bike = (self.out / "bike-data.jsonl").open("a", encoding="utf-8")
        self.f_cp = (self.out / "controlpoint.jsonl").open("a", encoding="utf-8")
        self._load_state()

    # ── Zustand ueber Aufrufe hinweg ────────────────────────────────────
    def _state_path(self) -> Path:
        return self.out / "state.json"

    def _load_state(self):
        p = self._state_path()
        if not p.exists():
            return
        try:
            s = json.loads(p.read_text(encoding="utf-8"))
        except Exception:
            return
        self.seq = s.get("seq", 0)
        self.bike = s.get("bike")

    def _save_state(self):
        self._state_path().write_text(json.dumps({
            "seq": self.seq, "bike": self.bike, "link": self.link,
        }, indent=2), encoding="utf-8")

    def close(self):
        for f in (self.f_all, self.f_bike, self.f_cp):
            try:
                f.close()
            except Exception:
                pass
        self._save_state()

    # ── Log abholen ─────────────────────────────────────────────────────
    def drain(self) -> int:
        """Holt neue Logzeilen, schreibt sie weg und parst 0x2AD2 mit."""
        n = 0
        while True:
            lines = self.p.log_lines(self.seq, 300)
            if not lines:
                break
            first = lines[0]["seq"]
            if first > self.seq + 1 and self.seq:
                warn(f"Luecke im Log: erwartet seq {self.seq + 1}, bekommen {first} "
                     f"— Ringpuffer war zu klein, oefter abholen")
            for e in lines:
                self.f_all.write(json.dumps(e, ensure_ascii=False) + "\n")
                uuid = e.get("uuid", "")
                if uuid == UUID_BIKE_DATA:
                    self.f_bike.write(json.dumps(e, ensure_ascii=False) + "\n")
                    if e.get("dir") in ("notify", "indicate") and e.get("hex"):
                        self._add_sample(e)
                elif uuid in (UUID_CONTROL, UUID_STATUS):
                    self.f_cp.write(json.dumps(e, ensure_ascii=False) + "\n")
                self.seq = max(self.seq, e["seq"])
                n += 1
            if len(lines) < 300:
                break
        for f in (self.f_all, self.f_bike, self.f_cp):
            f.flush()
        return n

    def _add_sample(self, entry: dict):
        try:
            data = bytes.fromhex(entry["hex"])
            parsed = ftms.parse_indoor_bike_data(data)
        except (ValueError, ftms.FtmsParseError) as e:
            msg = f"seq {entry['seq']} {entry.get('hex')}: {e}"
            if len(self.parse_errors) < 20:
                self.parse_errors.append(msg)
            return
        parsed["_phase"] = entry.get("phase", "")
        self.samples.append((entry["ts"], parsed))

    def wait(self, seconds: float, label: str = ""):
        """Wartet und holt zwischendurch das Log ab, damit der Ring nicht ueberlaeuft."""
        end = time.time() + seconds
        while time.time() < end:
            time.sleep(min(2.0, max(0.2, end - time.time())))
            self.drain()
            left = int(end - time.time())
            if label and left >= 0:
                print(f"\r    {label} … {left:3d} s ", end="", flush=True)
        if label:
            print("\r" + " " * 40 + "\r", end="", flush=True)

    def ask(self, prompt: str) -> str:
        if self.args.no_prompt:
            say(f"    (uebersprungen: {prompt})")
            return ""
        try:
            return input(c("?? " + prompt + " ", "36"))
        except EOFError:
            return ""

    # ── Auswertung ──────────────────────────────────────────────────────
    def window(self, t0: int, t1: int) -> dict:
        """Mittelwerte der 0x2AD2-Felder im Fenster [t0, t1] (Geraete-millis)."""
        vals: dict[str, list[float]] = {}
        n = 0
        for ts, s in self.samples:
            if ts < t0 or ts > t1:
                continue
            n += 1
            for k in ("power", "resistance", "cadence", "speed", "heart_rate"):
                if k in s and isinstance(s[k], (int, float)):
                    vals.setdefault(k, []).append(float(s[k]))
        out = {"samples": n, "windowMs": t1 - t0}
        for k, v in vals.items():
            out[k] = {
                "mean": round(statistics.fmean(v), 1),
                "min": round(min(v), 1),
                "max": round(max(v), 1),
            }
        return out

    # ── Schritt 1: Advertising ──────────────────────────────────────────
    def step_scan(self) -> dict:
        head("Schritt 1 — Scan")
        self.p.scan_start()
        self.wait(self.args.scan_seconds, "scanne")
        self.p.scan_stop()
        d = self.p.devices()
        self.drain()
        (self.out / "adv.json").write_text(json.dumps(d, indent=2, ensure_ascii=False),
                                           encoding="utf-8")
        devs = sorted(d.get("devices", []), key=lambda x: -x.get("rssi", -127))
        say(f"    {len(devs)} Geraete")
        for x in devs:
            svc = " ".join(s["uuid"] for s in x.get("services", []))
            flag = " [FTMS]" if x.get("ftms") else ""
            say(f"    {x['rssi']:4d} dBm  {x['mac']}  {x.get('name') or '(ohne Namen)':<22}"
                f" {svc}{flag}")

        pick = None
        if self.args.mac:
            pick = next((x for x in devs if x["mac"].lower() == self.args.mac.lower()), None)
            if not pick:
                warn(f"{self.args.mac} war im Scan nicht zu sehen — verbinde trotzdem")
                pick = {"mac": self.args.mac.lower(), "name": "(nicht im Scan)",
                        "rssi": None, "ftms": None, "services": []}
        else:
            pick = next((x for x in devs if x.get("ftms")), None)
            if not pick:
                up = [(x, (x.get("name") or "").upper()) for x in devs]
                pick = next((x for x, n in up if any(h in n for h in NAME_HINTS)), None)
        if not pick:
            raise ProbeError("kein Kandidat gefunden — mit --mac nachhelfen")
        self.bike = pick["mac"]
        good(f"Kandidat: {pick.get('name') or '(ohne Namen)'} {pick['mac']}"
             f"{' — bewirbt 0x1826' if pick.get('ftms') else ' — bewirbt kein 0x1826'}")
        self.results["adv"] = pick
        self.results["adv_all_count"] = len(devs)
        hr = next((x for x, n in [(y, (y.get("name") or "").upper()) for y in devs]
                   if any(h in n for h in HR_HINTS)), None)
        if hr:
            self.results["hr_adv"] = hr
            say(f"    Gurt gesehen: {hr.get('name')} {hr['mac']}")
        self._save_state()
        return pick

    # ── Schritt 2: GATT ─────────────────────────────────────────────────
    def step_gatt(self) -> dict:
        head("Schritt 2 — Services und Characteristics")
        mac = self.args.mac or self.bike
        if not mac:
            raise ProbeError("keine MAC bekannt — erst scan laufen lassen")
        r = self.p.connect(mac)
        if not r.get("ok"):
            raise ProbeError(f"connect fehlgeschlagen: {r.get('error')}")
        self.link = r["link"]
        gatt = r.get("gatt", {})
        (self.out / "gatt.json").write_text(json.dumps(gatt, indent=2, ensure_ascii=False),
                                            encoding="utf-8")
        self.drain()
        sm = gatt.get("summary", {})
        for sv in gatt.get("services", []):
            mark = c(" (vendor)", "33") if sv.get("vendor") else ""
            say(f"    {sv['uuid']}  {sv.get('label', '')}{mark}")
            for ch in sv.get("chars", []):
                pr = ch.get("props", {})
                flags = "".join(k[0].upper() for k in
                                ("read", "write", "notify", "indicate") if pr.get(k))
                say(f"        {ch['uuid']}  {ch.get('label', ''):<34} {flags}")
        self.results["gatt_summary"] = sm
        self.results["gatt_services"] = [s["uuid"] for s in gatt.get("services", [])]
        if sm.get("ftms"):
            good(f"0x1826 vorhanden, Control Point {'da' if sm.get('controlPoint') else 'FEHLT'}")
        else:
            warn(f"kein 0x1826 — Befund: {sm.get('verdict')}")
        self._save_state()
        return gatt

    # ── Schritt 3: statische Reads ──────────────────────────────────────
    def step_reads(self) -> dict:
        head("Schritt 3 — Feature- und Bereichs-Reads")
        if self.link is None:
            raise ProbeError("kein Link offen")
        want = [
            (UUID_FEATURE, "Fitness Machine Feature", ftms.parse_feature),
            (UUID_RES_RANGE, "Supported Resistance Level Range", ftms.parse_resistance_range),
            (UUID_PWR_RANGE, "Supported Power Range", ftms.parse_power_range),
            ("2A00", "Device Name", None),
            ("2A29", "Manufacturer Name", None),
            ("2A24", "Model Number", None),
            ("2A26", "Firmware Revision", None),
        ]
        reads = {}
        for uuid, label, parser in want:
            r = self.p.read(uuid, self.link)
            if not r.get("ok"):
                reads[uuid] = {"label": label, "error": r.get("error")}
                say(f"    {uuid}  {label:<34} — {r.get('error')}")
                continue
            hexs = r.get("hex", "")
            entry = {"label": label, "hex": hexs, "len": r.get("len")}
            data = bytes.fromhex(hexs) if hexs else b""
            if parser:
                try:
                    entry["parsed"] = parser(data)
                except ftms.FtmsParseError as e:
                    entry["parseError"] = str(e)
            else:
                try:
                    entry["text"] = data.decode("utf-8", "replace").strip("\x00")
                except Exception:
                    pass
            reads[uuid] = entry
            extra = entry.get("text") or ""
            say(f"    {uuid}  {label:<34} {hexs}  {extra}")

        f = reads.get(UUID_FEATURE, {}).get("parsed")
        if f:
            say("        steuerbar: " + (", ".join(f["target"]) or "nichts"))
            if not f["supports_power_target"] and not f["supports_resistance_target"]:
                warn("weder Power- noch Resistance-Target im Feature-Bitfeld")
        rr = reads.get(UUID_RES_RANGE, {}).get("parsed")
        if rr:
            say(f"        Widerstand roh {rr['raw']} → Lesart {rr['likely_unit']}")
        pr = reads.get(UUID_PWR_RANGE, {}).get("parsed")
        if pr:
            say(f"        Leistung {pr['min_w']}..{pr['max_w']} W, Schritt {pr['increment_w']}")
            if self.args.watt > pr["max_w"]:
                warn(f"--watt {self.args.watt} liegt ueber dem Maximum {pr['max_w']}")

        (self.out / "reads.json").write_text(json.dumps(reads, indent=2, ensure_ascii=False),
                                             encoding="utf-8")
        self.drain()
        self.results["reads"] = reads
        return reads

    # ── Schritt 4a: Indoor Bike Data ────────────────────────────────────
    def step_bikedata(self) -> dict:
        head("Schritt 4a — Indoor Bike Data (0x2AD2)")
        if self.link is None:
            raise ProbeError("kein Link offen")
        r = self.p.subscribe(UUID_BIKE_DATA, "notify", self.link)
        if not r.get("ok"):
            raise ProbeError(f"Notify auf 0x2AD2 fehlgeschlagen: {r.get('error')}")
        good("Notify auf 0x2AD2 aktiv")

        self.p.phase("idle")
        say("    Ruhephase, nicht treten.")
        self.wait(self.args.idle_seconds, "Ruhe")
        t_idle = self.p.now_ms()

        self.ask("Jetzt gleichmaessig treten und Enter druecken.")
        self.p.phase("pedaling")
        t0 = self.p.now_ms()
        self.wait(self.args.pedal_seconds, "Trittphase")
        t1 = self.p.now_ms()

        idle = self.window(t_idle - self.args.idle_seconds * 1000, t_idle)
        ped = self.window(t0, t1)
        say(f"    Ruhe:  {self._fmt_window(idle)}")
        say(f"    Treten:{self._fmt_window(ped)}")
        if ped["samples"] == 0:
            bad("keine Pakete in der Trittphase — Notify oder Kadenz fehlt")
        elif "power" not in ped:
            warn("0x2AD2 liefert kein Power-Feld")

        fields = set()
        flags_seen = set()
        for _, s in self.samples:
            flags_seen.add(s["flags"])
            fields.update(k for k in s if not k.startswith("_") and k != "flags"
                          and k != "flags_bin")
        self.results["bikedata"] = {
            "packets": len(self.samples),
            "fields": sorted(fields),
            "flags_seen": [f"0x{f:04X} ({', '.join(ftms.ibd_flag_list(f))})"
                           for f in sorted(flags_seen)],
            "idle": idle,
            "pedaling": ped,
            "parse_errors": self.parse_errors,
            "example_hex": self._example_hex(3),
        }
        say(f"    Felder: {', '.join(sorted(fields)) or 'keine'}")
        for f in self.results["bikedata"]["flags_seen"]:
            say(f"    Flags {f}")
        if self.parse_errors:
            bad(f"{len(self.parse_errors)} Pakete nicht parsebar — siehe Bericht")
        return self.results["bikedata"]

    def _example_hex(self, n: int) -> list[str]:
        out = []
        path = self.out / "bike-data.jsonl"
        if not path.exists():
            return out
        for line in path.read_text(encoding="utf-8").splitlines()[-60:]:
            try:
                e = json.loads(line)
            except Exception:
                continue
            if e.get("hex") and e.get("phase") == "pedaling":
                out.append(e["hex"])
        return out[-n:]

    @staticmethod
    def _fmt_window(w: dict) -> str:
        if not w.get("samples"):
            return " keine Pakete"
        parts = [f" {w['samples']:3d} Pakete"]
        for k, unit in (("power", "W"), ("resistance", ""), ("cadence", "rpm"),
                        ("speed", "km/h")):
            if k in w:
                parts.append(f"{k} {w[k]['mean']}{unit} ({w[k]['min']}..{w[k]['max']})")
        return "  ".join(parts)

    # ── Schritt 4b: Control Point ───────────────────────────────────────
    def step_control(self) -> dict:
        head("Schritt 4b — Control Point (0x2AD9)")
        if self.link is None:
            raise ProbeError("kein Link offen")

        # Spec: Indicate. Manche Bikes liefern die CP-Antwort nur als Notify.
        r = self.p.subscribe(UUID_CONTROL, "indicate", self.link)
        if not r.get("ok"):
            warn(f"Indicate auf 0x2AD9 nicht moeglich ({r.get('error')}) — versuche Notify")
            r = self.p.subscribe(UUID_CONTROL, "notify", self.link)
            if not r.get("ok"):
                raise ProbeError(f"Notify/Indicate auf 0x2AD9 fehlgeschlagen: {r.get('error')}")
            good("Notify auf 0x2AD9 aktiv (Bike ohne Indicate)")
        else:
            good("Indications auf 0x2AD9 aktiv")
        st = self.p.subscribe(UUID_STATUS, "notify", self.link)
        if st.get("ok"):
            good("Notify auf 0x2ADA aktiv")
        if not self.p.status().get("probe", {}).get("links"):
            raise ProbeError("Link ist weg")

        ka = Keepalive(self.p)
        ka.start()
        cp = {"commands": []}
        try:
            grant = self._cp("00", "Request Control")
            cp["request_control"] = grant
            self.results["control_granted"] = bool(grant.get("success"))
            if not grant.get("success"):
                bad("Request Control abgelehnt oder unbeantwortet — "
                    "das ist Ausgang 2 aus BLE-SCAN.md")
                cp["commands"].append(grant)
                self.results["control"] = cp
                return cp

            cp["commands"].append(self._cp("07", "Start or Resume"))

            self.ask("Fuer die Wirkungsmessung durchgehend treten. Enter, wenn du trittst.")
            self.p.phase("pedaling")

            tests = [
                (f"write-power-{self.args.watt}",
                 ftms.hexs(ftms.set_target_power(self.args.watt)),
                 f"Set Target Power {self.args.watt} W"),
                (f"write-level-{self.args.level}",
                 ftms.hexs(ftms.set_target_level(self.args.level)),
                 f"Set Target Resistance {self.args.level} (uint8)"),
                (f"write-level-{self.args.level}-wide",
                 ftms.hexs(ftms.set_target_level(self.args.level, wide=True)),
                 f"Set Target Resistance {self.args.level},0 (sint16, 0,1er)"),
            ]
            for phase, hexcmd, label in tests:
                eff = self.effect(phase, hexcmd, label)
                self.results["effects"].append(eff)
                cp["commands"].append(eff["write"])

            cp["commands"].append(self._cp("08 01", "Stop"))
        finally:
            ka.stop()
            self.p.phase("idle")
            # Aufraeumen: nichts unter Last stehen lassen
            try:
                self.p.write(UUID_CONTROL, "08 01", self.link, await_ind=True, timeout_ms=2000)
            except ProbeError:
                pass
            self.drain()

        self.results["control"] = cp
        return cp

    def step_watt(self) -> dict:
        """Sauberer Watt-Nachtest: durchgehend treten, nur 05, Wirkung messen."""
        head(f"Watt-Nachtest — Set Target Power {self.args.watt} W")
        if self.link is None:
            raise ProbeError("kein Link offen")
        r = self.p.subscribe(UUID_CONTROL, "notify", self.link)
        if not r.get("ok"):
            r = self.p.subscribe(UUID_CONTROL, "indicate", self.link)
        self.p.subscribe(UUID_BIKE_DATA, "notify", self.link)
        self.ask("Gleichmaessig treten und Enter — nicht stehen bleiben.")
        self.p.phase("pedaling")
        ka = Keepalive(self.p)
        ka.start()
        out = {}
        try:
            out["request_control"] = self._cp("00", "Request Control")
            out["start"] = self._cp("07", "Start or Resume")
            hexcmd = ftms.hexs(ftms.set_target_power(self.args.watt))
            eff = self.effect(f"watt-{self.args.watt}", hexcmd,
                              f"Set Target Power {self.args.watt} W")
            out["effect"] = eff
            self.results["effects"].append(eff)
            out["stop"] = self._cp("08 01", "Stop")
        finally:
            ka.stop()
            try:
                self.p.write(UUID_CONTROL, "08 01", self.link, await_ind=True, timeout_ms=2000)
            except ProbeError:
                pass
        self.results["watt_retest"] = out
        (self.out / "watt-retest.json").write_text(
            json.dumps(out, indent=2, ensure_ascii=False), encoding="utf-8")
        return out

    def step_sim(self) -> dict:
        """Simulation 0x11 Smoke-Test (braucht guardAllowSim=true)."""
        head(f"Simulation-Smoke — Steigung {self.args.grade} %")
        if self.link is None:
            raise ProbeError("kein Link offen")
        # Sim freischalten falls noetig
        cfg = self.p.get("/api/config/get")
        if not cfg.get("guardAllowSim"):
            warn("guardAllowSim war aus — aktiviere temporaer")
            self.p.post("/api/config/save", {"guardAllowSim": True})
        r = self.p.subscribe(UUID_CONTROL, "notify", self.link)
        if not r.get("ok"):
            self.p.subscribe(UUID_CONTROL, "indicate", self.link)
        self.p.subscribe(UUID_BIKE_DATA, "notify", self.link)
        self.ask("Treten und Enter fuer Simulationstest.")
        ka = Keepalive(self.p)
        ka.start()
        out = {}
        try:
            out["request_control"] = self._cp("00", "Request Control")
            out["start"] = self._cp("07", "Start or Resume")
            grade_cdeg = int(round(self.args.grade * 100))  # 0.01 %
            # wind=0, grade, crr=0, cw=0 — Layout siehe FTMS
            payload = bytes([
                0x11,
                0x00, 0x00,  # wind speed
                grade_cdeg & 0xFF, (grade_cdeg >> 8) & 0xFF,
                0x00, 0x00,  # crr / cw often uint8 pair in some stacks — FTMS uses
            ])
            # Spec: wind sint16 0.001 m/s, grade sint16 0.01%, crr uint8 0.0001, cw uint8 0.01
            # Full 7 bytes: op + wind(2) + grade(2) + crr(1) + cw(1)
            payload = bytes([
                0x11,
                0x00, 0x00,
                grade_cdeg & 0xFF, (grade_cdeg >> 8) & 0xFF,
                0x00, 0x00,
            ])
            hexcmd = ftms.hexs(payload)
            eff = self.effect(f"sim-grade-{self.args.grade}", hexcmd,
                              f"Set Sim grade {self.args.grade}%")
            out["effect"] = eff
            self.results["effects"].append(eff)
            out["stop"] = self._cp("08 01", "Stop")
        finally:
            ka.stop()
            try:
                self.p.write(UUID_CONTROL, "08 01", self.link, await_ind=True, timeout_ms=2000)
            except ProbeError:
                pass
        self.results["sim_test"] = out
        (self.out / "sim-test.json").write_text(
            json.dumps(out, indent=2, ensure_ascii=False), encoding="utf-8")
        return out

    def _cp(self, hexcmd: str, label: str) -> dict:
        r = self.p.write(UUID_CONTROL, hexcmd, self.link, await_ind=True, timeout_ms=3000)
        self.drain()
        resp = r.get("response", {}) or {}
        rec = {
            "label": label,
            "sent": r.get("hex", hexcmd),
            "ok": bool(r.get("ok")),
            "guard": r.get("guard", {}),
            "response_hex": resp.get("hex"),
            "result": resp.get("resultName"),
            "success": bool(resp.get("success")),
            "timeout": bool(resp.get("timeout")),
            "error": r.get("error"),
        }
        if not r.get("ok"):
            bad(f"{label}: {r.get('error')}")
        elif resp.get("timeout"):
            warn(f"{label}: gesendet, keine Antwort in {resp.get('waitedMs')} ms")
        else:
            line = f"{label}: {rec['sent']} → {resp.get('hex')} {resp.get('resultName')}"
            (good if rec["success"] else bad)(line)
        g = r.get("guard", {})
        if g.get("modified"):
            warn(f"    Limiter hat geklemmt: {g.get('reason')}")
        return rec

    def effect(self, phase: str, hexcmd: str, label: str) -> dict:
        """Vorher/nachher je 15 s mitteln — Zahl statt Bauchgefuehl."""
        secs = self.args.effect_seconds
        self.p.phase("pre-" + phase)
        self.wait(secs, f"Referenz vor {label}")
        t_pre_end = self.p.now_ms()
        pre = self.window(t_pre_end - secs * 1000, t_pre_end)

        self.p.phase(phase)
        write = self._cp(hexcmd, label)
        t_post_start = self.p.now_ms()
        self.wait(secs, f"Wirkung von {label}")
        post = self.window(t_post_start, self.p.now_ms())

        def delta(key):
            if key in pre and key in post:
                return round(post[key]["mean"] - pre[key]["mean"], 1)
            return None

        d = {k: delta(k) for k in ("power", "resistance", "cadence", "speed")}
        say(f"    vor:   {self._fmt_window(pre)}")
        say(f"    nach:  {self._fmt_window(post)}")
        say("    Delta: " + (", ".join(f"{k} {v:+}" for k, v in d.items() if v is not None)
                            or "nichts messbar"))

        felt = self.ask("Hat der Widerstand angezogen? (j/n/unklar)")
        verdict = self._verdict(write, d, felt)
        (good if verdict.startswith("wirkt") else warn)(f"Befund: {verdict}")
        return {
            "phase": phase, "label": label, "write": write,
            "pre": pre, "post": post, "delta": d,
            "felt": felt.strip().lower(), "verdict": verdict,
        }

    @staticmethod
    def _verdict(write: dict, d: dict, felt: str) -> str:
        if not write.get("ok"):
            return "nicht gesendet"
        if write.get("timeout"):
            return "unbeantwortet — Wirkung nur an den Zahlen ablesbar"
        if not write.get("success"):
            return f"abgelehnt ({write.get('result')})"
        res, pwr = d.get("resistance"), d.get("power")
        if res is not None and abs(res) >= 1:
            return f"wirkt — resistance_level {res:+}"
        if pwr is not None and abs(pwr) >= 10:
            return f"wirkt — Power {pwr:+} W"
        if felt.startswith("j"):
            return "wirkt spuerbar, aber ohne messbare Aenderung in 0x2AD2"
        return "keine messbare Wirkung"

    # ── Risiko 1: Verbindungsbudget ─────────────────────────────────────
    def step_dual(self) -> dict:
        head(f"Verbindungsbudget — Bike plus Gurt, {self.args.minutes} min")
        mac = self.args.mac or self.bike
        if not mac:
            raise ProbeError("keine Bike-MAC bekannt")
        if self.link is None:
            r = self.p.connect(mac)
            if not r.get("ok"):
                raise ProbeError(f"Bike-connect fehlgeschlagen: {r.get('error')}")
            self.link = r["link"]
        self.p.subscribe(UUID_BIKE_DATA, "notify", self.link)

        hr_mac = self.args.hr_mac or (self.results.get("hr_adv") or {}).get("mac")
        if not hr_mac:
            self.p.scan_start()
            self.wait(8, "suche Gurt")
            self.p.scan_stop()
            for x in self.p.devices().get("devices", []):
                n = (x.get("name") or "").upper()
                if x.get("hr") or any(h in n for h in HR_HINTS):
                    hr_mac = x["mac"]
                    break
        if not hr_mac:
            raise ProbeError("kein Gurt gefunden — mit --hr-mac angeben")
        r = self.p.connect(hr_mac)
        if not r.get("ok"):
            raise ProbeError(f"Gurt-connect fehlgeschlagen: {r.get('error')} "
                             f"— genau das ist die Frage aus §14")
        self.hr_link = r["link"]
        good(f"zwei Links offen: Bike {self.link}, Gurt {self.hr_link}")
        self.p.subscribe("2A37", "notify", self.hr_link)

        self.p.phase("dual-link")
        losses0 = self.p.status()["probe"]["linkLosses"]
        minute_stats = []
        for m in range(self.args.minutes):
            self.wait(60, f"Minute {m + 1}/{self.args.minutes}")
            st = self.p.status()["probe"]
            per_link = {str(l["link"]): l["notifies"] for l in st.get("links", [])}
            minute_stats.append({"minute": m + 1, "notifies": per_link,
                                 "links": st["linkCount"],
                                 "losses": st["linkLosses"] - losses0})
            say(f"    Minute {m + 1}: Links {st['linkCount']}, "
                f"Notifies {per_link}, Verluste {st['linkLosses'] - losses0}")
        st = self.p.status()["probe"]
        out = {
            "minutes": self.args.minutes,
            "bike_mac": mac, "hr_mac": hr_mac,
            "link_losses": st["linkLosses"] - losses0,
            "links_at_end": st["linkCount"],
            "per_minute": minute_stats,
            "verdict": ("zwei Links stabil" if st["linkCount"] == 2
                        and st["linkLosses"] - losses0 == 0
                        else "instabil — siehe Verluste"),
        }
        self.results["dual"] = out
        (self.out / "dual-link.json").write_text(json.dumps(out, indent=2), encoding="utf-8")
        (good if out["link_losses"] == 0 else warn)(out["verdict"])
        return out

    # ── Risiko 2: Widerstand nach Client-Crash ──────────────────────────
    def step_crash(self) -> dict:
        head("Crash-Test — was macht das Bike ohne Stop-Kommando?")
        if self.link is None:
            mac = self.args.mac or self.bike
            r = self.p.connect(mac)
            if not r.get("ok"):
                raise ProbeError(f"connect fehlgeschlagen: {r.get('error')}")
            self.link = r["link"]
        self.p.subscribe(UUID_BIKE_DATA, "notify", self.link)
        self.p.subscribe(UUID_CONTROL, "indicate", self.link)

        ka = Keepalive(self.p)
        ka.start()
        try:
            if not self._cp("00", "Request Control")["success"]:
                raise ProbeError("ohne Control-Freigabe ist der Test sinnlos")
            self._cp("07", "Start or Resume")
            self.ask("Treten und waehrend des ganzen Tests weitertreten. Enter.")
            self.p.phase("crash-load")
            self._cp(ftms.hexs(ftms.set_target_power(self.args.watt)),
                     f"Set Target Power {self.args.watt} W")
            self.wait(self.args.effect_seconds, "Last aufbauen")
            t = self.p.now_ms()
            before = self.window(t - self.args.effect_seconds * 1000, t)
            say(f"    unter Last: {self._fmt_window(before)}")
        finally:
            ka.stop()

        # Das Log liegt im RAM und ist nach dem Neustart weg.
        self.drain()
        say("    Log gesichert, loese Neustart aus (ohne 08 01, ohne Disconnect)")
        r = self.p.post("/api/probe/crash", {"confirm": "crash", "mode": "restart"})
        if not r.get("ok"):
            raise ProbeError(f"Crash-Endpunkt: {r.get('error')}")
        crash_seq = r.get("lastSeq")
        self.link = None

        say("    warte auf die Sonde ...")
        back = None
        for _ in range(40):
            time.sleep(3)
            try:
                st = self.p.status()
                if st.get("uptimeS", 9999) < 120:
                    back = st
                    break
            except ProbeError:
                print(".", end="", flush=True)
        if not back:
            raise ProbeError("Sonde ist nicht zurueckgekommen — Strom pruefen")
        good(f"Sonde wieder da, Uptime {back['uptimeS']} s")

        felt = self.ask("Was macht das Bike jetzt? (haelt Last / frei / gebremst)")
        # Nachmessen: neu verbinden und ohne Steuerkommando lesen, was 0x2AD2 sagt
        after = {}
        mac = self.args.mac or self.bike
        if mac:
            self.seq = 0  # neues Log nach dem Neustart
            r = self.p.connect(mac)
            if r.get("ok"):
                self.link = r["link"]
                self.p.subscribe(UUID_BIKE_DATA, "notify", self.link)
                self.p.phase("after-crash")
                self.samples = []
                self.wait(self.args.effect_seconds, "Zustand nach dem Crash")
                t = self.p.now_ms()
                after = self.window(t - self.args.effect_seconds * 1000, t)
                say(f"    nach dem Crash: {self._fmt_window(after)}")
            else:
                warn(f"Reconnect fehlgeschlagen: {r.get('error')} "
                     f"— haelt das Bike den alten Link fest?")
                after = {"reconnectError": r.get("error")}

        out = {"before": before, "after": after, "felt": felt.strip().lower(),
               "crash_at_seq": crash_seq, "reconnect_ok": self.link is not None}
        self.results["crash"] = out
        (self.out / "crash-test.json").write_text(json.dumps(out, indent=2), encoding="utf-8")
        return out

    # ── Bericht ─────────────────────────────────────────────────────────
    def report(self) -> Path:
        head("Bericht")
        r = self.results
        adv = r.get("adv", {})
        sm = r.get("gatt_summary", {})
        reads = r.get("reads", {})
        bd = r.get("bikedata", {})
        cp = r.get("control", {})
        L = []
        A = L.append

        A(f"# BLE-Scan Varon XTR II — {dt.datetime.now():%Y-%m-%d %H:%M}")
        A("")
        A(f"Aufgenommen mit `esp32.ftmsprobe` auf {r['host']}, gesteuert von "
          f"`tools/probe-run.py`. Rohbytes liegen als JSONL im selben Ordner und "
          f"sind damit direkt als Fixtures fuer die `FtmsCodec`-Tests brauchbar.")
        A("")
        A("## Die sechs Punkte")
        A("")
        A("### 1. Advertising-Name und beworbene Services")
        if adv:
            svc = ", ".join(f"`{s['uuid']}`" + (f" ({s['label']})" if s.get("label") else "")
                            for s in adv.get("services", [])) or "keine"
            A(f"- Name: `{adv.get('name') or '(ohne Namen)'}`")
            A(f"- MAC: `{adv['mac']}` (Adresstyp {adv.get('addrType')})")
            A(f"- RSSI: {adv.get('rssi')} dBm, {adv.get('seen')} Pakete gesehen")
            A(f"- Beworbene Services: {svc}")
            if adv.get("payload"):
                A(f"- Rohes Advertising-Paket: `{adv['payload']}`")
        else:
            A("- kein Scan gelaufen")
        A("")
        A("### 2. Ist 0x1826 (FTMS) vorhanden?")
        if sm:
            A(f"- Im GATT: **{'ja' if sm.get('ftms') else 'nein'}**")
            A(f"- Control Point 0x2AD9: {'ja' if sm.get('controlPoint') else 'nein'}")
            A(f"- Indoor Bike Data 0x2AD2: {'ja' if sm.get('indoorBikeData') else 'nein'}")
            A(f"- Vendor-Services: {sm.get('vendorServices')}")
            A(f"- Maschinenbefund: `{sm.get('verdict')}`")
        else:
            A("- kein GATT-Dump")
        A("")
        A("### 3. Bytes von 0x2ACC, 0x2AD6, 0x2AD8")
        if reads:
            A("")
            A("| UUID | Bedeutung | Rohbytes | Auswertung |")
            A("|---|---|---|---|")
            for uuid in (UUID_FEATURE, UUID_RES_RANGE, UUID_PWR_RANGE):
                e = reads.get(uuid, {})
                if not e:
                    continue
                note = e.get("error") or ""
                p = e.get("parsed")
                if p and uuid == UUID_FEATURE:
                    note = "steuerbar: " + (", ".join(p["target"]) or "nichts")
                elif p and uuid == UUID_RES_RANGE:
                    note = (f"roh {p['raw']}, Lesart {p['likely_unit']} "
                            f"(Stufen {p['as_levels']['min']}..{p['as_levels']['max']})")
                elif p and uuid == UUID_PWR_RANGE:
                    note = f"{p['min_w']}..{p['max_w']} W, Schritt {p['increment_w']} W"
                A(f"| `{uuid}` | {e.get('label')} | `{e.get('hex', '-')}` | {note} |")
            for uuid in ("2A00", "2A29", "2A24", "2A26"):
                e = reads.get(uuid)
                if e and e.get("text"):
                    A(f"| `{uuid}` | {e['label']} | `{e.get('hex')}` | {e['text']} |")
            A("")
        else:
            A("- keine Reads")
        A("")
        A("### 4. Notify-Pakete von 0x2AD2")
        if bd:
            A(f"- {bd.get('packets')} Pakete geparst, Felder: "
              f"{', '.join(bd.get('fields', [])) or 'keine'}")
            for f in bd.get("flags_seen", []):
                A(f"- Flags {f}")
            A(f"- Ruhe: `{self._fmt_window(bd.get('idle', {})).strip()}`")
            A(f"- Treten: `{self._fmt_window(bd.get('pedaling', {})).strip()}`")
            for h in bd.get("example_hex", []):
                A(f"- Beispielpaket: `{h}`")
            if bd.get("parse_errors"):
                A("- **Parse-Fehler** (fuer die Codec-Tests wichtig):")
                for e in bd["parse_errors"][:5]:
                    A(f"  - {e}")
            A(f"- Vollstaendige Rohbytes: `bike-data.jsonl`")
        else:
            A("- keine Bike-Data-Phase gelaufen")
        A("")
        A("### 5. Antwort auf 0x00 am Control Point")
        g = cp.get("request_control")
        if g:
            A(f"- Gesendet: `{g['sent']}`")
            A(f"- Antwort: `{g.get('response_hex') or 'keine'}` → "
              f"{g.get('result') or ('Timeout' if g.get('timeout') else g.get('error'))}")
            A(f"- Steuerung freigegeben: **{'ja' if g.get('success') else 'nein'}**")
        else:
            A("- Control Point nicht getestet")
        A("")
        A("### 6. Zieht der Widerstand an?")
        if r.get("effects"):
            A("")
            A("| Kommando | gesendet | Antwort | Power vor→nach | resistance vor→nach "
              "| gefuehlt | Befund |")
            A("|---|---|---|---|---|---|---|")
            for e in r["effects"]:
                w = e["write"]
                pw = self._pair(e["pre"], e["post"], "power")
                rs = self._pair(e["pre"], e["post"], "resistance")
                A(f"| {e['label']} | `{w['sent']}` | {w.get('result') or 'keine'} | {pw} "
                  f"| {rs} | {e.get('felt') or '-'} | {e['verdict']} |")
            A("")
            A("Die Deltas stammen aus je "
              f"{self.args.effect_seconds} s Mittelwert vor und nach dem Write, "
              "nicht aus dem Gefuehl im Bein.")
        else:
            A("- keine Wirkungsmessung gelaufen")
        A("")

        # ── Ausgang ────────────────────────────────────────────────────
        A("## Ausgang")
        A("")
        A(self._outcome())
        A("")

        if r.get("dual"):
            d = r["dual"]
            A("## Verbindungsbudget (§14)")
            A("")
            A(f"- Bike `{d['bike_mac']}` und Gurt `{d['hr_mac']}` "
              f"{d['minutes']} min parallel")
            A(f"- Linkverluste: {d['link_losses']}, Links am Ende: {d['links_at_end']}")
            A(f"- Befund: **{d['verdict']}**")
            A("")
            A("| Minute | Links | Notifies je Link | Verluste |")
            A("|---|---|---|---|")
            for m in d.get("per_minute", []):
                nn = ", ".join(f"L{k}: {v}" for k, v in sorted(m["notifies"].items()))
                A(f"| {m['minute']} | {m['links']} | {nn} | {m['losses']} |")
            A("")
            A("Getestet wurde mit `CONFIG_BT_NIMBLE_MAX_CONNECTIONS=3`, belegt sind zwei "
              "Central-Links. Der dritte Link in v0.2 ist die Peripheral-Rolle zur App, "
              "nicht ein weiterer Central — dafuer muessen die beiden "
              "`ROLE_*_DISABLED`-Flags fallen, was RAM kostet und hier noch nicht "
              "mitgemessen ist.")
            A("")
        if r.get("crash"):
            cr = r["crash"]
            A("## Widerstand nach Client-Crash (§14)")
            A("")
            A(f"- Unter Last: `{self._fmt_window(cr['before']).strip()}`")
            A(f"- Nach dem Neustart: `{self._fmt_window(cr['after']).strip()}`"
              if "samples" in cr.get("after", {}) else
              f"- Nach dem Neustart: {cr.get('after')}")
            A(f"- Beobachtung am Rad: {cr.get('felt') or '-'}")
            A(f"- Reconnect danach: {'ok' if cr.get('reconnect_ok') else 'fehlgeschlagen'}")
            A("")

        A("## Randbedingungen des Laufs")
        A("")
        A("- Nur ein Central gleichzeitig: nRF Connect, MyWhoosh und Kinomap waren zu.")
        A("- Das Konsolendisplay des Bikes ist waehrend der Verbindung aus.")
        A("- Alle Control-Writes liefen durch den Limiter der Firmware "
          "(Whitelist 00/01/04/05/07/08, Watt- und Stufenklemme, Deadman).")
        pe = self.results.get("bikedata", {}).get("parse_errors")
        if pe:
            A(f"- {len(pe)} Pakete waren mit dem aktuellen Parser nicht lesbar.")
        A("")
        A("## Dateien")
        A("")
        for name, what in (("adv.json", "Advertising aller gesehenen Geraete"),
                           ("gatt.json", "vollstaendiger Attributbaum"),
                           ("reads.json", "statische Reads mit Auswertung"),
                           ("bike-data.jsonl", "Rohbytes 0x2AD2 mit Phasenmarke"),
                           ("controlpoint.jsonl", "Rohbytes 0x2AD9 und 0x2ADA"),
                           ("probe-log.jsonl", "alles, in Reihenfolge"),
                           ("effects.json", "Wirkungsmessung je Kommando")):
            if (self.out / name).exists():
                A(f"- `{name}` — {what}")

        path = self.out / "bericht.md"
        path.write_text("\n".join(L) + "\n", encoding="utf-8")
        if r.get("effects"):
            (self.out / "effects.json").write_text(
                json.dumps(r["effects"], indent=2, ensure_ascii=False), encoding="utf-8")
        (self.out / "ergebnis.json").write_text(
            json.dumps(r, indent=2, ensure_ascii=False), encoding="utf-8")
        good(f"{path}")
        return path

    @staticmethod
    def _pair(pre: dict, post: dict, key: str) -> str:
        if key in pre and key in post:
            return f"{pre[key]['mean']} → {post[key]['mean']}"
        return "-"

    def _outcome(self) -> str:
        r = self.results
        sm = r.get("gatt_summary", {})
        granted = r.get("control_granted")
        works = any(e["verdict"].startswith("wirkt") for e in r.get("effects", []))
        if not sm:
            return "Kein GATT-Dump — kein Ausgang bestimmbar."
        if not sm.get("ftms"):
            return ("**Ausgang 3: proprietaer.** Kein 0x1826 im GATT. Das Bike spricht "
                    "kein FTMS; die Vendor-Services aus `gatt.json` muessten "
                    "reverse-engineert werden. Fuer esp32.ergo heisst das: v0.1 laeuft "
                    "nur lesend oder gar nicht, die Annahme des Pflichtenhefts "
                    "(Standard-FTMS mit offenem Control Point) traegt nicht.")
        if not sm.get("controlPoint"):
            return ("**Zwischenfall:** 0x1826 ist da, aber ohne 0x2AD9. Lesen geht, "
                    "steuern nicht. esp32.ergo v0.1 wird damit zum Trainingscomputer "
                    "ohne Steuerung.")
        if granted and works:
            return ("**Ausgang 1: Standard-FTMS mit offenem Control Point.** Die Annahme "
                    "des Pflichtenhefts traegt. esp32.ergo v0.1 kann wie geplant gebaut "
                    "werden; die Sonden-Firmware wandert als `BleCentral` plus "
                    "`FtmsClient` weiter.")
        if granted and not works:
            return ("**Grenzfall:** Control Point antwortet mit Success, aber die "
                    "Zielvorgabe zeigt keine messbare Wirkung in 0x2AD2. Vor v0.1 klaeren, "
                    "ob das Bike den Wert nur quittiert, ob es einen Start-Befehl braucht "
                    "oder ob die Einheit des Parameters nicht passt (0x2AD6 vergleichen).")
        return ("**Ausgang 2: Control Point verweigert.** Request Control wird nicht "
                "gewaehrt oder bleibt unbeantwortet. Meist haengt das an der Reihenfolge "
                "(Indications vor 0x00) — die war hier eingehalten. Bleibt es dabei, ist "
                "das Bike nur lesbar und §6 des Pflichtenhefts (Steuerung) entfaellt.")


# ── CLI ──────────────────────────────────────────────────────────────────────

def default_outdir() -> Path:
    # Bevorzugt das Repo-docs-Verzeichnis der Sonde
    repo = Path(__file__).resolve().parent.parent / "docs" / "ergometer"
    return repo / f"scan-{dt.date.today():%Y%m%d}"


def main(argv=None) -> int:
    ap = argparse.ArgumentParser(
        description="BLE-Scan des Ergometers ueber die ESP32-Sonde abfahren")
    ap.add_argument("command",
                    choices=["all", "scan", "gatt", "read", "bikedata", "control",
                             "dual", "crash", "report", "panic", "status",
                             "summary", "live", "export", "suite", "watt", "sim"],
                    help="all=Schritt 1-4; suite=erweiterte Tests; summary/live/export=Abruf")
    ap.add_argument("--host", required=False, default="probe.local",
                    help="IP oder Name der Sonde")
    ap.add_argument("--out", type=Path, default=None, help="Ausgabeordner")
    ap.add_argument("--mac", default=None, help="MAC des Bikes, sonst automatisch")
    ap.add_argument("--hr-mac", default=None, help="MAC des Brustgurts")
    ap.add_argument("--watt", type=int, default=100, help="Zielleistung fuer den Test")
    ap.add_argument("--level", type=int, default=10, help="Zielstufe fuer den Test")
    ap.add_argument("--scan-seconds", type=int, default=15)
    ap.add_argument("--idle-seconds", type=int, default=20)
    ap.add_argument("--pedal-seconds", type=int, default=60)
    ap.add_argument("--effect-seconds", type=int, default=15,
                    help="Fenster fuer die Wirkungsmessung vor und nach dem Write")
    ap.add_argument("--minutes", type=int, default=10, help="Dauer fuer dual")
    ap.add_argument("--grade", type=float, default=2.0,
                    help="Steigung %% fuer sim-Test (0x11)")
    ap.add_argument("--no-prompt", action="store_true",
                    help="keine Rueckfragen (dann fehlen die Trittphasen)")
    ap.add_argument("--keep-link", action="store_true",
                    help="Link am Ende offen lassen")
    args = ap.parse_args(argv)

    outdir = args.out or default_outdir()
    probe = Probe(args.host)

    if args.command == "status":
        print(json.dumps(probe.status(), indent=2, ensure_ascii=False))
        return 0
    if args.command == "summary":
        print(json.dumps(probe.get("/api/probe/summary"), indent=2, ensure_ascii=False))
        return 0
    if args.command == "live":
        print(json.dumps(probe.get("/api/probe/live"), indent=2, ensure_ascii=False))
        return 0
    if args.command == "export":
        outdir.mkdir(parents=True, exist_ok=True)
        text = probe.get_text("/api/probe/export")
        path = outdir / "probe-export.ndjson"
        path.write_text(text, encoding="utf-8")
        lines = text.count("\n")
        print(f"wrote {path} ({lines} lines, {len(text)} bytes)")
        return 0
    if args.command == "panic":
        r = probe.panic("runner panic")
        print(json.dumps(r, indent=2))
        return 0 if r.get("ok") else 1

    try:
        st = probe.status()
    except ProbeError as e:
        bad(f"Sonde nicht erreichbar: {e}")
        return 2
    say(f"Sonde v{st.get('version')} auf {st.get('ip')} — "
        f"{st.get('boardLabel')}, Heap {st.get('heap', 0) // 1024} kB")
    guard = st.get("probe", {}).get("guard", {})
    say(f"Limiter: max {guard.get('maxWatt')} W, max Stufe {guard.get('maxLevel')}, "
        f"Deadman {guard.get('deadmanS')} s, Control "
        f"{'erlaubt' if guard.get('allowControl') else 'gesperrt'}")
    say(f"Ausgabe: {outdir}")

    r = Runner(probe, outdir, args)
    rc = 0
    try:
        if args.command in ("all", "scan"):
            r.step_scan()
        if args.command in ("all", "gatt", "read", "bikedata", "control"):
            if r.link is None:
                r.step_gatt()
        if args.command in ("all", "read"):
            r.step_reads()
        if args.command in ("all", "bikedata"):
            r.step_bikedata()
        if args.command in ("all", "control"):
            r.step_control()
        if args.command == "watt":
            if r.link is None:
                r.step_gatt()
            r.step_watt()
        if args.command == "sim":
            if r.link is None:
                r.step_gatt()
            r.step_sim()
        if args.command == "suite":
            if r.link is None:
                r.step_gatt()
            r.step_reads()
            r.step_bikedata()
            r.step_control()
            say("\n— Suite: Watt-Nachtest —")
            r.step_watt()
            say("\n— Suite: Simulation (optional) —")
            try:
                r.step_sim()
            except ProbeError as e:
                warn(f"sim uebersprungen: {e}")
            # Summary der Sonde mit ablegen
            try:
                summ = probe.get("/api/probe/summary")
                (outdir / "probe-summary.json").write_text(
                    json.dumps(summ, indent=2, ensure_ascii=False), encoding="utf-8")
                good("probe-summary.json geschrieben")
            except ProbeError as e:
                warn(f"summary: {e}")
        if args.command == "dual":
            r.step_dual()
        if args.command == "crash":
            r.step_crash()
        if args.command in ("all", "report", "control", "bikedata", "dual", "crash",
                            "watt", "sim", "suite"):
            r.report()
    except KeyboardInterrupt:
        bad("abgebrochen — sende Not-Stop")
        try:
            probe.panic("runner abgebrochen")
        except ProbeError:
            pass
        rc = 130
    except ProbeError as e:
        bad(str(e))
        try:
            probe.panic("runner fehler")
        except ProbeError:
            pass
        rc = 1
    finally:
        # Nach einem Crash-Test kann die Sonde gerade neu starten — jeder
        # Aufraeumschritt hier darf scheitern, ohne den Lauf zu verlieren.
        try:
            r.drain()
        except ProbeError as e:
            warn(f"Log nicht vollstaendig abgeholt: {e}")
        if not args.keep_link and args.command != "crash":
            try:
                probe.disconnect_all()
            except ProbeError:
                pass
        r.close()
    return rc


if __name__ == "__main__":
    sys.exit(main())
