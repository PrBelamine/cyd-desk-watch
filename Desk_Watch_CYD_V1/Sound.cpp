#include <Arduino.h>
#include "App.h"
#include "Settings.h"

namespace {
// 8-bit PWM duty per volume level (0 = muted). 128 (50 %) is the loudest square wave; steps are roughly logarithmic.
constexpr uint8_t DUTY[VOLUME_MAX + 1] = {0, 4, 12, 32, 70, 128};
constexpr uint8_t LEDC_CHANNEL = 4;     // the backlight uses channel 0; channels 0/1 share one timer, 4/5 another
const Tune* tune = nullptr;
bool looping = false, active = false, attached = false;
uint8_t noteIndex = 0, duty = 0;
uint32_t noteEnd = 0;

void toneOut(unsigned freq) {
  if (!attached) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcAttachChannel(SPEAKER_PIN, 2000, 8, LEDC_CHANNEL);   // own timer: melodies must not change the backlight PWM frequency
#else
    ledcSetup(LEDC_CHANNEL, 2000, 8); ledcAttachPin(SPEAKER_PIN, LEDC_CHANNEL);
#endif
    attached = true;
  }
  if (!freq || !duty) {
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
    ledcWrite(SPEAKER_PIN, 0);
#else
    ledcWrite(LEDC_CHANNEL, 0);
#endif
    return;
  }
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  ledcChangeFrequency(SPEAKER_PIN, freq, 8); ledcWrite(SPEAKER_PIN, duty);
#else
  ledcWriteTone(LEDC_CHANNEL, freq); ledcWrite(LEDC_CHANNEL, duty);
#endif
}
void startNote() {
  toneOut(tune->notes[noteIndex]);
  noteEnd = millis() + unsigned(tune->lengths[noteIndex]) * 10U;
}
}  // namespace

void soundBegin() { pinMode(SPEAKER_PIN, OUTPUT); digitalWrite(SPEAKER_PIN, LOW); }
void soundPlay(const Tune& t, bool loop, uint8_t volume) {
  tune = &t; looping = loop; active = true; noteIndex = 0;
  duty = DUTY[volume > VOLUME_MAX ? VOLUME_MAX : volume];
  startNote();
}
void soundStop() {
  active = false;
  if (attached) toneOut(0);
}
bool soundActive() { return active; }
void soundTick() {
  if (!active || int32_t(millis() - noteEnd) < 0) return;
  if (++noteIndex >= tune->count) {
    if (!looping) { soundStop(); return; }
    noteIndex = 0;
  }
  startNote();
}
