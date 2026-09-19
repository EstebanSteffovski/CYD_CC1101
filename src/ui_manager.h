#pragma once
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include "U8g2_for_TFT_eSPI.h"
#include "config.h"

// ============ UI СНИФЕРА — MATRIX THEME ============
// Чёрный фон, зелёная матрица. Один экран-меню: 8 слотов + SCAN.
// Скан не уводит на другой экран: кнопка SCAN переливается, светодиод
// мигает синим 1 Гц, при захвате — переход на экран слотов (Save data).

// Цвета матрицы
#define COL_BG      TFT_BLACK
#define COL_MATRIX  TFT_GREEN        // 0x07E0 — классика «Матрицы»
#define COL_DIM     0x03E0           // тёмно-зелёный
#define COL_BRIGHT  0xBFE7           // белёсо-зелёный для акцентов
#define COL_BLUE    0x001F           // синий (LED/акценты)
#define COL_ERR     0xF800           // красный (стирание)

// Экраны
#define SCREEN_MAIN    0
#define SCREEN_SLOTS   1
#define SCREEN_PRESETS 2

// Превью импульсов (для отладки в Serial — на экране не рисуем)
#define UI_MAX_PULSES_PREVIEW 80

// Есть ли несохранённый свежий захват (определяет main, живёт в main.cpp)
bool freshSignalValid();

class UIManager {
public:
    void setup();
    void loop();

    // Результат скана (заполняет main)
    bool scanSuccess = false;
    int scanPulses = 0;
    int scanRssi = -999;
    // Декод протокола (для Serial-лога)
    bool protoValid = false;
    char protoName[24] = {0};
    char protoCode[20] = {0};

    // Состояние сканера — управляет main
    bool scanning = false;            // true: кнопка SCAN переливается, LED мигает

    // Частота (отображение внизу)
    float displayFreq = 433.92f;

    // Слоты-пресеты
    bool hasStored[SLOT_COUNT] = {};

    // Действия от тача (обрабатывает main)
    bool actionToggleScan = false;    // тап по SCAN: старт/стоп скана
    bool actionPlaySlot = false;
    int  playSlot = -1;
    bool actionEditSlot = false;
    int  editSlot = -1;

    void gotoMain();
    void gotoSlots();
    void gotoPresets();
    void redrawRequest() { redraw = true; }
    // Мы на экране Save data? (main различает «играть» и «записать» по экрану)
    bool currentScreenIsSlots() const { return currentScreen == SCREEN_SLOTS; }
    // Прерывание скана: скан идёт, только пока мы на главном экране
    bool scanAborted() const { return currentScreen != SCREEN_MAIN; }

private:
    TFT_eSPI tft;
    U8g2_for_TFT_eSPI u8g2;
    XPT2046_Touchscreen* touch = nullptr;

    uint8_t currentScreen = SCREEN_MAIN;
    bool redraw = true;
    uint32_t lastTouch = 0;

    void drawLogo();
    void drawMain();
    void drawSlots();
    void drawPresets();
    void drawTitle();
    void drawSlotButton(int i, bool pressed);
    void drawButton(int x, int y, int w, int h, const char* label,
                    uint16_t border, uint16_t text, bool filled = false,
                    uint16_t fill = COL_BG);
    void handleTouch(int16_t tx, int16_t ty);
};

extern UIManager ui;