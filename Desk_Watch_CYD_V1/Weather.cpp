// Downloads the weather from Open-Meteo (https://open-meteo.com, free for non-commercial use, no API key).
#include <Arduino.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <HTTPClient.h>
#include "Weather.h"
#include "Settings.h"

bool weatherFetch(WeatherData& out, float latitude, float longitude) {
  if (WiFi.status() != WL_CONNECTED) return false;
  char url[360];
  snprintf(url, sizeof url,
    "https://api.open-meteo.com/v1/forecast?latitude=%.3f&longitude=%.3f"
    "&current=temperature_2m,relative_humidity_2m,apparent_temperature,weather_code,wind_speed_10m,is_day"
    "&daily=weather_code,temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=4",
    latitude, longitude);
  WiFiClientSecure client;
  client.setInsecure();                    // public weather data only: nothing private is sent, so the certificate is not checked
  client.setTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(5000); http.setTimeout(5000);
  if (!http.begin(client, url)) return false;
  bool ok = false;
  if (http.GET() == 200) {
    const String body = http.getString();
    WeatherData d;
    if (parseWeatherJson(body.c_str(), d)) { d.updatedMs = millis(); out = d; ok = true; }
  }
  http.end();
  return ok;
}
