#pragma once
#include "Arduino.h"
enum {WL_CONNECTED=3,WL_DISCONNECTED=6}; enum {WIFI_STA=1}; enum {WIFI_AUTH_OPEN=0};
struct WiFiMock{ int st=WL_DISCONNECTED; std::string ssid,pass; bool okPass=true;
 void disconnect(bool){st=WL_DISCONNECTED;} void mode(int){} void begin(const char*s,const char*p){ssid=s;pass=p; st=(pass=="goodpass")?WL_CONNECTED:WL_DISCONNECTED;}
 int status(){return st;} int scanNetworks(){return 1;} String SSID(int){return String("Home");} int RSSI(int){return -50;} int encryptionType(int){return 3;} void scanDelete(){} };
inline WiFiMock WiFi;
