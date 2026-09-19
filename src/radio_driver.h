#pragma once
#include <Arduino.h>
#include "config.h"
#include <vector>

// ============ ДРАЙВЕР CC1101 (поверх SmartRC-CC1101) ============
// Режимы:
//  - пакетный (ccmode=true): приём/передача готовых пакетов
//  - асинхронный OOK (ccmode=false): GDO0 выдаёт/принимает сырой битовый поток
// Вся работа с радио — только через этот класс.

enum CaptureState {
    CAP_IDLE = 0,       // ничего не делаем
    CAP_WAITING,        // ждём сигнал (снифер)
    CAP_DONE,           // захват завершён
    CAP_TIMEOUT         // таймаут, сигнала нет
};

struct CaptureResult {
    CaptureState state;
    std::vector<uint16_t> pulses;  // длительности импульсов, мкс
    std::vector<uint8_t> levels;   // уровень каждого импульса (1=HIGH, 0=LOW)
    float freqMHz;                 // частота, на которой слушали
    int rssi;                      // RSSI в момент захвата
};

class CC1101Radio {
public:
    void begin();                  // инициализация SPI+радио, пакетный режим
    bool detected();               // есть ли CC1101 на шине
    void setFrequency(float mhz);  // смена частоты
    float getFrequency() const { return freqMHz; }
    void setModulation(uint8_t m); // 0=2FSK 1=GFSK 2=ASK/OOK 3=4FSK 4=MSK

    // --- Пакетный режим ---
    void startPacketRx();
    bool hasPacket();
    int  readPacket(uint8_t* buf, int maxLen);
    void sendPacket(const uint8_t* data, int len, int repeats = 1);

    // --- Асинхронный захват (снифер) ---
    // captureAsync: блокирует до timeoutMs (старый вызов, оставлен для совместимости)
    void captureAsync(CaptureResult& result, uint32_t timeoutMs);
    // captureRound: ОДИН раунд ожидания roundMs — вызывается в цикле,
    // между раундами loop() живёт (тач, экран). Возвращает true, если поймали.
    bool captureRound(CaptureResult& result, uint32_t roundMs);
    void finishCapture();          // вернуть радио в пакетный режим после раундов

    int readRssi();
    void idle();

    // Воспроизведение через GDO0 (RMT TX -> CC1101 async)
    void replay(const std::vector<uint16_t>& pulses, uint8_t modulation,
                const std::vector<uint8_t>& levels = std::vector<uint8_t>());

    bool isInitialized() const { return initialized; }

    // Переключение мультиплексора VSPI: CC1101 (18/19/23) <-> тач (25/32/39)
    void spiToRadio();
    void spiToTouch();

private:
    float freqMHz = BARRIER_FREQ_MHZ;
    uint8_t modulation = 2;  // ASK/OOK по умолчанию
    bool initialized = false;
    void configureAsyncOOK();
    void configurePacketMode();
};

extern CC1101Radio radio;