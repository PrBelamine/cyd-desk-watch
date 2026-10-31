// Drawing for every page of the Desk Watch CYD. All pages are composed in an off-screen 8-bit
// sprite and pushed once, so nothing flickers.
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <string.h>
#include <time.h>
#include "App.h"
#include "Settings.h"
#include "Font15.h"
#include "Font60.h"
#include "Font44.h"

#if !defined(TFT_BL) || !defined(SMOOTH_FONT)
#error "TFT_eSPI is not configured for the CYD: copy User_Setup_CYD.h over TFT_eSPI/User_Setup.h (see README), then Verify again."
#endif

TFT_eSPI tft;
const uint8_t BRIGHT_LEVELS[BRIGHT_LEVEL_COUNT] = {5, 10, 20, 30, 50, 70, 85, 100};

namespace {
TFT_eSprite screen(&tft);
constexpr uint16_t BG = 0x0000, WHITE = 0xF7BF, MUTED = 0xADFA, LINE = 0x220A, CYAN = 0x667F, YELLOW = 0xF727;
constexpr uint16_t GREEN = 0x47B0, ORANGE = 0xFDC9, RED = 0xF9A6, DARKRED = 0x8000, PURPLE = 0x92DB, TEAL = 0x0659, DIM = 0x4208, PINK = 0xFD52;
// Post-it page: yellow note, brown ink.
constexpr uint16_t NOTE = 0xFF0C, INK = 0x3961, DONE = 0x9408, RULE = 0xE648, TICK = 0x2C67;
uint16_t textBackground = BG;       // smooth fonts blend their edges with this colour
int selectedFont = -1;
bool backlightAttached = false;
uint16_t buttonBack = BG;          // colour behind the labels of outline buttons (the ringing screen flashes)

void font(int id) {
  if (selectedFont == id) return;
  screen.unloadFont(); screen.setTextSize(1);
  if (id == 2) screen.loadFont(Font15);
  else if (id == 3) screen.loadFont(Font60);
  else if (id == 4) screen.loadFont(Font44);
  else screen.setTextFont(1);
  selectedFont = id;
}
// fontId: 1 = built-in 6x8, 2 = 15 px with accents, 3 = 60 px digits, 4 = 44 px digits.  align: 0 left, 1 centre, 2 right.
int text(const char* value, int x, int y, int width, uint16_t color, int fontId = 2, int align = 0) {
  font(fontId);
  char buffer[96]; strncpy(buffer, value, sizeof buffer - 1); buffer[sizeof buffer - 1] = '\0';
  size_t length = strlen(buffer);
  if (screen.textWidth(buffer) > width) {
    while (length && screen.textWidth(String(buffer) + "..") > width) { length = utf8DropLast(buffer, length); buffer[length] = '\0'; }
    strncat(buffer, "..", sizeof buffer - strlen(buffer) - 1);
  }
  const int measured = screen.textWidth(buffer);
  int start = x;
  if (align == 1) start += (width - measured) / 2;
  else if (align == 2) start += width - measured;
  screen.setTextDatum(TL_DATUM); screen.setTextPadding(0); screen.setTextColor(color, textBackground);
  screen.drawString(buffer, start, y);
  return measured;
}
void divider(int y) { screen.drawFastHLine(10, y, 300, LINE); }

struct LocalTime { bool valid = false; int h = 0, m = 0, s = 0; struct tm tm = {}; };
LocalTime localNow() {
  LocalTime t; const time_t stamp = time(nullptr);
  if (stamp < 1700000000) return t;
  localtime_r(&stamp, &t.tm); t.valid = true; t.h = t.tm.tm_hour; t.m = t.tm.tm_min; t.s = t.tm.tm_sec;
  return t;
}
bool wifiUp() { return WiFi.status() == WL_CONNECTED; }

uint8_t gLang = 0;                       // 0 = English, 1 = Français (set from the saved settings)
const char* L(const char* en, const char* fr) { return gLang == 1 ? fr : en; }
// Weekday and month names written out so the date does not depend on the C library's locale.
void formatDate(char* out, size_t cap, const struct tm& t) {
  static const char* const dayEn[7] = {"Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"};
  static const char* const dayFr[7] = {"dimanche", "lundi", "mardi", "mercredi", "jeudi", "vendredi", "samedi"};
  static const char* const monEn[12] = {"January", "February", "March", "April", "May", "June", "July", "August", "September", "October", "November", "December"};
  static const char* const monFr[12] = {"janvier", "février", "mars", "avril", "mai", "juin", "juillet", "août", "septembre", "octobre", "novembre", "décembre"};
  const int wd = t.tm_wday % 7, mo = t.tm_mon % 12;
  if (gLang == 1) snprintf(out, cap, "%s %d %s %d", dayFr[wd], t.tm_mday, monFr[mo], t.tm_year + 1900);
  else snprintf(out, cap, "%s %d %s %d", dayEn[wd], t.tm_mday, monEn[mo], t.tm_year + 1900);
}

uint16_t accentFor(const AppState& s, int page) {
  switch (page) {
    case PG_CLOCK: return CYAN;
    case PG_TIMER: return s.pomo.phase == POMO_WORK ? ORANGE : GREEN;
    case PG_CHRONO: return PURPLE;
    case PG_COUNT: return TEAL;
    case PG_ALARM: return YELLOW;
    case PG_CLIMATE: return s.climateGuard.active() ? RED : GREEN;
    case PG_WEATHER: return CYAN;
    case PG_RADIO: return s.radio != RADIO_OFF ? GREEN : PINK;
    case PG_SLEEP: return PURPLE;
    default: return CYAN;
  }
}

// ---------------- buttons ----------------
uint8_t alarmVolume(const AppState& s) { return alarmSoundIsRadio(s.cfg.tune) ? s.cfg.radioVolume : s.cfg.volume; }   // volume the alarm page edits
bool timerLocked(const AppState& s) { return !s.pomo.idle(); }
void describe(const AppState& s, Action a, char* label, size_t n, bool& primary, bool& enabled) {
  primary = false; enabled = true; label[0] = '\0';
  switch (a) {
    case T_MINUS: strlcpy(label, "-", n); enabled = !timerLocked(s); break;
    case T_PLUS: strlcpy(label, "+", n); enabled = !timerLocked(s); break;
    case T_START: strlcpy(label, s.pomo.running ? L("PAUSE", "PAUSE") : s.pomo.idle() ? L("START", "LANCER") : L("RESUME", "REPRISE"), n); primary = !s.pomo.running; break;
    case T_RESET: strlcpy(label, L("RESET", "RAZ"), n); enabled = !s.pomo.idle(); break;
    case S_START: strlcpy(label, s.watch.running ? "STOP" : L("START", "LANCER"), n); primary = !s.watch.running; break;
    case S_RESET: strlcpy(label, L("RESET", "RAZ"), n); enabled = !s.watch.idle(); break;
    case C_MINUS: strlcpy(label, "-", n); enabled = s.counter.active; break;
    case C_PLUS: strlcpy(label, "+", n); enabled = s.counter.active; primary = s.counter.active; break;
    case C_LOCK: strlcpy(label, s.counter.active ? L("LOCK", "BLOQUER") : L("UNLOCK", "LIBÉRER"), n); break;
    case C_RESET: strlcpy(label, L("RESET", "RAZ"), n); enabled = s.counter.value != 0; break;
    case C_YES: strlcpy(label, L("YES, RESET", "OUI, RAZ"), n); primary = true; break;
    case C_NO: strlcpy(label, L("NO", "NON"), n); break;
    case A_TOGGLE: strlcpy(label, s.alarm.armed ? "ON" : "OFF", n); primary = s.alarm.armed; break;
    case A_TUNE_PREV: strlcpy(label, "<", n); break;
    case A_TUNE_NEXT: strlcpy(label, ">", n); break;
    case A_TEST: strlcpy(label, s.previewing ? "STOP" : "TEST", n); break;
    case A_VOL_MINUS: strlcpy(label, "-", n); enabled = alarmVolume(s) > 0; break;
    case A_VOL_PLUS: strlcpy(label, "+", n); enabled = alarmVolume(s) < VOLUME_MAX; break;
    case R_SNOOZE: strlcpy(label, L("SNOOZE 5 MIN", "REPORT 5 MIN"), n); primary = true; break;
    case R_STOP: strlcpy(label, "STOP", n); break;
    case P_BRIGHT_MINUS: strlcpy(label, "-", n); enabled = s.cfg.brightIndex > 0; break;
    case P_BRIGHT_PLUS: strlcpy(label, "+", n); enabled = s.cfg.brightIndex < BRIGHT_LEVEL_COUNT - 1; break;
    case P_VOL_MINUS: strlcpy(label, "-", n); enabled = s.cfg.volume > 0; break;
    case P_VOL_PLUS: strlcpy(label, "+", n); enabled = s.cfg.volume < VOLUME_MAX; break;
    case P_NIGHT: strlcpy(label, s.cfg.night ? "Auto 22:00-06:00" : L("Off", "Non"), n); break;
    case P_CALIBRATE: strlcpy(label, L("Calibrate touch", "Calibrer le tactile"), n); break;
    case P_LANG: strlcpy(label, gLang == 1 ? "Français" : "English", n); break;
    case K_TEMP_MINUS: strlcpy(label, "-", n); enabled = s.cfg.tempMax > TEMP_ALERT_MIN; break;
    case K_TEMP_PLUS: strlcpy(label, "+", n); enabled = s.cfg.tempMax < TEMP_ALERT_MAX; break;
    case K_HUM_MINUS: strlcpy(label, "-", n); enabled = s.cfg.humMax > HUM_ALERT_MIN; break;
    case K_HUM_PLUS: strlcpy(label, "+", n); enabled = s.cfg.humMax < HUM_ALERT_MAX; break;
    case K_ALERT: strlcpy(label, s.cfg.climateAlert ? L("Alert ON", "Alerte ON") : L("Alert OFF", "Alerte OFF"), n); primary = s.cfg.climateAlert; break;
    case D_PREV: strlcpy(label, "<", n); break;
    case D_NEXT: strlcpy(label, ">", n); break;
    case D_PLAY: strlcpy(label, s.radio != RADIO_OFF ? "STOP" : L("PLAY", "ÉCOUTER"), n); primary = s.radio == RADIO_OFF; break;
    case D_VOL_MINUS: strlcpy(label, "-", n); enabled = s.cfg.radioVolume > 1; break;
    case D_VOL_PLUS: strlcpy(label, "+", n); enabled = s.cfg.radioVolume < VOLUME_MAX; break;
    case D_NOISE: {
      static const char* const en[3] = {"Noise: Off", "Noise: Soft", "Noise: Strong"};
      static const char* const fr[3] = {"Bruit: Non", "Bruit: Leger", "Bruit: Fort"};
      strlcpy(label, (gLang == 1 ? fr : en)[s.cfg.radioNoise > 2 ? 1 : s.cfg.radioNoise], n); break;
    }
    case V_SCR_MINUS: strlcpy(label, "-", n); enabled = s.cfg.screenSleep > 0; break;
    case V_SCR_PLUS: strlcpy(label, "+", n); enabled = s.cfg.screenSleep < SCREEN_SLEEP_COUNT - 1; break;
    case V_TMR_MINUS: strlcpy(label, "-", n); enabled = !s.sleep.active && s.cfg.sleepTimer > 0; break;
    case V_TMR_PLUS: strlcpy(label, "+", n); enabled = !s.sleep.active && s.cfg.sleepTimer < SLEEP_TIMER_COUNT - 1; break;
    case V_GO: {
      if (s.sleep.active) {
        char t[12]; formatTimer(t, sizeof t, s.sleep.remaining(millis()));
        snprintf(label, n, "%s %s", L("CANCEL", "ANNULER"), t);
      } else strlcpy(label, L("SLEEP", "DORMIR"), n);
      primary = !s.sleep.active; break;
    }
    case V_SCREEN_OFF: strlcpy(label, L("Screen off now", "Ecran en veille"), n); break;
    case P_WIFI: strlcpy(label, s.wifiOff || !wifiUp() ? "ON" : "OFF", n); primary = s.wifiOff; break;
    case D_DITHER: {
      static const char* const en[4] = {"Dither: Off", "Dither: Low", "Dither: Mid", "Dither: High"};
      static const char* const fr[4] = {"Dither: Non", "Dither: Bas", "Dither: Moy", "Dither: Haut"};
      strlcpy(label, (gLang == 1 ? fr : en)[s.cfg.radioDither > 3 ? 2 : s.cfg.radioDither], n); break;
    }
    default: break;
  }
}
void drawButton(const AppState& s, const Button& b, uint16_t accent) {
  if (b.action == D_PLAY && s.radio != RADIO_OFF) accent = PINK;      // radio on: the page turns green, only the STOP outline stays red
  char label[24]; bool primary, enabled;
  describe(s, b.action, label, sizeof label, primary, enabled);
  const uint16_t edge = enabled ? accent : LINE;
  if (primary && enabled) screen.fillRoundRect(b.r.x, b.r.y, b.r.w, b.r.h, 6, accent);
  else { screen.drawRoundRect(b.r.x, b.r.y, b.r.w, b.r.h, 6, edge); screen.drawRoundRect(b.r.x + 1, b.r.y + 1, b.r.w - 2, b.r.h - 2, 5, edge); }
  const uint16_t ink = !enabled ? DIM : primary ? BG : WHITE;
  const int cx = b.r.x + b.r.w / 2, cy = b.r.y + b.r.h / 2;
  if (b.action == A_HP || b.action == A_MP) screen.fillTriangle(cx, cy - 7, cx - 10, cy + 6, cx + 10, cy + 6, enabled ? accent : DIM);
  else if (b.action == A_HM || b.action == A_MM) screen.fillTriangle(cx, cy + 7, cx - 10, cy - 6, cx + 10, cy - 6, enabled ? accent : DIM);
  else if (label[0] == '+' && !label[1]) {     // plus / minus drawn as shapes: much easier to read than a 15 px glyph
    screen.fillRect(cx - 10, cy - 2, 21, 5, ink); screen.fillRect(cx - 2, cy - 10, 5, 21, ink);
  } else if (label[0] == '-' && !label[1]) {
    screen.fillRect(cx - 10, cy - 2, 21, 5, ink);
  } else {
    textBackground = primary && enabled ? accent : buttonBack;
    text(label, b.r.x + 2, b.r.y + (b.r.h - 19) / 2, b.r.w - 4, ink, 2, 1);
    textBackground = BG;
  }
}
}  // namespace

bool clockIsValid() { return localNow().valid; }

// The button list is the single source of truth for drawing and for touch handling.
int pageButtons(const AppState& s, Button* out, int capacity) {
  int n = 0;
  auto addButton = [&](int x, int y, int w, int h, Action a, bool repeat) { if (n < capacity) out[n++] = {{int16_t(x), int16_t(y), int16_t(w), int16_t(h)}, a, repeat}; };
  auto add = [&](int x, int y, int w, int h, Action a) { addButton(x, y, w, h, a, false); };
  auto addRepeat = [&](int x, int y, int w, int h, Action a) { addButton(x, y, w, h, a, true); };
  if (s.alarm.ringing) { add(10, 128, 300, 40, R_SNOOZE); add(10, 176, 300, 40, R_STOP); return n; }
  switch (s.page) {
    case PG_TIMER:
      addRepeat(10, 166, 52, 50, T_MINUS); addRepeat(66, 166, 52, 50, T_PLUS);
      add(122, 166, 108, 50, T_START); add(234, 166, 76, 50, T_RESET);
      break;
    case PG_CHRONO: add(10, 166, 190, 50, S_START); add(206, 166, 104, 50, S_RESET); break;
    case PG_COUNT:
      if (s.counterConfirm) { add(10, 120, 148, 60, C_YES); add(162, 120, 148, 60, C_NO); }
      else { add(10, 106, 148, 80, C_MINUS); add(162, 106, 148, 80, C_PLUS); add(10, 192, 148, 28, C_LOCK); add(162, 192, 148, 28, C_RESET); }
      break;
    case PG_ALARM:
      addRepeat(64, 38, 84, 28, A_HP); addRepeat(172, 38, 84, 28, A_MP);
      addRepeat(64, 124, 84, 28, A_HM); addRepeat(172, 124, 84, 28, A_MM);
      add(10, 156, 40, 26, A_TUNE_PREV); add(270, 156, 40, 26, A_TUNE_NEXT);
      add(10, 190, 86, 28, A_TOGGLE); add(102, 190, 86, 28, A_TEST);
      addRepeat(194, 190, 34, 28, A_VOL_MINUS); addRepeat(278, 190, 32, 28, A_VOL_PLUS);
      break;
    case PG_SETUP:
      addRepeat(130, 28, 44, 28, P_BRIGHT_MINUS); addRepeat(240, 28, 44, 28, P_BRIGHT_PLUS);
      addRepeat(130, 60, 44, 28, P_VOL_MINUS); addRepeat(240, 60, 44, 28, P_VOL_PLUS);
      add(130, 92, 154, 28, P_NIGHT); add(130, 124, 154, 28, P_LANG); add(10, 156, 300, 28, P_CALIBRATE); add(240, 186, 70, 24, P_WIFI);
      break;
    case PG_CLIMATE:
      addRepeat(150, 120, 44, 28, K_TEMP_MINUS); addRepeat(260, 120, 44, 28, K_TEMP_PLUS);
      addRepeat(150, 152, 44, 28, K_HUM_MINUS); addRepeat(260, 152, 44, 28, K_HUM_PLUS);
      add(10, 186, 300, 30, K_ALERT);
      break;
    case PG_SLEEP:
      addRepeat(130, 28, 44, 28, V_SCR_MINUS); addRepeat(240, 28, 44, 28, V_SCR_PLUS);
      addRepeat(130, 62, 44, 28, V_TMR_MINUS); addRepeat(240, 62, 44, 28, V_TMR_PLUS);
      add(10, 110, 300, 46, V_GO);
      add(10, 168, 300, 30, V_SCREEN_OFF);
      break;
    case PG_RADIO:
      add(10, 30, 300, 48, D_PLAY);                                                           // PLAY / STOP first, at the top
      if (RADIO_COUNT > 1) { add(10, 88, 44, 34, D_PREV); add(266, 88, 44, 34, D_NEXT); }   // station arrows only with several stations
      addRepeat(130, 150, 44, 28, D_VOL_MINUS); addRepeat(240, 150, 44, 28, D_VOL_PLUS);
      add(10, 186, 146, 28, D_NOISE); add(164, 186, 146, 28, D_DITHER);
      break;
    default: break;
  }
  return n;
}

namespace {
// ---------------- common furniture ----------------
// Hours and minutes, always at the top right of every page.
void headerClock() {
  const LocalTime t = localNow(); char b[8];
  if (t.valid) snprintf(b, sizeof b, "%02d:%02d", t.h, t.m); else strlcpy(b, "--:--", sizeof b);
  text(b, 254, 4, 56, t.valid ? WHITE : MUTED, 2, 2);
}
void header(const AppState& s, const char* title, uint16_t accent) {
  text(title, 12, 4, 170, WHITE, 2);
  headerClock();
  int x = 244;
  const struct { bool on; const char* letter; uint16_t color; } badges[] = {
    {s.watch.running, "C", PURPLE}, {s.pomo.running, "M", GREEN}, {s.alarm.armed, "R", YELLOW}, {s.radio != RADIO_OFF, "F", strcmp(title, "Radio") == 0 ? GREEN : PINK}, {s.sleep.active, "Z", PURPLE}};
  for (const auto& b : badges) if (b.on) { text(b.letter, x - 2, 4, 20, b.color, 2, 1); x -= 22; }
  screen.drawFastHLine(10, 23, 300, accent);
}
void footer(int page) {
  static const char* const names[PAGE_TOTAL] = {"Clock", "Timer", "Chrono", "Count", "Alarm", "Notes", "Setup", "Temp", "Weather", "Radio", "Sleep"};
  static const char* const namesFr[PAGE_TOTAL] = {"Heure", "Minut.", "Chrono", "Compt.", "Alarme", "Notes", "Param.", "Temp.", "Meteo", "Radio", "Veille"};   // built-in 6x8 font: ASCII only
  divider(223);
  const int row = pageRow(page);
  for (int slot = 0; slot < FOOTER_SLOTS; ++slot) {
    const int x0 = slot * 320 / FOOTER_SLOTS, x1 = (slot + 1) * 320 / FOOTER_SLOTS, w = x1 - x0;
    const int target = footerTarget(row, (x0 + x1) / 2);
    if (target == FOOT_NONE) continue;
    if (target == FOOT_DOWN || target == FOOT_UP) {            // arrow: down opens the second row, up goes back
      const int cx = x0 + w / 2, cy = 232;
      if (target == FOOT_DOWN) screen.fillTriangle(cx - 8, cy - 4, cx + 8, cy - 4, cx, cy + 6, CYAN);
      else screen.fillTriangle(cx - 8, cy + 5, cx + 8, cy + 5, cx, cy - 5, CYAN);
      continue;
    }
    if (target == page) screen.drawFastHLine(x0 + 4, 223, w - 8, CYAN);
    text(gLang == 1 ? namesFr[target] : names[target], x0 + 1, 230, w - 2, target == page ? CYAN : MUTED, 1, 1);
  }
}
void drawButtons(const AppState& s, uint16_t accent) {
  Button list[12]; const int n = pageButtons(s, list, 12);
  for (int i = 0; i < n; ++i) drawButton(s, list[i], accent);
}
void bigCentered(const char* value, int y, uint16_t color) { text(value, 0, y, 320, color, 3, 1); }

// ---------------- pages ----------------
void clockPage(const AppState& s) {
  const LocalTime t = localNow();
  header(s, "Desk Watch", CYAN);
  char b[32];
  if (t.valid) snprintf(b, sizeof b, "%02d:%02d:%02d", t.h, t.m, t.s); else strlcpy(b, "--:--:--", sizeof b);
  bigCentered(b, 58, YELLOW);
  if (t.valid) formatDate(b, sizeof b, t.tm);
  else strlcpy(b, wifiUp() ? L("Syncing the clock...", "Synchronisation de l'heure...") : L("Connecting to Wi-Fi...", "Connexion au Wi-Fi..."), sizeof b);
  text(b, 10, 118, 300, WHITE, 2, 1);
  text(s.cfg.tzLabel, 10, 140, 300, MUTED, 2, 1);
  if (s.alarm.armed) snprintf(b, sizeof b, "%s %02u:%02u", L("Alarm", "Alarme"), s.alarm.hour, s.alarm.minute); else strlcpy(b, L("Alarm off", "Alarme désactivée"), sizeof b);
  text(b, 10, 164, 300, s.alarm.armed ? YELLOW : MUTED, 2, 1);
  text(wifiUp() ? L("Wi-Fi connected", "Wi-Fi connecté") : L("Wi-Fi offline", "Wi-Fi hors ligne"), 10, 190, 300, wifiUp() ? GREEN : ORANGE, 2, 1);
}
void timerPage(const AppState& s) {
  const uint16_t accent = accentFor(s, PG_TIMER);
  header(s, L("Timer", "Minuteur"), accent);
  const char* phase = s.pomo.phase == POMO_WORK ? L("WORK", "TRAVAIL") : s.pomo.phase == POMO_SHORT ? L("SHORT BREAK", "PAUSE COURTE") : L("LONG BREAK", "PAUSE LONGUE");
  text(phase, 10, 32, 300, accent, 2, 1);
  char b[32];
  formatTimer(b, sizeof b, s.pomo.remainingMs);
  bigCentered(b, 62, WHITE);
  if (s.pomo.idle()) { char d[16]; formatWorkDuration(d, sizeof d, s.pomo.workMin); snprintf(b, sizeof b, "%s %s", L("Work time", "Durée de travail"), d); }
  else snprintf(b, sizeof b, "%s %u", L("Sessions done:", "Sessions terminées :"), s.pomo.sessions);
  text(b, 10, 126, 300, MUTED, 2, 1);
  drawButtons(s, accent);
}
void chronoPage(const AppState& s, uint32_t now) {
  header(s, L("Stopwatch", "Chronomètre"), PURPLE);
  text(s.watch.running ? L("RUNNING", "EN COURS") : s.watch.idle() ? L("READY", "PRÊT") : L("STOPPED", "ARRÊTÉ"), 10, 32, 300, PURPLE, 2, 1);
  char b[32]; formatStopwatch(b, sizeof b, s.watch.elapsed(now));
  bigCentered(b, 70, WHITE);
  drawButtons(s, PURPLE);
}
void counterPage(const AppState& s) {
  header(s, L("Counter", "Compteur"), TEAL);
  char b[32];
  if (s.counterConfirm) {
    text(L("Reset the counter to zero?", "Remettre le compteur à zéro ?"), 10, 60, 300, WHITE, 2, 1);
    snprintf(b, sizeof b, "%s %u", L("Current value:", "Valeur actuelle :"), s.counter.value);
    text(b, 10, 86, 300, MUTED, 2, 1);
    drawButtons(s, TEAL); return;
  }
  text(s.counter.active ? L("ACTIVE", "ACTIF") : L("LOCKED", "VERROUILLÉ"), 10, 30, 300, s.counter.active ? TEAL : ORANGE, 2, 1);
  snprintf(b, sizeof b, "%u", s.counter.value);
  bigCentered(b, 56, WHITE);
  drawButtons(s, TEAL);
}
void alarmPage(const AppState& s) {
  header(s, L("Alarm", "Alarme"), YELLOW);
  char b[32]; snprintf(b, sizeof b, "%02u:%02u", s.alarm.hour, s.alarm.minute);
  bigCentered(b, 70, s.alarm.armed ? YELLOW : MUTED);
  text(alarmSoundName(s.cfg.tune), 54, 160, 212, WHITE, 2, 1);
  snprintf(b, sizeof b, "Vol %u", alarmVolume(s));
  text(b, 226, 196, 52, MUTED, 2, 1);
  drawButtons(s, YELLOW);
}
void radioPage(const AppState& s) {
  const uint16_t accent = s.radio != RADIO_OFF ? GREEN : PINK;      // red while stopped, green while the radio is on
  header(s, "Radio", accent);
  text(RADIOS[s.cfg.radioStation < RADIO_COUNT ? s.cfg.radioStation : 0].name, 58, 96, 204, WHITE, 2, 1);
  const char* status; uint16_t color = MUTED;
  if (s.radio == RADIO_PLAYING) { status = L("On air", "En direct"); color = GREEN; }
  else if (s.radio == RADIO_CONNECTING) { status = L("Connecting...", "Connexion..."); color = YELLOW; }
  else if (s.radio == RADIO_ERROR) { status = wifiUp() ? L("No signal - retrying", "Pas de signal - nouvel essai") : L("Wi-Fi needed", "Wi-Fi requis"); color = ORANGE; }
  else status = wifiUp() ? L("Press PLAY to listen", "Appuyez sur ÉCOUTER") : L("Wi-Fi needed for the radio", "Wi-Fi requis pour la radio");
  text(status, 10, 128, 300, color, 2, 1);
  text("Volume", 12, 154, 112, WHITE, 2);
  char b[16]; snprintf(b, sizeof b, "%u/%d", s.cfg.radioVolume, VOLUME_MAX); text(b, 176, 154, 62, YELLOW, 2, 1);
  drawButtons(s, accent);
}
void sleepPage(const AppState& s) {
  header(s, L("Sleep", "Veille"), PURPLE);
  char b[24];
  text(L("Screen", "Ecran"), 12, 32, 112, WHITE, 2);
  const uint8_t scr = SCREEN_SLEEP_MIN[s.cfg.screenSleep < SCREEN_SLEEP_COUNT ? s.cfg.screenSleep : 0];
  if (scr == 0) strlcpy(b, L("Never", "Jamais"), sizeof b); else snprintf(b, sizeof b, "%u min", scr);
  text(b, 176, 32, 62, YELLOW, 2, 1);
  text(L("Timer", "Minuterie"), 12, 66, 112, WHITE, 2);
  snprintf(b, sizeof b, "%u min", SLEEP_TIMER_MIN[s.cfg.sleepTimer < SLEEP_TIMER_COUNT ? s.cfg.sleepTimer : 2]); text(b, 176, 66, 62, s.sleep.active ? MUTED : YELLOW, 2, 1);
  text(s.sleep.active ? L("Radio stops by itself", "La radio s'arrete seule") : L("SLEEP: radio + timer", "DORMIR: radio + minuterie"), 10, 92, 300, MUTED, 1, 1);
  drawButtons(s, PURPLE);
  text(L("A touch wakes the screen. Alarms still ring.", "Un toucher rallume l'ecran. Les alarmes sonnent."), 10, 206, 300, MUTED, 1, 1);
}
void setupPage(const AppState& s) {
  header(s, L("Settings", "Réglages"), WHITE);
  char b[40];
  text(L("Brightness", "Luminosité"), 12, 32, 112, WHITE, 2);
  snprintf(b, sizeof b, "%u%%", BRIGHT_LEVELS[s.cfg.brightIndex]); text(b, 176, 32, 62, YELLOW, 2, 1);
  text("Volume", 12, 64, 112, WHITE, 2);
  snprintf(b, sizeof b, "%u/%d", s.cfg.volume, VOLUME_MAX); text(b, 176, 64, 62, YELLOW, 2, 1);
  text(L("Night mode", "Mode nuit"), 12, 96, 112, WHITE, 2);
  text(L("Language", "Langue"), 12, 128, 112, WHITE, 2);
  drawButtons(s, CYAN);
  if (wifiUp()) { snprintf(b, sizeof b, "Wi-Fi %s", WiFi.localIP().toString().c_str()); text(b, 10, 189, 228, GREEN, 2, 1); }
  else text(s.wifiOff ? L("Wi-Fi off", "Wi-Fi coupé") : L("Wi-Fi offline", "Wi-Fi hors ligne"), 10, 189, 228, ORANGE, 2, 1);
  text("Desk Watch CYD V1  -  PR BELAMINE MAKERLAB", 10, 212, 300, MUTED, 1, 1);
}
void climatePage(const AppState& s) {
  const ClimateReading& c = s.climate;
  header(s, L("Temp / Humidity", "Temp. / Humidité"), s.climateGuard.active() ? RED : GREEN);
  char b[32];
  // The big fonts only have digits, so the units go on a small line underneath.
  if (c.valid) snprintf(b, sizeof b, "%.1f", c.tempC); else strlcpy(b, "--.-", sizeof b);
  text(b, 0, 50, 160, !c.valid ? MUTED : s.climateGuard.tempHigh ? RED : WHITE, 4, 1);
  if (c.valid) snprintf(b, sizeof b, c.humPct >= 99.95f ? "%.0f" : "%.1f", c.humPct); else strlcpy(b, "--.-", sizeof b);
  text(b, 160, 50, 160, !c.valid ? MUTED : s.climateGuard.humHigh ? RED : WHITE, 4, 1);
  if (c.valid) {
    text("°C", 0, 96, 160, MUTED, 2, 1);
    text(L("% humidity", "% humidité"), 160, 96, 160, MUTED, 2, 1);
  } else if (c.error == 0) text(L("Reading the sensor...", "Lecture du capteur..."), 10, 96, 300, MUTED, 2, 1);
  else text(c.error == 1 ? L("No sensor: check wiring (IO27)", "Pas de capteur : câblage (IO27)") : L("Sensor read error", "Erreur de lecture du capteur"), 10, 96, 300, ORANGE, 2, 1);
  char v[16];
  text(L("Max temp.", "Temp. max"), 12, 124, 134, WHITE, 2);
  snprintf(v, sizeof v, "%u °C", s.cfg.tempMax); text(v, 194, 124, 66, YELLOW, 2, 1);
  text(L("Max humidity", "Humidité max"), 12, 156, 134, WHITE, 2);
  snprintf(v, sizeof v, "%u %%", s.cfg.humMax); text(v, 194, 156, 66, YELLOW, 2, 1);
  drawButtons(s, s.climateGuard.active() ? RED : GREEN);
}
// ---------------- weather ----------------
void cloudShape(int cx, int cy, int r, uint16_t color) {        // r = size of the main puff
  screen.fillCircle(cx - r, cy + r / 3, r * 2 / 3, color); screen.fillCircle(cx, cy - r / 4, r, color);
  screen.fillCircle(cx + r, cy + r / 3, r * 3 / 4, color); screen.fillRect(cx - r, cy + r / 3, r * 2, r * 3 / 4, color);
}
void weatherIcon(WeatherKind kind, bool day, int cx, int cy, int r) {      // simple icon drawn with shapes, about 3r wide
  const uint16_t sun = day ? YELLOW : MUTED, cloud = 0xC638;
  switch (kind) {
    case WX_CLEAR:
      screen.fillCircle(cx, cy, r, sun);
      for (int i = 0; i < 8; ++i) {                              // 8 rays
        static const int8_t dx[8] = {1, 1, 0, -1, -1, -1, 0, 1}, dy[8] = {0, 1, 1, 1, 0, -1, -1, -1};
        screen.drawLine(cx + dx[i] * (r + 3), cy + dy[i] * (r + 3), cx + dx[i] * (r + 6), cy + dy[i] * (r + 6), sun);
      }
      break;
    case WX_PARTLY: screen.fillCircle(cx + r / 2, cy - r / 2, r * 2 / 3, sun); cloudShape(cx - r / 4, cy + r / 4, r * 2 / 3, cloud); break;
    case WX_CLOUDY: cloudShape(cx, cy, r * 3 / 4, cloud); break;
    case WX_FOG: for (int i = -1; i <= 1; ++i) screen.fillRect(cx - r, cy + i * (r / 2) - 1, r * 2, 3, MUTED); break;
    case WX_DRIZZLE: case WX_RAIN: case WX_STORM: case WX_SNOW: {
      cloudShape(cx, cy - r / 3, r * 2 / 3, kind == WX_STORM ? MUTED : cloud);
      for (int i = -1; i <= 1; ++i) {
        const int x = cx + i * (r * 2 / 3), y = cy + r / 2;
        if (kind == WX_SNOW) screen.fillCircle(x, y + 4, 2, WHITE);
        else if (kind == WX_STORM && i == 0) screen.fillTriangle(x + 3, y - 2, x - 4, y + 5, x + 1, y + 5, YELLOW), screen.fillTriangle(x - 1, y + 4, x + 4, y + 4, x - 3, y + 12, YELLOW);
        else screen.drawLine(x + 2, y, x - 2, y + (kind == WX_DRIZZLE ? 6 : 10), 0x5D7F);
      }
      break;
    }
  }
}
const char* weatherText(const WeatherData& w) {
  switch (weatherKind(w.code)) {
    case WX_CLEAR: return w.day ? L("Clear sky", "Ciel dégagé") : L("Clear night", "Nuit claire");
    case WX_PARTLY: return L("Partly cloudy", "Éclaircies");
    case WX_CLOUDY: return L("Cloudy", "Nuageux");
    case WX_FOG: return L("Fog", "Brouillard");
    case WX_DRIZZLE: return L("Drizzle", "Bruine");
    case WX_RAIN: return L("Rain", "Pluie");
    case WX_SNOW: return L("Snow", "Neige");
    default: return L("Thunderstorm", "Orage");
  }
}
void weatherPage(const AppState& s) {
  const WeatherData& w = s.weather;
  header(s, L("Weather", "Météo"), CYAN);
  text(s.cfg.wPlace, 100, 4, 100, MUTED, 2, 1);
  if (!w.valid) {
    text(wifiUp() ? L("Loading the weather...", "Chargement de la météo...") : L("Wi-Fi needed for the weather", "Wi-Fi requis pour la météo"), 10, 100, 300, MUTED, 2, 1);
    return;
  }
  char b[40];
  snprintf(b, sizeof b, "%.0f", w.tempC); text(b, 0, 50, 150, WHITE, 4, 1);
  text("°C", 150, 56, 30, MUTED, 2);
  weatherIcon(weatherKind(w.code), w.day, 250, 68, 22);
  text(weatherText(w), 10, 96, 300, CYAN, 2, 1);
  snprintf(b, sizeof b, "%s %.0f °C   %u %%   %.0f km/h", L("Feels", "Ress."), w.feelsC, w.humidity, w.windKmh);
  text(b, 10, 118, 300, WHITE, 2, 1);
  static const char* const dayEn[7] = {"Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"};
  static const char* const dayFr[7] = {"Dim", "Lun", "Mar", "Mer", "Jeu", "Ven", "Sam"};
  divider(142);
  for (int i = 1; i < w.days && i <= 3; ++i) {                 // tomorrow and the next two days
    const int x0 = (i - 1) * 100 + 10, wd = weekdayOf(w.dayDate[i]);
    if (wd >= 0) text(gLang == 1 ? dayFr[wd] : dayEn[wd], x0, 148, 90, MUTED, 2, 1);
    weatherIcon(weatherKind(w.dayCode[i]), true, x0 + 45, 184, 12);
    snprintf(b, sizeof b, "%.0f / %.0f", w.dayMax[i], w.dayMin[i]); text(b, x0, 200, 90, WHITE, 2, 1);
  }
}
void notesPage(const AppState& s) {
  const TaskStore& list = s.tasks; const int scroll = clampTaskScroll(s.taskScroll, list.count());
  text(L("To do", "À faire"), 12, 3, 110, WHITE, 2);
  const bool scrolls = list.count() > TASK_VISIBLE;
  char summary[24] = "";
  if (list.count()) snprintf(summary, sizeof summary, L("%d/%d done", "%d/%d faites"), list.doneCount(), list.count());
  headerClock();
  text(summary, 60, 3, 98, MUTED, 2, 2);
  if (scrolls) {
    char pg[24]; snprintf(pg, sizeof pg, "%d/%d", taskPageOf(scroll) + 1, taskPageCount(list.count()));
    screen.fillTriangle(176, 7, 168, 17, 184, 17, scroll > 0 ? YELLOW : LINE);
    text(pg, 190, 3, 28, WHITE, 2, 1);
    screen.fillTriangle(232, 17, 224, 7, 240, 7, scroll + TASK_VISIBLE < list.count() ? YELLOW : LINE);
  }
  divider(23);
  screen.fillRoundRect(6, 27, 308, 192, 6, NOTE);
  textBackground = NOTE;
  for (int row = 0; row < TASK_VISIBLE && scroll + row < list.count(); ++row) {
    const TodoItem& item = list.at(scroll + row); const int y = TASK_TOP + row * TASK_ROW_H;
    screen.drawRoundRect(14, y + 5, 20, 20, 3, INK); screen.drawRoundRect(15, y + 6, 18, 18, 2, INK);
    if (item.done) {
      screen.fillRoundRect(14, y + 5, 20, 20, 3, TICK);
      for (int d = 0; d < 2; ++d) { screen.drawLine(19, y + 15 + d, 23, y + 19 + d, WHITE); screen.drawLine(23, y + 19 + d, 30, y + 10 + d, WHITE); }
    }
    const int width = text(item.text, 44, y + 5, 262, item.done ? DONE : INK, 2);
    if (item.done) screen.drawFastHLine(44, y + 15, width, DONE);
    screen.drawFastHLine(14, y + 30, 292, RULE);
  }
  if (!list.count()) {
    text(L("No tasks yet", "Aucune tâche"), 6, 78, 308, DONE, 2, 1);
    text(L("Add some from your phone", "Ajoutez-en depuis un téléphone"), 6, 100, 308, INK, 2, 1);
  }
  if (list.count() <= 4) {
    char b[48];
    if (wifiUp()) {
      snprintf(b, sizeof b, "http://%s.local", TASK_HOSTNAME); text(b, 14, 168, 292, INK, 2, 1);
      snprintf(b, sizeof b, "%s http://%s", L("or", "ou"), WiFi.localIP().toString().c_str()); text(b, 14, 190, 292, DONE, 2, 1);
    } else text(L("Wi-Fi offline", "Wi-Fi hors ligne"), 14, 168, 292, INK, 2, 1);
  }
  textBackground = BG;
}
void ringingScreen(const AppState& s, uint32_t now) {
  screen.fillSprite(((now / 500) & 1) ? DARKRED : BG);
  textBackground = ((now / 500) & 1) ? DARKRED : BG;
  text(L("ALARM", "ALARME"), 10, 3, 300, WHITE, 2, 1);
  // name of the sound that is ringing (a melody, or the web radio chosen on the Alarm page)
  const bool radioChosen = alarmSoundIsRadio(s.cfg.tune);
  const int shown = (radioChosen && s.ringMode == RING_RADIO) ? s.cfg.tune : (radioChosen ? 0 : s.cfg.tune);
  char b[48];
  snprintf(b, sizeof b, "%s %s", (radioChosen && s.ringMode == RING_RADIO) ? "Radio :" : L("Ringtone:", "Sonnerie :"), alarmSoundName(shown));
  text(b, 10, 25, 300, CYAN, 2, 1);
  snprintf(b, sizeof b, "%02u:%02u", s.alarm.hour, s.alarm.minute);
  bigCentered(b, 50, YELLOW);
  // what the radio is doing (only when the radio was chosen)
  if (radioChosen) {
    const char* status = nullptr; uint16_t color = MUTED;
    if (s.ringMode == RING_RADIO) {
      if (s.radio == RADIO_PLAYING) { status = L("On air", "En direct"); color = GREEN; }
      else if (s.radio == RADIO_ERROR) { status = L("No signal - retrying", "Pas de signal - nouvel essai"); color = ORANGE; }
      else { status = L("Connecting...", "Connexion..."); color = YELLOW; }
    } else if (s.ringMode == RING_RADIO_FAILED) { status = L("Radio silent: melody instead", "Radio muette : mélodie"); color = ORANGE; }
    else if (s.ringMode == RING_WAIT_WIFI) { status = L("No Wi-Fi: radio starts when it is back", "Pas de Wi-Fi : radio dès son retour"); color = ORANGE; }
    if (status) text(status, 10, 106, 300, color, 2, 1);
  }
  textBackground = BG;
  buttonBack = ((now / 500) & 1) ? DARKRED : BG;
  drawButtons(s, WHITE);
  buttonBack = BG;
}
}  // namespace

// ---------------- public ----------------
void setBacklightPercent(uint8_t percent) {
  if (percent < 5) percent = 5;
  if (percent > 100) percent = 100;
  constexpr uint32_t freq = 5000, bits = 10;
  const uint32_t maxDuty = (1UL << bits) - 1;
  const uint32_t duty = (maxDuty * percent * percent + 5000) / 10000;   // perceived brightness ~ linear
#if defined(ESP_ARDUINO_VERSION_MAJOR) && ESP_ARDUINO_VERSION_MAJOR >= 3
  if (!backlightAttached) { ledcAttachChannel(TFT_BL, freq, bits, 0); backlightAttached = true; }
  ledcWrite(TFT_BL, duty);
#else
  if (!backlightAttached) { ledcSetup(0, freq, bits); ledcAttachPin(TFT_BL, 0); backlightAttached = true; }
  ledcWrite(0, duty);
#endif
}
uint8_t effectiveBrightness(const AppState& s, int hour, bool clockValid) {
  const uint8_t chosen = BRIGHT_LEVELS[s.cfg.brightIndex < BRIGHT_LEVEL_COUNT ? s.cfg.brightIndex : BRIGHT_LEVEL_COUNT - 1];
  if (s.cfg.night && clockValid && isNight(hour) && chosen > 8) return 8;
  return chosen;
}
bool displayBegin() {
  tft.begin(); tft.invertDisplay(DISPLAY_INVERT_COLORS); tft.setRotation(DISPLAY_ROTATION); tft.fillScreen(BG);
  screen.setColorDepth(8);
  return screen.createSprite(320, 240) != nullptr;
}
void setUiLanguage(uint8_t lang) { gLang = lang == 1 ? 1 : 0; }
const char* tr(const char* en, const char* fr) { return L(en, fr); }

void renderApp(const AppState& s, uint32_t now) {
  gLang = s.cfg.lang == 1 ? 1 : 0;
  textBackground = BG;
  if (s.alarm.ringing) { ringingScreen(s, now); screen.pushSprite(0, 0); return; }
  screen.fillSprite(BG);
  switch (s.page) {
    case PG_CLOCK: clockPage(s); break;
    case PG_TIMER: timerPage(s); break;
    case PG_CHRONO: chronoPage(s, now); break;
    case PG_COUNT: counterPage(s); break;
    case PG_ALARM: alarmPage(s); break;
    case PG_NOTES: notesPage(s); break;
    case PG_CLIMATE: climatePage(s); break;
    case PG_WEATHER: weatherPage(s); break;
    case PG_RADIO: radioPage(s); break;
    case PG_SLEEP: sleepPage(s); break;
    default: setupPage(s); break;
  }
  footer(s.page);
  if (now < s.bannerUntil) {                     // transient message across every page (e.g. end of a work session)
    screen.fillRect(0, 0, 320, 24, ORANGE);
    textBackground = ORANGE; text(s.banner, 6, 3, 308, BG, 2, 1); textBackground = BG;
  }
  screen.pushSprite(0, 0);
}
void renderCalibration(int step, bool failed) {
  screen.fillSprite(BG); textBackground = BG;
  text(L("Touch calibration", "Calibrage du tactile"), 12, 4, 296, WHITE, 2, 1); divider(23);
  text(failed ? L("Try again: press each target steadily", "Réessayez : appuyez fermement") : L("Press and hold each target", "Maintenez appuyé sur chaque cible"), 12, 86, 296, WHITE, 2, 1);
  char b[40]; snprintf(b, sizeof b, L("Point %d of 3", "Point %d sur 3"), step + 1); text(b, 12, 110, 296, CYAN, 2, 1);
  const int xs[] = {20, 300, 160}, ys[] = {20, 20, 210};
  screen.drawCircle(xs[step], ys[step], 10, YELLOW);
  screen.drawFastHLine(xs[step] - 14, ys[step], 29, YELLOW); screen.drawFastVLine(xs[step], ys[step] - 14, 29, YELLOW);
  screen.pushSprite(0, 0);
}
void renderSplash(int percent) {
  screen.fillSprite(BG); textBackground = BG;
  screen.setTextFont(1); screen.setTextSize(4); selectedFont = -1;
  screen.setTextColor(YELLOW, BG); screen.setTextDatum(MC_DATUM); screen.drawString("DESK WATCH", 160, 70);
  screen.setTextSize(1);
  screen.setTextColor(WHITE, BG); screen.drawString("PR BELAMINE MAKERLAB", 160, 115, 2);
  char edition[32]; snprintf(edition, sizeof edition, "CYD EDITION %s", FIRMWARE_EDITION);
  screen.setTextColor(CYAN, BG); screen.drawString(edition, 160, 140, 2);
  screen.drawRoundRect(60, 175, 200, 14, 4, MUTED);
  screen.fillRoundRect(62, 177, (196 * (percent < 0 ? 0 : percent > 100 ? 100 : percent)) / 100, 10, 3, GREEN);
  screen.pushSprite(0, 0);
}
