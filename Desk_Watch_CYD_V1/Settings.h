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
// The task list is edited at http://<TASK_HOSTNAME>.local
inline constexpr char TASK_HOSTNAME[] = "postit";

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
