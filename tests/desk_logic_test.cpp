#include "DeskLogic.h"
#include "Weather.h"
#include <assert.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

static void testPomodoro() {
  assert(pomoWorkMinutes(0) == 1 && pomoWorkMinutes(24) == 25 && pomoWorkMinutes(59) == 60);
  assert(pomoWorkMinutes(60) == 65 && pomoWorkMinutes(95) == 240 && pomoWorkMinutes(-5) == 1 && pomoWorkMinutes(500) == 240);
  assert(pomoWorkIndex(25) == 24 && pomoWorkIndex(240) == 95 && pomoWorkIndex(62) == 59 && pomoWorkIndex(63) == 60 && pomoWorkIndex(64) == 60);
  Pomodoro p; assert(p.idle() && p.remainingMs == 25 * 60000UL);
  p.changeWork(+1); assert(p.workMin == 26 && p.remainingMs == 26 * 60000UL);
  p.changeWork(-30); assert(p.workMin == 1);
  p.changeWork(+200); assert(p.workMin == 240);
  p.workMin = 1; p.remainingMs = 60000;
  assert(p.tick(5000) == POMO_NONE && p.remainingMs == 60000);         // not running: nothing happens
  p.startPause(); assert(p.running && !p.idle());
  p.changeWork(+1); assert(p.workMin == 1);                            // locked while running
  assert(p.tick(59999) == POMO_NONE && p.remainingMs == 1);
  assert(p.tick(1) == POMO_WORK_DONE && p.phase == POMO_SHORT && p.sessions == 1 && p.remainingMs == 5 * 60000UL);
  assert(p.tick(5 * 60000UL) == POMO_BREAK_DONE && p.phase == POMO_WORK && p.remainingMs == 60000);
  for (int i = 2; i <= 4; ++i) {                                       // sessions 2..4: the 4th is followed by a long break
    assert(p.tick(60000) == POMO_WORK_DONE);
    if (i < 4) { assert(p.phase == POMO_SHORT); assert(p.tick(5 * 60000UL) == POMO_BREAK_DONE); }
  }
  assert(p.phase == POMO_LONG && p.sessions == 4 && p.remainingMs == 15 * 60000UL);
  p.startPause(); assert(!p.running);
  p.reset(); assert(p.idle() && p.sessions == 0 && p.workMin == 1);
}
static void testStopwatch() {
  Stopwatch s; assert(s.idle() && s.elapsed(1000) == 0);
  s.toggle(1000); assert(s.running && s.elapsed(1750) == 750);
  s.toggle(2000); assert(!s.running && s.elapsed(9999) == 1000 && !s.idle());
  s.toggle(5000); assert(s.elapsed(5500) == 1500);                      // resumes from the accumulated time
  s.reset(); assert(s.idle());
  s.toggle(4294967000u); assert(s.elapsed(300) == 596);                 // millis() wrap-around
}
static void testCounter() {
  Counter c; assert(!c.decrement() && c.increment() && c.value == 1 && c.decrement() && c.value == 0);
  c.value = COUNTER_MAX; assert(!c.increment() && c.value == COUNTER_MAX);
  c.active = false; assert(!c.increment() && !c.decrement() && c.value == COUNTER_MAX);
  c.reset(); assert(c.value == 0);
}
static void testAlarm() {
  AlarmClock a; a.hour = 6; a.minute = 30;
  assert(!a.update(6, 30, true, 0));                                   // not armed
  a.setArmed(true);
  assert(!a.update(6, 29, true, 0) && !a.update(6, 30, false, 0));     // wrong minute / clock not valid
  assert(a.update(6, 30, true, 1000) && a.ringing);
  assert(!a.update(6, 30, true, 2000));                                // already ringing
  assert(!a.ringTimeout(1000 + ALARM_RING_MAX_MS - 1));
  assert(a.ringTimeout(1000 + ALARM_RING_MAX_MS) && !a.ringing && a.snoozing);
  const uint32_t t = 1000 + ALARM_RING_MAX_MS;
  assert(!a.update(6, 31, true, t + ALARM_SNOOZE_MS - 1));
  assert(a.update(6, 35, true, t + ALARM_SNOOZE_MS) && a.ringing);     // rings again 5 min later
  a.stop(); assert(!a.ringing && !a.snoozing);
  assert(!a.update(6, 30, true, 9000000));                             // same minute already fired today
  assert(!a.update(6, 31, true, 9000001));
  a.adjustMinute(+1); a.adjustMinute(-1); assert(a.update(6, 30, true, 9100000));   // editing re-arms the minute
  a.snooze(100); assert(a.snoozing && !a.ringing); a.setArmed(false); assert(!a.snoozing);
  a.hour = 23; a.adjustHour(+1); assert(a.hour == 0); a.adjustHour(-1); assert(a.hour == 23);
  a.minute = 59; a.adjustMinute(+1); assert(a.minute == 0); a.adjustMinute(-1); assert(a.minute == 59);
}
static void testTunes() {
  for (int i = 0; i < TUNE_COUNT; ++i) {
    assert(TUNES[i].count > 8 && TUNES[i].name && *TUNES[i].name);
    bool hasNote = false;
    for (int n = 0; n < TUNES[i].count; ++n) { assert(TUNES[i].lengths[n] > 0); hasNote |= TUNES[i].notes[n] > 0; }
    assert(hasNote);
  }
  assert(BEEP_WORK_DONE.count == 6 && BEEP_BREAK_DONE.count == 4);
}
static void testTouch() {
  int x, y; const TouchCal d = defaultTouchCal();
  mapTouch(d, 200, 200, x, y); assert(x == 0 && y == 0);
  mapTouch(d, 3800, 3800, x, y); assert(x == 319 && y == 239);
  mapTouch(d, -500, 9000, x, y); assert(x == 0 && y == 239);
  // calibration from three touches on a rotated, offset panel
  const int scr[3][2] = {{20, 20}, {300, 20}, {160, 210}};
  int raw[3][2];
  const int rawTrue[3][2] = {{500, 400}, {3600, 450}, {2000, 3300}};
  for (int i = 0; i < 3; ++i) { raw[i][0] = rawTrue[i][0]; raw[i][1] = rawTrue[i][1]; }
  TouchCal cal; assert(fitTouchCal(raw, scr, cal));
  for (int i = 0; i < 3; ++i) { mapTouch(cal, raw[i][0], raw[i][1], x, y); assert(abs(x - scr[i][0]) <= 1 && abs(y - scr[i][1]) <= 1); }
  mapTouch(cal, 2050, 1200, x, y); assert(x > 100 && x < 200 && y > 60 && y < 130);   // an interior point lands inside
  const int aligned[3][2] = {{500, 500}, {1500, 1500}, {2500, 2500}};
  assert(!fitTouchCal(aligned, scr, cal));
  const int tiny[3][2] = {{1000, 1000}, {1010, 1000}, {1000, 1010}};
  assert(!fitTouchCal(tiny, scr, cal));                                // points too close: scale is absurd
  assert(inside({10, 10, 20, 20}, 10, 10) && !inside({10, 10, 20, 20}, 30, 15) && !inside({10, 10, 20, 20}, 9, 15));
}
static void testRadio() {
    // radios editable at runtime
  assert(radioValid("FIP", "http://icecast.radiofrance.fr/fip-midfi.mp3") && !radioValid("", "http://a.b/c") && !radioValid("FIP", "https://a.b/c") && !radioValid("FIP", "http://a b") && !radioValid("FIP", "ftp://a.b"));
  assert(!radioValid("Seventeen chars ok", "http://a.b/c") && radioValid("Sixteen chars ok!", "http://a.b/c") == (strlen("Sixteen chars ok!") <= 16));
  assert(!radioValid("a\"b", "http://a.b/c") && !radioValid(" lead", "http://a.b/c") && !radioValid("caf\xc3\xa9", "http://a.b/c"));
  { char longUrl[200]; memcpy(longUrl, "http://", 7); memset(longUrl + 7, 'a', 190); longUrl[197] = 0; assert(!radioValid("X", longUrl)); longUrl[7 + 152] = 0; assert(radioValid("X", longUrl)); }
  { const RadioStation keep = RADIOS[1];
    assert(radioSet(1, "My radio", "http://x.y/z.mp3") && strcmp(RADIOS[1].name, "My radio") == 0 && strcmp(RADIOS[1].url, "http://x.y/z.mp3") == 0);
    assert(!radioSet(1, "Bad", "https://x.y") && strcmp(RADIOS[1].name, "My radio") == 0 && !radioSet(3, "A", "http://a.b") && !radioSet(-1, "A", "http://a.b"));
    radioDefaults(); assert(strcmp(RADIOS[1].name, keep.name) == 0 && strcmp(RADIOS[1].url, keep.url) == 0); }
  assert(RADIO_COUNT == 3 && ALARM_SOUND_COUNT == TUNE_COUNT + RADIO_COUNT);
  for (int i = 0; i < RADIO_COUNT; ++i) {
    assert(RADIOS[i].name && *RADIOS[i].name && strncmp(RADIOS[i].url, "http://", 7) == 0);
    assert(alarmSoundIsRadio(TUNE_COUNT + i) && alarmSoundStation(TUNE_COUNT + i) == i && strcmp(alarmSoundName(TUNE_COUNT + i), RADIOS[i].name) == 0);
  }
  assert(!alarmSoundIsRadio(0) && !alarmSoundIsRadio(TUNE_COUNT - 1) && !alarmSoundIsRadio(ALARM_SOUND_COUNT) && strcmp(alarmSoundName(0), TUNES[0].name) == 0);
  assert(strcmp(alarmSoundName(99), TUNES[0].name) == 0);
  // fallback: melody takes over if the radio is not playing RADIO_FALLBACK_MS after the start, once only
  RadioWatch w; w.start(1000);
  assert(!w.update(1000, false) && !w.update(1000 + RADIO_FALLBACK_MS - 1, false));
  assert(w.update(1000 + RADIO_FALLBACK_MS, false) && w.fallback);
  assert(!w.update(40000, false) && !w.update(41000, true));           // only once
  // 'now' slightly older than the start time (same loop turn): no fallback, and the countdown still works afterwards
  { RadioWatch u; u.start(5000); assert(!u.update(4990, false) && !u.fallback);
    assert(!u.update(5000 + RADIO_FALLBACK_MS - 1, false) && u.update(5000 + RADIO_FALLBACK_MS, false)); }
  // radio playing: the countdown restarts each time it is heard; it plays, drops, and takes RADIO_FALLBACK_MS of silence to give up
  RadioWatch v; v.start(0);
  assert(!v.update(5000, true) && !v.update(5000 + RADIO_FALLBACK_MS - 1, false) && v.update(5000 + RADIO_FALLBACK_MS, false));
}
static void testNavigation() {
  assert(wrapPage(-1) == 10 && wrapPage(11) == 0 && PAGE_TOTAL == 11);
  assert(pageRow(0) == 0 && pageRow(5) == 0 && pageRow(PAGE_SETUP) == 1 && pageRow(PAGE_CLIMATE) == 1);
  // row 1: six apps then the down arrow
  assert(footerTarget(0, 0) == 0 && footerTarget(0, 45) == 0 && footerTarget(0, 46) == 1 && footerTarget(0, 250) == 5 && footerTarget(0, 319) == FOOT_DOWN);
  // row 2: Climate, Setup, empty slots, then the up arrow
  assert(footerTarget(1, 10) == PAGE_CLIMATE && footerTarget(1, 60) == PAGE_WEATHER && footerTarget(1, 100) == PAGE_RADIO && footerTarget(1, 140) == PAGE_SLEEP && footerTarget(1, 190) == PAGE_SETUP && footerTarget(1, 230) == FOOT_NONE && footerTarget(1, 250) == FOOT_NONE && footerTarget(1, 319) == FOOT_UP);
  assert(pageRow(PAGE_WEATHER) == 1 && pageRow(PAGE_RADIO) == 1);
  assert(pageAfterSwipe(2, -60, 5) == 3 && pageAfterSwipe(0, 60, 5) == 5 && pageAfterSwipe(5, -60, 5) == 0);   // row 1 wraps on itself
  assert(pageAfterSwipe(2, 20, 0) == 2 && pageAfterSwipe(2, -60, 80) == 2);
  assert(pageAfterSwipe(PAGE_CLIMATE, -60, 0) == PAGE_WEATHER && pageAfterSwipe(PAGE_WEATHER, -60, 0) == PAGE_RADIO && pageAfterSwipe(PAGE_RADIO, -60, 0) == PAGE_SLEEP && pageAfterSwipe(PAGE_SLEEP, -60, 0) == PAGE_SETUP && pageAfterSwipe(PAGE_SETUP, -60, 0) == PAGE_CLIMATE);
  assert(pageAfterSwipe(PAGE_CLIMATE, 60, 0) == PAGE_SETUP && pageAfterSwipe(PAGE_SETUP, 60, 0) == PAGE_SLEEP && pageAfterSwipe(PAGE_SLEEP, 60, 0) == PAGE_RADIO && pageAfterSwipe(PAGE_RADIO, 60, 0) == PAGE_WEATHER);
}
static void testSleep() {
  SleepTimer t; assert(!t.active && t.remaining(0) == 0 && !t.update(0));
  t.start(1000, 30); assert(t.active && t.remaining(1000) == 30UL * 60000UL && !t.update(1000 + 29UL * 60000UL));
  assert(t.fadeLevel(1000, 8) == 8);                                       // far from the end: full volume
  assert(t.fadeLevel(1000 + 30UL * 60000UL - 15000, 8) == 4);              // half way through the fade
  assert(t.fadeLevel(1000 + 30UL * 60000UL - 500, 8) == 1);                // never muted by the fade itself
  assert(t.update(1000 + 30UL * 60000UL) && !t.active && !t.update(1000 + 31UL * 60000UL));   // true once
  t.start(0, 10); t.cancel(); assert(!t.active && t.fadeLevel(5000, 6) == 6);
  const uint32_t base = 4294960000u; SleepTimer w; w.start(base, 10); assert(w.remaining(base) == 600000u && w.update(uint32_t(base + 600000u)));   // millis() wraps
  assert(!screenShouldSleep(0, 99UL * 60000UL) && screenShouldSleep(1, 60000) && !screenShouldSleep(1, 59999) && screenShouldSleep(3, 5UL * 60000UL) && !screenShouldSleep(3, 299999));
  assert(!screenShouldSleep(9, 99UL * 60000UL) && SCREEN_SLEEP_MIN[0] == 0 && SLEEP_TIMER_MIN[SLEEP_TIMER_COUNT - 1] == 90);
}
static void testClimate() {
  uint8_t d[5] = {0x02, 0x2B, 0x01, 0x2E, 0};   // 55.5 % and 30.2 C
  d[4] = d[0] + d[1] + d[2] + d[3];
  float t = 0, h = 0; assert(decodeDht22(d, t, h) && fabsf(t - 30.2f) < 0.01f && fabsf(h - 55.5f) < 0.01f);
  d[4] ^= 1; assert(!decodeDht22(d, t, h));                                   // bad checksum
  uint8_t neg[5] = {0x01, 0xF4, 0x80, 0x65, 0}; neg[4] = neg[0] + neg[1] + neg[2] + neg[3];   // 50.0 %, -10.1 C
  assert(decodeDht22(neg, t, h) && fabsf(t + 10.1f) < 0.01f && fabsf(h - 50.0f) < 0.01f);
  uint8_t bad[5] = {0x04, 0xB0, 0x00, 0x64, 0}; bad[4] = bad[0] + bad[1] + bad[2] + bad[3];     // 120 % is impossible
  assert(!decodeDht22(bad, t, h));
  ClimateGuard g;
  assert(!g.update(30.0f, 50.0f, 32, 85) && !g.active());
  assert(g.update(32.0f, 50.0f, 32, 85) && g.tempHigh);                       // crossing: one alert
  assert(!g.update(33.0f, 50.0f, 32, 85));                                    // stays high: no second alert
  assert(!g.update(31.5f, 50.0f, 32, 85) && g.tempHigh);                      // inside the hysteresis band
  assert(!g.update(30.9f, 50.0f, 32, 85) && !g.tempHigh);                     // released
  assert(g.update(30.0f, 90.0f, 32, 85) && g.humHigh);
  assert(!g.update(30.0f, 83.0f, 32, 85) && g.humHigh && !g.update(30.0f, 81.9f, 32, 85) && !g.humHigh);
}
static void testWeather() {
  const char* json = "{\"latitude\":21.0,\"current_units\":{\"time\":\"iso8601\",\"temperature_2m\":\"\xC2\xB0""C\",\"weather_code\":\"wmo code\"},"
    "\"current\":{\"time\":\"2026-10-30T21:00\",\"interval\":900,\"temperature_2m\":27.4,\"relative_humidity_2m\":78,\"apparent_temperature\":31.2,\"weather_code\":61,\"wind_speed_10m\":12.6,\"is_day\":0},"
    "\"daily_units\":{\"time\":\"iso8601\",\"weather_code\":\"wmo code\"},"
    "\"daily\":{\"time\":[\"2026-10-30\",\"2026-10-31\",\"2026-11-01\",\"2026-11-02\"],\"weather_code\":[61,3,0,95],\"temperature_2m_max\":[29.1,28.0,30.5,26.2],\"temperature_2m_min\":[23.0,22.4,-1.5,21.0]}}";
  WeatherData w; assert(parseWeatherJson(json, w) && w.valid);
  assert(fabsf(w.tempC - 27.4f) < 0.01f && fabsf(w.feelsC - 31.2f) < 0.01f && fabsf(w.windKmh - 12.6f) < 0.01f && w.humidity == 78 && w.code == 61 && !w.day);
  assert(w.days == 4 && w.dayCode[0] == 61 && w.dayCode[3] == 95 && fabsf(w.dayMax[2] - 30.5f) < 0.01f && fabsf(w.dayMin[2] + 1.5f) < 0.01f);
  assert(!strcmp(w.dayDate[0], "2026-10-30") && !strcmp(w.dayDate[3], "2026-11-02"));
  assert(weekdayOf("2026-10-30") == 5 && weekdayOf("2026-11-01") == 0 && weekdayOf("2000-01-01") == 6 && weekdayOf("bad") == -1);   // Friday, Sunday, Saturday
  assert(weatherKind(0) == WX_CLEAR && weatherKind(2) == WX_PARTLY && weatherKind(3) == WX_CLOUDY && weatherKind(45) == WX_FOG && weatherKind(53) == WX_DRIZZLE);
  assert(weatherKind(65) == WX_RAIN && weatherKind(81) == WX_RAIN && weatherKind(73) == WX_SNOW && weatherKind(96) == WX_STORM);
  WeatherData bad; assert(!parseWeatherJson("{\"error\":true}", bad) && !bad.valid);
  assert(!parseWeatherJson("{\"current\":{\"temperature_2m\":20}}", bad));                // humidity and code missing
  const char* noDaily = "{\"current\":{\"temperature_2m\":20,\"relative_humidity_2m\":50,\"weather_code\":1}}";
  assert(parseWeatherJson(noDaily, bad) && bad.days == 0 && bad.day);
}
static void testFormat() {
  char b[16];
  formatTimer(b, sizeof b, 25 * 60000UL); assert(!strcmp(b, "25:00"));
  formatTimer(b, sizeof b, 1); assert(!strcmp(b, "00:01"));              // rounds up like a countdown
  formatTimer(b, sizeof b, 3600000UL + 5 * 60000UL); assert(!strcmp(b, "1:05:00"));
  formatTimer(b, sizeof b, 0); assert(!strcmp(b, "00:00"));
  formatStopwatch(b, sizeof b, 61234); assert(!strcmp(b, "01:01.2"));
  formatStopwatch(b, sizeof b, 3723000UL); assert(!strcmp(b, "1:02:03"));
  formatWorkDuration(b, sizeof b, 25); assert(!strcmp(b, "25 min"));
  formatWorkDuration(b, sizeof b, 65); assert(!strcmp(b, "1 h 05"));
  assert(isNight(22) && isNight(23) && isNight(0) && isNight(5) && !isNight(6) && !isNight(21));
}
int main() {
  testPomodoro(); testStopwatch(); testCounter(); testAlarm(); testTunes(); testTouch(); testNavigation(); testSleep(); testRadio(); testClimate(); testWeather(); testFormat();
  puts("desk_logic_test ok");
}
