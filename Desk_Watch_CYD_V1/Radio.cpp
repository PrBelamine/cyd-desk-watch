// Web radio: HTTP stream -> buffer -> MP3 decoder -> ESP32 8-bit DAC (GPIO26, the pin that feeds the CYD speaker amplifier).
// The 8-bit DAC is made to sound as clean as possible: the audio is oversampled to ~176-192 kHz and the rounding noise is
// filtered above 14 kHz so the amplifier never sees ultrasonic copies of the music; rounding uses a little dither (no noise
// shaping: on this board it produced an audible hiss), then a band-pass filter removes rumble and hiss.
#include <Arduino.h>
#include <WiFi.h>
#include <esp_log.h>
#include "AudioFileSourceHTTPStream.h"
#include "AudioFileSourceBuffer.h"
#include "AudioGeneratorMP3.h"
#include "AudioOutput.h"
#include "AudioLogger.h"
#include "driver/dac_continuous.h"
#include "Radio.h"
#include "Settings.h"

namespace {
constexpr size_t RADIO_BUFFER = 16 * 1024;   // RAM kept ahead of the decoder (the screen buffer uses 75 KB)

struct Biquad {                                              // 2nd-order Butterworth filter (RBJ cookbook)
  float b0 = 1, b1 = 0, b2 = 0, a1 = 0, a2 = 0, z1 = 0, z2 = 0;
  void design(bool highpass, float fc, float fs, float q = 0.7071f) {
    if (fc > fs * 0.45f) fc = fs * 0.45f;
    const float w = 2.0f * PI * fc / fs, c = cosf(w), al = sinf(w) / (2.0f * q), a0 = 1.0f + al;
    if (highpass) { b0 = (1 + c) / 2; b1 = -(1 + c); b2 = (1 + c) / 2; } else { b0 = (1 - c) / 2; b1 = 1 - c; b2 = (1 - c) / 2; }
    b0 /= a0; b1 /= a0; b2 /= a0; a1 = -2 * c / a0; a2 = (1 - al) / a0; z1 = z2 = 0;
  }
  float run(float x) { const float y = b0 * x + z1; z1 = b1 * x - a1 * y + z2; z2 = b2 * x - a2 * y; return y; }
};

class DacOutput : public AudioOutput {
 public:
  ~DacOutput() override { stop(); }
  bool begin() override { return true; }                    // the DAC is opened when the sample rate is known
  bool SetRate(int hz) override {
    if (hz == hertz && handle) return true;
    hertz = hz; close();
    up = (176400 + hz / 2) / hz; if (up < 1) up = 1; if (up > 32) up = 32;
    prev = 0;
    i1.design(false, 14000.0f, (float)hz * up, 0.5412f); i2.design(false, 14000.0f, (float)hz * up, 1.3066f);   // keeps the ultrasonic copies of the music away from the amplifier
    applied = 255;                                          // filters are (re)designed for the current noise setting on the next sample
    dac_continuous_config_t cfg = {};
    cfg.chan_mask = DAC_CHANNEL_MASK_CH1;                    // CH1 = GPIO26
    cfg.desc_num = 8; cfg.buf_size = 2048; cfg.freq_hz = hz * up;
    cfg.offset = 0; cfg.clk_src = DAC_DIGI_CLK_SRC_APLL;
    cfg.chan_mode = DAC_CHANNEL_MODE_SIMUL;
    if (dac_continuous_new_channels(&cfg, &handle) != ESP_OK) { handle = nullptr; return false; }
    if (dac_continuous_enable(handle) != ESP_OK) { close(); return false; }
    n = 0;
    return true;
  }
  bool ConsumeSample(int16_t sample[2]) override {
    if (!handle) return true;
    if (n + up > sizeof buf) { push(); if (n + up > sizeof buf) return false; }   // DAC buffer full: give control back to the decoder loop
    if (mode != applied) applyMode();
    int32_t m = ((int32_t)sample[0] + sample[1]) / 2;       // mono mix
    float f = lp.run(hp.run((float)m));                     // band-pass
    if (gate) {                                             // strong mode: quiet passages (hiss between words) are turned down
      const float a = fabsf(f); env = a > env ? a : env * 0.9995f;
      const float target = env < 450.0f ? 0.15f : 1.0f;
      g += (target - g) * (target > g ? 0.01f : 0.0004f);
      f *= g;
    }
    f *= volume;
    const float T = 26000.0f, a = fabsf(f);                  // soft limiter: loud broadcast peaks never reach the hard clip
    if (a > T) { const float lim = T + (a - T) * 0.25f; f = f < 0 ? -lim : lim; }
    if (f > 32767.0f) f = 32767.0f; if (f < -32768.0f) f = -32768.0f;
    for (int i = 0; i < up; ++i) {
      float x = prev + (f - prev) * (float)(i + 1) / up;     // linear interpolation between two samples...
      x = i2.run(i1.run(x));                                 // ...then everything above 14 kHz is filtered out
      // No noise shaping (it made a hiss of its own that the amplifier turned into audible noise): plain rounding with a
      // little triangular dither (+-0.5 step), so quiet passages do not crackle.
      rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5;
      const float d = ((float)((int32_t)(rng & 127) - (int32_t)((rng >> 8) & 127))) * ditherScale;   // triangular, +-0.5 step x scale
      int32_t q = (int32_t)floorf((x + d + 32768.0f) * (1.0f / 256.0f) + 0.5f);
      if (q < 0) q = 0; if (q > 255) q = 255;
      buf[n++] = (uint8_t)q;
    }
    prev = f;
    return true;
  }
  void SetDither(uint8_t level) { static const float S[4] = {0.0f, 0.5f, 1.0f, 2.0f}; ditherScale = S[level > 3 ? 2 : level]; }   // 0 off, 1 low, 2 normal, 3 high
  void SetVolume(float v) { volume = v; }                      // linear gain 0..1
  void SetMode(uint8_t m) { mode = m > 2 ? 1 : m; }          // noise reduction: 0 off (light rumble filter), 1 soft, 2 strong
  bool loop() override { push(); return true; }
  void flush() override { push(); }
  bool stop() override { close(); return true; }
 private:
  void applyMode() {
    const float fs = hertz > 0 ? hertz : 44100;
    static const float HP[3] = {60.0f, 150.0f, 300.0f}, LP[3] = {12000.0f, 9000.0f, 4500.0f};
    const uint8_t md = mode > 2 ? 1 : mode;
    hp.design(true, HP[md], fs); lp.design(false, LP[md], fs);
    gate = md == 2; env = 0; g = 1.0f; applied = mode;
  }
  void push() {                                              // short, non-blocking write to the DAC
    if (!handle || !n) return;
    size_t done = 0; dac_continuous_write(handle, buf, n, &done, 3);
    if (done >= n) n = 0; else if (done) { memmove(buf, buf + done, n - done); n -= done; }
  }
  void close() { if (handle) { dac_continuous_disable(handle); dac_continuous_del_channels(handle); handle = nullptr; } n = 0; }
  dac_continuous_handle_t handle = nullptr;
  uint8_t buf[1024]; size_t n = 0; int up = 1; float prev = 0; volatile float volume = 0.0f, ditherScale = 1.0f; uint32_t rng = 2463534242u;
  Biquad hp, lp, i1, i2;
  volatile uint8_t mode = 1; uint8_t applied = 255; bool gate = false; float env = 0, g = 1.0f;
};

DacOutput* out = nullptr;
AudioGeneratorMP3* dec = nullptr;
AudioFileSourceBuffer* buffer = nullptr;
void* decoderMemory = nullptr;                              // MP3 decoder work memory: taken in one piece, before anything else, to avoid fragmentation
volatile bool want = false, restartNow = false, stopped = true;   // stopped: the decoder is gone and the DAC released
volatile int station = 0;
volatile uint32_t stopAskedAt = 0;
volatile uint8_t state = RADIO_OFF;
uint32_t lastTry = 0;

float gainFor(int level) {                                  // 1 -> 0.04 ... 10 -> 1.0, logarithmic steps
  if (level <= 0) return 0.0f;
  if (level > 10) level = 10;
  return 0.04f * powf(25.0f, (level - 1) / 9.0f);
}

// Wraps the library's HTTP source. The library waits for data in a tight loop that gives the processor back to nobody
// and that STOP cannot interrupt: when a station stalls, the radio task stays stuck for minutes (the alarm then finds
// it busy). This wrapper only ever asks the library for what has already arrived, sleeps while waiting, and gives up
// the instant STOP (or another station) is pressed.
class GuardedStream : public AudioFileSource {
 public:
  bool open(const char* url) override { total = 0; lastData = millis(); return inner.open(url); }
  uint32_t read(void* data, uint32_t len) override {
    uint8_t* p = (uint8_t*)data; uint32_t got = 0;
    const uint32_t t0 = millis(), enough = len < 1024 ? len : 1024;
    for (;;) {
      got += inner.readNonBlock(p + got, len - got);
      if (got >= enough || got >= len) break;                // enough to keep the decoder going
      if (got > 0 && millis() - t0 > 400) break;             // a trickle: give it what there is
      if (!want || restartNow || millis() - t0 > 3000) break; // STOP / new station, or nothing for 3 s
      if (!inner.isOpen()) break;                            // the station hung up
      vTaskDelay(2);
    }
    total += got; if (got) lastData = millis();
    return got;
  }
  uint32_t readNonBlock(void* data, uint32_t len) override { const uint32_t n = inner.readNonBlock(data, len); total += n; if (n) lastData = millis(); return n; }
  bool seek(int32_t pos, int dir) override { return inner.seek(pos, dir); }
  bool close() override { return inner.close(); }
  bool isOpen() override { return inner.isOpen(); }
  uint32_t getSize() override { return inner.getSize(); }
  uint32_t getPos() override { return inner.getPos(); }
  uint32_t total = 0, lastData = 0;
 private:
  AudioFileSourceHTTPStream inner;
};
GuardedStream* stream = nullptr;
void stopDecoder() {
  if (dec) { dec->stop(); delete dec; dec = nullptr; }
  if (buffer) { delete buffer; buffer = nullptr; }
  if (stream) { delete stream; stream = nullptr; }
  if (decoderMemory) { free(decoderMemory); decoderMemory = nullptr; }
}
bool startDecoder() {
  stopDecoder();
  Serial.printf("RADIO start: Wi-Fi %s, free RAM %u, largest block %u\n", WiFi.status() == WL_CONNECTED ? "ok" : "OFFLINE", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  if (WiFi.status() != WL_CONNECTED) return false;
  char url[RADIO_URL_MAX + 1]; strlcpy(url, RADIOS[station].url, sizeof url);   // own copy: the web page may rewrite the list at any time
  // Memory is taken biggest first: decoder, then the buffer, then the connection.
  decoderMemory = malloc(AudioGeneratorMP3::preAllocSize());
  if (!decoderMemory) { Serial.printf("RADIO failed: no memory for the decoder (%d bytes)\n", (int)AudioGeneratorMP3::preAllocSize()); stopDecoder(); return false; }
  Serial.printf("RADIO decoder memory taken: free RAM %u, largest block %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  if (!want || restartNow) { stopDecoder(); return false; }   // STOP or another station pressed while preparing
  stream = new GuardedStream();                  // plain stream: no ICY metadata mixed into the audio
  if (!stream->open(url)) { Serial.println("RADIO failed: the stream does not answer (address, DNS or memory)"); stopDecoder(); return false; }
  if (!want || restartNow) { stopDecoder(); return false; }   // STOP or another station pressed while connecting
  buffer = new AudioFileSourceBuffer(stream, RADIO_BUFFER);
  dec = new AudioGeneratorMP3(decoderMemory, AudioGeneratorMP3::preAllocSize());
  if (!dec->begin(buffer, out)) { Serial.println("RADIO failed: decoder start"); stopDecoder(); return false; }
  Serial.printf("RADIO connected: free RAM %u, largest block %u\n", (unsigned)ESP.getFreeHeap(), (unsigned)ESP.getMaxAllocHeap());
  return true;
}

void audioTask(void*) {
  uint32_t lastReport = 0;
  for (;;) {
    if (state == RADIO_PLAYING && stream && millis() - lastReport > 5000) {
      lastReport = millis();
      Serial.printf("RADIO alive: %u KB received, last data %u ms ago, free RAM %u\n", (unsigned)(stream->total / 1024), (unsigned)(millis() - stream->lastData), (unsigned)ESP.getFreeHeap());
    }
    if (restartNow) { restartNow = false; Serial.printf("RADIO task: start request seen (want=%d)\n", (int)want); stopDecoder(); lastTry = 0; }
    if (!want) {
      if (dec || stream) stopDecoder();
      state = RADIO_OFF;
      if (!stopped) { stopped = true; Serial.printf("RADIO released %u ms after STOP\n", (unsigned)(millis() - stopAskedAt)); }
      vTaskDelay(pdMS_TO_TICKS(30)); continue;
    }
    if (!dec) {                                              // not running: (re)connect, at most every 4 s
      if (millis() - lastTry > 4000 || lastTry == 0) {
        lastTry = millis(); state = RADIO_CONNECTING;
        const bool ok = startDecoder();
        state = !want ? RADIO_OFF : (ok ? RADIO_PLAYING : RADIO_ERROR);   // never bring the state back after STOP
      }
      vTaskDelay(pdMS_TO_TICKS(50)); continue;
    }
    if (!dec->isRunning() || !dec->loop()) {
      if (!want) continue;                                   // STOP pressed: the top of the loop cleans up
      Serial.println("RADIO stream ended, reconnecting"); stopDecoder(); lastTry = millis(); if (want) state = RADIO_ERROR; continue;
    }
    vTaskDelay(1);
  }
}
}  // namespace

void radioBegin() {
  esp_log_level_set("dac_continuous", ESP_LOG_NONE);        // a full DMA buffer is normal: do not print it (printing starves the audio)
  if (out) return;
  audioLogger = &Serial;                                    // the audio library explains its own errors on the Serial Monitor
  out = new DacOutput();
  out->SetVolume(gainFor(4));
  xTaskCreatePinnedToCore(audioTask, "radio", 10240, nullptr, 3, nullptr, 0);
}
void radioPlay(int s, uint8_t volume) {
  if (!out) return;
  station = (s < 0 || s >= RADIO_COUNT) ? 0 : s;
  out->SetVolume(gainFor(volume));
  Serial.printf("RADIO play requested: station %d, volume %d, task %s\n", (int)station, (int)volume, stopped ? "idle" : "busy");
  stopped = false; want = true; restartNow = true; state = RADIO_CONNECTING;
}
void radioStop() {                                          // returns once the DAC is released (at most ~0.4 s), so a beep can follow at once
  if (out) out->SetVolume(0.0f);                             // silence at once: the task may take a while to let go of the network
  stopAskedAt = millis();
  stopped = false; want = false; state = RADIO_OFF;           // 'stopped' is cleared first, so the wait below is real
  for (int i = 0; i < 80 && !stopped; ++i) delay(5);
  Serial.printf("RADIO stop: %s\n", stopped ? "released" : "task still busy (it will release by itself)");
}
void radioSetDither(uint8_t level) { if (out) out->SetDither(level); }
void radioSetNoise(uint8_t level) { if (out) out->SetMode(level); }
void radioSetVolume(uint8_t volume) { if (out && want) out->SetVolume(gainFor(volume)); }   // never un-mute a stopped radio
RadioState radioState() { return (RadioState)state; }
