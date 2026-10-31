#pragma once
#include "TaskStore.h"
#include "App.h"
#include "TzRule.h"
// Local web pages (English / French) served by the watch at http://cyd.local, from a phone or computer on the same Wi-Fi:
//   /          To-Do (the Post-it)        /location   weather town, language, time zone        /radios   the three web radios
// onChange runs after every task modification; onSettings after the language, time zone or weather town changes;
// onRadios after the radios were rewritten (the radio is stopped first).
void beginTaskWeb(TaskStore& store, void (*onChange)(), Config& cfg, void (*onSettings)(), void (*onRadios)());
void handleTaskWeb();   // call often from loop(); starts the server once Wi-Fi is up
