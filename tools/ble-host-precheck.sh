#!/usr/bin/env bash
# Prueft, ob auf diesem Host ein BLE-Adapter fuer den optionalen Gegencheck
# nutzbar ist. Auf dem Proxmox-Host ausfuehren, nicht im LXC:
#
#   bash ble-host-precheck.sh
#
# Bluetooth ist im Linux-Kernel nicht namespace-faehig. Im Container ist ein
# HCI-Adapter deshalb grundsaetzlich nicht sichtbar, auch nicht per
# Device-Passthrough. Das Skript erkennt diesen Fall und sagt es.

set -u

pass=0
warn=0
fail=0

hdr() { printf '\n\033[1m%s\033[0m\n' "$1"; }
ok()   { printf '  \033[32m[ ok ]\033[0m   %s\n' "$1"; pass=$((pass+1)); }
nok()  { printf '  \033[31m[fail]\033[0m   %s\n' "$1"; fail=$((fail+1)); }
note() { printf '  \033[33m[hinw]\033[0m   %s\n' "$1"; warn=$((warn+1)); }
info() { printf '  \033[2m[info]\033[0m   %s\n' "$1"; }

have() { command -v "$1" >/dev/null 2>&1; }

hdr "1. Umgebung"

virt="physisch/unbekannt"
if have systemd-detect-virt; then
    virt="$(systemd-detect-virt 2>/dev/null || echo none)"
fi
info "Virtualisierung: ${virt}"
info "Kernel: $(uname -r)  Arch: $(uname -m)"

case "$virt" in
    lxc|lxc-libvirt|systemd-nspawn|docker|podman)
        nok "Dies ist ein Container. HCI-Adapter sind hier nicht sichtbar," 
        info "     weil der Bluetooth-Stack nicht namespace-faehig ist."
        info "     Skript auf dem Proxmox-Host laufen lassen (pct exec verlassen)."
        ;;
    *)
        ok "Kein Container erkannt — HCI-Zugriff grundsaetzlich moeglich"
        ;;
esac

if [ -r /proc/1/environ ] && grep -qa container=lxc /proc/1/environ 2>/dev/null; then
    note "/proc/1/environ meldet container=lxc"
fi

hdr "2. Hardware"

found_hw=0
if have lspci; then
    hits="$(lspci -nn 2>/dev/null | grep -iE 'bluetooth|network controller' || true)"
    if [ -n "$hits" ]; then
        info "lspci:"
        printf '           %s\n' "$hits"
        found_hw=1
    fi
else
    note "lspci fehlt (apt install pciutils)"
fi

if have lsusb; then
    hits="$(lsusb 2>/dev/null | grep -iE 'bluetooth|wireless|intel corp|realtek|broadcom|cambridge silicon' || true)"
    if [ -n "$hits" ]; then
        info "lsusb:"
        printf '           %s\n' "$hits"
        found_hw=1
    fi
else
    note "lsusb fehlt (apt install usbutils)"
fi

if [ "$found_hw" -eq 1 ]; then
    ok "Bluetooth-verdaechtige Hardware gefunden"
else
    nok "Keine Bluetooth-Hardware in lspci/lsusb — ThinkCentre-Modelle ohne WLAN-Karte haben keine"
    info "     Abhilfe: USB-BLE-Dongle (BT 4.0+), oder direkt den ESP32-Weg nehmen"
fi

hdr "3. Kernel und Treiber"

if [ -d /sys/class/bluetooth ] && [ -n "$(ls -A /sys/class/bluetooth 2>/dev/null)" ]; then
    ok "/sys/class/bluetooth: $(ls /sys/class/bluetooth | tr '\n' ' ')"
else
    nok "/sys/class/bluetooth ist leer — kein HCI-Device registriert"
fi

mods="$(lsmod 2>/dev/null | awk '/^(bluetooth|btusb|btintel|btrtl|btbcm)/{print $1}' | tr '\n' ' ')"
if [ -n "$mods" ]; then
    ok "Module geladen: ${mods}"
else
    note "Keine BT-Module geladen (modprobe btusb) oder in modprobe.d geblacklistet"
    if grep -rqs '^ *blacklist \+bt' /etc/modprobe.d/ 2>/dev/null; then
        nok "btusb/bt* ist in /etc/modprobe.d blacklistet — typisch nach VM-Passthrough-Setup"
    fi
fi

if have dmesg; then
    hits="$(dmesg 2>/dev/null | grep -i bluetooth | tail -n 8 || true)"
    if [ -n "$hits" ]; then
        info "dmesg (letzte Zeilen):"
        printf '           %s\n' "$hits"
        if printf '%s' "$hits" | grep -qiE 'firmware.*(failed|not found)|failed to load'; then
            nok "Firmware-Ladefehler im Log — pve-firmware/firmware-iwlwifi pruefen"
        fi
    else
        note "dmesg nennt kein Bluetooth"
    fi
fi

hdr "4. Userland"

if have bluetoothctl; then
    ok "bluez installiert ($(bluetoothctl --version 2>/dev/null || echo Version unbekannt))"
    ctrls="$(bluetoothctl list 2>/dev/null || true)"
    if [ -n "$ctrls" ]; then
        ok "Controller sichtbar:"
        printf '           %s\n' "$ctrls"
    else
        nok "bluetoothctl sieht keinen Controller"
    fi
else
    note "bluez fehlt (apt install bluez) — fuer den Gegencheck noetig"
fi

if have rfkill; then
    if rfkill list bluetooth 2>/dev/null | grep -qi 'yes'; then
        nok "Bluetooth ist per rfkill gesperrt — rfkill unblock bluetooth"
    else
        info "rfkill: keine Sperre"
    fi
fi

if have systemctl; then
    st="$(systemctl is-active bluetooth 2>/dev/null || echo inaktiv)"
    if [ "$st" = "active" ]; then
        ok "bluetooth.service laeuft"
    else
        note "bluetooth.service: ${st} (systemctl enable --now bluetooth)"
    fi
fi

if have python3; then
    ok "python3 vorhanden ($(python3 -V 2>&1)) — venv fuer bleak moeglich"
else
    note "python3 fehlt"
fi

hdr "5. Reichweite"

cat <<'TXT'
  Nicht automatisch pruefbar. BLE traegt ohne Sichtlinie realistisch 5-10 m;
  eine Betondecke oder zwei Waende dazwischen genuegen fuer Abbrueche.
  Steht dieser Host nicht im selben Raum wie das Ergometer, ist der
  Gegencheck sinnlos - dann zaehlt nur der ESP32 direkt am Bike.

  Wenn ein Controller da ist, gibt dieser Aufruf die Antwort in 10 s:
      bluetoothctl --timeout 10 scan on | grep -iE 'FS-|HAMMER|VARON|iConsole|Polar'
TXT

hdr "Ergebnis"
printf '  ok: %d   Hinweise: %d   Probleme: %d\n\n' "$pass" "$warn" "$fail"

if [ "$fail" -eq 0 ] && [ "$found_hw" -eq 1 ]; then
    echo "  Gegencheck per bleak ist moeglich. Naechster Schritt:"
    echo "      python3 -m venv ~/.ble-probe && ~/.ble-probe/bin/pip install bleak"
    echo "  Der Hauptweg (ESP32-Sonde am Bike) bleibt davon unabhaengig."
    exit 0
fi

echo "  Kein nutzbarer BLE-Adapter auf diesem Host. Das ist kein Blocker:"
echo "  der Hauptweg laeuft ueber die ESP32-Sonde am Bike (siehe README)."
exit 1
