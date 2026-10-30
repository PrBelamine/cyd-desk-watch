#pragma once
#include "TaskStore.h"
#include "App.h"
#include "TzRule.h"
// Local web pages (English / French) to edit the Post-it and the settings (language, time zone)
// from a phone or computer on the same Wi-Fi.
// onChange runs after every task modification; onSettings after the language or time zone changes.
void beginTaskWeb(TaskStore& store, void (*onChange)(), Config& cfg, void (*onSettings)());
void handleTaskWeb();   // call often from loop(); starts the server once Wi-Fi is up
