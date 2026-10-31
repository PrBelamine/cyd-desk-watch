#pragma once
// Web radio on the CYD speaker (Hit Radio, MP3 128 kbps). The stream is decoded in its own task so the screen and the
// touch never wait for the sound. Needs the "ESP8266Audio" library (Library Manager) and the Huge APP partition scheme.
#include <stdint.h>
#include "DeskLogic.h"

void radioBegin();                                  // creates the audio task (call once, after Wi-Fi.mode)
void radioPlay(int station, uint8_t volume);        // start, or switch station; volume 1..10
void radioStop();
void radioSetNoise(uint8_t level);                 // 0 off, 1 soft, 2 strong: removes hiss and rumble (live, no restart)
void radioSetDither(uint8_t level);                // 0 off, 1 low, 2 normal, 3 high: grain that stops crackling at low volume (adds a little hiss)
void radioSetVolume(uint8_t volume);                // 0 = muted, 1..10
RadioState radioState();
inline bool radioActive() { return radioState() != RADIO_OFF; }
