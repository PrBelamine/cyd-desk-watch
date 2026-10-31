#pragma once
// Weather page: current conditions and 3-day forecast from Open-Meteo (free, no account, no API key).
#include <stdint.h>

struct WeatherData {
  bool valid = false;
  float tempC = 0, feelsC = 0, windKmh = 0;
  uint8_t humidity = 0, code = 0;              // code = WMO weather code
  bool day = true;
  int days = 0;                                // forecast days parsed (max 4, day 0 = today)
  uint8_t dayCode[4] = {};
  float dayMax[4] = {}, dayMin[4] = {};
  char dayDate[4][11] = {};                    // "YYYY-MM-DD"
  uint32_t updatedMs = 0;
};
enum WeatherKind : uint8_t { WX_CLEAR, WX_PARTLY, WX_CLOUDY, WX_FOG, WX_DRIZZLE, WX_RAIN, WX_SNOW, WX_STORM };

// Pure functions (host-tested in desk_logic_test.cpp).
bool parseWeatherJson(const char* json, WeatherData& out);   // false if the current conditions are missing
WeatherKind weatherKind(int wmoCode);
int weekdayOf(const char* ymd);                              // 0 = Sunday .. 6 = Saturday, -1 if malformed
// Weather.cpp (network, ESP32 only)
bool weatherFetch(WeatherData& out, float latitude, float longitude);                         // HTTPS request, about 1-2 s; false on failure
