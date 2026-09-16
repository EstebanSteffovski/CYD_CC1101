// ============================================================
// ПРОТОТИП: СНИФЕР ШЛАГБАУМА НА CYD + CC1101 (433 МГц)
// v1.0.0
// ============================================================
// 3 кнопки на экране:
//   1. Снифер     — режим захвата сигнала пульта шлагбаума
//   2. Записать   — сохранить последний захват в NVS
//   3. Воспроизв. — переиграть сохранённый сигнал
// ============================================================

#include <Arduino.h>
#include <SmartRC_CC1101.h>
#include "config.h"
#include "radio_driver.h"
#include "signal_store.h"
#include "ui_manager.h"

#define FW_VERSION "v1.0.8"

// Глобальный объект радио-библиотеки (используется в radio_driver.cpp)
SmartRC_CC1101& rf = ELECHOUSE_cc1101;

// Последний захват (живёт в RAM до записи в NVS)
static CaptureResult lastCapture;
static StoredSignal storedSignal;

// ================== ДЕЙСТВИЯ UI ==================

void doSniff() {
    Serial.println("[MAIN] Режим снифера: жду сигнал...");
    ui.sniffSuccess = false;
    ui.sniffPulses = 0;
    ui.sniffRssi = -999;
    ui.sniffFreq = radio.getFrequency();

    radio.spiToRadio();                          // шина VSPI -> CC1101
    radio.captureAsync(lastCapture, CAPTURE_TIMEOUT_MS);
    radio.spiToTouch();                          // шина VSPI -> тач

    ui.sniffSuccess = (lastCapture.state == CAP_DONE && lastCapture.pulses.size() >= 8);
    ui.sniffPulses = lastCapture.pulses.size();
    ui.sniffRssi = lastCapture.rssi;
    ui.sniffFreq = lastCapture.freqMHz;

    if (ui.sniffSuccess) {
        // Превью для осциллограммы
        ui.previewCount = (ui.sniffPulses < UI_MAX_PULSES_PREVIEW) ? ui.sniffPulses : UI_MAX_PULSES_PREVIEW;
        for (int i = 0; i < ui.previewCount; i++) {
            ui.previewPulses[i] = lastCapture.pulses[i];
        }
        // Переносим в storedSignal для записи (вместе с уровнями)
        storedSignal.valid = true;
        storedSignal.freqMHz = lastCapture.freqMHz;
        storedSignal.modulation = 2;   // OOK
        storedSignal.count = lastCapture.pulses.size();
        for (size_t i = 0; i < storedSignal.count && i < 400; i++) {
            storedSignal.pulses[i] = lastCapture.pulses[i];
            if (i < lastCapture.levels.size()) {
                storedSignal.levels[i] = lastCapture.levels[i];
            } else {
                storedSignal.levels[i] = (i % 2 == 0) ? 1 : 0;  // fallback
            }
        }
        Serial.printf("[MAIN] Захват ОК: %u имп., RSSI=%d\r\n", ui.sniffPulses, ui.sniffRssi);
    } else {
        Serial.println("[MAIN] Захвата нет");
    }

    ui.gotoResult();
}

void doSave() {
    if (!storedSignal.valid) {
        Serial.println("[MAIN] Нечего сохранять — сначала снифер");
        return;
    }
    if (store.save(storedSignal)) {
        ui.saveFlashUntil = millis() + 2000;
        ui.hasStored = true;
        Serial.println("[MAIN] Сигнал сохранён в память");
    }
}

void doReplay() {
    StoredSignal s;
    if (!store.load(s) || !s.valid || s.count == 0) {
        Serial.println("[MAIN] Нет сохранённого сигнала");
        return;
    }
    Serial.printf("[MAIN] Воспроизведение: %.2f МГц, %u имп.\r\n", s.freqMHz, s.count);

    std::vector<uint16_t> pulses(s.pulses, s.pulses + s.count);
    std::vector<uint8_t> levels(s.levels, s.levels + s.count);
    radio.spiToRadio();                          // шина VSPI -> CC1101
    radio.setFrequency(s.freqMHz);   // перед воспроизведением выставляем частоту
    radio.replay(pulses, s.modulation, levels);
    radio.spiToTouch();                          // шина VSPI -> тач

    ui.replayFlashUntil = millis() + 2000;
}

// ================== SETUP / LOOP ==================

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\r\n=== СНИФЕР ШЛАГБАУМА CYD+CC1101 %s ===\r\n", FW_VERSION);

    // РАДИО ПЕРВЫМ: оно первым берёт VSPI-шину (тач подключится позже,
    // его begin(SPI) не сбрасывает пины — мультиплексор остаётся на радио
    // до первого spiToTouch).
    radio.begin();          // SPI на 18/19/23 (SD-слот), CS=22, GDO0=27

    // Диагностика: читаем статусные регистры напрямую
    uint8_t partnum = rf.SpiReadStatus(CC1101_PARTNUM);
    uint8_t version = rf.SpiReadStatus(CC1101_VERSION);
    Serial.printf("[MAIN] CC1101 PARTNUM=0x%02X VERSION=0x%02X (ожидаются 0x00/0x14)\r\n",
                  partnum, version);

    ui.setup();             // тач поверх шины (SPI тача = тот же объект)
    radio.spiToTouch();     // отдаём шину тачу для меню
    // Радио уже проверено выше (PARTNUM/VERSION до подключения тача).
    // Повторная проверка здесь не имеет смысла: шина сейчас на таче,
    // detected() читал бы тач и всегда отвечал бы "НЕ НАЙДЕН".
    Serial.println("[MAIN] CC1101: ОК (радио инициализировано)");

    store.begin();

    // Если есть сохранённый сигнал — выставим его частоту
    StoredSignal s;
    if (store.load(s) && s.valid) {
        radio.spiToRadio();
        radio.setFrequency(s.freqMHz);
        radio.spiToTouch();
        ui.displayFreq = s.freqMHz;
        ui.hasStored = true;
    }
}

void loop() {
    ui.loop();

    // Действия от тача
    if (ui.actionSniff) {
        ui.actionSniff = false;
        doSniff();
    }
    if (ui.actionSave) {
        ui.actionSave = false;
        doSave();
    }
    if (ui.actionReplay) {
        ui.actionReplay = false;
        doReplay();
    }
}