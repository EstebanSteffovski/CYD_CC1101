#pragma once
#include <TFT_eSPI.h>
#include <XPT2046_Touchscreen.h>
#include <SPI.h>
#include "U8g2_for_TFT_eSPI.h"

// ============ UI ПРОТОТИПА СНИФЕРА ШЛАГБАУМА ============
// Кириллица через U8g2 (u8g2_font_10x20_t_cyrillic, u8g2_font_6x12_t_cyrillic)
// Слои: меню из 3 кнопок, экран снифера, экран результата (сохранить/воспроизвести)

// Цвета
#define COL_BG      TFT_BLACK
#define COL_ACCENT  TFT_CYAN
#define COL_OK      TFT_GREEN
#define COL_WARN    TFT_ORANGE
#define COL_ERR     TFT_RED
#define COL_TEXT    TFT_WHITE
#define COL_DIM     0x528A      // серый

// Экраны
#define SCREEN_MENU   0
#define SCREEN_SNIFF  1
#define SCREEN_RESULT 2

// Данные последнего захвата для мини-осциллограммы (заполняет main)
#define UI_MAX_PULSES_PREVIEW 80

class UIManager {
public:
    void setup();
    void loop();

    // Результат снифа для отображения (заполняется из main)
    bool sniffSuccess = false;      // что-то поймали?
    int sniffPulses = 0;            // сколько импульсов
    int sniffRssi = -999;
    float sniffFreq = 433.92f;
    // Превью импульсов для осциллограммы
    uint16_t previewPulses[UI_MAX_PULSES_PREVIEW];
    int previewCount = 0;
    // Статус сохранения/воспроизведения (мс до скрытия, 0 = скрыть)
    uint32_t saveFlashUntil = 0;
    uint32_t replayFlashUntil = 0;
    // Частота для отображения в меню
    float displayFreq = 433.92f;
    // Есть ли сохранённый сигнал в памяти (для подписи кнопок)
    bool hasStored = false;

    // Действия, запрошенные тачем (обрабатываются в main)
    bool actionSniff = false;
    bool actionSave = false;
    bool actionReplay = false;

    void gotoResult();   // main вызывает после завершения захвата
    void gotoMenu();

private:
    TFT_eSPI tft;
    U8g2_for_TFT_eSPI u8g2;
    XPT2046_Touchscreen* touch = nullptr;

    uint8_t currentScreen = SCREEN_MENU;
    bool redraw = true;
    uint32_t lastTouch = 0;
    uint32_t sniffStartTime = 0;

    void drawMenu();
    void drawSniff();
    void drawResult();
    void drawButton(int x, int y, int w, int h, const char* label, uint16_t color);
    void handleTouch(int16_t tx, int16_t ty);
};

extern UIManager ui;