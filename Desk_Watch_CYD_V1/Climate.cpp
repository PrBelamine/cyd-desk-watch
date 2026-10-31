// DHT22 temperature / humidity sensor on one GPIO (bit-banged, no extra library).
// Protocol: the board pulls the line low for 2 ms, the sensor answers with 40 bits; a "1" bit has a
// longer high pulse than low pulse. Decoding and the checksum are in DeskLogic.cpp (host-tested).
#include <Arduino.h>
#include "App.h"
#include "Settings.h"

namespace {
// Length (in microseconds) of the current pulse at 'level'; 0 on timeout.
uint32_t pulseUs(int level) {
  const uint32_t start = micros();
  while (digitalRead(DHT_PIN) == level) if (micros() - start > 150) return 0;
  return micros() - start;
}
}  // namespace

// 0 = OK, 1 = no answer from the sensor (wiring), 2 = timing error, 3 = checksum / range error.
// Takes about 7 ms; call at most once every 2 s.
int climateRead(float& tempC, float& humPct) {
  uint8_t data[5] = {0, 0, 0, 0, 0};
  uint32_t lows[40], highs[40];
  pinMode(DHT_PIN, OUTPUT); digitalWrite(DHT_PIN, LOW);
  delay(2);
  pinMode(DHT_PIN, INPUT_PULLUP);
  delayMicroseconds(20);
  noInterrupts();
  bool ok = pulseUs(HIGH) != 0;
  ok = ok && pulseUs(LOW) != 0 && pulseUs(HIGH) != 0;
  int err = ok ? 0 : 1;
  for (int i = 0; ok && i < 40; ++i) {
    lows[i] = pulseUs(LOW); highs[i] = pulseUs(HIGH);
    if (!lows[i] || !highs[i]) { ok = false; err = 2; }
  }
  interrupts();
  if (!ok) return err;
  for (int i = 0; i < 40; ++i) { data[i / 8] <<= 1; if (highs[i] > lows[i]) data[i / 8] |= 1; }
  return decodeDht22(data, tempC, humPct) ? 0 : 3;
}
