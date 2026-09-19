#include "prefs_helpers.h"
#include "config.h"

// ============ Текущая частота в NVS ============

static Preferences prefs;
static bool started = false;

Preferences& snifferPrefs() { return prefs; }

void prefsBegin() {
    if (!started) {
        prefs.begin(NVS_NAMESPACE, false);
        started = true;
    }
}

float prefsGetCurFreq() {
    prefsBegin();
    return prefs.getFloat(NVS_KEY_CUR_FREQ, BARRIER_FREQ_MHZ);
}

void prefsSetCurFreq(float f) {
    prefsBegin();
    prefs.putFloat(NVS_KEY_CUR_FREQ, f);
}