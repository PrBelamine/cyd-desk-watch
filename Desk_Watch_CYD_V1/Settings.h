#pragma once
#include <Arduino.h>
#if __has_include("Secrets.h")
#include "Secrets.h"
#else
#include "Secrets.example.h"
#endif

// Time zone in POSIX format. Hanoi is ICT-7 (UTC+7, no daylight saving).
// Other examples: "CET-1CEST,M3.5.0,M10.5.0/3" (Paris), "GMT0BST,M3.5.0/1,M10.5.0" (London).
inline constexpr char TIMEZONE_POSIX[] = "ICT-7";
// The watch's web page (To-Do, Location, Radios) is at http://<TASK_HOSTNAME>.local
inline constexpr char TASK_HOSTNAME[] = "cyd";

// Edition number, shown on the start-up screen and on the Serial Monitor.
inline constexpr char FIRMWARE_EDITION[] = "1.27";

inline constexpr uint8_t DISPLAY_ROTATION = 1;
// Colour inversion of the panel. true = black/white swapped on your board is fixed. Set to false if the colours look inverted after flashing.
inline constexpr bool DISPLAY_INVERT_COLORS = true;
inline constexpr uint8_t TOUCH_ROTATION = 1;
// Touch controller (XPT2046) on its own SPI bus, as wired on the ESP32-2432S028R "CYD".
inline constexpr int TOUCH_CS_PIN = 33;
inline constexpr int TOUCH_IRQ_PIN = 36;
inline constexpr int TOUCH_CLK_PIN = 25;
inline constexpr int TOUCH_MISO_PIN = 39;
inline constexpr int TOUCH_MOSI_PIN = 32;
// Speaker (through the on-board amplifier) and the backlight.
inline constexpr int SPEAKER_PIN = 26;
inline constexpr int DHT_PIN = 27;   // DHT22 data wire on connector CN1 (3V3 / IO27 / IO22 / GND)
// Weather page (Open-Meteo, no account needed): coordinates and name of the town the watch starts with (Hanoi).
// You choose your own town afterwards on http://cyd.local/location (saved on the watch).
inline constexpr float WEATHER_LATITUDE = 21.028f, WEATHER_LONGITUDE = 105.854f;
inline constexpr char WEATHER_PLACE[] = "Hanoi";
