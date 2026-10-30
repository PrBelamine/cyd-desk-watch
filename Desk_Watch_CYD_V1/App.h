#pragma once
#include <stdint.h>
#include "DeskLogic.h"
#include "TaskStore.h"

enum Page : uint8_t { PG_CLOCK, PG_TIMER, PG_CHRONO, PG_COUNT, PG_ALARM, PG_NOTES, PG_SETUP };

enum Action : uint8_t {
  ACT_NONE,
  T_MINUS, T_PLUS, T_START, T_RESET,           // timer
  S_START, S_RESET,                            // stopwatch
  C_MINUS, C_PLUS, C_LOCK, C_RESET, C_YES, C_NO,   // counter
  A_HP, A_HM, A_MP, A_MM, A_TOGGLE, A_TUNE_PREV, A_TUNE_NEXT, A_TEST, A_VOL_MINUS, A_VOL_PLUS,   // alarm
  R_SNOOZE, R_STOP,                            // ringing
  P_BRIGHT_MINUS, P_BRIGHT_PLUS, P_VOL_MINUS, P_VOL_PLUS, P_NIGHT, P_LANG, P_CALIBRATE   // setup
};
struct Button { Rect r; Action action; bool repeat; };

constexpr int BRIGHT_LEVEL_COUNT = 8;
extern const uint8_t BRIGHT_LEVELS[BRIGHT_LEVEL_COUNT];   // percent
constexpr int VOLUME_MAX = 5;                         // 0 = muted, 1..5 = five volume levels

struct Config {                                // everything saved in flash (NVS)
  uint8_t workMin = 25, alarmHour = 7, alarmMinute = 0, armed = 0, tune = 0;
  uint8_t volume = 3, brightIndex = 7, night = 0;       // night: 0 off, 1 auto (22:00-06:00, dim screen, quiet timer beeps)
  uint8_t lang = 0;                                     // 0 = English, 1 = Français
  char tz[48] = "ICT-7";                                // POSIX time zone rule (set from http://postit.local/settings)
  char tzLabel[48] = "Hanoi (Vietnam)";                 // what the Clock page shows under the date
};

struct AppState {
  Config cfg;
  Pomodoro pomo;
  Stopwatch watch;
  Counter counter;
  AlarmClock alarm;
  TaskStore tasks;
  int page = PG_CLOCK;
  int taskScroll = 0;
  bool counterConfirm = false;
  uint32_t counterConfirmAt = 0;
  bool previewing = false;
  char banner[40] = "";
  uint32_t bannerUntil = 0;
};

// ---- Screens.cpp ----
bool displayBegin();
void setBacklightPercent(uint8_t percent);
uint8_t effectiveBrightness(const AppState& s, int hour, bool clockValid);
int pageButtons(const AppState& s, Button* out, int capacity);   // buttons currently on screen (drawing and touch share it)
void renderApp(const AppState& s, uint32_t nowMs);
void setUiLanguage(uint8_t lang);                 // 0 = English, 1 = Français (also used by the calibration and splash screens)
const char* tr(const char* en, const char* fr);   // picks the text for the current language
void renderCalibration(int step, bool failed);
void renderSplash(int percent);
bool clockIsValid();
// ---- Sound.cpp ----
void soundBegin();
void soundPlay(const Tune& tune, bool loop, uint8_t volume);
void soundStop();
void soundTick();
bool soundActive();
