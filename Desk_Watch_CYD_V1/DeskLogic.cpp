#include "DeskLogic.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

uint8_t pomoWorkMinutes(int index) {
  if (index < 0) index = 0;
  if (index > POMO_WORK_STEPS - 1) index = POMO_WORK_STEPS - 1;
  return uint8_t(index < 60 ? index + 1 : 60 + (index - 59) * 5);
}
int pomoWorkIndex(uint8_t minutes) {
  int best = 0, bestDistance = 1000;
  for (int i = 0; i < POMO_WORK_STEPS; ++i) {
    const int d = abs(int(pomoWorkMinutes(i)) - int(minutes));
    if (d < bestDistance) { best = i; bestDistance = d; }
  }
  return best;
}

uint32_t Pomodoro::phaseMs(PomoPhase p) const {
  return (p == POMO_WORK ? workMin : p == POMO_SHORT ? POMO_SHORT_BREAK_MIN : POMO_LONG_BREAK_MIN) * 60000UL;
}
bool Pomodoro::idle() const {
  return !running && phase == POMO_WORK && sessions == 0 && remainingMs == phaseMs(POMO_WORK);
}
void Pomodoro::reset() { phase = POMO_WORK; sessions = 0; running = false; remainingMs = phaseMs(POMO_WORK); }
void Pomodoro::changeWork(int steps) {
  if (!idle()) return;
  workMin = pomoWorkMinutes(pomoWorkIndex(workMin) + steps);
  remainingMs = phaseMs(POMO_WORK);
}
void Pomodoro::startPause() { running = !running; }
PomoEvent Pomodoro::tick(uint32_t elapsedMs) {
  if (!running) return POMO_NONE;
  if (elapsedMs < remainingMs) { remainingMs -= elapsedMs; return POMO_NONE; }
  PomoEvent event;
  if (phase == POMO_WORK) {
    ++sessions;
    phase = (sessions % POMO_LONG_EVERY == 0) ? POMO_LONG : POMO_SHORT;
    event = POMO_WORK_DONE;
  } else {
    phase = POMO_WORK;
    event = POMO_BREAK_DONE;
  }
  remainingMs = phaseMs(phase);
  return event;
}

void Stopwatch::toggle(uint32_t nowMs) {
  if (running) { accumulated = nowMs - startMs; running = false; }
  else { startMs = nowMs - accumulated; running = true; }
}

bool Counter::increment() { if (!active || value >= COUNTER_MAX) return false; ++value; return true; }
bool Counter::decrement() { if (!active || value == 0) return false; --value; return true; }

void AlarmClock::adjustHour(int delta) { hour = uint8_t(((hour + delta) % 24 + 24) % 24); lastFiredMinute = -1; snoozing = false; }
void AlarmClock::adjustMinute(int delta) { minute = uint8_t(((minute + delta) % 60 + 60) % 60); lastFiredMinute = -1; snoozing = false; }
void AlarmClock::setArmed(bool on) { armed = on; lastFiredMinute = -1; if (!on) snoozing = false; }
bool AlarmClock::update(int nowHour, int nowMinute, bool clockValid, uint32_t nowMs) {
  if (ringing || !clockValid) return false;      // no alarm without a reliable clock
  const int minuteOfDay = nowHour * 60 + nowMinute;
  if (!armed) snoozing = false;
  if (snoozing) {
    if (int32_t(nowMs - snoozeUntilMs) >= 0) { ringing = true; snoozing = false; ringStartMs = nowMs; return true; }
    return false;
  }
  if (armed && nowHour == hour && nowMinute == minute && lastFiredMinute != minuteOfDay) {
    lastFiredMinute = minuteOfDay; ringing = true; ringStartMs = nowMs; return true;
  }
  return false;
}
bool AlarmClock::ringTimeout(uint32_t nowMs) {
  if (!ringing || nowMs - ringStartMs < ALARM_RING_MAX_MS) return false;
  snooze(nowMs); return true;
}
void AlarmClock::snooze(uint32_t nowMs) { ringing = false; snoozing = true; snoozeUntilMs = nowMs + ALARM_SNOOZE_MS; }
void AlarmClock::stop() { ringing = false; snoozing = false; }

// Original alarm themes of the Desk Watch (Hz / 10 ms units).
static const uint16_t AURORA_NOTE[] = {
  523, 659, 784, 1047, 988, 784, 659, 0,  587, 740, 880, 1175, 1047, 880, 740, 0,
  659, 784, 1047, 1319, 1175, 988, 1047, 0 };
static const uint8_t AURORA_LEN[] = {
  18, 18, 18, 36, 18, 18, 36, 12,  18, 18, 18, 36, 18, 18, 36, 12,  18, 18, 24, 36, 18, 18, 72, 24 };
static const uint16_t COSMIC_NOTE[] = {
  440, 0, 880, 659, 0, 523, 659, 0,  440, 0, 988, 784, 0, 659, 523, 0,
  587, 0, 1175, 880, 0, 698, 880, 0,  1047, 988, 880, 784, 659, 523, 440, 0 };
static const uint8_t COSMIC_LEN[] = {
  12, 6, 24, 12, 6, 12, 24, 12,  12, 6, 24, 12, 6, 12, 24, 12,
  12, 6, 24, 12, 6, 12, 24, 12,  12, 12, 12, 12, 18, 18, 48, 24 };
static const uint16_t ARCADE_NOTE[] = {
  784, 0, 784, 988, 1175, 0, 1568, 1175,  1319, 0, 1175, 988, 1047, 0, 784, 0,
  880, 1047, 1319, 0, 1175, 988, 1568, 0,  1319, 1175, 988, 1175, 1568, 0, 1568, 0 };
static const uint8_t ARCADE_LEN[] = {
  12, 6, 12, 12, 24, 6, 18, 12,  24, 6, 12, 12, 24, 6, 24, 12,
  12, 12, 24, 6, 12, 12, 24, 12,  12, 12, 12, 12, 36, 12, 48, 24 };
static const uint16_t EPIC_NOTE[] = {
  392, 0, 392, 523, 659, 0, 784, 659,  587, 0, 587, 740, 880, 0, 1175, 880,
  659, 784, 988, 1175, 784, 988, 1175, 1397,  1047, 0, 1319, 0, 1568, 1319, 1568, 0 };
static const uint8_t EPIC_LEN[] = {
  27, 9, 12, 24, 36, 12, 36, 18,  27, 9, 12, 24, 36, 12, 36, 18,
  12, 12, 12, 24, 12, 12, 12, 24,  36, 12, 36, 12, 36, 18, 72, 24 };
static_assert(sizeof(AURORA_NOTE) / sizeof(uint16_t) == sizeof(AURORA_LEN), "Aurora length mismatch");
static_assert(sizeof(COSMIC_NOTE) / sizeof(uint16_t) == sizeof(COSMIC_LEN), "Cosmic length mismatch");
static_assert(sizeof(ARCADE_NOTE) / sizeof(uint16_t) == sizeof(ARCADE_LEN), "Arcade length mismatch");
static_assert(sizeof(EPIC_NOTE) / sizeof(uint16_t) == sizeof(EPIC_LEN), "Epic length mismatch");
const Tune TUNES[TUNE_COUNT] = {
  {"Aurora", AURORA_NOTE, AURORA_LEN, sizeof(AURORA_LEN)},
  {"Cosmic Pulse", COSMIC_NOTE, COSMIC_LEN, sizeof(COSMIC_LEN)},
  {"Arcade Hero", ARCADE_NOTE, ARCADE_LEN, sizeof(ARCADE_LEN)},
  {"Epic Rise", EPIC_NOTE, EPIC_LEN, sizeof(EPIC_LEN)},
};
static const uint16_t WORK_DONE_NOTE[] = {880, 0, 880, 0, 880, 0};
static const uint8_t WORK_DONE_LEN[] = {15, 8, 15, 8, 15, 8};
static const uint16_t BREAK_DONE_NOTE[] = {660, 0, 660, 0};
static const uint8_t BREAK_DONE_LEN[] = {30, 10, 30, 10};
static const uint16_t PREVIEW_NOTE[] = {1175};
static const uint8_t PREVIEW_LEN[] = {10};
const Tune BEEP_WORK_DONE = {"work done", WORK_DONE_NOTE, WORK_DONE_LEN, 6};
const Tune BEEP_BREAK_DONE = {"break done", BREAK_DONE_NOTE, BREAK_DONE_LEN, 4};
const Tune BEEP_PREVIEW_OK = {"ok", PREVIEW_NOTE, PREVIEW_LEN, 1};
static const uint16_t CLIMATE_NOTE[] = {2000, 0, 2000, 0, 2000, 0};
static const uint8_t CLIMATE_LEN[] = {12, 12, 12, 12, 12, 12};
const Tune BEEP_CLIMATE = {"climate", CLIMATE_NOTE, CLIMATE_LEN, 6};

RadioStation RADIOS[RADIO_COUNT] = {                    // factory list (Radios.h); the user's choice replaces it at start
  {RADIO1_NAME, RADIO1_URL}, {RADIO2_NAME, RADIO2_URL}, {RADIO3_NAME, RADIO3_URL},
};
bool radioValid(const char* name, const char* url) {
  if (!name || !url) return false;
  const size_t n = strlen(name), u = strlen(url);
  if (n < 1 || n > RADIO_NAME_MAX || u > RADIO_URL_MAX || !radioHttp(url)) return false;
  for (size_t i = 0; i < n; ++i) if (name[i] < 0x20 || name[i] > 0x7E || name[i] == '"' || name[i] == '\\') return false;
  if (name[0] == ' ' || name[n - 1] == ' ') return false;
  for (size_t i = 0; i < u; ++i) if (url[i] <= 0x20 || url[i] > 0x7E || url[i] == '"' || url[i] == '\\') return false;
  return true;
}
bool radioSet(int index, const char* name, const char* url) {
  if (index < 0 || index >= RADIO_COUNT || !radioValid(name, url)) return false;
  snprintf(RADIOS[index].name, sizeof RADIOS[index].name, "%s", name); snprintf(RADIOS[index].url, sizeof RADIOS[index].url, "%s", url);
  return true;
}
void radioDefaults() {
  radioSet(0, RADIO1_NAME, RADIO1_URL); radioSet(1, RADIO2_NAME, RADIO2_URL); radioSet(2, RADIO3_NAME, RADIO3_URL);
}
const char* alarmSoundName(int sound) {
  if (sound >= 0 && sound < TUNE_COUNT) return TUNES[sound].name;
  if (alarmSoundIsRadio(sound)) return RADIOS[alarmSoundStation(sound)].name;
  return TUNES[0].name;
}
bool RadioWatch::update(uint32_t nowMs, bool playing) {
  if (fallback) return false;
  if (playing) { lastGoodMs = nowMs; return false; }
  // Signed difference: the caller's 'now' can be a few ms older than the moment start() was called (same loop turn), which
  // used to wrap around to a huge number and made the melody take over at once.
  if ((int32_t)(nowMs - lastGoodMs) >= (int32_t)RADIO_FALLBACK_MS) { fallback = true; return true; }
  return false;
}

// Typical CYD panel: raw 200..3800 on both axes with the XPT2046 library in rotation 1.
TouchCal defaultTouchCal() {
  return {319.0f / 3600, 0, -200 * 319.0f / 3600, 0, 239.0f / 3600, -200 * 239.0f / 3600};
}
bool fitTouchCal(const int raw[3][2], const int screen[3][2], TouchCal& out) {
  // Solve [rx ry 1] * [a b c]^T = target for x and y with Cramer's rule.
  const float det = float(raw[0][0]) * (raw[1][1] - raw[2][1]) - float(raw[0][1]) * (raw[1][0] - raw[2][0]) +
                    float(raw[1][0]) * raw[2][1] - float(raw[2][0]) * raw[1][1];
  if (fabsf(det) < 100000.0f) return false;           // points almost aligned
  auto solve = [&](int axis, float& a, float& b, float& c) {
    const float t0 = float(screen[0][axis]), t1 = float(screen[1][axis]), t2 = float(screen[2][axis]);
    a = (t0 * (raw[1][1] - raw[2][1]) - raw[0][1] * (t1 - t2) + (t1 * raw[2][1] - t2 * raw[1][1])) / det;
    b = (raw[0][0] * (t1 - t2) - t0 * (raw[1][0] - raw[2][0]) + (raw[1][0] * t2 - raw[2][0] * t1)) / det;
    c = (raw[0][0] * (raw[1][1] * t2 - raw[2][1] * t1) - raw[0][1] * (raw[1][0] * t2 - raw[2][0] * t1) +
         t0 * (raw[1][0] * raw[2][1] - raw[2][0] * raw[1][1])) / det;
  };
  TouchCal r;
  solve(0, r.xx, r.xy, r.xc); solve(1, r.yx, r.yy, r.yc);
  // Reject results that are clearly not a screen mapping (wrong corners touched, swapped order...).
  if (fabsf(r.xx) + fabsf(r.xy) < 0.02f || fabsf(r.yx) + fabsf(r.yy) < 0.02f) return false;
  if (fabsf(r.xx) + fabsf(r.xy) > 0.5f || fabsf(r.yx) + fabsf(r.yy) > 0.5f) return false;
  out = r; return true;
}
void mapTouch(const TouchCal& c, int rx, int ry, int& x, int& y) {
  const float fx = c.xx * rx + c.xy * ry + c.xc, fy = c.yx * rx + c.yy * ry + c.yc;
  x = fx < 0 ? 0 : fx > 319 ? 319 : int(fx + 0.5f);
  y = fy < 0 ? 0 : fy > 239 ? 239 : int(fy + 0.5f);
}

uint8_t SleepTimer::fadeLevel(uint32_t nowMs, uint8_t volume) const {
  const uint32_t left = remaining(nowMs);
  if (!active || left >= SLEEP_FADE_MS || volume <= 1) return volume;
  const uint32_t v = (uint32_t(volume) * left + SLEEP_FADE_MS / 2) / SLEEP_FADE_MS;
  return v < 1 ? 1 : uint8_t(v);
}
bool screenShouldSleep(uint8_t sleepIndex, uint32_t idleMs) {
  if (sleepIndex == 0 || sleepIndex >= SCREEN_SLEEP_COUNT) return false;
  return idleMs >= uint32_t(SCREEN_SLEEP_MIN[sleepIndex]) * 60000UL;
}
int wrapPage(int page) { return (page % PAGE_TOTAL + PAGE_TOTAL) % PAGE_TOTAL; }
int row2Page(int index) { static const int order[ROW2_COUNT] = {PAGE_CLIMATE, PAGE_WEATHER, PAGE_RADIO, PAGE_SLEEP, PAGE_SETUP}; return order[index < 0 ? 0 : index >= ROW2_COUNT ? ROW2_COUNT - 1 : index]; }
int pageRow(int page) { return page >= PAGE_SETUP ? 1 : 0; }
int footerTarget(int row, int x) {
  int slot = x * FOOTER_SLOTS / 320; slot = slot < 0 ? 0 : slot >= FOOTER_SLOTS ? FOOTER_SLOTS - 1 : slot;
  if (row == 0) return slot < PAGE_SETUP ? slot : FOOT_DOWN;
  if (slot < ROW2_COUNT) return row2Page(slot);
  return slot == FOOTER_SLOTS - 1 ? FOOT_UP : FOOT_NONE;
}
int pageAfterSwipe(int current, int dx, int dy) {
  if (!(abs(dx) >= 40 && abs(dx) > abs(dy))) return current;
  const int step = dx < 0 ? 1 : -1;
  if (pageRow(current) == 1) {
    int i = 0; while (i < ROW2_COUNT - 1 && row2Page(i) != current) ++i;
    return row2Page((i + step + ROW2_COUNT) % ROW2_COUNT);
  }
  const int n = PAGE_SETUP;                                                                // first row: Clock..Notes
  return ((current + step) % n + n) % n;
}

bool decodeDht22(const uint8_t b[5], float& tempC, float& humPct) {
  if (uint8_t(b[0] + b[1] + b[2] + b[3]) != b[4]) return false;
  const float h = ((b[0] << 8) | b[1]) * 0.1f;
  float t = (((b[2] & 0x7F) << 8) | b[3]) * 0.1f;
  if (b[2] & 0x80) t = -t;
  if (h < 0.0f || h > 100.0f || t < -40.0f || t > 80.0f) return false;
  tempC = t; humPct = h; return true;
}
bool ClimateGuard::update(float t, float h, int tempMax, int humMax) {
  bool started = false;
  if (!tempHigh && t >= tempMax) { tempHigh = true; started = true; }
  else if (tempHigh && t < tempMax - CLIMATE_HYSTERESIS_T) tempHigh = false;
  if (!humHigh && h >= humMax) { humHigh = true; started = true; }
  else if (humHigh && h < humMax - CLIMATE_HYSTERESIS_H) humHigh = false;
  return started;
}

size_t formatTimer(char* out, size_t cap, uint32_t remainingMs) {
  const uint32_t s = (remainingMs + 999) / 1000;
  if (s >= 3600) return snprintf(out, cap, "%u:%02u:%02u", unsigned(s / 3600), unsigned(s / 60 % 60), unsigned(s % 60));
  return snprintf(out, cap, "%02u:%02u", unsigned(s / 60), unsigned(s % 60));
}
size_t formatStopwatch(char* out, size_t cap, uint32_t ms) {
  const uint32_t h = ms / 3600000UL, m = ms / 60000UL % 60, s = ms / 1000UL % 60;
  if (h > 0) return snprintf(out, cap, "%u:%02u:%02u", unsigned(h % 100), unsigned(m), unsigned(s));
  return snprintf(out, cap, "%02u:%02u.%u", unsigned(m), unsigned(s), unsigned(ms / 100UL % 10));
}
size_t formatWorkDuration(char* out, size_t cap, uint8_t minutes) {
  if (minutes < 60) return snprintf(out, cap, "%u min", unsigned(minutes));
  return snprintf(out, cap, "%u h %02u", unsigned(minutes / 60), unsigned(minutes % 60));
}
bool isNight(int hour) { return hour >= 22 || hour < 6; }
