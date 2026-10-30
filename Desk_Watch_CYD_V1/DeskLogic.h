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
extern const Tune BEEP_WORK_DONE, BEEP_BREAK_DONE, BEEP_PREVIEW_OK;

// ---------- Touch ----------
struct TouchCal { float xx, xy, xc, yx, yy, yc; };   // screen = M * raw + c
TouchCal defaultTouchCal();
// Exact affine fit through three non-collinear points; false if they are (nearly) collinear.
bool fitTouchCal(const int raw[3][2], const int screen[3][2], TouchCal& out);
void mapTouch(const TouchCal& cal, int rawX, int rawY, int& x, int& y);   // clamped to 320x240

struct Rect { int16_t x, y, w, h; };
inline bool inside(const Rect& r, int px, int py) { return px >= r.x && px < r.x + r.w && py >= r.y && py < r.y + r.h; }

// ---------- Navigation ----------
constexpr int PAGE_TOTAL = 7;               // Clock, Timer, Chrono, Count, Alarm, Notes, Setup
int pageFromFooter(int x);                  // 0..6 for a tap on the footer bar
int pageAfterSwipe(int current, int dx, int dy);   // horizontal swipe >= 40 px; otherwise current
int wrapPage(int page);

// ---------- Formatting ----------
// "MM:SS" or "H:MM:SS" (timer), "MM:SS.t" (stopwatch), "HH:MM:SS" (clock). Return the length.
size_t formatTimer(char* out, size_t cap, uint32_t remainingMs);
size_t formatStopwatch(char* out, size_t cap, uint32_t ms);
size_t formatWorkDuration(char* out, size_t cap, uint8_t minutes);   // "25 min", "1 h 05"
bool isNight(int hour);                     // 22:00 .. 05:59
