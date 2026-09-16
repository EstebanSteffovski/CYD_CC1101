#include "radio_driver.h"
#include "config.h"
#include <SmartRC_CC1101.h>

// RMT через Arduino HAL (core 3.x): rmtInit/rmtRead/rmtWrite.
// Тик = 1/частота. Задаём 1 МГц => 1 тик = 1 мкс.

extern SmartRC_CC1101& rf;   // глобальный объект из main

CC1101Radio radio;

// Буферы RMT (максимум импульсов — половина, т.к. 2 значения на символ)
static rmt_data_t rmtSymbols[MAX_PULSES];

// ================== МУЛЬТИПЛЕКСОР VSPI ==================
// CC1101 (18/19/23) и тач (25/32/39) не могут работать одновременно:
// перед радиооперацией переключаем мультиплексор на радио,
// после — обратно на тач. Вызывается из main вокруг каждого радио-действия.

void CC1101Radio::spiToRadio() {
    SPI.end();   // иначе core 3.x проигнорирует повторный begin с новыми пинами
    SPI.begin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
}

void CC1101Radio::spiToTouch() {
    SPI.end();
    SPI.begin(TOUCH_SCK, TOUCH_MISO, TOUCH_MOSI, -1);
    // CS тача управляется библиотекой XPT2046 сама (пин 33)
}

// ================== ИНИЦИАЛИЗАЦИЯ ==================

void CC1101Radio::begin() {
    // Пины задаём ДО Init — библиотека их подхватит
    rf.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
    rf.setGDO0(CC1101_GDO0);
    SPI.end();   // тач мог уже владеть шиной — переинициализируем честно
    rf.Init();   // SPI.begin(SCK,MISO,MOSI,SS) + Reset + RegConfig
    rf.setCCMode(true);              // пакетный режим по умолчанию
    rf.setModulation(modulation);    // 2 = ASK/OOK
    rf.setMHZ(freqMHz);
    rf.SetRx();
    initialized = true;
    Serial.printf("[CC1101] init, %.2f МГц, модуляция OOK\r\n", freqMHz);
}

bool CC1101Radio::detected() {
    return rf.getCC1101();
}

void CC1101Radio::setFrequency(float mhz) {
    freqMHz = mhz;
    rf.SetRx(mhz);   // SIDLE + setMHZ + SRX
}

void CC1101Radio::setModulation(uint8_t m) {
    modulation = m;
    rf.setModulation(m);
}

void CC1101Radio::idle() {
    rf.setSidle();
}

int CC1101Radio::readRssi() {
    return rf.getRssi();
}

// ================== ПАКЕТНЫЙ РЕЖИМ ==================

void CC1101Radio::startPacketRx() {
    configurePacketMode();
    rf.SetRx();
}

void CC1101Radio::configurePacketMode() {
    rf.setCCMode(true);
    rf.setModulation(modulation);
}

bool CC1101Radio::hasPacket() {
    return rf.CheckReceiveFlag() == 1;
}

int CC1101Radio::readPacket(uint8_t* buf, int maxLen) {
    if (!rf.CheckCRC()) return -1;
    int len = rf.ReceiveData(buf);
    if (len > maxLen) len = maxLen;
    return len;
}

void CC1101Radio::sendPacket(const uint8_t* data, int len, int repeats) {
    for (int i = 0; i < repeats; i++) {
        rf.SendData((byte*)data, (byte)len);
        delay(10);
    }
}

// ================== АСИНХРОННЫЙ Захват (СНИФЕР) ==================

// Прозрачный async-режим: GDO0 повторяет демодулированный сигнал (IOCFG0=0x0D,
// PKTCTRL0=0x32 = без CRC, бесконечная длина, без белила).
void CC1101Radio::configureAsyncOOK() {
    rf.setCCMode(false);            // IOCFG0=0x0D, PKTCTRL0=0x32
    rf.setDRate(10.0f);             // 10 кБод — типичный диапазон статики шлагбаумов
    rf.setRxBW(270.0f);             // полоса 270 кГц — устойчивость к расстройке
    rf.setSyncMode(0);              // MDMCFG2 без требования синхрослова
    rf.SpiWriteReg(CC1101_PKTCTRL0, 0x32);  // white off, без CRC, длина бесконечна
    rf.SpiWriteReg(CC1101_IOCFG0, 0x0D);    // GDO0 = демодулированные данные (async)
    rf.SetRx();
}

void CC1101Radio::captureAsync(CaptureResult& result, uint32_t timeoutMs) {
    result.state = CAP_WAITING;
    result.pulses.clear();
    result.freqMHz = freqMHz;
    result.rssi = -999;

    configureAsyncOOK();
    delay(5);

    // RMT RX: GDO0 -> RMT. 1 МГц => 1 тик = 1 мкс.
    // 4 блока = 256 символов (2 блоков мало: rmt_receive отвергает конфиг)
    rmtInit(CC1101_GDO0, RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_4, 1000000);
    // Аппаратный фильтр глитчей НЕ используем: на классическом ESP32 фильтр
    // тактируется от APB 80 МГц с максимумом 255 тиков = 3187 нс (rmt_receive
    // отвергает большие значения). Глитчи < 20 мкс режем программно при
    // распаковке (MIN_PULSE_US в config.h).
    // Порог тишины: максимум 32767 тиков (15 бит на символ). 30000 = 30 мс.
    rmtSetRxMaxThreshold(CC1101_GDO0, 30000);

    // ВАЖНО: readSymbols — входной параметр! Сколько символов максимум читать.
    // Баг v1.0.3: передавали 0 -> buffer_size = 0 -> rmt_receive invalid argument.
    size_t readSymbols = MAX_PULSES / 2;   // символов RMT (в каждом 2 импульса) = 200

    bool ok = rmtRead(CC1101_GDO0, rmtSymbols, &readSymbols, timeoutMs);

    rmtDeinit(CC1101_GDO0);

    if (ok && readSymbols > 0) {
        // Распаковка символов: duration0 (level0), duration1 (level1)
        // Программный фильтр глитчей: импульсы < MIN_PULSE_US выкидываем,
        // соседние короткие склеиваем, чтобы не ломать чётность high/low.
        for (size_t i = 0; i < readSymbols && result.pulses.size() < MAX_PULSES; i++) {
            uint16_t d0 = rmtSymbols[i].duration0;
            uint16_t d1 = rmtSymbols[i].duration1;
            bool valid0 = (d0 >= MIN_PULSE_US);
            bool valid1 = (d1 >= MIN_PULSE_US);
            if (valid0 && valid1) {
                result.pulses.push_back(d0);
                result.pulses.push_back(d1);
            } else if (valid0 && result.pulses.size() > 0) {
                // глитч на low: расширяем предыдущий low-импульс
                result.pulses.back() += d0;
            } else if (valid1 && result.pulses.size() > 0) {
                result.pulses.back() += d1;
            }
        }
        result.state = CAP_DONE;
        result.rssi = readRssi();
        Serial.printf("[SNIF] Захвачено %u имп., RSSI=%d\r\n",
                      (unsigned)result.pulses.size(), result.rssi);
    } else {
        result.state = CAP_TIMEOUT;
        Serial.println("[SNIF] Ничего не поймано");
    }

    // Вернуть радио в пакетный режим
    configurePacketMode();
    rf.SetRx();
}

// ================== ВОСПРОИЗВЕДЕНИЕ (RMT TX через GDO0 -> CC1101) ==================

// Async TX: CC1101 в TX-режиме читает данные С пина GDO0 (IOCFG0=0x0D в TX — вход данных).
// RMT гонит импульсы на GPIO27 (GDO0) -> CC1101 модулирует ими несущую.
void CC1101Radio::replay(const std::vector<uint16_t>& pulses, uint8_t mod) {
    if (pulses.size() < 2) return;

    // Async TX: PKTCTRL0=0x32, IOCFG0=0x0D (вход данных в TX), модуляция OOK
    rf.setSidle();
    rf.setCCMode(false);
    rf.setModulation(2);            // ASK/OOK
    rf.setDRate(10.0f);
    rf.SpiWriteReg(CC1101_IOCFG0, 0x0D);   // в TX — вход данных для модулятора
    rf.SetTx();                     // несущая, ждёт данных с GDO0
    delay(2);

    // RMT TX: 1 МГц, 1 тик = 1 мкс
    size_t nSymbols = 0;
    for (size_t i = 0; i + 1 < pulses.size(); i += 2) {
        uint16_t hi = pulses[i];
        uint16_t lo = pulses[i + 1];
        if (hi == 0) hi = 1;
        if (lo == 0) lo = 1;
        rmtSymbols[nSymbols].level0 = 1;
        rmtSymbols[nSymbols].duration0 = hi;
        rmtSymbols[nSymbols].level1 = 0;
        rmtSymbols[nSymbols].duration1 = lo;
        nSymbols++;
    }

    rmtInit(CC1101_GDO0, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_2, 1000000);
    rmtSetEOT(CC1101_GDO0, 0);   // после конца — LOW (нет несущей)
    rmtWrite(CC1101_GDO0, rmtSymbols, nSymbols, RMT_WAIT_FOR_EVER);
    rmtDeinit(CC1101_GDO0);

    rf.setSidle();
    Serial.println("[REPLAY] Отправлено");

    configurePacketMode();
    rf.SetRx();
}