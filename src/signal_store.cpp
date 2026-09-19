#include "signal_store.h"
#include "config.h"

SignalStore store;

void SignalStore::begin() {
    prefs.begin(NVS_NAMESPACE, false);
    migrateLegacy();
    for (int i = 0; i < SLOT_COUNT; i++) {
        char key[16];
        snprintf(key, sizeof(key), "slot%d_len", i);
        hasAny[i] = prefs.isKey(key);
        if (hasAny[i]) {
            Serial.printf("[NVS] Слот %d: есть сигнал\r\n", i);
        }
    }
}

// Перенос старой одиночной записи (v1.0.x) в слот 0 — один раз, ключи старые удаляем
void SignalStore::migrateLegacy() {
    if (!prefs.isKey(NVS_OLD_KEY_LEN)) return;
    uint16_t len = prefs.getUShort(NVS_OLD_KEY_LEN, 0);
    if (len == 0 || len > 400) { prefs.remove(NVS_OLD_KEY_LEN); return; }
    if (prefs.getBytesLength(NVS_OLD_KEY_DATA) != len * sizeof(uint16_t)) {
        // битая старая запись — просто сносим
        prefs.remove(NVS_OLD_KEY_LEN); prefs.remove(NVS_OLD_KEY_DATA);
        prefs.remove(NVS_OLD_KEY_FREQ); prefs.remove(NVS_OLD_KEY_MOD);
        prefs.remove(NVS_OLD_KEY_LEVELS);
        return;
    }
    StoredSignal s;
    s.valid = true;
    s.freqMHz = prefs.getFloat(NVS_OLD_KEY_FREQ, 433.92f);
    s.modulation = prefs.getUChar(NVS_OLD_KEY_MOD, 2);
    s.count = len;
    prefs.getBytes(NVS_OLD_KEY_DATA, s.pulses, len * sizeof(uint16_t));
    if (prefs.isKey(NVS_OLD_KEY_LEVELS) && prefs.getBytesLength(NVS_OLD_KEY_LEVELS) == len) {
        prefs.getBytes(NVS_OLD_KEY_LEVELS, s.levels, len);
    } else {
        for (size_t i = 0; i < len; i++) s.levels[i] = (i % 2 == 0) ? 1 : 0;
    }
    save(0, s);   // в слот 0
    prefs.remove(NVS_OLD_KEY_LEN); prefs.remove(NVS_OLD_KEY_DATA);
    prefs.remove(NVS_OLD_KEY_FREQ); prefs.remove(NVS_OLD_KEY_MOD);
    prefs.remove(NVS_OLD_KEY_LEVELS);
    Serial.println("[NVS] Старая запись мигрирована в слот 1");
}

bool SignalStore::save(int slot, const StoredSignal& s) {
    if (slot < 0 || slot >= SLOT_COUNT) return false;
    if (!s.valid || s.count == 0) return false;
    char key[16];
    snprintf(key, sizeof(key), "slot%d_freq", slot);
    prefs.putFloat(key, s.freqMHz);
    snprintf(key, sizeof(key), "slot%d_mod", slot);
    prefs.putUChar(key, s.modulation);
    snprintf(key, sizeof(key), "slot%d_len", slot);
    prefs.putUShort(key, s.count);
    snprintf(key, sizeof(key), "slot%d_pulses", slot);
    prefs.putBytes(key, s.pulses, s.count * sizeof(uint16_t));
    snprintf(key, sizeof(key), "slot%d_levels", slot);
    prefs.putBytes(key, s.levels, s.count);
    hasAny[slot] = true;
    Serial.printf("[NVS] Слот %d сохранён: %.2f МГц, %u имп.\r\n",
                  slot + 1, s.freqMHz, s.count);
    return true;
}

bool SignalStore::load(int slot, StoredSignal& s) {
    if (slot < 0 || slot >= SLOT_COUNT) return false;
    char key[16];
    snprintf(key, sizeof(key), "slot%d_len", slot);
    if (!prefs.isKey(key)) return false;
    uint16_t len = prefs.getUShort(key, 0);
    if (len == 0 || len > 400) return false;
    snprintf(key, sizeof(key), "slot%d_pulses", slot);
    if (prefs.getBytesLength(key) != len * sizeof(uint16_t)) return false;
    s.valid = true;
    snprintf(key, sizeof(key), "slot%d_freq", slot);
    s.freqMHz = prefs.getFloat(key, 433.92f);
    snprintf(key, sizeof(key), "slot%d_mod", slot);
    s.modulation = prefs.getUChar(key, 2);
    s.count = len;
    snprintf(key, sizeof(key), "slot%d_pulses", slot);
    prefs.getBytes(key, s.pulses, len * sizeof(uint16_t));
    snprintf(key, sizeof(key), "slot%d_levels", slot);
    if (prefs.isKey(key) && prefs.getBytesLength(key) == len) {
        prefs.getBytes(key, s.levels, len);
    } else {
        for (size_t i = 0; i < len; i++) s.levels[i] = (i % 2 == 0) ? 1 : 0;
    }
    return true;
}

bool SignalStore::hasSignal(int slot) const {
    if (slot < 0 || slot >= SLOT_COUNT) return false;
    return hasAny[slot];
}

bool SignalStore::clear(int slot) {
    if (slot < 0 || slot >= SLOT_COUNT) return false;
    char key[16];
    const char* suffixes[] = {"freq", "mod", "len", "pulses", "levels"};
    for (const char* suf : suffixes) {
        snprintf(key, sizeof(key), "slot%d_%s", slot, suf);
        prefs.remove(key);
    }
    hasAny[slot] = false;
    Serial.printf("[NVS] Слот %d очищен\r\n", slot + 1);
    return true;
}