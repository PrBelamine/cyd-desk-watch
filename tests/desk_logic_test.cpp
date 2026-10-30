#include "DeskLogic.h"
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
static void testNavigation() {
  assert(wrapPage(-1) == 6 && wrapPage(7) == 0 && PAGE_TOTAL == 7);
  assert(pageFromFooter(0) == 0 && pageFromFooter(45) == 0 && pageFromFooter(46) == 1 && pageFromFooter(319) == 6 && pageFromFooter(160) == 3);
  assert(pageAfterSwipe(2, -60, 5) == 3 && pageAfterSwipe(0, 60, 5) == 6 && pageAfterSwipe(2, 20, 0) == 2 && pageAfterSwipe(2, -60, 80) == 2);
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
  testPomodoro(); testStopwatch(); testCounter(); testAlarm(); testTunes(); testTouch(); testNavigation(); testFormat();
  puts("desk_logic_test ok");
}
