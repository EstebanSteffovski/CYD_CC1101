#include "radio_driver.h"
#include "config.h"
#include <SmartRC_CC1101.h>

// RMT через Arduino HAL (core 3.x): rmtInit/rmtRead/rmtWrite.
// Тик = 1/частота. Задаём 1 МГц => 1 тик = 1 мкс.

extern SmartRC_CC1101& rf;   // глобальный объект из main

CC1101Radio radio;

// Буферы RMT (максимум импульсов — половина, т.к. 2 значения на символ)
static rmt_data_t rmtSymbols[MAX_PULSES];

// Уровни последнего захвата — для replay (нормализация фазы)
static std::vector<uint8_t> startLevels;

// Уровень последнего сохранённого импульса (1=HIGH, 0=LOW); -1 если пусто
static int lastLevelOf(const CaptureResult& r) {
    return r.levels.empty() ? -1 : (int)r.levels.back();
}

// Совпадает ли уровень последнего импульса с заданным
static bool levelsMatch(const CaptureResult& r, uint8_t level) {
    return !r.levels.empty() && r.levels.back() == level;
}

// ===== Дамп захвата для диагностики протокола =====
static void dumpCapture(const CaptureResult& r) {
    size_t n = r.pulses.size();
    if (n == 0) return;

    // Статистика длительностей
    uint16_t minH = 0xFFFF, maxH = 0, minL = 0xFFFF, maxL = 0;
    for (size_t i = 0; i < n && i < r.levels.size(); i++) {
        if (r.levels[i] == 1) {
            if (r.pulses[i] < minH) minH = r.pulses[i];
            if (r.pulses[i] > maxH) maxH = r.pulses[i];
        } else {
            if (r.pulses[i] < minL) minL = r.pulses[i];
            if (r.pulses[i] > maxL) maxL = r.pulses[i];
        }
    }
    Serial.printf("[DUMP] HIGH: %u-%u мкс, LOW: %u-%u мкс\r\n",
                  (unsigned)minH, (unsigned)maxH, (unsigned)minL, (unsigned)maxL);

    // Первые 100 импульсов в формате "H:длительность " / "L:длительность "
    Serial.print("[DUMP] Импульсы: ");
    for (size_t i = 0; i < n && i < 100; i++) {
        Serial.printf("%s%u ", (i < r.levels.size() && r.levels[i] == 0) ? "L" : "H",
                      (unsigned)r.pulses[i]);
    }
    Serial.println();
}

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
    rf.setCCMode(false);            // база async
    rf.setDRate(2.5f);              // 2.5 кБод: T≈400 мкс — PT2262/EV1527 семейство
    rf.setRxBW(270.0f);             // полоса 270 кГц — устойчивость к расстройке
    rf.setSyncMode(0);              // MDMCFG2: OOK, без синхрослова
    // PKTCTRL0 = 0xC2: PKT_FORMAT=11 (асинхронный последовательный режим —
    // TX данные ВХОДЯТ с GDO0, RX данные ВЫХОДЯТ на GDO0), длина бесконечна,
    // без белила. Прежний 0x32 = PKT_FORMAT=00 (FIFO) — модулятор в TX
    // брал данные из пустого FIFO, а не с пина: в эфир шла чистая несущая!
    rf.SpiWriteReg(CC1101_PKTCTRL0, 0xC2);
    // IOCFG0 = 0x0D: RX — async serial data output; TX — async serial data input
    // (по даташиту CC1101 и практике Flipper/ESPHome). 0x2D из v1.0.8 был ошибкой.
    rf.SpiWriteReg(CC1101_IOCFG0, 0x0D);
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
        // Распаковка с учётом РЕАЛЬНЫХ уровней level0/level1 (v1.0.7).
        // Раньше предполагали чередование high/low начиная с high — но RMT
        // пишет фактический уровень пина, и запись могла начаться с LOW
        // (тишина перед посылкой) => повтор шёл в противофазе.
        // Храним пары (duration, level), затем нормализуем: посылка
        // должна начинаться с HIGH-импульса.
        struct RawPulse { uint16_t dur; uint8_t level; };
        static std::vector<RawPulse> raw;   // static — не фрагментируем кучу
        raw.clear();
        for (size_t i = 0; i < readSymbols; i++) {
            if (rmtSymbols[i].duration0 > 0)
                raw.push_back({rmtSymbols[i].duration0, (uint8_t)rmtSymbols[i].level0});
            if (rmtSymbols[i].duration1 > 0)
                raw.push_back({rmtSymbols[i].duration1, (uint8_t)rmtSymbols[i].level1});
        }

        // Программный фильтр глитчей (< MIN_PULSE_US приклеиваем к соседнему
        // импульсу ТОГО ЖЕ уровня; чередование high/low сохраняется само,
        // т.к. уровни теперь реальные).
        for (size_t i = 0; i < raw.size() && result.pulses.size() < MAX_PULSES; i++) {
            if (raw[i].dur < MIN_PULSE_US) {
                // глитч: слить с предыдущим импульсом того же уровня
                if (!result.pulses.empty() && raw[i].level == lastLevelOf(result)) {
                    // соседний тот же уровень -> расширяем (бывает после склейки)
                    result.pulses.back() += raw[i].dur;
                }
                continue;
            }
            // Если предыдущий сохранённый импульс был ТОГО ЖЕ уровня —
            // склеиваем (между ними был отфильтрованный глитч)
            if (!result.pulses.empty() && raw[i].level == lastLevelOf(result) &&
                result.pulses.size() > 0 && levelsMatch(result, raw[i].level)) {
                result.pulses.back() += raw[i].dur;
            } else {
                result.pulses.push_back(raw[i].dur);
                result.levels.push_back(raw[i].level);
            }
        }

        // Нормализация: посылка должна начинаться с HIGH.
        // Если первый импульс LOW (была тишина) — отбрасываем ведущие LOW,
        // иначе replay начнёт с LOW и весь сигнал пойдёт в противофазе.
        size_t start = 0;
        if (!result.pulses.empty() && !result.levels.empty() && result.levels[0] == 0) {
            // ведущие LOW: пропускаем, но если LOW был длинным (пауза) — просто теряем его,
            // это безопасно: приёмник синхронизируется по первому фронту
            start = 1;
            result.pulses.erase(result.pulses.begin());
            result.levels.erase(result.levels.begin());
            // после отброса LOW на чётность не проверяем — уровни реальные
        }

        result.state = CAP_DONE;
        result.rssi = readRssi();
        Serial.printf("[SNIF] Захвачено %u имп., RSSI=%d\r\n",
                      (unsigned)result.pulses.size(), result.rssi);

        // ===== ДАМП для диагностики протокола =====
        dumpCapture(result);
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
void CC1101Radio::replay(const std::vector<uint16_t>& pulses, uint8_t mod,
                         const std::vector<uint8_t>& levels) {
    if (pulses.size() < 2) return;

    // Сохраняем уровни для нормализации фазы
    startLevels = levels;
    if (startLevels.empty()) {
        // Нет уровней (старая запись) — считаем чередование с HIGH
        startLevels.resize(pulses.size());
        for (size_t i = 0; i < startLevels.size(); i++) {
            startLevels[i] = (i % 2 == 0) ? 1 : 0;
        }
    }

    // Async TX: PKTCTRL0=0xC2 (PKT_FORMAT=11 — данные с GDO0 в модулятор),
    // IOCFG0=0x0D (async data input), модуляция OOK.
    // ФИКС v1.0.9: было PKT_FORMAT=00 (FIFO) — модулятор читал пустой FIFO,
    // в эфир уходила чистая немодулированная несущая.
    rf.setSidle();
    rf.setCCMode(false);
    rf.setModulation(2);            // ASK/OOK
    rf.setDRate(2.5f);              // как при захвате: T≈400 мкс
    rf.setPA(10);                   // максимальная мощность +10 дБм
    rf.SpiWriteReg(CC1101_PKTCTRL0, 0xC2);  // async serial: данные с GDO0
    rf.SpiWriteReg(CC1101_IOCFG0, 0x0D);   // GDO0 = вход данных модулятора в TX
    rf.SetTx();                     // несущая, ждёт данных с GDO0
    delay(2);

    // RMT TX: 1 МГц, 1 тик = 1 мкс
    // Сигнал уже нормализован при захвате: [0]=HIGH, [1]=LOW, чередование.
    // Проверим на всякий случай по сохранённым уровням (если есть).
    size_t nSymbols = 0;
    size_t startIdx = 0;
    if (!startLevels.empty() && startIdx < startLevels.size() && startLevels[0] == 0) {
        startIdx = 1;   // начинаем с HIGH
    }
    // Стартовая LOW-пауза 4 мс + фронт-заглушка 100 мкс (RMT требует duration>0),
    // затем основная последовательность с HIGH. Приёмник синхронизируется по первому фронту.
    rmtSymbols[0].level0 = 0;  rmtSymbols[0].duration0 = 4000;
    rmtSymbols[0].level1 = 1;  rmtSymbols[0].duration1 = 100;
    nSymbols = 1;
    for (size_t i = startIdx; i + 1 < pulses.size(); i += 2) {
        uint16_t hi = pulses[i];
        uint16_t lo = pulses[i + 1];
        if (hi == 0) hi = 1;
        if (lo == 0) lo = 1;
        rmtSymbols[nSymbols].level0 = 1;
        rmtSymbols[nSymbols].duration0 = hi;
        rmtSymbols[nSymbols].level1 = 0;
        rmtSymbols[nSymbols].duration1 = lo;
        nSymbols++;
        if (nSymbols >= MAX_PULSES - 1) break;
    }

    rmtInit(CC1101_GDO0, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_2, 1000000);
    rmtSetEOT(CC1101_GDO0, 0);   // после конца — LOW (нет несущей)

    // Отправляем посылку 4 раза с паузой ~15 мс (пульты шлют 3-5 повторов,
    // приёмник шлагбаума ждёт валидный фрейм, единичная посылка часто теряется)
    for (int rep = 0; rep < 4; rep++) {
        rmtWrite(CC1101_GDO0, rmtSymbols, nSymbols, RMT_WAIT_FOR_EVER);
        if (rep < 3) delay(15);
    }

    rmtDeinit(CC1101_GDO0);

    rf.setSidle();
    Serial.println("[REPLAY] Отправлено x4");

    configurePacketMode();
    rf.SetRx();
}