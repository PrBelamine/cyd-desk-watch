// Parsing of the Open-Meteo JSON answer without a JSON library (the answer is small and its layout is fixed).
#include "Weather.h"
#include <stdlib.h>
#include <string.h>

namespace {
// Pointer just after `key` (for example "\"temperature_2m\":") searching from 'from', or nullptr.
const char* after(const char* from, const char* key) {
  if (!from) return nullptr;
  const char* p = strstr(from, key);
  return p ? p + strlen(key) : nullptr;
}
bool number(const char* from, const char* key, float& out) {
  const char* p = after(from, key);
  if (!p) return false;
  char* end; const double v = strtod(p, &end);
  if (end == p) return false;
  out = float(v); return true;
}
int numberArray(const char* from, const char* key, float* out, int cap) {
  const char* p = after(from, key);
  if (!p || *p != '[') return 0;
  ++p; int n = 0;
  while (n < cap) {
    char* end; const double v = strtod(p, &end);
    if (end == p) break;
    out[n++] = float(v); p = end;
    if (*p == ',') ++p; else break;
  }
  return n;
}
}  // namespace

bool parseWeatherJson(const char* json, WeatherData& out) {
  const char* cur = after(json, "\"current\":{");        // not "current_units"
  if (!cur) return false;
  WeatherData d;
  float t, f, h, w, c, day = 1;
  if (!number(cur, "\"temperature_2m\":", t) || !number(cur, "\"relative_humidity_2m\":", h) || !number(cur, "\"weather_code\":", c)) return false;
  if (!number(cur, "\"apparent_temperature\":", f)) f = t;
  if (!number(cur, "\"wind_speed_10m\":", w)) w = 0;
  number(cur, "\"is_day\":", day);
  d.tempC = t; d.feelsC = f; d.windKmh = w; d.humidity = uint8_t(h < 0 ? 0 : h > 100 ? 100 : h + 0.5f);
  d.code = uint8_t(c < 0 ? 0 : c > 255 ? 255 : c); d.day = day != 0;
  const char* daily = after(json, "\"daily\":{");
  if (daily) {
    float codes[4], mx[4], mn[4];
    const int nc = numberArray(daily, "\"weather_code\":", codes, 4), nx = numberArray(daily, "\"temperature_2m_max\":", mx, 4), nn = numberArray(daily, "\"temperature_2m_min\":", mn, 4);
    int n = nc < nx ? nc : nx; if (nn < n) n = nn;
    const char* dates = after(daily, "\"time\":[");
    for (int i = 0; i < n; ++i) {
      d.dayCode[i] = uint8_t(codes[i]); d.dayMax[i] = mx[i]; d.dayMin[i] = mn[i];
      d.dayDate[i][0] = '\0';
      if (dates) {
        const char* q = strchr(dates, '"');
        if (q && strlen(q) > 11 && q[11] == '"') { memcpy(d.dayDate[i], q + 1, 10); d.dayDate[i][10] = '\0'; dates = q + 12; if (*dates == ',') ++dates; }
        else dates = nullptr;
      }
    }
    d.days = n;
  }
  d.valid = true;
  out = d; return true;
}

WeatherKind weatherKind(int c) {
  if (c == 0) return WX_CLEAR;
  if (c <= 2) return WX_PARTLY;
  if (c == 3) return WX_CLOUDY;
  if (c == 45 || c == 48) return WX_FOG;
  if (c >= 51 && c <= 57) return WX_DRIZZLE;
  if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return WX_RAIN;
  if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WX_SNOW;
  if (c >= 95) return WX_STORM;
  return WX_CLOUDY;
}

int weekdayOf(const char* s) {
  if (!s || strlen(s) != 10 || s[4] != '-' || s[7] != '-') return -1;
  int y = atoi(s), m = atoi(s + 5), d = atoi(s + 8);
  if (y < 2000 || m < 1 || m > 12 || d < 1 || d > 31) return -1;
  static const int t[12] = {0, 3, 2, 5, 0, 3, 5, 1, 4, 6, 2, 4};
  if (m < 3) --y;
  return (y + y / 4 - y / 100 + y / 400 + t[m - 1] + d) % 7;
}
