#pragma once
#include <stdint.h>
#include "DeskLogic.h"
#include "TaskStore.h"
#include "Weather.h"
#include "Settings.h"

enum Page : uint8_t { PG_CLOCK, PG_TIMER, PG_CHRONO, PG_COUNT, PG_ALARM, PG_NOTES, PG_SETUP, PG_CLIMATE, PG_WEATHER, PG_RADIO, PG_SLEEP };
static_assert(PG_SETUP == PAGE_SETUP && PG_CLIMATE == PAGE_CLIMATE && PG_WEATHER == PAGE_WEATHER && PG_RADIO == PAGE_RADIO && PG_SLEEP == PAGE_SLEEP, "page numbers must match DeskLogic.h");

enum Action : uint8_t {
  ACT_NONE,
  T_MINUS, T_PLUS, T_START, T_RESET,           // timer
  S_START, S_RESET,                            // stopwatch
  C_MINUS, C_PLUS, C_LOCK, C_RESET, C_YES, C_NO,   // counter
  A_HP, A_HM, A_MP, A_MM, A_TOGGLE, A_TUNE_PREV, A_TUNE_NEXT, A_TEST, A_VOL_MINUS, A_VOL_PLUS,   // alarm
  R_SNOOZE, R_STOP,                            // ringing
  P_BRIGHT_MINUS, P_BRIGHT_PLUS, P_VOL_MINUS, P_VOL_PLUS, P_NIGHT, P_LANG, P_CALIBRATE,   // setup
  K_TEMP_MINUS, K_TEMP_PLUS, K_HUM_MINUS, K_HUM_PLUS, K_ALERT,   // temperature / humidity page
  D_PREV, D_NEXT, D_PLAY, D_VOL_MINUS, D_VOL_PLUS, D_NOISE, D_DITHER,   // radio page
  V_SCR_MINUS, V_SCR_PLUS, V_TMR_MINUS, V_TMR_PLUS, V_GO, V_SCREEN_OFF,   // sleep page
  P_WIFI                                       // settings: Wi-Fi off / on
};
// What the ringing alarm is playing (shown on the ringing screen).
enum RingMode : uint8_t { RING_MELODY, RING_RADIO, RING_RADIO_FAILED, RING_WAIT_WIFI };   // melody / web radio / radio silent so a melody plays / no Wi-Fi yet, a melody plays until it is back
struct Button { Rect r; Action action; bool repeat; };

constexpr int BRIGHT_LEVEL_COUNT = 8;
extern const uint8_t BRIGHT_LEVELS[BRIGHT_LEVEL_COUNT];   // percent
constexpr int VOLUME_MAX = 10;                        // 0 = muted, 1..10 = ten volume levels (1 = very quiet)

struct Config {                                // everything saved in flash (NVS)
  uint8_t workMin = 25, alarmHour = 7, alarmMinute = 0, armed = 0, tune = 0;   // tune: alarm sound, 0..3 melodies then the web radios (see DeskLogic.h)
  uint8_t radioStation = 0, radioVolume = 4, radioNoise = 1, radioDither = 2;
  uint8_t screenSleep = 0, sleepTimer = 2;             // screen standby delay index (0 = never) and radio sleep timer index (see DeskLogic.h)            // web radio: last station, own volume (the speaker is driven differently)
  uint8_t volume = 4, brightIndex = 7, night = 0;       // night: 0 off, 1 auto (22:00-06:00, dim screen, quiet timer beeps)
  uint8_t tempMax = 32, humMax = 85, climateAlert = 1;  // DHT22 alert thresholds (deg C, % RH) and on/off
  uint8_t lang = 0;                                     // 0 = English, 1 = Français
  char tz[48] = "ICT-7";                                // POSIX time zone rule (set from http://cyd.local/location)
  char tzLabel[48] = "Hanoi (Vietnam)";                 // what the Clock page shows under the date
  float wLat = WEATHER_LATITUDE, wLon = WEATHER_LONGITUDE;   // weather town (set from http://cyd.local/location)
  char wPlace[24] = "Hanoi";                            // its name, shown on the Weather page
};

struct ClimateReading {                        // last DHT22 measurement (not saved)
  bool valid = false;                          // false until the first good read, or after repeated failures
  float tempC = 0, humPct = 0;
  uint8_t error = 0;                           // 0 ok, 1 no answer (wiring), 2 timing, 3 checksum
  uint32_t lastOkMs = 0;
};

struct AppState {
  Config cfg;
  Pomodoro pomo;
  Stopwatch watch;
  Counter counter;
  AlarmClock alarm;
  ClimateReading climate;
  RadioState radio = RADIO_OFF;                // web radio status (not saved)
  uint8_t ringMode = RING_MELODY;              // set when the alarm starts ringing (see RingMode)
  WeatherData weather;                         // last answer from Open-Meteo (not saved)
  ClimateGuard climateGuard;
  TaskStore tasks;
  int page = PG_CLOCK;
  int taskScroll = 0;
  bool counterConfirm = false;
  uint32_t counterConfirmAt = 0;
  bool previewing = false;
  bool wifiOff = false;                        // Wi-Fi switched off from the settings page (not saved: it comes back on at the next start)
  bool screenOff = false;                      // screen in standby (backlight off), a touch wakes it
  SleepTimer sleep;                            // radio sleep timer
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
// ---- Climate.cpp ----
int climateRead(float& tempC, float& humPct);   // DHT22: 0 ok, 1 no answer, 2 timing, 3 checksum
// ---- Sound.cpp ----
void soundBegin();
void soundPlay(const Tune& tune, bool loop, uint8_t volume);
void soundStop();
void soundTick();
bool soundActive();
void soundSetVolume(uint8_t volume);                // changes the volume of the melody that is playing, at once
void soundPause();                                // stops the sound and frees the speaker pin for the web radio
