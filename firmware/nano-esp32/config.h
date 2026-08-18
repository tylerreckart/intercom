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
#define ALFRED_HOST "192.168.1.10"
#endif
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

// Arduino Nano ESP32 silkscreen -> GPIO (nora variant).
#define PIN_MIC_WS 5    // D2
#define PIN_MIC_BCLK 6  // D3
#define PIN_MIC_SD 7    // D4
#define PIN_SPK_WS 8    // D5
#define PIN_SPK_BCLK 9  // D6
#define PIN_SPK_DIN 10  // D7
#define PIN_PTT 17      // D8, to GND, INPUT_PULLUP
#define PIN_AMP_SD 18   // D9 -> MAX98357A SD (HIGH=on, LOW=mute)

// ICS-43434 L/R tied to GND => left slot. Set 1 if you wired L/R to 3V3.
#define MIC_LEFT_SLOT 1
