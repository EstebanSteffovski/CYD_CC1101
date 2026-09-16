#include "signal_store.h"
#include "config.h"

SignalStore store;

void SignalStore::begin() {
    prefs.begin(NVS_NAMESPACE, false);
    hasAny = prefs.isKey(NVS_KEY_LEN);
    if (hasAny) {
        Serial.println("[NVS] Есть сохранённый сигнал");
    }
}

bool SignalStore::save(const StoredSignal& s) {
    if (!s.valid || s.count == 0) return false;
    // Перезаписываем ключи: сначала удаляем, потом пишем
    prefs.clear();  // в namespace только наши ключи
    prefs.putFloat(NVS_KEY_FREQ, s.freqMHz);
    prefs.putUChar(NVS_KEY_MOD, s.modulation);
    prefs.putUShort(NVS_KEY_LEN, s.count);
    size_t bytes = s.count * sizeof(uint16_t);
    prefs.putBytes(NVS_KEY_DATA, s.pulses, bytes);
    hasAny = true;
    Serial.printf("[NVS] Сохранено: %.2f МГц, %u импульсов\r\n", s.freqMHz, s.count);
    return true;
}

bool SignalStore::load(StoredSignal& s) {
    if (!prefs.isKey(NVS_KEY_LEN)) { s.valid = false; return false; }
    s.valid = true;
    s.freqMHz = prefs.getFloat(NVS_KEY_FREQ, 433.92f);
    s.modulation = prefs.getUChar(NVS_KEY_MOD, 2);
    s.count = prefs.getUShort(NVS_KEY_LEN, 0);
    if (s.count > 400) s.count = 400;
    if (s.count > 0) {
        prefs.getBytes(NVS_KEY_DATA, s.pulses, s.count * sizeof(uint16_t));
    }
    return true;
}