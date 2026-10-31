/*
 * Desk Watch CYD V1 - clock, Pomodoro timer, stopwatch, counter, alarm and a Post-it task list
 * for the ESP32-2432S028R "Cheap Yellow Display" (2.8" touchscreen).
 * By Pr. BELAMINE MakerLab. Port of the Desk Watch JOY STUDENT edition (Arduino UNO).
 * Libraries: TFT_eSPI, XPT2046_Touchscreen. No LittleFS data upload is needed.
 */
#include <Arduino.h>
#include <SPI.h>
#include <WiFi.h>
#include <Preferences.h>
#include <XPT2046_Touchscreen.h>
#include <time.h>
#include "Settings.h"
#include "App.h"
#include "Radio.h"
#include "TaskWeb.h"
#include "Improv.h"

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touchPanel(TOUCH_CS_PIN, 255);   // pressure polling: the IRQ line is unreliable on some boards
Preferences prefs;
AppState app;
TouchCal touchCal;

bool touchReady = false, dirty = true;
bool cfgDirty = false, counterDirty = false;
bool ringRadio = false; RadioWatch ringWatch;      // the alarm is ringing with a web radio (and the melody takes over if it fails)
uint32_t cfgChangedAt = 0, counterChangedAt = 0, lastTick = 0, lastDraw = 0, lastBrightness = 0;

// ---- calibration ----
bool calibrating = false, calWaitRelease = false, calFailed = false;
int calStep = 0, calRaw[3][2] = {}, calSamples = 0;
long calSumX = 0, calSumY = 0;
uint32_t calStarted = 0, calPressStart = 0;
const int CAL_SCREEN[3][2] = {{20, 20}, {300, 20}, {160, 210}};

// ---- touch gesture ----
bool fingerDown = false, repeatFired = false;
int startX = 0, startY = 0, endX = 0, endY = 0, downButton = -1;
uint32_t fingerStart = 0, lastSeen = 0, nextRepeat = 0;
Button activeButtons[12]; int activeCount = 0;

// ---------------- helpers ----------------
bool wifiOnline() { return WiFi.status() == WL_CONNECTED; }
uint32_t lastActivity = 0;                         // last touch (or event that wakes the screen): drives the screen standby
bool localFields(int& hour, int& minute) {
  const time_t stamp = time(nullptr);
  if (stamp < 1700000000) return false;
  struct tm t; localtime_r(&stamp, &t); hour = t.tm_hour; minute = t.tm_min; return true;
}
void applyBrightness() {
  int h = 0, m = 0; const bool valid = localFields(h, m);
  setBacklightPercent(app.screenOff ? 0 : effectiveBrightness(app, h, valid));
  lastBrightness = millis();
}
void applyTimezone() {            // POSIX rule saved from the settings page (default Vietnam)
  configTzTime(app.cfg.tz, "pool.ntp.org", "time.google.com");
  dirty = true;
}
void showBanner(const char* message, uint32_t ms) {
  strlcpy(app.banner, message, sizeof app.banner); app.bannerUntil = millis() + ms; dirty = true;
}
void wakeScreen() {                                // touch, alarm, timer end or climate alert: the screen comes back
  lastActivity = millis();
  if (app.screenOff) { app.screenOff = false; applyBrightness(); dirty = true; }
}
void markCfg() { cfgDirty = true; cfgChangedAt = millis(); }
void markCounter() { counterDirty = true; counterChangedAt = millis(); }

void saveCfg() {
  app.cfg.workMin = app.pomo.workMin; app.cfg.alarmHour = app.alarm.hour; app.cfg.alarmMinute = app.alarm.minute;
  app.cfg.armed = app.alarm.armed ? 1 : 0;
  prefs.putUChar("work", app.cfg.workMin); prefs.putUChar("aH", app.cfg.alarmHour); prefs.putUChar("aM", app.cfg.alarmMinute);
  prefs.putUChar("armed", app.cfg.armed); prefs.putUChar("tune", app.cfg.tune); prefs.putUChar("vol10", app.cfg.volume); prefs.putUChar("rStn", app.cfg.radioStation); prefs.putUChar("rVol", app.cfg.radioVolume); prefs.putUChar("rNoi", app.cfg.radioNoise); prefs.putUChar("rDit", app.cfg.radioDither); prefs.putUChar("sSlp", app.cfg.screenSleep); prefs.putUChar("sTmr", app.cfg.sleepTimer);
  prefs.putUChar("bright", app.cfg.brightIndex); prefs.putUChar("night", app.cfg.night);
  prefs.putUChar("tMax", app.cfg.tempMax); prefs.putUChar("hMax", app.cfg.humMax); prefs.putUChar("cAlert", app.cfg.climateAlert);
  prefs.putUChar("lang", app.cfg.lang); prefs.putString("tz", app.cfg.tz); prefs.putString("tzname", app.cfg.tzLabel);
  prefs.putString("wName", app.cfg.wPlace); prefs.putFloat("wLat", app.cfg.wLat); prefs.putFloat("wLon", app.cfg.wLon);
  cfgDirty = false;
}
void saveRadios() {              // the three radios chosen on http://cyd.local/radios
  for (int i = 0; i < RADIO_COUNT; ++i) {
    char key[4] = {'r', 'n', char('0' + i), 0}; prefs.putString(key, RADIOS[i].name);
    key[1] = 'u'; prefs.putString(key, RADIOS[i].url);
  }
}
void loadRadios() {
  for (int i = 0; i < RADIO_COUNT; ++i) {
    char key[4] = {'r', 'n', char('0' + i), 0}; char name[RADIO_NAME_MAX + 2] = "", url[RADIO_URL_MAX + 2] = "";
    prefs.getString(key, name, sizeof name); key[1] = 'u'; prefs.getString(key, url, sizeof url);
    if (name[0]) radioSet(i, name, url);                // an invalid saved pair is ignored: the factory radio stays
  }
}
void saveTasks() {
  uint8_t buffer[TASK_STORAGE_MAX];
  const size_t length = app.tasks.serialize(buffer, sizeof buffer);
  if (length) prefs.putBytes("tasks", buffer, length);
}
bool weatherAgain = false;       // set when the weather town changes: the next loop downloads the new weather
void settingsChanged() {         // language, time zone or weather town changed from the web page
  setUiLanguage(app.cfg.lang); saveCfg(); applyTimezone(); applyBrightness();
  app.weather.valid = false; weatherAgain = true; dirty = true;
}
void radiosChanged() {           // radios rewritten from the web page (the radio was stopped before)
  saveRadios(); dirty = true;
}
void tasksChanged() {            // called after any change from the screen or the web page
  saveTasks(); app.taskScroll = clampTaskScroll(app.taskScroll, app.tasks.count());
  Serial.printf("POSTIT %d task(s), %d done\n", app.tasks.count(), app.tasks.doneCount());
  dirty = true;
}
void loadAll() {
  Config& c = app.cfg;
  c.workMin = prefs.getUChar("work", 25); c.alarmHour = prefs.getUChar("aH", 7); c.alarmMinute = prefs.getUChar("aM", 0);
  c.armed = prefs.getUChar("armed", 0) ? 1 : 0; c.tune = prefs.getUChar("tune", 0); c.volume = prefs.getUChar("vol10", 4);
  c.radioStation = prefs.getUChar("rStn", 0); if (c.radioStation >= RADIO_COUNT) c.radioStation = 0;
  c.screenSleep = prefs.getUChar("sSlp", 0); if (c.screenSleep >= SCREEN_SLEEP_COUNT) c.screenSleep = 0;
  c.sleepTimer = prefs.getUChar("sTmr", 2); if (c.sleepTimer >= SLEEP_TIMER_COUNT) c.sleepTimer = 2;
  c.radioDither = prefs.getUChar("rDit", 2); if (c.radioDither > 3) c.radioDither = 2;
  c.radioNoise = prefs.getUChar("rNoi", 1); if (c.radioNoise > 2) c.radioNoise = 1;
  c.radioVolume = prefs.getUChar("rVol", 4); if (c.radioVolume < 1 || c.radioVolume > VOLUME_MAX) c.radioVolume = 4;
  c.brightIndex = prefs.getUChar("bright", BRIGHT_LEVEL_COUNT - 1); c.night = prefs.getUChar("night", 0) ? 1 : 0;
  c.lang = prefs.getUChar("lang", 0) == 1 ? 1 : 0;
  c.tempMax = prefs.getUChar("tMax", 32); if (c.tempMax < TEMP_ALERT_MIN || c.tempMax > TEMP_ALERT_MAX) c.tempMax = 32;
  c.humMax = prefs.getUChar("hMax", 85); if (c.humMax < HUM_ALERT_MIN || c.humMax > HUM_ALERT_MAX) c.humMax = 85;
  c.climateAlert = prefs.getUChar("cAlert", 1) ? 1 : 0;
  {
    char place[sizeof c.wPlace] = ""; const float la = prefs.getFloat("wLat", 1000), lo = prefs.getFloat("wLon", 1000);
    prefs.getString("wName", place, sizeof place);
    if (place[0] && la >= -90 && la <= 90 && lo >= -180 && lo <= 180) { strlcpy(c.wPlace, place, sizeof c.wPlace); c.wLat = la; c.wLon = lo; }
  }
  loadRadios();
  {
    char tz[sizeof c.tz] = "", label[sizeof c.tzLabel] = "";
    prefs.getString("tz", tz, sizeof tz); prefs.getString("tzname", label, sizeof label);
    if (tzRuleValid(tz) && label[0]) { strlcpy(c.tz, tz, sizeof c.tz); strlcpy(c.tzLabel, label, sizeof c.tzLabel); }
  }
  if (c.workMin < 1 || c.workMin > 240) c.workMin = 25;
  if (c.alarmHour > 23) c.alarmHour = 7;
  if (c.alarmMinute > 59) c.alarmMinute = 0;
  if (c.tune >= ALARM_SOUND_COUNT) c.tune = 0;
  if (c.volume > VOLUME_MAX) c.volume = 4;
  if (c.brightIndex >= BRIGHT_LEVEL_COUNT) c.brightIndex = BRIGHT_LEVEL_COUNT - 1;
  app.pomo.workMin = c.workMin; app.pomo.remainingMs = app.pomo.phaseMs(POMO_WORK);
  app.alarm.hour = c.alarmHour; app.alarm.minute = c.alarmMinute; app.alarm.armed = c.armed;
  app.counter.value = prefs.getUShort("cnt", 0); if (app.counter.value > COUNTER_MAX) app.counter.value = 0;
  touchCal = defaultTouchCal();
  if (prefs.getBytesLength("cal") == sizeof(TouchCal)) prefs.getBytes("cal", &touchCal, sizeof(TouchCal));
  const size_t length = prefs.getBytesLength("tasks");
  if (length) {
    uint8_t buffer[TASK_STORAGE_MAX];
    if (length > sizeof buffer || prefs.getBytes("tasks", buffer, length) != length || !app.tasks.deserialize(buffer, length))
      Serial.println("POSTIT saved list unreadable; starting empty");
  }
}

// ---- sound helpers: the speaker pin belongs either to the beeper (PWM) or to the web radio (DAC), never both ----
void wifiConnect() {                               // join the saved network (also used to switch the Wi-Fi back on)
  WiFi.setAutoReconnect(true);
  char ssid[33], password[65];
  if (wifiLoadSaved(prefs, ssid, sizeof ssid, password, sizeof password)) WiFi.begin(ssid, password);
  else if (strcmp(WIFI_SSID, "your-wifi-name") != 0) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);   // Secrets.h, for people building from source
  else WiFi.reconnect();
}
void silence() { soundStop(); radioStop(); ringRadio = false; app.sleep.cancel(); }
void beep(const Tune& tune) { if (!radioActive()) soundPlay(tune, false, app.cfg.volume); }   // no beeps over the radio (the banner still shows)
bool startRadioNow() {                                // station and volume chosen on the radio page
  if (!wifiOnline()) { showBanner(tr("Wi-Fi needed for the radio", "Wi-Fi requis pour la radio"), 3000); return false; }
  soundPause(); radioPlay(app.cfg.radioStation, app.cfg.radioVolume); return true;
}
void previewStart() {                                 // alarm page, TEST button
  silence();
  if (alarmSoundIsRadio(app.cfg.tune)) {
    if (!wifiOnline()) { showBanner(tr("Wi-Fi needed for the radio", "Wi-Fi requis pour la radio"), 3000); return; }
    soundPause(); radioPlay(alarmSoundStation(app.cfg.tune), app.cfg.radioVolume);
  } else soundPlay(TUNES[app.cfg.tune], false, app.cfg.volume);
  app.previewing = true;
}
void previewStop() { silence(); app.previewing = false; }

int lastFirstRowPage = PG_CLOCK, lastSecondRowPage = PG_CLIMATE;   // where the arrows return to
void changePage(int next) {
  next = wrapPage(next);
  if (pageRow(next) == 0) lastFirstRowPage = next; else lastSecondRowPage = next;
  if (app.page == PG_ALARM && next != PG_ALARM && app.previewing) previewStop();
  if (next != PG_COUNT) app.counterConfirm = false;
  app.page = next; dirty = true;
}

// ---------------- actions ----------------
void startRing() {
  wakeScreen();
  if (app.previewing) app.previewing = false;
  silence();
  const bool wantRadio = alarmSoundIsRadio(app.cfg.tune);
  Serial.printf("ALARM time: sound %d (%s), Wi-Fi %s\n", app.cfg.tune, alarmSoundName(app.cfg.tune), wifiOnline() ? "up" : "DOWN");
  if (wantRadio && wifiOnline()) {
    soundPause(); radioPlay(alarmSoundStation(app.cfg.tune), app.cfg.radioVolume);
    ringRadio = true; ringWatch.start(millis()); app.ringMode = RING_RADIO;
  } else if (wantRadio) {                              // no Wi-Fi at this very moment: the melody rings at once, the radio takes over as soon as the Wi-Fi is back
    WiFi.reconnect(); app.ringMode = RING_WAIT_WIFI;
    soundPlay(TUNES[0], true, app.cfg.volume);
  } else { app.ringMode = RING_MELODY; soundPlay(TUNES[app.cfg.tune], true, app.cfg.volume); }
  dirty = true;
  Serial.printf("ALARM ringing (mode %d)\n", (int)app.ringMode);
}
void startCalibration() {
  calibrating = true; calStep = 0; calWaitRelease = true; calFailed = false; calStarted = millis();
  calSamples = 0; calPressStart = 0; fingerDown = false;
  renderCalibration(0, false); Serial.println("CALIBRATION started");
}
void perform(Action a) {
  const uint32_t now = millis();
  switch (a) {
    case T_MINUS: app.pomo.changeWork(-1); markCfg(); break;
    case T_PLUS: app.pomo.changeWork(+1); markCfg(); break;
    case T_START: app.pomo.startPause(); markCfg(); break;
    case T_RESET: app.pomo.reset(); break;
    case S_START: app.watch.toggle(now); break;
    case S_RESET: app.watch.reset(); break;
    case C_PLUS: if (app.counter.increment()) markCounter(); break;
    case C_MINUS: if (app.counter.decrement()) markCounter(); break;
    case C_LOCK: app.counter.active = !app.counter.active; break;
    case C_RESET: app.counterConfirm = true; app.counterConfirmAt = now; break;
    case C_YES: app.counter.reset(); app.counterConfirm = false; markCounter(); break;
    case C_NO: app.counterConfirm = false; break;
    case A_HP: app.alarm.adjustHour(+1); markCfg(); break;
    case A_HM: app.alarm.adjustHour(-1); markCfg(); break;
    case A_MP: app.alarm.adjustMinute(+1); markCfg(); break;
    case A_MM: app.alarm.adjustMinute(-1); markCfg(); break;
    case A_TOGGLE: app.alarm.setArmed(!app.alarm.armed); markCfg(); break;
    case A_TUNE_PREV: app.cfg.tune = (app.cfg.tune + ALARM_SOUND_COUNT - 1) % ALARM_SOUND_COUNT; markCfg(); if (app.previewing) previewStart(); break;
    case A_TUNE_NEXT: app.cfg.tune = (app.cfg.tune + 1) % ALARM_SOUND_COUNT; markCfg(); if (app.previewing) previewStart(); break;
    case A_TEST: if (app.previewing) previewStop(); else previewStart(); break;
    case A_VOL_MINUS: case A_VOL_PLUS: {                // edits the volume of the selected alarm sound (melody or radio)
      const int step = a == A_VOL_PLUS ? 1 : -1;
      if (alarmSoundIsRadio(app.cfg.tune)) {
        const int v = app.cfg.radioVolume + step;
        if (v >= 0 && v <= VOLUME_MAX) { app.cfg.radioVolume = v; markCfg(); radioSetVolume(v); }
      } else {
        const int v = app.cfg.volume + step;
        if (v >= 0 && v <= VOLUME_MAX) { app.cfg.volume = v; markCfg(); soundSetVolume(v); }
      }
      break;
    }
    case R_SNOOZE: app.alarm.snooze(now); silence(); break;
    case R_STOP: app.alarm.stop(); silence(); break;
    case P_BRIGHT_MINUS: if (app.cfg.brightIndex > 0) { --app.cfg.brightIndex; markCfg(); applyBrightness(); } break;
    case P_BRIGHT_PLUS: if (app.cfg.brightIndex < BRIGHT_LEVEL_COUNT - 1) { ++app.cfg.brightIndex; markCfg(); applyBrightness(); } break;
    case P_VOL_MINUS: if (app.cfg.volume > 0) { --app.cfg.volume; markCfg(); beep(BEEP_PREVIEW_OK); } break;
    case P_VOL_PLUS: if (app.cfg.volume < VOLUME_MAX) { ++app.cfg.volume; markCfg(); beep(BEEP_PREVIEW_OK); } break;
    case P_NIGHT: app.cfg.night = app.cfg.night ? 0 : 1; markCfg(); applyBrightness(); break;
    case P_LANG: app.cfg.lang = app.cfg.lang ? 0 : 1; setUiLanguage(app.cfg.lang); markCfg(); break;
    case P_CALIBRATE: startCalibration(); break;
    case K_TEMP_MINUS: if (app.cfg.tempMax > TEMP_ALERT_MIN) { --app.cfg.tempMax; markCfg(); } break;
    case K_TEMP_PLUS: if (app.cfg.tempMax < TEMP_ALERT_MAX) { ++app.cfg.tempMax; markCfg(); } break;
    case K_HUM_MINUS: if (app.cfg.humMax > HUM_ALERT_MIN) { --app.cfg.humMax; markCfg(); } break;
    case K_HUM_PLUS: if (app.cfg.humMax < HUM_ALERT_MAX) { ++app.cfg.humMax; markCfg(); } break;
    case D_PREV: case D_NEXT: {
      const uint8_t before = app.cfg.radioStation;
      app.cfg.radioStation = (app.cfg.radioStation + RADIO_COUNT + (a == D_NEXT ? 1 : -1)) % RADIO_COUNT; markCfg();
      if (radioActive()) {                              // playing: the current radio is stopped, then the new one starts
        Serial.printf("RADIO switch: %s -> %s\n", RADIOS[before].name, RADIOS[app.cfg.radioStation].name);
        radioStop(); radioPlay(app.cfg.radioStation, app.cfg.radioVolume);
        dirty = true;
      }
      break;
    }
    case D_PLAY: {                                      // PLAY / STOP
      static uint32_t lastToggle = 0;
      if (lastToggle && now - lastToggle < 1000) { Serial.println("RADIO button ignored: second touch within 1 s (bounce or double tap)"); break; }
      lastToggle = now;
      Serial.printf("RADIO button: radio was %s -> %s\n", radioActive() ? "on" : "off", radioActive() ? "STOP" : "PLAY");
      if (radioActive()) silence(); else startRadioNow();
      break;
    }
    case V_SCR_MINUS: if (app.cfg.screenSleep > 0) { --app.cfg.screenSleep; markCfg(); } break;
    case V_SCR_PLUS: if (app.cfg.screenSleep < SCREEN_SLEEP_COUNT - 1) { ++app.cfg.screenSleep; markCfg(); } break;
    case V_TMR_MINUS: if (!app.sleep.active && app.cfg.sleepTimer > 0) { --app.cfg.sleepTimer; markCfg(); } break;
    case V_TMR_PLUS: if (!app.sleep.active && app.cfg.sleepTimer < SLEEP_TIMER_COUNT - 1) { ++app.cfg.sleepTimer; markCfg(); } break;
    case V_GO:
      if (app.sleep.active) { app.sleep.cancel(); radioSetVolume(app.cfg.radioVolume); }
      else {
        if (!radioActive() && !startRadioNow()) break;       // no Wi-Fi: the banner says so
        app.sleep.start(now, SLEEP_TIMER_MIN[app.cfg.sleepTimer]);
      }
      break;
    case V_SCREEN_OFF: app.screenOff = true; applyBrightness(); break;
    case P_WIFI:
      if (app.wifiOff || !wifiOnline()) { app.wifiOff = false; wifiConnect(); showBanner(tr("Connecting to Wi-Fi...", "Connexion au Wi-Fi..."), 2500); }
      else { silence(); app.wifiOff = true; WiFi.setAutoReconnect(false); WiFi.disconnect(false); Serial.println("WIFI switched off"); }
      dirty = true; break;
    case D_DITHER: app.cfg.radioDither = (app.cfg.radioDither + 1) % 4; markCfg(); radioSetDither(app.cfg.radioDither); break;
    case D_NOISE: app.cfg.radioNoise = (app.cfg.radioNoise + 1) % 3; markCfg(); radioSetNoise(app.cfg.radioNoise); break;
    case D_VOL_MINUS: if (app.cfg.radioVolume > 1) { --app.cfg.radioVolume; markCfg(); radioSetVolume(app.cfg.radioVolume); } break;
    case D_VOL_PLUS: if (app.cfg.radioVolume < VOLUME_MAX) { ++app.cfg.radioVolume; markCfg(); radioSetVolume(app.cfg.radioVolume); } break;
    case K_ALERT: app.cfg.climateAlert = app.cfg.climateAlert ? 0 : 1; if (!app.cfg.climateAlert) app.climateGuard.clear(); markCfg(); break;
    default: break;
  }
  dirty = true;
}

// ---------------- touch ----------------
void calibrateSample(bool pressed, const TS_Point& raw) {
  if (millis() - calStarted > 90000) { calibrating = false; dirty = true; Serial.println("CALIBRATION timed out"); return; }
  if (calWaitRelease) {
    if (!pressed) {
      calWaitRelease = false; calSamples = 0; calPressStart = 0; calSumX = calSumY = 0;
      if (calStep == 3) {
        TouchCal result;
        if (fitTouchCal(calRaw, CAL_SCREEN, result)) {
          touchCal = result; prefs.putBytes("cal", &touchCal, sizeof(TouchCal));
          calibrating = false; dirty = true; Serial.println("CALIBRATION saved"); return;
        }
        calStep = 0; calFailed = true; Serial.println("CALIBRATION rejected; retry");
      }
      renderCalibration(calStep, calFailed);
    }
    return;
  }
  if (!pressed) { calSamples = 0; calPressStart = 0; calSumX = calSumY = 0; return; }
  if (!calSamples) calPressStart = millis();
  calSumX += raw.x; calSumY += raw.y; ++calSamples;
  if (millis() - calPressStart >= 350 && calSamples >= 8) {
    calRaw[calStep][0] = calSumX / calSamples; calRaw[calStep][1] = calSumY / calSamples;
    Serial.printf("CALIBRATION point=%d raw=%d,%d\n", calStep, calRaw[calStep][0], calRaw[calStep][1]);
    ++calStep; calWaitRelease = true;
  }
}
int hitActive(int x, int y) {
  for (int i = 0; i < activeCount; ++i) if (inside(activeButtons[i].r, x, y)) return i;
  return -1;
}
void handleNotesTap(int x, int y) {
  const int direction = taskScrollAt(x, y);
  if (direction && app.tasks.count() > TASK_VISIBLE) {
    app.taskScroll = taskPageScroll(app.taskScroll, app.tasks.count(), direction); dirty = true; return;
  }
  const int row = taskRowAt(x, y);
  if (row >= 0 && app.tasks.toggleAt(app.taskScroll + row)) tasksChanged();
}
void handleRelease() {
  const int dx = endX - startX, dy = endY - startY;
  const bool tap = abs(dx) < 20 && abs(dy) < 20;
  Serial.printf("TOUCH %d,%d -> %d,%d\n", startX, startY, endX, endY);
  if (app.alarm.ringing) {                          // while ringing only the two buttons work
    const int hit = hitActive(endX, endY);
    if (tap && hit >= 0 && hit == downButton) perform(activeButtons[hit].action);
    return;
  }
  if (abs(dx) >= 40 && abs(dx) > abs(dy)) { changePage(pageAfterSwipe(app.page, dx, dy)); return; }
  if (!tap) return;
  if (startY >= 222 && endY >= 222) {
    const int target = footerTarget(pageRow(app.page), endX);
    if (target == FOOT_DOWN) changePage(lastSecondRowPage);
    else if (target == FOOT_UP) changePage(lastFirstRowPage);
    else if (target != FOOT_NONE) changePage(target);
    return;
  }
  const int hit = hitActive(endX, endY);
  if (hit >= 0 && hit == downButton) {
    if (!(activeButtons[hit].repeat && repeatFired)) perform(activeButtons[hit].action);   // a held repeat button already acted
    return;
  }
  if (app.page == PG_NOTES) handleNotesTap(endX, endY);
}
void pollTouch() {
  if (!touchReady) return;
  static uint32_t lastPoll = 0;
  if (millis() - lastPoll < 12) return;
  lastPoll = millis();
  TS_Point raw; bool pressed = touchPanel.touched();
  if (pressed) { raw = touchPanel.getPoint(); pressed = raw.z >= 200; }
  if (calibrating) { calibrateSample(pressed, raw); return; }
  static bool swallow = false; static uint32_t lastPress = 0;     // the touch that wakes the screen must not press a button
  if (pressed) { lastPress = millis(); lastActivity = lastPress; if (app.screenOff) { wakeScreen(); swallow = true; } }
  if (swallow) { if (!pressed && millis() - lastPress > 70) swallow = false; return; }
  const uint32_t now = millis();
  if (pressed) {
    int x, y; mapTouch(touchCal, raw.x, raw.y, x, y);
    lastSeen = now; endX = x; endY = y;
    if (!fingerDown) {
      fingerDown = true; fingerStart = now; startX = x; startY = y; repeatFired = false; nextRepeat = now + 450;
      activeCount = pageButtons(app, activeButtons, 12); downButton = hitActive(x, y);
    } else if (downButton >= 0 && activeButtons[downButton].repeat && inside(activeButtons[downButton].r, x, y) && int32_t(now - nextRepeat) >= 0) {
      perform(activeButtons[downButton].action); repeatFired = true;
      nextRepeat = now + (now - fingerStart > 1500 ? 50 : 120);       // faster after 1.5 s, like the joystick
    }
    // A long press on an empty area starts the touch calibration (useful if the touch is badly off).
    if (downButton < 0 && app.page != PG_NOTES && !app.alarm.ringing && now - fingerStart > 3000 &&
        abs(endX - startX) < 20 && abs(endY - startY) < 20) startCalibration();
  } else if (fingerDown && now - lastSeen > 70) {
    fingerDown = false;
    if (now - fingerStart >= 35) handleRelease();
  }
}

// ---------------- setup / loop ----------------
void setup() {
  Serial.begin(115200); delay(200);
  Serial.printf("Desk Watch CYD V1 - CYD EDITION %s\n", FIRMWARE_EDITION);
  prefs.begin("deskwatch", false);
  loadAll();
  setUiLanguage(app.cfg.lang);
  soundBegin();
  if (!displayBegin()) { Serial.println("FATAL: screen buffer allocation failed"); while (true) delay(1000); }
  applyBrightness();
  pinMode(TOUCH_CS_PIN, OUTPUT); digitalWrite(TOUCH_CS_PIN, HIGH); pinMode(TOUCH_IRQ_PIN, INPUT);
  touchSPI.begin(TOUCH_CLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS_PIN);
  touchReady = touchPanel.begin(touchSPI); touchPanel.setRotation(TOUCH_ROTATION);
  Serial.printf("TOUCH ready=%d\n", touchReady);
  WiFi.mode(WIFI_STA); WiFi.setSleep(false); WiFi.setAutoReconnect(true);
  radioBegin(); radioSetNoise(app.cfg.radioNoise); radioSetDither(app.cfg.radioDither);                                      // web radio task (idle until a station is started)
  improvBegin(prefs);                                // Wi-Fi can be set from the web installer over USB
  wifiConnect();
  applyTimezone();
  beginTaskWeb(app.tasks, tasksChanged, app.cfg, settingsChanged, radiosChanged);
  bool touchedAtBoot = false;
  for (int i = 0; i <= 20; ++i) {                    // splash with a loading bar (about 1.4 s); touch it to calibrate
    renderSplash(i * 5);
    if (touchReady && touchPanel.touched() && touchPanel.getPoint().z >= 200) touchedAtBoot = true;
    delay(60);
  }
  lastTick = millis(); lastActivity = lastTick; dirty = true;
  if (touchedAtBoot) startCalibration();
}
const char* loopStep = "";                           // where the main loop is: shown if one turn ever takes more than 1.5 s
void loop() {
  const uint32_t now = millis();
  static uint32_t prevTurn = 0;
  if (prevTurn && now - prevTurn > 1500) Serial.printf("LOOP stalled %u ms, last step: %s\n", (unsigned)(now - prevTurn), loopStep);
  prevTurn = now;
  loopStep = "touch"; pollTouch();
  loopStep = "web"; handleTaskWeb();
  loopStep = "improv"; improvPoll();
  loopStep = "sound"; soundTick();
  loopStep = "logic";

  // ---- background logic: runs on every page ----
  const uint32_t dt = now - lastTick; lastTick = now;
  const PomoEvent event = app.pomo.tick(dt);
  int hour = 0, minute = 0; const bool clockValid = localFields(hour, minute);
  if (event != POMO_NONE) {
    wakeScreen();
    const bool quiet = app.cfg.night && clockValid && isNight(hour);
    if (!app.alarm.ringing && !quiet) beep(event == POMO_WORK_DONE ? BEEP_WORK_DONE : BEEP_BREAK_DONE);
    showBanner(event == POMO_BREAK_DONE ? tr("Break over - back to work", "Pause finie - au travail") : app.pomo.phase == POMO_LONG ? tr("Work done - long break!", "Travail fini - longue pause !") : tr("Work done - take a break", "Travail fini - faites une pause"), 6000);
  }
  if (app.wifiOff && app.alarm.armed && alarmSoundIsRadio(app.cfg.tune) && clockValid &&
      (app.alarm.hour * 60 + app.alarm.minute - (hour * 60 + minute) + 1440) % 1440 <= 2) {   // radio alarm within 2 min: the Wi-Fi comes back by itself
    app.wifiOff = false; wifiConnect(); dirty = true; Serial.println("WIFI back on: radio alarm soon");
  }
  if (!app.wifiOff && app.alarm.armed && alarmSoundIsRadio(app.cfg.tune) && clockValid && !wifiOnline() && !app.alarm.ringing &&
      (app.alarm.hour * 60 + app.alarm.minute - (hour * 60 + minute) + 1440) % 1440 <= 2) {   // radio alarm within 2 min and the Wi-Fi dropped: reconnect now
    static uint32_t lastPreWifi = 0;
    if (now - lastPreWifi > 15000UL) { lastPreWifi = now; WiFi.reconnect(); Serial.println("WIFI reconnect: radio alarm soon"); }
  }
  if (app.alarm.update(hour, minute, clockValid, now)) startRing();
  if (app.alarm.ringing && app.ringMode == RING_WAIT_WIFI) {       // the alarm rang without Wi-Fi: switch to the radio as soon as it is back
    static uint32_t lastWifiTry = 0;
    if (wifiOnline()) {
      Serial.println("ALARM Wi-Fi is back: the radio starts");
      soundPause(); radioPlay(alarmSoundStation(app.cfg.tune), app.cfg.radioVolume);
      ringRadio = true; ringWatch.start(now); app.ringMode = RING_RADIO; dirty = true;
    } else if (now - lastWifiTry > 10000UL) { lastWifiTry = now; WiFi.reconnect(); }
  }
  if (app.alarm.ringing && ringRadio) {                              // progress of the radio alarm in the Serial Monitor, every 5 s
    static uint32_t lastRingLog = 0;
    if (now - lastRingLog > 5000UL) { lastRingLog = now; Serial.printf("ALARM radio state %d, Wi-Fi %s\n", (int)radioState(), wifiOnline() ? "up" : "DOWN"); }
  }
  if (app.sleep.active) {
    if (app.sleep.update(now)) {                       // time is up: the radio stops and the screen goes to standby
      silence(); app.screenOff = true; applyBrightness(); Serial.println("SLEEP timer finished");
    } else if (radioActive()) {
      static uint32_t lastFade = 0;
      if (now - lastFade >= 1000) { lastFade = now; radioSetVolume(app.sleep.fadeLevel(now, app.cfg.radioVolume)); }   // fade out during the last 30 s
    }
  }
  if (!app.screenOff && !calibrating && !app.alarm.ringing && screenShouldSleep(app.cfg.screenSleep, now - lastActivity)) {
    app.screenOff = true; applyBrightness(); Serial.println("SCREEN standby");
  }
  if (app.alarm.ringing && ringRadio && ringWatch.update(now, radioState() == RADIO_PLAYING)) {   // radio silent for 12 s: the melody takes over
    Serial.printf("ALARM radio not playing after %u s (state %d): melody instead\n", (unsigned)(RADIO_FALLBACK_MS / 1000), (int)radioState());
    radioStop(); ringRadio = false; app.ringMode = RING_RADIO_FAILED; dirty = true; soundPlay(TUNES[0], true, app.cfg.volume);
  }
  if (app.alarm.ringTimeout(now)) { silence(); dirty = true; }
  const RadioState radioNow = radioState();
  if (radioNow != app.radio) { app.radio = radioNow; dirty = true; }
  static uint32_t lastClimate = 0; static uint8_t climateFails = 0;
  if (now - lastClimate >= 3000) {                 // DHT22 needs at least 2 s between reads
    lastClimate = now;
    float t = 0, h = 0; loopStep = "climate"; const int err = climateRead(t, h);
    if (err == 0) {
      climateFails = 0; app.climate = {true, t, h, 0, now};
      if (app.climateGuard.update(t, h, app.cfg.tempMax, app.cfg.humMax) && app.cfg.climateAlert && !app.alarm.ringing)
        { wakeScreen(); beep(BEEP_CLIMATE); }
      if (!app.cfg.climateAlert) app.climateGuard.clear();
    } else {
      app.climate.error = err;
      if (++climateFails >= 3) { app.climate.valid = false; app.climateGuard.clear(); climateFails = 3; }   // about 9 s without a good read
    }
    if (app.page == PG_CLIMATE) dirty = true;
  }
  static uint32_t lastWeather = 0, radioIdleSince = 0; static bool weatherTried = false;
  if (weatherAgain) { weatherAgain = false; weatherTried = false; }       // new town: download again (once the radio is off)   // no TLS request while the radio plays: memory is tight
  if (radioActive()) radioIdleSince = now;      // the (blocking) weather request waits 15 s after the radio stops, so STOP never freezes the screen
  if (wifiOnline() && !radioActive() && !app.alarm.ringing && now - radioIdleSince > 15000UL && (!weatherTried || now - lastWeather >= (app.weather.valid ? 1200000UL : 120000UL))) {   // every 20 min, retry after 2 min
    weatherTried = true; lastWeather = now; loopStep = "weather";
    if (weatherFetch(app.weather, app.cfg.wLat, app.cfg.wLon)) { Serial.println("WEATHER ok"); if (app.page == PG_WEATHER) dirty = true; }
    else Serial.println("WEATHER failed");
  }
  if (app.previewing && !soundActive() && !radioActive()) { app.previewing = false; dirty = true; }
  if (app.counterConfirm && now - app.counterConfirmAt > 5000) { app.counterConfirm = false; dirty = true; }
  if (cfgDirty && now - cfgChangedAt > 1500) saveCfg();
  if (counterDirty && now - counterChangedAt > 3000) { prefs.putUShort("cnt", app.counter.value); counterDirty = false; }
  if (now - lastBrightness > 30000) applyBrightness();   // follows the night mode as the clock moves on

  // ---- redraw ----
  static time_t lastSecond = 0, lastMinute = 0; static wl_status_t lastWifi = WL_NO_SHIELD; static bool bannerShown = false;
  const time_t second = time(nullptr);
  if (second != lastSecond) {
    lastSecond = second;
    if (app.page == PG_CLOCK || app.page == PG_TIMER || (app.page == PG_SLEEP && app.sleep.active)) dirty = true;
    if (second / 60 != lastMinute) { lastMinute = second / 60; dirty = true; }   // the header clock shows hours:minutes on every page
  }
  const wl_status_t wifi = WiFi.status();
  if (wifi != lastWifi) { lastWifi = wifi; if (app.page == PG_CLOCK || app.page == PG_NOTES || app.page == PG_SETUP || app.page == PG_WEATHER || app.page == PG_RADIO) dirty = true; }
  const bool bannerNow = now < app.bannerUntil;
  if (bannerNow != bannerShown) { bannerShown = bannerNow; dirty = true; }
  if (app.alarm.ringing && now - lastDraw >= 250) dirty = true;
  if (app.page == PG_CHRONO && app.watch.running && now - lastDraw >= 100) dirty = true;
  loopStep = "draw";
  if (dirty && !calibrating && !app.screenOff) { renderApp(app, now); dirty = false; lastDraw = now; }
  delay(3);
}
