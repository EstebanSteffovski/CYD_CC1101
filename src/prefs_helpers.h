#pragma once
#include <Preferences.h>

// ============ Текущая частота в NVS (переживает ребут) ============
// prefs уже открыт в signal_store (namespace "sniffer") — используем его.

extern Preferences& snifferPrefs();

void    prefsBegin();
float   prefsGetCurFreq();
void    prefsSetCurFreq(float f);