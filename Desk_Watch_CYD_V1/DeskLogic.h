#pragma once
// Desk Watch logic without any hardware access, so it can be unit-tested on a PC.
// Behaviour follows the Desk Watch JOY STUDENT edition (Arduino UNO) by Pr. BELAMINE MakerLab.
#include <stddef.h>
#include <stdint.h>

// ---------- Pomodoro timer ----------
// Work durations: 1..60 min in 1 min steps, then 65 min..4 h in 5 min steps (96 values).
constexpr int POMO_WORK_STEPS = 96;
constexpr uint8_t POMO_SHORT_BREAK_MIN = 5, POMO_LONG_BREAK_MIN = 15, POMO_LONG_EVERY = 4;
uint8_t pomoWorkMinutes(int index);       // index is clamped to 0..95
int pomoWorkIndex(uint8_t minutes);       // nearest index

enum PomoPhase : uint8_t { POMO_WORK, POMO_SHORT, POMO_LONG };
enum PomoEvent : uint8_t { POMO_NONE, POMO_WORK_DONE, POMO_BREAK_DONE };

struct Pomodoro {
  uint8_t workMin = 25;
  PomoPhase phase = POMO_WORK;
  uint8_t sessions = 0;                   // finished work sessions
  bool running = false;
  uint32_t remainingMs = 25UL * 60000UL;

  uint32_t phaseMs(PomoPhase p) const;
  bool idle() const;                      // stopped, untouched, at the start of a work session
  void reset();
  void changeWork(int steps);             // only when idle; clamps at both ends
  void startPause();
  PomoEvent tick(uint32_t elapsedMs);     // call with the time since the previous call
};

// ---------- Stopwatch ----------
struct Stopwatch {
  bool running = false;
  uint32_t startMs = 0, accumulated = 0;
  uint32_t elapsed(uint32_t nowMs) const { return running ? nowMs - startMs : accumulated; }
  bool idle() const { return !running && accumulated == 0; }
  void toggle(uint32_t nowMs);
  void reset() { running = false; accumulated = 0; }
};

// ---------- Tally counter ----------
constexpr uint16_t COUNTER_MAX = 9999;
struct Counter {
  uint16_t value = 0;
  bool active = true;                     // "PAUSE" locks the counter against accidental touches
  bool increment();                       // false if locked or at the maximum
  bool decrement();                       // false if locked or already zero
  void reset() { value = 0; }
};

// ---------- Alarm clock ----------
constexpr uint32_t ALARM_SNOOZE_MS = 5UL * 60000UL, ALARM_RING_MAX_MS = 120UL * 1000UL;
struct AlarmClock {
  uint8_t hour = 7, minute = 0;
  bool armed = false, ringing = false, snoozing = false;
  uint32_t ringStartMs = 0, snoozeUntilMs = 0;
  int lastFiredMinute = -1;

  void adjustHour(int delta);             // wraps 0..23
  void adjustMinute(int delta);           // wraps 0..59
  void setArmed(bool on);
  bool update(int nowHour, int nowMinute, bool clockValid, uint32_t nowMs);  // true when ringing starts
  bool ringTimeout(uint32_t nowMs);       // after 2 min without reaction: snooze, returns true
  void snooze(uint32_t nowMs);
  void stop();
};

// ---------- Melodies (original tunes; frequency in Hz, length in 10 ms units, 0 Hz = rest) ----------
struct Tune { const char* name; const uint16_t* notes; const uint8_t* lengths; uint8_t count; };
constexpr int TUNE_COUNT = 4;
extern const Tune TUNES[TUNE_COUNT];
extern const Tune BEEP_WORK_DONE, BEEP_BREAK_DONE, BEEP_PREVIEW_OK, BEEP_CLIMATE;

// ---------- Web radio ----------
// Plain HTTP MP3 streams (no TLS: light on memory). Three radio slots. The factory list is in Radios.h; the user changes it
// from the watch's web page (http://cyd.local/radios), and the changes are saved in flash.
#include "Radios.h"
constexpr int RADIO_COUNT = 3;
constexpr int RADIO_NAME_MAX = 16, RADIO_URL_MAX = 159;
struct RadioStation { char name[RADIO_NAME_MAX + 1]; char url[RADIO_URL_MAX + 1]; };
constexpr int radioLen(const char* s) { int n = 0; while (s[n]) ++n; return n; }
constexpr bool radioHttp(const char* u) { return u[0] == 'h' && u[1] == 't' && u[2] == 't' && u[3] == 'p' && u[4] == ':' && u[5] == '/' && u[6] == '/' && u[7] != 0; }
constexpr bool radioDefaultOk(const char* name, const char* url) { return radioLen(name) >= 1 && radioLen(name) <= RADIO_NAME_MAX && radioHttp(url) && radioLen(url) <= RADIO_URL_MAX; }
static_assert(radioDefaultOk(RADIO1_NAME, RADIO1_URL) && radioDefaultOk(RADIO2_NAME, RADIO2_URL) && radioDefaultOk(RADIO3_NAME, RADIO3_URL),
              "Radios.h: each radio needs a name (1 to 16 characters) and an address starting with http://");
extern RadioStation RADIOS[RADIO_COUNT];                       // the radios in use (factory list, then what the user saved)
bool radioValid(const char* name, const char* url);            // name: 1..16 printable ASCII without quotes; url: http://..., no space, at most 159 characters
bool radioSet(int index, const char* name, const char* url);   // false (nothing changed) if invalid
void radioDefaults();                                          // back to the list of Radios.h
enum RadioState : uint8_t { RADIO_OFF, RADIO_CONNECTING, RADIO_PLAYING, RADIO_ERROR };
// Alarm sounds: the 4 melodies, then the web radios.
constexpr int ALARM_SOUND_COUNT = TUNE_COUNT + RADIO_COUNT;
inline bool alarmSoundIsRadio(int sound) { return sound >= TUNE_COUNT && sound < ALARM_SOUND_COUNT; }
inline int alarmSoundStation(int sound) { return sound - TUNE_COUNT; }
const char* alarmSoundName(int sound);
// When the alarm rings with a radio, the normal melody takes over if the radio is not playing within RADIO_FALLBACK_MS
// (and again if it stays silent that long once it was playing). A late alarm is better than none.
constexpr uint32_t RADIO_FALLBACK_MS = 20000;
struct RadioWatch {
  bool fallback = false;
  uint32_t lastGoodMs = 0;
  void start(uint32_t nowMs) { fallback = false; lastGoodMs = nowMs; }
  bool update(uint32_t nowMs, bool playing);   // true once, when the melody must take over
};

// ---------- Sleep: screen standby and radio sleep timer ----------
constexpr int SCREEN_SLEEP_COUNT = 6, SLEEP_TIMER_COUNT = 6;
constexpr uint8_t SCREEN_SLEEP_MIN[SCREEN_SLEEP_COUNT] = {0, 1, 2, 5, 10, 30};         // minutes of inactivity before the screen goes off (0 = never)
constexpr uint8_t SLEEP_TIMER_MIN[SLEEP_TIMER_COUNT] = {10, 15, 30, 45, 60, 90};       // minutes of radio before it stops by itself
constexpr uint32_t SLEEP_FADE_MS = 30000;                                              // the radio fades out over the last 30 s
struct SleepTimer {
  bool active = false;
  uint32_t endMs = 0;
  void start(uint32_t nowMs, uint8_t minutes) { active = true; endMs = nowMs + uint32_t(minutes) * 60000UL; }
  void cancel() { active = false; }
  uint32_t remaining(uint32_t nowMs) const { return !active ? 0 : int32_t(endMs - nowMs) > 0 ? endMs - nowMs : 0; }
  bool update(uint32_t nowMs) { if (active && remaining(nowMs) == 0) { active = false; return true; } return false; }   // true once, when the time is up
  uint8_t fadeLevel(uint32_t nowMs, uint8_t volume) const;     // volume to use now: full, then falling to 1 during the last 30 s
};
bool screenShouldSleep(uint8_t sleepIndex, uint32_t idleMs);   // true when the idle time reached the chosen delay (never when "Off")

// ---------- Touch ----------
struct TouchCal { float xx, xy, xc, yx, yy, yc; };   // screen = M * raw + c
TouchCal defaultTouchCal();
// Exact affine fit through three non-collinear points; false if they are (nearly) collinear.
bool fitTouchCal(const int raw[3][2], const int screen[3][2], TouchCal& out);
void mapTouch(const TouchCal& cal, int rawX, int rawY, int& x, int& y);   // clamped to 320x240

struct Rect { int16_t x, y, w, h; };
inline bool inside(const Rect& r, int px, int py) { return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; }

// ---------- Navigation ----------
// Pages 0..5 are the first row of apps (Clock..Notes); Setup and Climate form the second row,
// reached with the down arrow that replaces the old "Setup" tab. The arrows sit in the last footer slot.
constexpr int PAGE_TOTAL = 11;              // Clock, Timer, Chrono, Count, Alarm, Notes, Setup, Climate, Weather, Radio, Sleep
constexpr int PAGE_SETUP = 6, PAGE_CLIMATE = 7, PAGE_WEATHER = 8, PAGE_RADIO = 9, PAGE_SLEEP = 10;
constexpr int ROW2_COUNT = 5;               // second row, left to right: Climate, Weather, Radio, Sleep, Setup
int row2Page(int index);
constexpr int FOOTER_SLOTS = 7;             // footer width is split in 7 equal slots on both rows
constexpr int FOOT_NONE = -1, FOOT_DOWN = 100, FOOT_UP = 101;
int pageRow(int page);                      // 0 = first row of apps, 1 = second row
int footerTarget(int row, int x);           // page number, FOOT_DOWN / FOOT_UP for the arrow, or FOOT_NONE (empty slot)
int pageAfterSwipe(int current, int dx, int dy);   // horizontal swipe >= 40 px moves inside the current row; otherwise current
int wrapPage(int page);

// ---------- Temperature and humidity (DHT22) ----------
constexpr float CLIMATE_HYSTERESIS_T = 1.0f, CLIMATE_HYSTERESIS_H = 3.0f;
constexpr int TEMP_ALERT_MIN = 15, TEMP_ALERT_MAX = 50, HUM_ALERT_MIN = 30, HUM_ALERT_MAX = 95;
// Decodes the 5 bytes sent by a DHT22; false when the checksum or the range is wrong.
bool decodeDht22(const uint8_t b[5], float& tempC, float& humPct);
// Alert with hysteresis: update() returns true once when a threshold is crossed upwards.
struct ClimateGuard {
  bool tempHigh = false, humHigh = false;
  bool update(float tempC, float humPct, int tempMax, int humMax);
  bool active() const { return tempHigh || humHigh; }
  void clear() { tempHigh = humHigh = false; }
};

// ---------- Formatting ----------
// "MM:SS" or "H:MM:SS" (timer), "MM:SS.t" (stopwatch), "HH:MM:SS" (clock). Return the length.
size_t formatTimer(char* out, size_t cap, uint32_t remainingMs);
size_t formatStopwatch(char* out, size_t cap, uint32_t ms);
size_t formatWorkDuration(char* out, size_t cap, uint8_t minutes);   // "25 min", "1 h 05"
bool isNight(int hour);                     // 22:00 .. 05:59
