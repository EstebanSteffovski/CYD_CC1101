#include "radio_driver.h"
#include "config.h"
#include <SmartRC_CC1101.h>

// RMT через Arduino HAL (core 3.x): rmtInit/rmtRead/rmtWrite.
// Тик = 1/частота. Задаём 1 МГц => 1 тик = 1 мкс.

extern SmartRC_CC1101& rf;   // глобальный объект из main

CC1101Radio radio;

// Буферы RMT (максимум импульсов — половина, т.к. 2 значения на символ)
static rmt_data_t rmtSymbols[MAX_PULSES];

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
    rf.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
    rf.setGDO0(CC1101_GDO0);
    SPI.end();
    rf.Init();
    rf.setCCMode(true);
    rf.setModulation(modulation);
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
    rf.SetRx(mhz);
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
    rf.setCCMode(false);
    rf.setDRate(2.5f);
    rf.setRxBW(270.0f);
    rf.setSyncMode(0);
    // RX-конфигурация: PKTCTRL0=0x32 + IOCFG0=0x0D — ПРОВЕРЕНО, захват работает
    rf.SpiWriteReg(CC1101_PKTCTRL0, 0x32);
    rf.SpiWriteReg(CC1101_IOCFG0, 0x0D);
    rf.SetRx();
}

// Один раунд: RMT слушает GDO0 до roundMs. true = поймали посылку.
// Побеждает «хуйня какая-то, но работает»: rmtRead с таймаутом раунда,
// между раундами тач живёт.
bool CC1101Radio::captureRound(CaptureResult& result, uint32_t roundMs) {
    configureAsyncOOK();
    delay(3);

    rmtInit(CC1101_GDO0, RMT_RX_MODE, RMT_MEM_NUM_BLOCKS_4, 1000000);
    rmtSetRxMaxThreshold(CC1101_GDO0, 30000);

    // ВАЖНО: readSymbols — входной параметр (максимум символов читать)
    size_t readSymbols = MAX_PULSES / 2;
    bool ok = rmtRead(CC1101_GDO0, rmtSymbols, &readSymbols, roundMs);
    rmtDeinit(CC1101_GDO0);

    if (!ok || readSymbols == 0) {
        // Раунд пуст — радио остаётся в async RX для следующего раунда
        return false;
    }

    // ==== Распаковка ====
    struct RawPulse { uint16_t dur; uint8_t level; };
    static std::vector<RawPulse> raw;
    raw.clear();
    for (size_t i = 0; i < readSymbols; i++) {
        if (rmtSymbols[i].duration0 > 0)
            raw.push_back({rmtSymbols[i].duration0, (uint8_t)rmtSymbols[i].level0});
        if (rmtSymbols[i].duration1 > 0)
            raw.push_back({rmtSymbols[i].duration1, (uint8_t)rmtSymbols[i].level1});
    }

    // Фильтр глитчей (< MIN_PULSE_US): слить с предыдущим импульсом того же уровня
    result.pulses.clear();
    result.levels.clear();
    for (size_t i = 0; i < raw.size() && result.pulses.size() < MAX_PULSES; i++) {
        if (raw[i].dur < MIN_PULSE_US) {
            if (!result.pulses.empty() && raw[i].level == lastLevelOf(result)) {
                result.pulses.back() += raw[i].dur;
            }
            continue;
        }
        if (!result.pulses.empty() && levelsMatch(result, raw[i].level)) {
            result.pulses.back() += raw[i].dur;   // склейка через отфильтрованный глитч
        } else {
            result.pulses.push_back(raw[i].dur);
            result.levels.push_back(raw[i].level);
        }
    }

    // Нормализация: посылка начинается с HIGH. Ведущий LOW отбрасываем.
    if (!result.pulses.empty() && result.levels[0] == 0) {
        result.pulses.erase(result.pulses.begin());
        result.levels.erase(result.levels.begin());
    }

    if (result.pulses.size() < 8) return false;   // слишком короткое — ждём дальше

    result.state = CAP_DONE;
    result.rssi = readRssi();
    Serial.printf("[SNIF] Захвачено %u имп., RSSI=%d\r\n",
                  (unsigned)result.pulses.size(), result.rssi);
    dumpCapture(result);
    return true;
}

// Общий таймаут поверх раундов — старый блокирующий путь (совместимость)
void CC1101Radio::captureAsync(CaptureResult& result, uint32_t timeoutMs) {
    result.state = CAP_WAITING;
    result.pulses.clear();
    result.freqMHz = freqMHz;
    result.rssi = -999;

    uint32_t deadline = millis() + timeoutMs;
    bool caught = false;
    while (millis() < deadline && !caught) {
        caught = captureRound(result, CAPTURE_ROUND_MS);
    }
    if (!caught) {
        result.state = CAP_TIMEOUT;
        Serial.println("[SNIF] Ничего не поймано");
    }
    finishCapture();
}

// Вернуть радио в пакетный режим после раундов/захвата
void CC1101Radio::finishCapture() {
    configurePacketMode();
    rf.SetRx();
}

// ================== ВОСПРОИЗВЕДЕНИЕ (RMT TX через GDO0 -> CC1101) ==================

// Async TX: CC1101 в TX-режиме читает данные С пина GDO0 (IOCFG0=0x0D в TX — вход данных).
// RMT гонит импульсы на GPIO27 (GDO0) -> CC1101 модулирует ими несущую.
void CC1101Radio::replay(const std::vector<uint16_t>& pulses, uint8_t mod,
                         const std::vector<uint8_t>& levels) {
    if (pulses.size() < 2) return;

    // Уровни для нормализации фазы
    std::vector<uint8_t> startLevels = levels;
    if (startLevels.empty()) {
        startLevels.resize(pulses.size());
        for (size_t i = 0; i < startLevels.size(); i++) {
            startLevels[i] = (i % 2 == 0) ? 1 : 0;
        }
    }

    // TX-конфигурация: PKTCTRL0=0x32 (setCCMode(false)) = async serial, данные
    // ВХОДЯТ с GDO0. IOCFG0=0x0D: data input в TX.
    rf.setSidle();
    rf.setCCMode(false);
    rf.setModulation(2);            // ASK/OOK
    rf.setDRate(2.5f);
    rf.setPA(10);
    rf.SpiWriteReg(CC1101_IOCFG0, 0x0D);

    // v1.0.15: пин GDO0 — ВЫХОД и LOW до включения несущей. Между rmtDeinit
    // прошлого захвата и rmtInit пин висел Hi-Z (на CYD подтянут вверх) —
    // CC1101 в async-TX читал с него HIGH и молотил несущей до посылки.
    // Приёмник видел первый бит растянутым на миллисекунды и отбрасывал кадр.
    pinMode(CC1101_GDO0, OUTPUT);
    digitalWrite(CC1101_GDO0, LOW);

    rf.SetTx();                     // несущая, ждёт данных с GDO0 (теперь честный LOW)
    delay(2);

    // RMT TX: 1 МГц, 1 тик = 1 мкс. Начинаем ПРЯМО с первого захваченного
    // импульса (он HIGH). До старта несущей нет (OOK без данных = тишина).
    size_t nSymbols = 0;
    size_t startIdx = 0;
    if (!startLevels.empty() && startLevels[0] == 0) {
        startIdx = 1;   // начинаем с HIGH
    }
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
        if (nSymbols >= MAX_PULSES) break;
    }

    rmtInit(CC1101_GDO0, RMT_TX_MODE, RMT_MEM_NUM_BLOCKS_2, 1000000);
    rmtSetEOT(CC1101_GDO0, 0);   // после конца — LOW (нет несущей)

    // ОДНА отправка: у шлагбаума каждая посылка — тумблер (старт/стоп)
    if (!rmtWrite(CC1101_GDO0, rmtSymbols, nSymbols, RMT_WAIT_FOR_EVER)) {
        Serial.println("[REPLAY] ОШИБКА: RMT write не прошёл");
    }

    // v1.0.15: порядок демонтажа — СНАЧАЛА гасим несущую (setSidle), ПОТОМ
    // отпускаем пин. Раньше rmtDeinit шёл до setSidle: пин отцеплялся, снова
    // висел Hi-Z, CC1101 ещё был в TX и успевал дать хвостовой выброс несущей.
    rf.setSidle();
    rmtDeinit(CC1101_GDO0);
    pinMode(CC1101_GDO0, INPUT);   // отдаём пин CC1101: в RX он сам его водитель
    Serial.println("[REPLAY] Отправлено x1");

    configurePacketMode();
    rf.SetRx();
}