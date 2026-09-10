#!/usr/bin/env bash
# Build und Ausrollen der Sonden-Firmware aus dem Debian-Container.
#
# Erstflash eines leeren ESP32 laeuft ueber das Hub-UI per USB (Reiter Geraete
# -> Flash) oder lokal per "pio run -t upload". Danach braucht es kein Kabel
# mehr: dieses Skript baut, legt die Bin nach dem Familienschema ab und rollt
# sie entweder direkt an die Sonde oder ueber den Hub aus.
#
#   ./deploy.sh --ota 192.168.178.55
#   ./deploy.sh --hub 192.168.178.113:8093 --mac A0B7651C2D3E
#   ./deploy.sh --env probe-s3 --build-only
#
# Der Hub-Weg ist der robustere: die Bin liegt danach in der Firmware-Ablage
# und der ESP holt sie beim naechsten Heartbeat (POST /api/ota-push setzt
# otaUrl, der Heartbeat liefert sie aus).

set -euo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
ENV_NAME="probe"
OTA_IP=""
HUB=""
MAC=""
BUILD_ONLY=0
NAME="ftmsprobe"

die() { printf '\033[31mFehler:\033[0m %s\n' "$1" >&2; exit 1; }
step() { printf '\n\033[1m==> %s\033[0m\n' "$1"; }

while [ $# -gt 0 ]; do
    case "$1" in
        --env) ENV_NAME="${2:-}"; shift 2;;
        --ota) OTA_IP="${2:-}"; shift 2;;
        --hub) HUB="${2:-}"; shift 2;;
        --mac) MAC="${2:-}"; shift 2;;
        --build-only) BUILD_ONLY=1; shift;;
        -h|--help) sed -n '2,20p' "$0"; exit 0;;
        *) die "unbekannte Option: $1";;
    esac
done

case "$ENV_NAME" in
    probe)    FAMILY="esp32";;
    probe-s3) FAMILY="esp32s3";;
    *) die "env muss probe oder probe-s3 sein";;
esac

command -v pio >/dev/null 2>&1 || command -v platformio >/dev/null 2>&1 \
    || die "platformio nicht gefunden (pip install platformio)"
PIO="$(command -v pio || command -v platformio)"
command -v curl >/dev/null 2>&1 || die "curl nicht gefunden"

VERSION="$(grep -oP 'FW_VERSION=\\"\K[0-9.]+' "$ROOT/platformio.ini" | head -n1 || true)"
[ -n "$VERSION" ] || die "FW_VERSION nicht aus platformio.ini lesbar"

step "Build $ENV_NAME (v$VERSION)"
"$PIO" run -d "$ROOT" -e "$ENV_NAME"

SRC="$ROOT/.pio/build/$ENV_NAME/firmware.bin"
[ -f "$SRC" ] || die "Bin nicht gefunden: $SRC"

# Namensschema der Familie: {name}.{version}.{family}.bin
BIN="$NAME.$VERSION.$FAMILY.bin"
mkdir -p "$ROOT/dist"
cp "$SRC" "$ROOT/dist/$BIN"
SIZE="$(stat -c %s "$ROOT/dist/$BIN")"
printf '    dist/%s  (%s kB)\n' "$BIN" "$((SIZE / 1024))"

if [ "$BUILD_ONLY" -eq 1 ]; then
    step "Fertig (nur Build)"
    exit 0
fi

if [ -z "$OTA_IP" ] && [ -z "$HUB" ]; then
    step "Kein Ziel angegeben"
    echo "    --ota <sonden-ip>   direkter Upload an die Sonde"
    echo "    --hub <host:port> --mac <MAC>   Ablage im Hub plus OTA-Push"
    exit 0
fi

# ── Weg 1: direkt an die Sonde ───────────────────────────────────────────────
if [ -n "$OTA_IP" ]; then
    step "OTA direkt an $OTA_IP"
    BEFORE="$(curl -fsS --max-time 5 "http://$OTA_IP/api/status" 2>/dev/null \
        | grep -oP '"version":"\K[^"]+' || echo "?")"
    echo "    laufende Version: $BEFORE"
    LINKS="$(curl -fsS --max-time 5 "http://$OTA_IP/api/status" 2>/dev/null \
        | grep -oP '"linkCount":\K[0-9]+' | head -n1 || echo 0)"
    if [ "${LINKS:-0}" != "0" ]; then
        die "Sonde hat $LINKS offene BLE-Links — erst trennen, nicht mitten in einer Messung flashen"
    fi
    curl -fsS --max-time 120 -F "firmware=@$ROOT/dist/$BIN" "http://$OTA_IP/ota-upload" \
        || die "Upload fehlgeschlagen"
    echo "    Upload ok, warte auf Neustart ..."
fi

# ── Weg 2: ueber den Hub ─────────────────────────────────────────────────────
if [ -n "$HUB" ]; then
    [ -n "$MAC" ] || die "--hub braucht --mac (siehe http://$HUB/ Reiter Geraete)"
    step "Firmware in die Hub-Ablage"
    curl -fsS --max-time 120 -F "firmware=@$ROOT/dist/$BIN" \
        "http://$HUB/api/firmware-upload" || die "firmware-upload fehlgeschlagen"
    echo
    step "OTA-Push planen fuer $MAC"
    curl -fsS --max-time 20 -X POST -H 'Content-Type: application/json' \
        -d "{\"mac\":\"$MAC\",\"firmware\":\"$BIN\"}" \
        "http://$HUB/api/ota-push" || die "ota-push fehlgeschlagen"
    echo
    echo "    Wird beim naechsten Heartbeat uebertragen (Standard 30 s)."
fi

# ── Ergebnis pruefen ─────────────────────────────────────────────────────────
if [ -n "$OTA_IP" ]; then
    step "Warte auf die neue Version"
    for i in $(seq 1 30); do
        sleep 3
        GOT="$(curl -fsS --max-time 4 "http://$OTA_IP/api/status" 2>/dev/null \
            | grep -oP '"version":"\K[^"]+' || true)"
        if [ "$GOT" = "$VERSION" ]; then
            echo "    Sonde laeuft auf v$VERSION"
            exit 0
        fi
        printf '.'
    done
    echo
    die "Sonde meldet nach 90 s nicht v$VERSION — Serial-Log pruefen"
fi
