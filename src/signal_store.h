#pragma once
#include <Arduino.h>
#include <Preferences.h>

// ============ ХРАНИЛИЩЕ Захваченных Сигналов (NVS) ============
// Храним: частоту, длительности импульсов (мкс), модуляцию.
// NVS: строка 'pulses' — бинарные u16 LE, 'pulses_len' — количество.

struct StoredSignal {
    bool valid = false;
    float freqMHz = 433.92f;
    uint8_t modulation = 2;
    uint16_t count = 0;                       // сколько импульсов
    uint16_t pulses[400];                     // до 400 импульсов
};

class SignalStore {
public:
    void begin();
    bool save(const StoredSignal& s);         // перезаписывает ячейку slot
    bool load(StoredSignal& s);               // загрузить
    bool hasSignal() const { return hasAny; }
private:
    Preferences prefs;
    bool hasAny = false;
};

extern SignalStore store;