#include "ui_manager.h"
#include "signal_store.h"
#include "radio_driver.h"
#include "logo.h"
#include <Preferences.h>

UIManager ui;

// ============ ГЕОМЕТРИЯ ============
// Главный: заголовок Gate Pirate + 8 слотов (2x4) + SCAN + PRESETS
#define PR_W        106
#define PR_H        38
#define PR_X0       8
#define PR_X1       126
#define PR_Y0       44            // слоты ниже — под заголовком (п.2)
#define PR_YGAP     5
#define SCAN_Y      262
#define SCAN_H      24
#define SCAN_W      224
#define SCAN_X      8
#define PRESET_Y    292
#define PRESET_H    24

// Слоты (Save data): 8 рядов + Exit
#define PS_H        25
#define PS_GAP      3
#define PS_Y0       36
#define EXIT_Y      288
#define EXIT_H      26

// Пресеты (редактирование): 8 рядов + Exit
#define EDT_H       25
#define EDT_GAP     3
#define EDT_Y0      36

// ============ ЛОГОТИП БРАТИКОВ (200x200, зелёный) ============

void UIManager::drawLogo() {
    tft.fillScreen(COL_BG);
    tft.pushImage(20, 60, LOGO_WIDTH, LOGO_HEIGHT, logo_data);
    delay(2500);
}

// ============ ПРИМИТИВЫ ============

void UIManager::drawButton(int x, int y, int w, int h, const char* label,
                           uint16_t border, uint16_t text, bool filled, uint16_t fill) {
    if (filled) tft.fillRoundRect(x, y, w, h, 6, fill);
    tft.drawRoundRect(x, y, w, h, 6, border);
    u8g2.setFont(u8g2_font_6x12_t_cyrillic);
    u8g2.setForegroundColor(text);
    u8g2.setBackgroundColor(filled ? fill : COL_BG);
    u8g2.setCursor(x + w / 2 - u8g2.getUTF8Width(label) / 2, y + h / 2 + 4);
    u8g2.print(label);
}

// Заголовок Gate Pirate — матрично-зелёный, по центру
void UIManager::drawTitle() {
    u8g2.setFont(u8g2_font_10x20_t_cyrillic);
    u8g2.setForegroundColor(COL_MATRIX);
    u8g2.setBackgroundColor(COL_BG);
    u8g2.setCursor(120 - u8g2.getUTF8Width("Gate Pirate") / 2, 26);
    u8g2.print("Gate Pirate");
    tft.drawFastHLine(0, 34, 240, COL_DIM);
}

// Кнопка слота на главном: БЕЗ номера — «занят/пусто» + импульсы
void UIManager::drawSlotButton(int i, bool pressed) {
    int col = i % 2, row = i / 2;
    int x = col ? PR_X1 : PR_X0;
    int y = PR_Y0 + row * (PR_H + PR_YGAP);
    char label[20];

    if (hasStored[i]) {
        StoredSignal s;
        if (store.load(i, s)) {
            snprintf(label, sizeof(label), "%u imp.", (unsigned)s.count);
        } else {
            label[0] = 0;
        }
    } else {
        snprintf(label, sizeof(label), "---");
    }

    if (pressed) {
        tft.fillRoundRect(x, y, PR_W, PR_H, 6, COL_MATRIX);
        tft.drawRoundRect(x, y, PR_W, PR_H, 6, COL_BRIGHT);
        u8g2.setFont(u8g2_font_6x12_t_cyrillic);
        u8g2.setForegroundColor(COL_BG);
        u8g2.setBackgroundColor(COL_MATRIX);
    } else {
        tft.fillRoundRect(x, y, PR_W, PR_H, 6, COL_BG);
        tft.drawRoundRect(x, y, PR_W, PR_H, 6,
                          hasStored[i] ? COL_MATRIX : COL_DIM);
        u8g2.setFont(u8g2_font_6x12_t_cyrillic);
        u8g2.setForegroundColor(hasStored[i] ? COL_BRIGHT : COL_DIM);
        u8g2.setBackgroundColor(COL_BG);
    }
    u8g2.setCursor(x + PR_W / 2 - u8g2.getUTF8Width(label) / 2, y + PR_H / 2 + 4);
    u8g2.print(label);
}

// ============ ГЛАВНЫЙ ЭКРАН: 8 СЛОТОВ + SCAN + PRESETS ============

void UIManager::drawMain() {
    tft.fillScreen(COL_BG);
    drawTitle();   // Gate Pirate сверху (п.2)

    for (int i = 0; i < SLOT_COUNT; i++) {
        drawSlotButton(i, false);
    }

    drawButton(SCAN_X, SCAN_Y, SCAN_W, SCAN_H,
               ui.scanning ? "SCAN... (tap = stop)" : "SCAN",
               COL_MATRIX, ui.scanning ? COL_MATRIX : COL_BRIGHT,
               ui.scanning, COL_BG);

    // PRESETS — редактирование пресетов (п.3)
    drawButton(SCAN_X, PRESET_Y, SCAN_W, PRESET_H, "PRESETS", COL_DIM, COL_BRIGHT);
}

// ============ ЭКРАН СЛОТОВ (Save data) ============

void UIManager::drawSlots() {
    tft.fillScreen(COL_BG);
    u8g2.setFont(u8g2_font_10x20_t_cyrillic);
    u8g2.setForegroundColor(COL_MATRIX);
    u8g2.setBackgroundColor(COL_BG);
    u8g2.setCursor(120 - u8g2.getUTF8Width("Save data") / 2, 26);
    u8g2.print("Save data");
    tft.drawFastHLine(0, 34, 240, COL_DIM);

    for (int i = 0; i < SLOT_COUNT; i++) {
        int y = PS_Y0 + i * (PS_H + PS_GAP);
        char label[28];
        if (hasStored[i]) {
            StoredSignal s;
            if (store.load(i, s)) {
                snprintf(label, sizeof(label), "%u imp.  %.2fMHz",
                         (unsigned)s.count, s.freqMHz);
            } else {
                label[0] = 0;
            }
        } else {
            snprintf(label, sizeof(label), "---");
        }
        // Слот — как в PRESETS: строка слева
        drawButton(8, y, 160, PS_H, label,
                   hasStored[i] ? COL_MATRIX : COL_DIM,
                   hasStored[i] ? COL_BRIGHT : COL_DIM);
        // Правая кнопка: при живом захвате «SAVE» (синяя), иначе пустая рамка
        if (ui.scanSuccess && freshSignalValid()) {
            drawButton(174, y, 58, PS_H, "SAVE", COL_BLUE, COL_BLUE);
        } else {
            drawButton(174, y, 58, PS_H, "", COL_DIM, COL_DIM);
        }
    }

    drawButton(8, EXIT_Y, 224, EXIT_H, "Exit", COL_MATRIX, COL_BRIGHT);
}

// ============ ЭКРАН ПРЕСЕТОВ (редактирование) ============
// Каждая строка: тап = играть, правая кнопка X = стереть. Exit внизу.

void UIManager::drawPresets() {
    tft.fillScreen(COL_BG);
    u8g2.setFont(u8g2_font_10x20_t_cyrillic);
    u8g2.setForegroundColor(COL_MATRIX);
    u8g2.setBackgroundColor(COL_BG);
    u8g2.setCursor(120 - u8g2.getUTF8Width("PRESETS") / 2, 26);
    u8g2.print("PRESETS");
    tft.drawFastHLine(0, 34, 240, COL_DIM);

    for (int i = 0; i < SLOT_COUNT; i++) {
        int y = EDT_Y0 + i * (EDT_H + EDT_GAP);
        char label[28];
        if (hasStored[i]) {
            StoredSignal s;
            if (store.load(i, s)) {
                snprintf(label, sizeof(label), "%u imp.  %.2fMHz",
                         (unsigned)s.count, s.freqMHz);
            } else {
                label[0] = 0;
            }
        } else {
            snprintf(label, sizeof(label), "---");
        }
        drawButton(8, y, 160, EDT_H, label,
                   hasStored[i] ? COL_MATRIX : COL_DIM,
                   hasStored[i] ? COL_BRIGHT : COL_DIM);
        // X — очистка слота (красная)
        drawButton(174, y, 58, EDT_H, "X", COL_ERR, COL_ERR);
    }

    drawButton(8, EXIT_Y, 224, EXIT_H, "Exit", COL_MATRIX, COL_BRIGHT);
}

// ============ НАВИГАЦИЯ ============

void UIManager::gotoMain()    { currentScreen = SCREEN_MAIN;    redraw = true; }
void UIManager::gotoSlots()   { currentScreen = SCREEN_SLOTS;   redraw = true; }
void UIManager::gotoPresets() { currentScreen = SCREEN_PRESETS; redraw = true; }

// ============ ТАЧ ============

void UIManager::handleTouch(int16_t tx, int16_t ty) {
    switch (currentScreen) {
    case SCREEN_MAIN:
        for (int i = 0; i < SLOT_COUNT; i++) {
            int col = i % 2, row = i / 2;
            int x = col ? PR_X1 : PR_X0;
            int y = PR_Y0 + row * (PR_H + PR_YGAP);
            if (tx >= x && tx <= x + PR_W && ty >= y && ty <= y + PR_H) {
                if (hasStored[i]) {
                    drawSlotButton(i, true);
                    delay(150);
                    drawSlotButton(i, false);
                    playSlot = i;
                    actionPlaySlot = true;
                }
            }
        }
        // SCAN: старт/стоп сканера
        if (tx >= SCAN_X && tx <= SCAN_X + SCAN_W && ty >= SCAN_Y && ty <= SCAN_Y + SCAN_H) {
            actionToggleScan = true;
        }
        // PRESETS: на страницу редактирования
        else if (tx >= SCAN_X && tx <= SCAN_X + SCAN_W && ty >= PRESET_Y && ty <= PRESET_Y + PRESET_H) {
            gotoPresets();
        }
        break;

    case SCREEN_SLOTS:
        // Правая кнопка «SAVE» при живом захвате = записать в этот слот.
        // Тап по строке слота тут не играет — воспроизведение только с главного.
        for (int i = 0; i < SLOT_COUNT; i++) {
            int y = PS_Y0 + i * (PS_H + PS_GAP);
            if (ty >= y && ty <= y + PS_H) {
                if (tx >= 174 && tx <= 232 && ui.scanSuccess && freshSignalValid()) {
                    playSlot = i;
                    actionPlaySlot = true;   // main решит: save (свободный/любой) при захвате
                }
            }
        }
        if (tx >= 8 && tx <= 232 && ty >= EXIT_Y && ty <= EXIT_Y + EXIT_H) {
            gotoMain();
        }
        break;

    case SCREEN_PRESETS:
        for (int i = 0; i < SLOT_COUNT; i++) {
            int y = EDT_Y0 + i * (EDT_H + EDT_GAP);
            if (ty >= y && ty <= y + EDT_H) {
                if (tx >= 174 && tx <= 232) {
                    // X = стереть слот. Тап по строке тут ничего не играет —
                    // воспроизведение только с главного экрана.
                    editSlot = i;
                    actionEditSlot = true;
                }
            }
        }
        if (tx >= 8 && tx <= 232 && ty >= EXIT_Y && ty <= EXIT_Y + EXIT_H) {
            gotoMain();
        }
        break;
    }
}

void UIManager::setup() {
    tft.begin();
    tft.setRotation(0);
    tft.fillScreen(COL_BG);
    u8g2.begin(tft);
    tft.setSwapBytes(true);   // pushImage: flash little-endian -> панель ждёт big-endian

    touch = new XPT2046_Touchscreen(33, 36);
    touch->begin(SPI);
    touch->setRotation(0);

    drawLogo();
    drawMain();
}

void UIManager::loop() {
    if (redraw) {
        switch (currentScreen) {
        case SCREEN_MAIN:    drawMain();    break;
        case SCREEN_SLOTS:   drawSlots();   break;
        case SCREEN_PRESETS: drawPresets(); break;
        }
        redraw = false;
    }

    if (currentScreen == SCREEN_MAIN) {
        // Переливание кнопки SCAN в режиме скана + синий LED 1 Гц
        static uint32_t lastAnim = 0;
        static bool ledState = false;
        if (millis() - lastAnim >= 500) {
            lastAnim = millis();
            ledState = !ledState;

            // Синий LED CYD: GPIO16, активный HIGH
            pinMode(16, OUTPUT);
            digitalWrite(16, ledState ? HIGH : LOW);

            if (ui.scanning) {
                static uint8_t phase = 0;
                uint16_t fills[3] = {COL_DIM, COL_MATRIX, COL_BRIGHT};
                drawButton(SCAN_X, SCAN_Y, SCAN_W, SCAN_H, "SCAN... (tap = stop)",
                           COL_MATRIX, COL_BG, true, fills[phase]);
                phase = (phase + 1) % 3;
            } else {
                digitalWrite(16, LOW);   // LED погашен вне скана
                drawButton(SCAN_X, SCAN_Y, SCAN_W, SCAN_H, "SCAN",
                           COL_MATRIX, COL_BRIGHT);
            }
        }
    }

    // Тач
    if (touch && touch->tirqTouched() && touch->touched()) {
        if (millis() - lastTouch < 250) return;
        lastTouch = millis();
        TS_Point p = touch->getPoint();
        int16_t tx = map(p.x, 200, 3700, 0, 240);
        int16_t ty = map(p.y, 240, 3800, 0, 320);
        if (tx < 0) tx = 0;
        if (tx > 239) tx = 239;
        if (ty < 0) ty = 0;
        if (ty > 319) ty = 319;
        handleTouch(tx, ty);
    }
}