#pragma once

#ifndef FW_VERSION
#define FW_VERSION "0.1.2"
#endif

#ifndef DEVICE_NAME_DEFAULT
#define DEVICE_NAME_DEFAULT "FTMS-Probe"
#endif

#ifndef HUB_HOST_DEFAULT
#define HUB_HOST_DEFAULT "192.168.178.113"
#endif

#ifndef HUB_PORT_DEFAULT
#define HUB_PORT_DEFAULT 8093
#endif

#ifndef WIFI_AP_NAME
#define WIFI_AP_NAME "ESP-Probe-Setup"
#endif

#ifndef WIFI_PORTAL_TIMEOUT_S
#define WIFI_PORTAL_TIMEOUT_S 180
#endif

#ifndef RESET_BUTTON_PIN
#define RESET_BUTTON_PIN 0
#endif

#ifndef RESET_HOLD_SEC
#define RESET_HOLD_SEC 3
#endif

/** Advertising-Cache. Am Bike stehen selten mehr als ein Dutzend Geraete. */
#ifndef PROBE_MAX_SCAN
#define PROBE_MAX_SCAN 24
#endif

/** Beworbene Service-UUIDs je Advertising-Eintrag. */
#ifndef PROBE_MAX_ADV_UUID
#define PROBE_MAX_ADV_UUID 6
#endif

/** Gleichzeitige Links. 2 reichen fuer Bike + Gurt, 3 deckt das NimBLE-Limit ab. */
#ifndef PROBE_MAX_LINKS
#define PROBE_MAX_LINKS 3
#endif

/** Ringpuffer fuer das JSONL-Log (Eintraege, nicht Bytes). */
#ifndef PROBE_LOG_SIZE
#define PROBE_LOG_SIZE 256
#endif

/** Nutzbare Rohbytes je Logeintrag. FTMS Indoor Bike Data bleibt darunter. */
#ifndef PROBE_LOG_DATA
#define PROBE_LOG_DATA 32
#endif

/** Phasenlabels, die der Runner setzen darf. */
#ifndef PROBE_MAX_PHASES
#define PROBE_MAX_PHASES 16
#endif

/** Abonnements je Link (0x2AD2, 0x2AD9, 0x2ADA, 0x2A37, ...). */
#ifndef PROBE_MAX_SUBS
#define PROBE_MAX_SUBS 6
#endif

// ── Safety-Limiter: Vorgaben, in der Config aenderbar ────────────────────────

/** Obergrenze fuer Set Target Power (0x05). */
#ifndef PROBE_MAX_WATT_DEFAULT
#define PROBE_MAX_WATT_DEFAULT 150
#endif

/** Obergrenze fuer Set Target Resistance Level (0x04). */
#ifndef PROBE_MAX_LEVEL_DEFAULT
#define PROBE_MAX_LEVEL_DEFAULT 12
#endif

/** Deadman: so lange darf das Keepalive der CLI ausbleiben. */
#ifndef PROBE_DEADMAN_S_DEFAULT
#define PROBE_DEADMAN_S_DEFAULT 20
#endif

#ifndef NTP_SERVER_DEFAULT
#define NTP_SERVER_DEFAULT "pool.ntp.org"
#endif

/** Europe/Berlin POSIX TZ (CET/CEST) */
#ifndef TZ_DEFAULT
#define TZ_DEFAULT "CET-1CEST,M3.5.0,M10.5.0/3"
#endif

#if defined(CONFIG_IDF_TARGET_ESP32S3)
#define PROBE_BOARD_ID "esp32-s3"
#define PROBE_BOARD_LABEL "ESP32-S3"
#define PROBE_HW_TYPE "esp32s3"
#else
#define PROBE_BOARD_ID "esp32-mini"
#define PROBE_BOARD_LABEL "ESP32 Mini D1"
#define PROBE_HW_TYPE "esp32"
#endif

#define PROBE_FW_TYPE "ftmsprobe"
