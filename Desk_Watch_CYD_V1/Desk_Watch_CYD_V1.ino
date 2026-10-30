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
#include "TaskWeb.h"
#include "Improv.h"

SPIClass touchSPI(VSPI);
XPT2046_Touchscreen touchPanel(TOUCH_CS_PIN, 255);   // pressure polling: the IRQ line is unreliable on some boards
Preferences prefs;
AppState app;
TouchCal touchCal;

bool touchReady = false, dirty = true;
bool cfgDirty = false, counterDirty = false;
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
bool localFields(int& hour, int& minute) {
  const time_t stamp = time(nullptr);
  if (stamp < 1700000000) return false;
  struct tm t; localtime_r(&stamp, &t); hour = t.tm_hour; minute = t.tm_min; return true;
}
void applyBrightness() {
  int h = 0, m = 0; const bool valid = localFields(h, m);
  setBacklightPercent(effectiveBrightness(app, h, valid));
  lastBrightness = millis();
}
void applyTimezone() {            // POSIX rule saved from the settings page (default Vietnam)
  configTzTime(app.cfg.tz, "pool.ntp.org", "time.google.com");
  dirty = true;
}
void showBanner(const char* message, uint32_t ms) {
  strlcpy(app.banner, message, sizeof app.banner); app.bannerUntil = millis() + ms; dirty = true;
}
void markCfg() { cfgDirty = true; cfgChangedAt = millis(); }
void markCounter() { counterDirty = true; counterChangedAt = millis(); }

void saveCfg() {
  app.cfg.workMin = app.pomo.workMin; app.cfg.alarmHour = app.alarm.hour; app.cfg.alarmMinute = app.alarm.minute;
  app.cfg.armed = app.alarm.armed ? 1 : 0;
  prefs.putUChar("work", app.cfg.workMin); prefs.putUChar("aH", app.cfg.alarmHour); prefs.putUChar("aM", app.cfg.alarmMinute);
  prefs.putUChar("armed", app.cfg.armed); prefs.putUChar("tune", app.cfg.tune); prefs.putUChar("vol", app.cfg.volume);
  prefs.putUChar("bright", app.cfg.brightIndex); prefs.putUChar("night", app.cfg.night);
  prefs.putUChar("lang", app.cfg.lang); prefs.putString("tz", app.cfg.tz); prefs.putString("tzname", app.cfg.tzLabel);
  cfgDirty = false;
}
void saveTasks() {
  uint8_t buffer[TASK_STORAGE_MAX];
  const size_t length = app.tasks.serialize(buffer, sizeof buffer);
  if (length) prefs.putBytes("tasks", buffer, length);
}
void settingsChanged() {         // language or time zone changed from the web page
  setUiLanguage(app.cfg.lang); saveCfg(); applyTimezone(); applyBrightness();
}
void tasksChanged() {            // called after any change from the screen or the web page
  saveTasks(); app.taskScroll = clampTaskScroll(app.taskScroll, app.tasks.count());
  Serial.printf("POSTIT %d task(s), %d done\n", app.tasks.count(), app.tasks.doneCount());
  dirty = true;
}
void loadAll() {
  Config& c = app.cfg;
  c.workMin = prefs.getUChar("work", 25); c.alarmHour = prefs.getUChar("aH", 7); c.alarmMinute = prefs.getUChar("aM", 0);
  c.armed = prefs.getUChar("armed", 0) ? 1 : 0; c.tune = prefs.getUChar("tune", 0); c.volume = prefs.getUChar("vol", 3);
  c.brightIndex = prefs.getUChar("bright", BRIGHT_LEVEL_COUNT - 1); c.night = prefs.getUChar("night", 0) ? 1 : 0;
  c.lang = prefs.getUChar("lang", 0) == 1 ? 1 : 0;
  {
    char tz[sizeof c.tz] = "", label[sizeof c.tzLabel] = "";
    prefs.getString("tz", tz, sizeof tz); prefs.getString("tzname", label, sizeof label);
    if (tzRuleValid(tz) && label[0]) { strlcpy(c.tz, tz, sizeof c.tz); strlcpy(c.tzLabel, label, sizeof c.tzLabel); }
  }
  if (c.workMin < 1 || c.workMin > 240) c.workMin = 25;
  if (c.alarmHour > 23) c.alarmHour = 7;
  if (c.alarmMinute > 59) c.alarmMinute = 0;
  if (c.tune >= TUNE_COUNT) c.tune = 0;
  if (c.volume > VOLUME_MAX) c.volume = 3;
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

void changePage(int next) {
  next = wrapPage(next);
  if (app.page == PG_ALARM && next != PG_ALARM && app.previewing) { soundStop(); app.previewing = false; }
  if (next != PG_COUNT) app.counterConfirm = false;
  app.page = next; dirty = true;
}

// ---------------- actions ----------------
void startRing() {
  if (app.previewing) app.previewing = false;
  soundPlay(TUNES[app.cfg.tune], true, app.cfg.volume);
  dirty = true;
  Serial.println("ALARM ringing");
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
    case A_TUNE_PREV: app.cfg.tune = (app.cfg.tune + TUNE_COUNT - 1) % TUNE_COUNT; markCfg(); if (app.previewing) soundPlay(TUNES[app.cfg.tune], false, app.cfg.volume); break;
    case A_TUNE_NEXT: app.cfg.tune = (app.cfg.tune + 1) % TUNE_COUNT; markCfg(); if (app.previewing) soundPlay(TUNES[app.cfg.tune], false, app.cfg.volume); break;
    case A_TEST:
      if (app.previewing) { soundStop(); app.previewing = false; }
      else { soundPlay(TUNES[app.cfg.tune], false, app.cfg.volume); app.previewing = true; }
      break;
    case A_VOL_MINUS: if (app.cfg.volume > 0) { --app.cfg.volume; markCfg(); } break;
    case A_VOL_PLUS: if (app.cfg.volume < VOLUME_MAX) { ++app.cfg.volume; markCfg(); } break;
    case R_SNOOZE: app.alarm.snooze(now); soundStop(); break;
    case R_STOP: app.alarm.stop(); soundStop(); break;
    case P_BRIGHT_MINUS: if (app.cfg.brightIndex > 0) { --app.cfg.brightIndex; markCfg(); applyBrightness(); } break;
    case P_BRIGHT_PLUS: if (app.cfg.brightIndex < BRIGHT_LEVEL_COUNT - 1) { ++app.cfg.brightIndex; markCfg(); applyBrightness(); } break;
    case P_VOL_MINUS: if (app.cfg.volume > 0) { --app.cfg.volume; markCfg(); soundPlay(BEEP_PREVIEW_OK, false, app.cfg.volume); } break;
    case P_VOL_PLUS: if (app.cfg.volume < VOLUME_MAX) { ++app.cfg.volume; markCfg(); soundPlay(BEEP_PREVIEW_OK, false, app.cfg.volume); } break;
    case P_NIGHT: app.cfg.night = app.cfg.night ? 0 : 1; markCfg(); applyBrightness(); break;
    case P_LANG: app.cfg.lang = app.cfg.lang ? 0 : 1; setUiLanguage(app.cfg.lang); markCfg(); break;
    case P_CALIBRATE: startCalibration(); break;
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
  if (startY >= 222 && endY >= 222) { changePage(pageFromFooter(endX)); return; }
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
  Serial.println("Desk Watch CYD V1");
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
  improvBegin(prefs);                                // Wi-Fi can be set from the web installer over USB
  {
    char ssid[33], password[65];
    if (wifiLoadSaved(prefs, ssid, sizeof ssid, password, sizeof password)) WiFi.begin(ssid, password);
    else if (strcmp(WIFI_SSID, "your-wifi-name") != 0) WiFi.begin(WIFI_SSID, WIFI_PASSWORD);   // Secrets.h, for people building from source
  }
  applyTimezone();
  beginTaskWeb(app.tasks, tasksChanged, app.cfg, settingsChanged);
  bool touchedAtBoot = false;
  for (int i = 0; i <= 20; ++i) {                    // splash with a loading bar (about 1.4 s); touch it to calibrate
    renderSplash(i * 5);
    if (touchReady && touchPanel.touched() && touchPanel.getPoint().z >= 200) touchedAtBoot = true;
    delay(60);
  }
  lastTick = millis(); dirty = true;
  if (touchedAtBoot) startCalibration();
}
void loop() {
  const uint32_t now = millis();
  pollTouch();
  handleTaskWeb();
  improvPoll();
  soundTick();

  // ---- background logic: runs on every page ----
  const uint32_t dt = now - lastTick; lastTick = now;
  const PomoEvent event = app.pomo.tick(dt);
  int hour = 0, minute = 0; const bool clockValid = localFields(hour, minute);
  if (event != POMO_NONE) {
    const bool quiet = app.cfg.night && clockValid && isNight(hour);
    if (!app.alarm.ringing && !quiet) soundPlay(event == POMO_WORK_DONE ? BEEP_WORK_DONE : BEEP_BREAK_DONE, false, app.cfg.volume);
    showBanner(event == POMO_BREAK_DONE ? tr("Break over - back to work", "Pause finie - au travail") : app.pomo.phase == POMO_LONG ? tr("Work done - long break!", "Travail fini - longue pause !") : tr("Work done - take a break", "Travail fini - faites une pause"), 6000);
  }
  if (app.alarm.update(hour, minute, clockValid, now)) startRing();
  if (app.alarm.ringTimeout(now)) { soundStop(); dirty = true; }
  if (app.previewing && !soundActive()) { app.previewing = false; dirty = true; }
  if (app.counterConfirm && now - app.counterConfirmAt > 5000) { app.counterConfirm = false; dirty = true; }
  if (cfgDirty && now - cfgChangedAt > 1500) saveCfg();
  if (counterDirty && now - counterChangedAt > 3000) { prefs.putUShort("cnt", app.counter.value); counterDirty = false; }
  if (now - lastBrightness > 30000) applyBrightness();   // follows the night mode as the clock moves on

  // ---- redraw ----
  static time_t lastSecond = 0, lastMinute = 0; static wl_status_t lastWifi = WL_NO_SHIELD; static bool bannerShown = false;
  const time_t second = time(nullptr);
  if (second != lastSecond) {
    lastSecond = second;
    if (app.page == PG_CLOCK || app.page == PG_TIMER) dirty = true;
    if (second / 60 != lastMinute) { lastMinute = second / 60; dirty = true; }   // the header clock shows hours:minutes on every page
  }
  const wl_status_t wifi = WiFi.status();
  if (wifi != lastWifi) { lastWifi = wifi; if (app.page == PG_CLOCK || app.page == PG_NOTES || app.page == PG_SETUP) dirty = true; }
  const bool bannerNow = now < app.bannerUntil;
  if (bannerNow != bannerShown) { bannerShown = bannerNow; dirty = true; }
  if (app.alarm.ringing && now - lastDraw >= 250) dirty = true;
  if (app.page == PG_CHRONO && app.watch.running && now - lastDraw >= 100) dirty = true;
  if (dirty && !calibrating) { renderApp(app, now); dirty = false; lastDraw = now; }
  delay(3);
}
