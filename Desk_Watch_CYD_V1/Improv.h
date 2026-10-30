#pragma once
// Wi-Fi setup over the USB cable ("Improv Wi-Fi Serial", https://www.improv-wifi.com).
// The web installer on minitech.club uses it to send the Wi-Fi name and password to the board
// after flashing; they are stored in the board's flash, never in the firmware file.
#include <Arduino.h>
#include <Preferences.h>

// Reads the saved Wi-Fi settings into ssid/password. Returns false when none are saved.
bool wifiLoadSaved(Preferences& prefs, char* ssid, size_t ssidSize, char* password, size_t passwordSize);
void improvBegin(Preferences& prefs);   // call once in setup()
void improvPoll();                      // call often from loop(): reads the serial port, answers the installer
