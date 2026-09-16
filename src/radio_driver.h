#pragma once
#include <Arduino.h>
#include <vector>
#include "config.h"

// ============ ДРАЙВЕР CC1101 (поверх SmartRC-CC1101) ============
// Режимы:
//  - пакетный (ccmode=true): приём/передача готовых пакетов
//  - асинхронный OOK (ccmode=false, PKTFORMAT=0x30): GDO0 выдаёт сырой
//    демодулированный сигнал — импульсы читаем по таймеру, как логический анализатор
// Вся работа с радио — только через этот класс.

enum CaptureState {
    CAP_IDLE = 0,       // ничего не делаем
    CAP_WAITING,        // ждём сигнал (снифер)
    CAP_DONE,           // захват завершён
    CAP_TIMEOUT         // таймаут, сигнала нет
};

struct CaptureResult {
    CaptureState state;
    std::vector<uint16_t> pulses;  // длительности импульсов, мкс (чётный=high, нечётный=low)
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
    void startPacketRx();                       // в режим RX (пакеты)
    bool hasPacket();                           // флаг пакета (GDO0 + RXBYTES)
    int  readPacket(uint8_t* buf, int maxLen);  // прочитать пакет (-1 = нет/CRC fail)
    void sendPacket(const uint8_t* data, int len, int repeats = 1);

    // --- Асинхронный захват (снифер) ---
    // Блокирует на время до timeoutMs. Пишет импульсы GDO0 в result.pulses.
    void captureAsync(CaptureResult& result, uint32_t timeoutMs);

    // RSSI текущего канала (для индикации)
    int readRssi();

    // Перевести радио в IDLE
    void idle();

    // Воспроизведение захваченной последовательности импульсов через GDO0 (RMT)
    void replay(const std::vector<uint16_t>& pulses, uint8_t modulation);

    bool isInitialized() const { return initialized; }

private:
    float freqMHz = BARRIER_FREQ_MHZ;
    uint8_t modulation = 2;  // ASK/OOK по умолчанию (шлагбаумы почти все OOK)
    bool initialized = false;
    void configureAsyncOOK();   // регистровая настройка прозрачного режима
    void configurePacketMode();
};

extern CC1101Radio radio;