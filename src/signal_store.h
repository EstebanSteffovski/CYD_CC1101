// signal_store.h должен видеть SLOT_COUNT
#include "config.h"
#include <Arduino.h>
#include <Preferences.h>

// ============ ХРАНИЛИЩЕ Захваченных Сигналов (NVS) ============
// v2: 3 слота. Слот хранит: частоту, модуляцию, импульсы (u16 LE), уровни.
// Ключи: slot0_pulses, slot0_len, slot0_levels, slot0_freq, slot0_mod.
// Старая запись одной ячейки (freq_mhz/pulses_len/pulses) мигрирует в слот 0.

struct StoredSignal {
    bool valid = false;
    float freqMHz = 433.92f;
    uint8_t modulation = 2;
    uint16_t count = 0;                       // сколько импульсов
    uint16_t pulses[400];                     // до 400 импульсов
    uint8_t levels[400];                      // уровень каждого импульса (1=HIGH, 0=LOW)
};

class SignalStore {
public:
    void begin();                             // init + миграция старых ключей
    bool save(int slot, const StoredSignal& s);
    bool load(int slot, StoredSignal& s);     // false = слот пуст
    bool clear(int slot);                     // стереть слот
    bool hasSignal(int slot) const;
private:
    Preferences prefs;
    bool hasAny[SLOT_COUNT] = {false, false, false};
    void migrateLegacy();
};

extern SignalStore store;