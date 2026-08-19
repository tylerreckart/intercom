#pragma once

// Copy this file's WIFI_* / ALFRED_TOKEN values, or create secrets.h next to
// the sketch (gitignored) with #define WIFI_SSID, WIFI_PASSWORD, ALFRED_TOKEN.

#if __has_include("secrets.h")
#include "secrets.h"
#endif

#ifndef WIFI_SSID
#define WIFI_SSID "YOUR_WIFI_SSID"
#endif
#ifndef WIFI_PASSWORD
#define WIFI_PASSWORD "YOUR_WIFI_PASSWORD"
#endif

#ifndef ALFRED_HOST
// LAN IP of the machine running Alfred — not 127.0.0.1 (that is the ESP itself).
#define ALFRED_HOST "192.168.86.40"
#endif
#define FIRMWARE_CONFIG_TAG "alfred-lan-v2"
#ifndef ALFRED_PORT
#define ALFRED_PORT 8090
#endif
#ifndef ALFRED_TOKEN
#define ALFRED_TOKEN "dev-device-secret-change-me"
#endif
#ifndef ALFRED_DEVICE_ID
#define ALFRED_DEVICE_ID ""
#endif

#define SAMPLE_RATE 16000
#define MAX_RECORD_MS 8000
#define MIN_RECORD_MS 400
#define HEADER_TIMEOUT_MS 120000
#define HTTP_TIMEOUT_MS 15000
#define AUDIO_WAIT_MS HEADER_TIMEOUT_MS

// Silkscreen D# / A0 labels — correct in either IDE pin numbering mode.
#define PIN_MIC_WS D2
#define PIN_MIC_BCLK D3
#define PIN_MIC_SD D4
#define PIN_SPK_WS D5
#define PIN_SPK_BCLK D6
#define PIN_SPK_DIN D7
#define PIN_PTT D8       // to GND, INPUT_PULLUP
#define PIN_AMP_SD D9    // MAX98357A SD (HIGH=on, LOW=mute)
#define PIN_THINK_LED D10
#define THINK_BLINK_MS 250
#define PIN_VOLUME A0    // A50k wiper (ends to 3V3 and GND)
#define ADC_MAX 4095
#define VOL_UPDATE_MS 30

// ICS-43434 L/R tied to GND => left slot. Set 1 if you wired L/R to 3V3.
#define MIC_LEFT_SLOT 1
