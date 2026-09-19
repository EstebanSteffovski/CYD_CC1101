// ============================================================
// СНИФЕР ШЛАГБАУМА CYD + CC1101 (433 МГц)
// v2.2.0: matrix-UI, лого братиков, 8 слотов, скан без смены экрана
// ============================================================
// Главный экран: 8 слотов (тап = играть) + кнопка SCAN внизу.
// SCAN: не уводит с экрана — кнопка переливается, синий LED мигает 1 Гц.
// При захвате — авто-переход на «Save data», где свободные слоты
// подсвечены синим (тап = записать), занятые играют по тапу. Exit — в меню.
// Повторный тап SCAN останавливает скан.

#include <Arduino.h>
#include <SmartRC_CC1101.h>
#include "config.h"
#include "radio_driver.h"
#include "signal_store.h"
#include "ui_manager.h"
#include "protocol.h"
#include "prefs_helpers.h"

#define FW_VERSION "v2.2.8"

// Глобальный объект радио-библиотеки (используется в radio_driver.cpp)
SmartRC_CC1101& rf = ELECHOUSE_cc1101;

// Последний захват (живёт в RAM до записи в слот)
static CaptureResult lastCapture;
static StoredSignal freshSignal;   // свежий захват, ждёт записи

// ============ LED ============
// Встроенный LED CYD (ESP32-2432S028R): синий = GPIO16, активный HIGH.
// GPIO2 НЕ трогаем — это TFT_DC (шина дисплея), мигать им нельзя.
#define LED_BLUE  16

static void ledBlue(bool on) {
    pinMode(LED_BLUE, OUTPUT);
    digitalWrite(LED_BLUE, on ? HIGH : LOW);
}

// ================== СКАН ==================
// Неблокирующий скан: раунды по 250 мс. Живёт, пока ui.scanning и мы на главном.
// Таймаута нет: остановка — только повторным тапом по SCAN.

static void scanTick() {
    static bool inRound = false;

    if (!ui.scanning || ui.scanAborted()) {
        if (inRound) {
            // Скан остановлен (тап или уход с экрана) — гасим радио-приём
            radio.finishCapture();
            radio.spiToTouch();
            inRound = false;
            ledBlue(false);
            Serial.println("[SCAN] Остановлен");
        }
        return;
    }

    radio.spiToRadio();
    bool caught = radio.captureRound(lastCapture, CAPTURE_ROUND_MS);
    radio.spiToTouch();

    if (caught) {
        // Захват! Останавливаем скан, готовим freshSignal, идём на Save data
        ui.scanning = false;
        ledBlue(false);
        radio.finishCapture();

        ui.scanSuccess = lastCapture.pulses.size() >= 8;
        ui.scanPulses = lastCapture.pulses.size();
        ui.scanRssi = lastCapture.rssi;
        Serial.printf("[SCAN] Захват: %u имп., RSSI=%d\r\n",
                      ui.scanPulses, ui.scanRssi);

        if (ui.scanSuccess) {
            freshSignal.valid = true;
            freshSignal.freqMHz = radio.getFrequency();
            freshSignal.modulation = 2;   // OOK
            freshSignal.count = lastCapture.pulses.size();
            for (size_t i = 0; i < freshSignal.count && i < 400; i++) {
                freshSignal.pulses[i] = lastCapture.pulses[i];
                if (i < lastCapture.levels.size()) {
                    freshSignal.levels[i] = lastCapture.levels[i];
                } else {
                    freshSignal.levels[i] = (i % 2 == 0) ? 1 : 0;
                }
            }
            // Декод протокола — в Serial и в UI-поля
            ProtocolInfo p = decodeProtocol(lastCapture.pulses, lastCapture.levels);
            ui.protoValid = p.valid;
            strncpy(ui.protoName, p.name, sizeof(ui.protoName) - 1);
            formatCode(p, ui.protoCode, sizeof(ui.protoCode));
            Serial.printf("[SCAN] %s код=%s TE=%d мкс\r\n",
                          p.valid ? p.name : "RAW",
                          ui.protoCode, p.teUs);
            ui.gotoSlots();   // при захвате — на экран слотов
        }
    }
}

// ================== ДЕЙСТВИЯ ==================

void doPlaySlot(int slot) {
    if (slot < 0 || slot >= SLOT_COUNT) return;
    StoredSignal s;
    if (!store.load(slot, s)) return;
    Serial.printf("[PLAY] Слот %d: %.2f МГц, %u имп.\r\n",
                  slot + 1, s.freqMHz, (unsigned)s.count);

    // Код посылки — одним блоком, для копипаста
    ProtocolInfo p = decodeProtocol(
        std::vector<uint16_t>(s.pulses, s.pulses + s.count),
        std::vector<uint8_t>(s.levels, s.levels + s.count));
    if (p.valid) {
        char code[20];
        formatCode(p, code, sizeof(code));
        Serial.printf("[CODE] %s  код=%s  TE=%d мкс  бит=%d\r\n",
                      p.name, code, p.teUs, p.bitCount);
    } else {
        Serial.printf("[CODE] RAW  TE=%d мкс\r\n", p.teUs);
    }
    // Полный дамп импульсов H/L — тоже можно копипастить
    Serial.print("[PULSES] ");
    for (size_t i = 0; i < s.count; i++) {
        Serial.printf("%s%u ", (i < s.count && s.levels[i] == 0) ? "L" : "H",
                      (unsigned)s.pulses[i]);
    }
    Serial.println();

    radio.spiToRadio();
    radio.setFrequency(s.freqMHz);
    radio.replay(std::vector<uint16_t>(s.pulses, s.pulses + s.count),
                 s.modulation,
                 std::vector<uint8_t>(s.levels, s.levels + s.count));
    radio.spiToTouch();
}

void doSaveCapture(int slot) {
    if (!freshSignal.valid) return;
    if (slot < 0 || slot >= SLOT_COUNT) return;
    if (store.save(slot, freshSignal)) {
        ui.hasStored[slot] = true;
        freshSignal.valid = false;   // записали — больше не предлагаем
        Serial.printf("[SAVE] Слот %d\r\n", slot + 1);
        ui.redrawRequest();
    }
}

void doClearSlot(int slot) {
    if (slot < 0 || slot >= SLOT_COUNT) return;
    store.clear(slot);
    ui.hasStored[slot] = false;
    Serial.printf("[CLEAR] Слот %d\r\n", slot + 1);
    ui.redrawRequest();
}

// Есть ли несохранённый свежий захват (для UI: показывать кнопки SAVE)
bool freshSignalValid() {
    return freshSignal.valid;
}

// ================== SETUP / LOOP ==================

void setup() {
    Serial.begin(115200);
    delay(300);
    Serial.printf("\r\n=== БРАТИК SNIFFER CYD+CC1101 %s ===\r\n", FW_VERSION);

    radio.begin();

    uint8_t partnum = rf.SpiReadStatus(CC1101_PARTNUM);
    uint8_t version = rf.SpiReadStatus(CC1101_VERSION);
    Serial.printf("[MAIN] CC1101 PARTNUM=0x%02X VERSION=0x%02X\r\n", partnum, version);

    ui.setup();
    radio.spiToTouch();

    store.begin();
    float f = prefsGetCurFreq();
    radio.setFrequency(f);
    ui.displayFreq = f;
    for (int i = 0; i < SLOT_COUNT; i++) {
        ui.hasStored[i] = store.hasSignal(i);
    }
    ledBlue(false);
}

void loop() {
    ui.loop();

    // Тап по SCAN: старт/стоп сканера (экран не меняем)
    if (ui.actionToggleScan) {
        ui.actionToggleScan = false;
        if (ui.scanning) {
            ui.scanning = false;         // повторный тап — стоп, режим обычный
            ledBlue(false);
            Serial.println("[SCAN] Стоп по тапу");
            ui.redrawRequest();
        } else {
            ui.scanning = true;          // старт скана
            ui.scanSuccess = false;
            freshSignal.valid = false;
            Serial.println("[SCAN] Старт");
            ui.redrawRequest();
        }
    }

    // Тап по слоту: с главного — играть; с Save data при захвате — записать
    if (ui.actionPlaySlot) {
        ui.actionPlaySlot = false;
        if (ui.scanSuccess && freshSignal.valid && ui.currentScreenIsSlots()) {
            doSaveCapture(ui.playSlot);   // кнопка SAVE на Save data
        } else {
            doPlaySlot(ui.playSlot);
        }
    }

    // X на странице пресетов: очистить слот
    if (ui.actionEditSlot) {
        ui.actionEditSlot = false;
        doClearSlot(ui.editSlot);
    }

    scanTick();   // неблокирующий скан
}