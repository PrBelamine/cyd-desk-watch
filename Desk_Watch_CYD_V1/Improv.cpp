#include "Improv.h"
#include <WiFi.h>
#include <string.h>
#include "Settings.h"

namespace {
enum : uint8_t { PKT_STATE = 0x01, PKT_ERROR = 0x02, PKT_RPC = 0x03, PKT_RESULT = 0x04 };
enum : uint8_t { ST_READY = 0x02, ST_PROVISIONING = 0x03, ST_PROVISIONED = 0x04 };
enum : uint8_t { ERR_NONE = 0x00, ERR_INVALID = 0x01, ERR_UNKNOWN_CMD = 0x02, ERR_CONNECT = 0x03 };
enum : uint8_t { CMD_WIFI = 0x01, CMD_STATE = 0x02, CMD_INFO = 0x03, CMD_SCAN = 0x04 };

constexpr uint32_t CONNECT_TIMEOUT_MS = 20000;

Preferences* store = nullptr;
uint8_t buf[160];                 // one incoming packet (header + data + checksum)
size_t have = 0;
uint8_t state = ST_READY;
bool connecting = false;
uint32_t connectStart = 0;
char pendingSsid[33], pendingPass[65];

void sendPacket(uint8_t type, const uint8_t* data, size_t length) {
  uint8_t out[160]; size_t n = 0;
  memcpy(out, "IMPROV", 6); n = 6;
  out[n++] = 1; out[n++] = type; out[n++] = uint8_t(length);
  memcpy(out + n, data, length); n += length;
  uint8_t sum = 0; for (size_t i = 0; i < n; ++i) sum += out[i];
  out[n++] = sum;
  Serial.write(out, n);
  Serial.write('\n');
}
void sendState(uint8_t s) { state = s; sendPacket(PKT_STATE, &s, 1); }
void sendError(uint8_t e) { sendPacket(PKT_ERROR, &e, 1); }
// RPC result: command id, data length, then length-prefixed strings.
void sendResult(uint8_t command, const char* const* strings, size_t count) {
  uint8_t data[140]; size_t n = 2; data[0] = command;
  for (size_t i = 0; i < count; ++i) {
    const size_t l = strlen(strings[i]);
    if (n + 1 + l > sizeof data) break;
    data[n++] = uint8_t(l); memcpy(data + n, strings[i], l); n += l;
  }
  data[1] = uint8_t(n - 2);
  sendPacket(PKT_RESULT, data, n);
}
String siteUrl() { return String("http://") + TASK_HOSTNAME + ".local/location"; }

void reportState() {
  sendState(WiFi.status() == WL_CONNECTED ? ST_PROVISIONED : ST_READY);
  if (state == ST_PROVISIONED) { const String url = siteUrl(); const char* s[] = {url.c_str()}; sendResult(CMD_STATE, s, 1); }
  else sendResult(CMD_STATE, nullptr, 0);
}
void startConnect(const char* ssid, const char* pass) {
  strlcpy(pendingSsid, ssid, sizeof pendingSsid); strlcpy(pendingPass, pass, sizeof pendingPass);
  WiFi.disconnect(false); WiFi.mode(WIFI_STA); WiFi.begin(pendingSsid, pendingPass);
  connecting = true; connectStart = millis();
  sendState(ST_PROVISIONING);
}
void handleRpc(const uint8_t* d, size_t length) {
  if (length < 2 || size_t(d[1]) + 2 != length) { sendError(ERR_INVALID); return; }
  sendError(ERR_NONE);
  switch (d[0]) {
    case CMD_WIFI: {
      const size_t ssidLen = d[2];
      if (length < 4 || 3 + ssidLen >= length) { sendError(ERR_INVALID); return; }
      const size_t passLen = d[3 + ssidLen];
      if (4 + ssidLen + passLen != length || ssidLen == 0 || ssidLen > 32 || passLen > 64) { sendError(ERR_INVALID); return; }
      char ssid[33], pass[65];
      memcpy(ssid, d + 3, ssidLen); ssid[ssidLen] = 0;
      memcpy(pass, d + 4 + ssidLen, passLen); pass[passLen] = 0;
      startConnect(ssid, pass);
      break;
    }
    case CMD_STATE: reportState(); break;
    case CMD_INFO: {
      const char* s[] = {"Desk Watch CYD", "1.0", "ESP32", "Desk Watch"};
      sendResult(CMD_INFO, s, 4);
      break;
    }
    case CMD_SCAN: {
      const int found = WiFi.scanNetworks();
      for (int i = 0; i < found && i < 12; ++i) {
        const String name = WiFi.SSID(i); char rssi[8]; snprintf(rssi, sizeof rssi, "%d", WiFi.RSSI(i));
        if (!name.length() || name.length() > 32) continue;
        const char* s[] = {name.c_str(), rssi, WiFi.encryptionType(i) == WIFI_AUTH_OPEN ? "NO" : "YES"};
        sendResult(CMD_SCAN, s, 3);
      }
      sendResult(CMD_SCAN, nullptr, 0);
      WiFi.scanDelete();
      break;
    }
    default: sendError(ERR_UNKNOWN_CMD);
  }
}
}  // namespace

bool wifiLoadSaved(Preferences& prefs, char* ssid, size_t ssidSize, char* password, size_t passwordSize) {
  ssid[0] = password[0] = 0;
  prefs.getString("wssid", ssid, ssidSize);
  prefs.getString("wpass", password, passwordSize);
  return ssid[0] != 0;
}

void improvBegin(Preferences& prefs) { store = &prefs; have = 0; state = ST_READY; }

void improvPoll() {
  if (connecting) {
    if (WiFi.status() == WL_CONNECTED) {
      connecting = false;
      if (store) { store->putString("wssid", pendingSsid); store->putString("wpass", pendingPass); }
      sendState(ST_PROVISIONED);
      const String url = siteUrl(); const char* s[] = {url.c_str()}; sendResult(CMD_WIFI, s, 1);
    } else if (millis() - connectStart > CONNECT_TIMEOUT_MS) {
      connecting = false; WiFi.disconnect(false);
      sendState(ST_READY); sendError(ERR_CONNECT);
    }
  }
  while (Serial.available()) {
    const uint8_t c = uint8_t(Serial.read());
    if (have < 6) {                                   // looking for the "IMPROV" header
      if (c == uint8_t("IMPROV"[have])) buf[have++] = c;
      else if (c == 'I') { buf[0] = c; have = 1; }
      else have = 0;
      continue;
    }
    if (have >= sizeof buf) { have = 0; continue; }
    buf[have++] = c;
    if (have >= 9) {
      const size_t total = 9 + size_t(buf[8]) + 1;
      if (total > sizeof buf) { have = 0; continue; }
      if (have == total) {
        uint8_t sum = 0; for (size_t i = 0; i + 1 < total; ++i) sum += buf[i];
        if (sum == buf[total - 1] && buf[6] == 1 && buf[7] == PKT_RPC) handleRpc(buf + 9, buf[8]);
        else if (sum != buf[total - 1]) sendError(ERR_INVALID);
        have = 0;
      }
    }
  }
}
